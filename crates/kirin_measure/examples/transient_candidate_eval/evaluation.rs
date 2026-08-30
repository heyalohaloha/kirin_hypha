use std::collections::HashSet;
use std::fs;
use std::path::Path;

#[path = "formal_evaluation.rs"]
mod formal_evaluation;

use super::contract::{sha256_bytes, PeakRule};
use super::input::{
    read_midi_labels, read_mono_pcm_wav, LabelEvent, MidiLabels, MonoWav, Selection,
};
use super::matching::{is_within_tolerance, match_events};
use super::metrics::{
    gates, macro_metrics, metrics, timing, Counts, EvaluationReport, TrackEvaluation,
};
use kirin_measure::{TransientCandidateAnalyzer, TransientOdfFrame, TransientOdfKind};

const REQUIRED_SAMPLE_RATE: u32 = 44_100;
const DURATION_TOLERANCE_SECS: f64 = 0.010;
const LABEL_RANGE_TOLERANCE_SECS: f64 = 0.002;
const HAT_NOTES: [u8; 5] = [22, 26, 42, 44, 46];

#[derive(Debug)]
pub(crate) struct LoadedTrack {
    pub(crate) selection: Selection,
    pub(crate) wav: MonoWav,
    pub(crate) labels: MidiLabels,
    pub(crate) midi_relative: String,
    pub(crate) audio_relative: String,
    pub(crate) midi_sha256: String,
    pub(crate) audio_sha256: String,
    pub(crate) midi_size_bytes: u64,
    pub(crate) audio_size_bytes: u64,
    pub(crate) peak_abs: f32,
}

#[derive(Default)]
struct Aggregate {
    counts: Counts,
    signed_timing_secs: Vec<f64>,
}

pub(crate) fn load_track(root: &Path, selection: Selection) -> Result<LoadedTrack, String> {
    if selection.formal.is_some() {
        return Err("formal evaluation blocked: not_ready_context_guard_unimplemented".to_string());
    }
    let audio_bytes = fs::read(&selection.audio).map_err(|error| error.to_string())?;
    let midi_bytes = fs::read(&selection.midi).map_err(|error| error.to_string())?;
    let wav = read_mono_pcm_wav(&selection.audio)?;
    if wav.metadata.sample_rate != REQUIRED_SAMPLE_RATE {
        return Err(format!("{} is not 44.1 kHz", selection.audio.display()));
    }
    if wav.samples.is_empty() || wav.samples.iter().all(|sample| *sample == 0.0) {
        return Err(format!("empty or exact-silent audio: {}", selection.id));
    }
    if (wav.metadata.duration_secs - selection.declared_duration).abs() > DURATION_TOLERANCE_SECS {
        return Err(format!("manifest duration mismatch: {}", selection.id));
    }
    let labels = read_midi_labels(&selection.midi)?;
    if labels.events.is_empty() {
        return Err(format!("no nonzero MIDI note-on: {}", selection.id));
    }
    let first = labels.events.first().unwrap().time_secs;
    let last = labels.events.last().unwrap().time_secs;
    if first < -LABEL_RANGE_TOLERANCE_SECS
        || last > wav.metadata.duration_secs + LABEL_RANGE_TOLERANCE_SECS
    {
        return Err(format!("MIDI label outside audio range: {}", selection.id));
    }
    if sha256_bytes(&fs::read(&selection.audio).map_err(|error| error.to_string())?)
        != sha256_bytes(&audio_bytes)
        || sha256_bytes(&fs::read(&selection.midi).map_err(|error| error.to_string())?)
            != sha256_bytes(&midi_bytes)
    {
        return Err(format!("input changed during preflight: {}", selection.id));
    }
    let midi_relative = relative_utf8(root, &selection.midi)?;
    let audio_relative = relative_utf8(root, &selection.audio)?;
    let peak_abs = wav
        .samples
        .iter()
        .map(|sample| sample.abs())
        .fold(0.0, f32::max);
    Ok(LoadedTrack {
        selection,
        wav,
        labels,
        midi_relative,
        audio_relative,
        midi_sha256: sha256_bytes(&midi_bytes),
        audio_sha256: sha256_bytes(&audio_bytes),
        midi_size_bytes: midi_bytes.len() as u64,
        audio_size_bytes: audio_bytes.len() as u64,
        peak_abs,
    })
}

