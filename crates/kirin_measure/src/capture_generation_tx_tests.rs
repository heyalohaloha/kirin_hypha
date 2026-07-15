use super::*;
use crate::capture_generation::{
    read_active_generation, read_current_generation, read_preparing_generation,
    CaptureGenerationMember,
};
use crate::record_signal::SignalStatus;

fn generation() -> CaptureGeneration {
    CaptureGeneration::new_for_members(
        "post-a".into(),
        "daw-a".into(),
        std::process::id(),
        vec![CaptureGenerationMember {
            project_hash: "project-a".into(),
            post_instance_id: "post-a".into(),
            pre_instance_id: "pre-a".into(),
            record_session_id: String::new(),
        }],
    )
}

fn ready_generation_writers(
    base_dir: &Path,
    generation: &CaptureGeneration,
) -> Vec<crate::record_writer_claim::WriterClaimGuard> {
    ready_writers_for_attestation(
        base_dir,
        generation,
        &generation.capture_generation_id,
        generation.started_at_ms,
    )
}

fn ready_writers_for_attestation(
    base_dir: &Path,
    generation: &CaptureGeneration,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
) -> Vec<crate::record_writer_claim::WriterClaimGuard> {
    let mut writers = Vec::with_capacity(generation.members.len() * 2);
    for member in &generation.members {
        for (role, instance_id) in [
            (Role::Pre, member.pre_instance_id.as_str()),
            (Role::Post, member.post_instance_id.as_str()),
        ] {
            let mut writer = crate::record_writer_claim::claim_writer_for_generation(
                base_dir,
                &member.project_hash,
                &member.record_session_id,
                role,
                instance_id,
                capture_generation_id,
                generation_started_at_ms,
            )
            .unwrap();
            writer.mark_ready().unwrap();
            writers.push(writer);
        }
    }
    writers
}

fn publish_active_residue(base_dir: &Path, generation: &CaptureGeneration) {
    crate::atomic_file::write_bytes_atomic(
        &active_generation_path(base_dir),
        &serde_json::to_vec(generation).unwrap(),
    )
    .unwrap();
}

#[test]
fn staged_roster_is_not_active_until_commit() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();

    transaction.stage().unwrap();

    assert_eq!(
        read_current_generation(temp.path(), "project-a")
            .unwrap()
            .unwrap(),
        generation
    );
    assert!(read_active_generation(temp.path()).unwrap().is_none());
    assert_eq!(
        read_preparing_generation(temp.path()).unwrap().unwrap(),
        generation
    );

    let _writers = ready_generation_writers(temp.path(), &generation);
    transaction.commit().unwrap();
    assert_eq!(
        read_active_generation(temp.path()).unwrap().unwrap(),
        generation
    );
    assert!(read_preparing_generation(temp.path()).unwrap().is_none());
}

#[test]
fn committed_generation_remains_the_stop_target_after_preparation_is_removed() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    let _writers = ready_generation_writers(temp.path(), &generation);

    assert_eq!(
        crate::capture_generation::read_stop_target_generation(temp.path(), "project-a", "post-a",)
            .unwrap()
            .unwrap(),
        generation,
        "preparing is the exact Stop authority before promotion",
    );
    transaction
        .commit_observing_active(|| {
            assert_eq!(
                crate::capture_generation::read_stop_target_generation(
                    temp.path(),
                    "project-a",
                    "post-a",
                )
                .unwrap()
                .unwrap(),
                generation,
                "during promotion both pointers must address the new generation",
            );
        })
        .unwrap();
    assert!(read_preparing_generation(temp.path()).unwrap().is_none());
    assert_eq!(
        crate::capture_generation::read_stop_target_generation(temp.path(), "project-a", "post-a",)
            .unwrap()
            .unwrap(),
        generation,
        "active becomes the same exact Stop authority with no old-generation fallback",
    );
}

