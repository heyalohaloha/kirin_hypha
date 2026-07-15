use super::*;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Arc, Barrier, Mutex};

fn isolated_base() -> PathBuf {
    let mut p = std::env::temp_dir();
    p.push(format!(
        "kirin_writer_claim_test_{}_{}",
        std::process::id(),
        TEMP_SEQ.fetch_add(1, Ordering::Relaxed)
    ));
    let _ = fs::remove_dir_all(&p);
    fs::create_dir_all(&p).unwrap();
    p
}

#[test]
fn same_session_role_instance_rejects_second_active_claim() {
    let base = isolated_base();
    let _first = claim_writer(&base, "ph", "session-a", Role::Post, "post-1").unwrap();
    let second = claim_writer(&base, "ph", "session-a", Role::Post, "post-1");
    assert!(matches!(
        second,
        Err(WriterClaimError::AlreadyActive { .. })
    ));
}

#[test]
fn exact_generation_ownership_is_not_ready_until_initial_flush_is_published() {
    let base = isolated_base();
    let capture_generation_id = uuid::Uuid::new_v4().to_string();
    let generation_started_at_ms = now_epoch_ms();
    let mut claim = claim_writer_for_generation(
        &base,
        "ph",
        "session-ready",
        Role::Post,
        "post-1",
        &capture_generation_id,
        generation_started_at_ms,
    )
    .unwrap();

    assert!(writer_claim_active(&base, "ph", "session-ready", Role::Post, "post-1").unwrap());
    assert!(!writer_claim_ready_for_generation(
        &base,
        "ph",
        "session-ready",
        Role::Post,
        "post-1",
        &capture_generation_id,
        generation_started_at_ms,
        std::process::id(),
    )
    .unwrap());

    claim.mark_ready().unwrap();
    assert!(writer_claim_ready_for_generation(
        &base,
        "ph",
        "session-ready",
        Role::Post,
        "post-1",
        &capture_generation_id,
        generation_started_at_ms,
        std::process::id(),
    )
    .unwrap());
    assert!(!writer_claim_ready_for_generation(
        &base,
        "ph",
        "session-ready",
        Role::Post,
        "post-1",
        &uuid::Uuid::new_v4().to_string(),
        generation_started_at_ms,
        std::process::id(),
    )
    .unwrap());
    assert!(!writer_claim_ready_for_generation(
        &base,
        "ph",
        "session-ready",
        Role::Post,
        "post-1",
        &capture_generation_id,
        generation_started_at_ms + 1,
        std::process::id(),
    )
    .unwrap());

    claim.mark_closed().unwrap();
    assert!(!writer_claim_ready_for_generation(
        &base,
        "ph",
        "session-ready",
        Role::Post,
        "post-1",
        &capture_generation_id,
        generation_started_at_ms,
        std::process::id(),
    )
    .unwrap());
}

#[test]
fn generationless_current_claim_can_never_arm_a_generation() {
    let base = isolated_base();
    let mut claim =
        claim_writer(&base, "ph", "session-generationless", Role::Post, "post-1").unwrap();
    claim.mark_ready().unwrap();

    assert!(
        writer_claim_active(&base, "ph", "session-generationless", Role::Post, "post-1",).unwrap()
    );
    assert!(!writer_claim_ready_for_generation(
        &base,
        "ph",
        "session-generationless",
        Role::Post,
        "post-1",
        &uuid::Uuid::new_v4().to_string(),
        now_epoch_ms(),
        std::process::id(),
    )
    .unwrap());
}

#[test]
fn generationless_claim_does_not_infer_attestation_from_roster_pointers() {
    let base = isolated_base();
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        "ph".into(),
        "post-1".into(),
        "pre-1".into(),
        "daw-a".into(),
        std::process::id(),
    );
    let member = generation.members.first().unwrap();
    let mut transaction =
        crate::capture_generation_tx::CaptureGenerationTransaction::begin(&base, &generation)
            .unwrap();
    transaction.stage().unwrap();
    let mut claim = claim_writer(
        &base,
        &member.project_hash,
        &member.record_session_id,
        Role::Post,
        &member.post_instance_id,
    )
    .unwrap();
    claim.mark_ready().unwrap();

    assert!(!writer_claim_ready_for_generation(
        &base,
        &member.project_hash,
        &member.record_session_id,
        Role::Post,
        &member.post_instance_id,
        &generation.capture_generation_id,
        generation.started_at_ms,
        generation.host_process_id,
    )
    .unwrap());
}

#[test]
fn live_pid_without_renewing_exact_lease_is_not_execution_authority() {
    let base = isolated_base();
    let capture_generation_id = uuid::Uuid::new_v4().to_string();
    let generation_started_at_ms = now_epoch_ms();
    let mut claim = claim_writer_for_generation(
        &base,
        "ph",
        "session-expired-execution",
        Role::Post,
        "post-1",
        &capture_generation_id,
        generation_started_at_ms,
    )
    .unwrap();
    claim.mark_ready().unwrap();
    let expired_heartbeat = now_epoch_ms() - WRITER_EXECUTION_LEASE_STALE_MS - 1;
    claim.claim.heartbeat_at_ms = expired_heartbeat;
    write_state_atomic(
        &claim.path,
        &claim.claim.owner_id,
        expired_heartbeat,
        claim.claim.ready_at_ms,
        claim.claim.closed_at_ms,
    )
    .unwrap();

    assert!(!writer_claim_execution_active_for_generation(
        &base,
        "ph",
        "session-expired-execution",
        Role::Post,
        "post-1",
        &capture_generation_id,
        generation_started_at_ms,
        std::process::id(),
    )
    .unwrap());
}

