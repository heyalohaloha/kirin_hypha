//! PRE exchange tick with every filesystem operation outside the session lock.

use std::sync::{MutexGuard, TryLockError};
use std::time::Instant;

use super::*;

impl SpectrumCoordinator {
    /// PRE IO-thread tick. An active exact request may be called at the 30 Hz Analysis cadence.
    pub(crate) fn pre_tick(&self, pre_instance_id: &str, instance_dir: &Path) -> bool {
        let request = validated_request(
            instance_dir,
            pre_instance_id,
            self.sample_rate,
            unix_ms_now(),
        );
        let Some((request_id, analysis_mode, channel_mode, requested_epoch)) = request else {
            self.retire_pre_session();
            return false;
        };
        if !self.runtime.set_analysis_mode(analysis_mode)
            || !self.runtime.set_channel_mode(channel_mode)
        {
            self.retire_pre_session();
            return false;
        }
        if !self.prepare_pre_session(request_id, instance_dir, analysis_mode) {
            return false;
        }
        if analysis_mode == AnalysisViewMode::Perceptual {
            if let Some(active) = self.service_perceptual_handshake(
                request_id,
                pre_instance_id,
                instance_dir,
                requested_epoch,
            ) {
                return active;
            }
        }
        self.publish_pre_snapshot(request_id, pre_instance_id, analysis_mode, instance_dir)
    }

    fn prepare_pre_session(
        &self,
        request_id: Uuid,
        instance_dir: &Path,
        analysis_mode: AnalysisViewMode,
    ) -> bool {
        let mut slot = match self.try_pre_session() {
            Some(slot) => slot,
            None => return false,
        };
        if slot.as_ref().map(|state| state.request_id) != Some(request_id) {
            let _ = self.runtime.set_enabled(false);
            if analysis_mode == AnalysisViewMode::Perceptual {
                let _ = self.runtime.set_perceptual_state_epoch(None);
            }
            if !self.runtime.set_enabled(true) {
                *slot = None;
                return false;
            }
            *slot = Some(PreSession {
                request_id,
                last_written_end: None,
                last_write_attempt_end: None,
                last_write_attempt_at: None,
                last_ready_written_at: None,
                instance_dir: instance_dir.to_path_buf(),
                state_epoch_samples: None,
            });
        }
        true
    }

    /// `Some` completes the tick in handshake state; `None` continues to payload publication.
    fn service_perceptual_handshake(
        &self,
        request_id: Uuid,
        pre_instance_id: &str,
        instance_dir: &Path,
        requested_epoch: Option<i64>,
    ) -> Option<bool> {
        let observed_end = self.runtime.latest_presentation_end()?;
        let rearm_required = self.runtime.take_perceptual_rearm_required();
        if rearm_required {
            let _ = self.runtime.set_perceptual_state_epoch(None);
            let publish = self.prepare_ready_publication(request_id, None, Instant::now())?;
            return Some(self.publish_pre_ready(
                request_id,
                pre_instance_id,
                instance_dir,
                observed_end,
                true,
                publish,
            ));
        }
        let Some(epoch) = requested_epoch else {
            if self.pre_state_epoch(request_id).is_some() {
                let _ = self.runtime.set_perceptual_state_epoch(None);
            }
            let publish = self.prepare_ready_publication(request_id, None, Instant::now())?;
            return Some(self.publish_pre_ready(
                request_id,
                pre_instance_id,
                instance_dir,
                observed_end,
                false,
                publish,
            ));
        };
        if self.pre_state_epoch(request_id) != Some(epoch) {
            if observed_end >= epoch || !self.runtime.set_perceptual_state_epoch(Some(epoch)) {
                let _ = self.runtime.set_perceptual_state_epoch(None);
                let publish = self.prepare_ready_publication(request_id, None, Instant::now())?;
                return Some(self.publish_pre_ready(
                    request_id,
                    pre_instance_id,
                    instance_dir,
                    observed_end,
                    true,
                    publish,
                ));
            }
            let mut slot = self.try_pre_session()?;
            let current = slot
                .as_mut()
                .filter(|current| current.request_id == request_id)?;
            current.state_epoch_samples = Some(epoch);
            current.last_written_end = None;
            current.last_write_attempt_end = None;
            current.last_write_attempt_at = None;
            current.last_ready_written_at = None;
            return Some(true);
        }
        None
    }

    fn prepare_ready_publication(
        &self,
        request_id: Uuid,
        state_epoch_samples: Option<i64>,
        now: Instant,
    ) -> Option<bool> {
        let mut slot = self.try_pre_session()?;
        let current = slot
            .as_mut()
            .filter(|current| current.request_id == request_id)?;
        current.state_epoch_samples = state_epoch_samples;
        current.last_written_end = None;
        current.last_write_attempt_end = None;
        current.last_write_attempt_at = None;
        let due = current
            .last_ready_written_at
            .is_none_or(|written| now.duration_since(written) >= REQUEST_RENEW_INTERVAL);
        if due {
            current.last_ready_written_at = Some(now);
        }
        Some(due)
    }

