//! Local-only E-GMD ATTACK candidate evaluation.
//!
//! The dataset stays outside the repository. Usage:
//! `cargo run -p kirin_measure --example transient_candidate_eval --release -- /path/to/subset`

use std::collections::BTreeMap;
use std::env;
use std::path::PathBuf;

use kirin_measure::{TransientCandidateAnalyzer, TransientOdfFrame, TransientOdfKind};

#[path = "transient_candidate_eval/input.rs"]
mod input;
use input::{read_midi_labels, read_pcm16_mono_wav, read_selection, LabelEvent, Selection};

const MATCH_TOLERANCE_SECS: f64 = 0.030;

#[derive(Clone, Copy, Debug)]
struct Gates {
    precision: f64,
    recall: f64,
    f1: f64,
    timing_p95_ms: f64,
    timing_max_ms: f64,
    false_positives_per_second: f64,
    kick_recall: f64,
    hat_recall: f64,
}

const PUBLICATION_GATES: Gates = Gates {
    precision: 0.85,
    recall: 0.75,
    f1: 0.80,
    timing_p95_ms: 15.0,
    timing_max_ms: 30.0,
    false_positives_per_second: 1.0,
    kick_recall: 0.75,
    hat_recall: 0.50,
};

struct Track {
    selection: Selection,
    sample_rate: u32,
    samples: Vec<f32>,
    labels: Vec<LabelEvent>,
    frames: BTreeMap<&'static str, Vec<TransientOdfFrame>>,
}

#[derive(Clone, Copy, Debug)]
struct PeakRule {
    threshold: f32,
    radius: usize,
    refractory_secs: f64,
}

#[derive(Clone, Debug, Default)]
struct Metrics {
    tp: usize,
    fp: usize,
    fn_count: usize,
    kick_tp: usize,
    kick_total: usize,
    hat_tp: usize,
    hat_total: usize,
    timing_ms: Vec<f64>,
    duration_secs: f64,
}

impl Metrics {
    fn add(&mut self, other: Self) {
        self.tp += other.tp;
        self.fp += other.fp;
        self.fn_count += other.fn_count;
        self.kick_tp += other.kick_tp;
        self.kick_total += other.kick_total;
        self.hat_tp += other.hat_tp;
        self.hat_total += other.hat_total;
        self.timing_ms.extend_from_slice(&other.timing_ms);
        self.duration_secs += other.duration_secs;
    }

    fn precision(&self) -> f64 {
        ratio(self.tp, self.tp + self.fp)
    }

    fn recall(&self) -> f64 {
        ratio(self.tp, self.tp + self.fn_count)
    }

    fn f1(&self) -> f64 {
        let precision = self.precision();
        let recall = self.recall();
        if precision + recall == 0.0 {
            0.0
        } else {
            2.0 * precision * recall / (precision + recall)
        }
    }

    fn timing_mean_ms(&self) -> f64 {
        if self.timing_ms.is_empty() {
            0.0
        } else {
            self.timing_ms.iter().sum::<f64>() / self.timing_ms.len() as f64
        }
    }

    fn timing_percentile_ms(&self, percentile: f64) -> f64 {
        if self.timing_ms.is_empty() {
            return 0.0;
        }
        let mut values = self.timing_ms.clone();
        values.sort_by(f64::total_cmp);
        let index = ((values.len() - 1) as f64 * percentile).ceil() as usize;
        values[index]
    }

    fn timing_max_ms(&self) -> f64 {
        self.timing_ms
            .iter()
            .copied()
            .reduce(f64::max)
            .unwrap_or(0.0)
    }

    fn false_positives_per_second(&self) -> f64 {
        if self.duration_secs <= 0.0 {
            0.0
        } else {
            self.fp as f64 / self.duration_secs
        }
    }

    fn passes(&self, gates: Gates) -> bool {
        self.precision() >= gates.precision
            && self.recall() >= gates.recall
            && self.f1() >= gates.f1
            && self.timing_percentile_ms(0.95) <= gates.timing_p95_ms
            && self.timing_max_ms() <= gates.timing_max_ms
            && self.false_positives_per_second() <= gates.false_positives_per_second
            && ratio(self.kick_tp, self.kick_total) >= gates.kick_recall
            && ratio(self.hat_tp, self.hat_total) >= gates.hat_recall
    }
}

fn ratio(numerator: usize, denominator: usize) -> f64 {
    if denominator == 0 {
        0.0
    } else {
        numerator as f64 / denominator as f64
    }
}

