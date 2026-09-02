use super::*;

fn claim(pair_owner_id: &str) -> crate::PairClaim {
    crate::PairClaim {
        schema: crate::PAIR_CLAIM_SCHEMA.to_string(),
        pre_instance_id: "pre-1".to_string(),
        project_hash: "project".to_string(),
        post_instance_id: "post-1".to_string(),
        post_watch_owner_id: String::new(),
        pair_owner_id: pair_owner_id.to_string(),
        host_process_id: std::process::id(),
        pair_claimed_at_bits: 1.0f64.to_bits(),
    }
}

#[test]
fn v2_claim_stays_owned_only_for_the_exact_engine_owner() {
    let owned = claim("pair-owner-a");
    assert!(pair_claim_matches_desired_binding(
        &owned,
        Some("pre-1"),
        "project",
        "post-1",
        "pair-owner-a",
        1.0,
    ));
    assert!(!pair_claim_matches_desired_binding(
        &owned,
        Some("pre-1"),
        "project",
        "post-1",
        "pair-owner-b",
        1.0,
    ));
}

#[test]
fn claim_reuse_requires_every_binding_dimension_to_match() {
    let owned = claim("pair-owner-a");
    let mismatches = [
        (None, "project", "post-1", "pair-owner-a", 1.0),
        (Some("pre-2"), "project", "post-1", "pair-owner-a", 1.0),
        (
            Some("pre-1"),
            "other-project",
            "post-1",
            "pair-owner-a",
            1.0,
        ),
        (Some("pre-1"), "project", "post-2", "pair-owner-a", 1.0),
        (Some("pre-1"), "project", "post-1", "pair-owner-b", 1.0),
        (
            Some("pre-1"),
            "project",
            "post-1",
            "pair-owner-a",
            1.000_000_000_000_000_2,
        ),
    ];

    for (pre, project, post, owner, claimed_at) in mismatches {
        assert!(!pair_claim_matches_desired_binding(
            &owned, pre, project, post, owner, claimed_at,
        ));
    }
}

#[test]
fn claim_timestamp_comparison_is_bit_exact() {
    let mut owned = claim("pair-owner-a");
    owned.pair_claimed_at_bits = (-0.0f64).to_bits();

    assert!(pair_claim_matches_desired_binding(
        &owned,
        Some("pre-1"),
        "project",
        "post-1",
        "pair-owner-a",
        -0.0,
    ));
    assert!(!pair_claim_matches_desired_binding(
        &owned,
        Some("pre-1"),
        "project",
        "post-1",
        "pair-owner-a",
        0.0,
    ));
}

#[test]
fn service_publishes_only_after_the_same_binding_snapshot_is_revalidated() {
    let temp = tempfile::tempdir().unwrap();
    let instance_dir = temp.path().join("project").join("post-1");
    fs::create_dir_all(&instance_dir).unwrap();
    let pair_owner = crate::PairOwnershipLease::new();
    let mut owned = None;
    let mut next_publish = Instant::now() - Duration::from_secs(1);

    service_pair_claim(
        temp.path(),
        &instance_dir,
        true,
        Some("pre-1"),
        "project",
        "post-1",
        1.0,
        &pair_owner,
        &mut owned,
        &mut next_publish,
        |expected_pre, expected_claimed_at| {
            expected_pre == Some("pre-1") && expected_claimed_at.to_bits() == 1.0f64.to_bits()
        },
    );

    let published = owned.expect("exact binding must publish one owned claim");
    assert!(pair_claim_matches_desired_binding(
        &published,
        Some("pre-1"),
        "project",
        "post-1",
        pair_owner.owner_id(),
        1.0,
    ));
}

#[test]
fn service_does_not_publish_before_post_snapshot_or_after_binding_changes() {
    for (post_snapshot_written, validation_result) in [(false, true), (true, false)] {
        let temp = tempfile::tempdir().unwrap();
        let instance_dir = temp.path().join("project").join("post-1");
        fs::create_dir_all(&instance_dir).unwrap();
        let pair_owner = crate::PairOwnershipLease::new();
        let mut owned = None;
        let mut next_publish = Instant::now() - Duration::from_secs(1);

        service_pair_claim(
            temp.path(),
            &instance_dir,
            post_snapshot_written,
            Some("pre-1"),
            "project",
            "post-1",
            1.0,
            &pair_owner,
            &mut owned,
            &mut next_publish,
            |_, _| validation_result,
        );

        assert!(owned.is_none());
        assert!(crate::pair_claim_index::read_pair_claim(
            temp.path(),
            crate::post_candidates::current_host_process_id(),
            "pre-1",
        )
        .is_none());
    }
}
