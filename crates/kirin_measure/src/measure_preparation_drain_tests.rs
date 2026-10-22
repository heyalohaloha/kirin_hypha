use super::*;

#[test]
fn preparing_a_lane_reports_the_processed_watch_input_without_losing_its_prefix() {
    let tracker = RecordTakeTracker::new();
    tracker.note_meter_active(true);
    tracker.note_block(crate::RecordTakeBlock {
        generation: 0,
        recording: false,
        rendered: false,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: 0,
        num_frames: 9_600,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    tracker.note_capture_window(true, 0, 9_600);
    let (mut producer, consumer) = rtrb::RingBuffer::new(19_200);
    for _ in 0..19_200 {
        producer.push(0.25).unwrap();
    }
    let mut consumer = MeasureSampleConsumer::watch(consumer);
    let mut history = RawPreRollHistory::new(19_200, 2);
    let session = Arc::new(Mutex::new(
        MeterSession::new(48_000, ChannelLayout::stereo()).unwrap(),
    ));
    let state = RecordStateMachine::new();
    state.begin_measure_preparation(2);
    let (mut scratch, mut native, mut consumed, mut resume) = (Vec::new(), 0, 0, None);
    let drained = drain_watch_with_progress(
        &mut consumer,
        &tracker,
        &mut history,
        &mut scratch,
        &mut native,
        &mut consumed,
        2,
        48_000,
        4_800,
        &AtomicU8::new(0),
        Some(&session),
        None,
        &mut resume,
        None,
        |samples| {
            state.note_measure_preparation_samples(samples);
            assert_eq!(
                state.measure_preparation_samples(2) / 2,
                session.lock().unwrap().snapshot().active_frames,
                "progress must follow completed meter processing"
            );
            assert_eq!(state.measure_ready_generation(), 0);
        },
    );
    assert_eq!(drained.samples, 19_200);
    assert_eq!(state.measure_preparation_samples(2), 19_200);
    assert_eq!(consumer.slots(), 0);
    assert_eq!((native, consumed), (9_600, 19_200));
    // Compare with the producer's factual input, not another measurement implementation.
    let prefix = history.take_positioned_prefix(0, 9_600, 2).unwrap();
    assert_eq!(prefix.samples.len(), 19_200);
    assert!(prefix.samples.iter().all(|sample| *sample == 0.25));
}
