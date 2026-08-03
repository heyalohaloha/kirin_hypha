use super::*;
use crate::{PairOwnershipLease, PairStatus, PrePairStatusObserver};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Barrier};

fn isolated_root() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!(
        "kirin_pair_linearization_{}_{}",
        std::process::id(),
        n
    ));
    fs::create_dir_all(&root).unwrap();
    root
}

fn claim_pair(
    owner: &PairOwnershipLease,
    root: &Path,
    project: &str,
    post: &str,
    pre: &str,
    claimed_at: f64,
) {
    let instance_dir = crate::pair_owner_instance_dir(root, project, post).unwrap();
    assert!(owner
        .commit_claimed_binding_if(
            root,
            Some(&instance_dir),
            Some(pre),
            project,
            post,
            claimed_at,
            || true,
            || Some(()),
        )
        .unwrap()
        .is_some());
}

#[test]
fn engine_proof_rejects_a_recreated_owner_and_repairs_the_instance_marker() {
    let root = isolated_root();
    let instance_dir = root.join("project-a").join("post-a");
    let incumbent = PairOwnershipLease::new();
    claim_pair(&incumbent, &root, "project-a", "post-a", "pre-a", 1.0);

    let incumbent_marker = crate::pair_ownership_marker::owner_marker_path(
        &instance_dir,
        incumbent.owner_id(),
        "pre-a",
        1.0f64.to_bits(),
    )
    .unwrap();
    fs::remove_file(incumbent_marker).unwrap();

    let recreated = PairOwnershipLease::new();
    assert_eq!(
        recreated
            .commit_claimed_binding_if(
                &root,
                Some(&instance_dir),
                Some("pre-a"),
                "project-a",
                "post-a",
                1.0,
                || true,
                || Some(()),
            )
            .unwrap(),
        None
    );
    claim_pair(&incumbent, &root, "project-a", "post-a", "pre-a", 1.0);

    assert!(crate::pair_ownership_marker::pair_owner_claim_is_live(
        &instance_dir,
        incumbent.owner_id(),
        "pre-a",
        1.0f64.to_bits(),
    ));
    assert!(!crate::pair_ownership_marker::pair_owner_claim_is_live(
        &instance_dir,
        recreated.owner_id(),
        "pre-a",
        1.0f64.to_bits(),
    ));
    let claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert!(pair_claim_is_owned(&root, &claim));
    assert!(!live_claim_owned_by_other(
        &root,
        "pre-a",
        "project-a",
        "post-a",
        incumbent.owner_id(),
        1.0,
    ));
    assert!(live_claim_owned_by_other(
        &root,
        "pre-a",
        "project-a",
        "post-a",
        recreated.owner_id(),
        1.0,
    ));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn prepared_claim_is_not_a_pre_pair_or_foreign_conflict_and_rollback_is_gap_free() {
    let root = isolated_root();
    let owner = Arc::new(PairOwnershipLease::new());
    claim_pair(&owner, &root, "project", "post", "pre-old", 1.0);
    let old_observer = PrePairStatusObserver::new();
    let new_observer = PrePairStatusObserver::new();
    assert_eq!(
        old_observer.observe(&root, "pre-old", "2Mix"),
        PairStatus::Paired
    );

    let entered = Arc::new(Barrier::new(2));
    let resume = Arc::new(Barrier::new(2));
    let transition = {
        let root = root.clone();
        let owner = Arc::clone(&owner);
        let entered = Arc::clone(&entered);
        let resume = Arc::clone(&resume);
        std::thread::spawn(move || {
            let instance_dir = crate::pair_owner_instance_dir(&root, "project", "post").unwrap();
            owner
                .commit_claimed_binding_if(
                    &root,
                    Some(&instance_dir),
                    Some("pre-new"),
                    "project",
                    "post",
                    2.0,
                    || true,
                    || {
                        entered.wait();
                        resume.wait();
                        None::<()>
                    },
                )
                .unwrap()
        })
    };
    entered.wait();

    assert_eq!(
        try_observe_pair_claim(&root, current_host_process_id(), "pre-new").unwrap(),
        StablePairClaimObservation::Contended
    );
    assert_eq!(
        new_observer.observe(&root, "pre-new", "2Mix"),
        PairStatus::Waiting
    );
    assert!(!live_claim_owned_by_other(
        &root,
        "pre-new",
        "foreign-project",
        "foreign-post",
        "foreign-owner",
        99.0,
    ));
    assert_eq!(
        old_observer.observe(&root, "pre-old", "2Mix"),
        PairStatus::Paired
    );

    resume.wait();
    assert_eq!(transition.join().unwrap(), None);
    assert_eq!(
        old_observer.observe(&root, "pre-old", "2Mix"),
        PairStatus::Paired
    );
    assert_eq!(
        new_observer.observe(&root, "pre-new", "2Mix"),
        PairStatus::Unpaired
    );
    drop(owner);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn consecutive_same_pre_commits_never_emit_unpaired() {
    let root = isolated_root();
    let owner = Arc::new(PairOwnershipLease::new());
    claim_pair(&owner, &root, "project", "post", "pre", 1.0);
    let observer = PrePairStatusObserver::new();
    assert_eq!(observer.observe(&root, "pre", "2Mix"), PairStatus::Paired);

    for generation in 2..=8 {
        let entered = Arc::new(Barrier::new(2));
        let resume = Arc::new(Barrier::new(2));
        let transition = {
            let root = root.clone();
            let owner = Arc::clone(&owner);
            let entered = Arc::clone(&entered);
            let resume = Arc::clone(&resume);
            std::thread::spawn(move || {
                let dir = crate::pair_owner_instance_dir(&root, "project", "post").unwrap();
                owner
                    .commit_claimed_binding_if(
                        &root,
                        Some(&dir),
                        Some("pre"),
                        "project",
                        "post",
                        f64::from(generation),
                        || true,
                        || {
                            entered.wait();
                            resume.wait();
                            Some(())
                        },
                    )
                    .unwrap()
            })
        };
        entered.wait();
        assert_eq!(observer.observe(&root, "pre", "2Mix"), PairStatus::Paired);
        resume.wait();
        assert!(transition.join().unwrap().is_some());
        assert_eq!(observer.observe(&root, "pre", "2Mix"), PairStatus::Paired);
    }
    drop(owner);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn engine_proof_blocks_takeover_after_instance_marker_unlink() {
    let root = isolated_root();
    let incumbent = Arc::new(PairOwnershipLease::new());
    let incumbent_dir = crate::pair_owner_instance_dir(&root, "project-a", "post-a").unwrap();
    claim_pair(&incumbent, &root, "project-a", "post-a", "pre", 1.0);
    let observer = PrePairStatusObserver::new();
    assert_eq!(observer.observe(&root, "pre", "2Mix"), PairStatus::Paired);
    let marker = crate::pair_ownership_marker::owner_marker_path(
        &incumbent_dir,
        incumbent.owner_id(),
        "pre",
        1.0f64.to_bits(),
    )
    .unwrap();
    fs::remove_file(&marker).unwrap();

    let incumbent_claim = read_pair_claim(&root, current_host_process_id(), "pre").unwrap();
    assert!(pair_claim_is_owned(&root, &incumbent_claim));
    assert_eq!(observer.observe(&root, "pre", "2Mix"), PairStatus::Paired);

    let competitor = Arc::new(PairOwnershipLease::new());
    let attempted = {
        let root = root.clone();
        let competitor = Arc::clone(&competitor);
        std::thread::spawn(move || {
            let dir = crate::pair_owner_instance_dir(&root, "project-b", "post-b").unwrap();
            competitor
                .commit_claimed_binding_if(
                    &root,
                    Some(&dir),
                    Some("pre"),
                    "project-b",
                    "post-b",
                    2.0,
                    || true,
                    || Some(()),
                )
                .unwrap()
        })
    };
    assert_eq!(attempted.join().unwrap(), None);
    assert_eq!(
        read_pair_claim(&root, current_host_process_id(), "pre"),
        Some(incumbent_claim)
    );

    assert_eq!(
        incumbent
            .commit_claimed_binding_if(
                &root,
                Some(&incumbent_dir),
                Some("pre"),
                "project-a",
                "post-a",
                1.0,
                || true,
                || Some(()),
            )
            .unwrap(),
        Some(())
    );
    assert!(marker.is_file());
    drop(competitor);
    drop(incumbent);
    assert_eq!(observer.observe(&root, "pre", "2Mix"), PairStatus::Unpaired);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn explicit_clear_removes_claim_before_retiring_engine_proof() {
    let root = isolated_root();
    let owner = PairOwnershipLease::new();
    claim_pair(&owner, &root, "project", "post", "pre", 1.0);
    let observer = PrePairStatusObserver::new();
    assert_eq!(observer.observe(&root, "pre", "2Mix"), PairStatus::Paired);

    assert!(owner
        .commit_claimed_binding_if(
            &root,
            None,
            None,
            "project",
            "post",
            0.0,
            || true,
            || Some(()),
        )
        .unwrap()
        .is_some());
    assert!(read_pair_claim(&root, current_host_process_id(), "pre").is_none());
    assert_eq!(observer.observe(&root, "pre", "2Mix"), PairStatus::Unpaired);
    drop(owner);
    let _ = fs::remove_dir_all(root);
}
