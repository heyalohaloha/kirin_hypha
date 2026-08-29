use super::*;
use std::thread;

fn push_stereo_pair_in_blocks(
    pre_runtime: &SpectrumRuntime,
    post_runtime: &SpectrumRuntime,
    pre_samples: &[f32],
    post_samples: &[f32],
) {
    const BLOCK_FRAMES: usize = 256;
    assert_eq!(pre_samples.len(), post_samples.len());
    assert_eq!(pre_samples.len() % 2, 0);
    for start in (0..pre_samples.len() / 2).step_by(BLOCK_FRAMES) {
        let end = (start + BLOCK_FRAMES).min(pre_samples.len() / 2);
        let sample_range = start * 2..end * 2;
        assert!(pre_runtime.push_block_from_audio(
            &pre_samples[sample_range.clone()],
            2,
            Some(start as i64),
        ));
        assert!(post_runtime.push_block_from_audio(
            &post_samples[sample_range],
            2,
            Some(start as i64),
        ));
        thread::sleep(Duration::from_millis(1));
    }
}

fn push_tone_pair_segment(
    pre_runtime: &SpectrumRuntime,
    post_runtime: &SpectrumRuntime,
    start_frame: i64,
    end_frame: i64,
    pre_hz: f32,
    post_hz: f32,
) {
    const BLOCK_FRAMES: usize = 256;
    let mut position = start_frame;
    while position < end_frame {
        let frames = BLOCK_FRAMES.min((end_frame - position) as usize);
        let mut pre_samples = Vec::with_capacity(frames * 2);
        let mut post_samples = Vec::with_capacity(frames * 2);
        for offset in 0..frames {
            let absolute = position as usize + offset;
            let pre = (std::f32::consts::TAU * pre_hz * absolute as f32 / 48_000.0).sin();
            let post = (std::f32::consts::TAU * post_hz * absolute as f32 / 48_000.0).sin();
            pre_samples.extend_from_slice(&[pre, -pre]);
            post_samples.extend_from_slice(&[post, -post]);
        }
        assert!(pre_runtime.push_block_from_audio(&pre_samples, 2, Some(position)));
        assert!(post_runtime.push_block_from_audio(&post_samples, 2, Some(position)));
        position += frames as i64;
        thread::sleep(Duration::from_millis(1));
    }
}

fn push_tone_segment(runtime: &SpectrumRuntime, start_frame: i64, end_frame: i64, hz: f32) {
    const BLOCK_FRAMES: usize = 256;
    let mut position = start_frame;
    while position < end_frame {
        let frames = BLOCK_FRAMES.min((end_frame - position) as usize);
        let mut samples = Vec::with_capacity(frames * 2);
        for offset in 0..frames {
            let absolute = position as usize + offset;
            let sample = (std::f32::consts::TAU * hz * absolute as f32 / 48_000.0).sin();
            samples.extend_from_slice(&[sample, -sample]);
        }
        assert!(runtime.push_block_from_audio(&samples, 2, Some(position)));
        position += frames as i64;
        thread::sleep(Duration::from_millis(1));
    }
}

#[test]
fn visible_exact_pair_exchanges_audio_derived_difference_end_to_end() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, 2);
    let post_runtime = SpectrumRuntime::new(48_000, 2);
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    post.set_post_visible(true);
    post.post_tick("post", Some(target.clone()));
    pre.pre_tick("pre", &pre_dir);

    let frames = 9_600_usize;
    let mut pre_samples = Vec::with_capacity(frames * 2);
    let mut post_samples = Vec::with_capacity(frames * 2);
    for index in 0..frames {
        let sample = (std::f32::consts::TAU * 1_000.0 * index as f32 / 48_000.0).sin();
        pre_samples.extend_from_slice(&[sample, -sample]);
        post_samples.extend_from_slice(&[sample * 0.5, sample * -0.5]);
    }
    push_stereo_pair_in_blocks(&pre_runtime, &post_runtime, &pre_samples, &post_samples);

    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline
        && (pre_runtime
            .try_history()
            .and_then(|history| history.newest().cloned())
            .is_none_or(|frame| frame.presentation_end_samples < 9_600)
            || post_runtime
                .try_history()
                .and_then(|history| history.newest().cloned())
                .is_none_or(|frame| frame.presentation_end_samples < 9_600))
    {
        thread::sleep(Duration::from_millis(2));
    }
    assert_eq!(
        pre_runtime
            .try_history()
            .and_then(|history| history.newest().map(|frame| frame.presentation_end_samples)),
        Some(9_600)
    );
    assert_eq!(
        post_runtime
            .try_history()
            .and_then(|history| history.newest().map(|frame| frame.presentation_end_samples)),
        Some(9_600)
    );

    pre.pre_tick("pre", &pre_dir);
    post.post_tick("post", Some(target));
    let view = post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    let difference = view.difference.unwrap();
    let strongest = difference
        .display_db
        .iter()
        .enumerate()
        .min_by(|(_, left), (_, right)| left.total_cmp(right))
        .map(|(index, _)| index)
        .unwrap();
    assert!((difference.raw_db[strongest] + 6.0206).abs() < 0.05);

    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}

