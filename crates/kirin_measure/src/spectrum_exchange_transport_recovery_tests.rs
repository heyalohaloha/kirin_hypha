//! Transport boundary and request-failure recovery fixtures.
use super::*;

#[test]
fn confirmed_backwards_transport_boundary_restarts_the_freq_timeline() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let mut session = PostSession {
        request_id: Uuid::new_v4(),
        target: None,
        last_renewed: None,
        last_renewal_attempt: None,
        started_at: Some(Instant::now() - WARMUP_LIMIT),
        last_presented_at: None,
        last_presented_end_samples: None,
        analysis_mode: AnalysisViewMode::Spectrum,
        channel_mode: SpectrumChannelMode::Lr,
        state_epoch_samples: None,
    };
    let now = Instant::now();
    let mut pre = SpectrumHistory::with_capacity();
    let mut post = SpectrumHistory::with_capacity();
    pre.push(frame(480_000, -20.0));
    post.push(frame(480_000, -14.0));
    store_joined_spectrum(&coordinator, &mut session, now, Some(&post), Some(&pre));
    assert_eq!(
        coordinator
            .try_view()
            .unwrap()
            .difference
            .unwrap()
            .presentation_end_samples,
        480_000
    );

    // Both newest histories now agree on a lower endpoint. This is a factual transport boundary,
    // not one delayed worker result, and must replace the old UI-recovery generation.
    pre.push(frame(4_800, -18.0));
    post.push(frame(4_800, -12.0));
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now + Duration::from_millis(34),
        Some(&post),
        Some(&pre),
    );
    let restarted = coordinator.try_view().unwrap();
    assert_eq!(restarted.status, SpectrumViewStatus::Active);
    assert_eq!(
        restarted
            .difference
            .as_ref()
            .unwrap()
            .presentation_end_samples,
        4_800
    );
    assert_eq!(
        restarted
            .spectrum_timeline
            .frames()
            .map(|frame| frame.presentation_end_samples)
            .collect::<Vec<_>>(),
        vec![4_800]
    );

    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn staggered_backwards_transport_workers_restart_freq_at_their_exact_intersection() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let mut session = PostSession {
        request_id: Uuid::new_v4(),
        target: None,
        last_renewed: None,
        last_renewal_attempt: None,
        started_at: Some(Instant::now() - WARMUP_LIMIT),
        last_presented_at: None,
        last_presented_end_samples: None,
        analysis_mode: AnalysisViewMode::Spectrum,
        channel_mode: SpectrumChannelMode::Lr,
        state_epoch_samples: None,
    };
    let now = Instant::now();
    let mut pre = SpectrumHistory::with_capacity();
    let mut post = SpectrumHistory::with_capacity();
    pre.push(frame(480_000, -20.0));
    post.push(frame(480_000, -14.0));
    store_joined_spectrum(&coordinator, &mut session, now, Some(&post), Some(&pre));

    // PRE is one cadence ahead after a backwards seek. Both sides have crossed the old
    // endpoint, and 4,800 is their newest exact intersection.
    pre.push(frame(4_800, -19.0));
    pre.push(frame(6_400, -18.0));
    post.push(frame(4_800, -13.0));
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now + Duration::from_millis(34),
        Some(&post),
        Some(&pre),
    );
    let restarted = coordinator.try_view().unwrap();
    assert_eq!(restarted.status, SpectrumViewStatus::Active);
    assert_eq!(
        restarted
            .difference
            .as_ref()
            .unwrap()
            .presentation_end_samples,
        4_800
    );
    assert_eq!(
        restarted
            .spectrum_timeline
            .frames()
            .map(|frame| frame.presentation_end_samples)
            .collect::<Vec<_>>(),
        vec![4_800]
    );
    assert_eq!(session.last_presented_end_samples, Some(4_800));

    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn one_sided_lower_freq_result_cannot_move_the_presentation_backwards() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let mut session = PostSession {
        request_id: Uuid::new_v4(),
        target: None,
        last_renewed: None,
        last_renewal_attempt: None,
        started_at: Some(Instant::now() - WARMUP_LIMIT),
        last_presented_at: None,
        last_presented_end_samples: None,
        analysis_mode: AnalysisViewMode::Spectrum,
        channel_mode: SpectrumChannelMode::Lr,
        state_epoch_samples: None,
    };
    let now = Instant::now();
    let mut pre = SpectrumHistory::with_capacity();
    let mut post = SpectrumHistory::with_capacity();
    pre.push(frame(480_000, -20.0));
    post.push(frame(480_000, -14.0));
    store_joined_spectrum(&coordinator, &mut session, now, Some(&post), Some(&pre));
    let original = coordinator.try_view().unwrap();

    // A lower POST frame can match an old PRE history point while PRE's newest endpoint still
    // belongs to the current run. This is not enough evidence to replace the display.
    pre.push(frame(4_800, -19.0));
    pre.push(frame(481_600, -18.0));
    post.push(frame(4_800, -13.0));
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now + Duration::from_millis(34),
        Some(&post),
        Some(&pre),
    );
    assert_eq!(coordinator.try_view().unwrap(), original);
    assert_eq!(session.last_presented_end_samples, Some(480_000));
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now + PRESENTATION_HOLD + Duration::from_millis(1),
        Some(&post),
        Some(&pre),
    );
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::Unavailable
    );
    assert_eq!(session.last_presented_end_samples, Some(480_000));

    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn repeated_stale_sharpness_endpoint_does_not_extend_the_gap_hold() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Perceptual));
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let mut session = PostSession {
        request_id: Uuid::new_v4(),
        target: None,
        last_renewed: None,
        last_renewal_attempt: None,
        started_at: Some(Instant::now() - WARMUP_LIMIT),
        last_presented_at: None,
        last_presented_end_samples: None,
        analysis_mode: AnalysisViewMode::Perceptual,
        channel_mode: SpectrumChannelMode::Lr,
        state_epoch_samples: Some(0),
    };
    let now = Instant::now();
    let mut pre = crate::PerceptualHistory::with_capacity();
    let mut post = crate::PerceptualHistory::with_capacity();
    pre.push(perceptual_frame(4_800, 1.0));
    post.push(perceptual_frame(4_800, 1.4));
    store_joined_perceptual(&coordinator, &mut session, now, Some(&post), Some(&pre));
    let active = coordinator.try_view().unwrap();
    assert_eq!(active.status, SpectrumViewStatus::Active);
    assert_eq!(active.perceptual_timeline.frames().len(), 1);

    post.push(perceptual_frame(9_600, 1.5));
    store_joined_perceptual(
        &coordinator,
        &mut session,
        now + PRESENTATION_HOLD / 2,
        Some(&post),
        Some(&pre),
    );
    assert_eq!(coordinator.try_view().unwrap(), active);
    store_joined_perceptual(
        &coordinator,
        &mut session,
        now + PRESENTATION_HOLD + Duration::from_millis(1),
        Some(&post),
        Some(&pre),
    );
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::Unavailable
    );

    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
#[cfg(not(windows))]
fn transient_request_renewal_failure_keeps_the_last_view_and_analysis_runtime_alive() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    coordinator.set_post_visible(true);
    assert!(coordinator.post_tick("post", Some(target.clone())));
    coordinator.store_view(SpectrumViewStatus::Active, None, None);
    let held = coordinator.try_view().unwrap();
    let spectrum_dir = pre_dir.join("spectrum");
    fs::remove_dir_all(&spectrum_dir).unwrap();
    fs::write(&spectrum_dir, b"block request renewal").unwrap();
    thread::sleep(REQUEST_RENEW_INTERVAL + Duration::from_millis(25));

    assert!(!coordinator.post_tick("post", Some(target)));
    assert!(runtime.is_enabled());
    assert_eq!(coordinator.try_view().unwrap(), held);

    fs::remove_file(spectrum_dir).unwrap();
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
#[cfg(not(windows))]
fn request_write_failure_is_unavailable_and_does_not_leave_fft_running() {
    let temp = tempfile::tempdir().unwrap();
    let blocked = temp.path().join("not-a-directory");
    fs::write(&blocked, b"file").unwrap();
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
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
