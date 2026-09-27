//! Engine cadence, floor, and True Peak regression tests.

use super::*;

const SR: u32 = 48_000;

fn sine_100ms(amp: f64) -> Vec<f64> {
    let frames = (SR / 10) as usize;
    let mut samples = Vec::with_capacity(frames * 2);
    for frame in 0..frames {
        let time = frame as f64 / SR as f64;
        let sample = amp * (2.0 * std::f64::consts::PI * 1000.0 * time).sin();
        samples.push(sample);
        samples.push(sample);
    }
    samples
}

fn silence_100ms() -> Vec<f64> {
    vec![0.0; (SR / 10) as usize * 2]
}

#[test]
fn push_observed_reports_input_samples_per_100ms_chunk() {
    let mut engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let mut mixed = silence_100ms();
    mixed.extend(sine_100ms(0.25));

    let mut observed = Vec::new();
    let result = engine.push_observed(&mixed, |frames, _, observed_samples| {
        observed.push((
            frames,
            observed_samples.len(),
            observed_samples.iter().all(|sample| *sample == 0.0),
            observed_samples.iter().any(|sample| sample.abs() > 0.01),
        ));
    });

    assert!(result.is_some());
    assert_eq!(observed.len(), 2);
    assert_eq!(observed[0], (4_800, 9_600, true, false));
    assert_eq!(observed[1], (9_600, 9_600, false, true));
}

#[test]
fn ten_ms_analysis_does_not_advance_the_public_clock_before_100ms() {
    let mut engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let samples_150ms = vec![0.0; (SR as usize * 15 / 100) * 2];
    let samples_50ms = vec![0.0; (SR as usize * 5 / 100) * 2];

    assert!(engine.push(&samples_150ms).is_some());
    assert_eq!(engine.total_frames(), 4_800);
    assert_eq!(engine.pending_frames(), 2_400);

    assert!(engine.push(&samples_50ms).is_some());
    assert_eq!(engine.total_frames(), 9_600);
    assert_eq!(engine.pending_frames(), 0);
}

#[test]
fn non_divisible_sample_rate_keeps_ten_analysis_phases_on_one_public_boundary() {
    const ODD_SR: u32 = 44_105;
    let publish_frames = ((ODD_SR as usize) + 5) / 10;
    let mut engine = MeasureEngine::new(ODD_SR, ChannelLayout::stereo()).unwrap();
    let samples = vec![0.0; publish_frames * 2];
    let mut observed = Vec::new();

    assert!(engine
        .push_observed(&samples, |frames, _, pcm| {
            observed.push((frames, pcm.len()));
        })
        .is_some());
    assert_eq!(observed, vec![(publish_frames as u64, samples.len())]);
    assert_eq!(engine.total_frames(), publish_frames as u64);
    assert_eq!(engine.pending_frames(), 0);
}

#[test]
fn sample_rate_too_low_for_ten_ms_cadence_is_rejected() {
    assert!(MeasureEngine::new(94, ChannelLayout::stereo())
        .err()
        .expect("94Hz must be rejected")
        .contains("too low for 10ms analysis cadence"));
    assert!(MeasureEngine::new(95, ChannelLayout::stereo()).is_ok());
}

#[test]
fn subsilence_floor_collapses_to_none() {
    let drive = |amp: f64| -> MeasureResult {
        let mut engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
        engine.reset();
        let mut last = None;
        for _ in 0..6 {
            if let Some(result) = engine.push(&sine_100ms(amp)) {
                last = Some(result);
            }
        }
        last.expect("result after 600ms")
    };

    let normal = drive(0.1);
    assert!(normal
        .lufs_m
        .is_some_and(|value| value > LUFS_VALID_FLOOR_LUFS));
    assert!(normal.true_peak.is_some());
    assert!(normal.crest.is_some());

    let tiny = drive(1e-6);
    assert!(tiny.lufs_m.is_none());
    assert!(tiny.true_peak.is_none());
    assert!(tiny.tp_session_max.is_none());
    assert!(tiny.crest.is_none());
    assert!(tiny.psr.is_none());
}

#[test]
fn short_term_window_remains_independent_when_momentary_tail_is_floored() {
    let mut engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let mut last = MeasureResult::default();
    for _ in 0..30 {
        last = engine.push(&sine_100ms(0.1)).expect("100ms result");
    }
    assert!(last.lufs_s.is_some());

    for _ in 0..5 {
        last = engine.push(&sine_100ms(1e-6)).expect("100ms result");
    }
    assert!(last.lufs_m.is_none());
    assert!(last.lufs_s.is_some());
    assert!(last.psr.is_none());
}

