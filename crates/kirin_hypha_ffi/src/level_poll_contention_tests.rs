use super::*;
use kirin_measure::{ComparisonReason, ComparisonSnapshot, ComparisonState, DeltaMode};

fn fixture() -> KirinHyphaEngine {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    engine.watchdog_shutdown.store(true, Ordering::Release);
    engine.shutdown.store(true, Ordering::Release);
    if let Some(worker) = engine.watchdog_handle.lock().unwrap().take() {
        worker.join().unwrap();
    }
    engine.set_signal_state(1);
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    *engine.delta_result.lock().unwrap() = DeltaResult {
        mode: DeltaMode::Active,
        lufs: Some(9.0),
        comparison: ComparisonSnapshot {
            state: ComparisonState::Active,
            reason: ComparisonReason::None,
            generation: 1,
            identity: 23,
        },
        ..Default::default()
    };
    engine
}

fn poll(engine: &KirinHyphaEngine, packet: &mut KirinLevelSnapshot) -> bool {
    unsafe {
        kirin_hypha_poll_level_snapshot(
            engine as *const KirinHyphaEngine as *mut KirinHyphaEngine,
            1,
            0,
            std::ptr::null_mut(),
            0,
            0,
            std::ptr::null_mut(),
            0,
            packet,
        )
    }
}

fn retained_frame() -> KirinObservatoryFrame {
    let mut frame: KirinObservatoryFrame = unsafe { std::mem::zeroed() };
    frame.version = 777;
    frame.comparison_state = KIRIN_COMPARISON_STATE_HOLDING;
    frame.comparison_reason = KIRIN_COMPARISON_REASON_STALE;
    frame.delta.lufs = 4.5;
    frame
}

fn assert_busy(engine: &KirinHyphaEngine) {
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    packet.version = 999;
    packet.frame = retained_frame();
    assert!(!poll(engine, &mut packet));
    assert_eq!(packet.version, 999);
    assert_eq!(
        packet.frame.comparison_state,
        KIRIN_COMPARISON_STATE_HOLDING
    );
    assert_eq!(packet.frame.delta.lufs, 4.5);
    let mut frame = retained_frame();
    assert!(!unsafe {
        kirin_hypha_poll_observatory_frame(
            engine as *const KirinHyphaEngine as *mut KirinHyphaEngine,
            &mut frame,
        )
    });
    assert_eq!(frame.version, 777);
    assert_eq!(frame.comparison_reason, KIRIN_COMPARISON_REASON_STALE);
    assert_eq!(frame.delta.lufs, 4.5);
}

#[test]
fn every_level_authority_lock_contention_retains_the_callers_holding_frame() {
    let engine = fixture();
    // Polling reads a shared engine; locking an interior field needs no exclusive alias.
    {
        let _g = engine.write_role.lock().unwrap();
        assert_busy(&engine);
    }
    {
        let _g = engine.identity.lock().unwrap();
        assert_busy(&engine);
    }
    {
        let _g = engine.pair_claimed_at.write().unwrap();
        assert_busy(&engine);
    }
    {
        let _g = engine.project_hash_cell.write().unwrap();
        assert_busy(&engine);
    }
    {
        let locator = engine.pair_binding.latched_pre();
        let _g = locator.lock().unwrap();
        assert_busy(&engine);
    }
    {
        let _g = engine.delta_result.lock().unwrap();
        assert_busy(&engine);
    }
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    assert!(poll(&engine, &mut packet));
    assert_eq!(
        packet.frame.comparison_state,
        KIRIN_COMPARISON_STATE_PREPARING
    );
    assert_eq!(
        packet.frame.comparison_reason,
        KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT
    );
    assert!(packet.frame.delta.lufs.is_nan()); // Real missing data publishes MEASURING.
}

