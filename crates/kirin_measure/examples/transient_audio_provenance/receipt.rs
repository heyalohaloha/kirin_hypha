use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::Path;

use serde::{Serialize, Serializer};

use crate::contract::{
    ARCHIVE_MEMBER_PREFIX, DATASET_ID, DATASET_VERSION, DURATION_TOLERANCE_SAMPLES,
    MIDI_RANGE_TOLERANCE_MICROS, OUTPUT_SCHEMA, PINNED_ARCHIVE_ENTRIES, PINNED_AUDIO_ARCHIVE_BYTES,
    PINNED_AUDIO_ARCHIVE_SHA256, PINNED_AUDIO_SELECTED_COMPRESSED_BYTES,
    PINNED_AUDIO_SELECTED_LEDGER_SHA256, PINNED_AUDIO_SELECTED_UNCOMPRESSED_BYTES,
    PINNED_CENTRAL_OFFSET, PINNED_CENTRAL_SIZE, PINNED_CORE_SAMPLES,
    PINNED_DECLARED_SOURCE_SAMPLES, PINNED_DEVELOPMENT_RECEIPT_SHA256, PINNED_FOLDS_SHA256,
    PINNED_FULL_LAYOUT_LEDGER_SHA256, PINNED_MANIFEST_ROWS, PINNED_MANIFEST_SHA256,
    PINNED_MIDI_ARCHIVE_SHA256, PINNED_MIDI_RECEIPT_SHA256, PINNED_MIDI_SELECTED_COMPRESSED_BYTES,
    PINNED_MIDI_SELECTED_LEDGER_SHA256, PINNED_MIDI_SELECTED_UNCOMPRESSED_BYTES, PINNED_TRAIN_ROWS,
    PINNED_VALIDATION_ROWS, PROFILE, PURPOSE,
};
use crate::sha256_bytes;

#[path = "receipt_audit.rs"]
mod audit;
use audit::{
    aggregate, validate_aggregate, validate_archive, validate_members, validate_parent, Aggregate,
};
#[path = "receipt_content_audit.rs"]
mod content_audit;
use content_audit::{
    audit_duplicates, audit_midi_binding, audit_silence, downstream_blockers, DuplicateAudit,
    MidiBindingAudit, SilenceAudit,
};

const B550_SOURCE_COMMIT: &str = "72cf4d1e34199b2c4294f049e7f0ba0984865d7f";
const B551_SOURCE_COMMIT: &str = "84d9cf5bd6e9f3ae16a5c95a5e5c4e2acb411280";
const FULL_LAYOUT_LEDGER_SOURCE_PINNED: bool = true;
const PINNED_METADATA_SHA256: &str =
    "80677e8fb00e973f33cb91ddaaf7f0cffe55359f9a76c1833ce56c84d1d92c64";
const PINNED_OPENED_LEDGER_SHA256: &str =
    "e9935efba336a40b44ba46bcaa234117927c05e36bb5ca8f80f257b8ba58b3ca";
const PINNED_ISOLATION_INCIDENT_SHA256: &str =
    "27c10a5c6de848029deb18c394181be76ed373d67269773f519708cf53494257";
pub(crate) const SOURCE_PCM_DOMAIN: &str = "attack-drum-source-pcm-signed24-v1";
pub(crate) const CORE_PCM_DOMAIN: &str = "attack-drum-core-pcm-signed24-v1";
pub(crate) const GUARD_PCM_DOMAIN: &str = "attack-drum-maximum-context-pcm-signed24-v1";

