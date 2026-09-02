use super::*;
use crate::capture_generation::{CaptureGeneration, CaptureGenerationMember};
use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};

fn generation(originator: &str, post_instance_id: &str) -> CaptureGeneration {
    let mut posts = vec![originator];
    if post_instance_id != originator {
        posts.push(post_instance_id);
    }
    CaptureGeneration::new_for_members(
        originator.to_string(),
        "daw-a".to_string(),
        crate::current_host_process_id(),
        posts
            .into_iter()
            .map(|post| CaptureGenerationMember {
                project_hash: "project-a".to_string(),
                post_instance_id: post.to_string(),
                pre_instance_id: format!("pre-{post}"),
                record_session_id: String::new(),
            })
            .collect(),
    )
}

#[test]
fn failed_keep_arm_retries_until_success_then_consumes_the_generation_edge() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation("post-origin", "post-self");
    let mut transaction =
        crate::capture_generation_tx::CaptureGenerationTransaction::begin(temp.path(), &generation)
            .unwrap();
    transaction.stage().unwrap();
    crate::all_keep_signal::write_broadcast_for_generation(
        temp.path(),
        "project-a",
        "post-origin",
        "daw-a".to_string(),
        crate::current_host_process_id(),
        &generation,
    )
    .unwrap();

    let generation_key = generation.capture_generation_id.clone();
    let expected_generation_key = generation_key.clone();
    let attempts = Arc::new(AtomicUsize::new(0));
    let attempt_count = Arc::clone(&attempts);
    let keep: TriggerPairResolutionFn = Arc::new(move |originator, _, observed| {
        assert_eq!(originator, "post-origin");
        assert_eq!(observed.capture_generation_id, expected_generation_key);
        attempt_count.fetch_add(1, AtomicOrdering::Relaxed) + 1 >= 2
    });
    let stop: TriggerStopResolutionFn = Arc::new(|_, _| {});
    let record_sm = RecordStateMachine::new();
    let daw_session_id = Arc::new(RwLock::new("daw-a".to_string()));
    let mut processed_keep = crate::broadcast_edge::BroadcastEdgeMemory::default();
    let mut processed_stop = crate::broadcast_edge::BroadcastEdgeMemory::default();

    poll_post_broadcasts(
        temp.path(),
        "project-a",
        "post-self",
        &daw_session_id,
        &record_sm,
        &mut processed_keep,
        &mut processed_stop,
        &keep,
        &stop,
    );
    assert_eq!(attempts.load(AtomicOrdering::Relaxed), 1);
    assert!(!processed_keep.contains("post-origin", &generation_key));

    poll_post_broadcasts(
        temp.path(),
        "project-a",
        "post-self",
        &daw_session_id,
        &record_sm,
        &mut processed_keep,
        &mut processed_stop,
        &keep,
        &stop,
    );
    assert_eq!(attempts.load(AtomicOrdering::Relaxed), 2);
    assert!(processed_keep.contains("post-origin", &generation_key));

    poll_post_broadcasts(
        temp.path(),
        "project-a",
        "post-self",
        &daw_session_id,
        &record_sm,
        &mut processed_keep,
        &mut processed_stop,
        &keep,
        &stop,
    );
    assert_eq!(attempts.load(AtomicOrdering::Relaxed), 2);
}

#[test]
fn fresh_stop_is_delivered_once_and_suppresses_an_older_keep_in_the_same_poll() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation("post-origin", "post-self");
    let mut transaction =
        crate::capture_generation_tx::CaptureGenerationTransaction::begin(temp.path(), &generation)
            .unwrap();
    transaction.stage().unwrap();
    let keep_broadcast = crate::all_keep_signal::write_broadcast_for_generation(
        temp.path(),
        "project-a",
        "post-origin",
        "daw-a".to_string(),
        crate::current_host_process_id(),
        &generation,
    )
    .unwrap();
    let mut stop_broadcast = crate::all_stop_signal::AllStopBroadcast::new_with_scope(
        "post-stopper".to_string(),
        "daw-a".to_string(),
        crate::current_host_process_id(),
    );
    stop_broadcast.started_at = (chrono::Utc::now() + chrono::Duration::seconds(1))
        .format("%Y-%m-%dT%H:%M:%SZ")
        .to_string();
    stop_broadcast.heartbeat = stop_broadcast.started_at.clone();
    crate::atomic_file::write_bytes_atomic(
        &crate::all_stop_signal::current_stop_path(temp.path(), "project-a"),
        &serde_json::to_vec(&stop_broadcast).unwrap(),
    )
    .unwrap();
    let order = Arc::new(Mutex::new(Vec::new()));
    let keep_order = Arc::clone(&order);
    let keep: TriggerPairResolutionFn = Arc::new(move |_, _, _| {
        keep_order.lock().unwrap().push("keep");
        true
    });
    let stop_order = Arc::clone(&order);
    let stop: TriggerStopResolutionFn = Arc::new(move |_, _| {
        stop_order.lock().unwrap().push("stop");
    });
    let record_sm = RecordStateMachine::new();
    let daw_session_id = Arc::new(RwLock::new("daw-a".to_string()));
    let mut processed_keep = crate::broadcast_edge::BroadcastEdgeMemory::default();
    let mut processed_stop = crate::broadcast_edge::BroadcastEdgeMemory::default();

    for _ in 0..2 {
        poll_post_broadcasts(
            temp.path(),
            "project-a",
            "post-self",
            &daw_session_id,
            &record_sm,
            &mut processed_keep,
            &mut processed_stop,
            &keep,
            &stop,
        );
    }

    assert_eq!(*order.lock().unwrap(), vec!["stop"]);
    assert!(processed_stop.contains(
        "post-stopper",
        &format!("legacy:{}", stop_broadcast.started_at)
    ));
    assert!(processed_keep.contains("post-origin", &keep_broadcast.capture_generation_id));
}

#[test]
fn self_originated_keep_is_cached_without_reentering_pair_resolution() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation("post-self", "post-self");
    let mut transaction =
        crate::capture_generation_tx::CaptureGenerationTransaction::begin(temp.path(), &generation)
            .unwrap();
    transaction.stage().unwrap();
    crate::all_keep_signal::write_broadcast_for_generation(
        temp.path(),
        "project-a",
        "post-self",
        "daw-a".to_string(),
        crate::current_host_process_id(),
        &generation,
    )
    .unwrap();
    let attempts = Arc::new(AtomicUsize::new(0));
    let attempt_count = Arc::clone(&attempts);
    let keep: TriggerPairResolutionFn = Arc::new(move |_, _, _| {
        attempt_count.fetch_add(1, AtomicOrdering::Relaxed);
        true
    });
    let stop: TriggerStopResolutionFn = Arc::new(|_, _| {});
    let record_sm = RecordStateMachine::new();
    let daw_session_id = Arc::new(RwLock::new("daw-a".to_string()));
    let mut processed_keep = crate::broadcast_edge::BroadcastEdgeMemory::default();
    let mut processed_stop = crate::broadcast_edge::BroadcastEdgeMemory::default();

    poll_post_broadcasts(
        temp.path(),
        "project-a",
        "post-self",
        &daw_session_id,
        &record_sm,
        &mut processed_keep,
        &mut processed_stop,
        &keep,
        &stop,
    );

    assert_eq!(attempts.load(AtomicOrdering::Relaxed), 0);
    assert!(processed_keep.contains("post-self", &generation.capture_generation_id));
}
