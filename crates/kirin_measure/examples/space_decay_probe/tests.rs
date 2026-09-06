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
