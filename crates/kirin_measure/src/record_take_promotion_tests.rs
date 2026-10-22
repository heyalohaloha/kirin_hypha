use super::*;

fn callback(
    tracker: &RecordTakeTracker,
    generation: u64,
    position: i64,
    frames: u64,
    offline: bool,
) {
    tracker.note_block(RecordTakeBlock {
        generation,
        recording: true,
        rendered: true,
        playing: true,
        offline,
        position_valid: true,
        position_samples: position,
        num_frames: frames,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    tracker.note_capture_window_with_clocks_boundary(
        true,
        position,
        frames,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(0),
        },
        AuxiliaryClockSamples {
            source: AuxiliaryClockSource::Vst3Continuous,
            samples: Some(tracker.captured_frames_total() as i64),
        },
        false,
    );
}

#[test]
fn promotion_preserves_the_requested_pass_first_callback_and_grows_only_that_pass() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 1, 37_440, 960, false);
    let from = tracker.selected_capture_epoch(1).unwrap();
    callback(&tracker, 1, 0, 4_800, false);
    let target = tracker.clock_point_for_captured_frame(961).unwrap().epoch;
    assert!(tracker.request_take_promotion(1, from, target));
    callback(&tracker, 1, 4_800, 4_800, false);
    assert_eq!(tracker.selected_capture_epoch(1), Some(target));
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 9_600);
    assert_eq!(
        tracker.snapshot(1).unwrap().host_start_position_samples,
        Some(0)
    );
    callback(&tracker, 1, 9_600, 28_800, false);
    callback(&tracker, 1, 0, 4_800, false);
    assert_eq!(tracker.selected_capture_epoch(1), Some(target));
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 38_400);
}

#[test]
fn a_late_request_keeps_the_first_complete_short_loop_instead_of_the_latest_lap() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 1, 37_440, 960, false);
    let from = tracker.selected_capture_epoch(1).unwrap();
    callback(&tracker, 1, 0, 38_400, false);
    let target = tracker.clock_point_for_captured_frame(961).unwrap().epoch;
    callback(&tracker, 1, 0, 38_400, false);
    assert!(tracker.request_take_promotion(1, from, target));
    callback(&tracker, 1, 0, 4_800, false);
    assert_eq!(tracker.selected_capture_epoch(1), Some(target));
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 38_400);
    callback(&tracker, 1, 4_800, 38_400, false);
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 38_400);
    assert!(!tracker.take_promotion_pending(1));
}

#[test]
fn promotion_loses_to_an_audio_thread_upgrade() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 1, 37_440, 960, false);
    let from = tracker.selected_capture_epoch(1).unwrap();
    callback(&tracker, 1, 0, 4_800, false);
    let target = tracker.clock_point_for_captured_frame(961).unwrap().epoch;
    assert!(tracker.request_take_promotion(1, from, target));
    callback(&tracker, 1, 0, 48_000, true);
    assert_ne!(tracker.selected_capture_epoch(1), Some(target));
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 48_000);
    assert!(!tracker.take_promotion_pending(1));
}

#[test]
fn a_request_from_a_closed_generation_cannot_replace_the_next_keep() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 1, 37_440, 960, false);
    let from = tracker.selected_capture_epoch(1).unwrap();
    callback(&tracker, 1, 0, 4_800, false);
    let target = tracker.clock_point_for_captured_frame(961).unwrap().epoch;
    assert!(tracker.request_take_promotion(1, from, target));
    callback(&tracker, 2, 100_000, 9_600, false);
    assert_ne!(tracker.selected_capture_epoch(2), Some(target));
    assert_eq!(tracker.snapshot(2).unwrap().duration_samples, 9_600);
    assert!(!tracker.take_promotion_pending(1));
}

#[test]
fn a_stopped_transport_callback_can_complete_a_pending_short_take_request() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 1, 37_440, 960, false);
    let from = tracker.selected_capture_epoch(1).unwrap();
    callback(&tracker, 1, 0, 4_800, false);
    let target = tracker.clock_point_for_captured_frame(961).unwrap().epoch;
    assert!(tracker.request_take_promotion(1, from, target));
    tracker.note_block(RecordTakeBlock {
        generation: 1,
        recording: true,
        rendered: false,
        playing: false,
        offline: false,
        position_valid: true,
        position_samples: 4_800,
        num_frames: 512,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    assert_eq!(tracker.selected_capture_epoch(1), Some(target));
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 4_800);
    assert!(!tracker.take_promotion_pending(1));
}

#[test]
fn a_promotion_stays_pending_until_all_producer_updates_are_published() {
    let request = TakePromotionRequest::default();
    request.from.store(1, Ordering::Relaxed);
    request.to.store(2, Ordering::Relaxed);
    request.generation.store(3, Ordering::Release);
    request.apply(|generation, from, to| {
        assert_eq!((generation, from, to), (3, 1, 2));
        assert_eq!(
            request.generation.load(Ordering::Acquire),
            3,
            "Measure must not consume the first new callback during the producer update"
        );
    });
    assert_eq!(request.generation.load(Ordering::Acquire), 0);
}