fn main() -> Result<(), String> {
    let root = env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .ok_or("subset root argument is required")?;
    let selections = read_selection(&root)?;
    let mut tracks = selections
        .into_iter()
        .map(load_track)
        .collect::<Result<Vec<_>, _>>()?;
    for track in &mut tracks {
        for kind in TransientOdfKind::ALL {
            let frames = analyze(&track.samples, track.sample_rate, kind)?;
            track.frames.insert(kind.as_str(), frames);
        }
    }

    println!("E-GMD v1.0.0 local subset; dataset files are not repository artifacts");
    println!(
        "pairs={} validation={} test={} tolerance_ms={}",
        tracks.len(),
        tracks
            .iter()
            .filter(|track| track.selection.split == "validation")
            .count(),
        tracks
            .iter()
            .filter(|track| track.selection.split == "test")
            .count(),
        MATCH_TOLERANCE_SECS * 1_000.0
    );
    println!(
        "gates precision>={:.2} recall>={:.2} f1>={:.2} timing_p95_ms<={:.1} timing_max_ms<={:.1} fp_per_s<={:.1} kick_recall>={:.2} hat_recall>={:.2}",
        PUBLICATION_GATES.precision,
        PUBLICATION_GATES.recall,
        PUBLICATION_GATES.f1,
        PUBLICATION_GATES.timing_p95_ms,
        PUBLICATION_GATES.timing_max_ms,
        PUBLICATION_GATES.false_positives_per_second,
        PUBLICATION_GATES.kick_recall,
        PUBLICATION_GATES.hat_recall,
    );

    let mut winner = None;
    for kind in TransientOdfKind::ALL {
        let rule = tune_rule(&tracks, kind)?;
        let validation = evaluate_split(&tracks, kind, rule, "validation")?;
        let gate = if validation.passes(PUBLICATION_GATES) {
            "pass"
        } else {
            "fail"
        };
        println!(
            "candidate={} threshold={:.8} radius={} refractory_ms={:.0} validation_gate={} {}",
            kind.as_str(),
            rule.threshold,
            rule.radius,
            rule.refractory_secs * 1_000.0,
            gate,
            format_metrics(&validation)
        );
        if validation.passes(PUBLICATION_GATES)
            && winner
                .as_ref()
                .is_none_or(|(_, _, best): &(TransientOdfKind, PeakRule, Metrics)| {
                    validation.f1() > best.f1()
                        || (validation.f1() == best.f1()
                            && validation.timing_mean_ms() < best.timing_mean_ms())
                })
        {
            winner = Some((kind, rule, validation));
        }
    }
    let (kind, rule, validation) = winner.ok_or("no candidate passed validation gates")?;
    let holdout = evaluate_split(&tracks, kind, rule, "test")?;
    println!(
        "selected={} validation {}",
        kind.as_str(),
        format_metrics(&validation)
    );
    println!(
        "holdout={} publication_gate={} {}",
        kind.as_str(),
        if holdout.passes(PUBLICATION_GATES) {
            "pass"
        } else {
            "fail"
        },
        format_metrics(&holdout)
    );
    println!(
        "candidate_layout_hash={}",
        TransientCandidateAnalyzer::new(44_100, kind)
            .map_err(str::to_string)?
            .layout()
            .definition_hex()
    );
    Ok(())
}

fn format_metrics(metrics: &Metrics) -> String {
    format!(
        "precision={:.4} recall={:.4} f1={:.4} timing_mean_ms={:.3} timing_p95_ms={:.3} timing_max_ms={:.3} fp_per_s={:.3} kick_recall={:.4} hat_recall={:.4} tp={} fp={} fn={}",
        metrics.precision(),
        metrics.recall(),
        metrics.f1(),
        metrics.timing_mean_ms(),
        metrics.timing_percentile_ms(0.95),
        metrics.timing_max_ms(),
        metrics.false_positives_per_second(),
        ratio(metrics.kick_tp, metrics.kick_total),
        ratio(metrics.hat_tp, metrics.hat_total),
        metrics.tp,
        metrics.fp,
        metrics.fn_count
    )
}

fn load_track(selection: Selection) -> Result<Track, String> {
    let (sample_rate, samples) = read_pcm16_mono_wav(&selection.audio)?;
    let labels = read_midi_labels(&selection.midi)?;
    if labels.is_empty() || samples.is_empty() {
        return Err(format!("empty E-GMD pair: {}", selection.audio.display()));
    }
    Ok(Track {
        selection,
        sample_rate,
        samples,
        labels,
        frames: BTreeMap::new(),
    })
}

fn analyze(
    samples: &[f32],
    sample_rate: u32,
    kind: TransientOdfKind,
) -> Result<Vec<TransientOdfFrame>, String> {
    let mut analyzer =
        TransientCandidateAnalyzer::new(sample_rate, kind).map_err(str::to_string)?;
    let window = analyzer.layout().window_samples;
    let hop = analyzer.layout().hop_samples;
    let mut frames = Vec::with_capacity(samples.len() / hop);
    if samples.len() < window {
        return Ok(frames);
    }
    for start in (0..=samples.len() - window).step_by(hop) {
        if let Some(frame) = analyzer
            .analyze_window(&samples[start..start + window], start as i64)
            .map_err(str::to_string)?
        {
            frames.push(frame);
        }
    }
    Ok(frames)
}

