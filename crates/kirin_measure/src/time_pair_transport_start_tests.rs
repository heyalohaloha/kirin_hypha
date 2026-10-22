//! Real measurement windows with transport starts carried through capture plans.
use super::*;
use crate::{channel_layout::ChannelLayout, MeterSession, RecordTakeTracker};

struct Side {
    session: MeterSession,
    tracker: RecordTakeTracker,
}
impl Side {
    fn new(epoch: u64) -> Self {
        Self {
            session: MeterSession::new_in_epoch(48_000, ChannelLayout::mono(), epoch).unwrap(),
            tracker: RecordTakeTracker::new(),
        }
    }
    fn start(&self, position: i64) {
        self.tracker.note_transport_start(position);
    }
    fn cell(&mut self, position: i64, noise: bool) {
        let before = self.tracker.captured_frames_total();
        self.tracker.note_capture_window_with_clocks_boundary(
            true,
            position,
            4_800,
            CaptureClockSource::ProjectTimeline,
            crate::PresentationLatencySamples::default(),
            crate::AuxiliaryClockSamples::default(),
            false,
        );
        let captured = self
            .tracker
            .clock_point_for_captured_frame(before + 1)
            .unwrap();
        let clock = crate::MeterClockStart {
            playback_start: self.tracker.transport_start_before(before),
            position_samples: Some(captured.position_samples - 1),
            epoch: Some(captured.epoch),
            source: captured.source,
            presentation_latency: captured.presentation_latency,
            ..Default::default()
        };
        let samples: Vec<_> = (position..position + 4_800)
            .map(|n| {
                if noise {
                    if n % 2 == 0 {
                        1e-6
                    } else {
                        -1e-6
                    }
                } else {
                    0.2 * (n as f64 * std::f64::consts::TAU * 997.0 / 48_000.0).sin()
                }
            })
            .collect();
        assert!(self.session.push_active_at(&samples, clock));
    }
}
fn join(pre: &Side, post: &Side, state: &mut TimeComparisonState) {
    let publication = TimePublication {
        span: pre.session.time_source_span(),
        points: pre
            .session
            .time_raw_tail(32)
            .into_iter()
            .map(|p| p.wire)
            .collect(),
    };
    state.ingest(
        Some(&publication),
        &post.session.time_raw_tail(64),
        &mut MeterHistory::new(),
    );
}
fn late_pair() -> (Side, Side, TimeComparisonState) {
    let mut pre = Side::new(1);
    let mut post = Side::new(2);
    let mut state = TimeComparisonState::default();
    pre.start(0);
    for cell in 0..50 {
        pre.cell(cell * 4_800, false);
    }
    for cell in 50..60 {
        pre.cell(cell * 4_800, false);
        post.cell(cell * 4_800, false);
        join(&pre, &post, &mut state);
        assert!(
            state.point.is_none(),
            "late insertion alone grants no common clock"
        );
    }
    (pre, post, state)
}
#[test]
fn late_post_compares_from_the_next_common_transport_start() {
    let (mut pre, mut post, mut state) = late_pair();
    pre.session.pause();
    post.session.pause();
    state.fail(TimeComparisonReason::Stopped);
    pre.start(0);
    post.start(0);
    for cell in 0..31 {
        pre.cell(cell * 4_800, false);
        post.cell(cell * 4_800, false);
        join(&pre, &post, &mut state);
        if cell == 4 {
            assert!(state
                .point
                .as_ref()
                .and_then(|p| p.wire.values[0])
                .is_some_and(|v| v.abs() < 1e-6));
        }
    }
    assert!(state
        .point
        .as_ref()
        .and_then(|p| p.wire.values[1])
        .is_some_and(|v| v.abs() < 1e-6));
    assert_ne!(
        pre.session.time_raw_tail(1)[0].wire.clock_origin,
        post.session.time_raw_tail(1)[0].wire.clock_origin
    );
}
#[test]
fn silent_head_with_noise_on_post_shares_the_transport_start() {
    let mut pre = Side::new(1);
    let mut post = Side::new(2);
    let mut state = TimeComparisonState::default();
    pre.start(0);
    post.start(0);
    for cell in 0..10 {
        post.cell(cell * 4_800, true);
    }
    for cell in 10..50 {
        pre.cell(cell * 4_800, false);
        post.cell(cell * 4_800, false);
        join(&pre, &post, &mut state);
    }
    let point = state
        .point
        .expect("common transport start proves the clock despite different signal onsets");
    assert!(point.wire.values[0].is_some_and(|m| m.abs() < 1e-3));
}
#[test]
fn different_transport_starts_without_latency_are_not_compared() {
    let mut pre = Side::new(1);
    let mut post = Side::new(2);
    pre.start(0);
    post.start(4_800);
    pre.cell(0, false);
    let mut state = TimeComparisonState::default();
    for cell in 1..40 {
        pre.cell(cell * 4_800, false);
        post.cell(cell * 4_800, false);
        join(&pre, &post, &mut state);
        assert!(state.point.is_none());
    }
}
#[test]
fn a_loop_wrap_is_a_common_transport_start() {
    let (mut pre, mut post, mut state) = late_pair();
    pre.start(0);
    post.start(0);
    for cell in 0..12 {
        pre.cell(cell * 4_800, false);
        post.cell(cell * 4_800, false);
        join(&pre, &post, &mut state);
    }
    assert!(state
        .point
        .as_ref()
        .and_then(|p| p.wire.values[0])
        .is_some_and(|v| v.abs() < 1e-6));
}
#[test]
fn a_paused_pre_retires_its_publication_before_rejoining_the_live_post() {
    let mut pre = Side::new(1);
    let mut post = Side::new(2);
    pre.start(0);
    post.start(0);
    let mut state = TimeComparisonState::default();
    for cell in 0..40 {
        pre.cell(cell * 4_800, false);
        post.cell(cell * 4_800, false);
        join(&pre, &post, &mut state);
    }
    assert!(state.point.is_some());
    pre.session.pause();
    assert!(pre.session.time_raw_tail(32).is_empty());
    join(&pre, &post, &mut state);
    assert!(
        state.point.is_none(),
        "the empty publication retires the old exact point"
    );
    for cell in 40..50 {
        post.cell(cell * 4_800, false);
    }
    for cell in 50..90 {
        pre.cell(cell * 4_800, false);
        post.cell(cell * 4_800, false);
        join(&pre, &post, &mut state);
    }
    assert!(state
        .point
        .as_ref()
        .and_then(|p| p.wire.values[0])
        .is_some_and(|v| v.abs() < 1e-6));
}

#[test]
fn a_peer_without_the_optional_start_field_keeps_the_older_clock_gate() {
    let mut pre = Side::new(1);
    let mut post = Side::new(2);
    pre.start(0);
    post.start(0);
    pre.cell(0, false);
    post.cell(4_800, false);
    let pre = pre.session.time_raw_tail(1)[0].wire;
    let post = post.session.time_raw_tail(1)[0].wire;
    assert!(pre.comparison_clock_proven(post));
    let mut old = serde_json::to_value(post).unwrap();
    old.as_object_mut().unwrap().remove("playback_start");
    let old: TimeWirePoint = serde_json::from_value(old).unwrap();
    assert_eq!(old.playback_start, None);
    assert!(!pre.comparison_clock_proven(old));
}
