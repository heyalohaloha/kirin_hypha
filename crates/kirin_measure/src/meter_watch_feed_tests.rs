use super::*;

fn meter() -> Arc<Mutex<MeterSession>> {
    Arc::new(Mutex::new(
        MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 1).unwrap(),
    ))
}
fn queued(samples: usize) -> MeasureSampleConsumer {
    let (mut producer, consumer) = rtrb::RingBuffer::new(samples);
    for _ in 0..samples {
        producer.push(0.25).unwrap();
    }
    MeasureSampleConsumer::watch(consumer)
}
fn inactive() -> AtomicU8 {
    let state = AtomicU8::new(0);
    store_signal_state(&state, SignalState::Inactive);
    state
}

#[test]
fn a_pass_cutover_feeds_the_previous_pass_without_resetting_its_cursor() {
    let tracker = RecordTakeTracker::new();
    tracker.note_meter_active(true);
    tracker.note_capture_window(true, 100_000, 4_800);
    tracker.note_capture_window(true, 300_000, 4_800);
    let mut consumer = queued(19_200);
    let session = meter();
    let mut history = RawPreRollHistory::new(19_200, 2);
    let (mut scratch, mut native, mut consumed, mut resume) = (Vec::new(), 0, 0, None);
    let state = inactive();
    let drained = drain_watch_into_raw_pre_roll(
        &mut consumer,
        &tracker,
        &mut history,
        &mut scratch,
        &mut native,
        &mut consumed,
        2,
        48_000,
        2_400,
        &state,
        Some(&session),
        None,
        &mut resume,
        Some(9_600),
    );
    assert_eq!(drained.samples, 9_600);
    assert!(drained.fed);
    assert_eq!(session.lock().unwrap().snapshot().active_frames, 4_800);
    assert_eq!((native, consumed, consumer.slots()), (4_800, 9_600, 9_600));
    history.clear();
    let next = capture_chunk_plan(&tracker, native, consumer.slots(), 2, 48_000);
    assert_eq!(next.position_start_samples, Some(300_000));
    drain_watch_into_raw_pre_roll(
        &mut consumer,
        &tracker,
        &mut history,
        &mut scratch,
        &mut native,
        &mut consumed,
        2,
        48_000,
        2_400,
        &state,
        Some(&session),
        None,
        &mut resume,
        None,
    );
    assert_eq!(session.lock().unwrap().snapshot().active_frames, 9_600);
    assert_eq!((native, consumed), (9_600, 19_200));
}

#[test]
fn watch_drain_skips_record_frames_without_feeding_them_again() {
    let tracker = RecordTakeTracker::new();
    tracker.note_meter_active(true);
    tracker.note_capture_window(true, 0, 9_600);
    let mut consumer = queued(9_600);
    let session = meter();
    let mut history = RawPreRollHistory::new(9_600, 2);
    let (mut scratch, mut native, mut consumed) = (Vec::new(), 0, 0);
    let mut resume = Some(WatchResume::after_keep(2_400, 4_800));
    drain_watch_into_raw_pre_roll(
        &mut consumer,
        &tracker,
        &mut history,
        &mut scratch,
        &mut native,
        &mut consumed,
        2,
        48_000,
        9_600,
        &inactive(),
        Some(&session),
        None,
        &mut resume,
        None,
    );
    assert_eq!(native, 9_600);
    assert_eq!(session.lock().unwrap().snapshot().active_frames, 4_800);
    assert!(resume.is_none());
}

#[test]
fn delayed_stop_tail_has_the_same_ebu_state_as_an_immediate_consumer() {
    let tracker = RecordTakeTracker::new();
    let fast = meter();
    let slow = meter();
    let old = meter();
    let signal = |frames: usize, level: f64| {
        (0..frames)
            .flat_map(|i| {
                let x = level * (std::f64::consts::TAU * 997.0 * i as f64 / 48_000.0).sin();
                [x, x]
            })
            .collect::<Vec<_>>()
    };
    let warm = signal(19_200, 0.1);
    let tail = signal(4_800, 0.7);
    let next = signal(4_800, 0.1);
    tracker.note_meter_active(true);
    tracker.note_capture_window(true, 0, 19_200);
    let warm_plan = capture_chunk_plan(&tracker, 0, warm.len(), 2, 48_000);
    for session in [&fast, &slow, &old] {
        MeterFeed {
            tracker: &tracker,
            session: Some(session),
            publication: None,
            n_channels: 2,
            fallback_active: true,
        }
        .feed(&warm, 0, warm_plan, 0);
    }
    tracker.note_capture_window(true, 19_200, 4_800);
    let tail_plan = capture_chunk_plan(&tracker, 19_200, tail.len(), 2, 48_000);
    MeterFeed {
        tracker: &tracker,
        session: Some(&fast),
        publication: None,
        n_channels: 2,
        fallback_active: true,
    }
    .feed(&tail, 19_200, tail_plan, 0);
    tracker.note_meter_active(false);
    MeterFeed {
        tracker: &tracker,
        session: Some(&slow),
        publication: None,
        n_channels: 2,
        fallback_active: false,
    }
    .feed(&tail, 19_200, tail_plan, 0);
    // Negative control: the previous pop-time gate skips this same already-pushed tail.
    for session in [&fast, &slow, &old] {
        pause_meter_session(Some(session), None);
    }
    tracker.note_meter_active(true);
    tracker.note_capture_window(true, 24_000, 4_800);
    let next_plan = capture_chunk_plan(&tracker, 24_000, next.len(), 2, 48_000);
    for session in [&fast, &slow, &old] {
        MeterFeed {
            tracker: &tracker,
            session: Some(session),
            publication: None,
            n_channels: 2,
            fallback_active: true,
        }
        .feed(&next, 24_000, next_plan, 0);
    }
    let fast = fast.lock().unwrap().snapshot();
    let slow = slow.lock().unwrap().snapshot();
    let old = old.lock().unwrap().snapshot();
    assert_eq!((fast.active_frames, slow.active_frames), (28_800, 28_800));
    assert!(fast.current.lufs_m.is_some() && fast.current.true_peak.is_some());
    assert_eq!(fast.current.lufs_m, slow.current.lufs_m);
    assert_eq!(fast.current.true_peak, slow.current.true_peak);
    assert_eq!(fast.current.crest, slow.current.crest);
    assert_eq!(fast.summary.max_true_peak, slow.summary.max_true_peak);
    assert_ne!(
        fast.summary.max_true_peak, old.summary.max_true_peak,
        "the tail must affect the oracle, rather than both paths remaining empty or constant"
    );
}
