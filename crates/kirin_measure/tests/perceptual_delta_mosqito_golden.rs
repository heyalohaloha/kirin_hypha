use kirin_measure::{
    perceptual_difference_post_minus_pre, SharpnessContinuousAnalyzer, SpectrumChannelMode,
};
use serde::Deserialize;

const FIXTURE: &str = include_str!("fixtures/mosqito_perceptual_delta_1_2_1.json");
const SAMPLE_RATE: u32 = 48_000;
const APERTURE: usize = 4_800;

#[derive(Deserialize)]
struct Fixture {
    schema: u32,
    reference: Reference,
    tolerance_acum: f64,
    post_minus_pre: Vec<f64>,
}

#[derive(Deserialize)]
struct Reference {
    library: String,
    version: String,
    sharpness: String,
    sample_rate_hz: u32,
    aperture_samples: usize,
    state_epoch_samples: i64,
}

fn step_tone_sample(index: usize) -> f32 {
    if !(SAMPLE_RATE as usize / 2..SAMPLE_RATE as usize * 2).contains(&index) {
        return 0.0;
    }
    let peak = 2.0e-5_f64 * 10.0_f64.powf(94.0 / 20.0) * 2.0_f64.sqrt();
    (peak * (std::f64::consts::TAU * 1_000.0 * index as f64 / f64::from(SAMPLE_RATE)).sin()) as f32
}

#[test]
fn continuous_post_minus_pre_matches_mosqito_at_every_100ms_endpoint() {
    let fixture: Fixture = serde_json::from_str(FIXTURE).unwrap();
    assert_eq!(fixture.schema, 1);
    assert_eq!(fixture.reference.library, "MoSQITo");
    assert_eq!(fixture.reference.version, "1.2.1");
    assert_eq!(fixture.reference.sharpness, "DIN 45692:2009 Widmann");
    assert_eq!(fixture.reference.sample_rate_hz, SAMPLE_RATE);
    assert_eq!(fixture.reference.aperture_samples, APERTURE);
    assert_eq!(fixture.reference.state_epoch_samples, 0);

    let mut pre = SharpnessContinuousAnalyzer::new(SAMPLE_RATE, 2).unwrap();
    let mut post = SharpnessContinuousAnalyzer::new(SAMPLE_RATE, 2).unwrap();
    pre.reset_at_epoch(0).unwrap();
    post.reset_at_epoch(0).unwrap();
    for (aperture_index, expected) in fixture.post_minus_pre.iter().enumerate() {
        let start = aperture_index * APERTURE;
        let pre_input = vec![1.0e-20_f32; APERTURE * 2];
        let post_input = (start..start + APERTURE)
            .flat_map(|index| {
                let sample = step_tone_sample(index);
                [sample, sample]
            })
            .collect::<Vec<_>>();
        let endpoint = (aperture_index + 1) as i64 * APERTURE as i64;
        let pre_frame = pre
            .analyze_aperture(&pre_input, SpectrumChannelMode::Lr, endpoint, 1)
            .unwrap()[0]
            .clone();
        let post_frame = post
            .analyze_aperture(&post_input, SpectrumChannelMode::Lr, endpoint, 1)
            .unwrap()[0]
            .clone();
        let difference = perceptual_difference_post_minus_pre(&post_frame, &pre_frame).unwrap();
        assert_eq!(difference.presentation_end_samples, endpoint);
        assert_eq!(difference.state_epoch_samples, 0);
        assert!(
            (difference.delta_sharpness - expected).abs() <= fixture.tolerance_acum,
            "endpoint {endpoint}: {} acum differs from MoSQITo {} by more than {}",
            difference.delta_sharpness,
            expected,
            fixture.tolerance_acum
        );
    }
}