#[test]
fn ready_commit_requires_exact_pre_and_post_writer_claims() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let member = generation.members[0].clone();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();

    let error = transaction
        .commit_when_ready(Duration::ZERO)
        .expect_err("an unarmed roster must not become consumer-active");
    assert!(matches!(
        error,
        CaptureGenerationError::Io(ref error) if error.kind() == io::ErrorKind::TimedOut
    ));
    assert!(read_active_generation(temp.path()).unwrap().is_none());

    let mut pre = crate::record_writer_claim::claim_writer_for_generation(
        temp.path(),
        &member.project_hash,
        &member.record_session_id,
        Role::Pre,
        &member.pre_instance_id,
        &generation.capture_generation_id,
        generation.started_at_ms,
    )
    .unwrap();
    let mut post = crate::record_writer_claim::claim_writer_for_generation(
        temp.path(),
        &member.project_hash,
        &member.record_session_id,
        Role::Post,
        &member.post_instance_id,
        &generation.capture_generation_id,
        generation.started_at_ms,
    )
    .unwrap();

    assert!(
        transaction.commit_when_ready(Duration::ZERO).is_err(),
        "claim ownership before initial artifact flush must not commit"
    );
    pre.mark_ready().unwrap();
    assert!(
        transaction.commit_when_ready(Duration::ZERO).is_err(),
        "one-sided writer readiness must not commit"
    );
    post.mark_ready().unwrap();

    transaction.commit_when_ready(Duration::ZERO).unwrap();
    assert_eq!(
        read_active_generation(temp.path()).unwrap().unwrap(),
        generation
    );
}

#[test]
fn ready_claim_from_another_generation_or_start_time_cannot_commit() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();

    let wrong_id = uuid::Uuid::new_v4().to_string();
    let wrong_generation = ready_writers_for_attestation(
        temp.path(),
        &generation,
        &wrong_id,
        generation.started_at_ms,
    );
    assert!(transaction.commit_when_ready(Duration::ZERO).is_err());
    assert!(read_active_generation(temp.path()).unwrap().is_none());
    drop(wrong_generation);

    let wrong_start = ready_writers_for_attestation(
        temp.path(),
        &generation,
        &generation.capture_generation_id,
        generation.started_at_ms + 1,
    );
    assert!(transaction.commit_when_ready(Duration::ZERO).is_err());
    assert!(read_active_generation(temp.path()).unwrap().is_none());
    drop(wrong_start);
}

#[test]
fn closed_ready_claim_is_revalidated_before_active_publication() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    let writers = ready_generation_writers(temp.path(), &generation);
    assert!(generation_producers_ready(temp.path(), &generation));
    drop(writers);

    let error = transaction
        .commit()
        .expect_err("a ready snapshot cannot outlive its exact writer leases");
    assert!(matches!(
        error,
        CaptureGenerationError::Io(ref error) if error.kind() == io::ErrorKind::TimedOut
    ));
    assert!(read_active_generation(temp.path()).unwrap().is_none());
}

#[test]
fn abandoned_preparation_removes_only_its_non_authoritative_pointers() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    {
        let mut transaction =
            CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
        transaction.stage().unwrap();
        assert!(read_preparing_generation(temp.path()).unwrap().is_some());
        assert!(read_current_generation(temp.path(), "project-a")
            .unwrap()
            .is_some());
    }

    assert!(read_preparing_generation(temp.path()).unwrap().is_none());
    assert!(read_current_generation(temp.path(), "project-a")
        .unwrap()
        .is_none());
    assert!(read_active_generation(temp.path()).unwrap().is_none());
}

#[test]
fn abandoned_preparation_releases_its_exact_signal_and_broadcasts_to_exact_shelf() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    {
        let mut transaction =
            CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
        transaction.stage().unwrap();
        crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
            temp.path(),
            "project-a",
            "post-a",
            "pre-a".into(),
            "daw-a".into(),
            None,
            &generation,
        )
        .unwrap();
    }

    let signal = crate::record_signal::read_signal(temp.path(), "project-a", "post-a")
        .expect("rolled-back signal remains as an explicit release marker");
    assert_eq!(signal.status, SignalStatus::Released);
    assert_eq!(
        signal.release_reason,
        Some(crate::record_signal::ReleaseReason::ManualStop)
    );
    let (_, stop) = crate::all_stop_signal::read_current_stop_broadcast(temp.path(), "project-a")
        .expect("rollback must wake remote state machines on the exact member shelf");
    assert_eq!(stop.originator_post_instance_id, "post-a");
    assert_eq!(stop.daw_session_id, "daw-a");
}

