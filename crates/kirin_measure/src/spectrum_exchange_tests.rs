use super::*;
use std::thread;

fn frame(end: i64, value: f32) -> SpectrumFrame {
    SpectrumFrame {
        schema_version: SPECTRUM_SCHEMA_VERSION,
        sample_rate: 48_000,
        fft_size: SPECTRUM_FFT_SIZE as u32,
        band_count: SPECTRUM_BAND_COUNT as u16,
        presentation_end_samples: end,
        generation: 7,
        min_hz: 10.0,
        max_hz: 22_000.0,
        dbfs: [value; SPECTRUM_BAND_COUNT],
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
fn truncated_trailing_or_nonfinite_snapshot_fails_closed() {
    let mut history = SpectrumHistory::with_capacity();
    history.push(frame(4_800, -20.0));
    let bytes = encode_snapshot(Uuid::new_v4(), &history);
    assert!(decode_snapshot(&bytes[..bytes.len() - 1]).is_none());
    let mut trailing = bytes.clone();
    trailing.push(0);
    assert!(decode_snapshot(&trailing).is_none());
    let mut nonfinite = bytes;
    let first_value = 40 + 24;
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
    assert!(pre_runtime.push_block_from_audio(&pre_samples, 2, Some(0)));
    assert!(post_runtime.push_block_from_audio(&post_samples, 2, Some(0)));

    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline
        && (pre_runtime
            .try_history()
            .and_then(|history| history.newest().cloned())
            .is_none()
            || post_runtime
                .try_history()
                .and_then(|history| history.newest().cloned())
                .is_none())
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
