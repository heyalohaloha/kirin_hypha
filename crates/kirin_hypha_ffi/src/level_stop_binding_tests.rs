use super::*;

pub(super) fn assert_stopped(engine: &KirinHyphaEngine, reason: u8) {
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    let mut frame = retained_frame();
    assert!(poll(engine, &mut packet));
    assert!(unsafe {
        kirin_hypha_poll_observatory_frame(engine as *const _ as *mut _, &mut frame)
    });
    for frame in [&frame, &packet.frame] {
        assert_eq!(frame.signal_state, KIRIN_SIGNAL_STATE_INACTIVE);
        assert_eq!(frame.comparison_state, KIRIN_COMPARISON_STATE_REJECTED);
        assert_eq!(frame.comparison_reason, reason);
        assert_eq!(frame.delta_available, 0);
        assert!(frame.delta.lufs.is_nan());
    }
    let request = KirinTimeSnapshotRequestV2 {
        version: 2,
        struct_size: std::mem::size_of::<KirinTimeSnapshotRequestV2>() as u32,
        packet_size: std::mem::size_of::<KirinTimeSnapshotV2>() as u32,
        entry_size: std::mem::size_of::<KirinTimeHistoryEntryV2>() as u32,
        main_target: u32::from(KIRIN_TARGET_DELTA),
        duration_frames: 0,
        resolution: 0,
    };
    let mut time = KirinTimeSnapshotV2::default();
    assert_eq!(
        unsafe {
            kirin_hypha_poll_time_snapshot_v2(
                engine,
                &request,
                std::ptr::null_mut(),
                0,
                std::ptr::null_mut(),
                0,
                &mut time,
            )
        },
        KIRIN_SNAPSHOT_SUCCESS
    );
    assert_eq!(time.main.current.state, KIRIN_TIME_CURRENT_STOPPED);
    assert_eq!(time.main.current.finite_mask, 0);
}

#[test]
fn stop_reason_uses_exact_binding_even_after_io_no_pre_or_holding() {
    let engine = fixture(); // Exact PRE with an empty human name.
    engine.set_signal_state(KIRIN_SIGNAL_STATE_INACTIVE);
    for mode in [
        DeltaMode::Active,
        DeltaMode::NoPre,
        DeltaMode::Stale,
        DeltaMode::PreInactive,
    ] {
        *engine.delta_result.lock().unwrap() = DeltaResult {
            mode,
            ..Default::default()
        };
        assert_stopped(&engine, KIRIN_COMPARISON_REASON_LOCAL_INACTIVE);
    }
    // A real release/deletion of the selection is distinct from an IO freshness failure.
    engine.pair_binding.replace_name(String::new());
    assert_stopped(&engine, KIRIN_COMPARISON_REASON_NO_PAIR);
    engine.pair_binding.replace_name("missing".into());
    assert_stopped(&engine, KIRIN_COMPARISON_REASON_NO_PAIR);
}

#[test]
fn measure_pause_overrides_io_no_pre_but_pre_only_stop_keeps_its_reason() {
    let engine = fixture();
    let snapshot = {
        let mut session = engine.meter_session.as_ref().unwrap().lock().unwrap();
        session.push_active(&[0.1; 9600]);
        session.pause();
        session.snapshot()
    };
    engine
        .meter_session_publication
        .as_ref()
        .unwrap()
        .publish(snapshot);
    *engine.delta_result.lock().unwrap() = DeltaResult::default();
    assert_stopped(&engine, KIRIN_COMPARISON_REASON_LOCAL_INACTIVE);
    let snapshot = {
        let mut session = engine.meter_session.as_ref().unwrap().lock().unwrap();
        session.push_active(&[0.1; 9600]);
        session.snapshot()
    };
    engine
        .meter_session_publication
        .as_ref()
        .unwrap()
        .publish(snapshot);
    *engine.delta_result.lock().unwrap() = DeltaResult {
        mode: DeltaMode::PreInactive,
        ..Default::default()
    };
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    assert!(poll(&engine, &mut packet));
    assert_eq!(packet.frame.signal_state, KIRIN_SIGNAL_STATE_ACTIVE);
    assert_eq!(
        packet.frame.comparison_reason,
        KIRIN_COMPARISON_REASON_PRE_INACTIVE
    );
}
