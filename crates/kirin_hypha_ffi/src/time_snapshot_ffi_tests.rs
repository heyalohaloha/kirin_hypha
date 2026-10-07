use super::*;
use kirin_measure::meter_session::TimeWirePoint;
use kirin_measure::{CaptureClockSource, MeterClockStart};
use std::cell::Cell;
use std::time::Duration;

thread_local! { static BOUNDARY: Cell<u8> = const { Cell::new(0) }; }
pub(super) fn at_boundary(engine: &KirinHyphaEngine) {
    match BOUNDARY.replace(0) {
        1 => engine
            .meter_session
            .as_ref()
            .unwrap()
            .lock()
            .unwrap()
            .reset(),
        2 => {
            engine.pair_binding.replace_name("changed".into());
        }
        4 => {
            *engine.project_hash_cell.write().unwrap() = "other-project".into();
        }
        5 => engine
            .meter_session
            .as_ref()
            .unwrap()
            .lock()
            .unwrap()
            .retire_time_worker_span(),
        3 => {
            engine
                .meter_session
                .as_ref()
                .unwrap()
                .lock()
                .unwrap()
                .push_active_at(
                    &[0.1; 9600],
                    MeterClockStart {
                        position_samples: Some(4800),
                        epoch: Some(1),
                        source: CaptureClockSource::ProjectTimeline,
                        ..Default::default()
                    },
                );
        }
        _ => {}
    }
}
fn request(target: u32) -> KirinTimeSnapshotRequestV2 {
    KirinTimeSnapshotRequestV2 {
        version: 2,
        struct_size: std::mem::size_of::<KirinTimeSnapshotRequestV2>() as u32,
        packet_size: std::mem::size_of::<KirinTimeSnapshotV2>() as u32,
        entry_size: std::mem::size_of::<KirinTimeHistoryEntryV2>() as u32,
        duration_frames: 48000,
        main_target: target,
        resolution: 0,
    }
}
fn call(
    engine: &mut KirinHyphaEngine,
    req: &KirinTimeSnapshotRequestV2,
    history: &mut [KirinTimeHistoryEntryV2],
    packet: &mut KirinTimeSnapshotV2,
) -> bool {
    unsafe {
        kirin_hypha_poll_time_snapshot_v2(
            engine,
            req,
            history.as_mut_ptr(),
            history.len() as u32,
            std::ptr::null_mut(),
            0,
            packet,
        ) == KIRIN_SNAPSHOT_SUCCESS
    }
}
fn engine() -> KirinHyphaEngine {
    let engine = KirinHyphaEngine::new(48000, ChannelLayout::stereo());
    // Deterministic local producer fixture: no identity, role writer or IO endpoint is enabled.
    // Stop the unused background worker before directly feeding MeterSession facts.
    engine.watchdog_shutdown.store(true, Ordering::Release);
    engine.shutdown.store(true, Ordering::Release);
    if let Some(worker) = engine.watchdog_handle.lock().unwrap().take() {
        worker.join().unwrap();
    }
    engine.set_signal_state(1);
    engine
        .meter_session
        .as_ref()
        .unwrap()
        .lock()
        .unwrap()
        .push_active_at(
            &[0.1; 9600],
            MeterClockStart {
                position_samples: Some(0),
                epoch: Some(1),
                source: CaptureClockSource::ProjectTimeline,
                ..Default::default()
            },
        );
    engine
}

#[test]
fn partial_resume_or_seek_cannot_revive_the_previous_run_current() {
    for after_pause in [true, false] {
        let mut engine = engine();
        let req = request(u32::from(KIRIN_TARGET_PRE));
        let mut packet = KirinTimeSnapshotV2::default();
        let mut history = [KirinTimeHistoryEntryV2::default(); 16];
        assert!(call(&mut engine, &req, &mut history, &mut packet));
        assert_eq!(packet.main.current.state, KIRIN_TIME_CURRENT_LIVE);
        assert_ne!(packet.main.current.finite_mask, 0);
        let before = packet.main.current;
        {
            let mut session = engine.meter_session.as_ref().unwrap().lock().unwrap();
            if after_pause {
                session.pause();
            }
            // No new complete 100 ms slot: old finite values are still within their TTL,
            // but pause/resume or a changed clock run has already retired their authority.
            assert!(session.push_active_at(
                &[0.1; 20],
                MeterClockStart {
                    position_samples: Some(if after_pause { 4800 } else { 96000 }),
                    epoch: Some(1),
                    source: CaptureClockSource::ProjectTimeline,
                    ..Default::default()
                },
            ));
        }
        assert!(call(&mut engine, &req, &mut history, &mut packet));
        assert_eq!(packet.local_cutoff, before.cutoff);
        assert_eq!(packet.main.current.finite_mask, 0);
        assert_eq!(packet.main.current.state, KIRIN_TIME_CURRENT_WAITING);
        assert!(packet
            .main
            .current
            .values
            .iter()
            .all(|value| value.is_nan()));
        assert!(
            packet.main.history_count > 0,
            "same-source history remains held"
        );
        assert_eq!(packet.main.history_hold, 1);
    }
}

