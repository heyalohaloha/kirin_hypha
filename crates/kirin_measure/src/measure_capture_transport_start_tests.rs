use super::*;
#[test]
fn a_delayed_chunk_uses_its_own_start_and_output_presentation_conversion() {
    let tracker = RecordTakeTracker::new();
    let latency = PresentationLatencySamples {
        source: crate::PresentationLatencySource::AudioUnitV2,
        input: None,
        output: Some(685),
    };
    let capture = |position| {
        tracker.note_capture_window_with_clocks_boundary(
            true,
            position,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            AuxiliaryClockSamples::default(),
            false,
        )
    };
    tracker.note_transport_start(1_000);
    capture(1_000);
    tracker.note_transport_start(40_000);
    capture(40_000);
    let old = capture_chunk_plan(&tracker, 0, 1_024, 2, 48_000);
    let new = capture_chunk_plan(&tracker, 512, 1_024, 2, 48_000);
    assert_eq!(old.meter_clock_start(200).playback_start, Some(315));
    assert_eq!(old.meter_clock_start(200).position_samples, Some(515));
    assert_eq!(new.meter_clock_start(0).playback_start, Some(39_315));
    tracker.reset_capture_clock();
    capture(80_000);
    assert_eq!(
        capture_chunk_plan(&tracker, 0, 1_024, 2, 48_000)
            .meter_clock_start(0)
            .playback_start,
        None
    );
}
