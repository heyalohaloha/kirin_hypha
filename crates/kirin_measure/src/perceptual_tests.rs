use super::*;
use crate::phase_d::channels::PhaseDChannelStream;

fn stereo_tone(
    sample_rate: u32,
    frequency: f32,
    right_polarity: f32,
    start_frame: usize,
) -> Vec<f32> {
    let frames = (sample_rate / PERCEPTUAL_PRESENTATION_HZ) as usize;
    (0..frames)
        .flat_map(|index| {
            let phase = std::f32::consts::TAU * frequency * (start_frame + index) as f32
                / sample_rate as f32;
            let sample = 0.25 * phase.sin();
            [sample, sample * right_polarity]
        })
        .collect()
}

fn first_frame(
    analyzer: &mut SharpnessContinuousAnalyzer,
    input: &[f32],
    mode: SpectrumChannelMode,
    endpoint: i64,
) -> PerceptualFrame {
    analyzer
        .analyze_aperture(input, mode, endpoint, 7)
        .unwrap()
        .first()
        .expect("48 kHz aperture must publish immediately")
        .clone()
}

#[test]
fn exact_endpoint_epoch_and_aperture_are_required_for_difference() {
    let frame = PerceptualFrame {
        schema_version: PERCEPTUAL_SCHEMA_VERSION,
        sample_rate: 48_000,
        aperture_samples: 4_800,
        presentation_end_samples: 48_000,
        state_epoch_samples: 0,
        generation: 1,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        sharpness: 1.25,
    };
    let mut post = frame.clone();
    post.sharpness = 1.55;
    let difference = difference_post_minus_pre(&post, &frame).unwrap();
    assert!((difference.delta_sharpness - 0.30).abs() < 1.0e-12);
    assert_eq!(difference.state_epoch_samples, 0);

    post.presentation_end_samples += 1;
    assert!(difference_post_minus_pre(&post, &frame).is_none());
    post.presentation_end_samples -= 1;
    post.state_epoch_samples = 4_800;
    assert!(difference_post_minus_pre(&post, &frame).is_none());
    post.state_epoch_samples = 0;
    post.aperture_samples -= 1;
    assert!(difference_post_minus_pre(&post, &frame).is_none());
}

#[test]
fn common_host_rates_produce_continuous_finite_apertures() {
    for sample_rate in [44_100_u32, 48_000, 96_000, 192_000] {
        let aperture = sample_rate / PERCEPTUAL_PRESENTATION_HZ;
        let mut analyzer = SharpnessContinuousAnalyzer::new(sample_rate, 2).unwrap();
        analyzer.reset_at_epoch(0).unwrap();
        let mut observed = Vec::new();
        for index in 0..12_usize {
            observed.extend_from_slice(
                analyzer
                    .analyze_aperture(
                        &stereo_tone(sample_rate, 1_000.0, 1.0, index * aperture as usize),
                        SpectrumChannelMode::Lr,
                        (index + 1) as i64 * i64::from(aperture),
                        7,
                    )
                    .unwrap(),
            );
        }
        assert!(observed.len() >= 10, "{sample_rate} Hz lost apertures");
        for frame in observed {
            assert_eq!(frame.sample_rate, sample_rate);
            assert_eq!(frame.aperture_samples, aperture);
            assert_eq!(frame.state_epoch_samples, 0);
            assert!(frame.sharpness.is_finite() && frame.sharpness > 0.0);
        }
    }
}

#[test]
fn continuous_48k_endpoints_match_the_record_phase_d_stream() {
    let mut analyzer = SharpnessContinuousAnalyzer::new(48_000, 2).unwrap();
    analyzer.reset_at_epoch(0).unwrap();
    let mut record_reference = PhaseDChannelStream::new(FieldType::Free, 2);

    for index in 0..8_usize {
        let input = stereo_tone(48_000, 317.0 + index as f32 * 211.0, -1.0, index * 4_800);
        let expected = record_reference
            .push_interleaved_slot(
                &input
                    .iter()
                    .map(|sample| f64::from(*sample))
                    .collect::<Vec<_>>(),
            )
            .unwrap()
            .sharpness;
        let observed = first_frame(
            &mut analyzer,
            &input,
            SpectrumChannelMode::Lr,
            (index + 1) as i64 * 4_800,
        );
        assert!((observed.sharpness - expected).abs() < 1.0e-12);
    }
}

