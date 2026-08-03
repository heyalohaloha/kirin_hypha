use super::*;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Barrier};

fn isolated_root() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!(
        "kirin_pair_claim_index_{}_{}",
        std::process::id(),
        n
    ));
    let _ = fs::remove_dir_all(&root);
    fs::create_dir_all(&root).unwrap();
    root
}

fn write_post(
    root: &Path,
    project_hash: &str,
    post_instance_id: &str,
    pre_instance_id: &str,
    pair_claimed_at: f64,
    pair_owner_id: &str,
    lease: &mut crate::watch_snapshot_lease::WatchSnapshotLease,
) {
    let instance_dir = root.join(project_hash).join(post_instance_id);
    lease.bind(&instance_dir).unwrap();
    fs::write(
        instance_dir.join("post.json"),
        serde_json::json!({
            "v": 2,
            "role": "POST",
            "instance_id": post_instance_id,
            "watch_owner_id": lease.owner_id(),
            "pair_owner_id": pair_owner_id,
            "host_process_id": current_host_process_id(),
            "signal_state": "inactive",
            "t": chrono::Utc::now().to_rfc3339(),
            "paired_pre_instance_id": pre_instance_id,
            "pair_claimed_at": pair_claimed_at,
        })
        .to_string(),
    )
    .unwrap();
}

fn publish(
    root: &Path,
    project_hash: &str,
    post_instance_id: &str,
    pre_instance_id: &str,
    pair_claimed_at: f64,
    lease: &crate::PairOwnershipLease,
) -> PublishPairClaimOutcome {
    publish_pair_claim(
        root,
        pre_instance_id,
        project_hash,
        post_instance_id,
        lease.owner_id(),
        current_host_process_id(),
        pair_claimed_at,
    )
    .unwrap()
}

#[test]
fn same_engine_can_atomically_move_project_and_post_identity_for_the_same_pre() {
    let root = isolated_root();
    let pair_owner = crate::PairOwnershipLease::new();
    let old_instance_dir = root.join("project-old").join("post-old");
    let new_instance_dir = root.join("project-new").join("post-new");

    let first = pair_owner
        .commit_binding(Some(&old_instance_dir), Some("pre-a"), 1.0, || {
            Some(
                publish_pair_claim(
                    &root,
                    "pre-a",
                    "project-old",
                    "post-old",
                    pair_owner.owner_id(),
                    current_host_process_id(),
                    1.0,
                )
                .unwrap(),
            )
        })
        .unwrap()
        .unwrap();
    assert_eq!(first, PublishPairClaimOutcome::Published);
    let old_claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();

    let moved = pair_owner
        .commit_binding(Some(&new_instance_dir), Some("pre-a"), 2.0, || {
            assert!(
                pair_claim_is_owned(&root, &old_claim),
                "the old marker remains live until the owner transition commits"
            );
            Some(
                publish_pair_claim(
                    &root,
                    "pre-a",
                    "project-new",
                    "post-new",
                    pair_owner.owner_id(),
                    current_host_process_id(),
                    2.0,
                )
                .unwrap(),
            )
        })
        .unwrap()
        .unwrap();
    assert_eq!(moved, PublishPairClaimOutcome::Published);

    let current = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(current.project_hash, "project-new");
    assert_eq!(current.post_instance_id, "post-new");
    assert_eq!(current.pair_owner_id, pair_owner.owner_id());
    assert_eq!(current.pair_claimed_at_bits, 2.0f64.to_bits());
    assert!(pair_claim_is_owned(&root, &current));
    assert!(!pair_claim_is_owned(&root, &old_claim));

    assert!(
        !release_pair_claim(&root, &old_claim).unwrap(),
        "a delayed cleanup from the old restore identity cannot delete the moved claim"
    );
    assert_eq!(
        read_pair_claim(&root, current_host_process_id(), "pre-a"),
        Some(current)
    );
}

#[test]
fn foreign_engine_cannot_use_identity_move_to_replace_a_live_claim() {
    let root = isolated_root();
    let incumbent = crate::PairOwnershipLease::new();
    incumbent
        .bind(
            &root.join("project-old").join("post-old"),
            Some("pre-a"),
            1.0,
        )
        .unwrap();
    assert_eq!(
        publish(&root, "project-old", "post-old", "pre-a", 1.0, &incumbent,),
        PublishPairClaimOutcome::Published
    );

    let foreign = crate::PairOwnershipLease::new();
    foreign
        .bind(
            &root.join("project-new").join("post-new"),
            Some("pre-a"),
            2.0,
        )
        .unwrap();
    assert_eq!(
        publish(&root, "project-new", "post-new", "pre-a", 2.0, &foreign,),
        PublishPairClaimOutcome::OwnedByOther
    );

    let current = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(current.project_hash, "project-old");
    assert_eq!(current.post_instance_id, "post-old");
    assert_eq!(current.pair_owner_id, incumbent.owner_id());
    assert!(pair_claim_is_owned(&root, &current));
}

