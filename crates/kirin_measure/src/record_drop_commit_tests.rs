use super::*;
use crate::record_expected::{
    begin_expected_session, claim_expected_metadata_for_session, mark_expected_metadata_consumed,
    read_claim_marker_for_session, write_expected_metadata,
};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir().join(format!(
        "kirin_record_drop_commit_test_{}_{}",
        std::process::id(),
        n
    ));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn metadata_fixture(bounce_id: &str) -> ExpectedWavMetadata {
    let now = chrono::Utc::now().timestamp_millis();
    ExpectedWavMetadata {
        expected_duration_samples: 48_000,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: Some(96_000),
        wav_path: format!("/tmp/{bounce_id}.wav"),
        bounce_id: bounce_id.to_string(),
        created_at_ms: now,
        wav_file_size: Some(1_000),
        wav_mtime_ms: now,
        wav_hash: Some(format!("hash-{bounce_id}")),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    }
}

fn write_drop_commit(
    base: &Path,
    project_hash: &str,
    session_id: &str,
    metadata: &ExpectedWavMetadata,
) {
    let drop_commit_id = format!("drop-{session_id}");
    let commit = DropRecordCommit {
        schema_version: DROP_COMMIT_SCHEMA.to_string(),
        drop_commit_id: drop_commit_id.clone(),
        project_hash: project_hash.to_string(),
        record_session_id: session_id.to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        metadata: metadata.clone(),
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_commit_path(base, project_hash, session_id),
        &serde_json::to_vec(&commit).unwrap(),
    )
    .unwrap();
    let transaction = DropRecordTransaction {
        schema_version: DROP_TRANSACTION_SCHEMA.to_string(),
        drop_commit_id,
        project_hash: project_hash.to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        record_session_ids: vec![session_id.to_string()],
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_transaction_path(base, project_hash, &transaction.drop_commit_id),
        &serde_json::to_vec(&transaction).unwrap(),
    )
    .unwrap();
    let generation_transaction = DropRecordGenerationTransaction {
        schema_version: DROP_GENERATION_TRANSACTION_SCHEMA.to_string(),
        drop_commit_id: transaction.drop_commit_id.clone(),
        capture_generation_id: transaction.capture_generation_id.clone(),
        generation_started_at_ms: transaction.generation_started_at_ms,
        created_at_ms: transaction.created_at_ms,
        bounce_id: transaction.bounce_id.clone(),
        wav_hash: transaction.wav_hash.clone(),
        projects: vec![DropRecordGenerationProject {
            project_hash: project_hash.to_string(),
            record_session_ids: transaction.record_session_ids.clone(),
        }],
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_generation_transaction_path(base, &generation_transaction.drop_commit_id),
        &serde_json::to_vec(&generation_transaction).unwrap(),
    )
    .unwrap();
}

#[test]
fn drop_commit_binds_only_the_exact_open_session() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    begin_expected_session(&base, "project-a", "session-b").unwrap();
    let metadata = metadata_fixture("drop-exact-session");
    write_drop_commit(&base, "project-a", "session-a", &metadata);

    assert_eq!(
        bind_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        Some(metadata.clone())
    );
    assert_eq!(
        bind_drop_commit_for_open_session(&base, "project-a", "session-b").unwrap(),
        None
    );
    assert_eq!(
        read_claim_marker_for_session(&base, "project-a", "session-a")
            .unwrap()
            .unwrap()
            .metadata,
        Some(metadata)
    );
}

