use kirin_measure::{SuperFluxAnalyzer, SuperFluxConfig, TransientOdfFrame, TransientOdfKind};
use serde::Serialize;

use super::{analyze_frames, pick_peaks, score_track, LoadedTrack};
use crate::contract::{FormalAnalyzer, PeakRule};
use crate::metrics::{
    gates, macro_metrics, metrics, timing, Counts, EvaluationReport, FormalEvaluationReport,
    TrackEvaluation,
};

#[allow(dead_code)] // Source pin and context-guarded scoring intentionally block this entrypoint.
pub(crate) fn evaluate_formal_tracks(
    _tracks: &[LoadedTrack],
    _analyzer: FormalAnalyzer,
    _rule: PeakRule,
) -> Result<FormalEvaluationReport, String> {
    Err("formal evaluation blocked: not_ready_context_guard_unimplemented".to_string())
}

#[derive(Clone, Debug, Serialize)]
#[allow(dead_code)] // Used by the separate development-pilot example.
pub(crate) struct DevelopmentPilotFold {
    pub(crate) fold: u8,
    pub(crate) evaluation: EvaluationReport,
}

#[derive(Clone, Debug, Serialize)]
#[allow(dead_code)] // Used by the separate development-pilot example.
pub(crate) struct DevelopmentPilotEvaluation {
    pub(crate) aggregation_contract: &'static str,
    pub(crate) pooled: EvaluationReport,
    pub(crate) folds: [DevelopmentPilotFold; 5],
    pub(crate) all_pooled_micro_gates_passed: bool,
    pub(crate) all_fold_micro_gates_passed: bool,
}

#[derive(Clone)]
#[allow(dead_code)] // Used by the separate development-pilot example.
struct ScoredTrack {
    fold: u8,
    track: TrackEvaluation,
    signed_timing_secs: Vec<f64>,
}

/// Scores the frozen development data without authorizing a winner.
/// Full source audio is analyzed on the source-sample-zero frame grid, while
/// predictions and labels are counted only inside each manifest core.
#[allow(dead_code)] // Used by the separate development-pilot example.
pub(crate) fn evaluate_development_pilot(
    tracks: &[LoadedTrack],
    analyzer: FormalAnalyzer,
    rule: PeakRule,
) -> Result<DevelopmentPilotEvaluation, String> {
    if tracks.len() != 290 {
        return Err("development pilot requires exactly 290 tracks".to_string());
    }
    let mut scored = Vec::with_capacity(tracks.len());
    for track in tracks {
        let formal = track
            .selection
            .formal
            .as_ref()
            .ok_or("development pilot received a row without core metadata")?;
        let frames = match analyzer {
            FormalAnalyzer::Mel32V2 => analyze_frames(
                &track.wav.samples,
                track.wav.metadata.sample_rate,
                TransientOdfKind::Mel32,
            )?,
            FormalAnalyzer::FixedSuperflux(config) => analyze_superflux_frames(
                &track.wav.samples,
                track.wav.metadata.sample_rate,
                config,
            )?,
        };
        let core_start = i64::try_from(formal.excerpt_start_sample_44100)
            .map_err(|_| "pilot core start does not fit i64")?;
        let core_end = i64::try_from(formal.excerpt_end_sample_44100)
            .map_err(|_| "pilot core end does not fit i64")?;
        let predictions = pick_peaks(&frames, track.wav.metadata.sample_rate, rule)
            .into_iter()
            .filter(|sample| core_start <= *sample && *sample < core_end)
            .collect::<Vec<_>>();
        let duration_seconds = (formal.excerpt_end_sample_44100 - formal.excerpt_start_sample_44100)
            as f64
            / f64::from(track.wav.metadata.sample_rate);
        let (counts, signed_timing_secs) = score_track(
            &predictions,
            &track.labels.events,
            track.wav.metadata.sample_rate,
            duration_seconds,
        )?;
        scored.push(ScoredTrack {
            fold: formal.fold,
            track: TrackEvaluation {
                performance_id: track.selection.id.clone(),
                kit_name: track.selection.kit_name.clone(),
                source_split: track.selection.split.clone(),
                metrics: metrics(&counts),
                timing: timing(&signed_timing_secs),
                counts,
            },
            signed_timing_secs,
        });
    }
    let pooled = build_report(scored.iter());
    let folds: [DevelopmentPilotFold; 5] = (0_u8..5)
        .map(|fold| {
            let members = scored
                .iter()
                .filter(|scored| scored.fold == fold)
                .collect::<Vec<_>>();
            if members.len() != 58 {
                return Err(format!(
                    "development pilot fold {fold} has {} tracks, expected 58",
                    members.len()
                ));
            }
            Ok(DevelopmentPilotFold {
                fold,
                evaluation: build_report(members.into_iter()),
            })
        })
        .collect::<Result<Vec<_>, String>>()?
        .try_into()
        .map_err(|_| "development pilot fold count invariant failed".to_string())?;
    Ok(DevelopmentPilotEvaluation {
        aggregation_contract:
            "development_measurement_only;full_source_grid;half_open_core_score;pooled_plus_each_fold;not_winner_eligible",
        all_pooled_micro_gates_passed: pooled.all_gates_passed,
        all_fold_micro_gates_passed: folds
            .iter()
            .all(|fold| fold.evaluation.all_gates_passed),
        pooled,
        folds,
    })
}

