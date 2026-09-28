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
                    .is_some_and(|point| point.end_sample >= 24_000)
        })
    })
}

#[test]
fn exact_pair_transports_real_pre_and_post_attack_histories_end_to_end() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();

    let pre_spectrum = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let post_spectrum =
        SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
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

    // A detail waits for its 130 ms windows and for every onset before the body end.
    push_impulse_pair(&pre_attack, &post_attack, 24_000, 8_000);
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
    // POST is measured at the PRE onset over the PRE detail's exact windows (B-1016).
    let pre_detail = *pre_history.details().next_back().unwrap();
    let anchored = view
        .post_anchored
        .iter()
        .find(|detail| detail.event.event_sample == pre_detail.event.event_sample)
        .expect("POST measured at the PRE onset");
    assert_eq!(
        anchored.features.window_start_sample,
        pre_detail.features.window_start_sample
    );
    assert_eq!(
        anchored.features.body_end_sample,
        pre_detail.features.body_end_sample
    );
    assert_eq!(
        anchored.event.generation,
        post_history.newest().unwrap().generation
    );
    assert!(
        (anchored.features.attack_rms_dbfs - pre_detail.features.attack_rms_dbfs + 6.020_6).abs()
            < 1e-3
    );

    pre.shutdown();
    post.shutdown();
    pre_attack.shutdown_and_join();
    post_attack.shutdown_and_join();
    pre_spectrum.shutdown_and_join();
    post_spectrum.shutdown_and_join();
}

#[test]
fn a_pairing_flicker_keeps_the_post_attack_history() {
    // With the transport stopped nothing new is measured, so HOLD shows the last six seconds only
    // if a pairing that drops for one tick and comes back does not restart POST's own ATTACK run.
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_spectrum = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let post_spectrum =
        SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
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
    push_impulse_pair(&pre_attack, &post_attack, 24_000, 8_000);
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline && !histories_are_ready(&pre_attack, &post_attack) {
        thread::sleep(Duration::from_millis(2));
    }
    assert!(histories_are_ready(&pre_attack, &post_attack));
    let before = post_attack.try_history().unwrap();

    assert!(
        post.post_tick("post", None),
        "the pairing drops for one tick"
    );
    assert!(
        post.post_tick("post", Some(target.clone())),
        "and comes back"
    );
    let after = post_attack.try_history().unwrap();
    assert_eq!(
        after.frames().count(),
        before.frames().count(),
        "POST keeps its measured ATTACK history"
    );
    assert_eq!(after.details().count(), before.details().count());
    assert_eq!(after.newest(), before.newest());

    // The new pair session joins the kept POST history with PRE again.
    let deadline = Instant::now() + Duration::from_secs(3);
    let mut status = SpectrumViewStatus::WarmingUp;
    while Instant::now() < deadline && status != SpectrumViewStatus::Active {
        assert!(pre.pre_tick("pre", &pre_dir));
        let _ = post.post_tick("post", Some(target.clone()));
        status = post.try_attack_view().map_or(status, |view| view.status);
        thread::sleep(Duration::from_millis(5));
    }
    assert_eq!(status, SpectrumViewStatus::Active);

    pre.shutdown();
    post.shutdown();
    pre_attack.shutdown_and_join();
    post_attack.shutdown_and_join();
    pre_spectrum.shutdown_and_join();
    post_spectrum.shutdown_and_join();
}

/// A 500 Hz burst at `onset` on both sides: POST at half the amplitude.
fn push_burst_pair(pre: &AttackRuntime, post: &AttackRuntime, frames: usize, onset: usize) {
    // 1 024-frame blocks: a second of audio fits the 128-slot ingress ring while the worker warms up.
    const BLOCK_FRAMES: usize = 1_024;
    let value_at = |absolute: usize, amplitude: f64| -> f32 {
        let t = (absolute as f64 - onset as f64) / 48_000.0;
        if t < 0.0 {
            return 0.0;
        }
        let envelope = if t < 0.006 {
            t / 0.006
        } else {
            (-(t - 0.006) / 0.045).exp()
        };
        (amplitude * envelope * (std::f64::consts::TAU * 500.0 * t).sin()) as f32
    };
    let mut position = 0;
    while position < frames {
        let count = BLOCK_FRAMES.min(frames - position);
        let mut pre_samples = Vec::with_capacity(count * 2);
        let mut post_samples = Vec::with_capacity(count * 2);
        for offset in 0..count {
            let pre_value = value_at(position + offset, 0.5);
            let post_value = value_at(position + offset, 0.25);
            pre_samples.extend_from_slice(&[pre_value, pre_value]);
            post_samples.extend_from_slice(&[post_value, post_value]);
        }
        assert!(pre.push_block_from_audio(&pre_samples, 2, Some(position as i64)));
        assert!(post.push_block_from_audio(&post_samples, 2, Some(position as i64)));
        position += count;
        thread::sleep(Duration::from_millis(1));
    }
}

#[test]
fn a_chosen_band_rides_the_request_and_comes_back_paired() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_spectrum = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let post_spectrum =
        SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
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
    let band = crate::attack_perception::band::AttackBand::from_index(4).unwrap();

    assert!(post.set_post_analysis_mode(AnalysisViewMode::Attack));
    post.set_post_visible(true);
    post_attack.set_band(Some(band));
    post.set_post_attack_band(Some(band));
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", &pre_dir));
    assert_eq!(
        pre_attack.band(),
        Some(band),
        "the request carried the band to PRE"
    );

    // The hit after Phase D settles, then its 300 ms tail and the decisions past it.
    push_burst_pair(&pre_attack, &post_attack, 48_000, 20_000);
    let deadline = Instant::now() + Duration::from_secs(4);
    let mut view = None;
    while Instant::now() < deadline {
        assert!(pre.pre_tick("pre", &pre_dir));
        assert!(post.post_tick("post", Some(target.clone())));
        view = post
            .try_attack_view()
            .filter(|view| view.band_pairs.iter().any(|pair| pair.post.is_some()));
        if view.is_some() {
            break;
        }
        thread::sleep(Duration::from_millis(5));
    }
    let view = view.expect("a paired band measure");
    assert_eq!(view.status, SpectrumViewStatus::Active);
    assert_eq!(view.band, Some(band));
    assert_eq!(view.pre_band, Some(band));
    let pair = view
        .band_pairs
        .iter()
        .find(|pair| pair.post.is_some())
        .unwrap();
    let post_measure = pair.post.unwrap();
    assert_eq!(pair.pre.event_sample, pair.event_sample);
    assert_eq!(post_measure.span_end_sample, pair.pre.span_end_sample);
    assert!((post_measure.level_dbfs - pair.pre.level_dbfs + 6.02).abs() < 0.3);
    let delay_ms = pair.delay_frames.unwrap() * 1_000.0 / 48_000.0;
    assert!(delay_ms.abs() < 0.3, "same content: {delay_ms} ms");

    // ALL on POST: the next request drops the band and PRE stops measuring it.
    post_attack.set_band(None);
    post.set_post_attack_band(None);
    thread::sleep(REQUEST_RENEW_INTERVAL);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", &pre_dir));
    assert_eq!(pre_attack.band(), None);

    pre.shutdown();
    post.shutdown();
    pre_attack.shutdown_and_join();
    post_attack.shutdown_and_join();
    pre_spectrum.shutdown_and_join();
    post_spectrum.shutdown_and_join();
}
