use super::*;
use crate::spectrum::{
    SpectrumFrame, SPECTRUM_BAND_COUNT, SPECTRUM_FFT_SIZE, SPECTRUM_SCHEMA_VERSION,
};
use crate::SPECTRUM_HISTORY_CAPACITY;
use std::thread;

fn frame(end: i64, value: f32) -> SpectrumFrame {
    SpectrumFrame {
        schema_version: SPECTRUM_SCHEMA_VERSION,
        sample_rate: 48_000,
        fft_size: SPECTRUM_FFT_SIZE as u32,
        band_count: SPECTRUM_BAND_COUNT as u16,
        presentation_end_samples: end,
        generation: 7,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        min_hz: 10.0,
        max_hz: 22_000.0,
        dbfs: [value; SPECTRUM_BAND_COUNT],
    }
}

fn perceptual_frame(end: i64, value: f64) -> crate::PerceptualFrame {
    crate::PerceptualFrame {
        schema_version: crate::PERCEPTUAL_SCHEMA_VERSION,
        sample_rate: 48_000,
        aperture_samples: 4_800,
        presentation_end_samples: end,
        state_epoch_samples: 0,
        generation: 7,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        sharpness: value,
    }
}

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

#[test]
fn fixed_snapshot_roundtrip_preserves_history_and_request() {
    let request_id = Uuid::new_v4();
    let mut history = SpectrumHistory::with_capacity();
    history.push(frame(4_800, -20.0));
    history.push(frame(9_600, -18.0));
    let bytes = encode_snapshot(request_id, &history);
    let decoded = decode_snapshot(&bytes).unwrap();
    assert_eq!(decoded.request_id, request_id);
    assert_eq!(decoded.history.frames().count(), 2);
    assert_eq!(decoded.history.newest().unwrap(), &frame(9_600, -18.0));
}

#[test]
fn perceptual_snapshot_roundtrip_preserves_exact_apertures() {
    let request_id = Uuid::new_v4();
    let mut history = crate::PerceptualHistory::with_capacity();
    history.push(perceptual_frame(4_800, 1.1));
    history.push(perceptual_frame(9_600, 1.3));
    let bytes = encode_perceptual_snapshot(request_id, &history);
    assert!(bytes.len() <= perceptual_codec::PERCEPTUAL_SNAPSHOT_MAX_BYTES as usize);
    let decoded = perceptual_codec::decode_perceptual_snapshot(&bytes).unwrap();
    assert_eq!(decoded.request_id, request_id);
    assert_eq!(decoded.history.frames().count(), 2);
    assert_eq!(
        decoded.history.newest(),
        Some(&perceptual_frame(9_600, 1.3))
    );
}

#[test]
#[ignore = "release-mode 30 Hz atomic exchange performance probe; run explicitly with --nocapture"]
fn active_pair_30hz_atomic_exchange_budget_is_quantified() {
    use std::hint::black_box;

    let temp = tempfile::tempdir().unwrap();
    let instance_dir = temp.path().join("project").join("pre");
    fs::create_dir_all(instance_dir.join("spectrum")).unwrap();
    let request_id = Uuid::new_v4();
    let mut history = SpectrumHistory::with_capacity();
    for index in 1..=SPECTRUM_HISTORY_CAPACITY {
        history.push(frame(index as i64 * 1_600, -20.0 + index as f32));
    }
    let bytes = encode_snapshot(request_id, &history);
    assert!(bytes.len() <= SNAPSHOT_MAX_BYTES as usize);

    let iterations = 300;
    let started = Instant::now();
    for _ in 0..iterations {
        crate::atomic_file::write_bytes_atomic(&snapshot_path(&instance_dir), &bytes).unwrap();
        let decoded = black_box(read_snapshot(&instance_dir).unwrap());
        assert_eq!(decoded.request_id, request_id);
    }
    let micros_per_exchange = started.elapsed().as_secs_f64() * 1_000_000.0 / iterations as f64;
    let projected_worker_percent = micros_per_exchange * 30.0 / 10_000.0;
    eprintln!(
        "30 Hz Spectrum file exchange: {} bytes, {micros_per_exchange:.2} us/cycle, \
         projected one-pair IO worker time {projected_worker_percent:.3}%",
        bytes.len()
    );
    assert!(projected_worker_percent < 10.0);
}