fn tune_rule(tracks: &[Track], kind: TransientOdfKind) -> Result<PeakRule, String> {
    let mut values = tracks
        .iter()
        .filter(|track| track.selection.split == "validation")
        .flat_map(|track| track.frames[kind.as_str()].iter().map(|frame| frame.value))
        .filter(|value| value.is_finite() && *value > 0.0)
        .collect::<Vec<_>>();
    values.sort_by(f32::total_cmp);
    if values.is_empty() {
        return Err(format!("no positive validation ODF: {}", kind.as_str()));
    }
    let quantiles = [
        0.50, 0.60, 0.70, 0.78, 0.84, 0.88, 0.91, 0.94, 0.96, 0.98, 0.99,
    ];
    let mut best = None;
    for quantile in quantiles {
        let index = ((values.len() - 1) as f64 * quantile).round() as usize;
        for radius in [1, 2, 3] {
            for refractory_ms in [20.0, 30.0, 40.0, 50.0, 65.0] {
                let rule = PeakRule {
                    threshold: values[index],
                    radius,
                    refractory_secs: refractory_ms / 1_000.0,
                };
                let metrics = evaluate_split(tracks, kind, rule, "validation")?;
                if best
                    .as_ref()
                    .is_none_or(|(_, current): &(PeakRule, Metrics)| {
                        metrics.f1() > current.f1()
                            || (metrics.f1() == current.f1()
                                && metrics.timing_mean_ms() < current.timing_mean_ms())
                    })
                {
                    best = Some((rule, metrics));
                }
            }
        }
    }
    best.map(|(rule, _)| rule).ok_or("no peak rule".to_string())
}

fn evaluate_split(
    tracks: &[Track],
    kind: TransientOdfKind,
    rule: PeakRule,
    split: &str,
) -> Result<Metrics, String> {
    let mut total = Metrics::default();
    for track in tracks.iter().filter(|track| track.selection.split == split) {
        let predictions = pick_peaks(&track.frames[kind.as_str()], track.sample_rate, rule);
        total.add(match_events(
            &predictions,
            &track.labels,
            track.samples.len() as f64 / track.sample_rate as f64,
        ));
    }
    Ok(total)
}

fn pick_peaks(frames: &[TransientOdfFrame], sample_rate: u32, rule: PeakRule) -> Vec<f64> {
    let mut candidates = Vec::new();
    for index in rule.radius..frames.len().saturating_sub(rule.radius) {
        let value = frames[index].value;
        if value < rule.threshold {
            continue;
        }
        let neighborhood = &frames[index - rule.radius..=index + rule.radius];
        if neighborhood.iter().any(|frame| frame.value > value)
            || neighborhood[..rule.radius]
                .iter()
                .any(|frame| frame.value == value)
        {
            continue;
        }
        candidates.push((
            frames[index].event_sample as f64 / sample_rate as f64,
            value,
        ));
    }
    let mut selected: Vec<(f64, f32)> = Vec::new();
    for candidate in candidates {
        if let Some(previous) = selected.last_mut() {
            if candidate.0 - previous.0 < rule.refractory_secs {
                if candidate.1 > previous.1 {
                    *previous = candidate;
                }
                continue;
            }
        }
        selected.push(candidate);
    }
    selected.into_iter().map(|(time, _)| time).collect()
}

fn match_events(predictions: &[f64], labels: &[LabelEvent], duration_secs: f64) -> Metrics {
    let mut pairs = Vec::new();
    for (prediction_index, prediction) in predictions.iter().enumerate() {
        for (label_index, label) in labels.iter().enumerate() {
            let error = (prediction - label.time_secs).abs();
            if error <= MATCH_TOLERANCE_SECS {
                pairs.push((error, prediction_index, label_index));
            }
        }
    }
    pairs.sort_by(|left, right| left.0.total_cmp(&right.0));
    let mut used_predictions = vec![false; predictions.len()];
    let mut used_labels = vec![false; labels.len()];
    let mut metrics = Metrics {
        kick_total: labels.iter().filter(|event| event.kick).count(),
        hat_total: labels.iter().filter(|event| event.hat).count(),
        duration_secs,
        ..Metrics::default()
    };
    for (error, prediction_index, label_index) in pairs {
        if used_predictions[prediction_index] || used_labels[label_index] {
            continue;
        }
        used_predictions[prediction_index] = true;
        used_labels[label_index] = true;
        metrics.tp += 1;
        metrics.timing_ms.push(error * 1_000.0);
        metrics.kick_tp += usize::from(labels[label_index].kick);
        metrics.hat_tp += usize::from(labels[label_index].hat);
    }
    metrics.fp = used_predictions.iter().filter(|used| !**used).count();
    metrics.fn_count = used_labels.iter().filter(|used| !**used).count();
    metrics
}