#[test]
fn different_engine_cannot_advance_an_owned_claim_for_the_same_post_identity() {
    let root = isolated_root();
    let incumbent = crate::PairOwnershipLease::new();
    let competitor = crate::PairOwnershipLease::new();
    let instance_dir = root.join("project-a").join("post-a");
    incumbent.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    competitor.bind(&instance_dir, Some("pre-a"), 2.0).unwrap();
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 1.0, &incumbent),
        PublishPairClaimOutcome::Published
    );

    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 2.0, &competitor),
        PublishPairClaimOutcome::OwnedByOther
    );
    let current = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(current.pair_owner_id, incumbent.owner_id());
    assert_eq!(current.pair_claimed_at_bits, 1.0f64.to_bits());
}

#[test]
fn later_owner_cannot_replace_an_established_pair() {
    let root = isolated_root();
    let old_pair_owner = crate::PairOwnershipLease::new();
    let new_pair_owner = crate::PairOwnershipLease::new();
    let mut old_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    let mut new_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    old_pair_owner
        .bind(&root.join("project-a").join("post-old"), Some("pre-a"), 1.0)
        .unwrap();
    new_pair_owner
        .bind(&root.join("project-a").join("post-new"), Some("pre-a"), 2.0)
        .unwrap();
    write_post(
        &root,
        "project-a",
        "post-old",
        "pre-a",
        1.0,
        old_pair_owner.owner_id(),
        &mut old_watch,
    );
    assert_eq!(
        publish(
            &root,
            "project-a",
            "post-old",
            "pre-a",
            1.0,
            &old_pair_owner,
        ),
        PublishPairClaimOutcome::Published
    );
    let old_claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();

    write_post(
        &root,
        "project-a",
        "post-new",
        "pre-a",
        2.0,
        new_pair_owner.owner_id(),
        &mut new_watch,
    );
    assert_eq!(
        publish(
            &root,
            "project-a",
            "post-new",
            "pre-a",
            2.0,
            &new_pair_owner,
        ),
        PublishPairClaimOutcome::OwnedByOther
    );
    let current = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(current, old_claim);
    assert!(pair_claim_is_owned(&root, &current));
}

#[test]
fn simultaneous_posts_have_exactly_one_early_pair_claim_winner() {
    let root = isolated_root();
    let first_owner = Arc::new(crate::PairOwnershipLease::new());
    let second_owner = Arc::new(crate::PairOwnershipLease::new());
    first_owner
        .bind(
            &root.join("project-a").join("post-first"),
            Some("pre-a"),
            1.0,
        )
        .unwrap();
    second_owner
        .bind(
            &root.join("project-a").join("post-second"),
            Some("pre-a"),
            2.0,
        )
        .unwrap();
    let barrier = Arc::new(Barrier::new(3));

    let spawn_claim =
        |post: &'static str, claimed_at: f64, owner: Arc<crate::PairOwnershipLease>| {
            let root = root.clone();
            let barrier = Arc::clone(&barrier);
            std::thread::spawn(move || {
                barrier.wait();
                publish_pair_claim(
                    &root,
                    "pre-a",
                    "project-a",
                    post,
                    owner.owner_id(),
                    current_host_process_id(),
                    claimed_at,
                )
                .unwrap()
            })
        };
    let first = spawn_claim("post-first", 1.0, Arc::clone(&first_owner));
    let second = spawn_claim("post-second", 2.0, Arc::clone(&second_owner));
    barrier.wait();
    let outcomes = [first.join().unwrap(), second.join().unwrap()];

    assert_eq!(
        outcomes
            .iter()
            .filter(|outcome| **outcome == PublishPairClaimOutcome::Published)
            .count(),
        1
    );
    assert_eq!(
        outcomes
            .iter()
            .filter(|outcome| **outcome == PublishPairClaimOutcome::OwnedByOther)
            .count(),
        1
    );
    let winner = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert!(pair_claim_is_owned(&root, &winner));
}
