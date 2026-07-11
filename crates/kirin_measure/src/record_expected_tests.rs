use super::*;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Barrier};
use std::thread;

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir().join(format!(
        "kirin_record_expected_test_{}_{}",
        std::process::id(),
        n
    ));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn metadata_fixture(bounce_id: &str) -> ExpectedWavMetadata {
    ExpectedWavMetadata {
        expected_duration_samples: 48_000,
        expected_sample_rate: 48_000,
        wav_path: format!("/tmp/{bounce_id}.wav"),
        bounce_id: bounce_id.to_string(),
        created_at_ms: now_epoch_ms(),
        wav_file_size: Some(1_000),
        wav_mtime_ms: now_epoch_ms(),
        wav_hash: Some(format!("hash-{bounce_id}")),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    }
}

#[test]
fn expected_metadata_roundtrips_under_project_dir() {
    let base = isolated_dir();
    let metadata = ExpectedWavMetadata {
        expected_duration_samples: 1_440_000,
        expected_sample_rate: 96_000,
        wav_path: "/Volumes/ALOHA/Peach19(10).wav".to_string(),
        bounce_id: "bounce-1".to_string(),
        created_at_ms: now_epoch_ms(),
        wav_file_size: Some(11_520_044),
        wav_mtime_ms: now_epoch_ms(),
        wav_hash: Some("hash-1".to_string()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };

    write_expected_metadata(&base, "ph", &metadata).unwrap();

    assert_eq!(read_expected_metadata(&base, "ph").unwrap(), metadata);
    assert!(expected_path(&base, "ph").exists());
}

#[test]
fn empty_wav_path_is_invalid() {
    let base = isolated_dir();
    let metadata = ExpectedWavMetadata {
        expected_duration_samples: 1,
        expected_sample_rate: 48_000,
        wav_path: String::new(),
        bounce_id: "bounce-1".to_string(),
        created_at_ms: now_epoch_ms(),
        wav_file_size: Some(1),
        wav_mtime_ms: now_epoch_ms(),
        wav_hash: Some("hash-1".to_string()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };

    assert!(matches!(
        write_expected_metadata(&base, "ph", &metadata),
        Err(ExpectedMetadataError::Invalid)
    ));
}

#[test]
fn consumed_marker_does_not_make_current_unarmable() {
    let base = isolated_dir();
    let metadata = metadata_fixture("bounce-consume");
    write_expected_metadata(&base, "ph", &metadata).unwrap();
    assert!(
        mark_expected_metadata_consumed(&base, "ph", Some("bounce-consume"), "session-1").unwrap()
    );
    assert_eq!(read_expected_metadata(&base, "ph").unwrap(), metadata);
    let claimed_sessions = claimed_session_ids_for_metadata(&base, "ph", &metadata).unwrap();
    assert_eq!(claimed_sessions, vec!["session-1"]);
}

#[test]
fn legacy_consumed_fields_do_not_make_current_unarmable() {
    let base = isolated_dir();
    let mut metadata = metadata_fixture("bounce-legacy-consumed");
    metadata.consumed_at_ms = Some(now_epoch_ms());
    metadata.consumed_by_session_id = Some("legacy-session".to_string());
    let path = expected_path(&base, "ph");
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&metadata).unwrap()).unwrap();

    let mut expected = metadata.clone();
    expected.consumed_at_ms = None;
    expected.consumed_by_session_id = None;
    assert_eq!(read_expected_metadata(&base, "ph").unwrap(), expected);
    assert_eq!(
        claim_expected_metadata_for_session(&base, "ph", "new-session").unwrap(),
        expected
    );
}

#[test]
fn claim_expected_metadata_snapshots_current_without_consuming_it() {
    let base = isolated_dir();
    let metadata = metadata_fixture("bounce-claim");
    write_expected_metadata(&base, "ph", &metadata).unwrap();

    let claimed = claim_expected_metadata_for_session(&base, "ph", "session-claim").unwrap();
    assert_eq!(claimed, metadata);
    assert!(claimed.consumed_at_ms.is_none());
    assert!(claimed.consumed_by_session_id.is_none());
    assert_eq!(read_expected_metadata(&base, "ph").unwrap(), metadata);

    let same_session = claim_expected_metadata_for_session(&base, "ph", "session-claim").unwrap();
    assert_eq!(same_session, metadata);

    let batch_peer = claim_expected_metadata_for_session(&base, "ph", "session-peer").unwrap();
    assert_eq!(batch_peer, metadata);

    let stored = fs::read(expected_path(&base, "ph")).unwrap();
    let stored: ExpectedWavMetadata = serde_json::from_slice(&stored).unwrap();
    let claimed_sessions = claimed_session_ids(&stored);
    assert!(claimed_sessions.is_empty());
    let claimed_sessions = claimed_session_ids_for_metadata(&base, "ph", &stored).unwrap();
    assert_eq!(claimed_sessions, vec!["session-claim", "session-peer"]);
}

