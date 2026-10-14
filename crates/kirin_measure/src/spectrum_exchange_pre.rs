//! PRE exchange tick with every filesystem operation outside the session lock.

use std::sync::{MutexGuard, TryLockError};
use std::time::Instant;

use super::*;
#[path = "spectrum_exchange_pre_clock.rs"]
mod clock_publication;
#[path = "spectrum_exchange_pre_perceptual.rs"]
mod perceptual_transaction;

impl SpectrumCoordinator {
    /// PRE IO-thread tick. An active exact request may be called at the 30 Hz Analysis cadence.
    pub(crate) fn pre_tick(&self, pre_instance_id: &str, instance_dir: &Path) -> bool {
        self.pre_tick_observed(pre_instance_id, instance_dir, || {})
    }

    fn pre_tick_observed(
        &self,
        pre_instance_id: &str,
        instance_dir: &Path,
        after_read: impl FnOnce(),
    ) -> bool {
        let Ok(previous) = self.pre_request_read_revision.fetch_update(
            Ordering::AcqRel,
            Ordering::Acquire,
            |revision| revision.checked_add(1),
        ) else {
            return false;
        };
        let read_revision = previous + 1;
        let request = validated_request(
            instance_dir,
            pre_instance_id,
            self.sample_rate,
            unix_ms_now(),
        );
        after_read();
        let Some(ValidatedRequest {
            request_id,
            analysis_mode,
            channel_mode,
            state_epoch_samples: requested_epoch,
            attack_band,
        }) = request
        else {
            self.retire_pre_session(read_revision);
            return false;
        };
        if !self.prepare_pre_session(
            read_revision,
            request_id,
            instance_dir,
            analysis_mode,
            channel_mode,
            attack_band,
        ) {
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
        read_revision: u64,
        request_id: Uuid,
        instance_dir: &Path,
        analysis_mode: AnalysisViewMode,
        channel_mode: SpectrumChannelMode,
        attack_band: Option<AttackBand>,
    ) -> bool {
        let mut slot = match self.try_pre_session() {
            Some(slot) => slot,
            None => return false,
        };
        if self.pre_request_read_revision.load(Ordering::Acquire) != read_revision {
            return false;
        }
        // The descriptor's mode and epoch owner cannot change separately from its session.
        if !self.runtime.set_analysis_mode(analysis_mode)
            || !self.runtime.set_channel_mode(channel_mode)
        {
            if !slot.as_ref().is_some_and(|current| current.awaiting_rearm) {
                let _ = slot.take();
            }
            self.disable_analysis_runtimes();
            return false;
        }
        if let Some(runtime) = self.attack_runtime.as_ref() {
            runtime.set_band(attack_band);
        }
        if slot.as_ref().map(|state| state.request_id) != Some(request_id) {
            self.restart_session_runtimes(analysis_mode);
            if !self.set_active_runtime_enabled(analysis_mode, true) {
                if !slot.as_ref().is_some_and(|current| current.awaiting_rearm) {
                    *slot = None;
                }
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
                awaiting_rearm: false,
            });
        }
        // An expired request may leave its rearm tombstone while the runtime is disabled.
        self.set_active_runtime_enabled(analysis_mode, true)
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
        let clock_revision = self.runtime.spectrum_clock_revision();
        let (newest_end, bytes, expected_epoch) = match analysis_mode {
            AnalysisViewMode::Spectrum => {
                if self.runtime.spectrum_clock_kind().is_none() {
                    return self.remove_local_spectrum_publication(request_id, instance_dir);
                }
                let Some((history, Some(clock_kind))) = self.runtime.try_history_with_clock()
                else {
                    return true;
                };
                (
                    history.newest().map(|frame| frame.presentation_end_samples),
                    encode_snapshot(request_id, clock_kind, &history),
                    None,
                )
            }
            AnalysisViewMode::Perceptual => {
                let Some((history, clock_kind)) = self.runtime.try_perceptual_history_with_clock()
                else {
                    return true;
                };
                (
                    history.newest().map(|frame| frame.presentation_end_samples),
                    encode_perceptual_snapshot_with_clock(request_id, &history, clock_kind),
                    self.runtime.perceptual_state_epoch(),
                )
            }
            AnalysisViewMode::Attack => {
                let Some(history) = self
                    .attack_runtime
                    .as_ref()
                    .and_then(|runtime| runtime.try_history())
                else {
                    return true;
                };
                let observations = match self
                    .attack_runtime
                    .as_ref()
                    .map(|runtime| runtime.try_observation_snapshot_result())
                {
                    Some(Ok(observations)) => observations,
                    Some(Err(
                        crate::attack_runtime::snapshot::AttackObservationReadError::Busy,
                    )) => {
                        return true;
                    }
                    _ => {
                        return self
                            .remove_unavailable_attack_publication(request_id, instance_dir);
                    }
                };
                let revision = history.waveform().next_back().map(|_| {
                    let mut hash = sha2::Sha256::new();
                    use sha2::Digest;
                    hash.update(history.revision().to_le_bytes());
                    hash.update(observations.revision.to_le_bytes());
                    i64::from_le_bytes(hash.finalize()[..8].try_into().unwrap())
                });
                (
                    revision,
                    encode_attack_observation_snapshot(request_id, &history, &observations),
                    None,
                )
            }
            AnalysisViewMode::Absolute => return false,
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
            if analysis_mode == AnalysisViewMode::Spectrum
                && self
                    .pre_spectrum_clock_revision
                    .swap(clock_revision, Ordering::AcqRel)
                    != clock_revision
            {
                current.last_written_end = None;
                current.last_write_attempt_end = None;
                current.last_write_attempt_at = None;
            }
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
        if analysis_mode == AnalysisViewMode::Spectrum
            && !self.runtime.spectrum_clock_is_current(clock_revision)
        {
            return false;
        }
        let write_started = Instant::now();
        let write_result = match analysis_mode {
            AnalysisViewMode::Spectrum => write_snapshot(instance_dir, &bytes),
            AnalysisViewMode::Perceptual => write_perceptual_snapshot(instance_dir, &bytes),
            AnalysisViewMode::Attack => write_attack_snapshot(instance_dir, &bytes),
            AnalysisViewMode::Absolute => return false,
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
        let request_is_live = write_started.elapsed() <= PRESENTATION_HOLD
            && self.pre_request_is_live(instance_dir, pre_instance_id, request_id);
        if analysis_mode == AnalysisViewMode::Spectrum
            && !self.runtime.spectrum_clock_is_current(clock_revision)
        {
            // The completed IO wrote facts cloned under retired authority. Remove only this
            // request's result; returning to the same endpoint must perform a fresh write.
            self.remove_retired_spectrum_result(request_id, instance_dir);
            return false;
        }
        if !request_is_live {
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
        if analysis_mode == AnalysisViewMode::Spectrum
            && !self.runtime.spectrum_clock_is_current(clock_revision)
        {
            drop(slot);
            self.remove_retired_spectrum_result(request_id, instance_dir);
            return false;
        }
        current.last_written_end = Some(newest_end);
        self.exchange_worker.record_published_update();
        true
    }

    fn remove_unavailable_attack_publication(&self, request_id: Uuid, instance_dir: &Path) -> bool {
        {
            // Non-RT, bounded state-only critical section. Filesystem work follows guard release.
            let mut slot = match self.pre_session.lock() {
                Ok(slot) => slot,
                Err(poisoned) => poisoned.into_inner(),
            };
            let Some(current) = slot.as_mut().filter(|current| {
                current.request_id == request_id && current.instance_dir == instance_dir
            }) else {
                return false;
            };
            current.last_written_end = None;
            current.last_write_attempt_end = None;
            current.last_write_attempt_at = None;
        }
        // A newer/foreign request's publication is never owned by this invalidated session.
        if super::attack_codec::read_attack_snapshot_request_id(instance_dir) == Some(request_id)
            && read_attack_snapshot(instance_dir)
                .is_some_and(|snapshot| snapshot.request_id == request_id)
        {
            remove_attack_snapshot(instance_dir);
        }
        true
    }

    fn retire_pre_session(&self, read_revision: u64) {
        if let Some(mut slot) = self.try_pre_session() {
            if self.pre_request_read_revision.load(Ordering::Acquire) != read_revision {
                return;
            }
            let has_session = slot.is_some();
            if let Some(current) = slot.as_mut() {
                if self.runtime.analysis_mode() == AnalysisViewMode::Perceptual
                    && self.runtime.take_perceptual_rearm_required()
                {
                    current.awaiting_rearm = true;
                    current.state_epoch_samples = None;
                }
            }
            if let Some(current) = slot.as_mut().filter(|current| current.awaiting_rearm) {
                current.last_ready_written_at = None;
                Self::clear_pre_epoch_publication(current);
            } else {
                let _ = slot.take();
            }
            if has_session {
                self.disable_analysis_runtimes();
                if let Some(runtime) = self.attack_runtime.as_ref() {
                    runtime.set_band(None);
                }
                let _ = self.runtime.set_perceptual_state_epoch(None);
            }
        }
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
        .is_some_and(|current| current.request_id == request_id)
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

#[cfg(test)]
#[path = "spectrum_exchange_pre_tests.rs"]
mod tests;
