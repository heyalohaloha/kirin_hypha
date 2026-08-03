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

fn write_legacy_v1_claim(
    root: &Path,
    project_hash: &str,
    post_instance_id: &str,
    pre_instance_id: &str,
    pair_claimed_at: f64,
    watch_owner_id: &str,
) -> PairClaim {
    let claim = PairClaim {
        schema: LEGACY_PAIR_CLAIM_SCHEMA.to_string(),
        pre_instance_id: pre_instance_id.to_string(),
        project_hash: project_hash.to_string(),
        post_instance_id: post_instance_id.to_string(),
        post_watch_owner_id: watch_owner_id.to_string(),
        pair_owner_id: String::new(),
        host_process_id: current_host_process_id(),
        pair_claimed_at_bits: pair_claimed_at.to_bits(),
    };
    let path = claim_path(root, current_host_process_id(), pre_instance_id).unwrap();
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(path, serde_json::to_vec(&claim).unwrap()).unwrap();
    claim
}

#[test]
fn exact_claim_ownership_survives_watch_loss_until_post_engine_drops() {
    let root = isolated_root();
    let pair_owner = crate::PairOwnershipLease::new();
    let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    let instance_dir = root.join("project-a").join("post-a");
    pair_owner.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    write_post(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        pair_owner.owner_id(),
        &mut watch_owner,
    );
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 1.0, &pair_owner,),
        PublishPairClaimOutcome::Published
    );
    let claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(claim.schema, PAIR_CLAIM_SCHEMA);
    assert_eq!(claim.pair_owner_id, pair_owner.owner_id());
    assert!(claim.post_watch_owner_id.is_empty());
    let persisted: serde_json::Value = serde_json::from_slice(
        &fs::read(claim_path(&root, current_host_process_id(), "pre-a").unwrap()).unwrap(),
    )
    .unwrap();
    assert_eq!(persisted["schema"], PAIR_CLAIM_SCHEMA);
    assert_eq!(persisted["pair_owner_id"], pair_owner.owner_id());
    assert!(persisted.get("post_watch_owner_id").is_none());
    assert!(pair_claim_is_owned(&root, &claim));
    assert!(pair_claim_is_live(&root, &claim));
    drop(watch_owner);
    assert!(pair_claim_is_owned(&root, &claim));
    assert!(!pair_claim_is_live(&root, &claim));
    drop(pair_owner);
    assert!(!pair_claim_is_owned(&root, &claim));
}

#[test]
fn v2_fresh_watch_snapshot_cannot_resurrect_an_unlinked_pair_marker() {
    let root = isolated_root();
    let pair_owner = crate::PairOwnershipLease::new();
    let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    let instance_dir = root.join("project-a").join("post-a");
    pair_owner.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    write_post(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        pair_owner.owner_id(),
        &mut watch_owner,
    );
    assert_ne!(pair_owner.owner_id(), watch_owner.owner_id());
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 1.0, &pair_owner),
        PublishPairClaimOutcome::Published
    );
    let claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();

    let marker = crate::pair_ownership_marker::owner_marker_path(
        &instance_dir,
        pair_owner.owner_id(),
        "pre-a",
        1.0f64.to_bits(),
    )
    .unwrap();
    assert!(pair_claim_is_live(&root, &claim));
    fs::remove_file(marker).unwrap();

    assert!(!pair_claim_is_owned(&root, &claim));
    assert!(!pair_claim_is_live(&root, &claim));
    let competing = crate::PairOwnershipLease::new();
    competing
        .bind(&root.join("project-a").join("post-b"), Some("pre-a"), 2.0)
        .unwrap();
    assert_eq!(
        publish(&root, "project-a", "post-b", "pre-a", 2.0, &competing),
        PublishPairClaimOutcome::Published,
        "v2 post.json must not revive ownership after the engine marker disappears"
    );
}

#[test]
fn legacy_v1_claim_uses_only_its_exact_fresh_live_watch_snapshot() {
    let root = isolated_root();
    let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    write_post(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        "unrelated-v2-pair-owner",
        &mut watch_owner,
    );
    let legacy = write_legacy_v1_claim(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        watch_owner.owner_id(),
    );

    let decoded = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(decoded, legacy);
    assert!(pair_claim_is_owned(&root, &decoded));
    assert!(pair_claim_is_live(&root, &decoded));

    drop(watch_owner);
    assert!(!pair_claim_is_owned(&root, &decoded));
    assert!(!pair_claim_is_live(&root, &decoded));
}

