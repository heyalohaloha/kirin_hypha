//! Intermediate Record display publication; final sealing remains the canonical drain path.
use std::sync::{Arc, Mutex};

use crate::record::RecordStateMachine;
use crate::record_measure_engines::RecordMeasureEngines;
use crate::{MeasureResult, SessionSummary};

pub(super) fn publish(
    engines: &mut RecordMeasureEngines,
    summary_slot: &Arc<Mutex<Option<SessionSummary>>>,
    record: &RecordStateMachine,
    display_generation: Option<u64>,
    latest_measure: &Option<MeasureResult>,
) {
    let summary = engines.summary().finalize();
    if let Ok(mut slot) = summary_slot.lock() {
        *slot = Some(summary);
    }
    if let Some(measure) = latest_measure.clone() {
        record.publish_record_display_measure(
            display_generation.unwrap_or_else(|| record.generation()),
            measure,
            summary,
        );
    }
}