/// 2026-07-10 (stopし忘れ検出): claim (Keep) と consume (Close) で `claimed_at_ms` /
/// `closed_at_ms` が正しく分離されることの回帰。Close は開始時刻を上書きしてはならず、
/// 終了時刻だけを新規に刻む。
#[test]
fn closed_at_ms_lifecycle_separates_claim_time_from_close_time() {
    let base = isolated_dir();
    let metadata = metadata_fixture("bounce-lifecycle");
    write_expected_metadata(&base, "ph", &metadata).unwrap();

    claim_expected_metadata_for_session(&base, "ph", "session-lifecycle").unwrap();
    let claimed_marker = read_claim_marker_for_session(&base, "ph", "session-lifecycle")
        .unwrap()
        .unwrap();
    assert_eq!(
        claimed_marker.closed_at_ms, None,
        "claim (Keep) must leave closed_at_ms unset — session is still open"
    );
    let claimed_at_ms = claimed_marker.claimed_at_ms;

    thread::sleep(std::time::Duration::from_millis(5));

    assert!(mark_expected_metadata_consumed(
        &base,
        "ph",
        Some("bounce-lifecycle"),
        "session-lifecycle"
    )
    .unwrap());
    let closed_marker = read_claim_marker_for_session(&base, "ph", "session-lifecycle")
        .unwrap()
        .unwrap();
    assert_eq!(
        closed_marker.claimed_at_ms, claimed_at_ms,
        "close must not overwrite the original claim (start) time"
    );
    let closed_at_ms = closed_marker
        .closed_at_ms
        .expect("close must stamp closed_at_ms");
    assert!(
        closed_at_ms > claimed_at_ms,
        "closed_at_ms must reflect the later close time, not the claim time"
    );
}

/// Close は冪等でなければならない。再tryや二重 finalize が走っても、最初に刻んだ
/// `closed_at_ms` を後から動かしてはいけない。
#[test]
fn mark_consumed_preserves_existing_closed_at_ms_on_retry() {
    let base = isolated_dir();
    let metadata = metadata_fixture("bounce-close-idempotent");
    write_expected_metadata(&base, "ph", &metadata).unwrap();

    claim_expected_metadata_for_session(&base, "ph", "session-close-idempotent").unwrap();
    assert!(mark_expected_metadata_consumed(
        &base,
        "ph",
        Some("bounce-close-idempotent"),
        "session-close-idempotent"
    )
    .unwrap());
    let first_closed_at_ms = read_claim_marker_for_session(&base, "ph", "session-close-idempotent")
        .unwrap()
        .unwrap()
        .closed_at_ms
        .expect("first close must stamp closed_at_ms");

    thread::sleep(std::time::Duration::from_millis(5));

    assert!(mark_expected_metadata_consumed(
        &base,
        "ph",
        Some("bounce-close-idempotent"),
        "session-close-idempotent"
    )
    .unwrap());
    let second_closed_at_ms =
        read_claim_marker_for_session(&base, "ph", "session-close-idempotent")
            .unwrap()
            .unwrap()
            .closed_at_ms
            .expect("retry close must keep closed_at_ms");

    assert_eq!(
        second_closed_at_ms, first_closed_at_ms,
        "retry close must preserve the original closed_at_ms"
    );
}

/// 2026-07-10 (バグ修正): Keep と Close の間に `current.json` が同じ bounce_id のまま
/// 新しい世代（別の created_at_ms / wav_hash）へロールオーバーしても、Close の書き込みは
/// Keep 時に claim した世代のスナップショットを使わなければならない — モジュール冒頭の
/// 設計原則（"close-time recovery cannot be pulled onto a later bounce"）そのものの回帰。
#[test]
fn mark_consumed_uses_originally_claimed_generation_not_live_current_json() {
    let base = isolated_dir();
    let mut first = metadata_fixture("bounce-rollover");
    first.wav_hash = Some("hash-gen-1".to_string());
    write_expected_metadata(&base, "ph", &first).unwrap();
    claim_expected_metadata_for_session(&base, "ph", "session-rollover").unwrap();
    let claimed_marker = read_claim_marker_for_session(&base, "ph", "session-rollover")
        .unwrap()
        .unwrap();
    assert_eq!(claimed_marker.created_at_ms, first.created_at_ms);

    // 同じ bounce_id のまま、新しい世代（別の created_at_ms / wav_hash）へロールオーバー。
    let mut second = first.clone();
    second.created_at_ms = first.created_at_ms + 1;
    second.wav_hash = Some("hash-gen-2".to_string());
    write_expected_metadata(&base, "ph", &second).unwrap();

    assert!(mark_expected_metadata_consumed(
        &base,
        "ph",
        Some("bounce-rollover"),
        "session-rollover"
    )
    .unwrap());

    let closed_marker = read_claim_marker_for_session(&base, "ph", "session-rollover")
        .unwrap()
        .unwrap();
    assert_eq!(
        closed_marker.created_at_ms, first.created_at_ms,
        "close must stamp the ORIGINALLY-claimed generation, not the rolled-over current.json"
    );
    assert_eq!(closed_marker.wav_hash, "hash-gen-1");
    assert!(closed_marker.closed_at_ms.is_some());
}