#[test]
fn exact_pair_resumes_after_a_staggered_backwards_seek_end_to_end() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, 2);
    let post_runtime = SpectrumRuntime::new(48_000, 2);
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", &pre_dir));
    push_tone_pair_segment(
        &pre_runtime,
        &post_runtime,
        480_000,
        489_600,
        1_000.0,
        1_000.0,
    );
    let initial_deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < initial_deadline
        && (pre_runtime
            .try_history()
            .and_then(|history| history.newest().cloned())
            .is_none_or(|frame| frame.presentation_end_samples < 489_600)
            || post_runtime
                .try_history()
                .and_then(|history| history.newest().cloned())
                .is_none_or(|frame| frame.presentation_end_samples < 489_600))
    {
        thread::sleep(Duration::from_millis(2));
    }
    assert!(pre.pre_tick("pre", &pre_dir));
    assert!(post.post_tick("post", Some(target.clone())));
    assert_eq!(
        post.try_view()
            .unwrap()
            .difference
            .unwrap()
            .presentation_end_samples,
        489_600
    );

    // The two workers re-enter the earlier transport run one 1,600-sample cadence apart.
    push_tone_segment(&pre_runtime, 0, 9_600, 1_000.0);
    push_tone_segment(&post_runtime, 0, 8_000, 1_000.0);
    let backwards_deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < backwards_deadline
        && (pre_runtime
            .try_history()
            .and_then(|history| history.newest().cloned())
            .is_none_or(|frame| frame.presentation_end_samples != 9_600)
            || post_runtime
                .try_history()
                .and_then(|history| history.newest().cloned())
                .is_none_or(|frame| frame.presentation_end_samples != 8_000))
    {
        thread::sleep(Duration::from_millis(2));
    }
    assert!(pre.pre_tick("pre", &pre_dir));
    assert!(post.post_tick("post", Some(target)));
    let restarted = post.try_view().unwrap();
    assert_eq!(restarted.status, SpectrumViewStatus::Active);
    assert_eq!(
        restarted
            .difference
            .as_ref()
            .unwrap()
            .presentation_end_samples,
        8_000
    );
    assert_eq!(
        restarted
            .spectrum_timeline
            .frames()
            .map(|frame| frame.presentation_end_samples)
            .collect::<Vec<_>>(),
        vec![8_000]
    );

    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}

