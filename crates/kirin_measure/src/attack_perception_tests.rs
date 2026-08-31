use super::*;

const RATE: u32 = 48_000;

fn windows(channels: usize) -> (Vec<f32>, Vec<f32>) {
    (
        vec![0.01; frames_for_micros(RATE, ATTACK_CONTEXT_MICROS) as usize * channels],
        vec![0.01; frames_for_micros(RATE, ATTACK_DETAIL_MICROS) as usize * channels],
    )
}

fn analyze(context: &[f32], attack: &[f32], channels: usize) -> AttackPerceptualFeatures {
    AttackPerceptualFeatures::analyze(context, attack, RATE, channels, Some(0.7)).unwrap()
}

#[test]
fn contrast_describes_attack_relative_to_preceding_context() {
    let (context, mut attack) = windows(1);
    attack.fill(0.1);
    let features = analyze(&context, &attack, 1);
    assert!((features.contrast_db - 20.0).abs() < 0.000_1);
    assert!((features.sample_peak_dbfs + 20.0).abs() < 0.000_1);
    assert!(features.crest_db.abs() < 0.000_1);
    assert!(!features.contrast_floor_limited);
    assert!(features.has_valid_layout());
}

#[test]
fn fixed_gain_preserves_contrast_shape_and_crest() {
    let (mut pre_context, mut pre_attack) = windows(1);
    pre_attack.fill(0.1);
    let mut post_context = pre_context.clone();
    let mut post_attack = pre_attack.clone();
    for sample in &mut post_context {
        *sample *= 2.0;
    }
    for sample in &mut post_attack {
        *sample *= 2.0;
    }
    let pre = analyze(&pre_context, &pre_attack, 1);
    let post = analyze(&post_context, &post_attack, 1);
    let delta = AttackPerceptualDelta::between(pre, post).unwrap();
    assert!(delta.contrast_db.abs() < 0.000_1);
    assert!(delta.crest_db.abs() < 0.000_1);
    assert!(delta.sample_edge_ratio_db.abs() < 0.000_1);
    assert!(delta.peak_plateau_ms.abs() < 0.000_1);
    assert!((delta.sample_peak_db - 6.020_6).abs() < 0.001);
    assert!(delta.temporal_centroid_ms.unwrap().abs() < 0.000_1);
    pre_context.fill(0.0);
}

#[test]
fn clipped_harmonic_texture_separates_edge_density_and_peak_rounding() {
    let (context, mut clean) = windows(1);
    clean.fill(0.0);
    for (index, sample) in clean.iter_mut().enumerate() {
        *sample = (std::f32::consts::TAU * 400.0 * index as f32 / RATE as f32).sin() * 0.7;
    }
    let shaped = clean
        .iter()
        .map(|sample| (sample * 3.0).tanh() / 3.0_f32.tanh())
        .collect::<Vec<_>>();
    let pre = analyze(&context, &clean, 1);
    let post = analyze(&context, &shaped, 1);
    let delta = AttackPerceptualDelta::between(pre, post).unwrap();
    assert!(delta.sample_edge_ratio_db > 0.1);
    assert!(delta.crest_db < -0.1);
    assert!(delta.peak_plateau_ms > 0.1);
}

#[test]
fn temporal_centroid_separates_fast_and_late_attacks() {
    let (context, mut fast) = windows(1);
    let mut late = fast.clone();
    fast.fill(0.0);
    late.fill(0.0);
    fast[0] = 1.0;
    late[(RATE / 100) as usize] = 1.0;
    let fast = analyze(&context, &fast, 1);
    let late = analyze(&context, &late, 1);
    assert!(fast.temporal_centroid_ms.unwrap() < 0.02);
    assert!((late.temporal_centroid_ms.unwrap() - 10.0).abs() < 0.02);
}

#[test]
fn steady_context_does_not_invent_an_attack_shape() {
    let (context, attack) = windows(1);
    let features = analyze(&context, &attack, 1);
    assert_eq!(features.temporal_centroid_ms, None);
}

#[test]
fn stereo_power_mean_preserves_dual_mono_and_exposes_one_sided_level() {
    let (mono_context, mut mono_attack) = windows(1);
    mono_attack.fill(0.25);
    let mono = analyze(&mono_context, &mono_attack, 1);

    let stereo_context = mono_context
        .iter()
        .flat_map(|sample| [*sample, *sample])
        .collect::<Vec<_>>();
    let dual_attack = mono_attack
        .iter()
        .flat_map(|sample| [*sample, *sample])
        .collect::<Vec<_>>();
    let one_sided_attack = mono_attack
        .iter()
        .flat_map(|sample| [*sample, 0.0])
        .collect::<Vec<_>>();
    let dual = analyze(&stereo_context, &dual_attack, 2);
    let one_sided = analyze(&stereo_context, &one_sided_attack, 2);
    assert!((dual.attack_rms_dbfs - mono.attack_rms_dbfs).abs() < 0.000_1);
    assert!((one_sided.attack_rms_dbfs - mono.attack_rms_dbfs + 3.010_3).abs() < 0.001);
}

#[test]
fn sharpness_delta_remains_pending_until_both_sides_exist() {
    let (context, attack) = windows(1);
    let pre = AttackPerceptualFeatures::analyze(&context, &attack, RATE, 1, None).unwrap();
    let post = analyze(&context, &attack, 1);
    let delta = AttackPerceptualDelta::between(pre, post).unwrap();
    assert_eq!(delta.sharpness_acum, None);
}

#[test]
fn exact_windows_and_finite_samples_are_required() {
    let (context, mut attack) = windows(1);
    assert_eq!(
        AttackPerceptualFeatures::analyze(&context[..context.len() - 1], &attack, RATE, 1, None),
        Err(AttackPerceptionError::ContextLengthMismatch)
    );
    attack[0] = f32::NAN;
    assert_eq!(
        AttackPerceptualFeatures::analyze(&context, &attack, RATE, 1, None),
        Err(AttackPerceptionError::NonFiniteSample)
    );
}

#[test]
fn pre_and_post_layouts_must_match() {
    let (context, attack) = windows(1);
    let mono = analyze(&context, &attack, 1);
    let stereo_context = context
        .iter()
        .flat_map(|sample| [*sample, *sample])
        .collect::<Vec<_>>();
    let stereo_attack = attack
        .iter()
        .flat_map(|sample| [*sample, *sample])
        .collect::<Vec<_>>();
    let stereo = analyze(&stereo_context, &stereo_attack, 2);
    assert_eq!(
        AttackPerceptualDelta::between(mono, stereo),
        Err(AttackPerceptionError::IdentityMismatch)
    );
}
