use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::Path;

use chrono::{DateTime, Utc};
use kirin_measure::TransientCandidateAnalyzer;
use serde::Serialize;

use super::contract::{sha256_bytes, CandidateArtifact, Cli};
use super::evaluation::LoadedTrack;
use super::input::SelectionManifest;
use super::metrics::EvaluationReport;

const RESULT_SCHEMA: &str = "kirin-hypha-attack-evaluation-result-v2";
const EVALUATOR_VERSION: &str = "B-548";

#[derive(Serialize)]
struct ResultArtifact<'a> {
    schema: &'static str,
    schema_version: u32,
    status: &'static str,
    command: &'static str,
    profile: &'static str,
    purpose: super::contract::Purpose,
    publication_eligible: bool,
    started_at_utc: String,
    finished_at_utc: String,
    provenance: Provenance<'a>,
    manifest: ManifestResult<'a>,
    candidate: CandidateResult<'a>,
    preflight: PreflightResult,
    inputs: Vec<InputResult<'a>>,
    evaluation_contract: EvaluationContract,
    evaluation: &'a EvaluationReport,
    errors: Vec<String>,
    deterministic_result_sha256: String,
}

#[derive(Serialize)]
struct Provenance<'a> {
    evaluator_version: &'static str,
    git_commit: &'a str,
    evaluation_definition_sha256: &'a str,
}

#[derive(Serialize)]
struct DatasetResult<'a> {
    id: &'a str,
    version: &'a str,
    archive_sha256: &'a str,
}

#[derive(Serialize)]
struct ManifestResult<'a> {
    id: String,
    path_argument: String,
    canonical_path: String,
    sha256_before: &'a str,
    sha256_after: &'a str,
    unchanged: bool,
    rows_total: usize,
    evaluation_partition: &'static str,
    source_split_counts: BTreeMap<String, usize>,
    performance_ids: Vec<String>,
    dataset: DatasetResult<'a>,
}

#[derive(Serialize)]
struct CandidateResult<'a> {
    id: &'a str,
    raw_config_sha256: &'a str,
    semantic_config_sha256: &'a str,
    analyzer: &'a super::contract::AnalyzerConfig,
    peak_picker: super::contract::PeakRule,
    analyzer_layout_hashes: BTreeMap<String, String>,
    measurement_definition_sha256: &'a str,
}

#[derive(Serialize)]
struct PreflightResult {
    status: &'static str,
    checks: Vec<&'static str>,
    included_count: usize,
    excluded: Vec<String>,
}

#[derive(Serialize)]
struct InputResult<'a> {
    performance_id: &'a str,
    metadata: MetadataResult<'a>,
    midi: MidiResult<'a>,
    audio: AudioResult<'a>,
    labels: LabelResult,
}

#[derive(Serialize)]
struct MetadataResult<'a> {
    drummer: &'a str,
    session: &'a str,
    style: &'a str,
    bpm: f64,
    beat_type: &'a str,
    time_signature: &'a str,
    declared_duration_seconds: f64,
    source_split: &'a str,
    kit_name: &'a str,
}

#[derive(Serialize)]
struct MidiResult<'a> {
    relative_path: &'a str,
    sha256: &'a str,
    size_bytes: u64,
    raw_note_count: usize,
}

#[derive(Serialize)]
struct AudioResult<'a> {
    relative_path: &'a str,
    sha256: &'a str,
    size_bytes: u64,
    sample_rate: u32,
    channels: u16,
    bits_per_sample: u16,
    sample_count: usize,
    duration_seconds: f64,
    peak_abs: f32,
}

#[derive(Serialize)]
struct LabelResult {
    event_count: usize,
    single_note_event_count: usize,
    multi_note_compound_count: usize,
    pitch_histogram: BTreeMap<u8, usize>,
    velocity_histogram: BTreeMap<u8, usize>,
    kick_only_count: usize,
    hat_only_count: usize,
    kick_containing_count: usize,
    hat_containing_count: usize,
    dense_pair_30_to_50_ms_count: usize,
}

