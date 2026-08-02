use super::*;
use std::sync::atomic::{AtomicU64, Ordering};

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
fn exact_claim_ownership_survives_watch_loss_until_post_engine_drops() {
    let root = isolated_root();
    let pair_owner = crate::PairOwnershipLease::new();
    let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    let instance_dir = root.join("project-a").join("post-a");
    pair_owner.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    write_post(&root, "project-a", "post-a", "pre-a", 1.0, &mut watch_owner);
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 1.0, &pair_owner,),
        PublishPairClaimOutcome::Published
    );
    let claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert!(pair_claim_is_owned(&root, &claim));
    assert!(pair_claim_is_live(&root, &claim));
    drop(watch_owner);
    assert!(pair_claim_is_owned(&root, &claim));
    assert!(!pair_claim_is_live(&root, &claim));
    drop(pair_owner);
    assert!(!pair_claim_is_owned(&root, &claim));
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
    write_post(&root, "project-a", "post-old", "pre-a", 1.0, &mut old_watch);
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

    write_post(&root, "project-a", "post-new", "pre-a", 2.0, &mut new_watch);
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
fn released_engine_owner_can_be_replaced_and_cannot_delete_new_claim() {
    let root = isolated_root();
    let old_pair_owner = crate::PairOwnershipLease::new();
    let mut old_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    old_pair_owner
        .bind(&root.join("project-a").join("post-old"), Some("pre-a"), 1.0)
        .unwrap();
    write_post(&root, "project-a", "post-old", "pre-a", 1.0, &mut old_watch);
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
    drop(old_pair_owner);

    let new_pair_owner = crate::PairOwnershipLease::new();
    let mut new_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    new_pair_owner
        .bind(&root.join("project-a").join("post-new"), Some("pre-a"), 2.0)
        .unwrap();
    write_post(&root, "project-a", "post-new", "pre-a", 2.0, &mut new_watch);
    assert_eq!(
        publish(
            &root,
            "project-a",
            "post-new",
            "pre-a",
            2.0,
            &new_pair_owner,
        ),
        PublishPairClaimOutcome::Published
    );
    assert!(!release_pair_claim(&root, &old_claim).unwrap());
    let current = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(current.post_instance_id, "post-new");
    assert!(pair_claim_is_owned(&root, &current));
}

#[test]
fn same_engine_reselection_invalidates_old_generation_before_republish() {
    let root = isolated_root();
    let pair_owner = crate::PairOwnershipLease::new();
    let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    let instance_dir = root.join("project-a").join("post-a");
    pair_owner.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    write_post(&root, "project-a", "post-a", "pre-a", 1.0, &mut watch_owner);
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 1.0, &pair_owner,),
        PublishPairClaimOutcome::Published
    );
    let old_claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();

    pair_owner.bind(&instance_dir, Some("pre-a"), 2.0).unwrap();
    assert!(!pair_claim_is_owned(&root, &old_claim));
    write_post(&root, "project-a", "post-a", "pre-a", 2.0, &mut watch_owner);
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 2.0, &pair_owner,),
        PublishPairClaimOutcome::Published
    );
    let current = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(current.pair_claimed_at_bits, 2.0f64.to_bits());
    assert!(pair_claim_is_owned(&root, &current));
}

#[test]
fn same_engine_moves_to_another_pre_without_keeping_two_owned_claims() {
    let root = isolated_root();
    let pair_owner = crate::PairOwnershipLease::new();
    let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    let instance_dir = root.join("project-a").join("post-a");
    pair_owner.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    write_post(&root, "project-a", "post-a", "pre-a", 1.0, &mut watch_owner);
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 1.0, &pair_owner,),
        PublishPairClaimOutcome::Published
    );
    let old_claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();

    pair_owner.bind(&instance_dir, Some("pre-b"), 2.0).unwrap();
    assert!(!pair_claim_is_owned(&root, &old_claim));
    write_post(&root, "project-a", "post-a", "pre-b", 2.0, &mut watch_owner);
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-b", 2.0, &pair_owner,),
        PublishPairClaimOutcome::Published
    );
    let new_claim = read_pair_claim(&root, current_host_process_id(), "pre-b").unwrap();
    assert!(pair_claim_is_owned(&root, &new_claim));
    assert!(!pair_claim_is_owned(&root, &old_claim));
}

#[test]
fn unsafe_identity_is_rejected_without_creating_an_escape_path() {
    let root = isolated_root();
    let result = publish_pair_claim(
        &root,
        "../escape",
        "project-a",
        "post-a",
        "owner-a",
        current_host_process_id(),
        1.0,
    );
    assert_eq!(result.unwrap_err().kind(), io::ErrorKind::InvalidInput);
    assert!(!root.parent().unwrap().join("escape.json").exists());
}

#[test]
fn unrelated_decoy_directories_do_not_participate_in_exact_lookup() {
    let root = isolated_root();
    for n in 0..64 {
        let dir = root.join(format!("decoy-{n}")).join("post-decoy");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("post.json"), b"not-json").unwrap();
    }
    assert!(read_pair_claim(&root, current_host_process_id(), "pre-a").is_none());
}
