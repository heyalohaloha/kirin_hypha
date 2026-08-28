use super::*;

fn sine(sample_rate: u32, frequency: f32, phase: f32) -> Vec<f32> {
    (0..SPECTRUM_WINDOW_SIZE)
        .map(|index| {
            (std::f32::consts::TAU * frequency * index as f32 / sample_rate as f32 + phase).sin()
        })
        .collect()
}

#[derive(Clone, Copy)]
struct ReferencePeakingEq {
    b0: f64,
    b1: f64,
    b2: f64,
    a1: f64,
    a2: f64,
    z1: f64,
    z2: f64,
}

impl ReferencePeakingEq {
    fn new(sample_rate: f64, frequency: f64, q: f64, gain_db: f64) -> Self {
        let amplitude = 10.0_f64.powf(gain_db / 40.0);
        let omega = std::f64::consts::TAU * frequency / sample_rate;
        let alpha = omega.sin() / (2.0 * q);
        let a0 = 1.0 + alpha / amplitude;
        Self {
            b0: (1.0 + alpha * amplitude) / a0,
            b1: (-2.0 * omega.cos()) / a0,
            b2: (1.0 - alpha * amplitude) / a0,
            a1: (-2.0 * omega.cos()) / a0,
            a2: (1.0 - alpha / amplitude) / a0,
            z1: 0.0,
            z2: 0.0,
        }
    }

    fn process(&mut self, input: f32) -> f32 {
        let output = self.b0 * f64::from(input) + self.z1;
        self.z1 = self.b1 * f64::from(input) - self.a1 * output + self.z2;
        self.z2 = self.b2 * f64::from(input) - self.a2 * output;
        output as f32
    }

    fn response_db(self, sample_rate: f64, frequency: f64) -> f64 {
        let omega = std::f64::consts::TAU * frequency / sample_rate;
        let numerator_real = self.b0 + self.b1 * omega.cos() + self.b2 * (2.0 * omega).cos();
        let numerator_imag = -self.b1 * omega.sin() - self.b2 * (2.0 * omega).sin();
        let denominator_real = 1.0 + self.a1 * omega.cos() + self.a2 * (2.0 * omega).cos();
        let denominator_imag = -self.a1 * omega.sin() - self.a2 * (2.0 * omega).sin();
        let magnitude_squared = (numerator_real * numerator_real + numerator_imag * numerator_imag)
            / (denominator_real * denominator_real + denominator_imag * denominator_imag);
        10.0 * magnitude_squared.log10()
    }
}

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

fn spectrum_band_frequency(index: usize, minimum_hz: f32, maximum_hz: f32) -> f32 {
    let ratio = maximum_hz / minimum_hz;
    minimum_hz * ratio.powf((index as f32 + 0.5) / SPECTRUM_BAND_COUNT as f32)
}

