use super::{paired_pre_target_snapshot, service_post_record_writer};
use crate::engine::SessionSummary;
use crate::record_mark::new_record_mark_queue;
use crate::record_writer::new_record_trace_queue;
use crate::storage::StoragePaths;
use crate::{License, MeasureResult, RecordStateMachine, RecordTakeTracker};
use std::fs;
use std::sync::atomic::AtomicU64;
use std::sync::{Arc, Mutex};

#[test]
fn writer_pair_target_snapshot_keeps_the_exact_instance_identity() {
    let target = Arc::new(Mutex::new(Some("pre-instance-exact".to_string())));
    assert_eq!(
        paired_pre_target_snapshot(&target).as_deref(),
        Some("pre-instance-exact")
    );
}

#[test]
fn absent_writer_pair_target_stays_absent() {
    let target = Arc::new(Mutex::new(None));
    assert_eq!(paired_pre_target_snapshot(&target), None);
}

#[test]
fn poisoned_writer_pair_target_fails_closed_without_inventing_an_identity() {
    let target = Arc::new(Mutex::new(Some("pre-before-poison".to_string())));
    let poison_target = Arc::clone(&target);
    let _ = std::thread::spawn(move || {
        let _guard = poison_target.lock().unwrap();
        panic!("poison paired PRE target");
    })
    .join();
    assert_eq!(paired_pre_target_snapshot(&target), None);
}

#[test]
fn post_writer_adapter_opens_and_closes_the_shared_record_writer() {
    let suffix = uuid::Uuid::new_v4();
    let project = format!("post-writer-adapter-{suffix}");
    let post = format!("post-{suffix}");
    let session = format!("session-{suffix}");
    let plugin_data = StoragePaths::default_platform().unwrap().plugin_data_dir();
    let project_dir = plugin_data.join(&project);
    let _ = fs::remove_dir_all(&project_dir);

    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record_started_at_clock_transaction(
        License::Os,
        chrono::Utc::now().timestamp_millis(),
        Some(0),
        session,
    )
    .unwrap();
    sm.mark_measure_ready(sm.generation());
    let paired = Arc::new(Mutex::new(Some(format!("pre-{suffix}"))));
    let measure = Arc::new(Mutex::new(MeasureResult::default()));
    let mut recording = None;
    let session_summary = Arc::new(Mutex::new(None::<SessionSummary>));
    let overflow = Arc::new(AtomicU64::new(0));
    let oversized_drop = Arc::new(AtomicU64::new(0));
    let trace_queue = new_record_trace_queue();
    let take_tracker = Arc::new(RecordTakeTracker::new());
    let mark_queue = new_record_mark_queue();

    service_post_record_writer(
        &sm,
        48_000,
        &project,
        &post,
        "PRE-A",
        &paired,
        &measure,
        &mut recording,
        &session_summary,
        &overflow,
        &oversized_drop,
        &trace_queue,
        &take_tracker,
        &mark_queue,
    );
    let staging_path = recording
        .as_ref()
        .expect("production adapter must open the shared POST writer")
        .staging_path
        .clone();
    assert!(staging_path.is_file());
    assert!(staging_path.starts_with(&project_dir));

    sm.bump_seal();
    sm.exit_record();
    service_post_record_writer(
        &sm,
        48_000,
        &project,
        &post,
        "PRE-A",
        &paired,
        &measure,
        &mut recording,
        &session_summary,
        &overflow,
        &oversized_drop,
        &trace_queue,
        &take_tracker,
        &mark_queue,
    );
    assert!(recording.is_none());

    fs::remove_dir_all(project_dir).unwrap();
}
