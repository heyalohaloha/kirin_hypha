//! Different loop passes must never acquire a fresh correspondence from equal start numbers.
use super::*;
use crate::{channel_layout::ChannelLayout, MeterClockStart, MeterSession};

fn feed(session: &mut MeterSession, from: i64, to: i64, start: i64, level: f64) {
    for position in (from..to).step_by(480) {
        let samples: Vec<_> = (position..position + 480)
            .map(|n| level * (n as f64 * std::f64::consts::TAU * 997.0 / 48_000.0).sin())
            .collect();
        assert!(session.push_active_at(
            &samples,
            MeterClockStart {
                position_samples: Some(position),
                playback_start: Some(start),
                source: CaptureClockSource::ProjectTimeline,
                epoch: Some(1),
                ..Default::default()
            }
        ));
    }
}
fn join(pre: &MeterSession, post: &MeterSession, state: &mut TimeComparisonState) {
    state.ingest(
        Some(&TimePublication {
            span: pre.time_source_span(),
            points: pre.time_raw_tail(32).iter().map(|p| p.wire).collect(),
        }),
        &post.time_raw_tail(64),
        &mut MeterHistory::new(),
    );
}

#[test]
fn a_temporary_start_mismatch_keeps_two_passes_of_lag_retired() {
    for lagging_pre in [true, false] {
        let mut ahead = MeterSession::new_in_epoch(48_000, ChannelLayout::mono(), 1).unwrap();
        let mut lag = MeterSession::new_in_epoch(48_000, ChannelLayout::mono(), 2).unwrap();
        let mut state = TimeComparisonState::default();
        let compare =
            |ahead: &MeterSession, lag: &MeterSession, state: &mut TimeComparisonState| {
                if lagging_pre {
                    join(lag, ahead, state)
                } else {
                    join(ahead, lag, state)
                }
            };
        // The first playback starts inside the cycle. Signal starts differ, transport starts agree.
        feed(&mut ahead, 24_000, 96_000, 24_000, 0.1);
        feed(&mut lag, 28_800, 72_000, 24_000, 0.1);
        compare(&ahead, &lag, &mut state);
        assert!(
            state.point.is_some(),
            "positive control before the wrap: {:?}, PRE {:?}, POST {:?}",
            state.reason,
            ahead.time_raw_tail(1),
            lag.time_raw_tail(1)
        );
        assert_ne!(
            ahead.time_raw_tail(1)[0].wire.clock_origin,
            lag.time_raw_tail(1)[0].wire.clock_origin
        );
        feed(&mut ahead, 0, 96_000, 0, 0.5);
        compare(&ahead, &lag, &mut state);
        assert!(
            state.point.is_none(),
            "one side has reached the cycle start"
        );
        feed(&mut ahead, 0, 96_000, 0, 0.3);
        feed(&mut lag, 72_000, 96_000, 24_000, 0.1);
        compare(&ahead, &lag, &mut state);
        for position in (0..96_000).step_by(4_800) {
            feed(&mut lag, position, position + 4_800, 0, 0.5);
            compare(&ahead, &lag, &mut state);
            assert!(
                state.point.is_none(),
                "equal start positions cannot compare pass two against pass three: {:?}",
                state.point
            );
        }
        feed(&mut lag, 0, 96_000, 0, 0.3);
        compare(&ahead, &lag, &mut state);
        let mut resumed = state.point.is_some();
        for position in (0..24_000).step_by(4_800) {
            feed(&mut ahead, position, position + 4_800, 0, 0.2);
            feed(&mut lag, position, position + 4_800, 0, 0.2);
            compare(&ahead, &lag, &mut state);
            if let Some(point) = state.point.as_ref() {
                assert!(point.wire.values[2].is_some_and(|v| v.abs() < 1e-6));
                resumed = true;
            }
        }
        assert!(
            resumed,
            "the same pass must compare again after catching up"
        );
    }
}
