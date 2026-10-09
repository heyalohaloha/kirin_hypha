use super::*;
use crate::channel_layout::ChannelLayout;
use crate::{MeterClockStart, MeterSession};
use std::time::{Duration, Instant};

fn produced(epoch: u64, slots: usize) -> Vec<TimeRawPoint> {
    let mut session = MeterSession::new_in_epoch(48000, ChannelLayout::stereo(), epoch).unwrap();
    let samples: Vec<_> = (0..slots * 4800)
        .flat_map(|frame| {
            let value = 0.1 * (frame as f64 * std::f64::consts::TAU * 997.0 / 48000.0).sin();
            [value, value]
        })
        .collect();
    session.push_active_at(
        &samples,
        MeterClockStart {
            position_samples: Some(0),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            presentation_latency: crate::PresentationLatencySamples {
                source: crate::PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(0),
            },
            ..Default::default()
        },
    );
    session.time_raw_tail(64)
}

#[test]
fn exact_producer_join_keeps_original_timestamp_and_delayed_cutoff() {
    let post = produced(2, 32);
    let pre = produced(1, 32);
    let publication = TimePublication {
        span: pre[0].wire.span,
        points: pre[..30].iter().map(|p| p.wire).collect(),
    };
    let mut state = TimeComparisonState::default();
    let mut history = MeterHistory::new();
    state.ingest(Some(&publication), &post, &mut history);
    let first = state.point.clone().unwrap();
    assert_eq!(first.wire.observed, 30 * 4800);
    assert_eq!(post.last().unwrap().wire.observed, 32 * 4800);
    assert_eq!(first.completed, post[29].completed);
    assert_eq!(first.wire.values[3], Some(0.0));
    state.ingest(Some(&publication), &post, &mut history);
    assert_eq!(state.point.as_ref().unwrap().completed, first.completed);
    assert_eq!(history.recent(MeterHistoryResolution::Hz10, 64).len(), 30);
    assert!(first.remaining(first.completed + Duration::from_millis(399)) > Duration::ZERO);
    assert_eq!(
        first.remaining(first.completed + Duration::from_millis(400)),
        Duration::ZERO
    );
    assert_eq!(
        first.remaining(first.completed + Duration::from_millis(401)),
        Duration::ZERO
    );
}

#[test]
fn paired_non_decile_rate_connects_1103_frame_slots_but_breaks_a_missing_slot() {
    let produce = |epoch| {
        let mut session = MeterSession::new_in_epoch(11025, ChannelLayout::mono(), epoch).unwrap();
        for slot in 0..12 {
            let offset = slot * 1103;
            let samples: Vec<_> = (offset..offset + 1103)
                .map(|frame| 0.2 * (frame as f64 * std::f64::consts::TAU * 997.0 / 11025.0).sin())
                .collect();
            assert!(session.push_active_at(
                &samples,
                MeterClockStart {
                    position_samples: Some(offset as i64),
                    epoch: Some(1),
                    source: CaptureClockSource::ProjectTimeline,
                    presentation_latency: crate::PresentationLatencySamples {
                        source: crate::PresentationLatencySource::Vst3,
                        input: Some(0),
                        output: Some(0)
                    },
                    ..Default::default()
                },
            ));
        }
        session.time_raw_tail(64)
    };
    let pre = produce(41);
    let post = produce(42);
    assert_eq!(pre.len(), 12);
    assert_eq!(post.len(), 12);
    assert_eq!(post[11].wire.observed, 13236);
    assert_eq!(post[11].wire.endpoint, Some(13236));
    for missing in [false, true] {
        let publication = TimePublication {
            span: pre[0].wire.span,
            points: pre
                .iter()
                .enumerate()
                .filter(|(index, _)| !missing || *index != 10)
                .map(|(_, point)| point.wire)
                .collect(),
        };
        let mut state = TimeComparisonState::default();
        let mut history = MeterHistory::new();
        state.ingest(Some(&publication), &post, &mut history);
        let rows = history.recent(MeterHistoryResolution::Hz10, 3);
        assert_eq!(state.point.as_ref().unwrap().wire.observed, 13236);
        assert_eq!(rows[2].last_observed_frames, 13236);
        if missing {
            assert_eq!(rows[1].last_observed_frames, 11030);
            assert_eq!(
                rows[2].last_observed_frames - rows[1].last_observed_frames,
                2206
            );
            assert_ne!(rows[1].segment_id, rows[2].segment_id);
            assert!(!rows[2].connects_previous);
        } else {
            assert_eq!(rows[0].last_observed_frames, 11030);
            assert_eq!(rows[1].last_observed_frames, 12133);
            assert_ne!(rows[0].segment_id, 0);
            assert!(rows.windows(2).all(|pair| {
                pair[1].last_observed_frames - pair[0].last_observed_frames == 1103
                    && pair[0].segment_id == pair[1].segment_id
                    && pair[1].connects_previous
            }));
        }
    }
}

