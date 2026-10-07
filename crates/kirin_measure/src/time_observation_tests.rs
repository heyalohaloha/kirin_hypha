use super::*;
use crate::channel_layout::ChannelLayout;
use crate::{CaptureClockSource, MeterClockStart};

fn clock(offset: i64) -> MeterClockStart {
    MeterClockStart {
        position_samples: Some(offset),
        epoch: Some(1),
        source: CaptureClockSource::ProjectTimeline,
        ..Default::default()
    }
}
fn audio(slots: usize) -> Vec<f64> {
    (0..slots * 4800)
        .flat_map(|frame| {
            let gain = if frame < 4800 {
                0.0
            } else {
                [0.02, 0.08, 0.04][frame / 4800 % 3]
            };
            let sample = gain * (frame as f64 * std::f64::consts::TAU * 997.0 / 48000.0).sin();
            [sample, sample * 0.5]
        })
        .collect()
}

#[test]
fn each_boundary_in_one_big_push_has_its_own_values_summary_none_and_completion() {
    let samples = audio(32);
    let mut big = MeterSession::new_in_epoch(48000, ChannelLayout::stereo(), 19).unwrap();
    let mut individual = MeterSession::new_in_epoch(48000, ChannelLayout::stereo(), 20).unwrap();
    assert!(big.push_active_at(&samples, clock(0)));
    let raw = big.time_raw_tail(64);
    assert_eq!(raw.len(), 32);
    for (slot, samples) in samples.chunks(9600).enumerate() {
        assert!(individual.push_active_at(samples, clock(slot as i64 * 4800)));
        let reference = individual.snapshot();
        let expected = [
            reference.current.lufs_m,
            reference.current.lufs_s,
            reference.current.true_peak,
            reference.current.psr,
            reference.plr,
            reference.stereo.correlation,
        ];
        assert_eq!(
            raw[slot].wire.values, expected,
            "100ms slot {slot} must not contain future summary/stereo"
        );
        assert_eq!(raw[slot].wire.observed, (slot as u64 + 1) * 4800);
        assert_eq!(raw[slot].wire.endpoint, Some((slot as i64 + 1) * 4800));
    }
    assert!(raw[0].wire.values[0].is_none());
    assert!(raw[0].wire.values[3].is_none());
    assert!(raw.last().unwrap().wire.values[3].is_some());
    assert!(raw.windows(2).all(|p| p[0].completed < p[1].completed));
    assert_ne!(raw[10].wire.values[0], raw[31].wire.values[0]);
    assert!(raw[31].completed <= std::time::Instant::now());
}

#[test]
fn raw_is_bounded_reset_retires_token_and_partial_push_does_not_complete() {
    let mut session = MeterSession::new_in_epoch(48000, ChannelLayout::stereo(), 21).unwrap();
    let token = session.time_span_token();
    let original = token.load(Ordering::Acquire);
    assert!(session.push_active_at(&audio(70), clock(0)));
    let raw = session.time_raw_tail(1000);
    assert_eq!(raw.len(), 64);
    assert_eq!(raw[0].wire.observed, 7 * 4800);
    let completed = raw[63].completed;
    assert!(session.push_active_at(&[0.1; 20], clock(70 * 4800)));
    assert_eq!(session.time_raw_tail(1)[0].completed, completed);
    session.reset();
    assert_ne!(token.load(Ordering::Acquire), original);
    assert!(session.time_raw_tail(64).is_empty());
    assert!(session
        .time_history(MeterHistoryResolution::Hz10, 0, u64::MAX, 1200)
        .unwrap()
        .is_empty());
}

#[test]
fn worker_retirement_keeps_session_statistics_but_requires_a_new_raw_slot_and_run() {
    let mut session = MeterSession::new_in_epoch(48000, ChannelLayout::stereo(), 22).unwrap();
    session.push_active_at(&audio(32), clock(0));
    let before = session.snapshot();
    let raw = session.time_raw_tail(1)[0].clone();
    let revision = session.history_publication_revision();
    session.retire_time_worker_span();
    let paused = session.snapshot();
    assert_eq!(paused.state, MeterSessionState::Paused);
    assert_eq!(paused.observed_frames, before.observed_frames);
    assert_eq!(paused.active_frames, before.active_frames);
    assert_eq!(paused.generation, before.generation);
    assert_eq!(paused.current.lufs_m, before.current.lufs_m);
    assert_eq!(paused.max_lufs_m, before.max_lufs_m);
    assert_eq!(paused.summary.lufs_i, before.summary.lufs_i);
    assert_eq!(paused.plr, before.plr);
    assert!(session.time_raw_tail(64).is_empty());
    assert_ne!(session.time_source_span().token, raw.wire.span.token);
    assert_ne!(session.history_publication_revision(), revision);
    session.push_active_at(&audio(1), clock(32 * 4800));
    let resumed = session.time_raw_tail(1)[0].clone();
    assert_ne!(resumed.wire.run, raw.wire.run);
    assert!(resumed.completed > raw.completed);
    let rows = session
        .time_history(MeterHistoryResolution::Hz10, 0, u64::MAX, 1200)
        .unwrap();
    assert_ne!(
        rows[rows.len() - 2].segment_id,
        rows.last().unwrap().segment_id
    );
    assert!(!rows.last().unwrap().connects_previous);
}

#[test]
fn non_decile_sample_rate_uses_the_same_rounded_step_as_the_engine() {
    // 11025 / 10 rounds to 1103 frames. Floor 1102 would invent a gap each slot.
    let mut session = MeterSession::new_in_epoch(11025, ChannelLayout::mono(), 23).unwrap();
    for slot in 0..12 {
        let offset = slot * 1103;
        let samples: Vec<_> = (offset..offset + 1103)
            .map(|frame| 0.2 * (frame as f64 * std::f64::consts::TAU * 997.0 / 11025.0).sin())
            .collect();
        assert!(session.push_active_at(&samples, clock(offset as i64)));
    }
    let raw = session.time_raw_tail(64);
    assert_eq!(raw.len(), 12);
    assert_eq!(raw[11].wire.observed, 13236);
    assert_eq!(raw[11].wire.endpoint, Some(13236));
    let exact = session.recent_history(MeterHistoryResolution::Hz10, 12);
    assert_eq!(exact.len(), 12);
    assert!(exact.windows(2).all(|rows| {
        rows[1].last_observed_frames - rows[0].last_observed_frames == 1103
            && (rows[0].valid_count != rows[1].valid_count || rows[1].connects_previous)
    }));
}
