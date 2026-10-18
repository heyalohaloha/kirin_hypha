//! A LOOP wrap repeats every position. When one side's Measure Thread lags, the late points of
//! its previous pass carry the same positions as the other side's new pass; joining them would
//! subtract two different passes (a wrong PRE/POST pair).
use super::*;
use crate::{channel_layout::ChannelLayout, MeterClockStart, MeterSession};

const LOOP: i64 = 96_000;
const BLOCK: usize = 480;

/// A pass of the loop: one capture epoch, a level per pass.
fn feed(session: &mut MeterSession, from: i64, to: i64, epoch: u64, level: f64) {
    let mut position = from;
    while position < to {
        let frames = BLOCK.min((to - position) as usize);
        let samples: Vec<_> = (position..position + frames as i64)
            .map(|n| level * (n as f64 * std::f64::consts::TAU * 997.0 / 48000.0).sin())
            .collect();
        assert!(session.push_active_at(
            &samples,
            MeterClockStart {
                position_samples: Some(position),
                epoch: Some(epoch),
                source: CaptureClockSource::ProjectTimeline,
                ..Default::default()
            }
        ));
        position += frames as i64;
    }
}

fn join(pre: &MeterSession, post: &MeterSession, state: &mut TimeComparisonState) {
    let points = pre.time_raw_tail(METER_HISTORY_EXCHANGE_POINTS);
    let publication = TimePublication {
        span: pre.time_source_span(),
        points: points.iter().map(|p| p.wire).collect(),
    };
    state.ingest(
        Some(&publication),
        &post.time_raw_tail(64),
        &mut MeterHistory::new(),
    );
}

/// Every joined point compares the same pass: identical audio on both sides gives zero.
fn assert_same_pass(state: &TimeComparisonState, when: &str) {
    if let Some(point) = state.point.as_ref() {
        for value in point.wire.values.iter().take(3).flatten() {
            assert!(value.abs() < 1e-6, "{when}: joined two passes, Δ {value}");
        }
    }
}

#[test]
fn a_lagging_side_never_pairs_its_previous_pass_with_the_new_one() {
    for lagging_pre in [true, false] {
        let mut pre = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 1).unwrap();
        let mut post = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 2).unwrap();
        let mut state = TimeComparisonState::default();
        let (lag, ahead) = if lagging_pre {
            (&mut pre, &mut post)
        } else {
            (&mut post, &mut pre)
        };
        // Pass 1 (quiet) on both, then the side ahead wraps into pass 2 (loud) while the lagging
        // side is still measuring the last third of pass 1.
        feed(ahead, 0, LOOP, 1, 0.1);
        feed(lag, 0, 2 * LOOP / 3, 1, 0.1);
        let (pre_ref, post_ref) = if lagging_pre {
            (&*lag, &*ahead)
        } else {
            (&*ahead, &*lag)
        };
        join(pre_ref, post_ref, &mut state);
        assert_same_pass(&state, "before the wrap");
        feed(ahead, 0, LOOP, 2, 0.5);
        let mut joined_late_pass_one = false;
        let mut position = 2 * LOOP / 3;
        while position < LOOP {
            feed(lag, position, position + 4_800, 1, 0.1);
            position += 4_800;
            let (pre_ref, post_ref) = if lagging_pre {
                (&*lag, &*ahead)
            } else {
                (&*ahead, &*lag)
            };
            join(pre_ref, post_ref, &mut state);
            assert_same_pass(&state, "the lagging side finishes pass 1");
            joined_late_pass_one |= state.point.is_some();
        }
        assert!(
            !joined_late_pass_one,
            "no comparison until both sides are in pass 2"
        );
        // The lagging side wraps and catches up; then both play the next pass together. The
        // comparison resumes on the new pass and joins only the same pass.
        feed(lag, 0, LOOP, 2, 0.5);
        let mut resumed = false;
        let mut position = 0;
        while position < LOOP / 4 {
            feed(&mut pre, position, position + 4_800, 3, 0.3);
            feed(&mut post, position, position + 4_800, 3, 0.3);
            position += 4_800;
            join(&pre, &post, &mut state);
            assert_same_pass(&state, "both on the next pass");
            resumed |= state.point.is_some();
        }
        assert!(resumed, "the comparison resumes once both sides moved on");
    }
}

