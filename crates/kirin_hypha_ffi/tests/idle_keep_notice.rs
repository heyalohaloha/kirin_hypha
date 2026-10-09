//! A real paired Keep auto-stop reports a consumable result without latching a fault.
//! This binary has one test so its private storage/timeout environment is process-isolated.
use kirin_hypha_ffi::{channel_abi::ChannelLayout, KirinHyphaEngine, KIRIN_KEEP_PHASE_ARMED};
use kirin_measure::{
    AuxiliaryClockSamples, CaptureClockSource, PresentationLatencySamples, RecordTakeBlock,
};
use std::thread::sleep;
use std::time::{Duration, Instant};

fn block(engine: &KirinHyphaEngine, position: i64) {
    let recording = engine.is_recording();
    engine.set_signal_state(1);
    engine.note_record_block(RecordTakeBlock {
        generation: 0,
        recording,
        rendered: recording,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: 512,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    engine.note_capture_window_with_clocks(
        true,
        position,
        512,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples::default(),
        AuxiliaryClockSamples::default(),
        false,
    );
    engine.push_samples(&[0.05; 1024], 2);
}

#[test]
fn real_idle_keep_completion_is_seen_once_and_not_replayed_by_watch_or_reopen() {
    let sandbox = tempfile::tempdir().unwrap();
    std::env::set_var(kirin_measure::TEST_STORAGE_ROOT_ENV, sandbox.path());
    std::env::set_var("KIRIN_RECORD_IDLE_TIMEOUT_SECS", "5");
    let paths = kirin_measure::PlatformPaths::default_current()
        .unwrap()
        .storage;
    std::fs::create_dir_all(&paths.kirin_os_root).unwrap();
    std::fs::write(paths.primary_path(), br#"{"schema_version":"1.0","installation_id":"idle-notice-fixture","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#).unwrap();
    let pre = KirinHyphaEngine::new(48000, ChannelLayout::stereo());
    pre.set_identity(
        "idle-pre".into(),
        "idle-project".into(),
        "".into(),
        "mix".into(),
    );
    pre.enable_pre_writes();
    let post = KirinHyphaEngine::new(48000, ChannelLayout::stereo());
    post.set_identity(
        "idle-post".into(),
        "idle-project".into(),
        "".into(),
        "mix".into(),
    );
    post.enable_post_writes();
    post.set_pair_target("mix".into());
    let mut position = 0;
    for _ in 0..120 {
        block(&pre, position);
        block(&post, position);
        position += 512;
        sleep(Duration::from_millis(11));
    }
    assert!(post.keep(), "private paired Keep accepted");
    let deadline = Instant::now() + Duration::from_secs(8);
    while post.keep_phase() != KIRIN_KEEP_PHASE_ARMED || !post.is_recording() {
        assert!(Instant::now() < deadline, "paired Keep admission deadline");
        block(&pre, position);
        block(&post, position);
        position += 512;
        sleep(Duration::from_millis(11));
    }
    for _ in 0..30 {
        block(&pre, position);
        block(&post, position);
        position += 512;
        sleep(Duration::from_millis(11));
    }
    pre.set_signal_state(0);
    post.set_signal_state(0);
    let deadline = Instant::now() + Duration::from_secs(8);
    let notice = loop {
        assert!(
            post.record_error_message().is_none(),
            "successful idle completion never latches a fault"
        );
        if let Some(notice) = post.drain_keep_action_notice() {
            break notice;
        }
        assert!(
            Instant::now() < deadline,
            "actual idle-stop notification deadline"
        );
        sleep(Duration::from_millis(20));
    };
    assert!(
        notice.starts_with("Auto-stopped after 5 sec idle."),
        "actual result: {notice}"
    );
    assert!(!post.is_recording());
    assert_eq!(post.drain_keep_action_notice(), None);
    sleep(Duration::from_millis(300));
    assert_eq!(
        post.drain_keep_action_notice(),
        None,
        "Watch polls cannot replay completion"
    );
    assert_eq!(post.record_error_message(), None);
    drop(post);
    let reopened = KirinHyphaEngine::new(48000, ChannelLayout::stereo());
    reopened.set_identity(
        "idle-post".into(),
        "idle-project".into(),
        "".into(),
        "mix".into(),
    );
    reopened.enable_post_writes();
    sleep(Duration::from_millis(150));
    assert_eq!(reopened.record_error_message(), None);
    assert_eq!(
        reopened.drain_keep_action_notice(),
        None,
        "reopening cannot revive the consumed result"
    );
}