#[test]
fn tp_recent_expires_in_400ms_while_session_holds() {
    let amplitude: f64 = 0.5;
    let expected = 20.0 * amplitude.log10();
    let mut engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    engine.reset();

    let mut chunks = vec![sine_100ms(amplitude)];
    for _ in 0..5 {
        chunks.push(silence_100ms());
    }
    let mut recent = Vec::new();
    let mut session = Vec::new();
    for chunk in &chunks {
        if let Some(result) = engine.push(chunk) {
            recent.push(result.true_peak);
            session.push(result.tp_session_max);
        }
    }
    assert_eq!(recent.len(), 6);
    for (index, value) in recent.iter().take(4).enumerate() {
        let value = value.unwrap_or_else(|| panic!("recent[{index}] missing"));
        assert!((value - expected).abs() < 0.5);
    }
    assert!(recent[4].is_none_or(|value| value < expected - 2.0));
    assert!(recent[5].is_none());
    for (index, value) in session.iter().enumerate() {
        let value = value.unwrap_or_else(|| panic!("session[{index}] missing"));
        assert!((value - expected).abs() < 0.5);
    }
}

#[test]
fn tp_recent_independent_of_push_block_size() {
    let mut signal = sine_100ms(0.5);
    for _ in 0..5 {
        signal.extend(silence_100ms());
    }
    let drive = |block_frames: usize| -> Vec<Option<f64>> {
        let mut engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
        engine.reset();
        let mut output = Vec::new();
        let block_samples = block_frames * 2;
        let mut start = 0;
        while start < signal.len() {
            let end = (start + block_samples).min(signal.len());
            if let Some(result) = engine.push(&signal[start..end]) {
                output.push(result.true_peak);
            }
            start = end;
        }
        output
    };

    let chunks_100ms = drive(4_800);
    let chunks_25ms = drive(1_200);
    assert_eq!(chunks_100ms.len(), chunks_25ms.len());
    for (index, (left, right)) in chunks_100ms.iter().zip(&chunks_25ms).enumerate() {
        match (left, right) {
            (Some(left), Some(right)) => assert!(
                (left - right).abs() < 1e-9,
                "True Peak differs at observer {index}: {left} vs {right}"
            ),
            (None, None) => {}
            _ => panic!("True Peak presence differs at observer {index}"),
        }
    }
}

#[test]
fn content_grid_splits_do_not_change_canonical_engine_facts() {
    use crate::{
        AuxiliaryClockSamples, AuxiliaryClockSource, CaptureClockSource,
        PresentationLatencySamples, PresentationLatencySource,
    };

    let mut baseline = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let mut gridded = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    gridded.enable_content_grid(SR);
    let mut base_points = Vec::new();
    let mut grid_points = Vec::new();
    let mut content_points = Vec::new();
    let total = SR as usize * 3;
    let mut start = 0_usize;
    while start < total {
        let frames = (total - start).min(528);
        let mut pcm = Vec::with_capacity(frames * 2);
        for frame in start..start + frames {
            let mut sample = (0.1 + (frame / 5_000 % 3) as f64 * 0.07)
                * (frame as f64 * std::f64::consts::TAU / 48.0).sin();
            if matches!(frame, 4_799 | 4_800 | 19_199 | 19_200 | 48_000) {
                sample = 0.91;
            }
            pcm.extend([sample, sample]);
        }
        baseline.push_observed(&pcm, |_, result, _| base_points.push(result.clone()));
        gridded.push_observed_with_session_facts_at(
            &pcm,
            MeterClockStart {
                position_samples: Some(start as i64),
                epoch: Some(1),
                source: CaptureClockSource::ProjectTimeline,
                auxiliary: AuxiliaryClockSamples {
                    source: AuxiliaryClockSource::Vst3Continuous,
                    samples: Some(start as i64 - 4_096),
                },
                presentation_latency: PresentationLatencySamples {
                    source: PresentationLatencySource::Vst3,
                    input: None,
                    output: Some(4_096),
                },
            },
            |_, result, _, _, _| grid_points.push(result.clone()),
            |point| content_points.push(point),
        );
        start += frames;
    }
    assert_eq!(base_points.len(), 30);
    assert_eq!(grid_points.len(), base_points.len());
    assert!(!content_points.is_empty());
    let near = |a: Option<f64>, b: Option<f64>| {
        assert_eq!(a.is_some(), b.is_some());
        if let (Some(a), Some(b)) = (a, b) {
            assert!((a - b).abs() <= 1e-9, "{a} != {b}");
        }
    };
    for (base, grid) in base_points.iter().zip(&grid_points) {
        near(base.lufs_m, grid.lufs_m);
        near(base.lufs_s, grid.lufs_s);
        near(base.true_peak, grid.true_peak);
        near(base.tp_session_max, grid.tp_session_max);
        near(base.crest, grid.crest);
        near(base.psr, grid.psr);
    }
    near(baseline.max_lufs_m(), gridded.max_lufs_m());
    near(baseline.max_lufs_s(), gridded.max_lufs_s());
    near(baseline.finalize().lufs_i, gridded.finalize().lufs_i);
    near(baseline.finalize().lra, gridded.finalize().lra);
}

#[test]
fn add_frames_f64_errors_on_misaligned_frame_count_is_observable() {
    let mut ebu = EbuR128::new(2, 48_000, Mode::M).unwrap();
    assert!(ebu.add_frames_f64(&[0.0_f64; 3]).is_err());
    assert!(ebu.add_frames_f64(&[0.0_f64; 4]).is_ok());
}
