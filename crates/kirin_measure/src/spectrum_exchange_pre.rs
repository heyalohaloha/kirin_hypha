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
        let mut session = match self.pre_session.try_lock() {
            Ok(session) => session,
            Err(TryLockError::WouldBlock) => return false,
            Err(TryLockError::Poisoned(poisoned)) => {
                let mut session = poisoned.into_inner();
                *session = None;
                session
            }
        };
        let Some((request_id, analysis_mode, channel_mode, requested_epoch)) = request else {
            if session.take().is_some() {
                let _ = self.runtime.set_enabled(false);
                let _ = fs::remove_file(snapshot_path(instance_dir));
                let _ = fs::remove_file(perceptual_snapshot_path(instance_dir));
                remove_ready(instance_dir);
            }
            return false;
        };
        if !self.runtime.set_analysis_mode(analysis_mode)
            || !self.runtime.set_channel_mode(channel_mode)
        {
            if session.take().is_some() {
                let _ = self.runtime.set_enabled(false);
                let _ = fs::remove_file(snapshot_path(instance_dir));
                let _ = fs::remove_file(perceptual_snapshot_path(instance_dir));
                remove_ready(instance_dir);
            }
            return false;
        }
        if session.as_ref().map(|state| state.request_id) != Some(request_id) {
            let _ = self.runtime.set_enabled(false);
            let _ = fs::remove_file(snapshot_path(instance_dir));
            let _ = fs::remove_file(perceptual_snapshot_path(instance_dir));
            remove_ready(instance_dir);
            if analysis_mode == AnalysisViewMode::Perceptual {
                let _ = self.runtime.set_perceptual_state_epoch(None);
            }
            if !self.runtime.set_enabled(true) {
                return false;
            }
            *session = Some(PreSession {
                request_id,
                last_written_end: None,
                instance_dir: instance_dir.to_path_buf(),
                state_epoch_samples: None,
            });
        }
        if analysis_mode == AnalysisViewMode::Perceptual {
            let state = session.as_mut().expect("PRE session established above");
            let Some(observed_end) = self.runtime.latest_presentation_end() else {
                return true;
            };
            if self.runtime.take_perceptual_rearm_required() {
                let _ = self.runtime.set_perceptual_state_epoch(None);
                state.state_epoch_samples = None;
                state.last_written_end = None;
                let _ = fs::remove_file(perceptual_snapshot_path(instance_dir));
                let ready = AnalysisReady::new(
                    request_id,
                    pre_instance_id,
                    self.sample_rate,
                    observed_end,
                    true,
                    unix_ms_now().saturating_add(REQUEST_LEASE_MS),
                );
                let written = write_ready(instance_dir, &ready).is_ok();
                if written {
                    self.exchange_worker.record_published_update();
                }
                return written;
            }
            let Some(epoch) = requested_epoch else {
                if state.state_epoch_samples.is_some() {
                    let _ = self.runtime.set_perceptual_state_epoch(None);
                    state.state_epoch_samples = None;
                    state.last_written_end = None;
                    let _ = fs::remove_file(perceptual_snapshot_path(instance_dir));
                }
                let ready = AnalysisReady::new(
                    request_id,
                    pre_instance_id,
                    self.sample_rate,
                    observed_end,
                    false,
                    unix_ms_now().saturating_add(REQUEST_LEASE_MS),
                );
                if write_ready(instance_dir, &ready).is_err() {
                    return false;
                }
                self.exchange_worker.record_published_update();
                return true;
            };
            if state.state_epoch_samples != Some(epoch) {
                if observed_end >= epoch || !self.runtime.set_perceptual_state_epoch(Some(epoch)) {
                    let _ = self.runtime.set_perceptual_state_epoch(None);
                    state.state_epoch_samples = None;
                    let ready = AnalysisReady::new(
                        request_id,
                        pre_instance_id,
                        self.sample_rate,
                        observed_end,
                        true,
                        unix_ms_now().saturating_add(REQUEST_LEASE_MS),
                    );
                    let written = write_ready(instance_dir, &ready).is_ok();
                    if written {
                        self.exchange_worker.record_published_update();
                    }
                    return written;
                }
                state.state_epoch_samples = Some(epoch);
                state.last_written_end = None;
                remove_ready(instance_dir);
            }
        }
        let (newest_end, bytes, path) = match analysis_mode {
            AnalysisViewMode::Spectrum => {
                let Some(history) = self.runtime.try_history() else {
                    return true;
                };
                (
                    history.newest().map(|frame| frame.presentation_end_samples),
                    encode_snapshot(request_id, &history),
                    snapshot_path(instance_dir),
                )
            }
            AnalysisViewMode::Perceptual => {
                let Some(history) = self.runtime.try_perceptual_history() else {
                    return true;
                };
                (
                    history.newest().map(|frame| frame.presentation_end_samples),
                    encode_perceptual_snapshot(request_id, &history),
                    perceptual_snapshot_path(instance_dir),
                )
            }
        };
        let state = session.as_mut().expect("PRE session established above");
        if newest_end.is_none() || newest_end == state.last_written_end {
            return true;
        }
        if crate::atomic_file::write_bytes_atomic(&path, &bytes).is_err() {
            return false;
        }
        state.last_written_end = newest_end;
        self.exchange_worker.record_published_update();
        true
    }
}