#[test]
fn legacy_v1_claim_rejects_a_different_live_watch_writer() {
    let root = isolated_root();
    let mut actual_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    write_post(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        "unrelated-v2-pair-owner",
        &mut actual_watch,
    );
    let claimed_watch_owner = uuid::Uuid::new_v4().to_string();
    let legacy = write_legacy_v1_claim(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        &claimed_watch_owner,
    );

    assert_ne!(actual_watch.owner_id(), claimed_watch_owner);
    assert!(!pair_claim_is_owned(&root, &legacy));
    assert!(!pair_claim_is_live(&root, &legacy));
}

#[test]
fn legacy_v1_claim_rejects_an_exact_but_stale_watch_snapshot() {
    let root = isolated_root();
    let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    write_post(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        "unrelated-v2-pair-owner",
        &mut watch_owner,
    );
    let legacy = write_legacy_v1_claim(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        watch_owner.owner_id(),
    );
    let post_path = root.join("project-a").join("post-a").join("post.json");
    let stale_at = SystemTime::now()
        .checked_sub(Duration::from_secs(DISCOVERY_STALE_SECS + 1))
        .unwrap();
    File::options()
        .write(true)
        .open(post_path)
        .unwrap()
        .set_times(std::fs::FileTimes::new().set_modified(stale_at))
        .unwrap();

    assert!(crate::watch_snapshot_lease::snapshot_owner_is_live(
        &root.join("project-a").join("post-a"),
        watch_owner.owner_id(),
    ));
    assert!(!pair_claim_is_owned(&root, &legacy));
    assert!(!pair_claim_is_live(&root, &legacy));
}

#[test]
fn recreated_engine_cannot_revive_the_previous_owner_claim() {
    let root = isolated_root();
    let old_owner = crate::PairOwnershipLease::new();
    let mut old_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    let instance_dir = root.join("project-a").join("post-a");
    old_owner.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    write_post(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        old_owner.owner_id(),
        &mut old_watch,
    );
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 1.0, &old_owner),
        PublishPairClaimOutcome::Published
    );
    let old_claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    drop(old_watch);
    drop(old_owner);

    let new_owner = crate::PairOwnershipLease::new();
    let mut new_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    new_owner.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    write_post(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        new_owner.owner_id(),
        &mut new_watch,
    );

    assert!(!pair_claim_is_owned(&root, &old_claim));
    assert_eq!(
        publish(&root, "project-a", "post-a", "pre-a", 1.0, &new_owner),
        PublishPairClaimOutcome::Published
    );
}

#[test]
fn live_legacy_v1_claim_retains_stable_identity_compatibility() {
    let root = isolated_root();
    let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    write_post(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        "unrelated-v2-pair-owner",
        &mut watch_owner,
    );
    write_legacy_v1_claim(
        &root,
        "project-a",
        "post-a",
        "pre-a",
        1.0,
        watch_owner.owner_id(),
    );

    assert!(!live_claim_owned_by_other(
        &root,
        "pre-a",
        "project-a",
        "post-a",
        "new-v2-engine-owner",
        1.0,
    ));
    assert!(live_claim_owned_by_other(
        &root,
        "pre-a",
        "project-a",
        "post-other",
        "new-v2-engine-owner",
        1.0,
    ));
}

#[test]
fn same_engine_can_advance_claim_while_old_and_new_markers_overlap() {
    let root = isolated_root();
    let pair_owner = crate::PairOwnershipLease::new();
    let instance_dir = root.join("project-a").join("post-a");

    let first = pair_owner
        .commit_binding(Some(&instance_dir), Some("pre-a"), 1.0, || {
            Some(
                publish_pair_claim(
                    &root,
                    "pre-a",
                    "project-a",
                    "post-a",
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

    let advanced = pair_owner
        .commit_binding(Some(&instance_dir), Some("pre-a"), 2.0, || {
            assert!(pair_claim_is_owned(
                &root,
                &read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap(),
            ));
            Some(
                publish_pair_claim(
                    &root,
                    "pre-a",
                    "project-a",
                    "post-a",
                    pair_owner.owner_id(),
                    current_host_process_id(),
                    2.0,
                )
                .unwrap(),
            )
        })
        .unwrap()
        .unwrap();
    assert_eq!(advanced, PublishPairClaimOutcome::Published);
    let current = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
    assert_eq!(current.pair_claimed_at_bits, 2.0f64.to_bits());
    assert!(pair_claim_is_owned(&root, &current));
    assert!(!release_pair_claim_for_engine_binding(
        &root,
        "pre-a",
        "project-a",
        "post-a",
        pair_owner.owner_id(),
        current_host_process_id(),
        1.0,
    )
    .unwrap());
    assert_eq!(
        read_pair_claim(&root, current_host_process_id(), "pre-a"),
        Some(current)
    );
}
