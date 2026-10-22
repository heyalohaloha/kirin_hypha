//! The Audio producer records activity against the same frame clock as PCM capture.
use super::*;

impl RecordTakeTracker {
    /// Single Audio writer, before capture. Freshness overrides never call this method.
    pub fn note_meter_active(&self, active: bool) {
        let encoded = if active { 2 } else { 1 };
        if self.last_meter_active.swap(encoded, Ordering::Relaxed) != encoded {
            self.active_edge_journal
                .note(self.captured_frames_total(), active);
        }
    }

    /// Audio-side ring replacement rebases the known current producer state after invalidation.
    pub fn rebind_meter_active_after_clock_reset(&self) {
        let encoded = self.last_meter_active.load(Ordering::Relaxed);
        if encoded != 0 {
            self.active_edge_journal
                .note(self.captured_frames_total(), encoded == 2);
        }
    }

    pub fn meter_active_at(&self, captured_frame: u64) -> Option<bool> {
        self.active_edge_journal.latest_at_or_before(captured_frame)
    }

    pub(crate) fn meter_activity_snapshot(&self) -> Option<AudioEdgeSnapshot<bool>> {
        self.active_edge_journal.snapshot()
    }
}
