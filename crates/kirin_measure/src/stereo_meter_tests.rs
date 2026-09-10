use super::*;

const SR: u32 = 48_000;
const FRAMES: usize = SR as usize / 10;

fn observation(left: f64, right: f64) -> Vec<f64> {
    [left, right].into_iter().cycle().take(FRAMES * 2).collect()
}

fn sine_observation(left_peak: f64, right_peak: f64) -> Vec<f64> {
    let mut result = Vec::with_capacity(FRAMES * 2);
    for frame in 0..FRAMES {
        let phase = std::f64::consts::TAU * 997.0 * frame as f64 / SR as f64;
        result.push(phase.sin() * left_peak);
        result.push(phase.sin() * right_peak);
    }
    result
}

fn phase_observation(inverse: bool) -> Vec<f64> {
    sine_observation(0.5, if inverse { -0.5 } else { 0.5 })
}

#[test]
fn in_phase_inverse_and_balance_share_exact_three_second_window() {
    let mut meter = StereoMeter::new(SR, 2).unwrap();
    for _ in 0..29 {
        assert!(meter.push_observation(&observation(0.5, 0.5)));
    }
    assert!(meter.snapshot().correlation.is_none());
    assert!(meter.push_observation(&observation(0.5, 0.5)));
    let in_phase = meter.snapshot();
    assert_eq!(in_phase.balance_state, BalanceState::Numeric);
    assert!(in_phase.balance_db.unwrap().abs() < 1.0e-12);
    assert!((in_phase.correlation.unwrap() - 1.0).abs() < 1.0e-12);

    meter.reset();
    for _ in 0..30 {
        assert!(meter.push_observation(&observation(0.5, -0.5)));
    }
    assert!((meter.snapshot().correlation.unwrap() + 1.0).abs() < 1.0e-12);
}

#[test]
fn one_sided_and_mono_are_explicit_not_invented_numeric_stereo() {
    let mut stereo = StereoMeter::new(SR, 2).unwrap();
    for _ in 0..30 {
        assert!(stereo.push_observation(&observation(0.25, 0.0)));
    }
    let left_only = stereo.snapshot();
    assert_eq!(left_only.balance_state, BalanceState::LeftOnly);
    assert!(left_only.balance_db.is_none());
    assert!(left_only.correlation.is_none());
    assert!(left_only.sample_peak_dbfs[0].is_some());
    assert!(left_only.sample_peak_dbfs[1].is_none());
    assert!(left_only.vu_dbfs[0].is_some());
    assert!(left_only.vu_dbfs[1].is_none());

    let mut mono = StereoMeter::new(SR, 1).unwrap();
    for _ in 0..3 {
        assert!(mono.push_observation(&vec![0.25; FRAMES]));
    }
    let mono = mono.snapshot();
    assert_eq!(mono.channels, 1);
    assert_eq!(mono.balance_state, BalanceState::Unavailable);
    assert!(mono.vu_dbfs[0].is_some());
    assert!(mono.vu_dbfs[1].is_none());
}

#[test]
fn malformed_observation_fails_without_partial_mutation() {
    let mut meter = StereoMeter::new(SR, 2).unwrap();
    let before = meter.snapshot();
    assert!(!meter.push_observation(&[]));
    assert!(!meter.push_observation(&[0.0]));
    assert!(!meter.push_observation(&[0.0, f64::NAN]));
    let after = meter.snapshot();
    assert_eq!(after.clip_events, before.clip_events);
    assert_eq!(after.sample_peak_dbfs, before.sample_peak_dbfs);
    assert_eq!(after.vu_dbfs, before.vu_dbfs);
}

#[test]
fn clip_events_are_channel_specific_contiguous_runs_across_observations() {
    let mut meter = StereoMeter::new(SR, 2).unwrap();
    assert!(meter.push_observation(&observation(1.0, 0.5)));
    assert!(meter.push_observation(&observation(1.2, 0.5)));
    assert_eq!(meter.snapshot().clip_events, [1, 0]);
    assert!(meter.push_observation(&observation(0.5, 0.5)));
    assert!(meter.push_observation(&observation(1.0, -1.0)));
    assert_eq!(meter.snapshot().clip_events, [2, 1]);
}

#[test]
fn vu_is_three_observations_and_sine_calibrated() {
    let mut meter = StereoMeter::new(SR, 2).unwrap();
    const LEFT: f64 = 0.125_892_541_179_416_73; // -18 dBFS peak
    for index in 0..3 {
        assert!(meter.push_observation(&sine_observation(LEFT, LEFT * 0.5)));
        if index < 2 {
            assert!(meter.snapshot().vu_dbfs.iter().all(Option::is_none));
        }
    }
    let vu = meter.snapshot().vu_dbfs;
    assert!((vu[0].unwrap() + 18.0).abs() < 0.01);
    assert!((vu[1].unwrap() + 24.020_599_913).abs() < 0.01);
}

