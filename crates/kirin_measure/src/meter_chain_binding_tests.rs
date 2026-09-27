use super::*;

fn target(identity: &std::path::Path, generation: u64, claimed_at: f64) -> MeterHistoryTarget {
    MeterHistoryTarget::from_pre_json("pre".into(), identity)
        .unwrap()
        .with_post_binding("post-pair-owner", "post", generation, claimed_at)
        .unwrap()
}

#[test]
fn new_post_pair_generation_discards_old_chain_even_with_the_same_pre_owner() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(Some(target(&identity, 7, 1.0)));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let before = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(before.status, chain::Status::Active);
    assert!(!before.points.is_empty());

    // The PRE identity and content publication are unchanged. A new explicit POST binding
    // must not inherit the old selected point or claim those windows as its own.
    post.service_post_endpoint(Some(target(&identity, 8, 2.0)));
    let after = post.chain_snapshot(0, &meter).unwrap();
    assert_ne!(after.binding, before.binding);
    assert!(after.points.is_empty());
    assert_ne!(after.status, chain::Status::Active);

    for block in 16..24_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(Some(target(&identity, 8, 2.0)));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let recovered = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(recovered.status, chain::Status::Active);
    assert!(recovered
        .points
        .iter()
        .all(|point| point.raw.endpoint > 16 * 4_800));
}

#[test]
fn pair_claim_revision_and_owner_are_part_of_the_binding() {
    let directory = tempfile::tempdir().unwrap();
    let identity = directory.path().join("pre.json");
    let base = MeterHistoryTarget::from_pre_json("pre".into(), &identity).unwrap();
    let first = base
        .clone()
        .with_post_binding("post-pair-owner", "post", 7, 1.0)
        .unwrap();
    assert_ne!(first, base);
    assert_ne!(
        first,
        base.clone()
            .with_post_binding("other-pair-owner", "post", 7, 1.0)
            .unwrap()
    );
    assert_ne!(
        first,
        base.clone()
            .with_post_binding("post-pair-owner", "post", 7, 2.0)
            .unwrap()
    );
    assert_ne!(
        first,
        base.clone()
            .with_post_binding("post-pair-owner", "another-post", 7, 1.0)
            .unwrap()
    );
    assert!(base.clone().with_post_binding("", "post", 7, 1.0).is_none());
    assert!(base
        .clone()
        .with_post_binding("post-pair-owner", "post", 0, 1.0)
        .is_none());
    assert!(base
        .clone()
        .with_post_binding("post-pair-owner", "post", 7, f64::NAN)
        .is_none());
    assert!(base
        .with_post_binding("post-pair-owner", "post", 7, 0.0)
        .is_none());
}

#[test]
fn rebind_clears_old_chain_before_retrying_a_missing_pre_identity() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(Some(target(&identity, 7, 1.0)));
    }
    let meter = post_session.lock().unwrap().snapshot();
    assert_eq!(
        post.chain_snapshot(0, &meter).unwrap().status,
        chain::Status::Active
    );
    std::fs::remove_file(&identity).unwrap();
    post.service_post_endpoint(Some(target(&identity, 8, 2.0)));
    let after = post.chain_snapshot(0, &meter).unwrap();
    assert!(after.points.is_empty());
    assert_ne!(after.status, chain::Status::Active);
}

#[test]
fn changed_pre_daw_session_invalidates_the_old_pair_history() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(Some(target(&identity, 7, 1.0)));
    }
    let meter = post_session.lock().unwrap().snapshot();
    assert_eq!(
        post.chain_snapshot(0, &meter).unwrap().status,
        chain::Status::Active
    );
    std::fs::write(
        &identity,
        br#"{"instance_id":"pre","watch_owner_id":"owner","daw_session_id":"other-song","signal_state":"active"}"#,
    )
    .unwrap();
    post.service_post_endpoint(Some(target(&identity, 7, 1.0)));
    let after = post.chain_snapshot(0, &meter).unwrap();
    assert!(after.points.is_empty());
    assert_ne!(after.status, chain::Status::Active);
}
