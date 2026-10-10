//! Non-RT TIME access and worker-span retirement, separate from measurement feeding.
use super::*;
impl MeterSession {
    /// Lightweight local playback fact; reading it does not recompute Session statistics.
    pub fn time_is_active(&self) -> bool {
        self.state == MeterSessionState::Active
    }

    pub fn time_source_span(&self) -> TimeSourceSpan {
        TimeSourceSpan {
            epoch: self.measurement_epoch,
            incarnation: self.history_incarnation,
            generation: self.generation,
            token: self.time.token.load(Ordering::Acquire),
            sample_rate: self.sample_rate,
            channels: self.n_channels as u8,
        }
    }

    pub fn time_span_token(&self) -> Arc<AtomicU64> {
        Arc::clone(&self.time.token)
    }

    pub fn time_raw_tail(&self, capacity: usize) -> Vec<TimeRawPoint> {
        self.time.tail(capacity)
    }

    /// Publisher cache metadata only; unchanged polls never clone the raw tail.
    pub(crate) fn time_latest_observed(&self) -> Option<u64> {
        self.time.latest_observed()
    }

    /// A replacement worker cannot reuse the old raw publication/proof, while the user's
    /// cumulative Meter Session remains intact. Non-RT watchdog only.
    pub fn retire_time_worker_span(&mut self) {
        self.time.reset();
        self.clock.break_continuity();
        self.engine.break_content_continuity();
        self.content_windows.clear();
        self.content_revision = self.content_revision.wrapping_add(1);
        self.history_revision = self.history_revision.wrapping_add(1);
        if self.state != MeterSessionState::Empty {
            self.state = MeterSessionState::Paused;
        }
    }

    pub fn time_history(
        &self,
        resolution: MeterHistoryResolution,
        lower: u64,
        cutoff: u64,
        capacity: usize,
    ) -> Result<Vec<MeterHistoryEntry>, crate::meter_history::TimeHistoryCountOverflow> {
        self.history.time_range(resolution, lower, cutoff, capacity)
    }
}
