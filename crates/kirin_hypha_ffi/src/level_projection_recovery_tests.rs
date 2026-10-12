use super::*;

#[test]
fn observatory_keeps_absolute_update_when_only_delta_acquisition_is_busy() {
    let engine = fixture();
    let snapshot = {
        let mut meter = engine.meter_session.as_ref().unwrap().lock().unwrap();
        let audio: Vec<_> = (0..24000)
            .flat_map(|n| {
                let sample = 0.1 * (std::f64::consts::TAU * 997.0 * n as f64 / 48000.0).sin();
                [sample, sample]
            })
            .collect();
        meter.push_active(&audio);
        meter.snapshot()
    };
    engine
        .meter_session_publication
        .as_ref()
        .unwrap()
        .publish(snapshot);
    let _guard = engine.write_role.lock().unwrap();
    let mut frame = retained_frame();
    assert!(unsafe {
        kirin_hypha_poll_observatory_frame(&engine as *const _ as *mut _, &mut frame)
    });
    assert_ne!(frame.version, 777);
    assert_eq!(frame.meter.observed_frames, 24000);
    assert!(frame.meter.lufs_m.is_finite());
    assert_retained_comparison(&frame);
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    packet.frame = retained_frame();
    assert!(poll(&engine, &mut packet));
    assert_eq!(packet.frame.meter.observed_frames, 24000);
    assert_eq!(packet.frame.meter.lufs_m, frame.meter.lufs_m);
    assert_retained_comparison(&packet.frame);
}

#[test]
fn active_audio_replaces_delayed_local_stop_reason_with_measurement_wait() {
    let engine = fixture();
    *engine.delta_result.lock().unwrap() = DeltaResult {
        mode: DeltaMode::Stale,
        comparison: ComparisonSnapshot {
            state: ComparisonState::Rejected,
            reason: ComparisonReason::LocalInactive,
            generation: 2,
            identity: 23,
        },
        ..Default::default()
    };
    let mut frame = retained_frame();
    assert!(unsafe {
        kirin_hypha_poll_observatory_frame(&engine as *const _ as *mut _, &mut frame)
    });
    assert_eq!(
        frame.comparison_reason,
        KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT
    );
    engine.set_signal_state(KIRIN_SIGNAL_STATE_BYPASSED);
    assert!(unsafe {
        kirin_hypha_poll_observatory_frame(&engine as *const _ as *mut _, &mut frame)
    });
    assert_eq!(frame.comparison_reason, 12); // Additive local-bypass ABI tag.
}

#[test]
fn active_audio_with_a_delayed_paused_meter_is_waiting_without_inventing_fresh_facts() {
    let engine = fixture();
    let snapshot = {
        let mut meter = engine.meter_session.as_ref().unwrap().lock().unwrap();
        meter.push_active(&vec![0.1; 48000]);
        meter.pause();
        meter.snapshot()
    };
    engine
        .meter_session_publication
        .as_ref()
        .unwrap()
        .publish(snapshot);
    let mut frame = retained_frame();
    assert!(unsafe {
        kirin_hypha_poll_observatory_frame(&engine as *const _ as *mut _, &mut frame)
    });
    assert_eq!(
        frame.comparison_reason,
        KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT
    );
    assert_eq!(frame.comparison_state, KIRIN_COMPARISON_STATE_PREPARING);
    assert_eq!(frame.signal_state, KIRIN_SIGNAL_STATE_INACTIVE);
    assert_eq!(frame.delta_available, 0);
    engine.set_signal_state(KIRIN_SIGNAL_STATE_INACTIVE);
    assert!(unsafe {
        kirin_hypha_poll_observatory_frame(&engine as *const _ as *mut _, &mut frame)
    });
    assert_eq!(
        frame.comparison_reason,
        KIRIN_COMPARISON_REASON_LOCAL_INACTIVE
    );
}