#[test]
fn eighty_hz_peaking_eq_matrix_pins_latency_first_accuracy() {
    const SAMPLE_RATE: u32 = 48_000;
    const CENTER_HZ: f32 = 80.0;
    const CADENCE: usize = SAMPLE_RATE as usize / SPECTRUM_PRESENTATION_HZ as usize;
    const FRAME_COUNT: usize = 24;
    const WARMUP: usize = SPECTRUM_WINDOW_SIZE * 2;
    let total = WARMUP + SPECTRUM_WINDOW_SIZE + CADENCE * (FRAME_COUNT - 1);
    let input = deterministic_noise(total);

    let mut pre_analyzer = SpectrumAnalyzer::new(SAMPLE_RATE).unwrap();
    let pre_frames = (0..FRAME_COUNT)
        .map(|frame| {
            let end = WARMUP + SPECTRUM_WINDOW_SIZE + frame * CADENCE;
            pre_analyzer
                .analyze(&input[end - SPECTRUM_WINDOW_SIZE..end], None, end as i64, 1)
                .unwrap()
        })
        .collect::<Vec<_>>();
    let minimum_hz = pre_frames[0].min_hz;
    let maximum_hz = pre_frames[0].max_hz;
    let center_band = (0..SPECTRUM_BAND_COUNT)
        .min_by(|left, right| {
            let left_error =
                (spectrum_band_frequency(*left, minimum_hz, maximum_hz) - CENTER_HZ).abs();
            let right_error =
                (spectrum_band_frequency(*right, minimum_hz, maximum_hz) - CENTER_HZ).abs();
            left_error.total_cmp(&right_error)
        })
        .unwrap();

    for q in [0.7_f64, 2.0, 4.0, 8.0] {
        for gain_db in [-18.0_f64, -12.0, -6.0, -3.0, 3.0, 6.0, 12.0, 18.0] {
            let reference =
                ReferencePeakingEq::new(f64::from(SAMPLE_RATE), f64::from(CENTER_HZ), q, gain_db);
            let mut filter = reference;
            let output = input
                .iter()
                .map(|sample| filter.process(*sample))
                .collect::<Vec<_>>();
            let mut post_analyzer = SpectrumAnalyzer::new(SAMPLE_RATE).unwrap();
            let mut mean = [0.0_f64; SPECTRUM_BAND_COUNT];
            let mut center_values = Vec::with_capacity(FRAME_COUNT);
            for (frame, pre) in pre_frames.iter().enumerate() {
                let end = WARMUP + SPECTRUM_WINDOW_SIZE + frame * CADENCE;
                let post = post_analyzer
                    .analyze(
                        &output[end - SPECTRUM_WINDOW_SIZE..end],
                        None,
                        end as i64,
                        1,
                    )
                    .unwrap();
                let difference = difference_post_minus_pre(&post, pre).unwrap();
                for (total, value) in mean.iter_mut().zip(difference.raw_db) {
                    *total += f64::from(value);
                }
                center_values.push(f64::from(difference.raw_db[center_band]));
            }
            for value in &mut mean {
                *value /= FRAME_COUNT as f64;
            }

            let center_frequency = spectrum_band_frequency(center_band, minimum_hz, maximum_hz);
            let expected_center =
                reference.response_db(f64::from(SAMPLE_RATE), f64::from(center_frequency));
            let center_error = (mean[center_band] - expected_center).abs();
            let center_motion = center_values
                .iter()
                .map(|value| (value - mean[center_band]).abs())
                .fold(0.0_f64, f64::max);
            let shape_error = mean
                .iter()
                .enumerate()
                .filter_map(|(index, measured)| {
                    let frequency = spectrum_band_frequency(index, minimum_hz, maximum_hz);
                    (40.0..=160.0).contains(&frequency).then(|| {
                        (measured
                            - reference.response_db(f64::from(SAMPLE_RATE), f64::from(frequency)))
                        .abs()
                    })
                })
                .fold(0.0_f64, f64::max);
            eprintln!(
                "80 Hz EQ Q={q:.1} gain={gain_db:+.0}: center error={center_error:.3} dB, \
                 frame motion={center_motion:.3} dB, shape error={shape_error:.3} dB"
            );
            assert!(
                center_error.is_finite() && center_motion.is_finite() && shape_error.is_finite()
            );
            assert_eq!(
                mean[center_band].is_sign_positive(),
                gain_db.is_sign_positive()
            );
            assert!(mean[center_band].abs() <= gain_db.abs() + 0.1);

            // The 85.3 ms aperture deliberately prioritises visual timing. Broad low-frequency
            // moves must remain quantitatively close; progressively narrower bells are bounded
            // and directionally correct but are not claimed to reconstruct a transfer function.
            let (maximum_error, maximum_motion) = if q < 1.0 {
                (1.75, 3.6)
            } else if q <= 2.0 {
                (4.0, 8.0)
            } else if q <= 4.0 {
                (7.0, 10.5)
            } else {
                (10.0, 10.5)
            };
            assert!(center_error <= maximum_error);
            assert!(shape_error <= maximum_error);
            assert!(center_motion <= maximum_motion);
        }
    }
}

#[test]
fn silence_is_finite_and_stays_at_the_floor() {
    assert_eq!(SPECTRUM_WINDOW_SIZE, 4_096);
    assert_eq!(SPECTRUM_FFT_SIZE, 8_192);
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let frame = analyzer
        .analyze(
            &[0.0; SPECTRUM_WINDOW_SIZE],
            None,
            SPECTRUM_WINDOW_SIZE as i64,
            1,
        )
        .unwrap();
    assert_eq!(frame.min_hz.to_bits(), SPECTRUM_MIN_HZ.to_bits());
    assert!(frame
        .dbfs
        .iter()
        .all(|value| value.to_bits() == SPECTRUM_FLOOR_DBFS.to_bits()));
}