#[test]
fn rollback_never_releases_a_signal_that_was_replaced_by_another_generation() {
    let temp = tempfile::tempdir().unwrap();
    let original = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &original).unwrap();
    transaction.stage().unwrap();

    let replacement = generation();
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        "project-a",
        "post-a",
        "pre-a".into(),
        "daw-a".into(),
        None,
        &replacement,
    )
    .unwrap();
    drop(transaction);

    let signal = crate::record_signal::read_signal(temp.path(), "project-a", "post-a")
        .expect("replacement signal must survive old transaction rollback");
    assert_eq!(signal.status, SignalStatus::Pending);
    assert_eq!(
        signal.capture_generation_id,
        replacement.capture_generation_id
    );
}

#[test]
fn staging_same_transaction_twice_is_rejected() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();

    let error = transaction
        .stage()
        .expect_err("stage is a one-shot mutation");
    assert!(matches!(
        error,
        CaptureGenerationError::Io(ref error)
            if error.kind() == io::ErrorKind::AlreadyExists
    ));
}

#[test]
fn rollback_restores_previous_project_pointer_without_changing_active_generation() {
    let temp = tempfile::tempdir().unwrap();
    let first = generation();
    let mut first_transaction = CaptureGenerationTransaction::begin(temp.path(), &first).unwrap();
    first_transaction.stage().unwrap();
    let writers = ready_generation_writers(temp.path(), &first);
    first_transaction.commit_when_ready(Duration::ZERO).unwrap();
    drop(first_transaction);
    drop(writers);

    let replacement = generation();
    {
        let mut replacement_transaction =
            CaptureGenerationTransaction::begin(temp.path(), &replacement).unwrap();
        replacement_transaction.stage().unwrap();
        assert_eq!(
            read_current_generation(temp.path(), "project-a")
                .unwrap()
                .unwrap(),
            replacement
        );
    }

    assert_eq!(
        read_current_generation(temp.path(), "project-a")
            .unwrap()
            .unwrap(),
        first
    );
    assert_eq!(read_active_generation(temp.path()).unwrap().unwrap(), first);
}

#[test]
fn partial_stage_failure_restores_every_project_pointer_already_written() {
    let temp = tempfile::tempdir().unwrap();
    let previous = generation();
    publish_current_generation(temp.path(), "project-a", &previous).unwrap();

    // Project order is deterministic. project-a is written first; project-b then fails because
    // its would-be directory is an ordinary file. Drop must own the partial mutation.
    std::fs::write(temp.path().join("project-b"), b"not-a-directory").unwrap();
    let replacement = CaptureGeneration::new_for_members(
        "post-a".into(),
        "daw-a".into(),
        std::process::id(),
        vec![
            CaptureGenerationMember {
                project_hash: "project-a".into(),
                post_instance_id: "post-a".into(),
                pre_instance_id: "pre-a".into(),
                record_session_id: String::new(),
            },
            CaptureGenerationMember {
                project_hash: "project-b".into(),
                post_instance_id: "post-b".into(),
                pre_instance_id: "pre-b".into(),
                record_session_id: String::new(),
            },
        ],
    );
    {
        let mut transaction =
            CaptureGenerationTransaction::begin(temp.path(), &replacement).unwrap();
        assert!(transaction.stage().is_err());
    }

    assert_eq!(
        read_current_generation(temp.path(), "project-a")
            .unwrap()
            .unwrap(),
        previous,
        "the first project must not retain a partially staged generation"
    );
    assert!(read_preparing_generation(temp.path()).unwrap().is_none());
    assert!(read_active_generation(temp.path()).unwrap().is_none());
}

#[test]
fn commit_without_stage_is_rejected() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();

    assert!(transaction.commit().is_err());
    assert!(read_active_generation(temp.path()).unwrap().is_none());
}

