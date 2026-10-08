use super::*;
use std::time::Instant;

fn series(epoch: u64) -> Vec<TimeRawPoint> {
    (1..=10)
        .map(|slot| TimeRawPoint {
            wire: TimeWirePoint {
                span: TimeSourceSpan {
                    epoch,
                    incarnation: 1,
                    generation: 1,
                    token: epoch,
                    sample_rate: 48000,
                    channels: 2,
                },
                run: 1,
                observed: slot * 4800,
                endpoint: Some(slot as i64 * 4800),
                clock: 1,
                usable: true,
                values: [Some(epoch as f64); 6],
            },
            completed: Instant::now(),
        })
        .collect()
}

fn sought(points: &[TimeRawPoint], slots: usize) -> Vec<TimeRawPoint> {
    let last = points.last().unwrap();
    (1..=slots)
        .map(|slot| TimeRawPoint {
            wire: TimeWirePoint {
                run: 2,
                observed: last.wire.observed + slot as u64 * 4800,
                endpoint: Some(10000000 + slot as i64 * 4800),
                ..last.wire
            },
            completed: Instant::now(),
        })
        .collect()
}

fn publication(pre: &[TimeRawPoint]) -> TimePublication {
    TimePublication {
        span: pre[0].wire.span,
        points: pre.iter().map(|p| p.wire).collect(),
    }
}

#[test]
fn same_seek_with_either_publication_one_tick_late_joins_the_first_sought_slot() {
    for pre_first in [false, true] {
        let mut pre = series(1);
        let mut post = series(2);
        let mut state = TimeComparisonState::default();
        let mut history = MeterHistory::new();
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        let new_pre = sought(&pre, 2);
        let new_post = sought(&post, 3);
        if pre_first {
            pre.extend(new_pre.clone());
        } else {
            post.extend(new_post.clone());
        }
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        assert!(state.point.is_none());
        assert_eq!(state.reason, TimeComparisonReason::Missing);
        assert_eq!(history.recent(MeterHistoryResolution::Hz10, 64).len(), 10);
        if pre_first {
            post.extend(new_post.clone());
        } else {
            pre.extend(new_pre);
        }
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        let rows = history.recent(MeterHistoryResolution::Hz10, 64);
        assert_eq!(rows.len(), 12);
        assert_eq!(rows[10].last_timeline_endpoint_samples, Some(10004800));
        assert_eq!(rows[11].last_timeline_endpoint_samples, Some(10009600));
        assert_ne!(rows[9].run_id, rows[10].run_id);
        assert_eq!(state.pre_admission_floor, 0);
        assert_eq!(state.post_admission_floor, 0);
        assert_eq!(
            state.point.as_ref().unwrap().completed,
            new_post[1].completed
        );
        assert_eq!(state.point.as_ref().unwrap().wire.values[3], Some(1.0));
        // The same publication never re-stamps or duplicates a slot.
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        assert_eq!(history.recent(MeterHistoryResolution::Hz10, 64).len(), 12);
        assert_eq!(
            state.point.as_ref().unwrap().completed,
            new_post[1].completed
        );
    }
}

#[test]
fn equal_endpoint_after_a_different_seek_or_worker_replacement_keeps_the_opposite_floor() {
    for replaced_worker in [false, true] {
        let mut pre = series(1);
        let mut post = series(2);
        let mut state = TimeComparisonState::default();
        let mut history = MeterHistory::new();
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        post.extend(sought(&post, 3));
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        assert!(state.point.is_none());
        assert_eq!(state.reason, TimeComparisonReason::Missing);
        let mut different = sought(&pre, 1).remove(0);
        if replaced_worker {
            different.wire.span.token += 1;
            pre.clear();
        } else {
            // The same endpoint was reached with a different measured transport shift.
            different.wire.observed += 4 * 4800;
        }
        pre.push(different);
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        assert!(state.point.is_none());
        assert_eq!(state.post_admission_floor, 13 * 4800);
        assert_eq!(
            state.reason,
            TimeComparisonReason::Missing,
            "worker_replaced={replaced_worker}"
        );
        let rows = history.recent(MeterHistoryResolution::Hz10, 64);
        assert_eq!(rows.len(), if replaced_worker { 0 } else { 10 });
    }
}

#[test]
fn initial_publication_lag_without_any_proven_pair_remains_waiting() {
    let pre = series(1);
    let mut post = series(2);
    for point in &mut post {
        point.wire.endpoint = point.wire.endpoint.map(|endpoint| endpoint + 48000);
    }
    let mut state = TimeComparisonState::default();
    let mut history = MeterHistory::new();
    state.ingest(Some(&publication(&pre)), &post, &mut history);
    assert!(state.point.is_none());
    assert_eq!(state.reason, TimeComparisonReason::Waiting);
    assert!(history.recent(MeterHistoryResolution::Hz10, 64).is_empty());
}

#[test]
fn same_seek_keeps_exact_baselines_across_each_sides_first_mixed_slot() {
    for pre_first in [false, true] {
        let mut pre = series(1);
        let mut post = series(2);
        let mut state = TimeComparisonState::default();
        let mut history = MeterHistory::new();
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        let new_pre = sought(&pre, 3);
        let new_post = sought(&post, 3);
        let mixed = |point: &TimeRawPoint| TimeRawPoint {
            wire: TimeWirePoint {
                run: 1,
                endpoint: None,
                usable: false,
                ..point.wire
            },
            completed: point.completed,
        };
        if pre_first {
            pre.push(mixed(&new_pre[0]));
        } else {
            post.push(mixed(&new_post[0]));
        }
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        if pre_first {
            pre.push(new_pre[1].clone());
            post.push(mixed(&new_post[0]));
        } else {
            post.push(new_post[1].clone());
            pre.push(mixed(&new_pre[0]));
        }
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        assert!(state.point.is_none());
        if pre_first {
            post.push(new_post[1].clone());
            pre.push(new_pre[2].clone());
        } else {
            pre.push(new_pre[1].clone());
            post.push(new_post[2].clone());
        }
        state.ingest(Some(&publication(&pre)), &post, &mut history);
        let rows = history.recent(MeterHistoryResolution::Hz10, 64);
        assert_eq!(rows.len(), 11);
        assert_eq!(rows[10].last_timeline_endpoint_samples, Some(10009600));
        assert_eq!(
            state.point.as_ref().unwrap().completed,
            new_post[1].completed
        );
        assert_eq!(state.pre_admission_floor, 0);
        assert_eq!(state.post_admission_floor, 0);
    }
}
