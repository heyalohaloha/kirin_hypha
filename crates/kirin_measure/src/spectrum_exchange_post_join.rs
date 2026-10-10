//! POST local observation and exact PRE/POST join have separate clock authority.
use super::*;

impl SpectrumCoordinator {
    pub(super) fn join_post_view(
        &self,
        session: &PostSession,
        target: &SpectrumTarget,
        now: Instant,
    ) -> bool {
        self.join_post_view_after_read(session, target, now, || {})
    }

    pub(in crate::spectrum_exchange) fn join_post_view_after_read(
        &self,
        session: &PostSession,
        target: &SpectrumTarget,
        now: Instant,
        after_read: impl FnOnce(),
    ) -> bool {
        let clock_revision = self.runtime.spectrum_clock_revision();
        let spectrum_observation = (session.analysis_mode == AnalysisViewMode::Spectrum)
            .then(|| self.runtime.try_history_with_clock())
            .flatten();
        let spectrum_clock_kind = spectrum_observation
            .as_ref()
            .map_or_else(|| self.runtime.spectrum_clock_kind(), |(_, kind)| *kind);
        let spectrum_local = spectrum_observation.map(|(history, _)| history);
        let perceptual_local = (session.analysis_mode == AnalysisViewMode::Perceptual)
            .then(|| self.runtime.try_perceptual_history())
            .flatten();
        let attack_local = (session.analysis_mode == AnalysisViewMode::Attack)
            .then(|| {
                self.attack_runtime
                    .as_ref()
                    .and_then(|runtime| runtime.try_history())
            })
            .flatten();
        let spectrum_remote = (session.analysis_mode == AnalysisViewMode::Spectrum
            && spectrum_clock_kind.is_some())
        .then(|| read_snapshot(&target.instance_dir))
        .flatten()
        .filter(|snapshot| snapshot.request_id == session.request_id);
        let clock_mismatch = spectrum_remote
            .as_ref()
            .is_some_and(|snapshot| Some(snapshot.clock_kind) != spectrum_clock_kind);
        let spectrum_remote = spectrum_remote
            .filter(|snapshot| Some(snapshot.clock_kind) == spectrum_clock_kind)
            .map(|snapshot| snapshot.history);
        let perceptual_remote = (session.analysis_mode == AnalysisViewMode::Perceptual)
            .then(|| read_perceptual_snapshot(&target.instance_dir))
            .flatten()
            .filter(|snapshot| snapshot.request_id == session.request_id)
            .map(|snapshot| snapshot.history);
        let attack_remote = (session.analysis_mode == AnalysisViewMode::Attack)
            .then(|| read_attack_snapshot(&target.instance_dir))
            .flatten()
            .filter(|snapshot| snapshot.request_id == session.request_id)
            .map(|snapshot| {
                (
                    snapshot.history,
                    snapshot.band_results,
                    snapshot.observations,
                )
            });
        after_read();
        let mut slot = match self.post_session.try_lock() {
            Ok(slot) => slot,
            Err(TryLockError::WouldBlock) => return false,
            Err(TryLockError::Poisoned(poisoned)) => poisoned.into_inner(),
        };
        let Some(current) = slot.as_mut().filter(|current| {
            self.post_visible()
                && current.authority_revision == session.authority_revision
                && current.attack_origin == session.attack_origin
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
        if session.analysis_mode == AnalysisViewMode::Spectrum
            && (!self.runtime.spectrum_clock_is_current(clock_revision)
                || self.post_spectrum_clock_revision.load(Ordering::Acquire) != clock_revision)
        {
            current.last_presented_at = None;
            current.last_presented_end_samples = None;
            self.reset_spectrum_clock_view(self.runtime.spectrum_clock_revision());
            if !self.runtime.spectrum_clock_is_current(clock_revision) {
                return false;
            }
        }
        if session.analysis_mode == AnalysisViewMode::Spectrum
            && (spectrum_clock_kind.is_none() || clock_mismatch)
        {
            // An old exact fact's lease cannot cross incompatible coordinate meanings.
            current.last_presented_at = None;
            current.last_presented_end_samples = None;
            self.store_spectrum_view(
                SpectrumViewStatus::Unavailable,
                None,
                spectrum_local.as_ref(),
            );
            return true;
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
            AnalysisViewMode::Attack => {
                store_joined_attack(self, current, now, attack_local, attack_remote)
            }
            AnalysisViewMode::Absolute => {}
        }
        true
    }
}