    fn publish_pre_ready(
        &self,
        request_id: Uuid,
        pre_instance_id: &str,
        instance_dir: &Path,
        observed_end: i64,
        rearm_required: bool,
        publish: bool,
    ) -> bool {
        if !publish {
            return true;
        }
        let issued_at_unix_ms = unix_ms_now();
        let ready = AnalysisReady::new(
            request_id,
            pre_instance_id,
            self.sample_rate,
            observed_end,
            rearm_required,
            issued_at_unix_ms.saturating_add(REQUEST_LEASE_MS),
        );
        if write_ready(instance_dir, &ready).is_err() {
            return false;
        }
        if unix_ms_now() > issued_at_unix_ms.saturating_add(REQUEST_LEASE_MS) {
            return false;
        }
        if self.pre_session_is_current(request_id, None)
            && self.pre_request_is_live(instance_dir, pre_instance_id, request_id)
        {
            self.exchange_worker.record_published_update();
            true
        } else {
            false
        }
    }

    fn publish_pre_snapshot(
        &self,
        request_id: Uuid,
        pre_instance_id: &str,
        analysis_mode: AnalysisViewMode,
        instance_dir: &Path,
    ) -> bool {
        let (newest_end, bytes, expected_epoch) = match analysis_mode {
            AnalysisViewMode::Spectrum => {
                let Some(history) = self.runtime.try_history() else {
                    return true;
                };
                (
                    history.newest().map(|frame| frame.presentation_end_samples),
                    encode_snapshot(request_id, &history),
                    None,
                )
            }
            AnalysisViewMode::Perceptual => {
                let Some(history) = self.runtime.try_perceptual_history() else {
                    return true;
                };
                (
                    history.newest().map(|frame| frame.presentation_end_samples),
                    encode_perceptual_snapshot(request_id, &history),
                    self.runtime.perceptual_state_epoch(),
                )
            }
        };
        let Some(newest_end) = newest_end else {
            return true;
        };
        let now = Instant::now();
        let publish = {
            let mut slot = match self.try_pre_session() {
                Some(slot) => slot,
                None => return false,
            };
            let Some(current) = slot.as_mut().filter(|current| {
                current.request_id == request_id
                    && current.instance_dir == instance_dir
                    && current.state_epoch_samples == expected_epoch
            }) else {
                return false;
            };
            if current.last_written_end == Some(newest_end) {
                return true;
            }
            let retry_available = current.last_write_attempt_end != Some(newest_end)
                || current
                    .last_write_attempt_at
                    .is_none_or(|attempt| now.duration_since(attempt) >= REQUEST_RENEW_INTERVAL);
            if retry_available {
                current.last_write_attempt_end = Some(newest_end);
                current.last_write_attempt_at = Some(now);
            }
            retry_available
        };
        if !publish {
            return true;
        }
        let write_started = Instant::now();
        let write_result = match analysis_mode {
            AnalysisViewMode::Spectrum => write_snapshot(instance_dir, &bytes),
            AnalysisViewMode::Perceptual => write_perceptual_snapshot(instance_dir, &bytes),
        };
        if write_result.is_err() {
            let mut slot = match self.try_pre_session() {
                Some(slot) => slot,
                None => return false,
            };
            if let Some(current) = slot.as_mut().filter(|current| {
                current.request_id == request_id
                    && current.instance_dir == instance_dir
                    && current.state_epoch_samples == expected_epoch
                    && current.last_write_attempt_end == Some(newest_end)
            }) {
                current.last_write_attempt_end = None;
                current.last_write_attempt_at = None;
            }
            return false;
        }
        if write_started.elapsed() > PRESENTATION_HOLD
            || !self.pre_request_is_live(instance_dir, pre_instance_id, request_id)
        {
            return false;
        }
        let mut slot = match self.try_pre_session() {
            Some(slot) => slot,
            None => return false,
        };
        let Some(current) = slot.as_mut().filter(|current| {
            current.request_id == request_id
                && current.instance_dir == instance_dir
                && current.state_epoch_samples == expected_epoch
        }) else {
            return false;
        };
        current.last_written_end = Some(newest_end);
        self.exchange_worker.record_published_update();
        true
    }

    fn retire_pre_session(&self) {
        let retired = self
            .try_pre_session()
            .and_then(|mut slot| slot.take())
            .is_some();
        if retired {
            let _ = self.runtime.set_enabled(false);
            let _ = self.runtime.set_perceptual_state_epoch(None);
        }
    }

    fn pre_state_epoch(&self, request_id: Uuid) -> Option<i64> {
        let slot = self.try_pre_session()?;
        slot.as_ref()
            .filter(|current| current.request_id == request_id)
            .and_then(|current| current.state_epoch_samples)
    }

    fn pre_session_is_current(&self, request_id: Uuid, epoch: Option<i64>) -> bool {
        let slot = match self.pre_session.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        slot.as_ref().is_some_and(|current| {
            current.request_id == request_id && current.state_epoch_samples == epoch
        })
    }

    fn pre_request_is_live(
        &self,
        instance_dir: &Path,
        pre_instance_id: &str,
        request_id: Uuid,
    ) -> bool {
        validated_request(
            instance_dir,
            pre_instance_id,
            self.sample_rate,
            unix_ms_now(),
        )
        .is_some_and(|(current_id, _, _, _)| current_id == request_id)
    }

    fn try_pre_session(&self) -> Option<MutexGuard<'_, Option<PreSession>>> {
        match self.pre_session.try_lock() {
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