#[test]
fn stop_publishes_local_facts_before_the_io_holding_publication() {
    let engine = fixture();
    engine.set_signal_state(0);
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    assert!(poll(&engine, &mut packet));
    assert_eq!(packet.frame.signal_state, KIRIN_SIGNAL_STATE_INACTIVE);
    assert_eq!(
        packet.frame.comparison_reason,
        KIRIN_COMPARISON_REASON_LOCAL_INACTIVE
    );
    assert_eq!(
        packet.frame.comparison_state,
        KIRIN_COMPARISON_STATE_REJECTED
    );
    assert_eq!(packet.frame.delta_available, 0);
    assert!(packet.frame.delta.lufs.is_nan());
    {
        let mut delta = engine.delta_result.lock().unwrap();
        delta.mode = DeltaMode::Stale;
        delta.last_active = Some(kirin_measure::DeltaSnapshot {
            lufs: Some(4.5),
            ..Default::default()
        });
        delta.comparison.state = ComparisonState::Holding;
        delta.comparison.reason = ComparisonReason::Stale;
    }
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    assert!(poll(&engine, &mut packet));
    assert_eq!(
        packet.frame.comparison_state,
        KIRIN_COMPARISON_STATE_HOLDING
    );
    assert_eq!(
        packet.frame.comparison_reason,
        KIRIN_COMPARISON_REASON_STALE
    );
}

#[test]
fn stopped_absolute_frames_survive_io_unavailability_and_delta_lock_contention() {
    let engine = fixture();
    let input: Vec<_> = (0..192_000)
        .flat_map(|n| {
            let sample = 0.1 * (std::f64::consts::TAU * 997.0 * n as f64 / 48_000.0).sin();
            [sample, sample]
        })
        .collect();
    let snapshot = {
        let mut session = engine.meter_session.as_ref().unwrap().lock().unwrap();
        session.push_active(&input);
        session.snapshot()
    };
    assert!(snapshot.current.lufs_m.is_some());
    engine
        .meter_session_publication
        .as_ref()
        .unwrap()
        .publish(snapshot.clone());
    for signal in [KIRIN_SIGNAL_STATE_INACTIVE, KIRIN_SIGNAL_STATE_BYPASSED] {
        engine.set_signal_state(signal);
        for busy in [false, true] {
            let _io_busy = busy.then(|| engine.delta_result.lock().unwrap());
            for _ in 0..4 {
                let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
                assert!(poll(&engine, &mut packet));
                let mut frame = retained_frame();
                assert!(unsafe {
                    kirin_hypha_poll_observatory_frame(&engine as *const _ as *mut _, &mut frame)
                });
                for frame in [&packet.frame, &frame] {
                    assert_eq!(frame.signal_state, signal);
                    assert_eq!(
                        frame.comparison_reason,
                        KIRIN_COMPARISON_REASON_LOCAL_INACTIVE
                    );
                    assert_eq!(frame.delta_available, 0);
                    assert!(frame.delta.lufs.is_nan());
                    assert_eq!(frame.meter.active_frames, snapshot.active_frames);
                    assert_eq!(frame.meter.observed_frames, snapshot.observed_frames);
                    assert_eq!(frame.meter.lufs_m, snapshot.current.lufs_m.unwrap());
                    assert_eq!(
                        frame.meter.max_true_peak,
                        snapshot.summary.max_true_peak.unwrap()
                    );
                }
            }
        }
    }
    engine.set_signal_state(KIRIN_SIGNAL_STATE_ACTIVE);
    let paused = {
        let mut session = engine.meter_session.as_ref().unwrap().lock().unwrap();
        session.pause();
        session.snapshot()
    };
    engine
        .meter_session_publication
        .as_ref()
        .unwrap()
        .publish(paused);
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    assert!(poll(&engine, &mut packet));
    assert_eq!(packet.frame.signal_state, KIRIN_SIGNAL_STATE_INACTIVE);
    assert_eq!(
        packet.frame.comparison_reason,
        KIRIN_COMPARISON_REASON_LOCAL_INACTIVE
    );
    let mut frame = retained_frame();
    assert!(unsafe {
        kirin_hypha_poll_observatory_frame(&engine as *const _ as *mut _, &mut frame)
    });
    assert_eq!(frame.signal_state, KIRIN_SIGNAL_STATE_INACTIVE);
    assert_eq!(frame.meter.lufs_m, snapshot.current.lufs_m.unwrap());
}

