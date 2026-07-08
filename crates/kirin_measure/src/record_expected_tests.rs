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
fn consumed_metadata_is_not_armable_again() {
    let base = isolated_dir();
    let metadata = metadata_fixture("bounce-consume");
    write_expected_metadata(&base, "ph", &metadata).unwrap();
    assert!(mark_expected_metadata_consumed(&base, "ph", "bounce-consume", "session-1").unwrap());
    assert!(matches!(
        read_expected_metadata(&base, "ph"),
        Err(ExpectedMetadataError::Consumed)
    ));
}

#[test]
fn claim_expected_metadata_binds_current_json_to_record_batch_sessions() {
    let base = isolated_dir();
    let metadata = metadata_fixture("bounce-claim");
    write_expected_metadata(&base, "ph", &metadata).unwrap();

    let claimed = claim_expected_metadata_for_session(&base, "ph", "session-claim").unwrap();
    assert_eq!(claimed, metadata);
    assert!(claimed.consumed_at_ms.is_none());
    assert!(claimed.consumed_by_session_id.is_none());
    assert!(matches!(
        read_expected_metadata(&base, "ph"),
        Err(ExpectedMetadataError::Consumed)
    ));

    let same_session = claim_expected_metadata_for_session(&base, "ph", "session-claim").unwrap();
    assert_eq!(same_session, metadata);

    let batch_peer = claim_expected_metadata_for_session(&base, "ph", "session-peer").unwrap();
    assert_eq!(batch_peer, metadata);

    let stored = fs::read(expected_path(&base, "ph")).unwrap();
    let stored: ExpectedWavMetadata = serde_json::from_slice(&stored).unwrap();
    let claimed_sessions = claimed_session_ids(&stored);
    assert_eq!(claimed_sessions, vec!["session-claim", "session-peer"]);
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
    assert!(matches!(
        read_expected_metadata(&base, "ph"),
        Err(ExpectedMetadataError::Consumed)
    ));
}
