use super::*;

fn deterministic_noise(length: usize) -> Vec<f32> {
    let mut state = 0x4d59_5df4_d0f3_3173_u64;
    (0..length)
        .map(|_| {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            let unit = ((state >> 40) as u32) as f32 / ((1_u32 << 24) - 1) as f32;
            (unit * 2.0 - 1.0) * 0.25
        })
        .collect()
}

#[test]
fn forty_four_one_uses_real_bin_floor_and_its_own_nyquist() {
    let mut analyzer = SpectrumAnalyzer::new(44_100).unwrap();
    let layout = analyzer.layout();
    let frame = analyzer
        .analyze(&vec![0.0; layout.aperture_samples], None, 44_100, 1)
        .unwrap();
    let first_bin = 44_100.0 / layout.fft_size as f32;
    assert_eq!(frame.sample_rate, 44_100);
    assert_eq!(frame.aperture_samples, 3_763);
    assert_eq!(frame.fft_size, 8_192);
    assert!((frame.min_hz - first_bin.max(SPECTRUM_MIN_HZ)).abs() < 1.0e-6);
    assert!(frame.max_hz < 22_050.0);
}

#[test]
fn host_rate_layout_keeps_one_observation_time_without_resampling() {
    let expected = [
        (44_100, 3_763, 8_192),
        (48_000, 4_096, 8_192),
        (88_200, 7_526, 16_384),
        (96_000, 8_192, 16_384),
        (176_400, 15_053, 32_768),
        (192_000, 16_384, 32_768),
        (384_000, 32_768, 65_536),
    ];
    for (sample_rate, aperture_samples, fft_size) in expected {
        let layout = SpectrumLayout::new(sample_rate).unwrap();
        assert_eq!(layout.sample_rate, sample_rate);
        assert_eq!(layout.aperture_samples, aperture_samples);
        assert_eq!(layout.fft_size, fft_size);
        let aperture_ms = layout.aperture_samples as f64 * 1_000.0 / sample_rate as f64;
        assert!(
            (aperture_ms - 85.333_333).abs() < 0.012,
            "{sample_rate} Hz aperture was {aperture_ms:.6} ms"
        );
        assert!((35.14..35.18).contains(&layout.approximate_below_hz));
    }

    let reference = SpectrumLayout::new(48_000).unwrap();
    assert_eq!(reference.aperture_samples, SPECTRUM_WINDOW_SIZE);
    assert_eq!(reference.fft_size, SPECTRUM_FFT_SIZE);
    assert_eq!(
        reference.approximate_below_hz.to_bits(),
        35.15625_f32.to_bits()
    );
}

#[test]
fn host_rate_delta_remains_exact_for_identity_and_scalar_gain() {
    const GAIN_DB: f32 = -6.0;
    const GAIN: f32 = 0.501_187_2;
    for sample_rate in [44_100_u32, 48_000, 96_000, 192_000, 384_000] {
        let layout = SpectrumLayout::new(sample_rate).unwrap();
        let pre_samples = deterministic_noise(layout.aperture_samples);
        let post_samples = pre_samples
            .iter()
            .map(|sample| *sample * GAIN)
            .collect::<Vec<_>>();
        let mut analyzer = SpectrumAnalyzer::new(sample_rate).unwrap();
        let pre = analyzer
            .analyze(&pre_samples, None, layout.aperture_samples as i64, 1)
            .unwrap();
        let identity = difference_post_minus_pre(&pre, &pre).unwrap();
        assert!(identity.raw_db.iter().all(|value| value.to_bits() == 0));
        let post = analyzer
            .analyze(&post_samples, None, layout.aperture_samples as i64, 1)
            .unwrap();
        let difference = difference_post_minus_pre(&post, &pre).unwrap();
        let visible = pre
            .dbfs
            .iter()
            .zip(difference.raw_db)
            .filter_map(|(pre_dbfs, delta)| (*pre_dbfs > -100.0).then_some(delta))
            .collect::<Vec<_>>();
        assert!(visible.len() > SPECTRUM_BAND_COUNT / 2);
        let maximum_error = visible
            .iter()
            .map(|delta| (*delta - GAIN_DB).abs())
            .fold(0.0_f32, f32::max);
        assert!(
            maximum_error < 0.001,
            "{sample_rate} Hz scalar delta error was {maximum_error:.6} dB"
        );
    }
}

#[test]
#[ignore = "release-mode host-rate FFT performance gate; run explicitly with --nocapture"]
fn host_rate_fft_layout_keeps_four_x_pair_headroom() {
    use std::hint::black_box;
    use std::time::Instant;

    for sample_rate in [48_000_u32, 96_000, 192_000, 384_000] {
        let mut analyzer = SpectrumAnalyzer::new(sample_rate).unwrap();
        let aperture = analyzer.aperture_samples();
        let left = (0..aperture)
            .map(|index| {
                (std::f32::consts::TAU * 997.0 * index as f32 / sample_rate as f32).sin() * 0.5
            })
            .collect::<Vec<_>>();
        let right = left.iter().map(|sample| -*sample).collect::<Vec<_>>();
        for iteration in 0..12 {
            black_box(
                analyzer
                    .analyze(&left, Some(&right), iteration * aperture as i64, 1)
                    .unwrap(),
            );
        }
        let mut micros = Vec::with_capacity(100);
        for iteration in 0..100 {
            let started = Instant::now();
            black_box(
                analyzer
                    .analyze(&left, Some(&right), iteration * aperture as i64, 1)
                    .unwrap(),
            );
            micros.push(started.elapsed().as_secs_f64() * 1_000_000.0);
        }
        micros.sort_by(f64::total_cmp);
        let mean = micros.iter().sum::<f64>() / micros.len() as f64;
        let p99 = micros[98];
        let pair_p99_ms = p99 * 2.0 / 1_000.0;
        let two_pair_p99_ms = pair_p99_ms * 2.0;
        let four_x_headroom_ms = 1_000.0 / SPECTRUM_PRESENTATION_HZ as f64 / 4.0;
        eprintln!(
            "{sample_rate} Hz Spectrum: aperture={aperture}, FFT={}, mean={mean:.2} us, \
             p99={p99:.2} us, PRE+POST p99={pair_p99_ms:.3} ms, \
             two-slot p99={two_pair_p99_ms:.3} ms",
            analyzer.fft_size()
        );
        assert!(
            two_pair_p99_ms < four_x_headroom_ms,
            "{sample_rate} Hz two-slot p99 {two_pair_p99_ms:.3} ms exceeded \
             {four_x_headroom_ms:.3} ms four-times-headroom gate"
        );
    }
}
