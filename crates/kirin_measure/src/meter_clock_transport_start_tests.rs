use super::*;
fn clock(position: i64, start: i64) -> MeterClockStart {
    MeterClockStart {
        position_samples: Some(position),
        playback_start: Some(start),
        epoch: Some(1),
        source: CaptureClockSource::ProjectTimeline,
        ..Default::default()
    }
}
#[test]
fn a_transport_start_rides_its_span_to_the_observation() {
    let mut tracker = MeterClockTracker::new();
    tracker.push_span(2_400, clock(0, -685));
    tracker.push_span(2_400, clock(20_000, 19_315));
    let mixed = tracker.consume_observation(4_800);
    assert!(!mixed.usable_for_history);
    assert_eq!(mixed.playback_start, None);
    tracker.push_span(4_800, clock(22_400, 19_315));
    let complete = tracker.consume_observation(4_800);
    assert!(complete.usable_for_history);
    assert_eq!(complete.playback_start, Some(19_315));
}
#[test]
fn changing_start_authority_retires_an_otherwise_continuous_observation() {
    let mut tracker = MeterClockTracker::new();
    tracker.push_span(2_400, clock(0, 0));
    tracker.push_span(2_400, clock(2_400, 2_400));
    let mixed = tracker.consume_observation(4_800);
    assert!(!mixed.usable_for_history);
    assert_eq!(mixed.playback_start, None);
    tracker.push_span(4_800, clock(4_800, 2_400));
    let resumed = tracker.consume_observation(4_800);
    assert_eq!(
        resumed.continuous_frames, 7_200,
        "only frames after the transport start prove a new window"
    );
}
