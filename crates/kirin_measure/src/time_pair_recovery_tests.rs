//! Recovery uses native sample coordinates, independent of callback size or missing audio.
use super::*;
use crate::{channel_layout::ChannelLayout, MeterClockStart, MeterSession};

fn feed(session: &mut MeterSession, start: i64, frames: usize) {
    let samples: Vec<_> = (start..start + frames as i64)
        .map(|n| 0.1 * (n as f64 * std::f64::consts::TAU * 997.0 / 48000.0).sin())
        .collect();
    assert!(session.push_active_at(
        &samples,
        MeterClockStart {
            position_samples: Some(start),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            ..Default::default()
        }
    ));
}

fn advance(session: &mut MeterSession, from: i64, to: i64, block: usize) {
    let mut position = from;
    while position < to {
        let frames = block.min((to - position) as usize);
        feed(session, position, frames);
        position += frames as i64;
    }
}

fn join(
    pre: &MeterSession,
    post: &MeterSession,
    state: &mut TimeComparisonState,
    history: &mut MeterHistory,
) {
    let points = pre.time_raw_tail(METER_HISTORY_EXCHANGE_POINTS);
    let publication = TimePublication {
        span: pre.time_source_span(),
        points: points.iter().map(|p| p.wire).collect(),
    };
    state.ingest(Some(&publication), &post.time_raw_tail(64), history);
}

#[test]
fn fractional_one_sided_boundaries_recover_all_current_level_and_time_windows() {
    for block in [512, 441, 528] {
        for side in [false, true] {
            for edge in ["reset", "pause", "gap", "worker", "stopped_reset"] {
                let mut pre = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 1).unwrap();
                let mut post = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 2).unwrap();
                let mut state = TimeComparisonState::default();
                let mut history = MeterHistory::new();
                let cut = (100 * block) as i64;
                advance(&mut pre, 0, cut, block);
                advance(&mut post, 0, cut, 257);
                join(&pre, &post, &mut state, &mut history);
                let affected = if side { &mut pre } else { &mut post };
                match edge {
                    "reset" => affected.reset(),
                    "pause" => affected.pause(),
                    "worker" => affected.retire_time_worker_span(),
                    "stopped_reset" => {
                        affected.pause();
                        affected.reset();
                    }
                    _ => {}
                }
                let resume = cut + 733;
                if side {
                    advance(&mut post, cut, resume, 257);
                } else {
                    advance(&mut pre, cut, resume, block);
                }
                advance(&mut pre, resume, resume + 9600, block);
                advance(&mut post, resume, resume + 9600, 257);
                join(&pre, &post, &mut state, &mut history);
                advance(&mut pre, resume + 9600, resume + 192000, block);
                advance(&mut post, resume + 9600, resume + 192000, 257);
                join(&pre, &post, &mut state, &mut history);
                let point = state.point.as_ref().unwrap_or_else(|| {
                    panic!("no comparison after {edge}, PRE edge={side}, block={block}")
                });
                assert_eq!(point.wire.endpoint.unwrap().rem_euclid(4800), 0);
                for metric in [0, 1, 2, 3] {
                    assert!(
                        point.wire.values[metric].unwrap().abs() < 0.01,
                        "{edge} metric {metric}: {:?}",
                        point.wire.values
                    );
                }
                assert!(point.wire.crest.unwrap().abs() < 0.01);
            }
        }
    }
}

#[test]
fn insertion_negative_position_and_unknown_clock_do_not_invent_a_pair() {
    let mut pre = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 1).unwrap();
    let mut post = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 2).unwrap();
    advance(&mut pre, -4096, 192000, 528);
    advance(&mut post, 577, 192000, 441);
    let mut state = TimeComparisonState::default();
    join(&pre, &post, &mut state, &mut MeterHistory::new());
    assert!(state.point.is_none());
    assert_eq!(state.reason, TimeComparisonReason::Incompatible);
    let mut unknown = MeterSession::new_in_epoch(48000, ChannelLayout::mono(), 3).unwrap();
    assert!(unknown.push_active(&vec![0.1; 192000]));
    join(&pre, &unknown, &mut state, &mut MeterHistory::new());
    assert!(state.point.is_none());
    assert_eq!(
        unknown
            .recent_history(MeterHistoryResolution::Hz10, 64)
            .len(),
        40
    );
}
