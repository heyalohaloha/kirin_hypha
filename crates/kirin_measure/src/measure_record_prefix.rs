//! Bind the Record reader to its admitted producer span and exact raw prefix.
use crate::capture_clock::CaptureClockSpan;
use crate::raw_pre_roll::{RawPreRollHistory, RawRecordPrefix};
use crate::record_take::RecordTakeTracker;

pub(super) struct RecordPrefixBinding {
    pub native_origin: u64,
    pub position: Option<i64>,
    pub epoch: u64,
    pub prefix: Option<RawRecordPrefix>,
}

pub(super) fn bind(
    history: &mut RawPreRollHistory,
    tracker: &RecordTakeTracker,
    generation: u64,
    capture_origin: u64,
) -> Option<RecordPrefixBinding> {
    let span = tracker.capture_span_for_frame(capture_origin.saturating_add(1))?;
    bind_span(history, tracker, generation, capture_origin, span)
}

fn bind_span(
    history: &mut RawPreRollHistory,
    tracker: &RecordTakeTracker,
    generation: u64,
    capture_origin: u64,
    mut span: CaptureClockSpan,
) -> Option<RecordPrefixBinding> {
    // An offline Watch span can still be promoted by the next armed callback. Keep its exact
    // history only while the producer can still promote this generation/epoch. An ordinary Watch
    // span is never eligible merely because control changed lanes mid-callback.
    if span.offline && span.generation == 0 {
        if tracker.offline_watch_promotion_pending(generation, span.epoch)
            && tracker
                .selected_capture_epoch(generation)
                .is_none_or(|selected| selected == span.epoch)
        {
            return None;
        }
        // The producer clears a candidate after publishing its promotion. Resolve that published
        // span again so a concurrent successful promotion cannot discard the exact prefix.
        span = tracker.capture_span_for_frame(capture_origin.saturating_add(1))?;
    }
    let prefix = if generation > 0 && span.generation == generation {
        history.take_positioned_prefix(span.capture_start_frame, capture_origin, span.epoch)
    } else {
        history.clear();
        None
    };
    let native_origin = prefix
        .as_ref()
        .map_or(capture_origin, |prefix| prefix.start_capture_frame);
    let epoch = prefix.as_ref().map_or(span.epoch, |prefix| prefix.epoch);
    Some(RecordPrefixBinding {
        native_origin,
        position: span.position_at_capture_boundary(native_origin),
        epoch,
        prefix,
    })
}

#[cfg(test)]
#[path = "measure_record_prefix_tests.rs"]
mod tests;
