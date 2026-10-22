//! Adversarial non-RT exchange order; real ingress, ready, request and exact joined output.
use super::*;
use std::sync::mpsc;
use std::thread;

const EVENTUAL: Duration = Duration::from_secs(30);

fn input(pre: &SpectrumRuntime, post: &SpectrumRuntime, start: i64, end: i64) {
    let mut position = start;
    while position < end {
        let frames = 256.min((end - position) as usize);
        let samples: Vec<f32> = (0..frames)
            .flat_map(|i| {
                let sample =
                    (std::f32::consts::TAU * 997.0 * (position + i as i64) as f32 / 48_000.0).sin()
                        * 0.25;
                [sample, sample]
            })
            .collect();
        for runtime in [pre, post] {
            assert!(runtime.push_block_from_audio(&samples, 2, Some(position)));
            assert!(runtime.wait_for_input_for_test(EVENTUAL));
        }
        position += frames as i64;
    }
}

#[test]
fn overlapping_perceptual_arm_preserves_one_epoch_and_exact_join() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let post_runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    assert!(post.set_post_analysis_mode(AnalysisViewMode::Perceptual));
    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", &pre_dir));
    input(&pre_runtime, &post_runtime, 0, 2_048);
    assert!(pre.pre_tick("pre", &pre_dir));
    let pending = post.post_session.lock().unwrap().as_ref().unwrap().clone();
    assert!(pending.state_epoch_samples.is_none());
    assert!(read_ready(&pre_dir).is_some());

    let (entered_tx, entered_rx) = mpsc::channel();
    let (resume_tx, resume_rx) = mpsc::channel();
    let first_post = Arc::clone(&post);
    let first_session = pending.clone();
    let first_target = target.clone();
    let first = thread::spawn(move || {
        first_post.arm_perceptual_session_observed(
            &first_session,
            "post",
            &first_target,
            Instant::now(),
            |epoch| {
                entered_tx.send(epoch).unwrap();
                resume_rx.recv_timeout(EVENTUAL).unwrap();
            },
        )
    });
    let first_epoch = entered_rx.recv_timeout(EVENTUAL).unwrap();
    // Advance through a different aperture, while remaining BEFORE the first future epoch.
    input(&pre_runtime, &post_runtime, 2_048, 8_192);
    assert!(8_192 < first_epoch);
    let (second_tx, second_rx) = mpsc::channel();
    let second_post = Arc::clone(&post);
    let second_target = target.clone();
    let second = thread::spawn(move || {
        let result = second_post.arm_perceptual_session_observed(
            &pending,
            "post",
            &second_target,
            Instant::now(),
            |_| {},
        );
        second_tx.send(result).unwrap();
        result
    });
    // Old code completes the stale arm here. A transaction may await the first owner.
    // Either ordering still MUST reject the stale clone and preserve the first epoch.
    let second_completed_before_resume = second_rx.recv_timeout(Duration::from_secs(1)).ok();
    resume_tx.send(()).unwrap();
    let first_result = first.join().unwrap();
    let second_result = second.join().unwrap();
    let runtime_epoch = post_runtime.perceptual_state_epoch();
    let session_epoch = post
        .post_session
        .lock()
        .unwrap()
        .as_ref()
        .unwrap()
        .state_epoch_samples;
    let request = validated_request(&pre_dir, "pre", 48_000, unix_ms_now()).unwrap();
    eprintln!(
        "overlap: first={first_epoch}, runtime={runtime_epoch:?}, session={session_epoch:?}, wire={:?}, stale_result={second_result}, completed_before_resume={second_completed_before_resume:?}",
        request.state_epoch_samples
    );
    assert!(first_result);
    assert!(
        !second_result,
        "a completed epoch cannot be replaced by a stale unarmed clone"
    );
    assert_eq!(runtime_epoch, Some(first_epoch));
    assert_eq!(session_epoch, Some(first_epoch));
    assert_eq!(request.state_epoch_samples, Some(first_epoch));

    assert!(pre.pre_tick("pre", &pre_dir));
    input(&pre_runtime, &post_runtime, 8_192, first_epoch + 9_600);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", &pre_dir));
    assert!(post.post_tick("post", Some(target.clone())));
    let view = post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    let difference = view.perceptual_difference.unwrap();
    assert_eq!(difference.state_epoch_samples, first_epoch);
    assert_eq!(difference.presentation_end_samples, first_epoch + 9_600);
    assert_eq!(difference.delta_sharpness, 0.0);
    // A stale arm arriving AFTER exact publication must not clear that current display.
    let mut stale = post.post_session.lock().unwrap().as_ref().unwrap().clone();
    stale.state_epoch_samples = None;
    let renewed = stale.last_renewed;
    assert!(!post.arm_perceptual_session(&stale, "post", &target, Instant::now()));
    assert_eq!(post.enable_current_post_runtime(&stale), None);
    let retained = post.try_view().unwrap();
    assert_eq!(retained.status, SpectrumViewStatus::Active);
    let retained_difference = retained.perceptual_difference.unwrap();
    assert_eq!(retained_difference.state_epoch_samples, first_epoch);
    assert_eq!(
        retained_difference.presentation_end_samples,
        first_epoch + 9_600
    );
    assert_eq!(post_runtime.perceptual_state_epoch(), Some(first_epoch));
    assert_eq!(
        post.post_session
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .last_renewed,
        renewed
    );
    assert!(post.set_post_analysis_mode(AnalysisViewMode::Spectrum));
    assert!(!post.arm_perceptual_session(&stale, "post", &target, Instant::now()));
    assert_eq!(post.enable_current_post_runtime(&stale), None);
    assert_eq!(post_runtime.analysis_mode(), AnalysisViewMode::Spectrum);
    assert!(post_runtime.perceptual_state_epoch().is_none());
    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}