#[test]
fn instant_true_peak_releases_without_erasing_four_observation_recent_peak() {
    let mut meter = StereoMeter::new(SR, 2).unwrap();
    assert!(meter.push_observation(&sine_observation(0.8, 0.4)));
    assert!(meter.push_observation(&sine_observation(0.08, 0.04)));
    // The first lower block contains the reconstructed boundary from the loud block. The next
    // exact block is wholly low while the separate 400 ms rail still retains the transient.
    assert!(meter.push_observation(&sine_observation(0.08, 0.04)));
    let snapshot = meter.snapshot();
    let instant = snapshot.instant_true_peak_dbtp[0].unwrap();
    let recent = snapshot.true_peak_dbtp[0].unwrap();
    assert!(
        instant < recent - 12.0,
        "instant={instant}, recent={recent}"
    );
    assert!(recent > -3.0);
}

#[test]
fn peak_hold_and_true_peak_are_per_channel_and_reset_only_explicitly() {
    let mut meter = StereoMeter::new(SR, 2).unwrap();
    for _ in 0..3 {
        assert!(meter.push_observation(&observation(0.5, 0.25)));
    }
    let held = meter.snapshot();
    assert!((held.sample_peak_hold_dbfs[0].unwrap() - 20.0 * 0.5_f64.log10()).abs() < 1e-9);
    assert!(held.true_peak_dbtp[0].unwrap() > held.true_peak_dbtp[1].unwrap());
    meter.reset();
    let reset = meter.snapshot();
    assert_eq!(reset.clip_events, [0, 0]);
    assert!(reset.sample_peak_hold_dbfs.iter().all(Option::is_none));
    assert!(reset.max_true_peak_dbtp.iter().all(Option::is_none));
    assert!(reset.instant_true_peak_dbtp.iter().all(Option::is_none));
    assert!(reset.vu_dbfs.iter().all(Option::is_none));
}

#[test]
fn clear_peak_clip_holds_preserves_live_windows_and_relatches_continuing_clip() {
    let mut meter = StereoMeter::new(SR, 2).unwrap();
    for _ in 0..3 {
        assert!(meter.push_observation(&observation(1.1, 0.4)));
    }
    let before = meter.snapshot();
    assert_eq!(before.clip_events, [1, 0]);
    assert_eq!(meter.session_clip_events(), [1, 0]);
    assert!(before.max_true_peak_dbtp[0].is_some());

    meter.clear_peak_clip_holds();
    let cleared = meter.snapshot();
    assert_eq!(cleared.clip_events, [0, 0]);
    assert!(cleared.max_true_peak_dbtp.iter().all(Option::is_none));
    assert_eq!(cleared.true_peak_dbtp, before.true_peak_dbtp);
    assert_eq!(
        cleared.instant_true_peak_dbtp,
        before.instant_true_peak_dbtp
    );
    assert_eq!(cleared.vu_dbfs, before.vu_dbfs);
    assert_eq!(cleared.sample_peak_hold_dbfs, before.sample_peak_hold_dbfs);
    assert_eq!(meter.session_clip_events(), [1, 0]);

    assert!(meter.push_observation(&observation(1.1, 0.4)));
    let relatched = meter.snapshot();
    assert_eq!(relatched.clip_events, [1, 0]);
    assert_eq!(meter.session_clip_events(), [1, 0]);
    assert!(relatched.max_true_peak_dbtp[0].is_some());
}

#[test]
fn field_density_has_mid_side_orientation_and_an_exact_three_second_window() {
    const CENTRE: usize = STEREO_FIELD_SIZE / 2;
    let mut meter = StereoMeter::new(SR, 2).unwrap();
    for _ in 0..30 {
        assert!(meter.push_observation(&phase_observation(false)));
    }
    let mid = meter.snapshot();
    assert_eq!(mid.field_observation_count, 30);
    assert!(mid.field_density.iter().any(|value| *value > 0));
    for (index, value) in mid.field_density.iter().copied().enumerate() {
        if value > 0 {
            assert_eq!(index % STEREO_FIELD_SIZE, CENTRE);
        }
    }
    for _ in 0..30 {
        assert!(meter.push_observation(&phase_observation(true)));
    }
    for (index, value) in meter.snapshot().field_density.iter().copied().enumerate() {
        if value > 0 {
            assert_eq!(index / STEREO_FIELD_SIZE, CENTRE);
        }
    }
    meter.reset();
    assert!(meter
        .snapshot()
        .field_density
        .iter()
        .all(|value| *value == 0));
}
