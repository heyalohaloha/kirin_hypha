//! A Session value and its exact processed/pending frame scope, acquired from the same state.
use super::*;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MeterSessionSummaryStatus {
    Empty = 0,
    Complete = 1,
    PendingTail = 2,
}

#[derive(Clone, Debug)]
pub struct MeterSessionSnapshotV2 {
    pub session: MeterSessionSnapshot,
    /// Channel-local sample frames submitted to EBU; summary values refer to this input.
    pub processed_frames: u64,
    /// Accepted frames awaiting the next analysis chunk; retained through pause/bypass.
    pub pending_frames: u64,
    /// Complete means input coverage, not that a silent or short signal has every metric.
    pub summary_status: MeterSessionSummaryStatus,
}

impl MeterSession {
    pub fn snapshot_v2(&self) -> MeterSessionSnapshotV2 {
        let processed_frames = self
            .engine
            .session_processed_frames()
            .saturating_sub(self.processed_origin_frames);
        let pending_frames = self.active_frames.saturating_sub(processed_frames);
        let summary_status = if self.active_frames == 0 {
            MeterSessionSummaryStatus::Empty
        } else if pending_frames == 0 {
            MeterSessionSummaryStatus::Complete
        } else {
            MeterSessionSummaryStatus::PendingTail
        };
        MeterSessionSnapshotV2 {
            session: self.snapshot(),
            processed_frames,
            pending_frames,
            summary_status,
        }
    }
}

#[cfg(test)]
#[path = "meter_session_coverage_tests.rs"]
mod tests;