#[test]
fn inverted_stereo_does_not_cancel() {
    let signal = sine(48_000, 1_000.0, 0.0);
    let inverted = signal.iter().map(|sample| -*sample).collect::<Vec<_>>();
    let mut mono = SpectrumAnalyzer::new(48_000).unwrap();
    let mut stereo = SpectrumAnalyzer::new(48_000).unwrap();
    let mono = mono.analyze(&signal, None, 9_600, 1).unwrap();
    let stereo = stereo.analyze(&signal, Some(&inverted), 9_600, 1).unwrap();
    for (mono, stereo) in mono.dbfs.iter().zip(stereo.dbfs) {
        assert!((mono - stereo).abs() < 1.0e-5);
    }
}

#[test]
fn mid_and_side_use_waveform_sum_and_difference_without_running_lr_in_parallel() {
    let signal = sine(48_000, 1_000.0, 0.0);
    let inverted = signal.iter().map(|sample| -*sample).collect::<Vec<_>>();
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();

    let mono = analyzer
        .analyze_mode(&signal, None, SpectrumChannelMode::Mid, 9_600, 1)
        .unwrap();
    let mid_same = analyzer
        .analyze_mode(&signal, Some(&signal), SpectrumChannelMode::Mid, 9_600, 1)
        .unwrap();
    let side_same = analyzer
        .analyze_mode(&signal, Some(&signal), SpectrumChannelMode::Side, 9_600, 1)
        .unwrap();
    let mid_inverted = analyzer
        .analyze_mode(&signal, Some(&inverted), SpectrumChannelMode::Mid, 9_600, 1)
        .unwrap();
    let side_inverted = analyzer
        .analyze_mode(
            &signal,
            Some(&inverted),
            SpectrumChannelMode::Side,
            9_600,
            1,
        )
        .unwrap();

    assert_eq!(mono.channel_mode, SpectrumChannelMode::Mid);
    assert_eq!(mono.channels, 1);
    assert_eq!(mid_same.channel_mode, SpectrumChannelMode::Mid);
    assert_eq!(side_inverted.channel_mode, SpectrumChannelMode::Side);
    assert_eq!(side_inverted.channels, 2);
    for index in 0..SPECTRUM_BAND_COUNT {
        assert!((mono.dbfs[index] - mid_same.dbfs[index]).abs() < 1.0e-5);
        assert_eq!(
            side_same.dbfs[index].to_bits(),
            SPECTRUM_FLOOR_DBFS.to_bits()
        );
        assert_eq!(
            mid_inverted.dbfs[index].to_bits(),
            SPECTRUM_FLOOR_DBFS.to_bits()
        );
        assert!((mono.dbfs[index] - side_inverted.dbfs[index]).abs() < 1.0e-5);
    }
    assert_eq!(
        analyzer.analyze_mode(&signal, None, SpectrumChannelMode::Side, 9_600, 1,),
        Err(SpectrumError::SideRequiresStereo)
    );
}

#[test]
fn forty_four_one_uses_real_bin_floor_and_its_own_nyquist() {
    let mut analyzer = SpectrumAnalyzer::new(44_100).unwrap();
    let frame = analyzer
        .analyze(&[0.0; SPECTRUM_WINDOW_SIZE], None, 44_100, 1)
        .unwrap();
    let first_bin = 44_100.0 / SPECTRUM_FFT_SIZE as f32;
    assert_eq!(frame.sample_rate, 44_100);
    assert!((frame.min_hz - first_bin.max(SPECTRUM_MIN_HZ)).abs() < 1.0e-6);
    assert!(frame.max_hz < 22_050.0);
}

#[test]
fn interactive_window_stays_below_86_ms_at_the_reference_rate() {
    let aperture_ms = SPECTRUM_WINDOW_SIZE as f64 * 1_000.0 / 48_000.0;
    assert!(aperture_ms <= 86.0, "aperture was {aperture_ms:.3} ms");
    assert!(48_000.0 / SPECTRUM_FFT_SIZE as f64 <= 12.0);
}

#[test]
fn post_minus_pre_is_signed_unclipped_and_exactly_timed() {
    assert_eq!(SPECTRUM_DIFF_RANGE_DB, 18.0);
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let signal = sine(48_000, 1_000.0, 0.0);
    let pre = analyzer.analyze(&signal, None, 48_000, 1).unwrap();
    let quieter = signal
        .iter()
        .map(|sample| *sample * 0.1)
        .collect::<Vec<_>>();
    let post = analyzer.analyze(&quieter, None, 48_000, 2).unwrap();
    let difference = difference_post_minus_pre(&post, &pre).unwrap();
    let strongest = pre
        .dbfs
        .iter()
        .enumerate()
        .max_by(|(_, left), (_, right)| left.total_cmp(right))
        .map(|(index, _)| index)
        .unwrap();
    assert_eq!(difference.pre_dbfs, pre.dbfs);
    assert_eq!(difference.post_dbfs, post.dbfs);
    assert!((difference.raw_db[strongest] + 20.0).abs() < 0.05);
    assert!(difference.raw_db[strongest] < -SPECTRUM_DIFF_RANGE_DB);
}