pub(crate) fn evaluate_tracks(
    tracks: &[LoadedTrack],
    kind: TransientOdfKind,
    rule: PeakRule,
) -> Result<EvaluationReport, String> {
    let mut aggregate = Aggregate::default();
    let mut reports = Vec::with_capacity(tracks.len());
    for track in tracks {
        let frames = analyze_frames(&track.wav.samples, track.wav.metadata.sample_rate, kind)?;
        let predictions = pick_peaks(&frames, track.wav.metadata.sample_rate, rule);
        let (counts, signed_timing_secs) = score_track(
            &predictions,
            &track.labels.events,
            track.wav.metadata.sample_rate,
            track.wav.metadata.duration_secs,
        )?;
        aggregate.counts.add(&counts);
        aggregate.signed_timing_secs.extend(&signed_timing_secs);
        reports.push(TrackEvaluation {
            performance_id: track.selection.id.clone(),
            kit_name: track.selection.kit_name.clone(),
            source_split: track.selection.split.clone(),
            metrics: metrics(&counts),
            timing: timing(&signed_timing_secs),
            counts,
        });
    }
    let micro = metrics(&aggregate.counts);
    let timing = timing(&aggregate.signed_timing_secs);
    let gates = gates(&micro, &timing);
    Ok(EvaluationReport {
        counts: aggregate.counts,
        micro,
        macro_values: macro_metrics(&reports),
        timing,
        all_gates_passed: gates.iter().all(|gate| gate.passed),
        gates,
        tracks: reports,
    })
}

fn analyze_frames(
    samples: &[f32],
    sample_rate: u32,
    kind: TransientOdfKind,
) -> Result<Vec<TransientOdfFrame>, String> {
    let mut analyzer =
        TransientCandidateAnalyzer::new(sample_rate, kind).map_err(str::to_string)?;
    let window = analyzer.layout().window_samples;
    let hop = analyzer.layout().hop_samples;
    let mut buffer = vec![0.0_f32; window];
    let warmup = if matches!(kind, TransientOdfKind::Complex | TransientOdfKind::Hybrid) {
        2
    } else {
        1
    };
    for index in 0..warmup {
        analyzer
            .analyze_window(&buffer, -(((warmup - index) * hop) as i64))
            .map_err(str::to_string)?;
    }
    if samples.is_empty() {
        return Ok(Vec::new());
    }
    let flush_end = samples
        .len()
        .checked_add(window / 2)
        .ok_or("analysis support end overflow")?;
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
            .analyze_window(&buffer, support_start)
            .map_err(str::to_string)?
        {
            frames.push(frame);
        }
    }
    Ok(frames)
}

fn pick_peaks(frames: &[TransientOdfFrame], sample_rate: u32, rule: PeakRule) -> Vec<i64> {
    let mut candidates = Vec::new();
    for (index, frame) in frames.iter().enumerate() {
        let eligible = match rule {
            PeakRule::LegacyAbsolute {
                threshold,
                radius_hops,
                ..
            } => {
                frame.value >= threshold
                    && is_earliest_maximum(frames, index, radius_hops, radius_hops)
            }
            PeakRule::LocalMean {
                delta,
                absolute_floor,
                pre_max_hops,
                post_max_hops,
                pre_avg_hops,
                post_avg_hops,
                ..
            } => {
                frame.value >= absolute_floor
                    && frame.value
                        >= padded_mean(frames, index, pre_avg_hops, post_avg_hops) + delta
                    && is_earliest_maximum(frames, index, pre_max_hops, post_max_hops)
            }
        };
        if eligible {
            candidates.push((frame.event_sample, frame.value));
        }
    }
    let refractory_samples = rule.refractory_samples(sample_rate);
    let mut selected: Vec<(i64, f32)> = Vec::new();
    for candidate in candidates {
        if let Some(previous) = selected.last_mut() {
            if candidate.0 - previous.0 <= refractory_samples {
                if candidate.1 > previous.1 {
                    *previous = candidate;
                }
                continue;
            }
        }
        selected.push(candidate);
    }
    selected.into_iter().map(|(sample, _)| sample).collect()
}