#[test]
fn optional_latency_absent_still_publishes_level_and_time_after_complete_windows() {
    use kirin_measure::meter_delta_history::{MeterDeltaHistoryExchange, MeterHistoryTarget};
    use kirin_measure::{LatchedPre, LatchedPreReadiness, MeterSession};
    let engine = fixture();
    let directory = tempfile::tempdir().unwrap();
    let project = directory.path().join("project");
    let instance = project.join("pre");
    std::fs::create_dir_all(&instance).unwrap();
    let json = instance.join("pre.json");
    std::fs::write(&json, br#"{"instance_id":"pre","watch_owner_id":"pre-owner","daw_session_id":"song","signal_state":"active"}"#).unwrap();
    engine.identity.lock().unwrap().instance_id = "post".into();
    *engine.project_hash_cell.write().unwrap() = "project".into();
    *engine.pair_claimed_at.write().unwrap() = 1.0;
    let binding = engine.pair_binding.replace_exact(
        "mix".into(),
        LatchedPre {
            name: "mix".into(),
            instance_id: "pre".into(),
            project_dir: project,
            pre_json: json.clone(),
            daw_session_id: Some("song".into()),
            host_process_id: None,
            readiness: LatchedPreReadiness::Confirmed,
        },
    );
    let target = MeterHistoryTarget::from_pre_json("pre".into(), &json)
        .unwrap()
        .with_post_binding(
            engine.pair_owner.owner_id(),
            "post",
            binding.generation,
            1.0,
        )
        .unwrap();
    let pre_session = Arc::new(Mutex::new(
        MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 31).unwrap(),
    ));
    let pre = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&pre_session));
    let mut complete_level = 0;
    let mut complete_time = 0;
    for slot in 0..44 {
        let input: Vec<f64> = (0..4800)
            .flat_map(|n| {
                let v = 0.1 * (std::f64::consts::TAU * 997.0 * n as f64 / 48000.0).sin();
                [v, v]
            })
            .collect();
        let clock = MeterClockStart {
            position_samples: Some(slot * 4800),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            ..Default::default()
        }; // No latency report, as on AAX and hosts that omit the optional callback.
        pre_session.lock().unwrap().push_active_at(&input, clock);
        let post_input: Vec<_> = input.iter().map(|v| v * 2.0).collect();
        let snapshot = {
            let mut session = engine.meter_session.as_ref().unwrap().lock().unwrap();
            session.push_active_at(&post_input, clock);
            session.snapshot()
        };
        engine
            .meter_session_publication
            .as_ref()
            .unwrap()
            .publish(snapshot);
        pre.service_pre_endpoint("pre", "song", "pre-owner", &instance)
            .unwrap();
        engine
            .meter_delta_history
            .as_ref()
            .unwrap()
            .service_post_endpoint(Some(target.clone()));
        let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
        assert!(poll(&engine, &mut packet));
        if slot < 3 {
            assert!(packet.frame.delta.lufs.is_nan());
        }
        if slot < 29 {
            assert!(packet.frame.delta.psr.is_nan());
        }
        if slot >= 30 {
            assert!((packet.frame.delta.lufs - 6.020599913).abs() < 0.001);
            assert!((packet.frame.delta.true_peak - 6.020599913).abs() < 0.001);
            assert!(packet.frame.delta.psr.abs() < 0.001);
            complete_level += 1;
            let req = super::super::time_ffi::KirinTimeSnapshotRequestV2 {
                version: 2,
                struct_size: std::mem::size_of::<KirinTimeSnapshotRequestV2>() as u32,
                packet_size: std::mem::size_of::<KirinTimeSnapshotV2>() as u32,
                entry_size: std::mem::size_of::<KirinTimeHistoryEntryV2>() as u32,
                duration_frames: 48000,
                main_target: u32::from(KIRIN_TARGET_DELTA),
                resolution: 0,
            };
            let mut time = KirinTimeSnapshotV2::default();
            assert_eq!(
                unsafe {
                    kirin_hypha_poll_time_snapshot_v2(
                        &engine,
                        &req,
                        std::ptr::null_mut(),
                        0,
                        std::ptr::null_mut(),
                        0,
                        &mut time,
                    )
                },
                KIRIN_SNAPSHOT_SUCCESS
            );
            assert!((time.main.current.values[0] - 6.020599913).abs() < 0.001);
            complete_time += 1;
        }
    }
    assert_eq!((complete_level, complete_time), (14, 14));
}
