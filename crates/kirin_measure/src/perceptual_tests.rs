use super::*;

fn stereo_tone(sample_rate: u32, frequency: f32, right_polarity: f32) -> Vec<f32> {
    let frames = (sample_rate / PERCEPTUAL_PRESENTATION_HZ) as usize;
    (0..frames)
        .flat_map(|index| {
            let phase = std::f32::consts::TAU * frequency * index as f32 / sample_rate as f32;
            let sample = 0.25 * phase.sin();
            [sample, sample * right_polarity]
        })
        .collect()
}

#[test]
fn exact_endpoint_and_aperture_are_required_for_difference() {
    let frame = PerceptualFrame {
        schema_version: PERCEPTUAL_SCHEMA_VERSION,
        sample_rate: 48_000,
        aperture_samples: 4_800,
        presentation_end_samples: 48_000,
        generation: 1,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        sharpness: 1.25,
    };
    let mut post = frame.clone();
    post.sharpness = 1.55;
    let difference = difference_post_minus_pre(&post, &frame).unwrap();
    assert!((difference.delta_sharpness - 0.30).abs() < 1.0e-12);

    post.presentation_end_samples += 1;
    assert!(difference_post_minus_pre(&post, &frame).is_none());
    post.presentation_end_samples -= 1;
    post.aperture_samples -= 1;
    assert!(difference_post_minus_pre(&post, &frame).is_none());
}

#[test]
fn common_host_rates_produce_finite_exact_apertures() {
    for sample_rate in [44_100_u32, 48_000, 96_000, 192_000] {
        let mut analyzer = SharpnessApertureAnalyzer::new(sample_rate, 2).unwrap();
        let aperture = sample_rate / PERCEPTUAL_PRESENTATION_HZ;
        let frame = analyzer
            .analyze(
                &stereo_tone(sample_rate, 1_000.0, 1.0),
                SpectrumChannelMode::Lr,
                i64::from(aperture),
                7,
            )
            .unwrap();
        assert_eq!(frame.sample_rate, sample_rate);
        assert_eq!(frame.aperture_samples, aperture);
        assert!(frame.sharpness.is_finite() && frame.sharpness > 0.0);
    }
}

#[test]
fn lr_is_polarity_safe_while_mid_and_side_observe_waveform_sums() {
    let mut in_phase = SharpnessApertureAnalyzer::new(48_000, 2).unwrap();
    let mut anti_phase = SharpnessApertureAnalyzer::new(48_000, 2).unwrap();
    let lr_in = in_phase
        .analyze(
            &stereo_tone(48_000, 1_000.0, 1.0),
            SpectrumChannelMode::Lr,
            4_800,
            1,
        )
        .unwrap();
    let lr_anti = anti_phase
        .analyze(
            &stereo_tone(48_000, 1_000.0, -1.0),
            SpectrumChannelMode::Lr,
            4_800,
            1,
        )
        .unwrap();
    assert!((lr_in.sharpness - lr_anti.sharpness).abs() < 1.0e-10);

    let mut mid = SharpnessApertureAnalyzer::new(48_000, 2).unwrap();
    let mut side = SharpnessApertureAnalyzer::new(48_000, 2).unwrap();
    let mid_anti = mid
        .analyze(
            &stereo_tone(48_000, 1_000.0, -1.0),
            SpectrumChannelMode::Mid,
            4_800,
            1,
        )
        .unwrap();
    let side_anti = side
        .analyze(
            &stereo_tone(48_000, 1_000.0, -1.0),
            SpectrumChannelMode::Side,
            4_800,
            1,
        )
        .unwrap();
    assert!(mid_anti.sharpness.abs() < 1.0e-12);
    assert!(side_anti.sharpness > 0.0);
}

#[test]
fn wrong_length_nonfinite_and_mono_side_fail_closed() {
    assert!(matches!(
        SharpnessApertureAnalyzer::new(11_025, 2),
        Err(PerceptualError::InvalidSampleRate)
    ));
    let mut stereo = SharpnessApertureAnalyzer::new(48_000, 2).unwrap();
    assert!(matches!(
        stereo.analyze(&[0.0; 16], SpectrumChannelMode::Lr, 8, 1),
        Err(PerceptualError::WrongApertureLength)
    ));
    let mut invalid = stereo_tone(48_000, 1_000.0, 1.0);
    invalid[10] = f32::NAN;
    assert!(matches!(
        stereo.analyze(&invalid, SpectrumChannelMode::Lr, 4_800, 1),
        Err(PerceptualError::NonFiniteInput)
    ));
    let mut mono = SharpnessApertureAnalyzer::new(48_000, 1).unwrap();
    assert!(matches!(
        mono.analyze(&[0.0; 4_800], SpectrumChannelMode::Side, 4_800, 1),
        Err(PerceptualError::SideRequiresStereo)
    ));
}

#[test]
#[ignore = "release-mode on-demand Sharpness performance probe; run explicitly with --nocapture"]
fn one_visible_pair_sharpness_worker_budget_is_quantified() {
    use std::hint::black_box;
    use std::time::Instant;

    let samples = stereo_tone(48_000, 1_000.0, 1.0);
    let mut analyzer = SharpnessApertureAnalyzer::new(48_000, 2).unwrap();
    let iterations = 40;
    let started = Instant::now();
    for index in 0..iterations {
        let result = analyzer
            .analyze(
                black_box(&samples),
                SpectrumChannelMode::Lr,
                (index + 1) as i64 * 4_800,
                1,
            )
            .unwrap();
        black_box(result);
    }
    let millis_per_aperture = started.elapsed().as_secs_f64() * 1_000.0 / iterations as f64;
    let projected_pair_worker_percent = millis_per_aperture * 10.0 * 2.0 / 10.0;
    eprintln!(
        "48k Perceptual Delta Sharpness: {millis_per_aperture:.3} ms/100ms aperture, \
         projected one visible PRE+POST worker CPU {projected_pair_worker_percent:.3}%"
    );
    assert!(projected_pair_worker_percent < 25.0);
}
