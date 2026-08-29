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
#[ignore = "release-mode 30 Hz Analysis transport performance probe; run explicitly with --nocapture"]
fn active_pair_30hz_analysis_transport_budget_is_quantified() {
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
        write_snapshot(&instance_dir, &bytes).unwrap();
        let decoded = black_box(read_snapshot(&instance_dir).unwrap());
        assert_eq!(decoded.request_id, request_id);
    }
    let micros_per_exchange = started.elapsed().as_secs_f64() * 1_000_000.0 / iterations as f64;
    let projected_worker_percent = micros_per_exchange * 30.0 / 10_000.0;
    eprintln!(
        "30 Hz Spectrum transport: {} bytes, {micros_per_exchange:.2} us/cycle, \
         projected one-pair IO worker time {projected_worker_percent:.3}%",
        bytes.len()
    );
    assert!(projected_worker_percent < 10.0);
}

#[test]
#[ignore = "release-mode 10 Hz Perceptual Delta transport probe; run explicitly with --nocapture"]
fn perceptual_pair_10hz_analysis_transport_budget_is_quantified() {
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
        write_perceptual_snapshot(&instance_dir, &bytes).unwrap();
        let decoded = black_box(read_perceptual_snapshot(&instance_dir).unwrap());
        assert_eq!(decoded.request_id, request_id);
    }
    let micros_per_exchange = started.elapsed().as_secs_f64() * 1_000_000.0 / iterations as f64;
    let projected_worker_percent = micros_per_exchange * 10.0 / 10_000.0;
    eprintln!(
        "10 Hz Perceptual Delta transport: {} bytes, {micros_per_exchange:.2} us/cycle, \
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
fn exactly_two_post_analysis_runtimes_are_active_per_process_lease() {
    let temp = tempfile::tempdir().unwrap();
    let lease_paths = [
        temp.path().join("analysis.0.lease"),
        temp.path().join("analysis.1.lease"),
    ];
    let first_runtime = SpectrumRuntime::new(48_000, 2);
    let second_runtime = SpectrumRuntime::new(48_000, 2);
    let third_runtime = SpectrumRuntime::new(48_000, 2);
    let first = SpectrumCoordinator::new_with_lease(
        48_000,
        first_runtime,
        crate::analysis_lease::AnalysisLease::at_paths(lease_paths.clone()),
    );
    let second = SpectrumCoordinator::new_with_lease(
        48_000,
        second_runtime,
        crate::analysis_lease::AnalysisLease::at_paths(lease_paths.clone()),
    );
    let third = SpectrumCoordinator::new_with_lease(
        48_000,
        third_runtime,
        crate::analysis_lease::AnalysisLease::at_paths(lease_paths),
    );
    first.set_post_visible(true);
    second.set_post_visible(true);
    third.set_post_visible(true);

    assert!(!first.post_tick_for_owner("post-a", None, "Mix"));
    assert!(!second.post_tick_for_owner("post-b", None, "Vocal"));
    assert!(!third.post_tick_for_owner("post-c", None, "Music"));
    assert_eq!(first.try_view().unwrap().status, SpectrumViewStatus::NoPair);
    assert_eq!(
        second.try_view().unwrap().status,
        SpectrumViewStatus::NoPair
    );
    assert_eq!(third.try_view().unwrap().status, SpectrumViewStatus::InUse);
    assert_eq!(
        third.try_view().unwrap().analysis_owner_names,
        ["Mix".to_string(), "Vocal".to_string()]
    );

    // FREQ -> SHARP -> LIVE changes only the analyzer inside the first owner's existing slot.
    // A waiting third page must not observe a release until the owner explicitly returns to
    // METERS (set_post_visible(false)) or closes.
    assert!(first.set_post_analysis_mode(AnalysisViewMode::Perceptual));
    assert!(!third.post_tick_for_owner("post-c", None, "Music"));
    assert_eq!(third.try_view().unwrap().status, SpectrumViewStatus::InUse);
    assert_eq!(
        third.try_view().unwrap().analysis_owner_names,
        ["Mix".to_string(), "Vocal".to_string()]
    );
    assert!(first.set_post_analysis_mode(AnalysisViewMode::Absolute));
    assert!(!third.post_tick_for_owner("post-c", None, "Music"));
    assert_eq!(third.try_view().unwrap().status, SpectrumViewStatus::InUse);

    first.set_post_visible(false);
    assert!(!third.post_tick_for_owner("post-c", None, "Music"));
    assert_eq!(third.try_view().unwrap().status, SpectrumViewStatus::NoPair);
    third.shutdown();
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
fn post_absolute_timeline_needs_no_pair_and_never_creates_a_pre_request() {
    let temp = tempfile::tempdir().unwrap();
    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Absolute));
    let coordinator = SpectrumCoordinator::new_with_lease(
        48_000,
        Arc::clone(&runtime),
        crate::analysis_lease::AnalysisLease::at_paths([
            temp.path().join("analysis.0.lease"),
            temp.path().join("analysis.1.lease"),
        ]),
    );
    coordinator.set_post_visible(true);
    assert!(coordinator.post_tick("post", None));
    assert!(runtime.is_enabled());
    let view = coordinator.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::WarmingUp);
    assert_eq!(view.analysis_mode, AnalysisViewMode::Absolute);
    assert!(view.difference.is_none());
    assert!(view.perceptual_difference.is_none());
    assert!(!temp.path().join("spectrum").join("request.json").exists());
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn active_spectrum_view_retains_eight_exact_differences_for_ui_recovery() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));

    for index in 1..=10 {
        let endpoint = index * 1_600;
        let pre = frame(endpoint, -30.0);
        let post = frame(endpoint, -27.0);
        let difference = crate::difference_post_minus_pre(&post, &pre).unwrap();
        coordinator.store_view(SpectrumViewStatus::Active, Some(difference), None);
    }

    let view = coordinator.try_view().unwrap();
    let endpoints = view
        .spectrum_timeline
        .frames()
        .map(|frame| frame.presentation_end_samples)
        .collect::<Vec<_>>();
    assert_eq!(
        endpoints,
        (3..=10).map(|index| index * 1_600).collect::<Vec<_>>()
    );
    assert_eq!(
        view.difference.as_ref().unwrap().presentation_end_samples,
        10 * 1_600
    );
    assert!((view.difference.as_ref().unwrap().display_db[0] - 3.0).abs() < 1.0e-6);

    let pre = frame(10 * 1_600, -30.0);
    let conflicting_duplicate = frame(10 * 1_600, -12.0);
    let conflicting_duplicate =
        crate::difference_post_minus_pre(&conflicting_duplicate, &pre).unwrap();
    coordinator.store_view(
        SpectrumViewStatus::Active,
        Some(conflicting_duplicate),
        None,
    );
    let stable = coordinator.try_view().unwrap();
    assert!((stable.difference.unwrap().display_db[0] - 3.0).abs() < 1.0e-6);
    assert_eq!(stable.spectrum_timeline.frames().count(), 8);

    coordinator.store_view(SpectrumViewStatus::WarmingUp, None, None);
    assert_eq!(
        coordinator
            .try_view()
            .unwrap()
            .spectrum_timeline
            .frames()
            .count(),
        0
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
