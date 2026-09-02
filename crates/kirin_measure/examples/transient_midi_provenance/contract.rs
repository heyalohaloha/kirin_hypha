use std::collections::BTreeMap;
use std::env;
use std::ffi::OsString;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use serde::Deserialize;

use crate::sha256_bytes;

pub(crate) const PURPOSE: &str = "midi-archive-provenance";
pub(crate) const PROFILE: &str = "DRUM";
pub(crate) const DATASET_ID: &str = "E-GMD";
pub(crate) const DATASET_VERSION: &str = "1.0.0";
pub(crate) const DEVELOPMENT_RECEIPT_SCHEMA: &str =
    "kirin-hypha-attack-drum-development-receipt-v2";
pub(crate) const OUTPUT_SCHEMA: &str = "kirin-hypha-attack-midi-archive-member-receipt-v1";
pub(crate) const PINNED_MANIFEST_SHA256: &str =
    "80ebe2961ece9833f554f98430e6617aad2496603f0c105c611dfa710938ad8c";
pub(crate) const PINNED_DEVELOPMENT_RECEIPT_SHA256: &str =
    "57fcd65d2b9f265796ba2142f367a89bd932e1cefd73c76f09e96d743963153a";
pub(crate) const PINNED_MIDI_ARCHIVE_SHA256: &str =
    "5e70a6f4d760385a5e5ec986a2f02179d96f61181a920e592876b577a75844d3";
pub(crate) const PINNED_METADATA_SHA256: &str =
    "80677e8fb00e973f33cb91ddaaf7f0cffe55359f9a76c1833ce56c84d1d92c64";
pub(crate) const PINNED_FOLDS_SHA256: &str =
    "51c0b30d535a2819dd17b0a49e410c378e2d436eb5f98be06caff5757be45675";
pub(crate) const PINNED_OPENED_LEDGER_SHA256: &str =
    "e9935efba336a40b44ba46bcaa234117927c05e36bb5ca8f80f257b8ba58b3ca";
pub(crate) const PINNED_ISOLATION_INCIDENT_SHA256: &str =
    "27c10a5c6de848029deb18c394181be76ed373d67269773f519708cf53494257";
pub(crate) const PINNED_MIDI_ARCHIVE_BYTES: u64 = 107_076_192;
pub(crate) const PINNED_ARCHIVE_ENTRIES: usize = 45_571;
pub(crate) const PINNED_MANIFEST_ROWS: usize = 290;
pub(crate) const PINNED_TRAIN_ROWS: usize = 246;
pub(crate) const PINNED_VALIDATION_ROWS: usize = 44;

const MAX_MANIFEST_BYTES: u64 = 2 * 1024 * 1024;
const MAX_RECEIPT_BYTES: u64 = 2 * 1024 * 1024;

#[derive(Clone, Debug)]
pub(crate) struct Cli {
    pub(crate) manifest: PathBuf,
    pub(crate) development_receipt: PathBuf,
    pub(crate) midi_archive: PathBuf,
    pub(crate) result: PathBuf,
}

#[derive(Clone, Debug)]
pub(crate) struct ParentEvidence {
    pub(crate) receipt_sha256: String,
    pub(crate) manifest_sha256: String,
    pub(crate) folds_sha256: String,
    pub(crate) archive_sha256: String,
    pub(crate) source_raw_notes: usize,
    pub(crate) source_compound_events: usize,
    pub(crate) source_kick_only_events: usize,
    pub(crate) source_hat_only_events: usize,
}

impl Cli {
    pub(crate) fn parse_env() -> Result<Self, String> {
        Self::parse(env::args_os().skip(1))
    }