#[test]
fn sized_version_null_and_short_requests_leave_packet_arrays_and_canaries_unchanged() {
    let mut engine = engine();
    let mut history = [KirinTimeHistoryEntryV2 {
        epoch: 777,
        ..Default::default()
    }; 8];
    let mut packet = KirinTimeSnapshotV2 {
        version: 999,
        ..Default::default()
    };
    for variant in 0..4 {
        let mut req = request(u32::from(KIRIN_TARGET_PRE));
        match variant {
            0 => req.version = 999,
            1 => req.struct_size -= 1,
            2 => req.packet_size -= 1,
            _ => req.entry_size -= 1,
        }
        assert!(!call(&mut engine, &req, &mut history, &mut packet));
        assert_eq!(packet.version, 999);
        assert!(history.iter().all(|p| p.epoch == 777));
    }
    let prefix = [2u32, 8];
    assert!(unsafe {
        kirin_hypha_poll_time_snapshot_v2(
            &engine,
            prefix.as_ptr().cast(),
            history.as_mut_ptr(),
            8,
            std::ptr::null_mut(),
            0,
            &mut packet,
        ) != KIRIN_SNAPSHOT_SUCCESS
    });
    assert!(unsafe {
        kirin_hypha_poll_time_snapshot_v2(
            &engine,
            std::ptr::null(),
            history.as_mut_ptr(),
            8,
            std::ptr::null_mut(),
            0,
            &mut packet,
        ) != KIRIN_SNAPSHOT_SUCCESS
    });
    assert!(unsafe {
        kirin_hypha_poll_time_snapshot_v2(
            &engine,
            &request(u32::from(KIRIN_TARGET_PRE)),
            std::ptr::null_mut(),
            1,
            std::ptr::null_mut(),
            0,
            &mut packet,
        ) != KIRIN_SNAPSHOT_SUCCESS
    });
    assert!(unsafe {
        kirin_hypha_poll_time_snapshot_v2(
            &engine,
            &request(u32::from(KIRIN_TARGET_PRE)),
            history.as_mut_ptr(),
            1201,
            std::ptr::null_mut(),
            0,
            &mut packet,
        ) != KIRIN_SNAPSHOT_SUCCESS
    });
    assert_eq!(packet.version, 999);
    assert!(history.iter().all(|p| p.epoch == 777));
}

#[test]
fn actual_poll_cutoff_source_races_and_busy_session_have_atomic_output() {
    let mut engine = engine();
    let req = request(u32::from(KIRIN_TARGET_PRE));
    let mut history = [KirinTimeHistoryEntryV2 {
        epoch: 777,
        ..Default::default()
    }; 8];
    let mut packet = KirinTimeSnapshotV2 {
        version: 999,
        ..Default::default()
    };
    for action in [1, 2, 4, 5] {
        BOUNDARY.set(action);
        assert!(!call(&mut engine, &req, &mut history, &mut packet));
        assert_eq!(packet.version, 999);
        assert!(history.iter().all(|p| p.epoch == 777));
    }
    let meter = Arc::clone(engine.meter_session.as_ref().unwrap());
    let _busy = meter.lock().unwrap();
    assert!(!call(&mut engine, &req, &mut history, &mut packet));
    assert_eq!(packet.version, 999);
    assert!(history.iter().all(|p| p.epoch == 777));
}

#[test]
fn ordinary_c_progress_does_not_reject_the_captured_packet_and_compact_psr_has_zero_history() {
    let mut engine = engine();
    BOUNDARY.set(3);
    let mut history = [KirinTimeHistoryEntryV2::default(); 8];
    let mut packet = KirinTimeSnapshotV2::default();
    assert!(call(
        &mut engine,
        &request(u32::from(KIRIN_TARGET_PRE)),
        &mut history,
        &mut packet
    ));
    assert_eq!(packet.local_cutoff, 4800);
    assert_eq!(packet.main.current.cutoff, 4800);
    assert_eq!(packet.psr.history_count, 0);
    assert!(history[..packet.main.history_count as usize]
        .iter()
        .all(|p| p.last_observed <= 4800));
    assert_eq!(
        engine
            .meter_session
            .as_ref()
            .unwrap()
            .lock()
            .unwrap()
            .snapshot()
            .observed_frames,
        9600
    );
}