#[test]
fn terminal_publication_wins_atomically_over_generation_commit() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    crate::capture_generation_lifecycle::mark_generation_terminal(
        temp.path(),
        &generation,
        crate::capture_generation_lifecycle::GenerationTerminalReason::AllStop,
    )
    .unwrap();

    let error = transaction
        .commit()
        .expect_err("a concurrently stopped preparation cannot become consumer-active");
    assert!(matches!(
        error,
        CaptureGenerationError::Io(ref error)
            if error.kind() == io::ErrorKind::AlreadyExists
    ));
    assert!(read_active_generation(temp.path()).unwrap().is_none());
}

#[test]
fn malformed_terminal_is_quarantined_and_exact_ready_generation_commits() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    let _writers = ready_generation_writers(temp.path(), &generation);
    let terminal_path = crate::capture_generation_lifecycle::generation_terminal_path(
        temp.path(),
        &generation.capture_generation_id,
    );
    crate::atomic_file::create_private_dir_all(terminal_path.parent().unwrap()).unwrap();
    std::fs::write(&terminal_path, b"not-json").unwrap();

    transaction.commit_when_ready(Duration::ZERO).unwrap();
    assert_eq!(
        read_active_generation(temp.path()).unwrap(),
        Some(generation),
    );
    assert!(!terminal_path.exists());
}

#[test]
fn concurrent_publisher_is_rejected_without_waiting() {
    let temp = tempfile::tempdir().unwrap();
    let first = generation();
    let first_transaction = CaptureGenerationTransaction::begin(temp.path(), &first).unwrap();
    let second = generation();

    let error = CaptureGenerationTransaction::begin(temp.path(), &second)
        .err()
        .expect("the publication lock must be non-blocking");

    assert!(matches!(
        error,
        CaptureGenerationError::Io(ref error)
            if error.kind() == io::ErrorKind::WouldBlock
    ));
    drop(first_transaction);
}

#[test]
fn live_open_generation_cannot_be_replaced() {
    let temp = tempfile::tempdir().unwrap();
    let first = generation();
    let mut first_transaction = CaptureGenerationTransaction::begin(temp.path(), &first).unwrap();
    first_transaction.stage().unwrap();
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        "project-a",
        "post-a",
        "pre-a".into(),
        "daw-a".into(),
        None,
        &first,
    )
    .unwrap();
    let _writers = ready_generation_writers(temp.path(), &first);
    first_transaction.commit().unwrap();
    drop(first_transaction);

    let second = generation();
    let error = CaptureGenerationTransaction::begin(temp.path(), &second)
        .err()
        .expect("a live open generation must retain ownership");
    assert!(matches!(
        error,
        CaptureGenerationError::Io(ref error)
            if error.kind() == io::ErrorKind::WouldBlock
    ));
    assert_eq!(read_active_generation(temp.path()).unwrap().unwrap(), first);
}

#[test]
fn live_open_generation_cannot_be_reentered_with_the_same_identity() {
    let temp = tempfile::tempdir().unwrap();
    let generation = generation();
    let member = generation.members[0].clone();
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        &member.project_hash,
        &member.post_instance_id,
        member.pre_instance_id.clone(),
        generation.daw_session_id.clone(),
        None,
        &generation,
    )
    .unwrap();
    let _writers = ready_generation_writers(temp.path(), &generation);
    transaction.commit().unwrap();
    drop(transaction);

    let error = CaptureGenerationTransaction::begin(temp.path(), &generation)
        .err()
        .expect("an active generation must have only one transaction owner");
    assert!(matches!(
        error,
        CaptureGenerationError::Io(ref error)
            if error.kind() == io::ErrorKind::WouldBlock
    ));
    assert_eq!(
        crate::record_signal::read_signal(
            temp.path(),
            &member.project_hash,
            &member.post_instance_id,
        )
        .unwrap()
        .status,
        SignalStatus::Pending,
        "a rejected duplicate transaction must not roll back the active signal"
    );
}

#[test]
fn dead_producer_generation_does_not_block_restart() {
    let temp = tempfile::tempdir().unwrap();
    let mut abandoned = generation();
    abandoned.host_process_id = i32::MAX as u32;
    publish_active_residue(temp.path(), &abandoned);
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        "project-a",
        "post-a",
        "pre-a".into(),
        "daw-a".into(),
        None,
        &abandoned,
    )
    .unwrap();

    let replacement = generation();
    let transaction = CaptureGenerationTransaction::begin(temp.path(), &replacement)
        .expect("a crashed producer must not own the next Keep");
    drop(transaction);
}

