use super::meter_session_ffi::{
    kirin_hypha_clear_meter_peak_clip_holds, kirin_hypha_reset_meter_session,
};
use super::*;
use kirin_measure::BalanceState;

#[test]
fn snapshot_layout_and_mapping_are_stable() {
    assert_eq!(std::mem::size_of::<KirinMeterSession>(), 872);
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, channel_clip_latched),
        90
    );
    assert_eq!(std::mem::offset_of!(KirinMeterSession, field_density), 200);
    assert_eq!(std::mem::offset_of!(KirinMeterSession, max_lufs_m), 832);
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, channel_vu_dbfs),
        840
    );
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, channel_instant_true_peak_dbtp),
        856
    );
    assert_eq!(std::mem::size_of::<KirinMeterHistoryRange>(), 24);
    assert_eq!(std::mem::size_of::<KirinMeterHistoryEntry>(), 184);
    assert_eq!(std::mem::size_of::<KirinObservatoryFrame>(), 1112);
    let current = MeasureResult {
        lufs_m: Some(-14.2),
        lufs_s: Some(-14.8),
        true_peak: Some(-1.1),
        ..MeasureResult::default()
    };
    let snapshot = MeterSessionSnapshot {
        generation: 3,
        state: MeterSessionState::Paused,
        sample_rate: 48_000,
        active_frames: 96_123,
        observed_frames: 96_000,
        current,
        max_lufs_m: Some(-10.6),
        maximum: MeasureResult::default(),
        summary: SessionSummary {
            lufs_i: Some(-15.0),
            lra: Some(4.2),
            max_true_peak: Some(-0.8),
        },
        plr: Some(14.2),
        stereo: kirin_measure::StereoMeterSnapshot {
            channels: 2,
            sample_peak_dbfs: [Some(-1.0), Some(-2.0)],
            sample_peak_hold_dbfs: [Some(-0.5), Some(-1.5)],
            true_peak_dbtp: [Some(-0.8), Some(-1.8)],
            instant_true_peak_dbtp: [Some(-0.9), Some(-1.9)],
            max_true_peak_dbtp: [Some(-0.3), Some(-1.3)],
            vu_dbfs: [Some(-18.0), Some(-20.0)],
            clip_events: [2, 1],
            clip_latched: [true, false],
            balance_db: Some(0.75),
            balance_state: BalanceState::Numeric,
            correlation: Some(0.91),
            field_density: {
                let mut density = [0; STEREO_FIELD_BINS];
                density[312] = 211;
                density
            },
            field_observation_count: 30,
        },
    };
    let mapped = to_c_meter_session(&snapshot);
    assert_eq!(mapped.state, KIRIN_METER_SESSION_PAUSED);
    assert_eq!(mapped.active_frames, 96_123);
    assert_eq!(mapped.observed_frames, 96_000);
    assert_eq!(mapped.lufs_m, -14.2);
    assert_eq!(mapped.max_lufs_m, -10.6);
    assert_eq!(mapped.lufs_s, -14.8);
    assert_eq!(mapped.lufs_i, -15.0);
    assert_eq!(mapped.lra, 4.2);
    assert_eq!(mapped.true_peak, -1.1);
    assert_eq!(mapped.max_true_peak, -0.8);
    assert_eq!(mapped.plr, 14.2);
    assert_eq!(mapped.channels, 2);
    assert_eq!(mapped.balance_state, KIRIN_BALANCE_NUMERIC);
    assert_eq!(mapped.sample_peak_dbfs, [-1.0, -2.0]);
    assert_eq!(mapped.channel_vu_dbfs, [-18.0, -20.0]);
    assert_eq!(mapped.channel_instant_true_peak_dbtp, [-0.9, -1.9]);
    assert_eq!(mapped.clip_events, [2, 1]);
    assert_eq!(mapped.channel_clip_latched, [1, 0]);
    assert_eq!(mapped.balance_db, 0.75);
    assert_eq!(mapped.correlation, 0.91);
    assert_eq!(mapped.field_size, KIRIN_STEREO_FIELD_SIZE);
    assert_eq!(mapped.field_observation_count, 30);
    assert_eq!(mapped.field_density[312], 211);

    let history = to_c_history_entry(MeterHistoryEntry {
        resolution: MeterHistoryResolution::Hz1,
        generation: 3,
        run_id: 7,
        observation_count: 10,
        first_observed_frames: 4_800,
        last_observed_frames: 48_000,
        first_timeline_endpoint_samples: Some(104_800),
        last_timeline_endpoint_samples: None,
        timeline_source: CaptureClockSource::ProjectTimeline,
        clip_event_count: [3, 1],
        lufs_m: MeterHistoryRange {
            min: Some(-16.0),
            max: Some(-13.0),
            mean: Some(-14.5),
        },
        lufs_s: MeterHistoryRange::default(),
        true_peak: MeterHistoryRange::default(),
        correlation: MeterHistoryRange::default(),
        plr: MeterHistoryRange {
            min: Some(12.0),
            max: Some(14.0),
            mean: Some(13.0),
        },
    });
    assert_eq!(history.resolution, KIRIN_METER_HISTORY_1_HZ);
    assert_eq!(history.observation_count, 10);
    assert_eq!(history.clip_event_count, [3, 1]);
    assert_eq!(history.first_timeline_endpoint_samples, 104_800);
    assert_eq!(history.plr.mean, 13.0);
    assert_eq!(history.last_timeline_endpoint_samples, i64::MIN);
    assert_eq!(history.lufs_m.min, -16.0);
    assert!(history.lufs_s.mean.is_nan());
}