#[test]
fn current_runtime_failure_clears_only_its_qualified_renewal() {
    let temp = tempfile::tempdir().unwrap();
    let pre_json = temp.path().join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    // An explicit Attack request without its optional runtime is a factual enable failure.
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    assert!(post.set_post_analysis_mode(AnalysisViewMode::Attack));
    post.set_post_visible(true);
    assert!(!post.post_tick("post", Some(target)));
    let current = post.post_session.lock().unwrap().as_ref().unwrap().clone();
    assert!(current.last_renewed.is_none());
    assert!(current.last_renewal_attempt.is_none());
    assert_eq!(
        post.try_view().unwrap().status,
        SpectrumViewStatus::Unavailable
    );
    assert_eq!(post.enable_current_post_runtime(&current), Some(false));
    post.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn post_rearm_survives_each_contended_guard() {
    let temp = tempfile::tempdir().unwrap();
    let pre_json = temp.path().join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    assert!(post.set_post_analysis_mode(AnalysisViewMode::Perceptual));
    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target.clone())));
    for guard in [0, 1] {
        let old = post
            .post_session
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .request_id;
        runtime.note_analysis_clock_drop_from_audio();
        if guard == 0 {
            let _held = post.post_session.lock().unwrap();
            assert!(!post.post_tick("post", Some(target.clone())));
        } else {
            let _held = post.attack_pair_authority.lock().unwrap();
            assert!(!post.post_tick("post", Some(target.clone())));
        }
        assert!(post.post_tick("post", Some(target.clone())));
        let current = post.post_session.lock().unwrap();
        let current = current.as_ref().unwrap();
        assert_ne!(current.request_id, old, "guard {guard} lost rearm");
        assert!(current.state_epoch_samples.is_none());
    }
    post.shutdown();
    runtime.shutdown_and_join();
}