#[test]
fn newer_expected_generation_ignores_previous_claim_markers_for_same_wav() {
    let base = isolated_dir();
    let mut first = metadata_fixture("expected-same-wav");
    first.wav_hash = Some("same-wav-hash".to_string());
    write_expected_metadata(&base, "ph", &first).unwrap();
    claim_expected_metadata_for_session(&base, "ph", "session-old").unwrap();

    let mut next = first.clone();
    next.created_at_ms += 1;
    write_expected_metadata(&base, "ph", &next).unwrap();

    let claimed = claim_expected_metadata_for_session(&base, "ph", "session-new").unwrap();
    assert_eq!(claimed, next);
    let claimed_sessions = claimed_session_ids_for_metadata(&base, "ph", &next).unwrap();
    assert_eq!(claimed_sessions, vec!["session-new"]);
}

#[test]
fn claimed_session_returns_immutable_snapshot_after_current_overwrite() {
    let base = isolated_dir();
    let first = metadata_fixture("bounce-session-first");
    let next = metadata_fixture("bounce-session-next");
    write_expected_metadata(&base, "ph", &first).unwrap();

    let claimed = claim_expected_metadata_for_session(&base, "ph", "stable-session").unwrap();
    assert_eq!(claimed, first);
    write_expected_metadata(&base, "ph", &next).unwrap();

    assert_eq!(
        claim_expected_metadata_for_session(&base, "ph", "stable-session").unwrap(),
        first,
        "once a session has a snapshot, later current.json generations must not retarget it"
    );
    assert_eq!(
        claim_expected_metadata_for_session(&base, "ph", "next-session").unwrap(),
        next
    );
}

#[test]
fn concurrent_claim_markers_preserve_all_batch_sessions() {
    let base = Arc::new(isolated_dir());
    let metadata = metadata_fixture("bounce-concurrent");
    write_expected_metadata(&base, "ph", &metadata).unwrap();
    let barrier = Arc::new(Barrier::new(8));
    let mut handles = Vec::new();
    for n in 0..8 {
        let base = Arc::clone(&base);
        let barrier = Arc::clone(&barrier);
        handles.push(thread::spawn(move || {
            barrier.wait();
            let session_id = format!("session-{n}");
            claim_expected_metadata_for_session(&base, "ph", &session_id)
        }));
    }
    for handle in handles {
        handle.join().unwrap().unwrap();
    }

    let claimed_sessions = claimed_session_ids_for_metadata(&base, "ph", &metadata).unwrap();
    assert_eq!(
        claimed_sessions,
        (0..8).map(|n| format!("session-{n}")).collect::<Vec<_>>()
    );
    assert_eq!(read_expected_metadata(&base, "ph").unwrap(), metadata);
}

#[test]
fn claim_expected_metadata_has_no_batch_size_cap() {
    let base = isolated_dir();
    let metadata = metadata_fixture("bounce-batch-no-cap");
    write_expected_metadata(&base, "ph", &metadata).unwrap();

    for n in 0..16 {
        let session_id = format!("session-{n}");
        assert_eq!(
            claim_expected_metadata_for_session(&base, "ph", &session_id).unwrap(),
            metadata
        );
    }

    let claimed_sessions = claimed_session_ids_for_metadata(&base, "ph", &metadata).unwrap();
    assert_eq!(claimed_sessions.len(), 16);
    assert_eq!(read_expected_metadata(&base, "ph").unwrap(), metadata);
}

#[test]
fn empty_session_lifecycle_does_not_snapshot_previous_current() {
    let base = isolated_dir();
    let previous = metadata_fixture("previous-drop");
    write_expected_metadata(&base, "ph", &previous).unwrap();

    begin_expected_session(&base, "ph", "new-record-session").unwrap();

    let marker = read_claim_marker_for_session(&base, "ph", "new-record-session")
        .unwrap()
        .expect("session lifecycle marker");
    assert!(marker.metadata.is_none());
    assert!(marker.bounce_id.is_empty());
    assert!(marker.closed_at_ms.is_none());
    assert_eq!(read_expected_metadata(&base, "ph").unwrap(), previous);
}

#[test]
fn empty_session_lifecycle_binds_only_drop_selected_at_finalize() {
    let base = isolated_dir();
    begin_expected_session(&base, "ph", "new-record-session").unwrap();
    let selected = metadata_fixture("selected-after-keep");
    write_expected_metadata(&base, "ph", &selected).unwrap();

    assert!(mark_expected_metadata_consumed(
        &base,
        "ph",
        Some(&selected.bounce_id),
        "new-record-session",
    )
    .unwrap());

    let marker = read_claim_marker_for_session(&base, "ph", "new-record-session")
        .unwrap()
        .expect("completed lifecycle marker");
    assert_eq!(marker.metadata.as_ref(), Some(&selected));
    assert_eq!(marker.bounce_id, selected.bounce_id);
    assert!(marker.closed_at_ms.is_some());
}
