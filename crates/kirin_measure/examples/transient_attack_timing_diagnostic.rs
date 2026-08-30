//! Diagnoses whether the B-553 timing tail is detector-local or track-aligned.
//!
//! Track recentering is reported only as a measurement diagnosis. It is not a
//! permitted detector correction and cannot authorize an ATTACK candidate.

use std::collections::BTreeMap;
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
use evaluation::{is_kick_only, pick_peaks};
use input::{read_development_pilot_selection, LabelEvent};
use matching::match_events;
use metrics::{timing, TimingValues};
use pilot_input::load_development_pilot_track;

const EXPECTED_CANDIDATE_ID: &str = "b553-superflux-pilot-best";
const HAT_NOTES: [u8; 5] = [22, 26, 42, 44, 46];

#[derive(Debug)]
struct Cli {
    root: PathBuf,
    manifest: PathBuf,
    folds: PathBuf,
    candidate: PathBuf,
    result: PathBuf,
}

#[derive(Default)]
struct CategoryErrors {
    all: Vec<f64>,
    kick_only: Vec<f64>,
    hat_only: Vec<f64>,
    other: Vec<f64>,
}

#[derive(Serialize)]
struct CategoryTiming {
    all: TimingValues,
    kick_only: TimingValues,
    hat_only: TimingValues,
    other: TimingValues,
}

#[derive(Serialize)]
struct TrackTiming {
    performance_id: String,
    fold: u8,
    matched_events: usize,
    raw: TimingValues,
    recentered: TimingValues,
}

#[derive(Serialize)]
struct Artifact {
    schema: &'static str,
    status: &'static str,
    profile: &'static str,
    candidate_id: &'static str,
    publication_eligible: bool,
    winner_eligible: bool,
    correction_eligible: bool,
    contract: &'static str,
    raw: CategoryTiming,
    first_constituent_note_diagnostic: CategoryTiming,
    nearest_constituent_note_diagnostic: CategoryTiming,
    track_recentered_diagnostic: CategoryTiming,
    tracks_with_signed_median_absolute_at_least_10_ms: usize,
    matched_events_in_those_tracks: usize,
    tracks: Vec<TrackTiming>,
}