#[test]
#[cfg(not(windows))]
fn delayed_unarmed_write_repairs_the_committed_epoch_even_after_lease_expiry() {
    for expire in [false, true] {
        let temp = tempfile::tempdir().unwrap();
        let pre_json = temp.path().join("pre.json");
        crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
        let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
        let pre_runtime =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let post_runtime =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
        let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
        let clock = super::super::clock::TestClock::new();
        pre.use_test_clock(&clock);
        post.use_test_clock(&clock);
        assert!(post.set_post_analysis_mode(AnalysisViewMode::Perceptual));
        post.set_post_visible(true);
        assert!(post.post_tick("post", Some(target.clone())));
        assert!(pre.pre_tick("pre", temp.path()));
        input(&pre_runtime, &post_runtime, 0, 2_048);
        assert!(pre.pre_tick("pre", temp.path()));
        let pending = post.post_session.lock().unwrap().as_ref().unwrap().clone();
        let pause = crate::atomic_file::AtomicWritePause::install(request_path(temp.path()));
        let stale_post = Arc::clone(&post);
        let stale_target = target.clone();
        let stale_session = pending.clone();
        let stale = thread::spawn(move || {
            stale_post.publish_post_request(&stale_session, "post", &stale_target)
        });
        pause.wait_until_entered();
        assert!(post.post_session.try_lock().is_ok());
        assert!(post.arm_perceptual_session(&pending, "post", &target, Instant::now()));
        let epoch = post_runtime.perceptual_state_epoch().unwrap();
        if expire {
            // One production lease boundary, not a polling sleep or an extended oracle.
            clock.advance(Duration::from_millis((REQUEST_LEASE_MS + 25) as u64));
        }
        pause.release();
        assert!(!stale.join().unwrap());
        let wire = validated_request(temp.path(), "pre", 48_000, post.clock_unix_ms())
            .expect("repair must leave a fresh lease");
        assert_eq!(wire.request_id, pending.request_id);
        assert_eq!(wire.state_epoch_samples, Some(epoch));
        assert_eq!(post_runtime.perceptual_state_epoch(), Some(epoch));
        assert!(pre.pre_tick("pre", temp.path()));
        assert_eq!(pre_runtime.perceptual_state_epoch(), Some(epoch));
        pre.shutdown();
        post.shutdown();
        pre_runtime.shutdown_and_join();
        post_runtime.shutdown_and_join();
    }
}

#[test]
fn stale_hidden_retirement_keeps_a_reopened_owner() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    post.set_post_visible(true);
    post.set_post_visible(false);
    post.set_post_visible(true);
    let reopened = post
        .post_session
        .lock()
        .unwrap()
        .as_ref()
        .unwrap()
        .request_id;
    post.retire_hidden_post_session();
    assert_eq!(
        post.post_session
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .request_id,
        reopened
    );
    assert!(post.post_visible());
    post.shutdown();
    runtime.shutdown_and_join();
}

#[test]
#[cfg(not(windows))]
fn delayed_armed_write_repairs_the_reopened_owner_or_its_same_epoch_lease() {
    for reopen in [false, true] {
        let temp = tempfile::tempdir().unwrap();
        let pre_json = temp.path().join("pre.json");
        crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
        let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
        let pre_runtime =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let post_runtime =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
        let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
        let clock = super::super::clock::TestClock::new();
        pre.use_test_clock(&clock);
        post.use_test_clock(&clock);
        assert!(post.set_post_analysis_mode(AnalysisViewMode::Perceptual));
        post.set_post_visible(true);
        assert!(post.post_tick("post", Some(target.clone())));
        assert!(pre.pre_tick("pre", temp.path()));
        input(&pre_runtime, &post_runtime, 0, 2_048);
        assert!(pre.pre_tick("pre", temp.path()));
        let pending = post.post_session.lock().unwrap().as_ref().unwrap().clone();
        assert!(post.arm_perceptual_session(&pending, "post", &target, Instant::now()));
        let armed = post.post_session.lock().unwrap().as_ref().unwrap().clone();
        let pause = crate::atomic_file::AtomicWritePause::install(request_path(temp.path()));
        let stale_post = Arc::clone(&post);
        let stale_target = target.clone();
        let stale_session = armed.clone();
        let stale = thread::spawn(move || {
            stale_post.publish_post_request(&stale_session, "post", &stale_target)
        });
        pause.wait_until_entered();
        if reopen {
            post.set_post_visible(false);
            assert!(!post_runtime.is_enabled());
            post.set_post_visible(true);
            assert!(post.post_tick("post", Some(target.clone())));
        } else {
            assert!(post.publish_post_request(&armed, "post", &target));
        }
        let current = post.post_session.lock().unwrap().as_ref().unwrap().clone();
        assert_eq!(current.request_id == armed.request_id, !reopen);
        let before = validated_request(temp.path(), "pre", 48_000, post.clock_unix_ms()).unwrap();
        assert_eq!(before.request_id, current.request_id);
        cleanup_owned_request(Some(&target), armed.request_id);
        assert_eq!(
            read_request(temp.path()).unwrap().request_id,
            current.request_id.to_string()
        );
        clock.advance(Duration::from_millis((REQUEST_LEASE_MS + 25) as u64));
        pause.release();
        assert!(!stale.join().unwrap());
        let repaired = validated_request(temp.path(), "pre", 48_000, post.clock_unix_ms())
            .expect("late write must restore the current owner with a fresh lease");
        assert_eq!(repaired.request_id, current.request_id);
        assert_eq!(repaired.state_epoch_samples, current.state_epoch_samples);
        assert_eq!(
            post_runtime.perceptual_state_epoch(),
            current.state_epoch_samples
        );
        assert!(post.try_view().unwrap().perceptual_difference.is_none());
        pre.shutdown();
        post.shutdown();
        pre_runtime.shutdown_and_join();
        post_runtime.shutdown_and_join();
    }
}

