use super::*;
use std::fs;
use std::sync::Arc;

fn isolated_dir(label: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "kirin_pair_lease_{label}_{}_{}",
        std::process::id(),
        Uuid::new_v4()
    ));
    fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn worker_generation_drop_does_not_release_engine_owned_pair() {
    let instance_dir = isolated_dir("restart");
    let engine_owner = Arc::new(PairOwnershipLease::new());
    engine_owner
        .bind(&instance_dir, Some("pre-a"), 1.0)
        .unwrap();
    let owner_id = engine_owner.owner_id().to_string();

    let worker_generation = Arc::clone(&engine_owner);
    drop(worker_generation);
    assert!(pair_owner_claim_is_live(
        &instance_dir,
        &owner_id,
        "pre-a",
        1.0f64.to_bits()
    ));

    drop(engine_owner);
    assert!(!pair_owner_claim_is_live(
        &instance_dir,
        &owner_id,
        "pre-a",
        1.0f64.to_bits()
    ));
    let _ = fs::remove_dir_all(instance_dir);
}

#[test]
fn reselect_acquires_new_marker_before_retiring_old_marker() {
    let first = isolated_dir("first");
    let second = isolated_dir("second");
    let lease = PairOwnershipLease::new();
    let owner_id = lease.owner_id().to_string();

    lease.bind(&first, Some("pre-a"), 1.0).unwrap();
    lease.bind(&second, Some("pre-b"), 2.0).unwrap();
    assert!(!pair_owner_claim_is_live(
        &first,
        &owner_id,
        "pre-a",
        1.0f64.to_bits()
    ));
    assert!(pair_owner_claim_is_live(
        &second,
        &owner_id,
        "pre-b",
        2.0f64.to_bits()
    ));

    let _ = fs::remove_dir_all(first);
    drop(lease);
    let _ = fs::remove_dir_all(second);
}

#[test]
fn claimed_clear_releases_canonical_claim_before_engine_proofs() {
    let root = isolated_dir("claimed_clear");
    let instance_dir = root.join("project").join("post-1");
    let lease = PairOwnershipLease::new();
    lease
        .commit_claimed_binding_if(
            &root,
            Some(&instance_dir),
            Some("pre-1"),
            "project",
            "post-1",
            1.0,
            || true,
            || Some(()),
        )
        .unwrap();
    let claim = crate::read_pair_claim(&root, std::process::id(), "pre-1").unwrap();
    assert!(crate::pair_claim_is_owned(&root, &claim));

    lease
        .commit_claimed_binding_if(
            &root,
            Some(&instance_dir),
            None,
            "project",
            "post-1",
            0.0,
            || true,
            || Some(()),
        )
        .unwrap();
    assert!(crate::read_pair_claim(&root, std::process::id(), "pre-1").is_none());
    assert!(!crate::pair_claim_is_owned(&root, &claim));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn claimed_binding_rejects_a_worker_directory_from_another_identity() {
    let root = isolated_dir("identity_guard");
    let wrong = root.join("other-project").join("post-1");
    let lease = PairOwnershipLease::new();
    let error = lease
        .commit_claimed_binding_if(
            &root,
            Some(&wrong),
            Some("pre-1"),
            "project",
            "post-1",
            1.0,
            || true,
            || Some(()),
        )
        .unwrap_err();
    assert_eq!(error.kind(), io::ErrorKind::InvalidInput);
    let _ = fs::remove_dir_all(root);
}
