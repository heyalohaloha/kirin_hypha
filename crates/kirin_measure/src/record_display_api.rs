//! Generation-bound Record display access.
use super::*;

impl RecordStateMachine {
    pub fn mark_record_display_measure_started(&self, generation: u64) {
        self.record_display.mark_measure_started(generation);
    }

    pub fn publish_record_display_measure(
        &self,
        generation: u64,
        measure: crate::MeasureResult,
        summary: crate::SessionSummary,
    ) {
        self.record_display
            .publish_measure(generation, measure, summary);
    }

    pub fn publish_record_display_delta(
        &self,
        generation: u64,
        delta: crate::DeltaResult,
        pair_pre_instance_id: Option<String>,
    ) {
        self.record_display
            .publish_delta(generation, delta, pair_pre_instance_id);
    }

    pub fn finalize_record_display(&self, generation: u64, summary: Option<crate::SessionSummary>) {
        self.record_display.finalize(generation, summary);
    }

    pub fn mark_record_display_unavailable(&self, generation: u64) {
        self.record_display.mark_unavailable(generation);
    }

    pub fn abandon_record_display(&self, generation: u64) {
        self.record_display.abandon(generation);
    }

    pub fn dismiss_record_display_on_watch_result(&self) {
        self.record_display.dismiss_on_watch_result();
    }

    pub fn try_record_display_snapshot(&self) -> Option<crate::RecordDisplaySnapshot> {
        self.record_display.try_snapshot()
    }
}