#[test]
fn lra_readiness_never_presents_an_early_finite_value_as_ready() {
    let mut snapshot = MeterSessionSnapshot {
        generation: 1,
        state: MeterSessionState::Active,
        sample_rate: 48_000,
        active_frames: 48_000 * 59,
        observed_frames: 48_000 * 59,
        current: MeasureResult::default(),
        max_lufs_m: Some(-11.0),
        maximum: MeasureResult::default(),
        summary: SessionSummary {
            lufs_i: Some(-14.0),
            lra: Some(0.0),
            max_true_peak: Some(-1.0),
        },
        plr: Some(13.0),
        stereo: kirin_measure::StereoMeterSnapshot {
            channels: 2,
            sample_peak_dbfs: [None; 2],
            sample_peak_hold_dbfs: [None; 2],
            true_peak_dbtp: [None; 2],
            instant_true_peak_dbtp: [None; 2],
            max_true_peak_dbtp: [None; 2],
            vu_dbfs: [None; 2],
            clip_events: [0; 2],
            clip_latched: [false; 2],
            balance_db: None,
            balance_state: BalanceState::Unavailable,
            correlation: None,
            field_density: [0; STEREO_FIELD_BINS],
            field_observation_count: 0,
        },
    };
    assert_eq!(lra_readiness(&snapshot).0, KIRIN_LRA_WARMING);
    snapshot.active_frames = 48_000 * 60;
    assert_eq!(lra_readiness(&snapshot).0, KIRIN_LRA_READY);
    snapshot.summary.lra = None;
    assert_eq!(lra_readiness(&snapshot).0, KIRIN_LRA_UNAVAILABLE);
    snapshot.state = MeterSessionState::Empty;
    assert_eq!(lra_readiness(&snapshot).0, KIRIN_LRA_UNAVAILABLE);
}

#[test]
fn observatory_delta_requires_active_freshness_even_when_stale_values_are_finite() {
    let mut delta = KirinDelta {
        mode: KIRIN_DELTA_MODE_ACTIVE,
        lufs: 1.0,
        true_peak: f64::NAN,
        crest: f64::NAN,
        psr: f64::NAN,
        n_prime_total: f64::NAN,
        sharpness: f64::NAN,
        lufs_s: f64::NAN,
        psb_bark: [f64::NAN; 20],
    };
    assert!(delta_has_finite_fact(&delta));
    delta.mode = delta_mode_to_abi(&DeltaMode::Stale);
    assert!(!delta_has_finite_fact(&delta));
}

