use super::*;

fn sine(sample_rate: u32, frequency: f32, phase: f32) -> Vec<f32> {
    (0..SPECTRUM_FFT_SIZE)
        .map(|index| {
            (std::f32::consts::TAU * frequency * index as f32 / sample_rate as f32 + phase).sin()
        })
        .collect()
}

#[test]
fn silence_is_finite_and_stays_at_the_floor() {
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let frame = analyzer
        .analyze(&[0.0; SPECTRUM_FFT_SIZE], None, 8_192, 1)
        .unwrap();
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
fn forty_four_one_uses_real_bin_floor_and_its_own_nyquist() {
    let mut analyzer = SpectrumAnalyzer::new(44_100).unwrap();
    let frame = analyzer
        .analyze(&[0.0; SPECTRUM_FFT_SIZE], None, 44_100, 1)
        .unwrap();
    let first_bin = 44_100.0 / SPECTRUM_FFT_SIZE as f32;
    assert_eq!(frame.sample_rate, 44_100);
    assert!((frame.min_hz - first_bin.max(SPECTRUM_MIN_HZ)).abs() < 1.0e-6);
    assert!(frame.max_hz < 22_050.0);
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
    mismatch.dbfs[0] = f32::NAN;
    assert!(difference_post_minus_pre(&reference, &mismatch).is_none());
}

#[test]
#[ignore = "release-mode 48 kHz performance probe; run explicitly with --nocapture"]
fn reference_48k_stereo_fft_budget_is_quantified() {
    use std::hint::black_box;
    use std::time::Instant;

    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let left = (0..SPECTRUM_FFT_SIZE)
        .map(|index| (std::f32::consts::TAU * 997.0 * index as f32 / 48_000.0).sin() * 0.5)
        .collect::<Vec<_>>();
    let right = left.iter().map(|sample| -*sample).collect::<Vec<_>>();
    for iteration in 0..20 {
        black_box(
            analyzer
                .analyze(&left, Some(&right), iteration * 4_800, 1)
                .unwrap(),
        );
    }

    let iterations = 200;
    let started = Instant::now();
    for iteration in 0..iterations {
        black_box(
            analyzer
                .analyze(&left, Some(&right), iteration * 4_800, 1)
                .unwrap(),
        );
    }
    let elapsed = started.elapsed();
    let micros_per_stereo_snapshot = elapsed.as_secs_f64() * 1_000_000.0 / iterations as f64;
    // One visible exact pair runs one stereo analyzer in PRE and one in POST, each at 30 Hz.
    let projected_pair_cpu_percent = micros_per_stereo_snapshot * 60.0 / 10_000.0;
    eprintln!(
        "48k Spectrum: {micros_per_stereo_snapshot:.2} us/stereo snapshot, \
         projected PRE+POST FFT CPU {projected_pair_cpu_percent:.3}%"
    );
    assert!(projected_pair_cpu_percent < 2.0);
}

#[test]
fn nonfinite_input_is_rejected_without_a_partial_frame() {
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let mut samples = [0.0; SPECTRUM_FFT_SIZE];
    samples[123] = f32::INFINITY;
    assert_eq!(
        analyzer.analyze(&samples, None, 8_192, 1),
        Err(SpectrumError::NonFiniteInput)
    );
}