#[test]
#[ignore = "release-mode 10 Hz Perceptual Delta exchange probe; run explicitly with --nocapture"]
fn perceptual_pair_10hz_atomic_exchange_budget_is_quantified() {
    use std::hint::black_box;

    let temp = tempfile::tempdir().unwrap();
    let instance_dir = temp.path().join("project").join("pre");
    fs::create_dir_all(instance_dir.join("spectrum")).unwrap();
    let request_id = Uuid::new_v4();
    let mut history = crate::PerceptualHistory::with_capacity();
    for index in 1..=crate::PERCEPTUAL_HISTORY_CAPACITY {
        history.push(perceptual_frame(
            index as i64 * 4_800,
            1.0 + index as f64 * 0.01,
        ));
    }
    let bytes = encode_perceptual_snapshot(request_id, &history);
    assert!(bytes.len() <= perceptual_codec::PERCEPTUAL_SNAPSHOT_MAX_BYTES as usize);

    let iterations = 300;
    let started = Instant::now();
    for _ in 0..iterations {
        crate::atomic_file::write_bytes_atomic(&perceptual_snapshot_path(&instance_dir), &bytes)
            .unwrap();
        let decoded = black_box(read_perceptual_snapshot(&instance_dir).unwrap());
        assert_eq!(decoded.request_id, request_id);
    }
    let micros_per_exchange = started.elapsed().as_secs_f64() * 1_000_000.0 / iterations as f64;
    let projected_worker_percent = micros_per_exchange * 10.0 / 10_000.0;
    eprintln!(
        "10 Hz Perceptual Delta file exchange: {} bytes, {micros_per_exchange:.2} us/cycle, \
         projected one-pair IO worker time {projected_worker_percent:.3}%",
        bytes.len()
    );
    assert!(projected_worker_percent < 5.0);
}

#[test]
fn truncated_trailing_or_nonfinite_snapshot_fails_closed() {
    let mut history = SpectrumHistory::with_capacity();
    history.push(frame(4_800, -20.0));
    let bytes = encode_snapshot(Uuid::new_v4(), &history);
    assert!(decode_snapshot(&bytes[..bytes.len() - 1]).is_none());
    let mut trailing = bytes.clone();
    trailing.push(0);
    assert!(decode_snapshot(&trailing).is_none());
    let mut nonfinite = bytes;
    let first_value = 40 + 28;
    nonfinite[first_value..first_value + 4].copy_from_slice(&f32::NAN.to_le_bytes());
    assert!(decode_snapshot(&nonfinite).is_none());
}

#[test]
fn difference_joins_only_an_exact_presentation_endpoint() {
    let mut pre = SpectrumHistory::with_capacity();
    let mut post = SpectrumHistory::with_capacity();
    pre.push(frame(4_800, -20.0));
    post.push(frame(4_801, -14.0));
    assert!(newest_exact_difference(&post, &pre).is_none());
    post.push(frame(4_800, -14.0));
    let difference = newest_exact_difference(&post, &pre).unwrap();
    assert!((difference.raw_db[0] - 6.0).abs() < 1.0e-6);
}

#[test]
fn perceptual_difference_joins_only_exact_endpoint_aperture_and_epoch() {
    let mut pre = crate::PerceptualHistory::with_capacity();
    let mut post = crate::PerceptualHistory::with_capacity();
    pre.push(perceptual_frame(4_800, 1.2));
    post.push(perceptual_frame(4_801, 1.6));
    assert!(newest_exact_perceptual_difference(&post, &pre).is_none());
    post.push(perceptual_frame(4_800, 1.6));
    let difference = newest_exact_perceptual_difference(&post, &pre).unwrap();
    assert!((difference.delta_sharpness - 0.4).abs() < 1.0e-12);

    let mut incompatible = perceptual_frame(4_800, 1.7);
    incompatible.aperture_samples = 4_799;
    let mut incompatible_post = crate::PerceptualHistory::with_capacity();
    incompatible_post.push(incompatible);
    assert!(newest_exact_perceptual_difference(&incompatible_post, &pre).is_none());

    let mut wrong_epoch = perceptual_frame(4_800, 1.7);
    wrong_epoch.state_epoch_samples = -4_800;
    let mut wrong_epoch_post = crate::PerceptualHistory::with_capacity();
    wrong_epoch_post.push(wrong_epoch);
    assert!(newest_exact_perceptual_difference(&wrong_epoch_post, &pre).is_none());
}