fn joined(now: Instant) -> (TimeRawPoint, Authority, TimeComparisonView) {
    let span = TimeSourceSpan {
        epoch: 1,
        incarnation: 2,
        generation: 3,
        token: 4,
        sample_rate: 48000,
        channels: 2,
    };
    let point = TimeRawPoint {
        wire: TimeWirePoint {
            span,
            run: 8,
            observed: 48000,
            endpoint: Some(48000),
            clock: 1,
            usable: true,
            values: [Some(1.0); 6],
        },
        completed: now,
    };
    let authority = Authority {
        pair: crate::pair_binding::PairObservationAuthority {
            generation: 7,
            selection_intent: true,
            exact: Some(crate::pair_binding::ExactPairBindingSnapshot {
                generation: 7,
                project_hash: "p".into(),
                pre_instance_id: "pre".into(),
            }),
        },
        signal: 1,
        role: Some(PluginDataRole::Post),
        post_id: "post".into(),
        post_project: "p".into(),
        owner: "owner".into(),
        claim: 5,
        span_token: 4,
        audition_epoch: 0,
        audition_active: false,
    };
    let view = TimeComparisonView {
        binding_revision: 7,
        pre_instance_id: "pre".into(),
        project_hash: "p".into(),
        owner_id: "owner".into(),
        post_instance_id: "post".into(),
        claimed_at_bits: 5,
        pre_span: TimeSourceSpan { epoch: 9, ..span },
        post_span: span,
        pre_run: 6,
        post_run: 8,
        cutoff: 48000,
        point: Some(point.clone()),
        reason: TimeComparisonReason::Active,
        history: Vec::new(),
    };
    (point, authority, view)
}

#[test]
fn original_ttl_axis_limit_latest_none_owner_claim_and_source_proof_are_independent() {
    let start = Instant::now();
    let (point, authority, view) = joined(start);
    let mut latest = point.clone();
    latest.wire.observed += 9600;
    for ms in [299, 399] {
        let component = compared(
            Some(&view),
            &authority,
            point.wire.span,
            Some(&latest),
            true,
            start + Duration::from_millis(ms),
            0,
        );
        assert_eq!(component.current.state, KIRIN_TIME_CURRENT_LIVE);
        assert!(component.current.values[3].is_finite());
        assert!((component.current.remaining_ms - (400 - ms) as f64).abs() < 0.0001);
        assert_eq!(component.current.cutoff, 48000);
    }
    let expired = compared(
        Some(&view),
        &authority,
        point.wire.span,
        Some(&latest),
        true,
        start + Duration::from_millis(400),
        0,
    );
    assert_eq!(expired.current.state, KIRIN_TIME_CURRENT_EXPIRED);
    assert_eq!(expired.current.remaining_ms, 0.0);
    latest.wire.values[3] = None;
    let none = psr_component(compared(
        Some(&view),
        &authority,
        point.wire.span,
        Some(&latest),
        true,
        start,
        0,
    ));
    assert_eq!(none.current.state, KIRIN_TIME_CURRENT_MISSING);
    assert!(none.current.values[3].is_nan());
    latest.wire.observed = point.wire.observed + 19200;
    let far = compared(
        Some(&view),
        &authority,
        point.wire.span,
        Some(&latest),
        true,
        start,
        0,
    );
    assert_eq!(far.current.state, KIRIN_TIME_CURRENT_MISSING);
    for variant in 0..4 {
        let mut changed = view.clone();
        match variant {
            0 => changed.owner_id = "other".into(),
            1 => changed.claimed_at_bits += 1,
            2 => changed.post_span.token += 1,
            _ => changed.binding_revision += 1,
        }
        assert!(!comparison_matches(&changed, &authority, point.wire.span));
        let waiting = compared(
            Some(&changed),
            &authority,
            point.wire.span,
            Some(&point),
            true,
            start,
            0,
        );
        assert_eq!(waiting.current.state, KIRIN_TIME_CURRENT_WAITING);
        assert_eq!(waiting.current.finite_mask, 0);
    }
}

#[path = "time_snapshot_status_tests.rs"]
mod status_tests;