#[test]
fn a_side_two_passes_behind_waits_until_it_reaches_the_same_pass() {
    let mut pre = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 1).unwrap();
    let mut post = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 2).unwrap();
    let mut state = TimeComparisonState::default();
    feed(&mut post, 0, LOOP, 1, 0.1);
    feed(&mut pre, 0, LOOP / 2, 1, 0.1);
    join(&pre, &post, &mut state);
    assert_same_pass(&state, "before the wraps");
    // POST plays two more passes while PRE is still in the first one.
    feed(&mut post, 0, LOOP, 2, 0.5);
    feed(&mut post, 0, LOOP, 3, 0.3);
    for (from, to, epoch, level) in [(LOOP / 2, LOOP, 1, 0.1), (0, LOOP, 2, 0.5)] {
        let mut position = from;
        while position < to {
            feed(&mut pre, position, position + 4_800, epoch, level);
            position += 4_800;
            join(&pre, &post, &mut state);
            assert_same_pass(&state, "PRE catches up through older passes");
            assert!(
                state.point.is_none(),
                "no comparison before PRE reaches pass 3"
            );
        }
    }
    // PRE reaches pass 3: the passes match again, then both play on together.
    feed(&mut pre, 0, LOOP, 3, 0.3);
    join(&pre, &post, &mut state);
    assert_same_pass(&state, "both in pass 3");
    let mut resumed = state.point.is_some();
    let mut position = 0;
    while position < LOOP / 4 {
        feed(&mut pre, position, position + 4_800, 4, 0.2);
        feed(&mut post, position, position + 4_800, 4, 0.2);
        position += 4_800;
        join(&pre, &post, &mut state);
        assert_same_pass(&state, "both on the next pass");
        resumed |= state.point.is_some();
    }
    assert!(resumed, "the comparison resumes once PRE caught up");
}

#[test]
fn a_side_that_skipped_audio_across_a_wrap_is_not_held_forever() {
    // POST pauses mid pass 1 (a bypass, a silent bus) and resumes in pass 2 at the position it
    // left: it never measured the wrap, so its total move differs from PRE's by the audio it
    // skipped. Both now measure the same pass; once POST has measured more than that difference
    // without catching up, the comparison returns instead of waiting for a transport stop.
    let mut pre = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 1).unwrap();
    let mut post = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 2).unwrap();
    let mut state = TimeComparisonState::default();
    feed(&mut pre, 0, LOOP / 2, 1, 0.1);
    feed(&mut post, 0, LOOP / 2, 1, 0.1);
    join(&pre, &post, &mut state);
    assert!(state.point.is_some(), "pass 1 compares");
    post.pause();
    feed(&mut pre, LOOP / 2, LOOP, 1, 0.1);
    feed(&mut pre, 0, LOOP / 2, 2, 0.5);
    join(&pre, &post, &mut state);
    let mut resumed_at = None;
    let mut position = LOOP / 2;
    while position < LOOP {
        feed(&mut pre, position, position + 4_800, 2, 0.5);
        feed(&mut post, position, position + 4_800, 2, 0.5);
        position += 4_800;
        join(&pre, &post, &mut state);
        assert_same_pass(&state, "the same pass after POST resumed");
        if state.point.is_some() && resumed_at.is_none() {
            resumed_at = Some(position);
        }
    }
    let mut position = 0;
    while resumed_at.is_none() && position < LOOP {
        feed(&mut pre, position, position + 4_800, 3, 0.3);
        feed(&mut post, position, position + 4_800, 3, 0.3);
        position += 4_800;
        join(&pre, &post, &mut state);
        assert_same_pass(&state, "the next pass together");
        resumed_at = state.point.is_some().then_some(position);
    }
    assert!(
        resumed_at.is_some(),
        "the hold ends once POST measured past the difference"
    );
}