fn main() {
    run().unwrap_or_else(|error| {
        eprintln!("ATTACK timing diagnostic failed: {error}");
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
        return Err("timing diagnostic requires the frozen B-553 candidate".to_string());
    }
    let (_, analyzer, rule) = candidate.config.formal_parts()?;
    let FormalAnalyzer::FixedSuperflux(config) = analyzer else {
        return Err("timing diagnostic requires fixed SuperFlux".to_string());
    };
    let mut raw = CategoryErrors::default();
    let mut first_constituent = CategoryErrors::default();
    let mut nearest_constituent = CategoryErrors::default();
    let mut recentered = CategoryErrors::default();
    let mut tracks = Vec::with_capacity(290);
    let mut biased_tracks = 0;
    let mut biased_track_matches = 0;
    for selection in manifest.selection.entries.iter().cloned() {
        let track = load_development_pilot_track(&root, selection)?;
        let formal = track.selection.formal.as_ref().unwrap();
        let frames = analyze_superflux_frames(&track.wav.samples, 44_100, config)?;
        let predictions = pick_peaks(&frames, 44_100, rule)
            .into_iter()
            .filter(|sample| {
                formal.excerpt_start_sample_44100 as i64 <= *sample
                    && *sample < formal.excerpt_end_sample_44100 as i64
            })
            .collect::<Vec<_>>();
        let matches = match_events(&predictions, 44_100, &track.labels.events)?;
        let track_errors = matches
            .iter()
            .map(|record| record.signed_error_seconds(44_100))
            .collect::<Vec<_>>();
        let track_median = timing(&track_errors)
            .signed_median_ms
            .ok_or("matched-event timing unexpectedly empty")?
            / 1_000.0;
        let centered = track_errors
            .iter()
            .map(|error| error - track_median)
            .collect::<Vec<_>>();
        if track_median.abs() >= 0.010 {
            biased_tracks += 1;
            biased_track_matches += matches.len();
        }
        for (record, (&error, &centered_error)) in
            matches.iter().zip(track_errors.iter().zip(&centered))
        {
            let label = &track.labels.events[record.label_index];
            let prediction = predictions[record.prediction_index];
            push_category(&mut raw, error, label);
            push_category(
                &mut first_constituent,
                note_error_seconds(prediction, 44_100, label.notes[0].time_micros),
                label,
            );
            push_category(
                &mut nearest_constituent,
                label
                    .notes
                    .iter()
                    .map(|note| note_error_seconds(prediction, 44_100, note.time_micros))
                    .min_by(|left, right| left.abs().total_cmp(&right.abs()))
                    .ok_or("compound label has no constituent note")?,
                label,
            );
            push_category(&mut recentered, centered_error, label);
        }
        tracks.push(TrackTiming {
            performance_id: track.selection.id,
            fold: formal.fold,
            matched_events: matches.len(),
            raw: timing(&track_errors),
            recentered: timing(&centered),
        });
    }
    let artifact = Artifact {
        schema: "kirin-hypha-attack-drum-timing-diagnostic-v1",
        status: "development_measurement_diagnostic_not_detector_correction",
        profile: "DRUM",
        candidate_id: EXPECTED_CANDIDATE_ID,
        publication_eligible: false,
        winner_eligible: false,
        correction_eligible: false,
        contract: "same-B553-predictions-and-matches;compare-compound-mean-with-first-and-nearest-constituent-note-and-per-track-recentered-errors;alternate-values-diagnostic-only",
        raw: category_timing(&raw),
        first_constituent_note_diagnostic: category_timing(&first_constituent),
        nearest_constituent_note_diagnostic: category_timing(&nearest_constituent),
        track_recentered_diagnostic: category_timing(&recentered),
        tracks_with_signed_median_absolute_at_least_10_ms: biased_tracks,
        matched_events_in_those_tracks: biased_track_matches,
        tracks,
    };
    publish_create_new(
        &cli.result,
        &serde_json::to_vec_pretty(&artifact)
            .map_err(|error| format!("cannot serialize timing diagnostic: {error}"))?,
    )?;
    println!(
        "ATTACK timing diagnostic complete: {}",
        cli.result.display()
    );
    Ok(())
}

fn push_category(errors: &mut CategoryErrors, error: f64, label: &LabelEvent) {
    errors.all.push(error);
    if is_kick_only(label) {
        errors.kick_only.push(error);
    } else if !label.pitches.is_empty()
        && label.pitches.iter().all(|pitch| HAT_NOTES.contains(pitch))
    {
        errors.hat_only.push(error);
    } else {
        errors.other.push(error);
    }
}

fn note_error_seconds(prediction: i64, sample_rate: u32, note_micros: u64) -> f64 {
    let units =
        i128::from(prediction) * 1_000_000 - i128::from(note_micros) * i128::from(sample_rate);
    units as f64 / (f64::from(sample_rate) * 1_000_000.0)
}

fn category_timing(errors: &CategoryErrors) -> CategoryTiming {
    CategoryTiming {
        all: timing(&errors.all),
        kick_only: timing(&errors.kick_only),
        hat_only: timing(&errors.hat_only),
        other: timing(&errors.other),
    }
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
            return Err("timing diagnostic permits only DRUM".to_string());
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
    use input::MidiNote;

    fn label(pitches: &[u8]) -> LabelEvent {
        LabelEvent {
            time_micros: 0,
            time_secs: 0.0,
            kick: pitches.contains(&36),
            hat: pitches.iter().any(|pitch| HAT_NOTES.contains(pitch)),
            pitches: pitches.to_vec(),
            note_count: pitches.len(),
            notes: pitches
                .iter()
                .map(|pitch| MidiNote {
                    time_micros: 0,
                    time_secs: 0.0,
                    pitch: *pitch,
                    velocity: 100,
                })
                .collect(),
        }
    }

    #[test]
    fn timing_categories_keep_kick_hat_and_other_separate() {
        let mut errors = CategoryErrors::default();
        push_category(&mut errors, 0.001, &label(&[36]));
        push_category(&mut errors, 0.002, &label(&[42, 46]));
        push_category(&mut errors, 0.003, &label(&[38]));
        assert_eq!(errors.all.len(), 3);
        assert_eq!(errors.kick_only, [0.001]);
        assert_eq!(errors.hat_only, [0.002]);
        assert_eq!(errors.other, [0.003]);
    }
}
