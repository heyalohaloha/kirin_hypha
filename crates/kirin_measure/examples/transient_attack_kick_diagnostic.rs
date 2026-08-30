//! Event-level diagnosis for the frozen B-553 ATTACK DRUM candidate.
//!
//! This tool explains kick-only misses. It does not tune a candidate, select a
//! winner, inspect a holdout, or authorize a public ATTACK build.

use std::collections::{BTreeMap, HashMap};
use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process;

use serde::Serialize;

#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/contract.rs"]
mod contract;
#[path = "transient_drum_excerpt/mod.rs"]
mod drum_excerpt;
#[path = "transient_drum_midi/mod.rs"]
mod drum_midi;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/evaluation.rs"]
mod evaluation;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/input.rs"]
mod input;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/matching.rs"]
mod matching;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/metrics.rs"]
mod metrics;
#[path = "transient_candidate_eval/pilot_input.rs"]
mod pilot_input;

use contract::{CandidateConfig, FormalAnalyzer};
use evaluation::formal_evaluation::analyze_superflux_frames;
use evaluation::{is_kick_only, peak_candidates, pick_peaks};
use input::{read_development_pilot_selection, LabelEvent};
use matching::match_events;
use pilot_input::load_development_pilot_track;

const EXPECTED_CANDIDATE_ID: &str = "b553-superflux-pilot-best";

#[derive(Debug)]
struct Cli {
    root: PathBuf,
    manifest: PathBuf,
    folds: PathBuf,
    candidate: PathBuf,
    result: PathBuf,
}

#[derive(Clone, Debug, Serialize)]
struct KickEventDiagnosis {
    performance_id: String,
    drummer: String,
    fold: u8,
    label_time_micros: u64,
    matched: bool,
    miss_class: &'static str,
    nearest_selected_error_ms: Option<f64>,
    nearest_eligible_error_ms: Option<f64>,
    midi_velocity_max: u8,
    attack_peak_dbfs: f64,
    attack_rise_db: f64,
}

#[derive(Debug, Serialize)]
struct DrummerSummary {
    drummer: String,
    total: usize,
    matched: usize,
    recall: f64,
    miss_classes: BTreeMap<&'static str, usize>,
    matched_peak_dbfs_median: Option<f64>,
    missed_peak_dbfs_median: Option<f64>,
    matched_rise_db_median: Option<f64>,
    missed_rise_db_median: Option<f64>,
    matched_velocity_median: Option<f64>,
    missed_velocity_median: Option<f64>,
}

#[derive(Serialize)]
struct DiagnosticArtifact {
    schema: &'static str,
    status: &'static str,
    profile: &'static str,
    candidate_id: &'static str,
    publication_eligible: bool,
    winner_eligible: bool,
    fresh_holdout_eligible: bool,
    event_contract: &'static str,
    total_kick_only: usize,
    matched_kick_only: usize,
    missed_kick_only: usize,
    miss_classes: BTreeMap<&'static str, usize>,
    drummer_summaries: Vec<DrummerSummary>,
    events: Vec<KickEventDiagnosis>,
}

fn main() {
    run().unwrap_or_else(|error| {
        eprintln!("ATTACK kick diagnostic failed: {error}");
        process::exit(1);
    });
}