#[test]
fn closed_claim_rejects_later_claim() {
    let base = isolated_base();
    let mut first = claim_writer(&base, "ph", "session-b", Role::Post, "post-1").unwrap();
    assert!(writer_claim_active(&base, "ph", "session-b", Role::Post, "post-1").unwrap());
    first.mark_closed().unwrap();
    let second = claim_writer(&base, "ph", "session-b", Role::Post, "post-1");
    assert!(matches!(
        second,
        Err(WriterClaimError::AlreadyClosed { .. })
    ));
    assert!(writer_claim_closed(&base, "ph", "session-b", Role::Post, "post-1").unwrap());
    assert!(!writer_claim_active(&base, "ph", "session-b", Role::Post, "post-1").unwrap());
}

#[test]
fn different_role_can_claim_same_session() {
    let base = isolated_base();
    let _post = claim_writer(&base, "ph", "session-c", Role::Post, "post-1").unwrap();
    let pre = claim_writer(&base, "ph", "session-c", Role::Pre, "pre-1");
    assert!(pre.is_ok());
}

#[test]
fn abandon_releases_startup_claim_for_retry() {
    let base = isolated_base();
    let first = claim_writer(&base, "ph", "session-abandon", Role::Post, "post-1").unwrap();
    let first_owner = first.claim.owner_id.clone();
    first.abandon().unwrap();

    let second = claim_writer(&base, "ph", "session-abandon", Role::Post, "post-1").unwrap();
    assert_ne!(second.claim.owner_id, first_owner);
}

#[test]
fn unwound_owner_drop_releases_claim_for_watchdog_restart() {
    let base = isolated_base();
    let mut first_owner = None;
    let unwind = catch_unwind(AssertUnwindSafe(|| {
        let first = claim_writer(&base, "ph", "session-unwind", Role::Post, "post-1").unwrap();
        first_owner = Some(first.claim.owner_id.clone());
        panic!("simulate IO writer panic");
    }));
    assert!(unwind.is_err());

    let second = claim_writer(&base, "ph", "session-unwind", Role::Post, "post-1").unwrap();
    assert_ne!(second.claim.owner_id, first_owner.unwrap());
}

#[test]
fn stale_reclaim_is_not_clobbered_by_old_guard_drop_or_heartbeat() {
    let base = isolated_base();
    let mut first =
        claim_writer(&base, "ph", "session-stale-reclaim", Role::Post, "post-1").unwrap();
    first.claim.heartbeat_at_ms = now_epoch_ms() - WRITER_CLAIM_STALE_MS - 1_000;
    let bytes = serde_json::to_vec(&first.claim).unwrap();
    crate::atomic_file::write_bytes_atomic(&first.path, &bytes).unwrap();

    let second = claim_writer(&base, "ph", "session-stale-reclaim", Role::Post, "post-1").unwrap();
    let second_owner = second.claim.owner_id.clone();

    assert!(matches!(
        first.heartbeat(),
        Err(WriterClaimError::AlreadyActive { .. })
    ));
    drop(first);

    let third = claim_writer(&base, "ph", "session-stale-reclaim", Role::Post, "post-1");
    match third {
        Err(WriterClaimError::AlreadyActive {
            owner_id: Some(owner),
            ..
        }) => assert_eq!(owner, second_owner),
        other => panic!("new reclaimed owner must remain active, got {other:?}"),
    }
}

#[test]
fn concurrent_claims_exactly_one_writer_wins() {
    const THREADS: usize = 12;
    let base = isolated_base();
    let base = Arc::new(base);
    let barrier = Arc::new(Barrier::new(THREADS));
    let outcomes = Arc::new(Mutex::new(Vec::new()));
    let winner = Arc::new(Mutex::new(None));
    let mut handles = Vec::new();

    for _ in 0..THREADS {
        let base = Arc::clone(&base);
        let barrier = Arc::clone(&barrier);
        let outcomes = Arc::clone(&outcomes);
        let winner = Arc::clone(&winner);
        handles.push(std::thread::spawn(move || {
            barrier.wait();
            let outcome = claim_writer(&base, "ph", "session-concurrent", Role::Post, "post-1")
                .map(|guard| {
                    *winner.lock().unwrap() = Some(guard);
                    "created"
                })
                .unwrap_or_else(|e| match e {
                    WriterClaimError::AlreadyActive { .. } => "already_active",
                    other => panic!("unexpected writer claim outcome: {other}"),
                });
            outcomes.lock().unwrap().push(outcome);
        }));
    }

    for handle in handles {
        handle.join().unwrap();
    }
    let outcomes = outcomes.lock().unwrap();
    assert_eq!(
        outcomes
            .iter()
            .filter(|outcome| **outcome == "created")
            .count(),
        1
    );
    assert_eq!(
        outcomes
            .iter()
            .filter(|outcome| **outcome == "already_active")
            .count(),
        THREADS - 1
    );
}
