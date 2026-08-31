use std::sync::Arc;
use std::thread;

use super::*;

fn push_impulse_pair(pre: &AttackRuntime, post: &AttackRuntime, frames: usize, impulse_at: usize) {
    const BLOCK_FRAMES: usize = 256;
    let mut position = 0;
    while position < frames {
        let count = BLOCK_FRAMES.min(frames - position);
        let mut pre_samples = Vec::with_capacity(count * 2);
        let mut post_samples = Vec::with_capacity(count * 2);
        for offset in 0..count {
            let absolute = position + offset;
            let pre_value = if absolute == impulse_at { 0.9 } else { 0.0 };
            let post_value = if absolute == impulse_at { 0.45 } else { 0.0 };
            pre_samples.extend_from_slice(&[pre_value, pre_value]);
            post_samples.extend_from_slice(&[post_value, post_value]);
        }
        assert!(pre.push_block_from_audio(&pre_samples, 2, Some(position as i64)));
        assert!(post.push_block_from_audio(&post_samples, 2, Some(position as i64)));
        position += count;
        thread::sleep(Duration::from_millis(1));
    }
}

fn histories_are_ready(pre: &AttackRuntime, post: &AttackRuntime) -> bool {
    [pre, post].into_iter().all(|runtime| {
        runtime.try_history().is_some_and(|history| {
            history.details().next_back().is_some()
                && history
                    .waveform()
                    .next_back()
                    .is_some_and(|point| point.end_sample >= 13_920)
        })
    })
}

#[test]
fn exact_pair_transports_real_pre_and_post_attack_histories_end_to_end() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();

    let pre_spectrum = SpectrumRuntime::new(48_000, 2);
    let post_spectrum = SpectrumRuntime::new(48_000, 2);
    let pre_attack = AttackRuntime::new(48_000, 2).unwrap();
    let post_attack = AttackRuntime::new(48_000, 2).unwrap();
    let pre = SpectrumCoordinator::new_with_attack(
        48_000,
        Arc::clone(&pre_spectrum),
        Some(Arc::clone(&pre_attack)),
    );
    let post = SpectrumCoordinator::new_with_attack(
        48_000,
        Arc::clone(&post_spectrum),
        Some(Arc::clone(&post_attack)),
    );
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    assert!(post.set_post_analysis_mode(AnalysisViewMode::Attack));
    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", &pre_dir));
    assert!(pre_attack.is_enabled());
    assert!(post_attack.is_enabled());

    push_impulse_pair(&pre_attack, &post_attack, 14_000, 8_000);
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline && !histories_are_ready(&pre_attack, &post_attack) {
        thread::sleep(Duration::from_millis(2));
    }
    assert!(histories_are_ready(&pre_attack, &post_attack));

    assert!(pre.pre_tick("pre", &pre_dir));
    assert!(post.post_tick("post", Some(target)));
    let view = post.try_attack_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    let pre_history = view.pre.expect("transported PRE ATTACK history");
    let post_history = view.post.expect("local POST ATTACK history");
    assert_eq!(
        pre_history.waveform().next_back().unwrap().end_sample,
        post_history.waveform().next_back().unwrap().end_sample
    );
    assert!(
        pre_history
            .details()
            .next_back()
            .unwrap()
            .features
            .sample_peak_dbfs
            > post_history
                .details()
                .next_back()
                .unwrap()
                .features
                .sample_peak_dbfs
    );
    assert!(view
        .pair_events
        .iter()
        .any(|event| event.kind == crate::AttackPairEventKind::Matched));

    pre.shutdown();
    post.shutdown();
    pre_attack.shutdown_and_join();
    post_attack.shutdown_and_join();
    pre_spectrum.shutdown_and_join();
    post_spectrum.shutdown_and_join();
}