#[test]
fn perceptual_join_recovers_every_exact_endpoint_after_a_delayed_presentation_tick() {
    let mut pre = crate::PerceptualHistory::with_capacity();
    let mut post = crate::PerceptualHistory::with_capacity();
    for index in 1..=6 {
        let endpoint = index * 4_800;
        pre.push(perceptual_frame(endpoint, 1.0 + index as f64 * 0.01));
        post.push(perceptual_frame(endpoint, 1.2 + index as f64 * 0.02));
    }
    let differences = exact_perceptual_differences(&post, &pre);
    assert_eq!(differences.len(), 6);
    assert_eq!(differences[0].presentation_end_samples, 4_800);
    assert_eq!(differences[5].presentation_end_samples, 28_800);
    assert!(differences.windows(2).all(|pair| {
        pair[1].presentation_end_samples - pair[0].presentation_end_samples == 4_800
    }));
}

#[test]
fn malformed_perceptual_payload_fails_closed() {
    let mut history = crate::PerceptualHistory::with_capacity();
    history.push(perceptual_frame(4_800, 1.2));
    let bytes = encode_perceptual_snapshot(Uuid::new_v4(), &history);
    assert!(perceptual_codec::decode_perceptual_snapshot(&bytes[..bytes.len() - 1]).is_none());
    let mut trailing = bytes.clone();
    trailing.push(0);
    assert!(perceptual_codec::decode_perceptual_snapshot(&trailing).is_none());
    let mut nonfinite = bytes;
    let sharpness_offset = 32 + 24;
    nonfinite[sharpness_offset..sharpness_offset + 8].copy_from_slice(&f64::NAN.to_le_bytes());
    assert!(perceptual_codec::decode_perceptual_snapshot(&nonfinite).is_none());
}

#[test]
fn exactly_one_post_analysis_runtime_is_active_per_process_lease() {
    let temp = tempfile::tempdir().unwrap();
    let lease_path = temp.path().join("one-analysis.lease");
    let first_runtime = SpectrumRuntime::new(48_000, 2);
    let second_runtime = SpectrumRuntime::new(48_000, 2);
    let first = SpectrumCoordinator::new_with_lease(
        48_000,
        first_runtime,
        crate::analysis_lease::AnalysisLease::at_path(lease_path.clone()),
    );
    let second = SpectrumCoordinator::new_with_lease(
        48_000,
        second_runtime,
        crate::analysis_lease::AnalysisLease::at_path(lease_path),
    );
    first.set_post_visible(true);
    second.set_post_visible(true);

    assert!(!first.post_tick("post-a", None));
    assert!(!second.post_tick("post-b", None));
    assert_eq!(first.try_view().unwrap().status, SpectrumViewStatus::NoPair);
    assert_eq!(second.try_view().unwrap().status, SpectrumViewStatus::InUse);

    first.set_post_visible(false);
    assert!(!second.post_tick("post-b", None));
    assert_eq!(
        second.try_view().unwrap().status,
        SpectrumViewStatus::NoPair
    );
    second.shutdown();
    first.shutdown();
}