#[test]
fn null_meter_session_calls_fail_closed_without_touching_output() {
    let mut frame: KirinObservatoryFrame = unsafe { std::mem::zeroed() };
    frame.version = 41;
    assert!(!unsafe { kirin_hypha_poll_observatory_frame(std::ptr::null_mut(), &mut frame) });
    assert_eq!(frame.version, 41);
    let mut out = KirinMeterSession {
        generation: 41,
        active_frames: 0,
        observed_frames: 0,
        sample_rate: 0,
        state: 0,
        reserved: [0; 3],
        lufs_m: 0.0,
        lufs_s: 0.0,
        lufs_i: 0.0,
        lra: 0.0,
        true_peak: 0.0,
        max_true_peak: 0.0,
        plr: 0.0,
        channels: 0,
        balance_state: 0,
        channel_clip_latched: [0; 2],
        stereo_reserved: [0; 4],
        sample_peak_dbfs: [0.0; 2],
        sample_peak_hold_dbfs: [0.0; 2],
        channel_true_peak_dbtp: [0.0; 2],
        channel_max_true_peak_dbtp: [0.0; 2],
        clip_events: [0; 2],
        balance_db: 0.0,
        correlation: 0.0,
        field_size: 0,
        field_observation_count: 0,
        field_reserved: [0; 6],
        field_density: [0; KIRIN_STEREO_FIELD_BINS],
        max_lufs_m: 0.0,
        channel_vu_dbfs: [0.0; 2],
        channel_instant_true_peak_dbtp: [0.0; 2],
    };
    assert!(!unsafe { kirin_hypha_poll_meter_session(std::ptr::null_mut(), &mut out) });
    let mut history_count = 41_u32;
    assert!(!unsafe {
        kirin_hypha_poll_meter_history(
            std::ptr::null_mut(),
            KIRIN_METER_HISTORY_10_HZ,
            std::ptr::null_mut(),
            0,
            &mut history_count,
        )
    });
    assert_eq!(history_count, 41);
    assert!(!unsafe {
        kirin_hypha_poll_meter_history_decimated(
            std::ptr::null_mut(),
            KIRIN_METER_HISTORY_10_HZ,
            300,
            std::ptr::null_mut(),
            0,
            &mut history_count,
        )
    });
    assert_eq!(history_count, 41);
    assert!(!unsafe {
        kirin_hypha_poll_meter_delta_history(
            std::ptr::null_mut(),
            KIRIN_METER_HISTORY_10_HZ,
            std::ptr::null_mut(),
            0,
            &mut history_count,
        )
    });
    assert_eq!(history_count, 41);
    assert!(!unsafe { kirin_hypha_reset_meter_session(std::ptr::null_mut()) });
    assert!(!unsafe { kirin_hypha_clear_meter_peak_clip_holds(std::ptr::null_mut()) });
    assert_eq!(out.generation, 41);
}

#[test]
fn meter_poll_reads_completed_publication_while_live_session_is_locked() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    let live_session = engine.meter_session.as_ref().unwrap();
    let _live_guard = live_session.lock().unwrap();

    let published = engine.poll_meter_session().unwrap();

    assert_eq!(published.state, MeterSessionState::Empty);
    assert_eq!(published.sample_rate, 48_000);
    assert_eq!(published.active_frames, 0);
}

#[test]
fn delta_history_abi_is_post_only_and_empty_is_a_valid_fact() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    assert!(engine
        .poll_meter_delta_history(MeterHistoryResolution::Hz10, 10)
        .is_none());
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Pre);
    assert!(engine
        .poll_meter_delta_history(MeterHistoryResolution::Hz10, 10)
        .is_none());
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert_eq!(
        engine
            .poll_meter_delta_history(MeterHistoryResolution::Hz10, 10)
            .unwrap(),
        Vec::<MeterHistoryEntry>::new()
    );
    let mut count = 41_u32;
    assert!(unsafe {
        kirin_hypha_poll_meter_delta_history(
            std::ptr::from_ref(&engine).cast_mut(),
            KIRIN_METER_HISTORY_10_HZ,
            std::ptr::null_mut(),
            0,
            &mut count,
        )
    });
    assert_eq!(count, 0);
}