#[derive(Clone, Copy, Serialize)]
struct EvaluationContract {
    event_timebase: &'static str,
    compound_cluster_span_ms_inclusive: f64,
    matching_tolerance_ms_inclusive: f64,
    matcher: &'static str,
    percentile_method: &'static str,
    pitch_mapping: &'static str,
    frame_edges: &'static str,
    macro_aggregation: &'static str,
}

#[derive(Serialize)]
struct DeterministicCore<'a> {
    schema: &'static str,
    evaluator_version: &'static str,
    git_commit: &'a str,
    manifest_sha256: &'a str,
    candidate_semantic_sha256: &'a str,
    evaluation_definition_sha256: &'a str,
    input_identities: Vec<(&'a str, &'a str, &'a str)>,
    evaluation_contract: EvaluationContract,
    evaluation: &'a EvaluationReport,
}

pub(crate) fn write_result(
    cli: &Cli,
    manifest: &SelectionManifest,
    candidate: &CandidateArtifact,
    tracks: &[LoadedTrack],
    evaluation: &EvaluationReport,
    started_at_utc: DateTime<Utc>,
) -> Result<(String, String), String> {
    if cli.result.exists() {
        return Err(format!("result already exists: {}", cli.result.display()));
    }
    let manifest_after =
        fs::read(&manifest.path).map_err(|error| format!("cannot re-read manifest: {error}"))?;
    let manifest_after_sha256 = sha256_bytes(&manifest_after);
    if manifest_after_sha256 != manifest.sha256 {
        return Err("manifest changed during evaluation".to_string());
    }
    let kind = candidate.config.kind()?;
    let layout_hash = TransientCandidateAnalyzer::new(44_100, kind)
        .map_err(str::to_string)?
        .layout()
        .definition_hex();
    let definition_sha256 = definition_hash(candidate, &layout_hash)?;
    let evaluation_contract = fixed_evaluation_contract();
    let deterministic_core = DeterministicCore {
        schema: RESULT_SCHEMA,
        evaluator_version: EVALUATOR_VERSION,
        git_commit: &cli.git_commit,
        manifest_sha256: &manifest.sha256,
        candidate_semantic_sha256: &candidate.semantic_sha256,
        evaluation_definition_sha256: &definition_sha256,
        input_identities: tracks
            .iter()
            .map(|track| {
                (
                    track.selection.id.as_str(),
                    track.midi_sha256.as_str(),
                    track.audio_sha256.as_str(),
                )
            })
            .collect(),
        evaluation_contract,
        evaluation,
    };
    let deterministic_result_sha256 = sha256_bytes(
        &serde_json::to_vec(&deterministic_core)
            .map_err(|error| format!("cannot serialize deterministic result: {error}"))?,
    );
    let inputs = tracks.iter().map(input_result).collect::<Vec<_>>();
    let mut layout_hashes = BTreeMap::new();
    layout_hashes.insert("44100".to_string(), layout_hash);
    let artifact = ResultArtifact {
        schema: RESULT_SCHEMA,
        schema_version: 2,
        status: "complete",
        command: "evaluate",
        profile: "DRUM",
        purpose: cli.purpose,
        publication_eligible: false,
        started_at_utc: started_at_utc.to_rfc3339(),
        finished_at_utc: Utc::now().to_rfc3339(),
        provenance: Provenance {
            evaluator_version: EVALUATOR_VERSION,
            git_commit: &cli.git_commit,
            evaluation_definition_sha256: &definition_sha256,
        },
        manifest: manifest_result(cli, manifest, &manifest_after_sha256),
        candidate: CandidateResult {
            id: &candidate.config.candidate_id,
            raw_config_sha256: &candidate.raw_sha256,
            semantic_config_sha256: &candidate.semantic_sha256,
            analyzer: &candidate.config.analyzer,
            peak_picker: candidate.config.peak_picker,
            analyzer_layout_hashes: layout_hashes,
            measurement_definition_sha256: &definition_sha256,
        },
        preflight: PreflightResult {
            status: "pass",
            checks: vec![
                "explicit_manifest",
                "canonical_paths_within_root",
                "unique_row_and_input_paths",
                "pcm_mono_44100_16_or_24_bit",
                "nonempty_nonsilent_audio",
                "duration_within_10_ms",
                "midi_note_on_and_audio_range",
                "manifest_unchanged",
            ],
            included_count: tracks.len(),
            excluded: Vec::new(),
        },
        inputs,
        evaluation_contract,
        evaluation,
        errors: Vec::new(),
        deterministic_result_sha256: deterministic_result_sha256.clone(),
    };
    let bytes = serde_json::to_vec_pretty(&artifact)
        .map_err(|error| format!("cannot serialize result: {error}"))?;
    atomic_create(&cli.result, &bytes)?;
    Ok((deterministic_result_sha256, definition_sha256))
}

