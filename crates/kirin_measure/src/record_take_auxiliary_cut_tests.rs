use super::*;

fn capture(cut: bool, forced: bool, jump: bool) -> RecordTakeTracker {
    capture_with_latency(
        cut,
        forced,
        jump,
        PresentationLatencySamples {
            source: PresentationLatencySource::AudioUnitV2,
            input: Some(0),
            output: Some(0),
        },
    )
}
fn capture_with_latency(
    cut: bool,
    forced: bool,
    jump: bool,
    latency: PresentationLatencySamples,
) -> RecordTakeTracker {
    let tracker = RecordTakeTracker::new();
    for index in 0..1125_i64 {
        let raw = index * 512 + if jump && index >= 750 { 512 } else { 0 };
        tracker.note_block(RecordTakeBlock {
            generation: 1,
            recording: true,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: raw,
            num_frames: 512,
            clock_start_samples: 0,
            clock_end_samples: None,
        });
        tracker.note_capture_window_with_clocks_boundary(
            true,
            raw,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            AuxiliaryClockSamples {
                source: AuxiliaryClockSource::AudioUnitRender,
                samples: Some(raw + if cut && index >= 750 { 512 } else { 0 }),
            },
            forced && index == 750,
        );
    }
    tracker
}

#[test]
fn auxiliary_cut_keeps_all_twelve_seconds_of_record_audio_but_splits_pair_clock() {
    let tracker = capture(true, false, false);
    let first = tracker.selected_capture_epoch(1).unwrap();
    let last = tracker
        .clock_point_for_captured_frame(576000)
        .unwrap()
        .epoch;
    assert_ne!(first, last); // Pair evidence must still retire at the auxiliary boundary.
    assert!(tracker.capture_epochs_are_latency_continuation(first, last));
    assert_eq!(tracker.snapshot(1).unwrap().duration_samples, 576000);
    assert_eq!(
        tracker.presentation_range_for_latency_epoch_chain(first, 0, 576000),
        Some((0, 576000))
    );
    // Every callback admitted by the actual Measure Thread continuity predicate.
    let mut admitted_epoch = first;
    let mut admitted_frames = 0;
    for frame in (512..=576000).step_by(512) {
        let epoch = tracker.clock_point_for_captured_frame(frame).unwrap().epoch;
        assert!(
            epoch == admitted_epoch
                || tracker.capture_epochs_are_latency_continuation(admitted_epoch, epoch)
        );
        admitted_epoch = epoch;
        admitted_frames += 512;
    }
    assert_eq!(admitted_frames, 576000);
}

#[test]
fn raw_seek_and_explicit_cut_cannot_use_the_auxiliary_exception() {
    for (forced, jump) in [(true, false), (false, true)] {
        let tracker = capture(true, forced, jump);
        let first = tracker.selected_capture_epoch(1).unwrap();
        let last = tracker
            .clock_point_for_captured_frame(576000)
            .unwrap()
            .epoch;
        assert!(!tracker.capture_epochs_are_latency_continuation(first, last));
        assert_eq!(
            tracker.presentation_range_for_latency_epoch_chain(first, 0, 576000),
            None
        );
    }
}

#[test]
fn replacement_ring_retires_current_coordinates_and_preserves_frozen_previous_take() {
    let tracker = capture(false, false, false);
    assert_eq!(tracker.selected_capture_epoch(1), Some(1));
    assert_eq!(
        tracker
            .clock_point_for_captured_frame(576000)
            .unwrap()
            .epoch,
        1
    );
    tracker.reset_capture_clock();
    assert!(tracker.clock_point_for_captured_frame(512).is_none());
    assert_eq!(
        tracker.presentation_range_for_latency_epoch_chain(1, 0, 576000),
        Some((0, 576000))
    );
}

#[test]
fn an_unknown_latency_cannot_qualify_an_auxiliary_cut_as_continuous_record_audio() {
    let tracker = capture_with_latency(
        true,
        false,
        false,
        PresentationLatencySamples {
            source: PresentationLatencySource::Unknown,
            input: Some(0),
            output: Some(0),
        },
    );
    let first = tracker.selected_capture_epoch(1).unwrap();
    let last = tracker
        .clock_point_for_captured_frame(576000)
        .unwrap()
        .epoch;
    assert_ne!(first, last);
    assert!(!tracker.capture_epochs_are_latency_continuation(first, last));
    assert_eq!(
        tracker.presentation_range_for_latency_epoch_chain(first, 0, 576000),
        None
    );
}
