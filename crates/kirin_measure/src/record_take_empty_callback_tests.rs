use super::*;

fn audio(tracker: &RecordTakeTracker, position: i64, frames: u64) {
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
fn an_empty_playing_callback_applies_promotion_without_ending_the_take() {
    let tracker = RecordTakeTracker::new();
    audio(&tracker, 37_440, 960);
    let from = tracker.selected_capture_epoch(1).unwrap();
    audio(&tracker, 0, 4_800);
    let target = tracker.clock_point_for_captured_frame(961).unwrap().epoch;
    assert!(tracker.request_take_promotion(1, from, target));
    let captured = tracker.captured_frames_total();
    tracker.note_block(RecordTakeBlock {
        generation: 1,
        recording: true,
        rendered: false,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: 4_800,
        num_frames: 0,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    assert_eq!(tracker.selected_capture_epoch(1), Some(target));
    assert_eq!(tracker.captured_frames_total(), captured);
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 4_800);
    audio(&tracker, 4_800, 4_800);
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 9_600);
    assert_eq!(
        tracker.snapshot(1).unwrap().host_start_position_samples,
        Some(0)
    );
}
