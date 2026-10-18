//! An aborted Keep gives back every pairing reservation it took (INV-R3): another POST can Keep
//! the same PRE, and a Reference audition or local Blind can start, at once rather than after the
//! 60 s lease ages out. A member whose signal now names another generation keeps its own.
use super::*;
use crate::capture_generation::CaptureGenerationMember;
use crate::project_audition_lease::ProjectAuditionLease;
use crate::reservation::{reserve_pairing, ReserveOutcome};

fn member(project: &str, post: &str, pre: &str) -> CaptureGenerationMember {
    CaptureGenerationMember {
        project_hash: project.into(),
        post_instance_id: post.into(),
        pre_instance_id: pre.into(),
        record_session_id: String::new(),
    }
}

/// The member reserved its pair and wrote its Pending signal for `generation`.
fn armed(base: &Path, generation: &CaptureGeneration, member: &CaptureGenerationMember) {
    let (project, post, pre) = (
        member.project_hash.as_str(),
        member.post_instance_id.as_str(),
        member.pre_instance_id.as_str(),
    );
    assert_eq!(
        reserve_pairing(base, project, pre, post).unwrap(),
        ReserveOutcome::Created
    );
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        base,
        project,
        post,
        pre.into(),
        "daw-a".into(),
        None,
        generation,
    )
    .unwrap();
}

#[test]
fn an_aborted_single_keep_gives_back_its_pairing_reservation() {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_single_named(
        "project-a".into(),
        "post-a".into(),
        "pre-a".into(),
        "daw-a".into(),
        std::process::id(),
        Some("2Mix".into()),
    );
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    armed(temp.path(), &generation, &generation.members[0]);
    // The producers missed the readiness deadline: the uncommitted generation aborts.
    drop(transaction);

    let mut audition = ProjectAuditionLease::new(temp.path(), "project-a");
    assert!(
        audition.try_acquire().unwrap(),
        "Reference audition and local Blind may start"
    );
    audition.release();
    assert_eq!(
        reserve_pairing(temp.path(), "project-a", "pre-a", "post-b").unwrap(),
        ReserveOutcome::Created,
        "another POST may Keep the same PRE"
    );
}

#[test]
fn an_aborted_all_keep_gives_back_every_member_but_never_a_replaced_one() {
    let temp = tempfile::tempdir().unwrap();
    let members = vec![
        member("project-a", "post-a", "pre-a"),
        member("project-a", "post-b", "pre-b"),
        member("project-a", "post-c", "pre-c"),
    ];
    let generation =
        CaptureGeneration::new_for_members("post-a".into(), "daw-a".into(), 1, members);
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    for member in &generation.members {
        armed(temp.path(), &generation, member);
    }
    // post-c already belongs to a newer generation that kept the same pair.
    let replacement = CaptureGeneration::new_single_named(
        "project-a".into(),
        "post-c".into(),
        "pre-c".into(),
        "daw-a".into(),
        1,
        None,
    );
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        "project-a",
        "post-c",
        "pre-c".into(),
        "daw-a".into(),
        None,
        &replacement,
    )
    .unwrap();
    drop(transaction);

    for (pre, post) in [("pre-a", "post-x"), ("pre-b", "post-y")] {
        assert_eq!(
            reserve_pairing(temp.path(), "project-a", pre, post).unwrap(),
            ReserveOutcome::Created,
            "{pre} is free again"
        );
    }
    assert_eq!(
        reserve_pairing(temp.path(), "project-a", "pre-c", "post-z").unwrap(),
        ReserveOutcome::PreInUse,
        "the replacement generation keeps its reservation"
    );
}

#[test]
fn a_reservation_taken_after_begin_survives_an_older_aborted_keep() {
    let temp = tempfile::tempdir().unwrap();
    let single = |post: &str| {
        CaptureGeneration::new_single_named(
            "project-a".into(),
            post.into(),
            "pre-a".into(),
            "daw-a".into(),
            1,
            None,
        )
    };
    let older = single("post-a");
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &older).unwrap();
    transaction.stage().unwrap();
    armed(temp.path(), &older, &older.members[0]);
    // Reserving before its own begin, the next Keep of the same pair would reuse the older
    // reservation, which the older Keep's abort then removes from under it.
    assert_eq!(
        reserve_pairing(temp.path(), "project-a", "pre-a", "post-a").unwrap(),
        ReserveOutcome::AlreadyReserved
    );
    // Beginning first, it waits for the older transaction, which gives its reservation back
    // while it still holds the lock.
    let next = single("post-a");
    assert!(CaptureGenerationTransaction::begin(temp.path(), &next).is_err());
    drop(transaction);
    let _next_transaction = CaptureGenerationTransaction::begin(temp.path(), &next).unwrap();
    assert_eq!(
        reserve_pairing(temp.path(), "project-a", "pre-a", "post-a").unwrap(),
        ReserveOutcome::Created,
        "the next Keep owns a reservation nothing older can remove"
    );
}