#[test]
fn drop_commit_cannot_reopen_or_retarget_a_closed_session() {
    let base = isolated_dir();
    let first = metadata_fixture("drop-closed-first");
    write_expected_metadata(&base, "project-a", &first).unwrap();
    claim_expected_metadata_for_session(&base, "project-a", "session-a").unwrap();
    mark_expected_metadata_consumed(&base, "project-a", Some(&first.bounce_id), "session-a")
        .unwrap();
    let later = metadata_fixture("drop-closed-later");
    write_drop_commit(&base, "project-a", "session-a", &later);

    assert_eq!(
        bind_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
    assert_eq!(
        read_claim_marker_for_session(&base, "project-a", "session-a")
            .unwrap()
            .unwrap()
            .metadata,
        Some(first)
    );
}

#[test]
fn exact_drop_can_complete_a_metadata_free_closed_pair_lifecycle() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    mark_expected_metadata_consumed(&base, "project-a", None, "session-a").unwrap();
    let closed_at_ms = read_claim_marker_for_session(&base, "project-a", "session-a")
        .unwrap()
        .unwrap()
        .closed_at_ms
        .expect("closed lifecycle");
    let metadata = metadata_fixture("drop-after-stop");
    write_drop_commit(&base, "project-a", "session-a", &metadata);

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
    let inspected = inspect_drop_commit_for_closed_pair_session(&base, "project-a", "session-a")
        .unwrap()
        .expect("exact closed-pair Drop");
    assert_eq!(inspected, metadata);
    assert_eq!(
        bind_drop_commit_for_closed_pair_session(&base, "project-a", "session-a", &inspected,)
            .unwrap(),
        Some(metadata.clone())
    );
    let marker = read_claim_marker_for_session(&base, "project-a", "session-a")
        .unwrap()
        .unwrap();
    assert_eq!(marker.metadata, Some(metadata));
    assert_eq!(marker.closed_at_ms, Some(closed_at_ms));
}

#[test]
fn closed_pair_bind_rejects_metadata_replaced_after_inspection() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let first = metadata_fixture("drop-inspected-first");
    write_drop_commit(&base, "project-a", "session-a", &first);
    let inspected = inspect_drop_commit_for_closed_pair_session(&base, "project-a", "session-a")
        .unwrap()
        .unwrap();

    let later = metadata_fixture("drop-replaced-later");
    write_drop_commit(&base, "project-a", "session-a", &later);

    assert_eq!(
        bind_drop_commit_for_closed_pair_session(&base, "project-a", "session-a", &inspected,)
            .unwrap(),
        None
    );
    assert_eq!(
        read_claim_marker_for_session(&base, "project-a", "session-a")
            .unwrap()
            .unwrap()
            .metadata,
        None
    );
}

#[test]
fn drop_commit_is_inert_until_transaction_manifest_is_ready() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let metadata = metadata_fixture("drop-not-ready");
    let commit = DropRecordCommit {
        schema_version: DROP_COMMIT_SCHEMA.to_string(),
        drop_commit_id: "drop-not-ready".to_string(),
        project_hash: "project-a".to_string(),
        record_session_id: "session-a".to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        metadata,
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_commit_path(&base, "project-a", "session-a"),
        &serde_json::to_vec(&commit).unwrap(),
    )
    .unwrap();

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
}

#[test]
fn project_transaction_is_inert_until_generation_manifest_is_ready() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let metadata = metadata_fixture("drop-generation-not-ready");
    write_drop_commit(&base, "project-a", "session-a", &metadata);
    fs::remove_file(drop_generation_transaction_path(&base, "drop-session-a")).unwrap();

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
}

#[test]
fn transaction_cannot_authorize_a_session_not_in_its_batch() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let metadata = metadata_fixture("drop-wrong-batch");
    write_drop_commit(&base, "project-a", "session-a", &metadata);
    let path = drop_transaction_path(&base, "project-a", "drop-session-a");
    let mut transaction: DropRecordTransaction =
        serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    transaction.record_session_ids = vec!["session-other".to_string()];
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&transaction).unwrap())
        .unwrap();

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
}

#[test]
fn generation_transaction_cannot_omit_the_project_batch() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let metadata = metadata_fixture("drop-generation-wrong-batch");
    write_drop_commit(&base, "project-a", "session-a", &metadata);
    let path = drop_generation_transaction_path(&base, "drop-session-a");
    let mut transaction: DropRecordGenerationTransaction =
        serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    transaction.projects = vec![DropRecordGenerationProject {
        project_hash: "project-other".to_string(),
        record_session_ids: vec!["session-other".to_string()],
    }];
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&transaction).unwrap())
        .unwrap();

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
}
