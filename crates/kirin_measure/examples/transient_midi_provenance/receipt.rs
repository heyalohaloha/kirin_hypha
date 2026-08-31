use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::Path;

use serde::Serialize;

use crate::contract::{
    ParentEvidence, DATASET_ID, DATASET_VERSION, OUTPUT_SCHEMA, PINNED_ARCHIVE_ENTRIES,
    PINNED_DEVELOPMENT_RECEIPT_SHA256, PINNED_MANIFEST_ROWS, PINNED_MANIFEST_SHA256,
    PINNED_MIDI_ARCHIVE_BYTES, PINNED_MIDI_ARCHIVE_SHA256, PINNED_TRAIN_ROWS,
    PINNED_VALIDATION_ROWS, PROFILE, PURPOSE,
};
use crate::sha256_bytes;

#[path = "receipt_audit.rs"]
mod audit;
use audit::{aggregate, duplicate_audit, duplicate_counts, validate_aggregate, validate_archive};
use audit::{validate_members, validate_parent};

const B550_SOURCE_COMMIT: &str = "72cf4d1e34199b2c4294f049e7f0ba0984865d7f";
const PINNED_FOLDS_SHA256: &str =
    "51c0b30d535a2819dd17b0a49e410c378e2d436eb5f98be06caff5757be45675";
const PINNED_METADATA_SHA256: &str =
    "80677e8fb00e973f33cb91ddaaf7f0cffe55359f9a76c1833ce56c84d1d92c64";
const PINNED_OPENED_LEDGER_SHA256: &str =
    "e9935efba336a40b44ba46bcaa234117927c05e36bb5ca8f80f257b8ba58b3ca";
const PINNED_ISOLATION_INCIDENT_SHA256: &str =
    "27c10a5c6de848029deb18c394181be76ed373d67269773f519708cf53494257";
const MIDI_SEMANTICS_VERSION: &str = "attack-drum-integer-microsecond-midi-v1";