fn manifest_result<'a>(
    cli: &'a Cli,
    manifest: &'a SelectionManifest,
    after_sha256: &'a str,
) -> ManifestResult<'a> {
    let mut split_counts = BTreeMap::new();
    let mut performance_ids = BTreeSet::new();
    for entry in &manifest.entries {
        *split_counts.entry(entry.split.clone()).or_insert(0) += 1;
        performance_ids.insert(entry.id.clone());
    }
    ManifestResult {
        id: format!(
            "opened-diagnostic-{}",
            manifest.sha256.get(..12).unwrap_or(&manifest.sha256)
        ),
        path_argument: cli.manifest.to_string_lossy().into_owned(),
        canonical_path: manifest.path.to_string_lossy().into_owned(),
        sha256_before: &manifest.sha256,
        sha256_after: after_sha256,
        unchanged: manifest.sha256 == after_sha256,
        rows_total: manifest.entries.len(),
        evaluation_partition: "opened_development_diagnostic",
        source_split_counts: split_counts,
        performance_ids: performance_ids.into_iter().collect(),
        dataset: DatasetResult {
            id: &cli.dataset_id,
            version: &cli.dataset_version,
            archive_sha256: &cli.dataset_archive_sha256,
        },
    }
}

fn input_result(track: &LoadedTrack) -> InputResult<'_> {
    let mut pitch_histogram = BTreeMap::new();
    let mut velocity_histogram = BTreeMap::new();
    for event in &track.labels.events {
        for note in &event.notes {
            *pitch_histogram.entry(note.pitch).or_insert(0) += 1;
            *velocity_histogram.entry(note.velocity).or_insert(0) += 1;
        }
    }
    let dense_pair_count = track
        .labels
        .events
        .windows(2)
        .filter(|pair| {
            let distance = pair[1].time_secs - pair[0].time_secs;
            distance > 0.030 && distance <= 0.050
        })
        .count();
    let kick_only_count = track
        .labels
        .events
        .iter()
        .filter(|event| !event.pitches.is_empty() && event.pitches.iter().all(|pitch| *pitch == 36))
        .count();
    let hat_only_count = track
        .labels
        .events
        .iter()
        .filter(|event| {
            !event.pitches.is_empty()
                && event
                    .pitches
                    .iter()
                    .all(|pitch| [22, 26, 42, 44, 46].contains(pitch))
        })
        .count();
    InputResult {
        performance_id: &track.selection.id,
        metadata: MetadataResult {
            drummer: &track.selection.drummer,
            session: &track.selection.session,
            style: &track.selection.style,
            bpm: track.selection.bpm,
            beat_type: &track.selection.beat_type,
            time_signature: &track.selection.time_signature,
            declared_duration_seconds: track.selection.declared_duration,
            source_split: &track.selection.split,
            kit_name: &track.selection.kit_name,
        },
        midi: MidiResult {
            relative_path: &track.midi_relative,
            sha256: &track.midi_sha256,
            size_bytes: track.midi_size_bytes,
            raw_note_count: track.labels.raw_note_count,
        },
        audio: AudioResult {
            relative_path: &track.audio_relative,
            sha256: &track.audio_sha256,
            size_bytes: track.audio_size_bytes,
            sample_rate: track.wav.metadata.sample_rate,
            channels: track.wav.metadata.channels,
            bits_per_sample: track.wav.metadata.bits_per_sample,
            sample_count: track.wav.metadata.sample_count,
            duration_seconds: track.wav.metadata.duration_secs,
            peak_abs: track.peak_abs,
        },
        labels: LabelResult {
            event_count: track.labels.events.len(),
            single_note_event_count: track
                .labels
                .events
                .iter()
                .filter(|event| event.note_count == 1)
                .count(),
            multi_note_compound_count: track
                .labels
                .events
                .iter()
                .filter(|event| event.note_count > 1)
                .count(),
            pitch_histogram,
            velocity_histogram,
            kick_only_count,
            hat_only_count,
            kick_containing_count: track
                .labels
                .events
                .iter()
                .filter(|event| event.kick)
                .count(),
            hat_containing_count: track.labels.events.iter().filter(|event| event.hat).count(),
            dense_pair_30_to_50_ms_count: dense_pair_count,
        },
    }
}

