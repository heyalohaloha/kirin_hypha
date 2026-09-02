use super::service_open_drop_commit_at;
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
    assert!(!service_open_drop_commit_at(
        &missing_base("watch"),
        &sm,
        &Arc::new(RecordTakeTracker::new()),
        &Arc::new(Mutex::new(Some("pre-exact".to_string()))),
        "project",
        "post"
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