fn is_earliest_maximum(
    frames: &[TransientOdfFrame],
    index: usize,
    before: usize,
    after: usize,
) -> bool {
    let start = index.saturating_sub(before);
    let end = (index + after + 1).min(frames.len());
    let value = frames[index].value;
    !frames[start..end].iter().any(|frame| frame.value > value)
        && !frames[start..index]
            .iter()
            .any(|frame| frame.value == value)
}

fn padded_mean(frames: &[TransientOdfFrame], index: usize, before: usize, after: usize) -> f32 {
    let start = index.saturating_sub(before);
    let end = (index + after + 1).min(frames.len());
    let sum = frames[start..end]
        .iter()
        .map(|frame| frame.value)
        .sum::<f32>();
    sum / (before + after + 1) as f32
}

fn score_track(
    prediction_samples: &[i64],
    labels: &[LabelEvent],
    sample_rate: u32,
    duration_seconds: f64,
) -> Result<(Counts, Vec<f64>), String> {
    let matches = match_events(prediction_samples, sample_rate, labels)?;
    let matched_predictions = matches
        .iter()
        .map(|record| record.prediction_index)
        .collect::<HashSet<_>>();
    let matched_labels = matches
        .iter()
        .map(|record| record.label_index)
        .collect::<HashSet<_>>();
    let matched_label_micros = matches
        .iter()
        .map(|record| labels[record.label_index].time_micros)
        .collect::<Vec<_>>();
    let matched_prediction_samples = matches
        .iter()
        .map(|record| prediction_samples[record.prediction_index])
        .collect::<Vec<_>>();
    let duplicate_fp = prediction_samples
        .iter()
        .enumerate()
        .filter(|(index, prediction)| {
            !matched_predictions.contains(index)
                && matched_label_micros
                    .iter()
                    .any(|label| is_within_tolerance(**prediction, sample_rate, *label))
        })
        .count();
    let merged_fn = labels
        .iter()
        .enumerate()
        .filter(|(index, label)| {
            !matched_labels.contains(index)
                && matched_prediction_samples.iter().any(|prediction| {
                    is_within_tolerance(*prediction, sample_rate, label.time_micros)
                })
        })
        .count();
    let mut counts = Counts {
        duration_seconds,
        label_count: labels.len(),
        prediction_count: prediction_samples.len(),
        tp: matches.len(),
        fp: prediction_samples.len() - matches.len(),
        fn_count: labels.len() - matches.len(),
        duplicate_fp,
        merged_fn,
        ..Counts::default()
    };
    for (index, label) in labels.iter().enumerate() {
        let matched = matched_labels.contains(&index);
        if is_kick_only(label) {
            counts.kick_only_total += 1;
            counts.kick_only_tp += usize::from(matched);
        }
        if is_hat_only(label) {
            counts.hat_only_total += 1;
            counts.hat_only_tp += usize::from(matched);
        }
        if label.kick {
            counts.kick_containing_total += 1;
            counts.kick_containing_tp += usize::from(matched);
        }
        if label.hat {
            counts.hat_containing_total += 1;
            counts.hat_containing_tp += usize::from(matched);
        }
    }
    Ok((
        counts,
        matches
            .iter()
            .map(|record| record.signed_error_seconds(sample_rate))
            .collect(),
    ))
}

fn is_kick_only(label: &LabelEvent) -> bool {
    !label.pitches.is_empty() && label.pitches.iter().all(|pitch| *pitch == 36)
}

fn is_hat_only(label: &LabelEvent) -> bool {
    !label.pitches.is_empty() && label.pitches.iter().all(|pitch| HAT_NOTES.contains(pitch))
}

fn relative_utf8(root: &Path, path: &Path) -> Result<String, String> {
    path.strip_prefix(root)
        .map_err(|_| format!("input outside canonical root: {}", path.display()))?
        .to_str()
        .map(str::to_string)
        .ok_or_else(|| format!("input path is not UTF-8: {}", path.display()))
}

#[cfg(test)]
#[path = "evaluation_tests.rs"]
mod tests;
