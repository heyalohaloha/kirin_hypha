use super::*;

fn exponential(rate: u32, seconds: f64, slope: f64) -> Vec<f32> {
    (0..(f64::from(rate) * seconds) as usize)
        .map(|index| (0.5 * 10.0_f64.powf(slope * index as f64 / f64::from(rate) / 20.0)) as f32)
        .collect()
}

#[test]
fn constant_power_uses_window_energy_not_normalized_mean_ratio() {
    let samples = vec![0.5; 48_000];
    let facts = analyze(&samples, 48_000, 1, 0, 1_000, -90.0).unwrap();
    assert!((facts.early_db.unwrap() - 10.0 * (80.0_f64 / 170.0).log10()).abs() < 1e-10);
    assert!(facts.fit.unwrap().d20_seconds.is_none());
}

#[test]
fn known_exponential_gives_equivalent_time_across_native_rates() {
    for rate in [8_000, 44_100, 48_000, 96_000, 192_000, 768_000] {
        let samples = exponential(rate, 1.0, -40.0);
        let facts = analyze(&samples, rate, 1, 0, 1_000, -90.0).unwrap();
        let fit = facts.fit.unwrap();
        assert_eq!(fit.points, 100);
        assert!((fit.slope_db_per_second + 40.0).abs() < 1e-5);
        assert!((fit.d20_seconds.unwrap() - 0.5).abs() < 1e-6);
        assert!(fit.r_squared > 0.999_999);
    }
}

#[test]
fn stereo_antiphase_and_constant_gain_do_not_change_facts() {
    let samples = exponential(48_000, 1.0, -40.0);
    let stereo: Vec<_> = samples.iter().flat_map(|&x| [x * 0.5, -x * 0.5]).collect();
    let mono = analyze(&samples, 48_000, 1, 0, 1_000, -120.0).unwrap();
    let pair = analyze(&stereo, 48_000, 2, 0, 1_000, -120.0).unwrap();
    assert!((mono.early_db.unwrap() - pair.early_db.unwrap()).abs() < 1e-8);
    assert!(
        (mono.fit.unwrap().d20_seconds.unwrap() - pair.fit.unwrap().d20_seconds.unwrap()).abs()
            < 1e-8
    );
}

#[test]
fn silence_incomplete_windows_floor_and_nonfinite_fail_without_padding() {
    assert!(analyze(&[], 48_000, 1, 0, 1_000, -90.0)
        .unwrap()
        .early_db
        .is_none());
    let silence = vec![0.0; 48_000];
    let facts = analyze(&silence, 48_000, 1, 0, 1_000, -90.0).unwrap();
    assert!(facts.early_db.is_none() && facts.fit.is_none());
    assert!(analyze(&[f32::NAN], 48_000, 1, 0, 1_000, -90.0).is_err());
    assert!(analyze(&silence, 48_000, 0, 0, 1_000, -90.0).is_err());
    assert!(analyze(&silence, 7_999, 1, 0, 1_000, -90.0).is_err());
    let tail = exponential(48_000, 1.0, -120.0);
    assert_eq!(
        analyze(&tail, 48_000, 1, 0, 1_000, -60.0)
            .unwrap()
            .fit_reason,
        Some("fit_at_or_below_supplied_floor")
    );
}

#[test]
fn short_or_shallow_fit_never_extrapolates_unobserved_twenty_db() {
    let samples = exponential(48_000, 1.0, -10.0);
    assert!(analyze(&samples, 48_000, 1, 0, 90, -90.0)
        .unwrap()
        .fit
        .is_none());
    assert!(analyze(&samples, 48_000, 1, 0, 1_000, -90.0)
        .unwrap()
        .fit
        .unwrap()
        .d20_seconds
        .is_none());
    for (start, end) in [(10, 10), (1, 1_000), (0, 6_010)] {
        assert_eq!(
            analyze(&samples, 48_000, 1, start, end, -90.0)
                .unwrap()
                .fit_reason,
            Some("fit_range_invalid")
        );
    }
}

