use super::*;

fn point(frame: u64) -> Point {
    Point {
        pre_epoch: 1,
        post_epoch: 2,
        pre_incarnation: 3,
        pre_generation: 4,
        post_generation: 5,
        pre_run: 6,
        post_run: 7,
        pre_observed: frame,
        post_observed: frame,
        endpoint: frame as i64,
        source: 1,
        pre_m: Some(-20.0),
        post_m: Some(-18.0),
        pre_tp: Some(-2.0),
        post_tp: Some(-1.8),
    }
}

#[test]
fn relation_uses_raw_facts_and_missing_is_not_none() {
    let mut p = point(4_800);
    assert!((p.delta_m().unwrap() - 2.0).abs() < 1e-9);
    assert!((p.delta_tp().unwrap() - 0.2).abs() < 1e-9);
    assert!((p.relation().unwrap() + 1.8).abs() < 1e-9);
    assert_eq!(p.crossing(), Crossing::None);
    p.pre_m = None;
    assert_eq!(p.relation(), None);
    assert_eq!(p.crossing(), Crossing::None);
    p.pre_tp = None;
    assert_eq!(p.crossing(), Crossing::Missing);
    p.pre_tp = Some(f64::NAN);
    assert_eq!(p.crossing(), Crossing::Missing);
    p.post_m = Some(f64::INFINITY);
    assert_eq!(p.delta_m(), None);
}

#[test]
fn strict_thresholds_classify_unrounded_values_on_both_sides() {
    for (tp, severity_expected) in [
        (None, 0),
        (Some(f64::INFINITY), 0),
        (Some((-1.0_f64).next_down()), 1),
        (Some(-1.0), 1),
        (Some((-1.0_f64).next_up()), 2),
        (Some(-0.0), 2),
        (Some(0.0), 2),
        (Some(0.0_f64.next_up()), 3),
    ] {
        assert_eq!(severity(tp), severity_expected, "{tp:?}");
    }
    let mut p = point(4_800);
    for (pre, post, expected) in [
        (-1.0, -1.0, Crossing::None),
        (-0.99, -1.0, Crossing::PreOnly),
        (-1.0, -0.99, Crossing::PostOnly),
        (-0.99, 0.001, Crossing::Both),
    ] {
        p.pre_tp = Some(pre);
        p.post_tp = Some(post);
        assert_eq!(p.crossing(), expected);
    }
}

#[test]
fn unchanged_snapshot_is_free_and_missing_samples_still_age_out() {
    let mut history = History::default();
    assert_eq!(history.points.capacity(), 0);
    history.advance(48_000, 4_800);
    history.push(point(4_800));
    history.update_freshness(true);
    assert_eq!(history.status, Status::Active);
    let revision = history.revision;
    let cached = *history.points.back().unwrap();
    for _ in 0..1_000 {
        history.push(point(4_800));
        history.advance(48_000, 4_800);
        history.update_freshness(true);
        assert!(history.snapshot(revision).is_none());
    }
    assert_eq!(*history.points.back().unwrap(), cached);
    assert!((cached.relation.unwrap() + 1.8).abs() < 1e-9);
    history.advance(48_000, 14_400);
    history.update_freshness(true);
    assert_eq!(history.status, Status::Active); // inclusive 200 ms display freshness
    history.advance(48_000, 14_401);
    history.update_freshness(true);
    assert_eq!(history.status, Status::Hold);
    history.advance(48_000, 2_884_800);
    history.update_freshness(true);
    assert!(history.points.is_empty());
    assert_eq!(history.status, Status::Syncing);
}

#[test]
fn storage_is_bounded_and_epoch_clear_invalidates_all_points() {
    let mut history = History::default();
    for i in 1..=6_001 {
        history.push(point(i * 4_800));
    }
    assert_eq!(history.points.len(), CAPACITY);
    assert_eq!(history.points.capacity(), CAPACITY);
    assert!(std::mem::size_of::<MatchedPoint>() * CAPACITY * 3 < 512 * 1024);
    let revision = history.revision;
    history.clear(8, Status::Syncing);
    let snapshot = history.snapshot(revision).unwrap();
    assert!(snapshot.points.is_empty());
    assert_eq!(snapshot.binding, 8);
}
