use super::*;

#[test]
fn post_forward_seek_does_not_join_pre_windows_published_before_the_seek() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    assert_eq!(
        post.chain_snapshot(0, &meter).unwrap().status,
        chain::Status::Active
    );

    // PRE continues while POST has no callbacks. Its complete windows now include future
    // coordinates, but they are from the old playback occurrence when POST seeks there.
    for block in 16..32_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
    }
    for block in 24..29_i64 {
        let at = block * 4_800;
        advance_exact(&post_session, at, at, 0, 0.5);
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Ambiguous);
    assert!(chain.points.is_empty());
}

#[test]
fn a_one_sided_forward_seek_does_not_recover_merely_when_pre_catches_up() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    for block in 16..32_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
    }
    for block in 24..29_i64 {
        let at = block * 4_800;
        advance_exact(&post_session, at, at, 0, 0.5);
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    // PRE's unbroken run later reaches equal coordinates, but it never participated in
    // POST's seek. Fresh publication alone cannot certify the same playback occurrence.
    for block in 32..48_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        let post_at = (block - 3) * 4_800;
        advance_exact(&post_session, post_at, post_at, 0, 0.5);
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Ambiguous);
    assert!(chain.points.is_empty());
}

#[test]
fn post_only_pause_recovers_after_pre_has_advanced_into_the_new_run() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..32_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        if block == 16 {
            post_session.lock().unwrap().pause();
        } else {
            advance_exact(&post_session, at, at, 0, 0.5);
        }
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Active);
    assert!(chain
        .points
        .last()
        .is_some_and(|p| p.raw.endpoint >= 32 * 4_800));
}

#[test]
fn initial_pair_does_not_adopt_a_static_pre_tail_from_an_earlier_occurrence() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..32_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
    }
    // Pairing begins after PRE has stopped. Equal coordinates alone do not make its
    // previously published 400 ms windows part of POST's newly observed occurrence.
    for block in 24..30_i64 {
        let at = block * 4_800;
        advance_exact(&post_session, at, at, 0, 0.5);
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Syncing);
    assert!(chain.points.is_empty());
}

#[test]
fn both_sides_seek_to_a_new_run_and_resume_only_with_fresh_windows() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    for block in 24..40_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Active);
    assert!(chain
        .points
        .iter()
        .all(|point| point.raw.pre_run == 2 && point.raw.post_run == 2));
    assert_eq!(chain.points.last().unwrap().raw.endpoint, 40 * 4_800);
}

#[test]
fn post_pause_then_forward_seek_cannot_join_a_continuing_pre_run() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    post_session.lock().unwrap().pause();
    // PRE carries on while POST has no callbacks. The latter resumes at an overlapping
    // future coordinate following a seek, not at the same playback occurrence.
    for block in 16..32_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
    }
    for block in 24..48_i64 {
        let at = block * 4_800;
        advance_exact(&post_session, at, at, 0, 0.5);
        if block >= 32 {
            advance_exact(&pre_session, at, at, 0, 0.25);
            pre.service_pre_endpoint("pre", "song", "owner", directory.path())
                .unwrap();
        }
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Ambiguous);
    assert!(chain.points.is_empty());
}

#[test]
fn pre_pause_then_forward_seek_cannot_join_a_continuing_post_run() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    pre_session.lock().unwrap().pause();
    for block in 16..32_i64 {
        let at = block * 4_800;
        advance_exact(&post_session, at, at, 0, 0.5);
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    for block in 24..48_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        if block >= 32 {
            advance_exact(&post_session, at, at, 0, 0.5);
        }
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Ambiguous);
    assert!(chain.points.is_empty());
}

#[test]
fn paused_post_seek_cannot_join_when_pre_catches_up_to_its_first_window() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    post_session.lock().unwrap().pause();
    for block in 32..36_i64 {
        let at = block * 4_800;
        advance_exact(&post_session, at, at, 0, 0.5);
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    for block in 16..36_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
    }
    post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    for block in 36..48_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Ambiguous);
    assert!(chain.points.is_empty());
}
