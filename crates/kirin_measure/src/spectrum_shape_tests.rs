use super::*;

fn tones(low: f32, high: f32) -> Vec<f32> {
    (0..SPECTRUM_WINDOW_SIZE)
        .map(|index| {
            let time = index as f32 / 48_000.0;
            low * (std::f32::consts::TAU * 250.0 * time).sin()
                + high * (std::f32::consts::TAU * 4_000.0 * time).sin()
        })
        .collect()
}

fn independent_windowed_energy(samples: &[f32]) -> f64 {
    let (sum, norm) =
        samples
            .iter()
            .enumerate()
            .fold((0.0_f64, 0.0_f64), |(sum, norm), (index, sample)| {
                let weight = 0.5
                    - 0.5
                        * (std::f64::consts::TAU * index as f64 / (samples.len() - 1) as f64).cos();
                (
                    sum + (f64::from(*sample) * weight).powi(2),
                    norm + weight.powi(2),
                )
            });
    sum / norm
}

fn band_center(index: usize, min_hz: f32, max_hz: f32) -> f32 {
    min_hz * (max_hz / min_hz).powf((index as f32 + 0.5) / SPECTRUM_BAND_COUNT as f32)
}

#[test]
fn windowed_energy_matches_the_aperture_and_stereo_mean_without_another_fft() {
    let left = tones(0.2, 0.1);
    let right = tones(0.4, 0.05);
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let lr = analyzer.analyze(&left, Some(&right), 9_600, 1).unwrap();
    let mid = analyzer
        .analyze_mode(&left, Some(&right), SpectrumChannelMode::Mid, 9_600, 1)
        .unwrap();
    let side = analyzer
        .analyze_mode(&left, Some(&right), SpectrumChannelMode::Side, 9_600, 1)
        .unwrap();
    let mid_samples: Vec<_> = left
        .iter()
        .zip(&right)
        .map(|(l, r)| (l + r) * 0.5)
        .collect();
    let side_samples: Vec<_> = left
        .iter()
        .zip(&right)
        .map(|(l, r)| (l - r) * 0.5)
        .collect();
    let expected_lr =
        (independent_windowed_energy(&left) + independent_windowed_energy(&right)) * 0.5;
    assert!((lr.windowed_energy - expected_lr).abs() < 1.0e-8);
    assert!((mid.windowed_energy - independent_windowed_energy(&mid_samples)).abs() < 1.0e-8);
    assert!((side.windowed_energy - independent_windowed_energy(&side_samples)).abs() < 1.0e-8);
}

#[test]
fn gain_only_is_flat_shape_and_eq_change_is_not_a_gain_estimate() {
    let pre_samples = tones(0.2, 0.2);
    let gained: Vec<_> = pre_samples.iter().map(|sample| sample * 2.0).collect();
    let eq_samples = tones(0.2, 0.4);
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let pre = analyzer.analyze(&pre_samples, None, 9_600, 1).unwrap();
    let gain = analyzer.analyze(&gained, None, 9_600, 1).unwrap();
    let eq = analyzer.analyze(&eq_samples, None, 9_600, 1).unwrap();
    let flat = difference_post_minus_pre(&gain, &pre).unwrap();
    assert!((f64::from(flat.energy_delta_db.unwrap()) - 20.0 * 2.0_f64.log10()).abs() < 1.0e-5);
    for (value, valid) in flat.shape_db.iter().zip(flat.shape_valid) {
        if valid {
            assert!(value.abs() < 0.02);
        }
    }
    let changed = difference_post_minus_pre(&eq, &pre).unwrap();
    let expected_g = 10.0
        * (independent_windowed_energy(&eq_samples) / independent_windowed_energy(&pre_samples))
            .log10();
    assert!((f64::from(changed.energy_delta_db.unwrap()) - expected_g).abs() < 1.0e-5);
    let low = (0..SPECTRUM_BAND_COUNT)
        .find(|&index| {
            changed.shape_valid[index]
                && (230.0..270.0).contains(&band_center(index, pre.min_hz, pre.max_hz))
        })
        .unwrap();
    let high = (0..SPECTRUM_BAND_COUNT)
        .find(|&index| {
            changed.shape_valid[index]
                && (3_800.0..4_200.0).contains(&band_center(index, pre.min_hz, pre.max_hz))
        })
        .unwrap();
    assert!(changed.shape_db[low] < -1.0);
    assert!(changed.shape_db[high] > 1.0);
}

#[test]
fn silence_and_floor_bands_have_no_shape_fact() {
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let silence = analyzer
        .analyze(&[0.0; SPECTRUM_WINDOW_SIZE], None, 9_600, 1)
        .unwrap();
    assert_eq!(silence.windowed_energy, 0.0);
    let signal = analyzer.analyze(&tones(0.2, 0.0), None, 9_600, 1).unwrap();
    let difference = difference_post_minus_pre(&signal, &silence).unwrap();
    assert!(difference.energy_delta_db.is_none());
    assert!(difference.shape_valid.iter().all(|valid| !valid));
    assert!(difference.shape_db.iter().all(|value| *value == 0.0));
    let below_floor = analyzer
        .analyze(&tones(1.0e-7, 0.0), None, 9_600, 1)
        .unwrap();
    let low_level = difference_post_minus_pre(&signal, &below_floor).unwrap();
    assert!(low_level.energy_delta_db.is_none());
    assert!(low_level.shape_valid.iter().all(|valid| !valid));
}