#[derive(Clone, Debug)]
pub(crate) struct ParentEvidence {
    pub(crate) development_receipt_sha256: String,
    pub(crate) development_manifest_sha256: String,
    pub(crate) development_folds_sha256: String,
    pub(crate) midi_receipt_sha256: String,
    pub(crate) midi_archive_sha256: String,
}

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum DevelopmentSplit {
    Train,
    Validation,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum MemberCompression {
    Stored,
    Deflated,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum AudioContainer {
    RiffWave,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum SampleEncoding {
    SignedLinearPcmIntegerLittleEndian,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
pub(crate) struct DecodeEvidence {
    pub(crate) container: AudioContainer,
    pub(crate) encoding: SampleEncoding,
    pub(crate) channels: u16,
    pub(crate) sample_rate_hz: u32,
    pub(crate) bits_per_sample: u16,
    pub(crate) actual_samples: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
pub(crate) struct PcmStatistics {
    pub(crate) zero_samples: u64,
    pub(crate) minimum_pcm24: i32,
    pub(crate) maximum_pcm24: i32,
    pub(crate) peak_abs_pcm24: u32,
    #[serde(serialize_with = "serialize_u128_decimal")]
    pub(crate) sum_squares_pcm24: u128,
}

fn serialize_u128_decimal<S>(value: &u128, serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    serializer.serialize_str(&value.to_string())
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub(crate) struct PcmRegionEvidence {
    pub(crate) start_sample_44100: u64,
    pub(crate) end_sample_44100: u64,
    pub(crate) samples: u64,
    pub(crate) canonical_sha256: String,
    pub(crate) statistics: PcmStatistics,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct BundledMidiEvidence {
    pub(crate) archive_member_name: String,
    pub(crate) b551_member_sha256: String,
    pub(crate) member_sha256: String,
    pub(crate) uncompressed_bytes: u64,
    pub(crate) compressed_bytes: u64,
    pub(crate) crc32: u32,
    pub(crate) compression: MemberCompression,
    pub(crate) first_note_micros: u64,
    pub(crate) last_note_micros: u64,
    pub(crate) annotation_bounds_pass: bool,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MemberEvidence {
    pub(crate) selection_rank: usize,
    pub(crate) selection_key: String,
    pub(crate) fold: u8,
    pub(crate) drummer: String,
    pub(crate) session: String,
    pub(crate) performance_id: String,
    pub(crate) split: DevelopmentSplit,
    pub(crate) manifest_audio_filename: String,
    pub(crate) manifest_midi_filename: String,
    pub(crate) archive_member_name: String,
    pub(crate) declared_source_samples_44100: u64,
    pub(crate) manifest_core_start_sample_44100: u64,
    pub(crate) manifest_core_end_sample_44100: u64,
    pub(crate) member_sha256: String,
    pub(crate) uncompressed_bytes: u64,
    pub(crate) compressed_bytes: u64,
    pub(crate) crc32: u32,
    pub(crate) compression: MemberCompression,
    pub(crate) decode: DecodeEvidence,
    pub(crate) source_pcm: PcmRegionEvidence,
    pub(crate) core_pcm: PcmRegionEvidence,
    pub(crate) guard_pcm: PcmRegionEvidence,
    pub(crate) bundled_midi: BundledMidiEvidence,
}

#[derive(Clone, Debug)]
pub(crate) struct ArchiveReceiptInput {
    pub(crate) archive_sha256: String,
    pub(crate) archive_bytes: u64,
    pub(crate) central_directory_entries: usize,
    pub(crate) central_directory_offset: u64,
    pub(crate) central_directory_size: u64,
    pub(crate) authenticated_local_header_count: usize,
    pub(crate) overlapping_payload_ranges: usize,
    pub(crate) full_layout_ledger_sha256: String,
    pub(crate) selected_audio_member_count: usize,
    pub(crate) selected_audio_uncompressed_bytes: u64,
    pub(crate) selected_audio_compressed_bytes: u64,
    pub(crate) selected_audio_ledger_sha256: String,
    pub(crate) selected_midi_member_count: usize,
    pub(crate) selected_midi_uncompressed_bytes: u64,
    pub(crate) selected_midi_compressed_bytes: u64,
    pub(crate) selected_midi_ledger_sha256: String,
    pub(crate) member_name_prefix: String,
}

pub(crate) struct ReceiptInput<'a> {
    pub(crate) parent: &'a ParentEvidence,
    pub(crate) archive: ArchiveReceiptInput,
    pub(crate) members: &'a [MemberEvidence],
}

#[derive(Serialize)]
struct Receipt<'a> {
    schema: &'static str,
    purpose: &'static str,
    profile: &'static str,
    authorization: Authorization,
    parents: Parents<'a>,
    archive: Archive,
    contracts: Contracts,
    members: Vec<&'a MemberEvidence>,
    aggregate: Aggregate,
    midi_binding_audit: MidiBindingAudit,
    duplicate_audit: DuplicateAudit,
    silence_audit: SilenceAudit,
    isolation: IsolationAssertions,
    downstream_blockers: Vec<&'static str>,
}

#[derive(Serialize)]
struct Authorization {
    component: &'static str,
    component_verified: bool,
    full_layout_ledger_source_pinned: bool,
    overall_formal_authorization: bool,
    formal_scoring_allowed: bool,
    context_evaluator_ready: bool,
    winner_allowed: bool,
    selection_replacement_allowed: bool,
}

#[derive(Serialize)]
struct Parents<'a> {
    b550_source_commit: &'static str,
    b551_source_commit: &'static str,
    development_receipt_sha256: &'a str,
    development_manifest_sha256: &'a str,
    development_folds_sha256: &'a str,
    midi_receipt_sha256: &'a str,
    midi_archive_sha256: &'a str,
    official_metadata_sha256: &'static str,
    opened_set_ledger_sha256: &'static str,
    test_isolation_incident_sha256: &'static str,
}

#[derive(Serialize)]
struct Archive {
    dataset_id: &'static str,
    dataset_version: &'static str,
    sha256: String,
    bytes: u64,
    central_directory_entries: usize,
    central_directory_offset: u64,
    central_directory_size: u64,
    authenticated_local_header_count: usize,
    overlapping_payload_ranges: usize,
    full_layout_ledger_sha256: String,
    selected_audio_member_count: usize,
    selected_audio_uncompressed_bytes: u64,
    selected_audio_compressed_bytes: u64,
    selected_audio_ledger_sha256: String,
    selected_midi_member_count: usize,
    selected_midi_uncompressed_bytes: u64,
    selected_midi_compressed_bytes: u64,
    selected_midi_ledger_sha256: String,
    member_name_prefix: String,
    selected_member_binding_sha256: String,
}

#[derive(Serialize)]
struct Contracts {
    archive_read: &'static str,
    full_layout_binding: &'static str,
    member_read: &'static str,
    selected_member_coverage: &'static str,
    split_scope: [&'static str; 2],
    decoded_container: &'static str,
    decoded_encoding: &'static str,
    sample_rate_hz: u32,
    channels: u16,
    accepted_bits_per_sample: [u16; 2],
    duration_binding: &'static str,
    duration_tolerance_samples_44100: u64,
    bundled_midi_binding: &'static str,
    midi_audio_range_tolerance_micros: u64,
    core_interval: &'static str,
    guard_policy: &'static str,
    canonical_pcm_encoding: &'static str,
    source_pcm_domain: &'static str,
    core_pcm_domain: &'static str,
    guard_pcm_domain: &'static str,
    rms_evidence: &'static str,
    silence_failure_policy: &'static str,
    evidence_source: &'static str,
}

#[derive(Serialize)]
struct IsolationAssertions {
    evidence_class: &'static str,
    full_archive_opaque_bytes_opened_and_hashed: bool,
    archive_central_directory_enumerated: bool,
    selected_audio_members_decompressed_hashed_and_decoded: usize,
    selected_companion_midi_members_decompressed_hashed_and_parsed: usize,
    unselected_member_payloads_decompressed_or_decoded: usize,
    test_member_payloads_decompressed_or_decoded: usize,
    external_midi_archive_member_payloads_reopened: usize,
    fresh_holdout_opened: bool,
    two_mix_opened: bool,
    candidate_scores_run: bool,
}

fn valid_member_identity(member: &MemberEvidence) -> bool {
    let mut session = member.session.split('/');
    let valid_session = session.next() == Some(member.drummer.as_str())
        && session.next().is_some_and(|part| !part.is_empty())
        && session.next().is_none();
    let prefix = format!("{}/", member.session);
    valid_session
        && member
            .performance_id
            .strip_prefix(&prefix)
            .is_some_and(|id| !id.is_empty() && !id.contains('/'))
        && member.manifest_audio_filename.starts_with(&prefix)
        && member.manifest_midi_filename.starts_with(&prefix)
}

fn safe_relative_name(value: &str) -> bool {
    !value.is_empty()
        && value.is_ascii()
        && !value.starts_with('/')
        && !value.contains(['\\', '\0', ':'])
        && value
            .split('/')
            .all(|part| !part.is_empty() && part != "." && part != "..")
}

pub(crate) fn render_receipt(input: ReceiptInput<'_>) -> Result<Vec<u8>, String> {
    validate_parent(input.parent)?;
    validate_archive(&input.archive)?;
    let mut members = input.members.iter().collect::<Vec<_>>();
    members.sort_by_key(|member| member.selection_rank);
    validate_members(&members, &input.archive.member_name_prefix)?;
    let midi_binding_audit = audit_midi_binding(&members);
    let duplicate_audit = audit_duplicates(&members);
    let silence_audit = audit_silence(&members);
    let component_verified = FULL_LAYOUT_LEDGER_SOURCE_PINNED
        && midi_binding_audit.passes()
        && duplicate_audit.passes()
        && silence_audit.passes();
    let aggregate = aggregate(&members)?;
    validate_aggregate(&aggregate, &input.archive)?;
    let downstream_blockers =
        downstream_blockers(&midi_binding_audit, &duplicate_audit, &silence_audit);
    let selected_member_binding_sha256 = sha256_bytes(
        &serde_json::to_vec(&members)
            .map_err(|error| format!("cannot bind selected audio members: {error}"))?,
    );
    let receipt = Receipt {
        schema: OUTPUT_SCHEMA,
        purpose: PURPOSE,
        profile: PROFILE,
        authorization: Authorization {
            component: "audio_archive_member_pcm_provenance",
            component_verified,
            full_layout_ledger_source_pinned: FULL_LAYOUT_LEDGER_SOURCE_PINNED,
            overall_formal_authorization: false,
            formal_scoring_allowed: false,
            context_evaluator_ready: false,
            winner_allowed: false,
            selection_replacement_allowed: false,
        },
        parents: Parents {
            b550_source_commit: B550_SOURCE_COMMIT,
            b551_source_commit: B551_SOURCE_COMMIT,
            development_receipt_sha256: &input.parent.development_receipt_sha256,
            development_manifest_sha256: &input.parent.development_manifest_sha256,
            development_folds_sha256: &input.parent.development_folds_sha256,
            midi_receipt_sha256: &input.parent.midi_receipt_sha256,
            midi_archive_sha256: &input.parent.midi_archive_sha256,
            official_metadata_sha256: PINNED_METADATA_SHA256,
            opened_set_ledger_sha256: PINNED_OPENED_LEDGER_SHA256,
            test_isolation_incident_sha256: PINNED_ISOLATION_INCIDENT_SHA256,
        },
        archive: Archive {
            dataset_id: DATASET_ID,
            dataset_version: DATASET_VERSION,
            sha256: input.archive.archive_sha256,
            bytes: input.archive.archive_bytes,
            central_directory_entries: input.archive.central_directory_entries,
            central_directory_offset: input.archive.central_directory_offset,
            central_directory_size: input.archive.central_directory_size,
            authenticated_local_header_count: input.archive.authenticated_local_header_count,
            overlapping_payload_ranges: input.archive.overlapping_payload_ranges,
            full_layout_ledger_sha256: input.archive.full_layout_ledger_sha256,
            selected_audio_member_count: input.archive.selected_audio_member_count,
            selected_audio_uncompressed_bytes: input.archive.selected_audio_uncompressed_bytes,
            selected_audio_compressed_bytes: input.archive.selected_audio_compressed_bytes,
            selected_audio_ledger_sha256: input.archive.selected_audio_ledger_sha256,
            selected_midi_member_count: input.archive.selected_midi_member_count,
            selected_midi_uncompressed_bytes: input.archive.selected_midi_uncompressed_bytes,
            selected_midi_compressed_bytes: input.archive.selected_midi_compressed_bytes,
            selected_midi_ledger_sha256: input.archive.selected_midi_ledger_sha256,
            member_name_prefix: input.archive.member_name_prefix,
            selected_member_binding_sha256,
        },
        contracts: Contracts {
            archive_read: "one stable file identity; opaque bytes hashed and ZIP structure parsed without replacement",
            full_layout_binding: "all 91108 local headers and nonoverlapping payload ranges are derived and authenticated under the full-archive SHA and exact source-pinned layout ledger",
            member_read: "each selected decompressed WAV buffer is raw-hashed, decoded, and canonicalized without reopening",
            selected_member_coverage: "exact B-550 manifest set; one member per rank and performance ID",
            split_scope: ["train", "validation"],
            decoded_container: "RIFF WAVE",
            decoded_encoding: "format code 1 signed little-endian integer PCM",
            sample_rate_hz: 44_100,
            channels: 1,
            accepted_bits_per_sample: [16, 24],
            duration_binding: "absolute difference between declared and actual source samples is at most 441",
            duration_tolerance_samples_44100: DURATION_TOLERANCE_SAMPLES,
            bundled_midi_binding: "same authenticated archive pass; raw SHA equals the B-551 member and shared parser note range is checked against decoded audio",
            midi_audio_range_tolerance_micros: MIDI_RANGE_TOLERANCE_MICROS,
            core_interval: "manifest half-open [start_sample,end_sample); canonical time origin is core-relative",
            guard_policy: "candidate-independent maximum context [0,actual_samples); evidence only, not context-evaluator readiness",
            canonical_pcm_encoding: "signed 24-bit numerator; PCM16 shifted left 8, PCM24 sign-extended; i32 big-endian; domain+rate+channels+sample-count+values",
            source_pcm_domain: SOURCE_PCM_DOMAIN,
            core_pcm_domain: CORE_PCM_DOMAIN,
            guard_pcm_domain: GUARD_PCM_DOMAIN,
            rms_evidence: "exact sum_squares_pcm24 decimal numerator over region sample count; no float is evidence",
            silence_failure_policy: "all 290 remain fixed; all-zero or constant source/core marks the component unverified and never triggers replacement",
            evidence_source: "semantic verifier output; no receipt boolean or operational assertion is accepted as formal proof",
        },
        members,
        aggregate,
        midi_binding_audit,
        duplicate_audit,
        silence_audit,
        isolation: IsolationAssertions {
            evidence_class: "operational_assertions_not_evidence",
            full_archive_opaque_bytes_opened_and_hashed: true,
            archive_central_directory_enumerated: true,
            selected_audio_members_decompressed_hashed_and_decoded: PINNED_MANIFEST_ROWS,
            selected_companion_midi_members_decompressed_hashed_and_parsed: PINNED_MANIFEST_ROWS,
            unselected_member_payloads_decompressed_or_decoded: 0,
            test_member_payloads_decompressed_or_decoded: 0,
            external_midi_archive_member_payloads_reopened: 0,
            fresh_holdout_opened: false,
            two_mix_opened: false,
            candidate_scores_run: false,
        },
        downstream_blockers,
    };
    let mut bytes = serde_json::to_vec_pretty(&receipt)
        .map_err(|error| format!("cannot serialize audio provenance receipt: {error}"))?;
    bytes.push(b'\n');
    Ok(bytes)
}

pub(crate) fn publish_receipt_create_new(path: &Path, bytes: &[u8]) -> Result<String, String> {
    if path.exists() {
        return Err(format!("receipt already exists: {}", path.display()));
    }
    let digest = sha256_bytes(bytes);
    let parent = path
        .parent()
        .ok_or("receipt result has no parent directory")?;
    let name = path.file_name().ok_or("receipt result has no file name")?;
    let mut temporary_name = OsString::from(".");
    temporary_name.push(name);
    temporary_name.push(format!(".{digest}.tmp"));
    let temporary = parent.join(temporary_name);
    let mut output = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&temporary)
        .map_err(|error| format!("cannot create receipt temporary file: {error}"))?;
    let staged = output.write_all(bytes).and_then(|()| output.sync_all());
    drop(output);
    if let Err(error) = staged {
        let _ = fs::remove_file(&temporary);
        return Err(format!("cannot stage receipt: {error}"));
    }
    if let Err(error) = fs::hard_link(&temporary, path) {
        let _ = fs::remove_file(&temporary);
        return Err(format!(
            "cannot publish new receipt {} without overwrite: {error}",
            path.display()
        ));
    }
    let _ = fs::remove_file(&temporary);
    fs::File::open(parent)
        .and_then(|directory| directory.sync_all())
        .map_err(|error| format!("cannot sync receipt parent directory: {error}"))?;
    Ok(digest)
}

#[cfg(test)]
#[path = "receipt_tests.rs"]
mod tests;