#[test]
fn live_measure_worker_advances_pauses_and_resets_independent_session() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    engine.set_signal_state(KIRIN_SIGNAL_STATE_ACTIVE);
    let mut samples = Vec::with_capacity(48_000 * 2);
    for frame in 0..48_000 {
        let sample = (2.0 * std::f32::consts::PI * 1_000.0 * frame as f32 / 48_000.0).sin() * 1.1;
        samples.extend_from_slice(&[sample, sample]);
    }
    for (index, chunk) in samples.chunks(480 * 2).enumerate() {
        let deadline = std::time::Instant::now() + Duration::from_secs(1);
        loop {
            engine.note_capture_window(
                true,
                (index * 480) as i64,
                480,
                CaptureClockSource::ProjectTimeline,
            );
            if engine.push_samples_transaction(chunk, 2) {
                break;
            }
            assert!(std::time::Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(1));
        }
    }

    let deadline = std::time::Instant::now() + Duration::from_secs(2);
    let active = loop {
        if let Some(snapshot) = engine
            .poll_meter_session()
            .filter(|snapshot| snapshot.active_frames == 48_000)
        {
            break snapshot;
        }
        assert!(std::time::Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(5));
    };
    assert_eq!(active.state, MeterSessionState::Active);
    assert_eq!(active.observed_frames, 48_000);
    assert!(active.current.lufs_m.is_some());
    assert!(active.summary.lufs_i.is_some());
    assert_eq!(active.stereo.channels, 2);
    assert!(active.stereo.sample_peak_dbfs.iter().all(Option::is_some));
    assert!(active
        .stereo
        .sample_peak_hold_dbfs
        .iter()
        .all(Option::is_some));
    assert!(active.stereo.true_peak_dbtp.iter().all(Option::is_some));
    assert!(active.stereo.max_true_peak_dbtp.iter().all(Option::is_some));
    assert!(active.stereo.clip_events.iter().all(|count| *count > 0));
    assert_eq!(active.stereo.clip_latched, [true, true]);
    assert!(active.stereo.correlation.is_none());
    let history = engine
        .poll_meter_history(MeterHistoryResolution::Hz10, 20)
        .unwrap();
    assert_eq!(history.len(), 10);
    assert_eq!(history[0].last_timeline_endpoint_samples, Some(4_800));
    assert_eq!(history[9].last_timeline_endpoint_samples, Some(48_000));
    let one_second = engine
        .poll_meter_history(MeterHistoryResolution::Hz1, 20)
        .unwrap();
    assert_eq!(one_second.len(), 1);
    assert_eq!(one_second[0].observation_count, 10);

    assert!(unsafe {
        kirin_hypha_clear_meter_peak_clip_holds(std::ptr::from_ref(&engine).cast_mut())
    });
    let cleared = engine.poll_meter_session().unwrap();
    assert_eq!(cleared.generation, active.generation);
    assert_eq!(cleared.active_frames, active.active_frames);
    assert_eq!(cleared.observed_frames, active.observed_frames);
    assert_eq!(cleared.summary.lufs_i, active.summary.lufs_i);
    assert_eq!(cleared.summary.lra, active.summary.lra);
    assert_eq!(cleared.summary.max_true_peak, active.summary.max_true_peak);
    assert_eq!(cleared.stereo.clip_events, active.stereo.clip_events);
    assert_eq!(cleared.stereo.clip_latched, [false, false]);
    assert!(cleared
        .stereo
        .max_true_peak_dbtp
        .iter()
        .all(Option::is_none));
    let mut ffi_cleared: KirinMeterSession = unsafe { std::mem::zeroed() };
    assert!(unsafe {
        kirin_hypha_poll_meter_session(std::ptr::from_ref(&engine).cast_mut(), &mut ffi_cleared)
    });
    assert_eq!(ffi_cleared.clip_events, active.stereo.clip_events);
    assert_eq!(ffi_cleared.channel_clip_latched, [0, 0]);

    let mut ffi_entries: Vec<std::mem::MaybeUninit<KirinMeterHistoryEntry>> =
        std::iter::repeat_with(std::mem::MaybeUninit::uninit)
            .take(12)
            .collect();
    let mut ffi_count = 0_u32;
    assert!(unsafe {
        kirin_hypha_poll_meter_history(
            std::ptr::from_ref(&engine).cast_mut(),
            KIRIN_METER_HISTORY_10_HZ,
            ffi_entries.as_mut_ptr().cast(),
            12,
            &mut ffi_count,
        )
    });
    assert_eq!(ffi_count, 10);
    let ffi_last = unsafe { ffi_entries[9].assume_init_ref() };
    assert_eq!(ffi_last.resolution, KIRIN_METER_HISTORY_10_HZ);
    assert_eq!(ffi_last.last_timeline_endpoint_samples, 48_000);

    ffi_count = 77;
    assert!(!unsafe {
        kirin_hypha_poll_meter_history(
            std::ptr::from_ref(&engine).cast_mut(),
            99,
            ffi_entries.as_mut_ptr().cast(),
            12,
            &mut ffi_count,
        )
    });
    assert_eq!(ffi_count, 77);

    engine.set_signal_state(KIRIN_SIGNAL_STATE_INACTIVE);
    let deadline = std::time::Instant::now() + Duration::from_secs(1);
    loop {
        if engine
            .poll_meter_session()
            .is_some_and(|snapshot| snapshot.state == MeterSessionState::Paused)
        {
            break;
        }
        assert!(std::time::Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(5));
    }
    while !engine.reset_meter_session() {
        assert!(std::time::Instant::now() < deadline);
        std::thread::yield_now();
    }
    let reset = engine.poll_meter_session().unwrap();
    assert_eq!(reset.state, MeterSessionState::Empty);
    assert_eq!(reset.generation, active.generation + 1);
    assert_eq!(reset.active_frames, 0);
}
