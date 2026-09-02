use super::{
    drop_commit_matches_observed_capture, idle_autostop_due, parse_idle_timeout,
    record_idle_timeout,
};
use crate::record_expected::ExpectedWavMetadata;
use crate::record_take::{RecordTakeBlock, RecordTakeTracker};
use std::time::Duration;

#[test]
fn record_idle_timeout_is_enabled_by_default() {
    assert_eq!(record_idle_timeout(), Some(Duration::from_secs(600)));
}

/// B-206: timeout override パース。無効/欠落/下限未満は既定 600s、有効値は採用。
#[test]
fn parse_idle_timeout_override() {
    assert_eq!(
        parse_idle_timeout(None),
        Duration::from_secs(600),
        "欠落 → 既定600s"
    );
    assert_eq!(
        parse_idle_timeout(Some("60".into())),
        Duration::from_secs(60),
        "有効値採用"
    );
    assert_eq!(
        parse_idle_timeout(Some(" 30 ".into())),
        Duration::from_secs(30),
        "trim して採用"
    );
    assert_eq!(
        parse_idle_timeout(Some("abc".into())),
        Duration::from_secs(600),
        "非数 → 既定"
    );
    assert_eq!(
        parse_idle_timeout(Some("0".into())),
        Duration::from_secs(600),
        "下限未満(0) → 既定"
    );
    assert_eq!(
        parse_idle_timeout(Some("4".into())),
        Duration::from_secs(600),
        "下限未満(4) → 既定"
    );
    assert_eq!(
        parse_idle_timeout(Some("5".into())),
        Duration::from_secs(5),
        "下限ちょうど → 採用"
    );
}

/// B-206: idle auto-stop 判定の境界。録音中×非Active×しきい値到達でのみ true。
#[test]
fn idle_autostop_due_boundary() {
    let t = Duration::from_secs(600);
    // 録音中・非Active・10分到達/超過 → 停止
    assert!(
        idle_autostop_due(true, false, Duration::from_secs(600), Some(t)),
        "ちょうど10分で停止"
    );
    assert!(
        idle_autostop_due(true, false, Duration::from_secs(601), Some(t)),
        "10分超で停止"
    );
    // 10分未満 → 停止しない
    assert!(
        !idle_autostop_due(true, false, Duration::from_secs(599), Some(t)),
        "10分未満は停止しない"
    );
    // Active 中は経過に関わらず絶対に停止しない（録音継続中）
    assert!(
        !idle_autostop_due(true, true, Duration::from_secs(99_999), Some(t)),
        "Active 中は停止しない"
    );
    // 非録音は停止対象外
    assert!(
        !idle_autostop_due(false, false, Duration::from_secs(99_999), Some(t)),
        "非録音は対象外"
    );
    assert!(
        !idle_autostop_due(true, false, Duration::from_secs(99_999), None),
        "timeout disabledなら停止権限を持たない"
    );
}

#[test]
fn drop_commit_requires_bwf_range_observed_by_this_post() {
    let tracker = RecordTakeTracker::new();
    tracker.note_capture_window(true, 96_000, 48_000);
    let mut expected = ExpectedWavMetadata {
        expected_duration_samples: 48_000,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: Some(96_000),
        wav_path: "/tmp/drop.wav".to_string(),
        bounce_id: "bounce".to_string(),
        created_at_ms: chrono::Utc::now().timestamp_millis(),
        wav_file_size: Some(1),
        wav_mtime_ms: chrono::Utc::now().timestamp_millis(),
        wav_hash: Some("hash".to_string()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };
    assert!(drop_commit_matches_observed_capture(&expected, &tracker, 1));

    expected.wav_time_reference_samples = Some(192_000);
    assert!(!drop_commit_matches_observed_capture(
        &expected, &tracker, 1
    ));
    expected.wav_time_reference_samples = None;
    assert!(!drop_commit_matches_observed_capture(
        &expected, &tracker, 1
    ));
}

#[test]
fn non_bwf_drop_commit_requires_exact_current_render_generation_and_duration() {
    let tracker = RecordTakeTracker::new();
    tracker.note_block(RecordTakeBlock {
        generation: 7,
        recording: true,
        rendered: true,
        playing: true,
        offline: true,
        position_valid: true,
        position_samples: 96_000,
        num_frames: 48_000,
        clock_start_samples: 96_000,
        clock_end_samples: Some(144_000),
    });
    let mut expected = ExpectedWavMetadata {
        expected_duration_samples: 48_000,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: None,
        wav_path: "/tmp/drop-no-bwf.wav".to_string(),
        bounce_id: "bounce-no-bwf".to_string(),
        created_at_ms: chrono::Utc::now().timestamp_millis(),
        wav_file_size: Some(1),
        wav_mtime_ms: chrono::Utc::now().timestamp_millis(),
        wav_hash: Some("hash-no-bwf".to_string()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };
    assert!(drop_commit_matches_observed_capture(&expected, &tracker, 7));
    assert!(!drop_commit_matches_observed_capture(
        &expected, &tracker, 8
    ));
    expected.expected_duration_samples += 1;
    assert!(!drop_commit_matches_observed_capture(
        &expected, &tracker, 7
    ));
}