#[test]
fn newest_none_duplicate_covered_gap_and_new_source_never_restore_old_finite() {
    let mut post = produced(2, 32);
    let pre = produced(1, 32);
    let mut publication = TimePublication {
        span: pre[0].wire.span,
        points: pre.iter().map(|p| p.wire).collect(),
    };
    let mut state = TimeComparisonState::default();
    let mut history = MeterHistory::new();
    post[31].wire.values[3] = None;
    state.ingest(Some(&publication), &post, &mut history);
    assert_eq!(state.point.as_ref().unwrap().wire.observed, 32 * 4800);
    assert_eq!(state.point.as_ref().unwrap().wire.values[3], None);
    assert_eq!(
        history.recent(MeterHistoryResolution::Hz10, 1)[0].valid_count[4],
        0
    );
    // Last PRE covers the endpoint but its key is ambiguous: current must disappear.
    publication.points[30].endpoint = publication.points[31].endpoint;
    state.ingest(Some(&publication), &post, &mut history);
    assert!(state.point.is_none());
    assert_eq!(state.reason, TimeComparisonReason::Missing);
    publication.span.epoch = 9;
    for p in &mut publication.points {
        p.span = publication.span;
    }
    publication.points[30].endpoint = pre[30].wire.endpoint;
    state.ingest(Some(&publication), &post, &mut history);
    assert_eq!(state.pre_span.unwrap().epoch, 9);
    assert!(history.recent(MeterHistoryResolution::Hz10, 64).is_empty());
    assert!(state.point.is_none());
    let mut new_post = post.last().unwrap().clone();
    new_post.wire.observed += 4800;
    new_post.wire.endpoint = Some(33 * 4800);
    let mut new_pre = *publication.points.last().unwrap();
    new_pre.observed += 4800;
    new_pre.endpoint = Some(33 * 4800);
    publication.points.remove(0);
    publication.points.push(new_pre);
    post.push(new_post);
    state.ingest(Some(&publication), &post, &mut history);
    assert_eq!(history.recent(MeterHistoryResolution::Hz10, 64).len(), 1);
    // Completion does not inherit a process-external PRE timestamp or the new IO join time.
    assert_eq!(
        state.point.as_ref().unwrap().completed,
        post.last().unwrap().completed
    );
}

#[test]
fn malformed_declared_tail_and_stop_preserve_no_current_or_extended_lifetime() {
    let post = produced(2, 32);
    let pre = produced(1, 32);
    let mut publication = TimePublication {
        span: pre[0].wire.span,
        points: pre.iter().map(|p| p.wire).collect(),
    };
    let mut state = TimeComparisonState::default();
    let mut history = MeterHistory::new();
    state.ingest(Some(&publication), &post, &mut history);
    let endpoint = state.last_cutoff;
    state.fail(TimeComparisonReason::Stopped);
    assert!(state.point.is_none());
    assert_eq!(state.last_cutoff, endpoint);
    publication.points[0].span.token += 1;
    assert!(!publication.valid());
    state.ingest(Some(&publication), &post, &mut history);
    assert_eq!(state.reason, TimeComparisonReason::Incompatible);
    assert!(state.point.is_none());
    assert!(history
        .recent(MeterHistoryResolution::Hz10, 64)
        .iter()
        .all(|p| p.last_observed_frames <= endpoint));
    let _ = Instant::now();
}

#[test]
fn seek_cannot_consume_the_previous_pre_slot_again_before_new_pre_run_arrives() {
    let mut post = produced(2, 32);
    let pre = produced(1, 32);
    let publication = TimePublication {
        span: pre[0].wire.span,
        points: pre.iter().map(|p| p.wire).collect(),
    };
    let mut state = TimeComparisonState::default();
    let mut history = MeterHistory::new();
    state.ingest(Some(&publication), &post, &mut history);
    let mut seek = post.last().unwrap().clone();
    seek.wire.run += 1;
    seek.wire.observed += 4800;
    seek.wire.endpoint = pre[30].wire.endpoint;
    seek.completed = Instant::now();
    post.push(seek);
    state.ingest(Some(&publication), &post, &mut history);
    assert!(state.point.is_none());
    assert_eq!(state.reason, TimeComparisonReason::Missing);
    assert_eq!(history.recent(MeterHistoryResolution::Hz10, 64).len(), 32);
}