#[test]
fn boundaries_round_from_origin_even_at_fractional_hop_rates() {
    assert_eq!(boundary(44_150, 10), Some(442));
    assert_eq!(boundary(44_150, 20), Some(883));
    assert_ne!(
        boundary(44_150, 10).unwrap() * 2,
        boundary(44_150, 20).unwrap()
    );
    let rate = 44_150;
    let samples = exponential(rate, 1.0, -40.0);
    let fit = analyze(&samples, rate, 1, 0, 1_000, -90.0)
        .unwrap()
        .fit
        .unwrap();
    assert!((fit.d20_seconds.unwrap() - 0.5).abs() < 1e-6);
}

fn levels(rate: u32, levels_db: &[f64]) -> Vec<f32> {
    let frames = boundary(rate, 10).unwrap();
    levels_db
        .iter()
        .flat_map(|db| std::iter::repeat_n(10.0_f64.powf(db / 20.0) as f32, frames))
        .collect()
}

#[test]
fn local_profile_keeps_multiple_decays_separate_from_single_interval_d20() {
    let pcm = levels(
        48_000,
        &[0.0, -3.0, -6.0, -4.0, -7.0, -10.0, -5.0, -8.0, -12.0],
    );
    let profile = local_decay_profile(&pcm, 48_000, 1, 0, 90, -90.0, &[1.5, 6.0]).unwrap();
    assert_eq!(profile.valid_bin_count, 9);
    assert_eq!(profile.below_floor_bin_count, 0);
    assert_eq!(profile.valid_span_count, 1);
    assert_eq!(profile.thresholds[0].episodes.len(), 3);
    assert_eq!(profile.thresholds[1].episodes.len(), 1);
    for (episode, expected) in profile.thresholds[0].episodes.iter().zip([6.0, 6.0, 7.0]) {
        assert!((episode.observed_fall_db - expected).abs() < 1e-5);
        assert!(episode.slope_db_per_second < 0.0);
    }
    assert_eq!(profile.thresholds[0].episodes[0].end_reason, "recovery");
    assert_eq!(profile.thresholds[0].episodes[2].end_reason, "interval_end");
    assert!((profile.thresholds[1].episodes[0].observed_fall_db - 12.0).abs() < 1e-5);
}

#[test]
fn floor_bins_split_local_spans_without_bridging_or_padding() {
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
    assert_eq!(profile.thresholds[0].episodes[1].end_reason, "interval_end");
}

#[test]
fn local_profile_is_invariant_to_native_rate_stereo_antiphase_and_fixed_gain() {
    for rate in [44_100, 48_000, 96_000] {
        for gain_db in [0.0, -12.0] {
            let shifted = [0.0, -3.0, -7.0, -1.0, -5.0, -9.0].map(|level| level + gain_db);
            let mono = levels(rate, &shifted);
            let stereo = mono
                .iter()
                .flat_map(|sample| [*sample, -*sample])
                .collect::<Vec<_>>();
            for (pcm, channels) in [(&mono, 1), (&stereo, 2)] {
                let profile =
                    local_decay_profile(pcm, rate, channels, 0, 60, -90.0, &[5.0]).unwrap();
                let episodes = &profile.thresholds[0].episodes;
                assert_eq!(episodes.len(), 2);
                assert!((episodes[0].observed_fall_db - 7.0).abs() < 1e-5);
                assert!((episodes[1].observed_fall_db - 8.0).abs() < 1e-5);
                assert_eq!(
                    episodes[0].trough_window_end_exclusive_sample
                        - episodes[0].peak_window_start_sample,
                    boundary(rate, 30).unwrap()
                );
                assert_eq!(episodes[0].end_reason, "recovery");
                assert_eq!(episodes[1].end_reason, "interval_end");
            }
        }
    }
}

#[test]
fn local_profile_refuses_unordered_or_nonfinite_diagnostic_thresholds() {
    let pcm = levels(48_000, &[0.0; 10]);
    for thresholds in [&[][..], &[2.0, 1.0], &[f64::NAN], &[0.0]] {
        assert_eq!(
            local_decay_profile(&pcm, 48_000, 1, 0, 100, -90.0, thresholds).unwrap_err(),
            "local_profile_parameters_invalid"
        );
    }
}