#[allow(dead_code)] // Used by the separate development-pilot example.
fn build_report<'a>(tracks: impl Iterator<Item = &'a ScoredTrack>) -> EvaluationReport {
    let tracks = tracks.collect::<Vec<_>>();
    let mut counts = Counts::default();
    let mut signed_timing_secs = Vec::new();
    for scored in &tracks {
        counts.add(&scored.track.counts);
        signed_timing_secs.extend_from_slice(&scored.signed_timing_secs);
    }
    let micro = metrics(&counts);
    let timing = timing(&signed_timing_secs);
    let gates = gates(&micro, &timing);
    let track_reports = tracks
        .into_iter()
        .map(|scored| scored.track.clone())
        .collect::<Vec<_>>();
    EvaluationReport {
        counts,
        micro,
        macro_values: macro_metrics(&track_reports),
        timing,
        all_gates_passed: gates.iter().all(|gate| gate.passed),
        gates,
        tracks: track_reports,
    }
}

#[allow(dead_code)] // Algorithm fixture; formal scoring remains context-guard blocked.
pub(super) fn analyze_superflux_frames(
    samples: &[f32],
    sample_rate: u32,
    config: SuperFluxConfig,
) -> Result<Vec<TransientOdfFrame>, String> {
    let mut analyzer = SuperFluxAnalyzer::new(sample_rate, config).map_err(str::to_string)?;
    let window = analyzer.layout().window_samples;
    let hop = analyzer.layout().hop_samples;
    let lag = analyzer.layout().spectral_lag_frames;
    let mut buffer = vec![0.0_f32; window];
    for warmup in (1..=lag).rev() {
        let center = -((warmup * hop) as i64);
        let support_start = center - (window / 2) as i64;
        analyzer
            .analyze_window(&buffer, None, support_start)
            .map_err(str::to_string)?;
    }
    if samples.is_empty() {
        return Ok(Vec::new());
    }
    let flush_end = samples
        .len()
        .checked_add(window / 2)
        .ok_or("SuperFlux analysis support end overflow")?;
    let mut frames = Vec::with_capacity(flush_end.div_ceil(hop));
    for center in (0..flush_end).step_by(hop) {
        buffer.fill(0.0);
        let support_start = center as i64 - (window / 2) as i64;
        for (window_index, value) in buffer.iter_mut().enumerate() {
            let sample_index = support_start + window_index as i64;
            if let Ok(sample_index) = usize::try_from(sample_index) {
                if let Some(sample) = samples.get(sample_index) {
                    *value = *sample;
                }
            }
        }
        if let Some(frame) = analyzer
            .analyze_window(&buffer, None, support_start)
            .map_err(str::to_string)?
        {
            frames.push(TransientOdfFrame {
                support_start_samples: frame.support_start_samples,
                support_end_samples: frame.support_end_samples,
                event_sample: frame.event_sample,
                mel_flux: 0.0,
                complex_odf: 0.0,
                value: frame.value,
            });
        }
    }
    Ok(frames)
}

#[cfg(test)]
#[path = "formal_evaluation_tests.rs"]
mod tests;
