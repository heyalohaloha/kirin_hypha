use super::*;

fn exponential(rate: u32, seconds: f64, slope: f64) -> Vec<f32> {
    (0..(f64::from(rate) * seconds) as usize)
        .map(|index| (0.5 * 10.0_f64.powf(slope * index as f64 / f64::from(rate) / 20.0)) as f32)
        .collect()
}

fn levels(rate: u32, levels_db: &[f64]) -> Vec<f32> {
    let frames = space_decay_boundary(rate, SPACE_DECAY_BIN_MILLISECONDS).unwrap();
    levels_db
        .iter()
        .flat_map(|db| std::iter::repeat_n(10.0_f64.powf(db / 20.0) as f32, frames))
        .collect()
}

#[test]
fn constant_power_uses_window_energy_not_mean_ratio() {
    let samples = vec![0.5; 48_000];
    let facts = analyze_space_decay(&samples, 48_000, 1, 0, 1_000, -90.0).unwrap();
    assert!((facts.early_db.unwrap() - 10.0 * (80.0_f64 / 170.0).log10()).abs() < 1e-10);
    assert!(facts.fit.unwrap().d20_seconds.is_none());
}

#[test]
fn known_exponential_is_native_rate_invariant() {
    for rate in [8_000, 44_100, 48_000, 96_000, 192_000, 768_000] {
        let samples = exponential(rate, 1.0, -40.0);
        let fit = analyze_space_decay(&samples, rate, 1, 0, 1_000, -90.0)
            .unwrap()
            .fit
            .unwrap();
        assert_eq!(fit.points, 100);
        assert!((fit.slope_db_per_second + 40.0).abs() < 1e-5);
        assert!((fit.d20_seconds.unwrap() - 0.5).abs() < 1e-6);
        assert!(fit.r_squared > 0.999_999);
    }
}

#[test]
fn stereo_phase_and_fixed_gain_do_not_change_facts() {
    let samples = exponential(48_000, 1.0, -40.0);
    let stereo = samples
        .iter()
        .flat_map(|&sample| [sample * 0.25, -sample * 0.25])
        .collect::<Vec<_>>();
    let mono = analyze_space_decay(&samples, 48_000, 1, 0, 1_000, -120.0).unwrap();
    let pair = analyze_space_decay(&stereo, 48_000, 2, 0, 1_000, -120.0).unwrap();
    assert!((mono.early_db.unwrap() - pair.early_db.unwrap()).abs() < 1e-8);
    assert!(
        (mono.fit.unwrap().d20_seconds.unwrap() - pair.fit.unwrap().d20_seconds.unwrap()).abs()
            < 1e-8
    );
}

#[test]
fn invalid_incomplete_floor_and_nonfinite_inputs_fail_closed() {
    let silence = vec![0.0; 48_000];
    let facts = analyze_space_decay(&silence, 48_000, 1, 0, 1_000, -90.0).unwrap();
    assert!(facts.early_db.is_none() && facts.fit.is_none());
    assert_eq!(
        facts.early_reason,
        Some("window_at_or_below_supplied_floor")
    );
    assert_eq!(facts.fit_reason, Some("fit_at_or_below_supplied_floor"));
    for (pcm, rate, channels, floor) in [
        (&[f32::NAN][..], 48_000, 1, -90.0),
        (&silence[..], 7_999, 1, -90.0),
        (&silence[..], 48_000, 0, -90.0),
        (&silence[..], 48_000, 1, f64::NAN),
    ] {
        assert_eq!(
            analyze_space_decay(pcm, rate, channels, 0, 1_000, floor).unwrap_err(),
            "invalid_input"
        );
    }
    let short = vec![0.5; 4_000];
    let facts = analyze_space_decay(&short, 48_000, 1, 0, 1_000, -90.0).unwrap();
    assert_eq!(facts.early_reason, Some("incomplete_window"));
    assert_eq!(facts.fit_reason, Some("incomplete_window"));
}

#[test]
fn shallow_or_short_fit_never_invents_twenty_decibels() {
    let samples = exponential(48_000, 1.0, -10.0);
    assert!(analyze_space_decay(&samples, 48_000, 1, 0, 1_000, -90.0)
        .unwrap()
        .fit
        .unwrap()
        .d20_seconds
        .is_none());
    assert_eq!(
        analyze_space_decay(&samples, 48_000, 1, 0, 90, -90.0)
            .unwrap()
            .fit_reason,
        Some("fewer_than_ten_points")
    );
    for (start, end) in [(10, 10), (1, 1_000), (0, 6_010)] {
        assert_eq!(
            analyze_space_decay(&samples, 48_000, 1, start, end, -90.0)
                .unwrap()
                .fit_reason,
            Some("fit_range_invalid")
        );
    }
}

