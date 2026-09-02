use super::{service_open_drop_commit_at, service_open_drop_commit_with_base};
use crate::record_drop_commit::{
    drop_commit_path, drop_generation_transaction_path, drop_transaction_path, DropRecordCommit,
    DropRecordGenerationProject, DropRecordGenerationTransaction, DropRecordTransaction,
    DROP_COMMIT_SCHEMA, DROP_GENERATION_TRANSACTION_SCHEMA, DROP_TRANSACTION_SCHEMA,
};
use crate::record_expected::{
    begin_expected_session, read_claim_marker_for_session, ExpectedWavMetadata,
};
use crate::{License, RecordStateMachine, RecordTakeTracker};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

fn missing_base(label: &str) -> PathBuf {
    std::env::temp_dir()
        .join("kirin")
        .join("post-open-drop-tests")
        .join(format!("{label}-{}", uuid::Uuid::new_v4()))
}

#[test]
fn watch_never_attempts_to_accept_a_drop_commit() {
    let sm = Arc::new(RecordStateMachine::new());
    assert!(!service_open_drop_commit_with_base(
        &sm,
        &Arc::new(RecordTakeTracker::new()),
        &Arc::new(Mutex::new(Some("pre-exact".to_string()))),
        "project",
        "post",
        || panic!("Watch must not resolve plugin_data storage"),
    ));
    assert!(!sm.is_recording());
}

#[test]
fn sessionless_record_is_not_closed_by_drop_polling() {
    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record(License::Os).unwrap();
    assert!(!service_open_drop_commit_at(
        &missing_base("sessionless"),
        &sm,
        &Arc::new(RecordTakeTracker::new()),
        &Arc::new(Mutex::new(Some("pre-exact".to_string()))),
        "project",
        "post"
    ));
    assert!(sm.is_recording());
}

#[test]
fn missing_exact_session_commit_keeps_transactional_record_open() {
    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record_started_at_clock_transaction(License::Os, 1_000, None, "session-exact")
        .unwrap();
    assert!(!service_open_drop_commit_at(
        &missing_base("missing-commit"),
        &sm,
        &Arc::new(RecordTakeTracker::new()),
        &Arc::new(Mutex::new(Some("pre-exact".to_string()))),
        "project",
        "post"
    ));
    assert!(sm.is_recording());
    assert_eq!(sm.record_session_id().as_deref(), Some("session-exact"));
}

#[test]
fn exact_drop_commit_binds_metadata_and_closes_only_the_open_record() {
    const PROJECT: &str = "project";
    const POST: &str = "post-exact";
    const PRE: &str = "pre-exact";
    const SESSION: &str = "session-exact";
    let base = missing_base("success");
    let now = chrono::Utc::now().timestamp_millis() + 1_000;
    let metadata = ExpectedWavMetadata {
        expected_duration_samples: 48_000,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: Some(96_000),
        wav_path: "/tmp/drop-success.wav".to_string(),
        bounce_id: "bounce-success".to_string(),
        created_at_ms: now,
        wav_file_size: Some(1_000),
        wav_mtime_ms: now,
        wav_hash: Some("hash-success".to_string()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };
    begin_expected_session(&base, PROJECT, SESSION).unwrap();
    write_drop_fixture(&base, PROJECT, SESSION, &metadata);

    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record_started_at_clock_transaction(License::Os, now - 1, None, SESSION)
        .unwrap();
    let tracker = Arc::new(RecordTakeTracker::new());
    tracker.note_capture_window(true, 96_000, 48_000);
    let paired = Arc::new(Mutex::new(Some(PRE.to_string())));

    assert!(service_open_drop_commit_at(
        &base, &sm, &tracker, &paired, PROJECT, POST,
    ));
    assert!(!sm.is_recording());
    assert_eq!(paired.lock().unwrap().as_deref(), Some(PRE));
    assert_eq!(
        read_claim_marker_for_session(&base, PROJECT, SESSION)
            .unwrap()
            .unwrap()
            .metadata,
        Some(metadata)
    );
}

fn write_drop_fixture(
    base: &std::path::Path,
    project: &str,
    session: &str,
    metadata: &ExpectedWavMetadata,
) {
    let drop_commit_id = format!("drop-{session}");
    let commit = DropRecordCommit {
        schema_version: DROP_COMMIT_SCHEMA.to_string(),
        drop_commit_id: drop_commit_id.clone(),
        project_hash: project.to_string(),
        record_session_id: session.to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        metadata: metadata.clone(),
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_commit_path(base, project, session),
        &serde_json::to_vec(&commit).unwrap(),
    )
    .unwrap();
    let transaction = DropRecordTransaction {
        schema_version: DROP_TRANSACTION_SCHEMA.to_string(),
        drop_commit_id: drop_commit_id.clone(),
        project_hash: project.to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        record_session_ids: vec![session.to_string()],
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_transaction_path(base, project, &drop_commit_id),
        &serde_json::to_vec(&transaction).unwrap(),
    )
    .unwrap();
    let generation = DropRecordGenerationTransaction {
        schema_version: DROP_GENERATION_TRANSACTION_SCHEMA.to_string(),
        drop_commit_id: drop_commit_id.clone(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        projects: vec![DropRecordGenerationProject {
            project_hash: project.to_string(),
            record_session_ids: vec![session.to_string()],
        }],
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_generation_transaction_path(base, &drop_commit_id),
        &serde_json::to_vec(&generation).unwrap(),
    )
    .unwrap();
}