#[test]
fn mismatched_position_rate_or_nonfinite_frame_fails_closed() {
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let signal = sine(48_000, 1_000.0, 0.0);
    let reference = analyzer.analyze(&signal, None, 48_000, 1).unwrap();
    let mut mismatch = reference.clone();
    mismatch.presentation_end_samples += 1;
    assert!(difference_post_minus_pre(&reference, &mismatch).is_none());
    mismatch = reference.clone();
    mismatch.sample_rate = 44_100;
    assert!(difference_post_minus_pre(&reference, &mismatch).is_none());
    mismatch = reference.clone();
    mismatch.channel_mode = SpectrumChannelMode::Mid;
    assert!(difference_post_minus_pre(&reference, &mismatch).is_none());
    mismatch = reference.clone();
    mismatch.channels = 2;
    assert!(difference_post_minus_pre(&reference, &mismatch).is_none());
    mismatch = reference.clone();
    mismatch.dbfs[0] = f32::NAN;
    assert!(difference_post_minus_pre(&reference, &mismatch).is_none());
}

#[test]
#[ignore = "release-mode 48 kHz performance probe; run explicitly with --nocapture"]
fn reference_48k_stereo_fft_budget_is_quantified() {
    use std::hint::black_box;
    use std::time::Instant;

    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let left = (0..SPECTRUM_WINDOW_SIZE)
        .map(|index| (std::f32::consts::TAU * 997.0 * index as f32 / 48_000.0).sin() * 0.5)
        .collect::<Vec<_>>();
    let right = left.iter().map(|sample| -*sample).collect::<Vec<_>>();
    let iterations = 200;
    for mode in [
        SpectrumChannelMode::Lr,
        SpectrumChannelMode::Mid,
        SpectrumChannelMode::Side,
    ] {
        for iteration in 0..20 {
            black_box(
                analyzer
                    .analyze_mode(&left, Some(&right), mode, iteration * 4_800, 1)
                    .unwrap(),
            );
        }
        let started = Instant::now();
        for iteration in 0..iterations {
            black_box(
                analyzer
                    .analyze_mode(&left, Some(&right), mode, iteration * 4_800, 1)
                    .unwrap(),
            );
        }
        let micros_per_snapshot = started.elapsed().as_secs_f64() * 1_000_000.0 / iterations as f64;
        // One selected mode runs in PRE and POST at 30 Hz; modes never run in parallel.
        let projected_pair_cpu_percent = micros_per_snapshot * 60.0 / 10_000.0;
        eprintln!(
            "48k Spectrum {mode:?}: {micros_per_snapshot:.2} us/snapshot, \
             projected PRE+POST FFT CPU {projected_pair_cpu_percent:.3}%"
        );
        assert!(projected_pair_cpu_percent < 2.0);
    }
}

#[test]
fn nonfinite_input_is_rejected_without_a_partial_frame() {
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let mut samples = [0.0; SPECTRUM_WINDOW_SIZE];
    samples[123] = f32::INFINITY;
    assert_eq!(
        analyzer.analyze(&samples, None, SPECTRUM_WINDOW_SIZE as i64, 1),
        Err(SpectrumError::NonFiniteInput)
    );
}

#[test]
fn short_and_long_windows_fail_closed() {
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    for length in [SPECTRUM_WINDOW_SIZE - 1, SPECTRUM_WINDOW_SIZE + 1] {
        assert_eq!(
            analyzer.analyze(&vec![0.0; length], None, length as i64, 1),
            Err(SpectrumError::WrongWindowLength)
        );
    }
}

#[test]
fn zero_padding_never_reuses_the_previous_transform_tail() {
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let signal = sine(48_000, 1_000.0, 0.0);
    analyzer.analyze(&signal, None, 4_800, 1).unwrap();
    let silence = analyzer
        .analyze(&[0.0; SPECTRUM_WINDOW_SIZE], None, 6_400, 1)
        .unwrap();
    assert!(silence
        .dbfs
        .iter()
        .all(|value| value.to_bits() == SPECTRUM_FLOOR_DBFS.to_bits()));
}
