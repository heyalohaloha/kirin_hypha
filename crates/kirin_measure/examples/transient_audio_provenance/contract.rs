use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::ffi::OsString;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use crate::sha256_bytes;

pub(crate) const PURPOSE: &str = "audio-archive-provenance";
pub(crate) const PROFILE: &str = "DRUM";
pub(crate) const DATASET_ID: &str = "E-GMD";
pub(crate) const DATASET_VERSION: &str = "1.0.0";
pub(crate) const OUTPUT_SCHEMA: &str = "kirin-hypha-attack-audio-archive-member-receipt-v1";
pub(crate) const ARCHIVE_MEMBER_PREFIX: &str = "e-gmd-v1.0.0/";
pub(crate) const PINNED_MANIFEST_SHA256: &str =
    "80ebe2961ece9833f554f98430e6617aad2496603f0c105c611dfa710938ad8c";
pub(crate) const PINNED_FOLDS_SHA256: &str =
    "51c0b30d535a2819dd17b0a49e410c378e2d436eb5f98be06caff5757be45675";
pub(crate) const PINNED_DEVELOPMENT_RECEIPT_SHA256: &str =
    "57fcd65d2b9f265796ba2142f367a89bd932e1cefd73c76f09e96d743963153a";
pub(crate) const PINNED_MIDI_RECEIPT_SHA256: &str =
    "7c923cf224f8201d0496c304cb160b0cc8859340cdb0b74c7b490b3cd6223447";
pub(crate) const PINNED_MIDI_ARCHIVE_SHA256: &str =
    "5e70a6f4d760385a5e5ec986a2f02179d96f61181a920e592876b577a75844d3";
pub(crate) const PINNED_AUDIO_ARCHIVE_SHA256: &str =
    "7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053";
pub(crate) const PINNED_AUDIO_ARCHIVE_BYTES: u64 = 96_422_999_145;
pub(crate) const PINNED_ARCHIVE_ENTRIES: usize = 91_108;
pub(crate) const PINNED_CENTRAL_OFFSET: u64 = 96_409_831_470;
pub(crate) const PINNED_CENTRAL_SIZE: u64 = 13_167_577;
pub(crate) const PINNED_FULL_LAYOUT_LEDGER_SHA256: &str =
    "6eee9c9f18e1b90355ea84c75ab5fe96f54b67ff8220a41e24bce16807ba61bf";
pub(crate) const PINNED_AUDIO_SELECTED_UNCOMPRESSED_BYTES: u64 = 774_062_982;
pub(crate) const PINNED_AUDIO_SELECTED_COMPRESSED_BYTES: u64 = 521_284_272;
pub(crate) const PINNED_AUDIO_SELECTED_LEDGER_SHA256: &str =
    "c41101704bff120f56b28fcb3032f9fcfdf5f598f288b0682809c145fbb1caa7";
pub(crate) const PINNED_MIDI_SELECTED_UNCOMPRESSED_BYTES: u64 = 932_889;
pub(crate) const PINNED_MIDI_SELECTED_COMPRESSED_BYTES: u64 = 546_934;
pub(crate) const PINNED_MIDI_SELECTED_LEDGER_SHA256: &str =
    "bc83ab4c62358ac2a06bb37f5b16d6180614e41093ff1024adca675bbdfe3018";
pub(crate) const PINNED_DECLARED_SOURCE_SAMPLES: u64 = 387_025_111;
pub(crate) const PINNED_CORE_SAMPLES: u64 = 136_801_333;
pub(crate) const DURATION_TOLERANCE_SAMPLES: u64 = 441;
pub(crate) const MIDI_RANGE_TOLERANCE_MICROS: u64 = 2_000;
pub(crate) const PINNED_MANIFEST_ROWS: usize = 290;
pub(crate) const PINNED_TRAIN_ROWS: usize = 246;
pub(crate) const PINNED_VALIDATION_ROWS: usize = 44;

const MAX_MIDI_RECEIPT_BYTES: u64 = 2 * 1024 * 1024;