const DOWNSTREAM_BLOCKERS: [&str; 9] = [
    "formal_authorization_not_pinned_in_source_commit",
    "audio_ingest_and_duplicate_verifier_not_implemented",
    "fold_balance_qualification_verifier_not_implemented",
    "blind_proxy_audit_verifier_not_implemented",
    "candidate_plan_ordered_configs_stages_controls_verifier_not_implemented",
    "not_ready_context_guard_unimplemented",
    "candidate_set_completion_receipt_not_implemented",
    "lodo_loso_diagnostic_results_not_ready",
    "midi_component_does_not_construct_formal_authorization",
];

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

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Serialize)]
pub(crate) struct EventCounts {
    pub(crate) raw_notes: usize,
    pub(crate) compound_events: usize,
    pub(crate) kick_only_events: usize,
    pub(crate) hat_only_events: usize,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct CanonicalEvidence {
    pub(crate) counts: EventCounts,
    pub(crate) notes_sha256: String,
    pub(crate) events_sha256: String,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct ExcerptEvidence {
    pub(crate) start_sample_44100: u64,
    pub(crate) end_sample_44100: u64,
    pub(crate) manifest_counts: EventCounts,
    pub(crate) observed: CanonicalEvidence,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MemberEvidence {
    pub(crate) selection_rank: usize,
    pub(crate) selection_key: String,
    pub(crate) fold: u8,
    pub(crate) performance_id: String,
    pub(crate) split: DevelopmentSplit,
    pub(crate) manifest_midi_filename: String,
    pub(crate) archive_member_name: String,
    pub(crate) manifest_midi_sha256: String,
    pub(crate) member_sha256: String,
    pub(crate) uncompressed_bytes: u64,
    pub(crate) compressed_bytes: u64,
    pub(crate) crc32: u32,
    pub(crate) compression: MemberCompression,
    pub(crate) source: CanonicalEvidence,
    pub(crate) excerpt: ExcerptEvidence,
}

#[derive(Clone, Debug)]
pub(crate) struct ArchiveReceiptInput {
    pub(crate) archive_sha256: String,
    pub(crate) archive_bytes: u64,
    pub(crate) central_directory_entries: usize,
    pub(crate) selected_uncompressed_bytes: u64,
    pub(crate) selected_compressed_bytes: u64,
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
    contracts: Contracts<'a>,
    members: Vec<&'a MemberEvidence>,
    aggregate: Aggregate,
    duplicate_audit: DuplicateAudit,
    isolation: IsolationAssertions,
    downstream_blockers: &'static [&'static str],
}

#[derive(Serialize)]
struct Authorization {
    component: &'static str,
    component_verified: bool,
    overall_formal_authorization: bool,
    formal_scoring_allowed: bool,
    winner_allowed: bool,
}

#[derive(Serialize)]
struct Parents<'a> {
    b550_source_commit: &'static str,
    development_receipt_sha256: &'a str,
    development_manifest_sha256: &'a str,
    development_folds_sha256: &'a str,
    official_metadata_sha256: &'static str,
    opened_set_ledger_sha256: &'static str,
    test_isolation_incident_sha256: &'static str,
    midi_archive_sha256: &'a str,
}

#[derive(Serialize)]
struct Archive {
    dataset_id: &'static str,
    dataset_version: &'static str,
    sha256: String,
    bytes: u64,
    central_directory_entries: usize,
    selected_member_count: usize,
    selected_uncompressed_bytes: u64,
    selected_compressed_bytes: u64,
    member_name_prefix: String,
    selected_member_binding_sha256: String,
}

#[derive(Serialize)]
struct Contracts<'a> {
    archive_read: &'static str,
    member_read: &'static str,
    selected_member_coverage: &'static str,
    split_scope: [&'static str; 2],
    excerpt_mapping_version: &'static str,
    excerpt_sample_rate_hz: u32,
    excerpt_interval: &'static str,
    midi_semantics_version: &'static str,
    compound_span_micros: u64,
    canonical_source_notes_domain: &'static str,
    canonical_source_events_domain: &'static str,
    canonical_excerpt_notes_domain: &'static str,
    canonical_excerpt_events_domain: &'static str,
    canonical_time_origin: &'static str,
    empty_excerpt_duplicate_policy: &'static str,
    evidence_source: &'a str,
}

#[derive(Serialize)]
struct Aggregate {
    members: usize,
    train_members: usize,
    validation_members: usize,
    folds: [usize; 5],
    member_uncompressed_bytes: u64,
    member_compressed_bytes: u64,
    source: EventCounts,
    excerpt: EventCounts,
    empty_excerpts: usize,
    duplicate_groups: DuplicateCounts,
}

#[derive(Serialize)]
struct DuplicateAudit {
    policy: &'static str,
    raw_members: DuplicateClass,
    source_canonical_composite: DuplicateClass,
    nonempty_excerpt_canonical_composite: DuplicateClass,
    cross_split_duplicate_groups: usize,
    empty_excerpt_performance_ids: Vec<String>,
}

#[derive(Clone, Copy, Default, Serialize)]
struct DuplicateCounts {
    raw_members: usize,
    source_canonical_composite: usize,
    nonempty_excerpt_canonical_composite: usize,
    cross_split: usize,
}

#[derive(Serialize)]
struct DuplicateClass {
    status: &'static str,
    duplicate_group_count: usize,
    groups: Vec<DuplicateGroup>,
}

#[derive(Clone, Serialize)]
struct DuplicateGroup {
    evidence_sha256: String,
    performance_ids: Vec<String>,
    splits: Vec<DevelopmentSplit>,
}

#[derive(Serialize)]
struct IsolationAssertions {
    evidence_class: &'static str,
    full_archive_opaque_bytes_opened_and_hashed: bool,
    archive_central_directory_enumerated: bool,
    selected_members_decompressed_and_semantically_parsed: usize,
    unselected_member_payloads_decompressed_or_semantically_parsed: usize,
    test_member_payloads_decompressed_or_semantically_parsed: usize,
    audio_opened: bool,
    fresh_holdout_opened: bool,
    two_mix_opened: bool,
    candidate_scores_run: bool,
}

pub(crate) fn render_receipt(input: ReceiptInput<'_>) -> Result<Vec<u8>, String> {
    validate_parent(input.parent)?;
    validate_archive(&input.archive, input.parent)?;
    let mut members = input.members.iter().collect::<Vec<_>>();
    members.sort_by_key(|member| member.selection_rank);
    validate_members(&members, &input.archive.member_name_prefix)?;
    let duplicate_audit = duplicate_audit(&members);
    let duplicate_counts = duplicate_counts(&duplicate_audit);
    if duplicate_counts.raw_members > 0
        || duplicate_counts.source_canonical_composite > 0
        || duplicate_counts.nonempty_excerpt_canonical_composite > 0
        || duplicate_counts.cross_split > 0
    {
        return Err("canonical MIDI duplicate audit failed".to_string());
    }
    let aggregate = aggregate(&members, duplicate_counts)?;
    validate_aggregate(&aggregate, &input.archive, input.parent)?;
    let selected_member_binding_sha256 = sha256_bytes(
        &serde_json::to_vec(&members)
            .map_err(|error| format!("cannot bind selected MIDI members: {error}"))?,
    );
    let receipt = Receipt {
        schema: OUTPUT_SCHEMA,
        purpose: PURPOSE,
        profile: PROFILE,
        authorization: Authorization {
            component: "midi_archive_member_provenance",
            component_verified: true,
            overall_formal_authorization: false,
            formal_scoring_allowed: false,
            winner_allowed: false,
        },
        parents: Parents {
            b550_source_commit: B550_SOURCE_COMMIT,
            development_receipt_sha256: &input.parent.receipt_sha256,
            development_manifest_sha256: &input.parent.manifest_sha256,
            development_folds_sha256: &input.parent.folds_sha256,
            official_metadata_sha256: PINNED_METADATA_SHA256,
            opened_set_ledger_sha256: PINNED_OPENED_LEDGER_SHA256,
            test_isolation_incident_sha256: PINNED_ISOLATION_INCIDENT_SHA256,
            midi_archive_sha256: &input.parent.archive_sha256,
        },
        archive: Archive {
            dataset_id: DATASET_ID,
            dataset_version: DATASET_VERSION,
            sha256: input.archive.archive_sha256,
            bytes: input.archive.archive_bytes,
            central_directory_entries: input.archive.central_directory_entries,
            selected_member_count: members.len(),
            selected_uncompressed_bytes: input.archive.selected_uncompressed_bytes,
            selected_compressed_bytes: input.archive.selected_compressed_bytes,
            member_name_prefix: input.archive.member_name_prefix,
            selected_member_binding_sha256,
        },
        contracts: Contracts {
            archive_read: "one immutable archive buffer is hashed and parsed",
            member_read: "each decompressed member buffer is hashed and parsed without reopening",
            selected_member_coverage:
                "exact B-550 manifest set; one member per rank and performance ID",
            split_scope: ["train", "validation"],
            excerpt_mapping_version: crate::drum_excerpt::EXCERPT_MAPPING_VERSION,
            excerpt_sample_rate_hz: crate::drum_excerpt::EXCERPT_SAMPLE_RATE,
            excerpt_interval: "half-open [start_sample,end_sample)",
            midi_semantics_version: MIDI_SEMANTICS_VERSION,
            compound_span_micros: crate::drum_midi::COMPOUND_SPAN_MICROS,
            canonical_source_notes_domain: crate::canonical::SOURCE_NOTES_DOMAIN,
            canonical_source_events_domain: crate::canonical::SOURCE_EVENTS_DOMAIN,
            canonical_excerpt_notes_domain: crate::canonical::EXCERPT_NOTES_DOMAIN,
            canonical_excerpt_events_domain: crate::canonical::EXCERPT_EVENTS_DOMAIN,
            canonical_time_origin:
                "time_micros*44100-start_sample*1000000 exact cross-product numerator",
            empty_excerpt_duplicate_policy:
                "empty excerpts are counted and exempt from sequence duplicate failure",
            evidence_source: "semantic verifier output; no receipt boolean is accepted as proof",
        },
        members,
        aggregate,
        duplicate_audit,
        isolation: IsolationAssertions {
            evidence_class: "operational_assertions_not_evidence",
            full_archive_opaque_bytes_opened_and_hashed: true,
            archive_central_directory_enumerated: true,
            selected_members_decompressed_and_semantically_parsed: PINNED_MANIFEST_ROWS,
            unselected_member_payloads_decompressed_or_semantically_parsed: 0,
            test_member_payloads_decompressed_or_semantically_parsed: 0,
            audio_opened: false,
            fresh_holdout_opened: false,
            two_mix_opened: false,
            candidate_scores_run: false,
        },
        downstream_blockers: &DOWNSTREAM_BLOCKERS,
    };
    let mut bytes = serde_json::to_vec_pretty(&receipt)
        .map_err(|error| format!("cannot serialize MIDI provenance receipt: {error}"))?;
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