#[test]
fn lr_is_polarity_safe_while_mid_and_side_observe_waveform_sums() {
    let input_in = stereo_tone(48_000, 1_000.0, 1.0, 0);
    let input_anti = stereo_tone(48_000, 1_000.0, -1.0, 0);
    let mut in_phase = SharpnessContinuousAnalyzer::new(48_000, 2).unwrap();
    let mut anti_phase = SharpnessContinuousAnalyzer::new(48_000, 2).unwrap();
    in_phase.reset_at_epoch(0).unwrap();
    anti_phase.reset_at_epoch(0).unwrap();
    let lr_in = first_frame(&mut in_phase, &input_in, SpectrumChannelMode::Lr, 4_800);
    let lr_anti = first_frame(&mut anti_phase, &input_anti, SpectrumChannelMode::Lr, 4_800);
    assert!((lr_in.sharpness - lr_anti.sharpness).abs() < 1.0e-10);

    let mut mid = SharpnessContinuousAnalyzer::new(48_000, 2).unwrap();
    let mut side = SharpnessContinuousAnalyzer::new(48_000, 2).unwrap();
    mid.reset_at_epoch(0).unwrap();
    side.reset_at_epoch(0).unwrap();
    let mid_anti = first_frame(&mut mid, &input_anti, SpectrumChannelMode::Mid, 4_800);
    let side_anti = first_frame(&mut side, &input_anti, SpectrumChannelMode::Side, 4_800);
    assert!(mid_anti.sharpness.abs() < 1.0e-12);
    assert!(side_anti.sharpness > 0.0);
}

#[test]
fn invalid_epoch_definition_input_and_mono_side_fail_closed() {
    assert!(matches!(
        SharpnessContinuousAnalyzer::new(11_025, 2),
        Err(PerceptualError::InvalidSampleRate)
    ));
    let mut stereo = SharpnessContinuousAnalyzer::new(48_000, 2).unwrap();
    assert!(matches!(
        stereo.reset_at_epoch(1),
        Err(PerceptualError::InvalidStateEpoch)
    ));
    stereo.reset_at_epoch(0).unwrap();
    assert!(matches!(
        stereo.analyze_aperture(&[0.0; 16], SpectrumChannelMode::Lr, 4_800, 1),
        Err(PerceptualError::WrongApertureLength)
    ));
    let mut invalid = stereo_tone(48_000, 1_000.0, 1.0, 0);
    invalid[10] = f32::NAN;
    assert!(matches!(
        stereo.analyze_aperture(&invalid, SpectrumChannelMode::Lr, 4_800, 1),
        Err(PerceptualError::NonFiniteInput)
    ));
    let valid = stereo_tone(48_000, 1_000.0, 1.0, 0);
    let _ = stereo
        .analyze_aperture(&valid, SpectrumChannelMode::Lr, 4_800, 1)
        .unwrap();
    assert!(matches!(
        stereo.analyze_aperture(&valid, SpectrumChannelMode::Mid, 9_600, 1),
        Err(PerceptualError::DefinitionChanged)
    ));

    let mut mono = SharpnessContinuousAnalyzer::new(48_000, 1).unwrap();
    mono.reset_at_epoch(0).unwrap();
    assert!(matches!(
        mono.analyze_aperture(&[0.0; 4_800], SpectrumChannelMode::Side, 4_800, 1),
        Err(PerceptualError::SideRequiresStereo)
    ));
}

#[test]
#[ignore = "release-mode on-demand Sharpness performance probe; run explicitly with --nocapture"]
fn one_visible_pair_continuous_sharpness_worker_budget_is_quantified() {
    use std::hint::black_box;
    use std::time::Instant;

    let samples = stereo_tone(48_000, 1_000.0, 1.0, 0);
    let mut analyzer = SharpnessContinuousAnalyzer::new(48_000, 2).unwrap();
    analyzer.reset_at_epoch(0).unwrap();
    let iterations = 40;
    let started = Instant::now();
    for index in 0..iterations {
        let result = analyzer
            .analyze_aperture(
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
        "48k continuous Perceptual Delta Sharpness: {millis_per_aperture:.3} ms/100ms aperture, \
         projected one visible PRE+POST worker CPU {projected_pair_worker_percent:.3}%"
    );
    assert!(projected_pair_worker_percent < 18.0);
}
