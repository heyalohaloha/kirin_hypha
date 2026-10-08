use super::*;
use crate::{CaptureClockSource, MeterClockStart};

fn clock(offset: i64) -> MeterClockStart {
    MeterClockStart {
        position_samples: Some(offset),
        epoch: Some(1),
        source: CaptureClockSource::ProjectTimeline,
        ..Default::default()
    }
}

fn audio(frames: usize, start: usize, gain: f64) -> Vec<f64> {
    (0..frames)
        .flat_map(|frame| {
            let value =
                gain * (std::f64::consts::TAU * 997.0 * (start + frame) as f64 / 48_000.0).sin();
            [value, value * 0.71]
        })
        .collect()
}

fn near(actual: Option<f64>, expected: Option<f64>) {
    match (actual, expected) {
        (Some(actual), Some(expected)) => {
            assert!((actual - expected).abs() < 1e-9, "{actual} != {expected}")
        }
        (None, None) => (),
        _ => panic!("{actual:?} != {expected:?}"),
    }
}

#[test]
fn session_stop_tail_keeps_peak_and_all_processed_summary_without_rewriting_time() {
    let mut session = MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 73).unwrap();
    let mut oracle = MeasureEngine::new(48_000, ChannelLayout::stereo()).unwrap();
    let prefix = audio(12 * 48_000, 0, 0.01);
    assert!(session.push_active_at(&prefix, clock(0)));
    oracle.push(&prefix);
    let completed = session.time_raw_tail(1)[0].clone();
    let before = session.snapshot();
    // Peak only in the processed 90 ms tail, with no next complete TIME/legacy current point.
    let tail = audio(4_320, 12 * 48_000, 0.8);
    assert!(session.push_active_at(&tail, clock(12 * 48_000)));
    oracle.push(&tail);
    assert_eq!(
        session.time_raw_tail(1)[0].wire.values,
        completed.wire.values
    );
    assert_eq!(session.time_raw_tail(1)[0].completed, completed.completed);
    session.pause();
    let stopped = session.snapshot();
    let expected = oracle.finalize();
    assert_eq!(stopped.observed_frames, before.observed_frames);
    assert_eq!(stopped.active_frames, before.active_frames + 4_320);
    assert_eq!(stopped.current.true_peak, before.current.true_peak);
    assert!(stopped.summary.max_true_peak.unwrap() > before.summary.max_true_peak.unwrap() + 30.0);
    near(stopped.summary.lufs_i, expected.lufs_i);
    near(stopped.summary.lra, expected.lra);
    near(stopped.summary.max_true_peak, expected.max_true_peak);
    near(stopped.maximum.true_peak, expected.max_true_peak);
    near(stopped.max_lufs_m, before.max_lufs_m);
    near(stopped.maximum.lufs_m, before.maximum.lufs_m);
    assert!(oracle.max_lufs_m().unwrap() > stopped.max_lufs_m.unwrap());
    near(
        stopped.plr,
        expected
            .max_true_peak
            .zip(expected.lufs_i)
            .map(|(p, i)| p - i),
    );
    assert!(
        session.time_raw_tail(1).is_empty(),
        "Stop retires V2 current facts"
    );
    let resumed = audio(480, 12 * 48_000 + 4_320, 0.01);
    session.push_active_at(&resumed, clock(12 * 48_000 + 4_320));
    oracle.push(&resumed);
    near(
        session.snapshot().summary.max_true_peak,
        oracle.finalize().max_true_peak,
    );
    session.reset();
    assert!(session.snapshot().summary.max_true_peak.is_none());
}

#[test]
fn session_i_lra_and_peak_match_scalar_engine_for_small_pushes_and_moving_gates() {
    let mut session = MeterSession::new(48_000, ChannelLayout::stereo()).unwrap();
    let mut oracle = MeasureEngine::new(48_000, ChannelLayout::stereo()).unwrap();
    for slot in 0..321 {
        let gain = [0.0001, 0.003, 0.2, 0.7, 0.0][slot / 13 % 5];
        let samples = audio(4_800, slot * 4_800, gain);
        for chunk in samples.chunks(64) {
            assert!(session.push_active(chunk));
            oracle.push(chunk);
        }
        let expected = oracle.finalize();
        let actual = session.snapshot();
        near(actual.summary.lufs_i, expected.lufs_i);
        near(actual.summary.lra, expected.lra);
        near(actual.summary.max_true_peak, expected.max_true_peak);
    }
    let before = session.snapshot();
    assert!(!session.push_active(&[f64::NAN; 64]));
    assert!(!session.push_active(&[0.1; 63]));
    assert_eq!(session.snapshot().summary.lufs_i, before.summary.lufs_i);
    assert_eq!(session.snapshot().active_frames, before.active_frames);
}