#[test]
fn pre_only_clock_drop_rotates_the_armed_post_and_rejoins_exact_new_state() {
    let temp = tempfile::tempdir().unwrap();
    let pre_json = temp.path().join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let post_runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    assert!(post.set_post_analysis_mode(AnalysisViewMode::Perceptual));
    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", temp.path()));
    input(&pre_runtime, &post_runtime, 0, 2048);
    assert!(pre.pre_tick("pre", temp.path()));
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", temp.path()));
    let old_epoch = post_runtime.perceptual_state_epoch().unwrap();
    let old_id = post
        .post_session
        .lock()
        .unwrap()
        .as_ref()
        .unwrap()
        .request_id;
    let first_end = old_epoch + 9600;
    input(&pre_runtime, &post_runtime, 2048, first_end);
    assert!(pre.pre_tick("pre", temp.path()));
    assert!(post.post_tick("post", Some(target.clone())));
    assert_eq!(post.try_view().unwrap().status, SpectrumViewStatus::Active);
    pre_runtime.note_analysis_clock_drop_from_audio();
    input(&pre_runtime, &post_runtime, first_end, first_end + 256);
    assert!(pre.pre_tick("pre", temp.path()));
    assert!(read_ready(temp.path()).unwrap().rearm_required());
    assert!(!post_runtime.take_perceptual_rearm_required());
    assert!(post.post_tick("post", Some(target.clone())));
    let new_id = post
        .post_session
        .lock()
        .unwrap()
        .as_ref()
        .unwrap()
        .request_id;
    assert_ne!(new_id, old_id);
    assert_eq!(post_runtime.perceptual_state_epoch(), None);
    // Recovery uses only the production ticks and factual new input, no manual request writes.
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", temp.path()));
    input(
        &pre_runtime,
        &post_runtime,
        first_end + 256,
        first_end + 512,
    );
    assert!(pre.pre_tick("pre", temp.path()));
    assert!(post.post_tick("post", Some(target.clone())));
    let new_epoch = post_runtime.perceptual_state_epoch().unwrap();
    assert!(new_epoch > old_epoch);
    assert!(pre.pre_tick("pre", temp.path()));
    assert_eq!(pre_runtime.perceptual_state_epoch(), Some(new_epoch));
    input(
        &pre_runtime,
        &post_runtime,
        first_end + 512,
        new_epoch + 9600,
    );
    assert!(pre.pre_tick("pre", temp.path()));
    assert!(post.post_tick("post", Some(target)));
    let view = post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    let difference = view.perceptual_difference.unwrap();
    assert_eq!(difference.state_epoch_samples, new_epoch);
    assert_eq!(difference.presentation_end_samples, new_epoch + 9600);
    assert_eq!(difference.delta_sharpness, 0.0);
    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}