    pub(crate) fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut values = BTreeMap::<String, OsString>::new();
        let mut arguments = arguments.into_iter();
        while let Some(flag) = arguments.next() {
            let flag = flag
                .into_string()
                .map_err(|_| "argument flags must be UTF-8".to_string())?;
            if !matches!(
                flag.as_str(),
                "--purpose"
                    | "--profile"
                    | "--manifest"
                    | "--development-receipt"
                    | "--midi-archive"
                    | "--result"
            ) {
                return Err(format!("unknown argument: {flag}"));
            }
            let value = arguments
                .next()
                .ok_or_else(|| format!("missing value for {flag}"))?;
            if values.insert(flag.clone(), value).is_some() {
                return Err(format!("duplicate argument: {flag}"));
            }
        }
        let text = |flag: &str| -> Result<String, String> {
            values
                .get(flag)
                .ok_or_else(|| format!("missing {flag}"))?
                .clone()
                .into_string()
                .map_err(|_| format!("{flag} must be UTF-8"))
        };
        if text("--purpose")? != PURPOSE {
            return Err(format!("purpose must be {PURPOSE}"));
        }
        if text("--profile")? != PROFILE {
            return Err(format!("profile must be {PROFILE}"));
        }
        let path = |flag: &str| -> Result<PathBuf, String> {
            values
                .get(flag)
                .cloned()
                .map(PathBuf::from)
                .ok_or_else(|| format!("missing {flag}"))
        };
        let cli = Self {
            manifest: path("--manifest")?,
            development_receipt: path("--development-receipt")?,
            midi_archive: path("--midi-archive")?,
            result: path("--result")?,
        };
        if cli.result == cli.manifest
            || cli.result == cli.development_receipt
            || cli.result == cli.midi_archive
        {
            return Err("result path must differ from every input path".to_string());
        }
        Ok(cli)
    }
}

pub(crate) fn verify_development_receipt(path: &Path) -> Result<ParentEvidence, String> {
    let bytes = read_bounded(path, MAX_RECEIPT_BYTES, "development receipt")?;
    let receipt_sha256 = sha256_bytes(&bytes);
    if receipt_sha256 != PINNED_DEVELOPMENT_RECEIPT_SHA256 {
        return Err("development receipt does not match the source-pinned B-550 bytes".to_string());
    }
    let receipt: DevelopmentReceipt = serde_json::from_slice(&bytes)
        .map_err(|error| format!("invalid development receipt: {error}"))?;
    verify_nested_shape(&bytes)?;
    receipt.verify()?;
    Ok(ParentEvidence {
        receipt_sha256,
        manifest_sha256: receipt.artifacts.manifest_sha256,
        folds_sha256: receipt.artifacts.folds_sha256,
        archive_sha256: receipt.dataset.midi_archive_sha256,
        source_raw_notes: receipt.source_midi_diagnostic.source_raw_notes,
        source_compound_events: receipt.source_midi_diagnostic.source_compound_events,
        source_kick_only_events: receipt.source_midi_diagnostic.source_kick_only_events,
        source_hat_only_events: receipt.source_midi_diagnostic.source_hat_only_events,
    })
}

pub(crate) fn read_manifest_bytes(path: &Path) -> Result<Vec<u8>, String> {
    let bytes = read_bounded(path, MAX_MANIFEST_BYTES, "development manifest")?;
    if sha256_bytes(&bytes) != PINNED_MANIFEST_SHA256 {
        return Err("manifest does not match the source-pinned B-550 bytes".to_string());
    }
    Ok(bytes)
}

fn read_bounded(path: &Path, maximum: u64, label: &str) -> Result<Vec<u8>, String> {
    let mut file = File::open(path).map_err(|error| format!("cannot open {label}: {error}"))?;
    let length = file
        .metadata()
        .map_err(|error| format!("cannot inspect {label}: {error}"))?
        .len();
    if length == 0 || length > maximum {
        return Err(format!("{label} byte length is outside the fixed bound"));
    }
    let capacity = usize::try_from(length).map_err(|_| format!("{label} is too large"))?;
    let mut bytes = vec![0_u8; capacity];
    file.read_exact(&mut bytes)
        .map_err(|error| format!("cannot read {label}: {error}"))?;
    let mut extra = [0_u8; 1];
    if file
        .read(&mut extra)
        .map_err(|error| format!("cannot finish {label}: {error}"))?
        != 0
    {
        return Err(format!("{label} changed while being read"));
    }
    Ok(bytes)
}

#[derive(Deserialize)]
#[allow(dead_code)]
#[serde(deny_unknown_fields)]
struct DevelopmentReceipt {
    schema: String,
    purpose: String,
    profile: String,
    dataset: Dataset,
    artifacts: Artifacts,
    assessment: Assessment,
    source_midi_diagnostic: SourceMidiDiagnostic,
    excerpt: Excerpt,
    selection: Selection,
    folds: Folds,
    candidate_evaluation_allowed: bool,
    winner_allowed: bool,
    winner_blockers: Vec<String>,
    legacy_v1_is_formal_input: bool,
    metadata: serde_json::Value,
    required_drummers: serde_json::Value,
    policy: serde_json::Value,
    blind_acoustic_audit: serde_json::Value,
    grid: serde_json::Value,
    this_selector_run: serde_json::Value,
}