fn fixed_evaluation_contract() -> EvaluationContract {
    EvaluationContract {
        event_timebase: "f64_seconds_from_midi_tempo_map",
        compound_cluster_span_ms_inclusive: 30.0,
        matching_tolerance_ms_inclusive: 25.0,
        matcher: "maximum_cardinality_then_minimum_total_absolute_error",
        percentile_method: "nearest_rank_ceil_p_times_n",
        pitch_mapping: "kick=36;hat=22,26,42,44,46",
        frame_edges: "zero_padded_center_grid_with_zero_state_warmup",
        macro_aggregation: "performance_id_counts_then_metrics",
    }
}

fn definition_hash(candidate: &CandidateArtifact, layout_hash: &str) -> Result<String, String> {
    let payload = serde_json::json!({
        "evaluator": EVALUATOR_VERSION,
        "candidate_semantic_sha256": candidate.semantic_sha256,
        "analyzer_layout_hash": layout_hash,
        "evaluation_contract": fixed_evaluation_contract(),
        "gates": {
            "precision_min": 0.85,
            "recall_min": 0.75,
            "f1_min": 0.80,
            "timing_p95_ms_max": 15.0,
            "fp_per_second_max": 1.0,
            "signed_median_abs_ms_max": 5.333,
            "kick_only_recall_min": 0.75,
            "hat_only_recall_min": 0.50
        }
    });
    serde_json::to_vec(&payload)
        .map(|bytes| sha256_bytes(&bytes))
        .map_err(|error| error.to_string())
}

fn atomic_create(path: &Path, bytes: &[u8]) -> Result<(), String> {
    if path.exists() {
        return Err(format!("result already exists: {}", path.display()));
    }
    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    if !parent.is_dir() {
        return Err(format!(
            "result parent is not a directory: {}",
            parent.display()
        ));
    }
    let file_name = path
        .file_name()
        .ok_or("result path has no file name")?
        .to_string_lossy();
    let temporary = parent.join(format!(".{file_name}.tmp.{}", std::process::id()));
    let write_result = (|| {
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary)
            .map_err(|error| format!("cannot create result temporary file: {error}"))?;
        file.write_all(bytes)
            .and_then(|()| file.sync_all())
            .map_err(|error| format!("cannot write result temporary file: {error}"))?;
        fs::hard_link(&temporary, path)
            .map_err(|error| format!("cannot publish result without overwrite: {error}"))?;
        Ok(())
    })();
    let _ = fs::remove_file(&temporary);
    write_result
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn atomic_result_never_overwrites_existing_file() {
        let directory = tempdir().unwrap();
        let path = directory.path().join("result.json");
        atomic_create(&path, b"first").unwrap();
        let error = atomic_create(&path, b"second").unwrap_err();
        assert!(error.contains("already exists"));
        assert_eq!(fs::read(path).unwrap(), b"first");
    }
}
