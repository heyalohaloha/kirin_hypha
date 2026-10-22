use super::*;

fn assert_split(generation: u64) {
    let tracker = RecordTakeTracker::new();
    tracker.note_transport_start(1_000);
    let capture = |position| {
        tracker.note_block(crate::record_take::RecordTakeBlock {
            generation,
            recording: generation > 0,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: position,
            num_frames: 512,
            clock_start_samples: position,
            clock_end_samples: Some(position + 512),
        });
        tracker.note_capture_window_with_clocks_boundary(
            true,
            position,
            512,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples::default(),
            AuxiliaryClockSamples::default(),
            false,
        )
    };
    capture(1_000);
    tracker.note_transport_start(1_512);
    capture(1_512);
    // The capture clock is one continuous span, as while Record suppresses Watch pass changes.
    assert_eq!(
        tracker.capture_span_for_frame(1).unwrap().epoch,
        tracker.capture_span_for_frame(1_024).unwrap().epoch
    );
    assert_eq!(
        tracker.capture_span_for_frame(1).unwrap().generation,
        generation
    );
    let first = capture_chunk_plan(&tracker, 0, 2_048, 2, 48_000);
    assert_eq!(
        first.sample_count, 1_024,
        "the old authority ends at frame 512"
    );
    assert_eq!(first.meter_clock_start(0).playback_start, Some(1_000));
    let second = capture_chunk_plan(&tracker, 512, 1_024, 2, 48_000);
    assert_eq!(second.sample_count, 1_024);
    assert_eq!(second.meter_clock_start(0).playback_start, Some(1_512));
}

#[test]
fn a_contiguous_capture_span_is_split_at_each_transport_start() {
    assert_split(0);
}
#[test]
fn a_contiguous_record_capture_span_is_split_at_each_transport_start() {
    assert_split(7);
}

#[test]
fn an_unconfirmed_start_splits_a_contiguous_record_span_without_erasing_its_prefix() {
    let tracker = RecordTakeTracker::new();
    tracker.note_transport_start(1_000);
    tracker.note_capture_window(true, 1_000, 512);
    tracker.note_unknown_transport_start();
    tracker.note_capture_window(true, 1_512, 512);
    let first = capture_chunk_plan(&tracker, 0, 2_048, 2, 48_000);
    assert_eq!(first.sample_count, 1_024);
    assert_eq!(first.transport_start_raw, Some(1_000));
    let second = capture_chunk_plan(&tracker, 512, 1_024, 2, 48_000);
    assert_eq!(second.sample_count, 1_024);
    assert_eq!(second.transport_start_raw, None);
}