#[test]
fn one_sided_run_or_worker_change_cannot_join_an_unconsumed_old_pre_suffix() {
    let post = produced(2, 10);
    let pre = produced(1, 32);
    let publication = TimePublication {
        span: pre[0].wire.span,
        points: pre.iter().map(|p| p.wire).collect(),
    };
    for worker_changed in [false, true] {
        let mut state = TimeComparisonState::default();
        let mut history = MeterHistory::new();
        state.ingest(Some(&publication), &post, &mut history);
        assert_eq!(state.point.as_ref().unwrap().wire.observed, 10 * 4800);
        let mut future_post = post.clone();
        let mut next = post.last().unwrap().clone();
        next.wire.run += 1;
        next.wire.observed += 4800;
        next.wire.endpoint = pre[10].wire.endpoint;
        if worker_changed {
            next.wire.span.token += 1;
        }
        if worker_changed {
            future_post.clear();
        }
        future_post.push(next);
        state.ingest(Some(&publication), &future_post, &mut history);
        assert!(
            state.point.is_none(),
            "unconsumed PRE11 is from the previous lineage"
        );
        assert_eq!(state.pre_admission_floor, 32 * 4800);
    }
}

#[test]
fn admission_floors_retire_with_their_source_and_fresh_opposite_slots_resume() {
    let post = produced(2, 32);
    let pre = produced(1, 32);
    for changed_pre in [true, false] {
        let mut local = if changed_pre {
            post[..10].to_vec()
        } else {
            post.clone()
        };
        let mut publication = TimePublication {
            span: pre[0].wire.span,
            points: if changed_pre {
                pre.iter().map(|p| p.wire).collect()
            } else {
                pre[..10].iter().map(|p| p.wire).collect()
            },
        };
        let mut state = TimeComparisonState::default();
        let mut history = MeterHistory::new();
        state.ingest(Some(&publication), &local, &mut history);
        if changed_pre {
            let mut sought = local.last().unwrap().clone();
            sought.wire.run += 1;
            sought.wire.observed = 11 * 4800;
            sought.wire.endpoint = pre[10].wire.endpoint;
            local.push(sought);
        } else {
            let mut sought = *publication.points.last().unwrap();
            sought.run += 1;
            sought.observed = 11 * 4800;
            sought.endpoint = post[10].wire.endpoint;
            publication.points.push(sought);
        }
        state.ingest(Some(&publication), &local, &mut history);
        assert!(
            state.point.is_none(),
            "the opposite old suffix is inadmissible"
        );
        if changed_pre {
            assert_eq!(state.pre_admission_floor, 32 * 4800);
            publication.span.token += 1;
            let mut fresh = *publication.points.last().unwrap();
            fresh.span = publication.span;
            fresh.observed = 4800;
            fresh.endpoint = Some(1000);
            publication.points = vec![fresh];
        } else {
            assert_eq!(state.post_admission_floor, 32 * 4800);
            let mut fresh = local.last().unwrap().clone();
            fresh.wire.span.token += 1;
            fresh.wire.observed = 4800;
            fresh.wire.endpoint = Some(1000);
            local = vec![fresh];
        }
        state.ingest(Some(&publication), &local, &mut history);
        assert!(
            state.point.is_none(),
            "a changed source waits for a fresh opposite slot"
        );
        let mut fresh_pre = *publication.points.last().unwrap();
        fresh_pre.observed += 4800;
        fresh_pre.endpoint = Some(2000);
        fresh_pre.values = [Some(2.0); 6];
        fresh_pre.continuous_frames = 144000; // Synthetic steady window, independent of observation-counter restart.
        publication.points.push(fresh_pre);
        let mut fresh_post = local.last().unwrap().clone();
        fresh_post.wire.observed += 4800;
        fresh_post.wire.endpoint = Some(2000);
        fresh_post.wire.values = [Some(5.0); 6];
        fresh_post.wire.continuous_frames = 144000;
        local.push(fresh_post);
        state.ingest(Some(&publication), &local, &mut history);
        let joined = state
            .point
            .as_ref()
            .expect("the new source counter must not inherit its old floor");
        assert_eq!(joined.wire.endpoint, Some(2000));
        assert_eq!(joined.completed, local.last().unwrap().completed);
        assert_eq!(joined.wire.values[3], Some(3.0));
        assert_eq!(
            joined.wire.values[4], None,
            "no invented integrated PLR delta"
        );
        assert_eq!(history.recent(MeterHistoryResolution::Hz10, 64).len(), 1);
    }
}