fn run() -> Result<(), String> {
    let cli = Cli::parse(std::env::args_os().skip(1))?;
    if cli.result.exists() {
        return Err(format!("result already exists: {}", cli.result.display()));
    }
    let root = fs::canonicalize(&cli.root).map_err(|error| format!("dataset root: {error}"))?;
    let manifest = read_development_pilot_selection(&root, &cli.manifest, &cli.folds)?;
    let candidate = CandidateConfig::read(&cli.candidate)?;
    if candidate.config.candidate_id() != EXPECTED_CANDIDATE_ID {
        return Err("kick diagnostic accepts only the frozen B-553 candidate".to_string());
    }
    let (_, analyzer, rule) = candidate.config.formal_parts()?;
    let FormalAnalyzer::FixedSuperflux(config) = analyzer else {
        return Err("kick diagnostic requires fixed SuperFlux".to_string());
    };
    let mut events = Vec::new();
    for selection in manifest.selection.entries.iter().cloned() {
        let track = load_development_pilot_track(&root, selection)?;
        let formal = track.selection.formal.as_ref().unwrap();
        let frames =
            analyze_superflux_frames(&track.wav.samples, track.wav.metadata.sample_rate, config)?;
        let core_start = formal.excerpt_start_sample_44100 as i64;
        let core_end = formal.excerpt_end_sample_44100 as i64;
        let eligible = peak_candidates(&frames, rule)
            .into_iter()
            .map(|candidate| candidate.0)
            .filter(|sample| core_start <= *sample && *sample < core_end)
            .collect::<Vec<_>>();
        let selected = pick_peaks(&frames, track.wav.metadata.sample_rate, rule)
            .into_iter()
            .filter(|sample| core_start <= *sample && *sample < core_end)
            .collect::<Vec<_>>();
        let matches = match_events(
            &selected,
            track.wav.metadata.sample_rate,
            &track.labels.events,
        )?;
        let matched = matches
            .into_iter()
            .map(|record| {
                (
                    record.label_index,
                    record.signed_error_seconds(track.wav.metadata.sample_rate) * 1_000.0,
                )
            })
            .collect::<HashMap<_, _>>();
        let drummer = track
            .selection
            .id
            .split('/')
            .next()
            .ok_or("performance ID has no drummer")?
            .to_string();
        for (label_index, label) in track.labels.events.iter().enumerate() {
            if !is_kick_only(label) {
                continue;
            }
            let matched_error = matched.get(&label_index).copied();
            let selected_error = nearest_error_ms(&selected, label, track.wav.metadata.sample_rate);
            let eligible_error = nearest_error_ms(&eligible, label, track.wav.metadata.sample_rate);
            let (attack_peak_dbfs, attack_rise_db) =
                audio_attack_features(&track.wav.samples, label, track.wav.metadata.sample_rate);
            events.push(KickEventDiagnosis {
                performance_id: track.selection.id.clone(),
                drummer: drummer.clone(),
                fold: formal.fold,
                label_time_micros: label.time_micros,
                matched: matched_error.is_some(),
                miss_class: classify_miss(matched_error, selected_error, eligible_error),
                nearest_selected_error_ms: selected_error,
                nearest_eligible_error_ms: eligible_error,
                midi_velocity_max: label
                    .notes
                    .iter()
                    .map(|note| note.velocity)
                    .max()
                    .unwrap_or(0),
                attack_peak_dbfs,
                attack_rise_db,
            });
        }
    }
    let matched_kick_only = events.iter().filter(|event| event.matched).count();
    let artifact = DiagnosticArtifact {
        schema: "kirin-hypha-attack-kick-diagnostic-v1",
        status: "development_diagnostic_complete_not_candidate_tuning",
        profile: "DRUM",
        candidate_id: EXPECTED_CANDIDATE_ID,
        publication_eligible: false,
        winner_eligible: false,
        fresh_holdout_eligible: false,
        event_contract: "kick-only;exact-B553-candidate;selected-vs-pre-refractory-eligible;audio[-30,-5)ms-to[0,30)ms",
        total_kick_only: events.len(),
        matched_kick_only,
        missed_kick_only: events.len() - matched_kick_only,
        miss_classes: miss_counts(&events),
        drummer_summaries: drummer_summaries(&events),
        events,
    };
    publish_create_new(
        &cli.result,
        &serde_json::to_vec_pretty(&artifact)
            .map_err(|error| format!("cannot serialize kick diagnostic: {error}"))?,
    )?;
    println!(
        "ATTACK kick diagnostic complete: total={} matched={} missed={} result={}",
        artifact.total_kick_only,
        artifact.matched_kick_only,
        artifact.missed_kick_only,
        cli.result.display()
    );
    Ok(())
}

fn classify_miss(
    matched_error: Option<f64>,
    selected_error: Option<f64>,
    eligible_error: Option<f64>,
) -> &'static str {
    if matched_error.is_some() {
        "matched"
    } else if selected_error.is_some_and(|error| error.abs() <= 25.0) {
        "one_to_one_competition"
    } else if eligible_error.is_some_and(|error| error.abs() <= 25.0) {
        "refractory_suppressed"
    } else if eligible_error.is_some_and(|error| error.abs() <= 50.0) {
        "eligible_peak_25_to_50_ms"
    } else {
        "no_eligible_peak_within_50_ms"
    }
}

fn nearest_error_ms(samples: &[i64], label: &LabelEvent, sample_rate: u32) -> Option<f64> {
    samples
        .iter()
        .map(|sample| {
            let units = i128::from(*sample) * 1_000_000
                - i128::from(label.time_micros) * i128::from(sample_rate);
            units as f64 / (f64::from(sample_rate) * 1_000.0)
        })
        .min_by(|left, right| left.abs().total_cmp(&right.abs()))
}

fn audio_attack_features(samples: &[f32], label: &LabelEvent, sample_rate: u32) -> (f64, f64) {
    let center =
        ((u128::from(label.time_micros) * u128::from(sample_rate) + 500_000) / 1_000_000) as usize;
    let millis = |value: usize| value * sample_rate as usize / 1_000;
    let pre = slice_clamped(
        samples,
        center.saturating_sub(millis(30)),
        center.saturating_sub(millis(5)),
    );
    let attack = slice_clamped(samples, center, center.saturating_add(millis(30)));
    let peak = attack
        .iter()
        .map(|sample| sample.abs())
        .fold(0.0_f32, f32::max) as f64;
    let pre_rms = rms(pre);
    let attack_rms = rms(attack);
    (db(peak), db(attack_rms) - db(pre_rms))
}

fn slice_clamped(samples: &[f32], start: usize, end: usize) -> &[f32] {
    &samples[start.min(samples.len())..end.min(samples.len()).max(start.min(samples.len()))]
}

