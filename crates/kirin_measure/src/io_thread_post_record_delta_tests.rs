use super::*;
use crate::{
    channel_layout::ChannelLayout,
    delta::{DeltaMode, DeltaSnapshot},
    MeterSession,
};

pub(super) fn exact() -> crate::meter_session::time_observation::TimeRawPoint {
    let mut session = MeterSession::new(48_000, ChannelLayout::mono()).unwrap();
    assert!(session.push_active(&vec![0.25; 4_800]));
    let mut point = session.time_raw_tail(1).pop().unwrap();
    point.wire.values = [Some(1.0), None, Some(2.0), Some(3.0), None, None];
    point.wire.crest = Some(4.0);
    point
}
pub(super) fn legacy() -> DeltaResult {
    DeltaResult {
        lufs: Some(-8.0),
        lufs_s: Some(-7.0),
        tp: Some(-6.0),
        psr: Some(-5.0),
        crest: Some(-4.0),
        mode: DeltaMode::Active,
        ..Default::default()
    }
}
pub(super) fn assert_exact(delta: &DeltaResult) {
    assert_eq!(delta.lufs, Some(1.0));
    assert_eq!(
        delta.lufs_s, None,
        "an unproven window cannot borrow a legacy value"
    );
    assert_eq!(delta.tp, Some(2.0));
    assert_eq!(delta.psr, Some(3.0));
    assert_eq!(delta.crest, Some(4.0));
}
#[test]
fn exact_values_replace_legacy_without_filling_unproven_windows() {
    let mut cache = RecordDeltaCache::default();
    let point = exact();
    let boundary = || RecordDeltaBoundary {
        span: point.wire.span,
        observed: 0,
    };
    let selected = cache.select(7, legacy(), Some(&point), Some(boundary()));
    assert_exact(&selected);
    assert_eq!(selected.mode, DeltaMode::Active);
}
#[test]
fn stop_and_contention_keep_the_last_exact_delta_only_within_its_generation() {
    let mut cache = RecordDeltaCache::default();
    let point = exact();
    let boundary = || RecordDeltaBoundary {
        span: point.wire.span,
        observed: 0,
    };
    cache.select(7, legacy(), Some(&point), Some(boundary()));
    assert_exact(&cache.select(7, legacy(), None, Some(boundary())));
    let next = cache.select(8, legacy(), None, Some(boundary()));
    assert_eq!(next.psr, Some(-5.0));
    assert_eq!(next.lufs_s, Some(-7.0));
}
#[test]
fn a_keep_without_exact_facts_retains_legacy_values() {
    let mut cache = RecordDeltaCache::default();
    let point = exact();
    let boundary = || RecordDeltaBoundary {
        span: point.wire.span,
        observed: 0,
    };
    let selected = cache.resolve(7, legacy(), None, Some(boundary()));
    assert_eq!(selected.lufs, Some(-8.0));
    assert_eq!(selected.psr, Some(-5.0));
}
#[test]
fn stale_materialization_cannot_restore_the_old_wall_clock_delta() {
    let mut cache = RecordDeltaCache::default();
    let point = exact();
    let boundary = || RecordDeltaBoundary {
        span: point.wire.span,
        observed: 0,
    };
    let mut old = legacy();
    old.mode = DeltaMode::Stale;
    old.last_active = Some(DeltaSnapshot {
        psr: Some(-9.0),
        ..Default::default()
    });
    let selected = cache.select(7, old, Some(&point), Some(boundary()));
    assert_exact(&selected);
    assert_eq!(selected.last_active.as_ref().unwrap().psr, Some(3.0));
    assert_eq!(selected.mode, DeltaMode::Stale);
}