#[test]
fn same_process_stale_signal_without_exact_writer_lease_does_not_block_upgrade() {
    let temp = tempfile::tempdir().unwrap();
    let residue = generation();
    publish_active_residue(temp.path(), &residue);
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        "project-a",
        "post-a",
        "pre-a".into(),
        "daw-a".into(),
        None,
        &residue,
    )
    .unwrap();

    let replacement = generation();
    let transaction = CaptureGenerationTransaction::begin(temp.path(), &replacement)
        .expect("durable signal residue is not a live producer lease");
    drop(transaction);
}

#[test]
fn exact_live_writer_blocks_next_generation_until_lease_is_closed() {
    let temp = tempfile::tempdir().unwrap();
    let first = generation();
    let member = first.members[0].clone();
    let mut first_transaction = CaptureGenerationTransaction::begin(temp.path(), &first).unwrap();
    first_transaction.stage().unwrap();
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        &member.project_hash,
        &member.post_instance_id,
        member.pre_instance_id.clone(),
        first.daw_session_id.clone(),
        None,
        &first,
    )
    .unwrap();
    let mut writers = ready_generation_writers(temp.path(), &first);
    first_transaction.commit().unwrap();
    drop(first_transaction);

    crate::capture_generation_lifecycle::mark_generation_terminal(
        temp.path(),
        &first,
        crate::capture_generation_lifecycle::GenerationTerminalReason::AllStop,
    )
    .unwrap();
    let replacement = generation();
    let blocked = CaptureGenerationTransaction::begin(temp.path(), &replacement)
        .err()
        .expect("terminal publication alone cannot overtake an exact live member");
    assert!(matches!(
        blocked,
        CaptureGenerationError::Io(ref error)
            if error.kind() == io::ErrorKind::WouldBlock
    ));

    crate::record_signal::mark_released_with_reason(
        temp.path(),
        &member.project_hash,
        &member.post_instance_id,
        crate::record_signal::ReleaseReason::AllStop,
    )
    .unwrap();
    let still_blocked = CaptureGenerationTransaction::begin(temp.path(), &replacement)
        .err()
        .expect("Released cannot overtake a writer that is still flushing/closing");
    assert!(matches!(
        still_blocked,
        CaptureGenerationError::Io(ref error)
            if error.kind() == io::ErrorKind::WouldBlock
    ));

    writers.clear();
    let transaction = CaptureGenerationTransaction::begin(temp.path(), &replacement)
        .expect("the exact execution lease, not signal residue, owns generation lifetime");
    drop(transaction);
}

#[test]
fn malformed_terminal_never_overrides_a_live_exact_writer_lease() {
    let temp = tempfile::tempdir().unwrap();
    let first = generation();
    let mut first_transaction = CaptureGenerationTransaction::begin(temp.path(), &first).unwrap();
    first_transaction.stage().unwrap();
    let writers = ready_generation_writers(temp.path(), &first);
    first_transaction.commit_when_ready(Duration::ZERO).unwrap();
    drop(first_transaction);

    let terminal_path = crate::capture_generation_lifecycle::generation_terminal_path(
        temp.path(),
        &first.capture_generation_id,
    );
    crate::atomic_file::create_private_dir_all(terminal_path.parent().unwrap()).unwrap();
    std::fs::write(&terminal_path, b"{").unwrap();
    let replacement = generation();
    let blocked = CaptureGenerationTransaction::begin(temp.path(), &replacement)
        .err()
        .expect("a malformed terminal is not authority to overtake live exact writers");
    assert!(matches!(
        blocked,
        CaptureGenerationError::Io(ref error)
            if error.kind() == io::ErrorKind::WouldBlock
    ));
    assert!(
        !terminal_path.exists(),
        "malformed residue must be quarantined"
    );

    drop(writers);
    let replacement_transaction = CaptureGenerationTransaction::begin(temp.path(), &replacement)
        .expect("closed exact leases allow the next Keep without terminal residue");
    drop(replacement_transaction);
}