impl DevelopmentReceipt {
    fn verify(&self) -> Result<(), String> {
        if self.schema != DEVELOPMENT_RECEIPT_SCHEMA
            || self.purpose != "development-selection"
            || self.profile != PROFILE
            || self.candidate_evaluation_allowed
            || self.winner_allowed
            || self.legacy_v1_is_formal_input
            || self.dataset.id != DATASET_ID
            || self.dataset.version != DATASET_VERSION
            || self.dataset.metadata_sha256 != PINNED_METADATA_SHA256
            || self.dataset.midi_archive_sha256 != PINNED_MIDI_ARCHIVE_SHA256
            || !self.dataset.midi_archive_verified
            || !self.dataset.selected_midi_files_hashed
            || self.dataset.midi_root_archive_provenance_verified
            || self.dataset.expected_audio_archive_sha256
                != "7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053"
            || self.dataset.audio_archive_verified
            || self.dataset.audio_hash_duplicate_check != "pending_until_audio_ingest"
            || self.dataset.opened_ledger_sha256 != PINNED_OPENED_LEDGER_SHA256
            || self.dataset.test_isolation_incident_sha256 != PINNED_ISOLATION_INCIDENT_SHA256
            || !self.dataset.original_fresh_holdout_isolation_breached
            || self.dataset.opened_ledger_ids != 24
            || self.dataset.opened_validation_required_ids != 6
            || self.dataset.opened_test_diagnostic_only_ids != 18
            || self.artifacts.manifest_sha256 != PINNED_MANIFEST_SHA256
            || self.artifacts.manifest_path != "attack_drum_development_manifest_v2.csv"
            || self.artifacts.manifest_rows != PINNED_MANIFEST_ROWS
            || self.artifacts.manifest_schema_columns != 23
            || self.artifacts.folds_path != "attack_drum_development_folds_v2.csv"
            || self.artifacts.folds_sha256 != PINNED_FOLDS_SHA256
            || self.artifacts.folds_rows != PINNED_MANIFEST_ROWS
            || self.assessment.status != "development_midi_quotas_ready"
            || self.assessment.winner_allowed
            || self.assessment.unique_performance_ids != PINNED_MANIFEST_ROWS
            || self.assessment.unique_duration_samples_44100 != 136_801_333
            || self.assessment.kick_only_events != 1_342
            || self.assessment.hat_only_events != 3_471
            || self.source_midi_diagnostic.source_duration_samples_44100 != 387_025_111
            || self.source_midi_diagnostic.source_raw_notes != 77_161
            || self.source_midi_diagnostic.source_compound_events != 53_868
            || self.source_midi_diagnostic.source_kick_only_events != 4_499
            || self.source_midi_diagnostic.source_hat_only_events != 9_070
            || self.selection.fixed_target_performance_ids != PINNED_MANIFEST_ROWS
            || self.excerpt.mapping_version != crate::drum_excerpt::EXCERPT_MAPPING_VERSION
            || self.excerpt.hash_domain != crate::drum_excerpt::EXCERPT_WINDOW_DOMAIN
            || self.excerpt.seed != crate::drum_excerpt::EXCERPT_WINDOW_SEED
            || self.excerpt.sample_rate != crate::drum_excerpt::EXCERPT_SAMPLE_RATE
            || self.excerpt.maximum_window_samples != crate::drum_excerpt::EXCERPT_CAP_SAMPLES
            || self.excerpt.start_quantum_samples
                != crate::drum_excerpt::EXCERPT_START_QUANTUM_SAMPLES
            || self.folds.count != 5
            || self.folds.unit != "performance_id"
            || self.folds.algorithm_version != "attack-drum-balanced-excerpt-folds-v2"
            || self.folds.qualification.status != "fold_balance_qualified"
            || !self.folds.qualification.qualified
        {
            return Err("development receipt semantic contract mismatch".to_string());
        }
        for blocker in [
            "midi_archive_member_provenance_unverified",
            "canonical_excerpt_label_duplicate_check_pending",
            "candidate_scores_not_run",
        ] {
            if !self.winner_blockers.iter().any(|value| value == blocker) {
                return Err(format!("development receipt is missing blocker: {blocker}"));
            }
        }
        Ok(())
    }
}