#[derive(Clone, Debug)]
pub(crate) struct Cli {
    pub(crate) manifest: PathBuf,
    pub(crate) development_receipt: PathBuf,
    pub(crate) midi_receipt: PathBuf,
    pub(crate) audio_archive: PathBuf,
    pub(crate) result: PathBuf,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct VerifiedParents {
    pub(crate) development_receipt_sha256: String,
    pub(crate) development_manifest_sha256: String,
    pub(crate) development_folds_sha256: String,
    pub(crate) midi_receipt_sha256: String,
    pub(crate) midi_archive_sha256: String,
    pub(crate) midi_members: Vec<VerifiedMidiMember>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct VerifiedMidiMember {
    pub(crate) selection_rank: usize,
    pub(crate) performance_id: String,
    pub(crate) manifest_midi_filename: String,
    pub(crate) member_sha256: String,
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
                    | "--midi-receipt"
                    | "--audio-archive"
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
        let required_text = |flag: &str| -> Result<String, String> {
            values
                .get(flag)
                .ok_or_else(|| format!("missing {flag}"))?
                .clone()
                .into_string()
                .map_err(|_| format!("{flag} must be UTF-8"))
        };
        if required_text("--purpose")? != PURPOSE {
            return Err(format!("purpose must be {PURPOSE}"));
        }
        if required_text("--profile")? != PROFILE {
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
            midi_receipt: path("--midi-receipt")?,
            audio_archive: path("--audio-archive")?,
            result: path("--result")?,
        };
        let inputs = [
            &cli.manifest,
            &cli.development_receipt,
            &cli.midi_receipt,
            &cli.audio_archive,
        ];
        if inputs.into_iter().any(|input| input == &cli.result) {
            return Err("result path must differ from every input path".to_string());
        }
        Ok(cli)
    }
}

pub(crate) fn verify_parent_chain(
    development_receipt: &Path,
    midi_receipt: &Path,
) -> Result<VerifiedParents, String> {
    let development = crate::development_contract::verify_development_receipt(development_receipt)?;
    let bytes = read_bounded(midi_receipt, MAX_MIDI_RECEIPT_BYTES, "B-551 MIDI receipt")?;
    let midi_receipt_sha256 = sha256_bytes(&bytes);
    if midi_receipt_sha256 != PINNED_MIDI_RECEIPT_SHA256 {
        return Err("MIDI receipt does not match the source-pinned B-551 bytes".to_string());
    }
    let midi_members = verify_midi_receipt_semantics(&bytes)?;
    if development.receipt_sha256 != PINNED_DEVELOPMENT_RECEIPT_SHA256
        || development.manifest_sha256 != PINNED_MANIFEST_SHA256
        || development.folds_sha256 != PINNED_FOLDS_SHA256
        || development.archive_sha256 != PINNED_MIDI_ARCHIVE_SHA256
    {
        return Err("B-550 parent evidence differs from the B-552 source pins".to_string());
    }
    Ok(VerifiedParents {
        development_receipt_sha256: development.receipt_sha256,
        development_manifest_sha256: development.manifest_sha256,
        development_folds_sha256: development.folds_sha256,
        midi_receipt_sha256,
        midi_archive_sha256: development.archive_sha256,
        midi_members,
    })
}

pub(crate) fn read_manifest_bytes(path: &Path) -> Result<Vec<u8>, String> {
    crate::development_contract::read_manifest_bytes(path)
}

fn verify_midi_receipt_semantics(bytes: &[u8]) -> Result<Vec<VerifiedMidiMember>, String> {
    let value: serde_json::Value = serde_json::from_slice(bytes)
        .map_err(|error| format!("invalid B-551 MIDI receipt: {error}"))?;
    let object = value
        .as_object()
        .ok_or("B-551 MIDI receipt must be a JSON object")?;
    let keys = object.keys().map(String::as_str).collect::<BTreeSet<_>>();
    let expected = BTreeSet::from([
        "schema",
        "purpose",
        "profile",
        "authorization",
        "parents",
        "archive",
        "contracts",
        "members",
        "aggregate",
        "duplicate_audit",
        "isolation",
        "downstream_blockers",
    ]);
    if keys != expected
        || value["schema"] != "kirin-hypha-attack-midi-archive-member-receipt-v1"
        || value["purpose"] != "midi-archive-provenance"
        || value["profile"] != PROFILE
        || value["authorization"]["component_verified"] != true
        || value["authorization"]["overall_formal_authorization"] != false
        || value["authorization"]["formal_scoring_allowed"] != false
        || value["authorization"]["winner_allowed"] != false
        || value["parents"]["development_receipt_sha256"] != PINNED_DEVELOPMENT_RECEIPT_SHA256
        || value["parents"]["development_manifest_sha256"] != PINNED_MANIFEST_SHA256
        || value["parents"]["development_folds_sha256"] != PINNED_FOLDS_SHA256
        || value["parents"]["midi_archive_sha256"] != PINNED_MIDI_ARCHIVE_SHA256
        || value["archive"]["sha256"] != PINNED_MIDI_ARCHIVE_SHA256
        || value["archive"]["selected_member_count"] != PINNED_MANIFEST_ROWS
        || value["aggregate"]["members"] != PINNED_MANIFEST_ROWS
        || value["isolation"]["selected_members_decompressed_and_semantically_parsed"]
            != PINNED_MANIFEST_ROWS
        || value["isolation"]["audio_opened"] != false
        || value["isolation"]["fresh_holdout_opened"] != false
        || value["isolation"]["two_mix_opened"] != false
        || value["duplicate_audit"]["raw_members"]["duplicate_group_count"] != 0
        || value["duplicate_audit"]["source_canonical_composite"]["duplicate_group_count"] != 0
        || value["duplicate_audit"]["nonempty_excerpt_canonical_composite"]["duplicate_group_count"]
            != 0
        || value["duplicate_audit"]["cross_split_duplicate_groups"] != 0
    {
        return Err("B-551 MIDI receipt semantic contract mismatch".to_string());
    }
    let members = value["members"]
        .as_array()
        .ok_or("B-551 MIDI receipt members must be an array")?;
    if members.len() != PINNED_MANIFEST_ROWS {
        return Err("B-551 MIDI receipt member count mismatch".to_string());
    }
    members
        .iter()
        .enumerate()
        .map(|(index, member)| {
            let rank = exact_usize(member, "selection_rank")?;
            let performance_id = exact_string(member, "performance_id")?;
            let filename = exact_string(member, "manifest_midi_filename")?;
            let digest = exact_string(member, "member_sha256")?;
            if rank != index + 1
                || performance_id.is_empty()
                || !safe_midi_name(&filename)
                || !is_sha256(&digest)
                || member["manifest_midi_sha256"] != digest
                || !matches!(member["split"].as_str(), Some("train" | "validation"))
                || member["fold"].as_u64().filter(|fold| *fold < 5).is_none()
            {
                return Err(format!(
                    "invalid B-551 member binding at selection rank {}",
                    index + 1
                ));
            }
            Ok(VerifiedMidiMember {
                selection_rank: rank,
                performance_id,
                manifest_midi_filename: filename,
                member_sha256: digest,
            })
        })
        .collect()
}

fn exact_usize(value: &serde_json::Value, field: &str) -> Result<usize, String> {
    value[field]
        .as_u64()
        .and_then(|number| usize::try_from(number).ok())
        .ok_or_else(|| format!("B-551 member {field} must be an integer"))
}

fn exact_string(value: &serde_json::Value, field: &str) -> Result<String, String> {
    value[field]
        .as_str()
        .map(str::to_string)
        .ok_or_else(|| format!("B-551 member {field} must be a string"))
}

fn safe_midi_name(value: &str) -> bool {
    value.ends_with(".midi")
        && !value.contains(['\\', '\0', ':'])
        && !value.starts_with('/')
        && value
            .split('/')
            .all(|part| !part.is_empty() && !matches!(part, "." | ".."))
}

fn is_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || matches!(byte, b'a'..=b'f'))
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
    let length = usize::try_from(length).map_err(|_| format!("{label} is too large"))?;
    let mut bytes = vec![0_u8; length];
    file.read_exact(&mut bytes)
        .map_err(|error| format!("cannot read {label}: {error}"))?;
    let mut trailing = [0_u8; 1];
    if file
        .read(&mut trailing)
        .map_err(|error| format!("cannot finish {label}: {error}"))?
        != 0
    {
        return Err(format!("{label} changed while being read"));
    }
    Ok(bytes)
}

#[cfg(test)]
#[path = "contract_tests.rs"]
mod tests;
