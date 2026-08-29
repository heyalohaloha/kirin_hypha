//! POST exchange tick with filesystem operations kept outside the session lock.
//!
//! Windows filter drivers may delay a tiny atomic rename for seconds. The exact session remains
//! available while that operation is pending, allowing the stable 10 Hz IO path to publish the
//! same request and keep the factual PRE/POST exchange alive.

use std::sync::{MutexGuard, TryLockError};
use std::time::Instant;

use super::*;

struct PreparedPostSession {
    session: PostSession,
    renewal: bool,
    retired: Option<(Option<SpectrumTarget>, Uuid)>,
    reset_runtime: bool,
}

impl SpectrumCoordinator {
    /// POST IO-thread tick. `target` comes from the already-confirmed exact pair latch.
    pub(crate) fn post_tick(&self, post_instance_id: &str, target: Option<SpectrumTarget>) -> bool {
        if !self.post_visible() {
            self.retire_hidden_post_session();
            return false;
        }
        match self.ensure_analysis_lease() {
            Ok(true) => {}
            Ok(false) => {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::InUse, None, None);
                return false;
            }
            Err(_) => {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::Unavailable, None, None);
                return false;
            }
        }
        if self.runtime.analysis_mode() == AnalysisViewMode::Absolute {
            return self.post_absolute_tick();
        }
        let Some(target) = target else {
            self.retire_unpaired_post_session();
            return false;
        };

        let analysis_mode = self.runtime.analysis_mode();
        let channel_mode = self.runtime.channel_mode();
        let rearm_required = analysis_mode == AnalysisViewMode::Perceptual
            && self.runtime.take_perceptual_rearm_required();
        let now = Instant::now();
        let Some(prepared) =
            self.prepare_post_session(&target, analysis_mode, channel_mode, rearm_required, now)
        else {
            return false;
        };
        let PreparedPostSession {
            session,
            renewal,
            retired,
            reset_runtime,
        } = prepared;
        if let Some((retired_target, retired_id)) = retired {
            cleanup_owned_request(retired_target.as_ref(), retired_id);
        }
        if reset_runtime {
            let _ = self.runtime.set_enabled(false);
            if analysis_mode == AnalysisViewMode::Perceptual {
                let _ = self.runtime.set_perceptual_state_epoch(None);
            }
        }
        if renewal && !self.publish_post_request(&session, post_instance_id, &target) {
            return false;
        }
        if !self.post_session_is_current(&session) {
            cleanup_owned_request(Some(&target), session.request_id);
            return false;
        }
        if !self.runtime.set_enabled(true) {
            cleanup_owned_request(Some(&target), session.request_id);
            self.clear_post_renewal(session.request_id);
            self.store_view(SpectrumViewStatus::Unavailable, None, None);
            return false;
        }
        if analysis_mode == AnalysisViewMode::Perceptual && session.state_epoch_samples.is_none() {
            return self.arm_perceptual_session(&session, post_instance_id, &target, now);
        }
        self.join_post_view(&session, &target, now)
    }

    fn post_absolute_tick(&self) -> bool {
        let retired = self.try_post_session().map(|mut slot| {
            let replace = slot
                .as_ref()
                .is_none_or(|session| session.analysis_mode != AnalysisViewMode::Absolute);
            if !replace {
                return None;
            }
            let retired = slot
                .take()
                .map(|session| (session.target, session.request_id));
            let mut session = self.new_post_session();
            session.analysis_mode = AnalysisViewMode::Absolute;
            *slot = Some(session);
            retired
        });
        if let Some(Some((target, request_id))) = retired {
            cleanup_owned_request(target.as_ref(), request_id);
        }
        if !self.runtime.set_enabled(true) {
            self.store_absolute_view(SpectrumViewStatus::Unavailable, Default::default());
            return false;
        }
        let Some(history) = self.runtime.try_absolute_history() else {
            return true;
        };
        let status = if history.newest().is_some() {
            SpectrumViewStatus::Active
        } else {
            SpectrumViewStatus::WarmingUp
        };
        self.store_absolute_view(status, history);
        true
    }

    fn prepare_post_session(
        &self,
        target: &SpectrumTarget,
        analysis_mode: AnalysisViewMode,
        channel_mode: SpectrumChannelMode,
        rearm_required: bool,
        now: Instant,
    ) -> Option<PreparedPostSession> {
        let mut slot = self.try_post_session()?;
        let session = slot.get_or_insert_with(|| self.new_post_session());
        let definition_changed = session.target.as_ref() != Some(target)
            || session.analysis_mode != analysis_mode
            || session.channel_mode != channel_mode;
        let mut retired = None;
        if definition_changed || rearm_required {
            retired = Some((session.target.clone(), session.request_id));
            session.request_id = Uuid::new_v4();
            session.target = Some(target.clone());
            session.last_renewed = None;
            session.last_renewal_attempt = None;
            session.started_at = None;
            session.last_presented_at = None;
            session.last_presented_end_samples = None;
            session.analysis_mode = analysis_mode;
            session.channel_mode = channel_mode;
            session.state_epoch_samples = None;
        }
        let renewal_due = session
            .last_renewed
            .is_none_or(|last| now.duration_since(last) >= REQUEST_RENEW_INTERVAL);
        let retry_available = session
            .last_renewal_attempt
            .is_none_or(|attempt| now.duration_since(attempt) >= REQUEST_RENEW_INTERVAL);
        let renewal = renewal_due && retry_available;
        if renewal {
            session.last_renewal_attempt = Some(now);
        }
        Some(PreparedPostSession {
            session: session.clone(),
            renewal,
            retired,
            reset_runtime: definition_changed || rearm_required,
        })
    }

    fn publish_post_request(
        &self,
        session: &PostSession,
        post_instance_id: &str,
        target: &SpectrumTarget,
    ) -> bool {
        let issued_at_unix_ms = unix_ms_now();
        let result = renew_request(
            session,
            post_instance_id,
            target,
            self.sample_rate,
            issued_at_unix_ms,
        );
        let completed_at = Instant::now();
        let completed_at_unix_ms = unix_ms_now();
        let published_live_lease = result.is_ok()
            && completed_at_unix_ms <= issued_at_unix_ms.saturating_add(REQUEST_LEASE_MS);
        let mut slot = match self.post_session.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        let current = slot.as_mut().filter(|current| {
            current.request_id == session.request_id
                && current.target.as_ref() == Some(target)
                && current.analysis_mode == session.analysis_mode
                && current.channel_mode == session.channel_mode
                && current.state_epoch_samples == session.state_epoch_samples
        });
        match (published_live_lease, current) {
            (true, Some(current)) if self.post_visible() => {
                current.last_renewed = Some(completed_at);
                self.exchange_worker.record_published_update();
                true
            }
            (true, _) => {
                drop(slot);
                cleanup_owned_request(Some(target), session.request_id);
                false
            }
            (false, Some(current)) => {
                let factual_lease = current.last_renewed.is_some_and(|renewed| {
                    completed_at.duration_since(renewed) < PRESENTATION_HOLD
                });
                if !factual_lease {
                    self.store_view(SpectrumViewStatus::Unavailable, None, None);
                }
                false
            }
            (false, None) => false,
        }
    }

    fn arm_perceptual_session(
        &self,
        session: &PostSession,
        post_instance_id: &str,
        target: &SpectrumTarget,
        now: Instant,
    ) -> bool {
        self.store_view(SpectrumViewStatus::WarmingUp, None, None);
        let ready = read_ready(&target.instance_dir).filter(|ready| {
            ready.matches(
                session.request_id,
                &target.pre_instance_id,
                self.sample_rate,
                unix_ms_now(),
            )
        });
        let Some(ready) = ready else { return true };
        if ready.rearm_required() {
            let retired = self.rotate_perceptual_request(session.request_id);
            if retired {
                cleanup_owned_request(Some(target), session.request_id);
                let _ = self.runtime.set_perceptual_state_epoch(None);
            }
            return true;
        }
        let Some(local_end) = self.runtime.latest_presentation_end() else {
            return true;
        };
        let aperture = i64::from(self.sample_rate / crate::PERCEPTUAL_PRESENTATION_HZ);
        let Some(epoch) = common_future_epoch(local_end, ready.observed_end(), aperture) else {
            self.store_view(SpectrumViewStatus::Unavailable, None, None);
            return false;
        };
        if !self.runtime.set_perceptual_state_epoch(Some(epoch)) {
            self.store_view(SpectrumViewStatus::Unavailable, None, None);
            return false;
        }
        let armed = {
            let mut slot = match self.post_session.lock() {
                Ok(slot) => slot,
                Err(poisoned) => poisoned.into_inner(),
            };
            let Some(current) = slot
                .as_mut()
                .filter(|current| current.request_id == session.request_id)
            else {
                return false;
            };
            current.state_epoch_samples = Some(epoch);
            current.last_renewed = None;
            current.last_renewal_attempt = Some(now);
            current.started_at = Some(now);
            current.clone()
        };
        self.publish_post_request(&armed, post_instance_id, target)
    }

    fn join_post_view(&self, session: &PostSession, target: &SpectrumTarget, now: Instant) -> bool {
        let spectrum_local = (session.analysis_mode == AnalysisViewMode::Spectrum)
            .then(|| self.runtime.try_history())
            .flatten();
        let perceptual_local = (session.analysis_mode == AnalysisViewMode::Perceptual)
            .then(|| self.runtime.try_perceptual_history())
            .flatten();
        let spectrum_remote = (session.analysis_mode == AnalysisViewMode::Spectrum)
            .then(|| read_snapshot(&target.instance_dir))
            .flatten()
            .filter(|snapshot| snapshot.request_id == session.request_id)
            .map(|snapshot| snapshot.history);
        let perceptual_remote = (session.analysis_mode == AnalysisViewMode::Perceptual)
            .then(|| read_perceptual_snapshot(&target.instance_dir))
            .flatten()
            .filter(|snapshot| snapshot.request_id == session.request_id)
            .map(|snapshot| snapshot.history);
        let mut slot = match self.post_session.try_lock() {
            Ok(slot) => slot,
            Err(TryLockError::WouldBlock) => return false,
            Err(TryLockError::Poisoned(poisoned)) => poisoned.into_inner(),
        };
        let Some(current) = slot.as_mut().filter(|current| {
            self.post_visible()
                && current.request_id == session.request_id
                && current.target.as_ref() == Some(target)
                && current.analysis_mode == session.analysis_mode
                && current.channel_mode == session.channel_mode
                && current.state_epoch_samples == session.state_epoch_samples
        }) else {
            return false;
        };
        if current.started_at.is_none() {
            current.started_at = Some(now);
        }
        match session.analysis_mode {
            AnalysisViewMode::Spectrum => store_joined_spectrum(
                self,
                current,
                now,
                spectrum_local.as_ref(),
                spectrum_remote.as_ref(),
            ),
            AnalysisViewMode::Perceptual => store_joined_perceptual(
                self,
                current,
                now,
                perceptual_local.as_ref(),
                perceptual_remote.as_ref(),
            ),
            AnalysisViewMode::Absolute => {}
        }
        true
    }

    fn post_session_is_current(&self, expected: &PostSession) -> bool {
        let slot = match self.post_session.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        self.post_visible()
            && slot.as_ref().is_some_and(|current| {
                current.request_id == expected.request_id
                    && current.target == expected.target
                    && current.analysis_mode == expected.analysis_mode
                    && current.channel_mode == expected.channel_mode
                    && current.state_epoch_samples == expected.state_epoch_samples
            })
    }

    fn clear_post_renewal(&self, request_id: Uuid) {
        let mut slot = match self.post_session.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        if let Some(current) = slot
            .as_mut()
            .filter(|current| current.request_id == request_id)
        {
            current.last_renewed = None;
            current.last_renewal_attempt = None;
        }
    }

    fn rotate_perceptual_request(&self, request_id: Uuid) -> bool {
        let mut slot = match self.post_session.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        let Some(current) = slot
            .as_mut()
            .filter(|current| current.request_id == request_id)
        else {
            return false;
        };
        current.request_id = Uuid::new_v4();
        current.last_renewed = None;
        current.last_renewal_attempt = None;
        current.state_epoch_samples = None;
        current.started_at = None;
        true
    }

    fn retire_hidden_post_session(&self) {
        let retired = self
            .try_post_session()
            .and_then(|mut slot| slot.take())
            .map(|session| (session.target, session.request_id));
        if let Some((target, request_id)) = retired {
            cleanup_owned_request(target.as_ref(), request_id);
        }
    }

    fn retire_unpaired_post_session(&self) {
        let retired = self.try_post_session().map(|mut slot| {
            let session = slot.get_or_insert_with(|| self.new_post_session());
            let retired = (session.target.take(), session.request_id);
            session.last_renewed = None;
            session.last_renewal_attempt = None;
            session.started_at = None;
            session.last_presented_at = None;
            session.last_presented_end_samples = None;
            retired
        });
        if let Some((target, request_id)) = retired {
            cleanup_owned_request(target.as_ref(), request_id);
        }
        let _ = self.runtime.set_enabled(false);
        self.store_view(SpectrumViewStatus::NoPair, None, None);
    }

    fn try_post_session(&self) -> Option<MutexGuard<'_, Option<PostSession>>> {
        match self.post_session.try_lock() {
            Ok(slot) => Some(slot),
            Err(TryLockError::WouldBlock) => None,
            Err(TryLockError::Poisoned(poisoned)) => {
                let mut slot = poisoned.into_inner();
                *slot = None;
                Some(slot)
            }
        }
    }
}
