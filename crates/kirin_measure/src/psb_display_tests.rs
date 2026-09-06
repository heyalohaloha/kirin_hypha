use super::*;
use crate::phase_d::display::valid_shares;

fn observe(channels: usize, polarity: f32, frequency: f32) -> PerceptualFrame {
    let mut analyzer = SharpnessContinuousAnalyzer::new(48_000, channels).unwrap();
    analyzer.reset_at_epoch(0).unwrap();
    let mut latest = None;
    for block in 0..12 {
        let input: Vec<f32> = (0..4_800)
            .flat_map(|i| {
                let sample = 0.2
                    * (std::f32::consts::TAU * frequency * (block * 4_800 + i) as f32 / 48_000.0)
                        .sin();
                (0..channels).map(move |ch| if ch == 0 { sample } else { sample * polarity })
            })
            .collect();
        latest = analyzer
            .analyze_aperture(
                &input,
                SpectrumChannelMode::Lr,
                (block + 1) as i64 * 4_800,
                7,
            )
            .unwrap()
            .last()
            .cloned();
    }
    latest.unwrap()
}

#[test]
fn psb_uses_real_specific_loudness_and_independent_channels() {
    let mono = observe(1, 1.0, 1_000.0).psb.unwrap();
    let stereo = observe(2, 1.0, 1_000.0).psb.unwrap();
    let anti = observe(2, -1.0, 1_000.0).psb.unwrap();
    for shares in [mono, stereo, anti] {
        assert!(valid_shares(&shares));
    }
    for i in 0..20 {
        assert!((mono[i] - stereo[i]).abs() < 1e-12);
        assert!((stereo[i] - anti[i]).abs() < 1e-12);
    }
    let low = observe(1, 1.0, 200.0).psb.unwrap();
    let high = observe(1, 1.0, 6_000.0).psb.unwrap();
    let centre = |p: [f64; 20]| p.iter().enumerate().map(|(i, v)| i as f64 * v).sum::<f64>();
    assert!(centre(high) > centre(low) + 5.0);
}

#[test]
fn silent_psb_is_missing_not_uniform_and_exact_pairing_is_required() {
    assert!(observe(2, 1.0, 0.0).psb.is_none());
    let pre = observe(2, 1.0, 200.0);
    let mut post = observe(2, 1.0, 6_000.0);
    let diff = difference_post_minus_pre(&post, &pre).unwrap();
    let sum: f64 = diff
        .post_psb
        .unwrap()
        .iter()
        .zip(diff.pre_psb.unwrap())
        .map(|(post, pre)| post - pre)
        .sum();
    assert!(sum.abs() < 1e-12);
    post.presentation_end_samples += 4_800;
    assert!(difference_post_minus_pre(&post, &pre).is_none());
}