fn rms(samples: &[f32]) -> f64 {
    if samples.is_empty() {
        return 0.0;
    }
    (samples
        .iter()
        .map(|sample| f64::from(*sample) * f64::from(*sample))
        .sum::<f64>()
        / samples.len() as f64)
        .sqrt()
}

fn db(value: f64) -> f64 {
    20.0 * value.max(1.0e-12).log10()
}

fn miss_counts(events: &[KickEventDiagnosis]) -> BTreeMap<&'static str, usize> {
    let mut counts = BTreeMap::new();
    for event in events.iter().filter(|event| !event.matched) {
        *counts.entry(event.miss_class).or_default() += 1;
    }
    counts
}

fn drummer_summaries(events: &[KickEventDiagnosis]) -> Vec<DrummerSummary> {
    let mut grouped = BTreeMap::<String, Vec<&KickEventDiagnosis>>::new();
    for event in events {
        grouped
            .entry(event.drummer.clone())
            .or_default()
            .push(event);
    }
    grouped
        .into_iter()
        .map(|(drummer, events)| {
            let matched = events.iter().filter(|event| event.matched).count();
            DrummerSummary {
                drummer,
                total: events.len(),
                matched,
                recall: matched as f64 / events.len() as f64,
                miss_classes: miss_counts(&events.iter().copied().cloned().collect::<Vec<_>>()),
                matched_peak_dbfs_median: median(
                    events
                        .iter()
                        .filter(|event| event.matched)
                        .map(|event| event.attack_peak_dbfs),
                ),
                missed_peak_dbfs_median: median(
                    events
                        .iter()
                        .filter(|event| !event.matched)
                        .map(|event| event.attack_peak_dbfs),
                ),
                matched_rise_db_median: median(
                    events
                        .iter()
                        .filter(|event| event.matched)
                        .map(|event| event.attack_rise_db),
                ),
                missed_rise_db_median: median(
                    events
                        .iter()
                        .filter(|event| !event.matched)
                        .map(|event| event.attack_rise_db),
                ),
                matched_velocity_median: median(
                    events
                        .iter()
                        .filter(|event| event.matched)
                        .map(|event| f64::from(event.midi_velocity_max)),
                ),
                missed_velocity_median: median(
                    events
                        .iter()
                        .filter(|event| !event.matched)
                        .map(|event| f64::from(event.midi_velocity_max)),
                ),
            }
        })
        .collect()
}

fn median(values: impl Iterator<Item = f64>) -> Option<f64> {
    let mut values = values.collect::<Vec<_>>();
    values.sort_by(f64::total_cmp);
    (!values.is_empty()).then(|| values[values.len() / 2])
}

fn publish_create_new(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|error| format!("cannot create {}: {error}", path.display()))?;
    file.write_all(bytes)
        .and_then(|()| file.write_all(b"\n"))
        .and_then(|()| file.sync_all())
        .map_err(|error| format!("cannot publish {}: {error}", path.display()))
}

impl Cli {
    fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut values = BTreeMap::<String, OsString>::new();
        let mut arguments = arguments.into_iter();
        while let Some(flag) = arguments.next() {
            let flag = flag.to_str().ok_or("CLI flag is not UTF-8")?.to_string();
            let value = arguments
                .next()
                .ok_or_else(|| format!("missing value for {flag}"))?;
            if values.insert(flag.clone(), value).is_some() {
                return Err(format!("duplicate CLI flag: {flag}"));
            }
        }
        if take_string(&mut values, "--profile")? != "DRUM" {
            return Err("kick diagnostic permits only DRUM".to_string());
        }
        let cli = Self {
            root: take_path(&mut values, "--root")?,
            manifest: take_path(&mut values, "--manifest")?,
            folds: take_path(&mut values, "--folds")?,
            candidate: take_path(&mut values, "--candidate-config")?,
            result: take_path(&mut values, "--result")?,
        };
        if let Some(flag) = values.keys().next() {
            return Err(format!("unknown CLI flag: {flag}"));
        }
        Ok(cli)
    }
}

fn take_path(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<PathBuf, String> {
    values
        .remove(flag)
        .map(PathBuf::from)
        .ok_or_else(|| format!("missing {flag}"))
}

fn take_string(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<String, String> {
    values
        .remove(flag)
        .ok_or_else(|| format!("missing {flag}"))?
        .into_string()
        .map_err(|_| format!("{flag} is not UTF-8"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn miss_class_order_is_fixed() {
        assert_eq!(classify_miss(Some(1.0), None, None), "matched");
        assert_eq!(
            classify_miss(None, Some(25.0), None),
            "one_to_one_competition"
        );
        assert_eq!(
            classify_miss(None, None, Some(-25.0)),
            "refractory_suppressed"
        );
        assert_eq!(
            classify_miss(None, None, Some(50.0)),
            "eligible_peak_25_to_50_ms"
        );
        assert_eq!(
            classify_miss(None, None, Some(50.1)),
            "no_eligible_peak_within_50_ms"
        );
    }
}