#[test]
fn analysis_lease_io_failure_is_unavailable_not_in_use() {
    let temp = tempfile::tempdir().unwrap();
    let blocker = temp.path().join("file");
    fs::write(&blocker, b"not a directory").unwrap();
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new_with_lease(
        48_000,
        Arc::clone(&runtime),
        crate::analysis_lease::AnalysisLease::at_path(blocker.join("analysis.lease")),
    );
    coordinator.set_post_visible(true);
    assert!(!coordinator.post_tick("post", None));
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::Unavailable
    );
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn poisoned_post_session_resets_instead_of_permanently_stalling_analysis() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    coordinator.set_post_visible(true);

    let poisoned = Arc::clone(&coordinator);
    assert!(thread::spawn(move || {
        let _session = poisoned.post_session.lock().unwrap();
        panic!("intentional POST session poison");
    })
    .join()
    .is_err());

    assert!(!coordinator.post_tick("post", None));
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::NoPair
    );
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn poisoned_pre_session_resets_instead_of_permanently_stalling_analysis() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    fs::create_dir_all(&pre_dir).unwrap();
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));

    let poisoned = Arc::clone(&coordinator);
    assert!(thread::spawn(move || {
        let _session = poisoned.pre_session.lock().unwrap();
        panic!("intentional PRE session poison");
    })
    .join()
    .is_err());

    assert!(!coordinator.pre_tick("pre", &pre_dir));
    assert!(!runtime.is_enabled());
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn post_without_exact_pair_never_enables_fft() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    coordinator.set_post_visible(true);
    coordinator.post_tick("post", None);
    assert!(!runtime.is_enabled());
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::NoPair
    );
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn lease_enables_exact_pre_and_close_disables_it() {
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
    assert!(post_runtime.is_enabled());
    pre.pre_tick("pre", &pre_dir);
    assert!(pre_runtime.is_enabled());

    post.set_post_visible(false);
    post.post_tick("post", Some(target));
    pre.pre_tick("pre", &pre_dir);
    assert!(!post_runtime.is_enabled());
    assert!(!pre_runtime.is_enabled());
    assert!(!request_path(&pre_dir).exists());

    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
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
    post.service_post_endpoint("post", Some(target));
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

#[test]
fn normal_io_supervisor_renews_freq_and_sharp_requests_when_worker_stops_progressing() {
    let temp = tempfile::tempdir().unwrap();
    for (suffix, mode) in [
        ("freq", AnalysisViewMode::Spectrum),
        ("sharp", AnalysisViewMode::Perceptual),
    ] {
        let pre_dir = temp.path().join(suffix).join("pre");
        let pre_json = pre_dir.join("pre.json");
        crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
        let runtime = SpectrumRuntime::new(48_000, 2);
        let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
        let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

        assert!(coordinator.set_post_analysis_mode(mode));
        coordinator.set_post_visible(true);
        coordinator.service_post_endpoint("post", Some(target.clone()));
        let first = read_request(&pre_dir).unwrap();
        assert_eq!(first.analysis_mode, mode as u8);
        coordinator
            .exchange_worker
            .pause_dedicated_ticks_for_test(true);
        thread::sleep(REQUEST_RENEW_INTERVAL + Duration::from_millis(25));

        // One call may observe the last completed heartbeat; the following unchanged 10 Hz calls
        // cross the watchdog boundary without starting a second Analysis owner.
        for _ in 0..=crate::spectrum_exchange_worker::SUPERVISOR_STALL_SERVICE_TICKS {
            coordinator.service_post_endpoint("post", Some(target.clone()));
        }
        let renewed = read_request(&pre_dir).unwrap();
        assert!(renewed.expires_at_unix_ms > first.expires_at_unix_ms);
        assert_eq!(renewed.requested_by_post_instance_id, "post");
        assert_eq!(renewed.request_id, first.request_id);
        assert_eq!(renewed.analysis_mode, mode as u8);
        assert!(runtime.is_enabled());

        coordinator
            .exchange_worker
            .pause_dedicated_ticks_for_test(false);
        coordinator.set_post_visible(false);
        coordinator.shutdown();
        runtime.shutdown_and_join();
    }
}

#[test]
fn request_write_failure_is_unavailable_and_does_not_leave_fft_running() {
    let temp = tempfile::tempdir().unwrap();
    let blocked = temp.path().join("not-a-directory");
    fs::write(&blocked, b"file").unwrap();
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    coordinator.set_post_visible(true);
    coordinator.post_tick(
        "post",
        Some(SpectrumTarget {
            pre_instance_id: "pre".to_string(),
            instance_dir: blocked,
        }),
    );
    assert!(!runtime.is_enabled());
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::Unavailable
    );
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn oversized_request_and_snapshot_are_rejected_before_reading_payloads() {
    let temp = tempfile::tempdir().unwrap();
    let spectrum_dir = temp.path().join("spectrum");
    fs::create_dir_all(&spectrum_dir).unwrap();
    fs::write(
        spectrum_dir.join("request.json"),
        vec![b' '; REQUEST_MAX_BYTES as usize + 1],
    )
    .unwrap();
    fs::write(
        spectrum_dir.join("pre.bin"),
        vec![0_u8; SNAPSHOT_MAX_BYTES as usize + 1],
    )
    .unwrap();
    assert!(read_request(temp.path()).is_none());
    assert!(read_snapshot(temp.path()).is_none());
}
