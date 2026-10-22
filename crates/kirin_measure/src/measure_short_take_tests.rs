use super::*;
use crate::record_take::{KeepChunk, RecordTakeBlock};

fn callback(tracker: &RecordTakeTracker, position: i64, frames: u64) {
    tracker.note_block(RecordTakeBlock {
        generation: 1,
        recording: true,
        rendered: true,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: frames,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    tracker.note_capture_window(true, position, frames);
}

#[test]
fn a_short_first_pass_yields_only_once_per_generation_even_for_subsecond_loops() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 37_440, 960);
    let selected = tracker.selected_capture_epoch(1);
    tracker.note_keep_chunk(KeepChunk {
        generation: 1,
        raw_host_start: Some(37_440),
        frames: 960,
        in_trace: true,
        in_summary: true,
        ..Default::default()
    });
    callback(&tracker, 0, 4_800);
    let candidate = tracker
        .clock_point_for_captured_frame(961)
        .map(|point| point.epoch);
    assert!(wait_for_short_take(
        &tracker,
        1,
        48_000,
        RecordChunkRoute::KeepPass,
        selected,
        candidate
    ));
    // Audio has not handled the request: leave the consumer at that first new callback.
    assert!(wait_for_short_take(
        &tracker,
        1,
        48_000,
        RecordChunkRoute::KeepPass,
        selected,
        candidate
    ));
    callback(&tracker, 4_800, 33_600);
    assert_eq!(tracker.selected_capture_epoch(1), candidate);
    let reason = tracker.trace_replacement_reason(candidate.unwrap());
    assert_eq!(reason, crate::record_take::KEEP_PASS_SHORT_TRACE_REPLACED);
    tracker.replace_trace_pass(1, reason);
    tracker.note_keep_chunk(KeepChunk {
        generation: 1,
        raw_host_start: Some(0),
        frames: 38_400,
        in_trace: true,
        in_summary: true,
        ..Default::default()
    });
    callback(&tracker, 0, 4_800);
    let next = tracker
        .clock_point_for_captured_frame(39_361)
        .map(|point| point.epoch);
    assert!(!wait_for_short_take(
        &tracker,
        1,
        48_000,
        RecordChunkRoute::KeepPass,
        candidate,
        next
    ));
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 38_400);
    let passes = tracker.keep_passes(1);
    assert_eq!(passes.iter().filter(|pass| pass.in_trace).count(), 1);
    assert_eq!(
        passes[0].trace_excluded_reason.as_deref(),
        Some(crate::record_take::KEEP_PASS_SHORT_TRACE_REPLACED)
    );
}

#[test]
fn a_first_pass_of_one_second_or_longer_keeps_its_original_take() {
    for frames in [48_000, 48_001] {
        let tracker = RecordTakeTracker::new();
        callback(&tracker, 0, frames);
        let selected = tracker.selected_capture_epoch(1);
        tracker.note_keep_chunk(KeepChunk {
            generation: 1,
            raw_host_start: Some(0),
            frames,
            in_trace: true,
            in_summary: true,
            ..Default::default()
        });
        callback(&tracker, 0, 4_800);
        let next = tracker
            .clock_point_for_captured_frame(frames + 1)
            .map(|point| point.epoch);
        assert!(!wait_for_short_take(
            &tracker,
            1,
            48_000,
            RecordChunkRoute::KeepPass,
            selected,
            next
        ));
        assert_eq!(tracker.selected_capture_epoch(1), selected);
    }
}

#[test]
fn a_stale_route_is_retried_after_the_producer_finishes_promotion() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 37_440, 960);
    let selected = tracker.selected_capture_epoch(1);
    tracker.note_keep_chunk(KeepChunk {
        generation: 1,
        raw_host_start: Some(37_440),
        frames: 960,
        in_trace: true,
        in_summary: true,
        ..Default::default()
    });
    callback(&tracker, 0, 4_800);
    let candidate = tracker
        .clock_point_for_captured_frame(961)
        .map(|point| point.epoch);
    assert!(wait_for_short_take(
        &tracker,
        1,
        48_000,
        RecordChunkRoute::KeepPass,
        selected,
        candidate
    ));
    callback(&tracker, 4_800, 4_800);
    assert!(!tracker.take_promotion_pending(1));
    assert!(
        wait_for_short_take(
            &tracker,
            1,
            48_000,
            RecordChunkRoute::KeepPass,
            selected,
            candidate
        ),
        "a route read before producer completion cannot consume the first new callback"
    );
}
