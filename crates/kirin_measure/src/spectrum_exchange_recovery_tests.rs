use super::*;
use crate::spectrum::{
    SpectrumFrame, SPECTRUM_BAND_COUNT, SPECTRUM_FFT_SIZE, SPECTRUM_SCHEMA_VERSION,
};
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

#[cfg(not(windows))]
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

        // One call may observe the last published update; the following unchanged 10 Hz calls
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
fn failed_worker_attempts_do_not_hide_a_stalled_request_from_the_supervisor() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    coordinator.set_post_visible(true);
    coordinator.service_post_endpoint("post", Some(target.clone()));
    let first = read_request(&pre_dir).unwrap();
    coordinator
        .exchange_worker
        .fail_dedicated_ticks_for_test(true);
    thread::sleep(REQUEST_RENEW_INTERVAL + Duration::from_millis(50));

    for _ in 0..=crate::spectrum_exchange_worker::SUPERVISOR_STALL_SERVICE_TICKS {
        coordinator.service_post_endpoint("post", Some(target.clone()));
    }
    let renewed = read_request(&pre_dir).unwrap();
    assert!(renewed.expires_at_unix_ms > first.expires_at_unix_ms);
    assert_eq!(renewed.request_id, first.request_id);

    coordinator
        .exchange_worker
        .fail_dedicated_ticks_for_test(false);
    coordinator.set_post_visible(false);
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
#[cfg(not(windows))]
fn failed_pre_snapshot_write_is_not_reported_as_exchange_progress() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, 2);
    let post_runtime = SpectrumRuntime::new(48_000, 2);
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target)));
    assert!(pre.pre_tick("pre", &pre_dir));

    let frames = 9_600_usize;
    let mut samples = Vec::with_capacity(frames * 2);
    for index in 0..frames {
        let sample = (std::f32::consts::TAU * 1_000.0 * index as f32 / 48_000.0).sin();
        samples.extend_from_slice(&[sample, -sample]);
    }
    push_stereo_pair_in_blocks(&pre_runtime, &post_runtime, &samples, &samples);
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline && pre_runtime.try_history().is_none() {
        thread::sleep(Duration::from_millis(2));
    }
    assert!(pre_runtime.try_history().is_some());

    fs::create_dir_all(snapshot_path(&pre_dir)).unwrap();
    assert!(!pre.pre_tick("pre", &pre_dir));
    fs::remove_dir(snapshot_path(&pre_dir)).unwrap();
    assert!(pre.pre_tick("pre", &pre_dir));
    assert!(read_snapshot(&pre_dir).is_some());

    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}

#[test]
fn transient_exchange_gap_holds_freq_and_sharp_presentations_until_the_lease_boundary() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let mut session = PostSession {
        request_id: Uuid::new_v4(),
        target: None,
        last_renewed: None,
        last_renewal_attempt: None,
        started_at: None,
        last_presented_at: None,
        last_presented_end_samples: None,
        analysis_mode: AnalysisViewMode::Spectrum,
        channel_mode: SpectrumChannelMode::Lr,
        state_epoch_samples: None,
    };
    let now = Instant::now();
    session.started_at = Some(now - WARMUP_LIMIT);

    let mut pre_spectrum = SpectrumHistory::with_capacity();
    let mut post_spectrum = SpectrumHistory::with_capacity();
    pre_spectrum.push(frame(4_800, -20.0));
    post_spectrum.push(frame(4_800, -14.0));
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now,
        Some(&post_spectrum),
        Some(&pre_spectrum),
    );
    let active_freq = coordinator.try_view().unwrap();
    assert_eq!(active_freq.status, SpectrumViewStatus::Active);
    assert!(active_freq.difference.is_some());
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now + PRESENTATION_HOLD / 2,
        Some(&post_spectrum),
        None,
    );
    assert_eq!(coordinator.try_view().unwrap(), active_freq);
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now + PRESENTATION_HOLD + Duration::from_millis(1),
        Some(&post_spectrum),
        None,
    );
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::Unavailable
    );

    let mut pre_perceptual = crate::PerceptualHistory::with_capacity();
    let mut post_perceptual = crate::PerceptualHistory::with_capacity();
    pre_perceptual.push(perceptual_frame(4_800, 1.0));
    post_perceptual.push(perceptual_frame(4_800, 1.4));
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Perceptual));
    session.analysis_mode = AnalysisViewMode::Perceptual;
    session.last_presented_at = None;
    session.last_presented_end_samples = None;
    let sharp_now = now + PRESENTATION_HOLD + Duration::from_secs(1);
    store_joined_perceptual(
        &coordinator,
        &mut session,
        sharp_now,
        Some(&post_perceptual),
        Some(&pre_perceptual),
    );
    let active_sharp = coordinator.try_view().unwrap();
    assert_eq!(active_sharp.status, SpectrumViewStatus::Active);
    assert!(active_sharp.perceptual_timeline.frames().len() > 0);
    store_joined_perceptual(
        &coordinator,
        &mut session,
        sharp_now + PRESENTATION_HOLD / 2,
        Some(&post_perceptual),
        None,
    );
    assert_eq!(coordinator.try_view().unwrap(), active_sharp);

    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn repeated_stale_exact_endpoint_does_not_extend_the_gap_hold() {
    let runtime = SpectrumRuntime::new(48_000, 2);
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
    pre.push(frame(4_800, -20.0));
    post.push(frame(4_800, -14.0));
    store_joined_spectrum(&coordinator, &mut session, now, Some(&post), Some(&pre));
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::Active
    );

    post.push(frame(9_600, -14.0));
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now + PRESENTATION_HOLD / 2,
        Some(&post),
        Some(&pre),
    );
    assert_eq!(
        coordinator.try_view().unwrap().status,
        SpectrumViewStatus::Active
    );
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

    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn stationary_exact_endpoint_remains_visible_when_both_sides_stop() {
    let runtime = SpectrumRuntime::new(48_000, 2);
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
    pre.push(frame(4_800, -20.0));
    post.push(frame(4_800, -14.0));
    store_joined_spectrum(&coordinator, &mut session, now, Some(&post), Some(&pre));
    let held = coordinator.try_view().unwrap();
    store_joined_spectrum(
        &coordinator,
        &mut session,
        now + PRESENTATION_HOLD + Duration::from_secs(1),
        Some(&post),
        Some(&pre),
    );
    assert_eq!(coordinator.try_view().unwrap(), held);

    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn confirmed_backwards_transport_boundary_restarts_the_freq_timeline() {
    let runtime = SpectrumRuntime::new(48_000, 2);
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
fn repeated_stale_sharpness_endpoint_does_not_extend_the_gap_hold() {
    let runtime = SpectrumRuntime::new(48_000, 2);
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
    let runtime = SpectrumRuntime::new(48_000, 2);
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