#[test]
fn strict_d20_policy_keeps_each_rejection_reason_distinct() {
    let policy = SpaceD20Policy {
        minimum_points: 10,
        minimum_r_squared: 0.98,
        maximum_rise_db: 1.0,
    };
    let accepted =
        fit_space_decay(&exponential(48_000, 1.0, -40.0), 48_000, 1, 0, 1_000, 1e-12).unwrap();
    assert!((qualify_space_d20(&accepted, policy).unwrap() - 0.5).abs() < 1e-6);

    let reject = |mut fit: SpaceDecayFit, expected| {
        fit.d20_seconds = (fit.slope_db_per_second < 0.0
            && fit.observed_fall_db >= SPACE_D20_OBSERVED_FALL_DB)
            .then(|| SPACE_D20_OBSERVED_FALL_DB / -fit.slope_db_per_second);
        assert_eq!(qualify_space_d20(&fit, policy), Err(expected));
    };
    let mut fit = accepted;
    fit.points = 9;
    reject(fit, SpaceD20Rejection::FewerThanRequiredPoints);
    fit = accepted;
    fit.slope_db_per_second = 1.0;
    reject(fit, SpaceD20Rejection::NonNegativeSlope);
    fit = accepted;
    fit.observed_fall_db = 19.999;
    reject(fit, SpaceD20Rejection::InsufficientObservedFall);
    fit = accepted;
    fit.largest_rise_db = 1.001;
    reject(fit, SpaceD20Rejection::ExcessiveRise);
    fit = accepted;
    fit.r_squared = 0.979;
    reject(fit, SpaceD20Rejection::RegressionFit);
    assert_eq!(
        SpaceD20Policy {
            minimum_points: 9,
            ..policy
        }
        .validate(),
        Err("invalid_d20_policy")
    );
    assert_eq!(
        qualify_space_d20(
            &accepted,
            SpaceD20Policy {
                minimum_points: 9,
                ..policy
            }
        ),
        Err(SpaceD20Rejection::InvalidPolicy)
    );
}

#[test]
fn fractional_hop_boundaries_are_rounded_from_origin() {
    assert_eq!(space_decay_boundary(44_150, 10), Some(442));
    assert_eq!(space_decay_boundary(44_150, 20), Some(883));
    let samples = exponential(44_150, 1.0, -40.0);
    let fit = analyze_space_decay(&samples, 44_150, 1, 0, 1_000, -90.0)
        .unwrap()
        .fit
        .unwrap();
    assert!((fit.d20_seconds.unwrap() - 0.5).abs() < 1e-6);
}

#[test]
fn local_profile_keeps_multiple_decays_separate_from_linear_d20() {
    let pcm = levels(
        48_000,
        &[0.0, -3.0, -6.0, -4.0, -7.0, -10.0, -5.0, -8.0, -12.0],
    );
    let profile = local_decay_profile(&pcm, 48_000, 1, 0, 90, -90.0, &[1.5, 6.0]).unwrap();
    assert_eq!(profile.valid_bin_count, 9);
    assert_eq!(profile.valid_span_count, 1);
    assert_eq!(profile.thresholds[0].episodes.len(), 3);
    assert_eq!(profile.thresholds[1].episodes.len(), 1);
    for (episode, expected) in profile.thresholds[0].episodes.iter().zip([6.0, 6.0, 7.0]) {
        assert!((episode.observed_fall_db - expected).abs() < 1e-5);
        assert!(episode.slope_db_per_second < 0.0);
    }
    assert_eq!(profile.thresholds[0].episodes[0].end_reason, "recovery");
    assert_eq!(profile.thresholds[0].episodes[2].end_reason, "interval_end");
}

#[test]
fn floor_bins_split_local_spans_without_bridging() {
    let pcm = levels(48_000, &[0.0, -3.0, -100.0, -1.0, -5.0]);
    let profile = local_decay_profile(&pcm, 48_000, 1, 0, 50, -90.0, &[2.0]).unwrap();
    assert_eq!(profile.valid_bin_count, 4);
    assert_eq!(profile.below_floor_bin_count, 1);
    assert_eq!(profile.valid_span_count, 2);
    assert_eq!(profile.thresholds[0].episodes.len(), 2);
    assert_eq!(
        profile.thresholds[0].episodes[0].end_reason,
        "floor_boundary"
    );
}

#[test]
fn local_profile_is_rate_phase_and_gain_invariant() {
    for rate in [44_100, 48_000, 96_000, 192_000] {
        for gain_db in [0.0, -12.0] {
            let shifted = [0.0, -3.0, -7.0, -1.0, -5.0, -9.0].map(|db| db + gain_db);
            let mono = levels(rate, &shifted);
            let stereo = mono
                .iter()
                .flat_map(|sample| [*sample, -*sample])
                .collect::<Vec<_>>();
            for (pcm, channels) in [(&mono, 1), (&stereo, 2)] {
                let profile =
                    local_decay_profile(pcm, rate, channels, 0, 60, -90.0, &[5.0]).unwrap();
                assert_eq!(profile.thresholds[0].episodes.len(), 2);
                assert!((profile.thresholds[0].episodes[0].observed_fall_db - 7.0).abs() < 1e-5);
                assert_eq!(
                    profile.thresholds[0].episodes[0].trough_window_end_exclusive_sample
                        - profile.thresholds[0].episodes[0].peak_window_start_sample,
                    space_decay_boundary(rate, 30).unwrap()
                );
            }
        }
    }
}

#[test]
fn local_profile_rejects_unordered_or_nonfinite_thresholds() {
    let pcm = levels(48_000, &[0.0; 10]);
    for thresholds in [&[][..], &[2.0, 1.0], &[f64::NAN], &[0.0]] {
        assert_eq!(
            local_decay_profile(&pcm, 48_000, 1, 0, 100, -90.0, thresholds).unwrap_err(),
            "local_profile_parameters_invalid"
        );
    }
}