fn verify_nested_shape(bytes: &[u8]) -> Result<(), String> {
    let value: serde_json::Value = serde_json::from_slice(bytes)
        .map_err(|error| format!("invalid development receipt JSON: {error}"))?;
    for (path, expected) in [
        (
            "excerpt",
            &[
                "duration_conversion",
                "empty_excerpt_policy",
                "hash_domain",
                "interval",
                "mapping_formula",
                "mapping_version",
                "maximum_window_samples",
                "note_inclusion_formula",
                "sample_rate",
                "sampling_audit",
                "seed",
                "source_duration_sample_binding_contract",
                "source_duration_sample_binding_sha256",
                "start_quantum_samples",
            ][..],
        ),
        (
            "folds.qualification",
            &["audit", "deficits", "policy", "qualified", "status"][..],
        ),
    ] {
        let object = path.split('.').try_fold(&value, |current, component| {
            current
                .get(component)
                .ok_or_else(|| format!("development receipt is missing {path}"))
        })?;
        let keys = object
            .as_object()
            .ok_or_else(|| format!("development receipt {path} must be an object"))?
            .keys()
            .map(String::as_str)
            .collect::<std::collections::BTreeSet<_>>();
        let expected = expected
            .iter()
            .copied()
            .collect::<std::collections::BTreeSet<_>>();
        if keys != expected {
            return Err(format!("development receipt {path} field set mismatch"));
        }
    }
    Ok(())
}

#[derive(Deserialize)]
#[allow(dead_code)]
#[serde(deny_unknown_fields)]
struct Dataset {
    id: String,
    version: String,
    metadata_sha256: String,
    midi_archive_sha256: String,
    midi_archive_verified: bool,
    selected_midi_files_hashed: bool,
    midi_root_archive_provenance_verified: bool,
    expected_audio_archive_sha256: String,
    audio_archive_verified: bool,
    audio_hash_duplicate_check: String,
    opened_ledger_sha256: String,
    test_isolation_incident_sha256: String,
    original_fresh_holdout_isolation_breached: bool,
    opened_sources: serde_json::Value,
    opened_ledger_ids: usize,
    opened_validation_required_ids: usize,
    opened_test_diagnostic_only_ids: usize,
}

#[derive(Deserialize)]
#[allow(dead_code)]
#[serde(deny_unknown_fields)]
struct Artifacts {
    manifest_path: String,
    manifest_schema_columns: usize,
    manifest_sha256: String,
    manifest_rows: usize,
    folds_path: String,
    folds_sha256: String,
    folds_rows: usize,
    exclusion_parts: serde_json::Value,
    reserve_parts: serde_json::Value,
}

#[derive(Deserialize)]
#[allow(dead_code)]
#[serde(deny_unknown_fields)]
struct Assessment {
    status: String,
    winner_allowed: bool,
    unique_performance_ids: usize,
    unique_duration_samples_44100: u64,
    unique_duration_secs: f64,
    beat_ids: usize,
    fill_ids: usize,
    beat_ratio: f64,
    fill_ratio: f64,
    styles: usize,
    kits: usize,
    drummers: usize,
    required_drummers: usize,
    kick_only_events: usize,
    hat_only_events: usize,
    forced_opened_validation_ids: usize,
    deficits: serde_json::Value,
}

#[derive(Deserialize)]
#[allow(dead_code)]
#[serde(deny_unknown_fields)]
struct SourceMidiDiagnostic {
    source_duration_samples_44100: u64,
    source_duration_secs: f64,
    source_raw_notes: usize,
    source_compound_events: usize,
    source_kick_only_events: usize,
    source_hat_only_events: usize,
}

#[derive(Deserialize)]
struct Excerpt {
    mapping_version: String,
    hash_domain: String,
    seed: String,
    sample_rate: u32,
    maximum_window_samples: u64,
    start_quantum_samples: u64,
}

#[derive(Deserialize)]
struct Selection {
    fixed_target_performance_ids: usize,
}

#[derive(Deserialize)]
struct Folds {
    count: usize,
    unit: String,
    algorithm_version: String,
    qualification: Qualification,
}

#[derive(Deserialize)]
struct Qualification {
    status: String,
    qualified: bool,
}

#[cfg(test)]
#[path = "contract_tests.rs"]
mod tests;
