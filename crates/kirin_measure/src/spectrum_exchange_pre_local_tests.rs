use super::*;
fn fixture() -> (
    Arc<SpectrumCoordinator>,
    Arc<SpectrumRuntime>,
    tempfile::TempDir,
    Arc<clock::TestClock>,
) {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let clock = clock::TestClock::new();
    pre.use_test_clock(&clock);
    (pre, runtime, tempfile::tempdir().unwrap(), clock)
}
fn request(pre: &SpectrumCoordinator, dir: &Path, id: Uuid) {
    write_request(
        dir,
        &AnalysisRequest {
            schema: REQUEST_SCHEMA.into(),
            request_id: id.to_string(),
            requested_by_post_instance_id: "post".into(),
            target_pre_instance_id: "pre".into(),
            sample_rate: 48_000,
            analysis_mode: AnalysisViewMode::Perceptual as u8,
            channel_mode: SpectrumChannelMode::Lr as u8,
            spectrum_scope: 0,
            state_epoch_samples: None,
            expires_at_unix_ms: pre.clock_unix_ms() + REQUEST_LEASE_MS,
            attack_band: None,
        },
    )
    .unwrap();
}
#[test]
fn pre_local_token_foreign_release_expiry_and_no_poll_renewal() {
    let (pre, runtime, dir, clock) = fixture();
    let token = pre.request_pre_sharp(0);
    assert_ne!(token, 0);
    assert_eq!(pre.request_pre_sharp(0), 0);
    assert!(pre.pre_tick("pre", dir.path()));
    assert!(runtime.is_enabled());
    assert!(!pre.release_pre_sharp(token + 1));
    assert!(runtime.is_enabled());
    assert!(pre.poll_pre_sharp(token).is_none()); // no input is never zero
    clock.advance(Duration::from_millis(1501));
    assert!(pre.poll_pre_sharp(token).is_none());
    assert_eq!(pre.request_pre_sharp(token), 0);
    assert!(!pre.pre_tick("pre", dir.path()));
    assert!(!runtime.is_enabled());
    let next = pre.request_pre_sharp(0);
    assert!(next > token);
    assert!(!pre.release_pre_sharp(token));
    assert!(pre.release_pre_sharp(next));
    pre.shutdown();
    runtime.shutdown_and_join();
}
#[test]
fn pre_local_external_priority_and_tombstone_survives_local_resume() {
    let (pre, runtime, dir, _) = fixture();
    let token = pre.request_pre_sharp(0);
    assert_ne!(token, 0);
    assert!(pre.pre_tick("pre", dir.path()));
    let id = Uuid::new_v4();
    request(&pre, dir.path(), id);
    assert!(pre.pre_tick("pre", dir.path()));
    assert_eq!(pre.pre_local.lock().unwrap().owner, Owner::External(id));
    assert!(pre.poll_pre_sharp(token).is_none());
    // Model the existing clock-rearm tombstone without altering its contract.
    pre.pre_session
        .lock()
        .unwrap()
        .as_mut()
        .unwrap()
        .awaiting_rearm = true;
    remove_request(dir.path());
    assert!(pre.pre_tick("pre", dir.path()));
    assert_eq!(pre.pre_local.lock().unwrap().owner, Owner::Local(token));
    assert!(
        pre.pre_session
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .awaiting_rearm
    );
    let generation = pre.pre_local.lock().unwrap().generation;
    assert!(pre.pre_tick("pre", dir.path()));
    assert_eq!(pre.pre_local.lock().unwrap().generation, generation);
    request(&pre, dir.path(), id);
    assert!(pre.pre_tick("pre", dir.path()));
    assert!(
        pre.pre_session
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .awaiting_rearm
    );
    assert_eq!(runtime.perceptual_state_epoch(), None);
    let fresh = Uuid::new_v4();
    request(&pre, dir.path(), fresh);
    assert!(pre.pre_tick("pre", dir.path()));
    assert!(
        !pre.pre_session
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .awaiting_rearm
    );
    assert!(pre.release_pre_sharp(token));
    // Releasing local demand cannot turn off external POST's runtime.
    assert!(runtime.is_enabled());
    pre.shutdown();
    runtime.shutdown_and_join();
}
#[test]
fn pre_local_ui_never_waits_or_takes_over_an_external_publication() {
    let (pre, runtime, dir, _) = fixture();
    let token = pre.request_pre_sharp(0);
    assert!(pre.pre_tick("pre", dir.path()));
    let guard = pre.pre_control.read().unwrap();
    assert_eq!(pre.request_pre_sharp(token), 0);
    assert!(!pre.release_pre_sharp(token));
    assert!(pre.poll_pre_sharp(token).is_none());
    drop(guard);
    assert!(pre.release_pre_sharp(token));
    assert!(!runtime.is_enabled());
    pre.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn pre_local_clock_change_invalidates_own_epoch_without_clearing_tombstone() {
    let (pre, runtime, dir, _) = fixture();
    let token = pre.request_pre_sharp(0);
    assert!(pre.pre_tick("pre", dir.path()));
    assert!(runtime.push_block_from_audio(&[0.1; 512], 2, Some(0)));
    assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
    assert!(pre.pre_tick("pre", dir.path()));
    let before = pre.pre_local.lock().unwrap().generation;
    assert!(pre.pre_local.lock().unwrap().epoch.is_some());
    runtime.note_analysis_clock_drop_from_audio();
    assert!(pre.poll_pre_sharp(token).is_none());
    assert!(pre.pre_tick("pre", dir.path()));
    assert!(pre.pre_local.lock().unwrap().generation > before);
    assert!(pre.release_pre_sharp(token));
    assert!(pre.poll_pre_sharp(token).is_none());
    pre.shutdown();
    runtime.shutdown_and_join();
}
