use super::*;
use crate::channel_layout::ChannelLayout;
use crate::{MeterClockStart, MeterSession, PresentationLatencySamples, PresentationLatencySource};

fn push(session: &mut MeterSession, start: i64, gain: f64) {
    let samples: Vec<_> = (start..start + 4800)
        .map(|n| gain * (n as f64 * std::f64::consts::TAU * 997.0 / 48000.0).sin())
        .collect();
    assert!(session.push_active_at(
        &samples,
        MeterClockStart {
            position_samples: Some(start),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            presentation_latency: PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(0)
            },
            ..Default::default()
        }
    ));
}

#[test]
fn resumed_pair_cannot_compare_the_old_window_with_a_new_instances_window() {
    let mut pre = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 1).unwrap();
    let mut post = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 2).unwrap();
    for slot in 0..30 {
        push(&mut post, slot * 4800, 0.02);
    }
    post.pause(); // Session summary and absolute readings intentionally survive.
    let mut state = TimeComparisonState::default();
    let mut history = MeterHistory::new();
    for slot in 0..32 {
        push(&mut pre, 192000 + slot * 4800, 0.2);
        push(&mut post, 192000 + slot * 4800, 0.2);
        let remote = pre.time_raw_tail(64);
        let local = post.time_raw_tail(64);
        let publication = TimePublication {
            span: remote[0].wire.span,
            points: remote.iter().map(|p| p.wire).collect(),
        };
        state.ingest(Some(&publication), &local, &mut history);
        assert_eq!(
            local.last().unwrap().wire.continuous_frames,
            (slot as u64 + 1) * 4800
        );
        if slot < 3 {
            assert!(state.point.is_none());
        }
        if let Some(point) = &state.point {
            if slot < 29 {
                assert!(point.wire.values[3].is_none());
            } else {
                assert!(point.wire.values[3].unwrap().abs() < 0.001);
            }
            assert!(point.wire.values[0].unwrap().abs() < 0.001);
            assert!(point.wire.crest.unwrap().abs() < 0.001);
        }
    }
    assert_eq!(post.snapshot().active_frames, 62 * 4800);
}

#[test]
fn missing_window_provenance_and_unknown_latency_never_grant_scalar_delta() {
    let mut session = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 1).unwrap();
    for slot in 0..32 {
        push(&mut session, slot * 4800, 0.2);
    }
    let point = session.time_raw_tail(1)[0].wire;
    for metric in [0, 1, 2, 3, 5] {
        assert!(point.window_proven(metric));
        assert!(!crate::meter_session::TimeWirePoint {
            latency_known: false,
            ..point
        }
        .window_proven(metric));
        assert!(!crate::meter_session::TimeWirePoint {
            continuous_frames: 0,
            ..point
        }
        .window_proven(metric));
    }
}
