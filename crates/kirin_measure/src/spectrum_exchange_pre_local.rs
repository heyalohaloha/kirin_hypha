//! Local PRE sharpness demand. No request file, paired delta or audio-path changes.
//! pre_control serializes local ownership with a complete external PRE publication.
use super::*;
use crate::perceptual::PerceptualFrame;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
enum Owner {
    #[default]
    None,
    Local(u64),
    External(Uuid),
}
#[derive(Default)]
pub(super) struct LocalSharp {
    owner: Owner,
    generation: u64,
    next_token: u64,
    token: u64,
    expires: Option<Instant>,
    epoch: Option<i64>,
    clock: (u64, u64),
}
impl LocalSharp {
    fn change_owner(&mut self, owner: Owner) {
        if self.owner != owner {
            self.generation = self.generation.saturating_add(1);
            self.owner = owner;
            self.epoch = None;
        }
    }
    fn live(&self, now: Instant) -> bool {
        self.token != 0 && self.expires.is_some_and(|end| now < end)
    }
}
impl SpectrumCoordinator {
    /// UI/non-RT. Zero obtains a new exclusive token; nonzero only renews its own token.
    /// Busy returns zero without changing the previous demand. Poll never renews it.
    pub fn request_pre_sharp(&self, token: u64) -> u64 {
        if !matches!(self.runtime.num_channels(), 1 | 2) {
            return 0;
        }
        let Ok(_control) = self.pre_control.try_write() else {
            return 0;
        };
        let Ok(mut local) = self.pre_local.lock() else {
            return 0;
        };
        let now = self.clock_now();
        if token == 0 {
            if local.live(now) {
                return 0;
            }
            self.stop_pre_local(&mut local);
            let Some(next) = local.next_token.checked_add(1) else {
                return 0;
            };
            local.next_token = next;
            local.token = next;
        } else if token != local.token || !local.live(now) {
            return 0;
        }
        local.expires = now.checked_add(Duration::from_millis(REQUEST_LEASE_MS as u64));
        let result = local.token;
        drop(local);
        self.exchange_worker.notify();
        result
    }

    /// False means busy/foreign, not released. The UI may retry; an unrenewed token expires.
    pub fn release_pre_sharp(&self, token: u64) -> bool {
        let Ok(_control) = self.pre_control.try_write() else {
            return false;
        };
        let Ok(mut local) = self.pre_local.lock() else {
            return false;
        };
        if token == 0 || local.token != token {
            return false;
        }
        self.stop_pre_local(&mut local);
        local.token = 0;
        local.expires = None;
        true
    }

    pub fn poll_pre_sharp(&self, token: u64) -> Option<(u64, PerceptualFrame)> {
        let _control = self.pre_control.try_write().ok()?;
        let local = self.pre_local.lock().ok()?;
        if token == 0
            || token != local.token
            || !local.live(self.clock_now())
            || local.owner != Owner::Local(token)
            || !self.runtime.is_enabled()
            || self.runtime.analysis_mode() != AnalysisViewMode::Perceptual
            || self.runtime.channel_mode() != SpectrumChannelMode::Lr
            || self.runtime.analysis_clock_token() != local.clock
        {
            return None;
        }
        let epoch = local.epoch?;
        let history = self.runtime.try_perceptual_history()?;
        let frame = history.newest()?.clone();
        if frame.state_epoch_samples != epoch
            || frame.sample_rate != self.sample_rate
            || frame.channel_mode != SpectrumChannelMode::Lr
            || !frame.sharpness.is_finite()
            || frame.aperture_samples != self.sample_rate / 10
            || self.runtime.analysis_clock_token() != local.clock
        {
            return None;
        }
        Some((local.generation, frame))
    }

    pub(crate) fn pre_local_requested(&self) -> bool {
        self.pre_local
            .lock()
            .is_ok_and(|local| local.live(self.clock_now()))
    }
    pub(super) fn pre_local_is_running(&self) -> bool {
        self.pre_local
            .lock()
            .is_ok_and(|local| matches!(local.owner, Owner::Local(_)))
    }
    // Called only inside pre_control + pre_session, before external descriptor changes.
    pub(super) fn pre_local_external(&self, id: Uuid) -> bool {
        let mut local = self.pre_local.lock().unwrap_or_else(|p| p.into_inner());
        let changed = local.owner != Owner::External(id);
        self.stop_pre_local(&mut local);
        local.change_owner(Owner::External(id));
        changed
    }
    pub(super) fn pre_local_external_retired(&self) {
        let mut local = self.pre_local.lock().unwrap_or_else(|p| p.into_inner());
        if matches!(local.owner, Owner::External(_)) {
            local.change_owner(Owner::None);
        }
    }
    fn stop_pre_local(&self, local: &mut LocalSharp) {
        if matches!(local.owner, Owner::Local(_)) {
            self.disable_analysis_runtimes();
            let _ = self.runtime.set_perceptual_state_epoch(None);
            self.release_analysis_lease();
            local.change_owner(Owner::None);
        }
    }
    /// Existing PRE IO/analysis worker only, after its latest read found no external request.
    pub(super) fn service_pre_local(&self, revision: u64) -> bool {
        if self.pre_request_read_revision.load(Ordering::Acquire) != revision {
            return false;
        }
        let mut local = self.pre_local.lock().unwrap_or_else(|p| p.into_inner());
        if !local.live(self.clock_now()) {
            self.stop_pre_local(&mut local);
            local.token = 0;
            local.expires = None;
            return false;
        }
        // A busy/obsolete retirement may leave the external owner intact. Never steal it.
        if matches!(local.owner, Owner::External(_)) {
            return false;
        }
        if !self
            .ensure_analysis_lease("Hypha PRE SHARP")
            .unwrap_or(false)
        {
            self.stop_pre_local(&mut local);
            return false;
        }
        let token = local.token;
        if local.owner != Owner::Local(token) {
            self.restart_session_runtimes(AnalysisViewMode::Perceptual);
            if !self.runtime.set_analysis_mode(AnalysisViewMode::Perceptual)
                || !self.runtime.set_channel_mode(SpectrumChannelMode::Lr)
                || !self.set_active_runtime_enabled(AnalysisViewMode::Perceptual, true)
            {
                self.release_analysis_lease();
                return false;
            }
            local.change_owner(Owner::Local(token));
            local.clock = self.runtime.analysis_clock_token();
        }
        if local.clock != self.runtime.analysis_clock_token()
            || self.runtime.take_perceptual_rearm_required()
        {
            let _ = self.runtime.set_perceptual_state_epoch(None);
            local.epoch = None;
            local.generation = local.generation.saturating_add(1);
            local.clock = self.runtime.analysis_clock_token();
        }
        if local.epoch.is_none() {
            let Some(end) = self.runtime.latest_presentation_end() else {
                return true;
            };
            let aperture = i64::from(self.sample_rate / 10);
            let Some(epoch) = end
                .div_euclid(aperture)
                .checked_add(2)
                .and_then(|x| x.checked_mul(aperture))
            else {
                return false;
            };
            if self.runtime.set_perceptual_state_epoch(Some(epoch)) {
                local.epoch = Some(epoch);
                local.clock = self.runtime.analysis_clock_token();
            }
        }
        true
    }
}

#[cfg(test)]
#[path = "spectrum_exchange_pre_local_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "spectrum_exchange_pre_local_audio_tests.rs"]
mod audio_tests;

#[cfg(test)]
#[path = "spectrum_exchange_pre_local_cpu_tests.rs"]
mod cpu_tests;