#[test]
fn perceptual_pair_arms_one_future_epoch_and_joins_only_continuous_state() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, 2);
    let post_runtime = SpectrumRuntime::new(48_000, 2);
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    assert!(post.set_post_analysis_mode(AnalysisViewMode::Perceptual));
    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", &pre_dir));
    assert_eq!(pre_runtime.perceptual_state_epoch(), None);
    assert_eq!(post_runtime.perceptual_state_epoch(), None);

    push_tone_pair_segment(&pre_runtime, &post_runtime, 0, 2_048, 1_000.0, 6_000.0);
    assert!(pre.pre_tick("pre", &pre_dir));
    assert!(post.post_tick("post", Some(target.clone())));
    let epoch = post_runtime
        .perceptual_state_epoch()
        .expect("POST commits a future epoch after PRE readiness");
    assert!(epoch >= 2_048 + 9_600);
    assert_eq!(epoch % 4_800, 0);
    assert!(pre.pre_tick("pre", &pre_dir));
    assert_eq!(pre_runtime.perceptual_state_epoch(), Some(epoch));

    let final_endpoint = epoch + 9_600;
    push_tone_pair_segment(
        &pre_runtime,
        &post_runtime,
        2_048,
        final_endpoint,
        1_000.0,
        6_000.0,
    );
    let deadline = Instant::now() + Duration::from_secs(4);
    while Instant::now() < deadline
        && (pre_runtime
            .try_perceptual_history()
            .and_then(|history| history.newest().cloned())
            .is_none_or(|frame| frame.presentation_end_samples < final_endpoint)
            || post_runtime
                .try_perceptual_history()
                .and_then(|history| history.newest().cloned())
                .is_none_or(|frame| frame.presentation_end_samples < final_endpoint))
    {
        thread::sleep(Duration::from_millis(2));
    }
    let pre_frames = pre_runtime
        .try_perceptual_history()
        .unwrap()
        .frames()
        .map(|frame| (frame.presentation_end_samples, frame.state_epoch_samples))
        .collect::<Vec<_>>();
    let post_frames = post_runtime
        .try_perceptual_history()
        .unwrap()
        .frames()
        .map(|frame| (frame.presentation_end_samples, frame.state_epoch_samples))
        .collect::<Vec<_>>();
    assert!(
        pre_frames.iter().any(|frame| frame.0 == final_endpoint),
        "PRE frames: {pre_frames:?}"
    );
    assert!(
        post_frames.iter().any(|frame| frame.0 == final_endpoint),
        "POST frames: {post_frames:?}"
    );
    assert!(
        !pre_runtime.take_perceptual_rearm_required(),
        "PRE requested re-arm after publishing {pre_frames:?}"
    );
    assert!(
        !post_runtime.take_perceptual_rearm_required(),
        "POST requested re-arm after publishing {post_frames:?}"
    );
    assert!(pre.pre_tick("pre", &pre_dir));
    let remote = read_perceptual_snapshot(&pre_dir).expect("PRE perceptual snapshot");
    let local = post_runtime.try_perceptual_history().unwrap();
    assert!(
        newest_exact_perceptual_difference(&local, &remote.history).is_some(),
        "local={post_frames:?} remote={:?}",
        remote
            .history
            .frames()
            .map(|frame| (frame.presentation_end_samples, frame.state_epoch_samples))
            .collect::<Vec<_>>()
    );
    assert!(post.post_tick("post", Some(target)));
    let view = post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    let difference = view.perceptual_difference.unwrap();
    assert_eq!(difference.state_epoch_samples, epoch);
    assert_eq!(difference.presentation_end_samples, final_endpoint);
    assert!(difference.delta_sharpness > 0.0);

    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}

#[test]
fn isolated_worker_advances_exact_pair_without_reentering_normal_io() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, 2);
    let post_runtime = SpectrumRuntime::new(48_000, 2);
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    post.set_post_visible(true);
    post.service_post_endpoint("post", Some(target), "Mix");
    pre.service_pre_endpoint("pre", &pre_dir);

    let frames = 12_800_usize;
    let mut pre_samples = Vec::with_capacity(frames * 2);
    let mut post_samples = Vec::with_capacity(frames * 2);
    for index in 0..frames {
        let sample = (std::f32::consts::TAU * 1_000.0 * index as f32 / 48_000.0).sin();
        pre_samples.extend_from_slice(&[sample, -sample]);
        post_samples.extend_from_slice(&[sample * 0.5, sample * -0.5]);
    }
    push_stereo_pair_in_blocks(&pre_runtime, &post_runtime, &pre_samples, &post_samples);

    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline
        && post.try_view().is_none_or(|view| {
            view.status != SpectrumViewStatus::Active || view.difference.is_none()
        })
    {
        thread::sleep(Duration::from_millis(5));
    }
    let view = post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    let difference = view.difference.unwrap();
    let strongest = difference
        .display_db
        .iter()
        .enumerate()
        .min_by(|(_, left), (_, right)| left.total_cmp(right))
        .map(|(index, _)| index)
        .unwrap();
    assert!((difference.raw_db[strongest] + 6.0206).abs() < 0.05);

    post.set_post_visible(false);
    let cleanup_deadline = Instant::now() + Duration::from_secs(1);
    while Instant::now() < cleanup_deadline
        && (request_path(&pre_dir).exists() || pre_runtime.is_enabled())
    {
        thread::sleep(Duration::from_millis(5));
    }
    assert!(!request_path(&pre_dir).exists());
    assert!(!pre_runtime.is_enabled());

    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}
