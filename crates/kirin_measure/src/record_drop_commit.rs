//! One-shot Drop transaction that closes only the Record sessions captured by Kirin OS.
//!
//! Per-session commit files are inert until a project transaction manifest is atomically
//! published. The POST still verifies that its producer capture clock observed the dropped BWF
//! range before this module binds metadata to the open session lifecycle.

use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use crate::record_expected::{
    expected_dir, now_epoch_ms, read_claim_marker_for_session, write_claim_marker_for_generation,
    write_expected_metadata, ExpectedMetadataError, ExpectedWavClaimMarker, ExpectedWavMetadata,
    EXPECTED_METADATA_FUTURE_SKEW_MS, EXPECTED_METADATA_MAX_AGE_MS,
};

pub const DROP_COMMIT_SCHEMA: &str = "drop_record_commit.v1";
pub const DROP_TRANSACTION_SCHEMA: &str = "drop_record_transaction.v1";
pub const DROP_GENERATION_TRANSACTION_SCHEMA: &str = "drop_record_generation_transaction.v1";
const DROP_COMMITS_SUBDIR: &str = "drop_commits";
const DROP_COMMITS_BY_SESSION_SUBDIR: &str = "by_session";
const DROP_TRANSACTIONS_SUBDIR: &str = "drop_transactions";
const DROP_GENERATION_TRANSACTIONS_SUBDIR: &str = "drop_transactions";
const DROP_GENERATION_ACCEPTANCE_SUBDIR: &str = "drop_acceptance";
const DROP_GENERATION_ACCEPTANCE_SCHEMA: &str = "drop_record_generation_acceptance.v2";

#[path = "record_drop_acceptance.rs"]
mod acceptance;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DropRecordCommit {
    pub schema_version: String,
    pub drop_commit_id: String,
    pub project_hash: String,
    pub record_session_id: String,
    #[serde(default)]
    pub capture_generation_id: String,
    #[serde(default)]
    pub generation_started_at_ms: i64,
    pub created_at_ms: i64,
    pub metadata: ExpectedWavMetadata,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DropRecordTransaction {
    pub schema_version: String,
    pub drop_commit_id: String,
    pub project_hash: String,
    #[serde(default)]
    pub capture_generation_id: String,
    #[serde(default)]
    pub generation_started_at_ms: i64,
    pub created_at_ms: i64,
    pub bounce_id: String,
    pub wav_hash: String,
    pub record_session_ids: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DropRecordGenerationProject {
    pub project_hash: String,
    pub record_session_ids: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DropRecordGenerationTransaction {
    pub schema_version: String,
    pub drop_commit_id: String,
    #[serde(default)]
    pub capture_generation_id: String,
    #[serde(default)]
    pub generation_started_at_ms: i64,
    pub created_at_ms: i64,
    pub bounce_id: String,
    pub wav_hash: String,
    pub projects: Vec<DropRecordGenerationProject>,
}

/// First immutable WAV transaction accepted for one exact producer generation.
/// Every member bind must match this fingerprint, so retries cannot split one Keep roster across
/// two WAVs even if individual commit files are replaced between member callbacks.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
struct DropRecordGenerationAcceptance {
    schema_version: String,
    capture_generation_id: String,
    generation_started_at_ms: i64,
    drop_commit_id: String,
    created_at_ms: i64,
    accepted_at_ms: i64,
    bounce_id: String,
    wav_hash: String,
    exact_artifacts_sha256: String,
    generation: crate::capture_generation::CaptureGeneration,
    members: Vec<acceptance::AcceptedDropMember>,
}

pub fn drop_commit_path(base_dir: &Path, project_hash: &str, session_id: &str) -> PathBuf {
    let session =
        crate::path_identity::guard_path_component(session_id, "record_drop_commit.session_id");
    expected_dir(base_dir, project_hash)
        .join(DROP_COMMITS_SUBDIR)
        .join(DROP_COMMITS_BY_SESSION_SUBDIR)
        .join(format!("{session}.json"))
}

pub fn drop_transaction_path(base_dir: &Path, project_hash: &str, drop_commit_id: &str) -> PathBuf {
    let commit = crate::path_identity::guard_path_component(
        drop_commit_id,
        "record_drop_commit.transaction.commit_id",
    );
    expected_dir(base_dir, project_hash)
        .join(DROP_TRANSACTIONS_SUBDIR)
        .join(format!("{commit}.json"))
}

pub fn drop_generation_transaction_path(base_dir: &Path, drop_commit_id: &str) -> PathBuf {
    let commit = crate::path_identity::guard_path_component(
        drop_commit_id,
        "record_drop_commit.generation_transaction.commit_id",
    );
    base_dir
        .join(crate::capture_generation::CAPTURE_GENERATION_SUBDIR)
        .join(DROP_GENERATION_TRANSACTIONS_SUBDIR)
        .join(format!("{commit}.json"))
}

fn drop_generation_acceptance_path(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
) -> PathBuf {
    let generation = crate::path_identity::guard_path_component(
        capture_generation_id,
        "record_drop_commit.acceptance.generation_id",
    );
    base_dir
        .join(crate::capture_generation::CAPTURE_GENERATION_SUBDIR)
        .join(DROP_GENERATION_ACCEPTANCE_SUBDIR)
        .join(format!("{generation}.{generation_started_at_ms}.v2.json"))
}

/// Bind an already inspected Drop commit to exactly one still-open session.
pub fn bind_drop_commit_for_open_session(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
) -> Result<Option<ExpectedWavMetadata>, ExpectedMetadataError> {
    let Some((metadata, marker)) =
        read_drop_commit_for_session(base_dir, project_hash, session_id, true)?
    else {
        return Ok(None);
    };
    terminalize_drop_generation(base_dir, &marker)?;
    if let Some(existing) = marker.metadata {
        return Ok((existing == metadata).then_some(existing));
    }
    write_expected_metadata(base_dir, project_hash, &metadata)?;
    write_claim_marker_for_generation(
        base_dir,
        project_hash,
        &metadata,
        session_id.trim(),
        marker.claimed_at_ms,
        None,
        &marker.requested_by_post_instance_id,
        &marker.capture_generation_id,
        marker.generation_started_at_ms,
    )?;
    Ok(Some(metadata.without_consumed_marker()))
}

/// Read-only validation. The IO owner must prove the BWF range against its capture clock before
/// calling `bind_drop_commit_for_open_session`.
pub fn inspect_drop_commit_for_open_session(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
) -> Result<Option<ExpectedWavMetadata>, ExpectedMetadataError> {
    Ok(
        read_drop_commit_for_session(base_dir, project_hash, session_id, true)?
            .map(|(metadata, _)| metadata),
    )
}

/// Read-only validation for a Record session whose closed PRE/POST artifacts are still pending.
///
/// Unlike the live IO path, this accepts either marker lifecycle state because a crash or the
/// pair-pending close path may leave a metadata-free marker without `closed_at_ms`. The caller
/// must independently prove the exact closed PRE/POST pair before binding the returned metadata.
pub(crate) fn inspect_drop_commit_for_closed_pair_session(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
) -> Result<Option<ExpectedWavMetadata>, ExpectedMetadataError> {
    Ok(
        read_drop_commit_for_session(base_dir, project_hash, session_id, false)?
            .map(|(metadata, _)| metadata),
    )
}

/// Complete one metadata-free Record lifecycle after its closed PRE/POST pair has been proven.
///
/// `inspected_metadata` makes the validation/bind boundary fail closed if Kirin OS replaces a
/// commit between the caller's sample-clock proof and this second read. Existing metadata is
/// immutable: an equal retry is idempotent and a different generation is rejected.
pub(crate) fn bind_drop_commit_for_closed_pair_session(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    inspected_metadata: &ExpectedWavMetadata,
) -> Result<Option<ExpectedWavMetadata>, ExpectedMetadataError> {
    let Some((metadata, marker)) =
        read_drop_commit_for_session(base_dir, project_hash, session_id, false)?
    else {
        return Ok(None);
    };
    if &metadata != inspected_metadata {
        return Ok(None);
    }
    terminalize_drop_generation(base_dir, &marker)?;
    if let Some(existing) = marker.metadata {
        return Ok((existing == metadata).then_some(existing));
    }
    write_expected_metadata(base_dir, project_hash, &metadata)?;
    write_claim_marker_for_generation(
        base_dir,
        project_hash,
        &metadata,
        session_id.trim(),
        marker.claimed_at_ms,
        marker.closed_at_ms.or(Some(now_epoch_ms())),
        &marker.requested_by_post_instance_id,
        &marker.capture_generation_id,
        marker.generation_started_at_ms,
    )?;
    Ok(Some(metadata.without_consumed_marker()))
}

fn terminalize_drop_generation(
    base_dir: &Path,
    marker: &ExpectedWavClaimMarker,
) -> Result<(), ExpectedMetadataError> {
    if marker.capture_generation_id.trim().is_empty() {
        return Ok(());
    }
    crate::capture_generation_lifecycle::mark_generation_terminal_by_identity(
        base_dir,
        &marker.capture_generation_id,
        marker.generation_started_at_ms,
        crate::capture_generation_lifecycle::GenerationTerminalReason::DropCommitted,
    )
    .map(|_| ())
    .map_err(|error| match error {
        crate::capture_generation_lifecycle::GenerationLifecycleError::Io(error) => {
            ExpectedMetadataError::Io(error)
        }
        crate::capture_generation_lifecycle::GenerationLifecycleError::Serde(error) => {
            ExpectedMetadataError::Serde(error)
        }
        crate::capture_generation_lifecycle::GenerationLifecycleError::Invalid => {
            ExpectedMetadataError::Invalid
        }
    })
}

fn read_drop_commit_for_session(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    require_open_marker: bool,
) -> Result<Option<(ExpectedWavMetadata, ExpectedWavClaimMarker)>, ExpectedMetadataError> {
    let session_id = session_id.trim();
    if session_id.is_empty() {
        return Ok(None);
    }
    let Some(marker) = read_claim_marker_for_session(base_dir, project_hash, session_id)? else {
        return Ok(None);
    };
    if marker.session_id != session_id || (require_open_marker && marker.closed_at_ms.is_some()) {
        return Ok(None);
    }
    if !marker.capture_generation_id.is_empty() {
        let Some(accepted) = acceptance::accepted_member_for_session(
            base_dir,
            &marker.capture_generation_id,
            marker.generation_started_at_ms,
            project_hash,
            session_id,
        )?
        else {
            return Ok(None);
        };
        let metadata = accepted.metadata.without_consumed_marker();
        let invalid = accepted.member.project_hash != project_hash
            || accepted.member.record_session_id != session_id
            || accepted.member.post_instance_id != marker.requested_by_post_instance_id
            || metadata.created_at_ms < marker.claimed_at_ms
            || marker
                .metadata
                .as_ref()
                .is_some_and(|existing| existing != &metadata);
        return Ok((!invalid).then_some((metadata, marker)));
    }

    read_legacy_drop_commit_for_session(
        base_dir,
        project_hash,
        session_id,
        require_open_marker,
        marker,
    )
}

/// Compatibility path for pre-generation captures. Current captures are resolved exclusively
/// through the immutable generation acceptance above.
fn read_legacy_drop_commit_for_session(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    require_open_marker: bool,
    marker: ExpectedWavClaimMarker,
) -> Result<Option<(ExpectedWavMetadata, ExpectedWavClaimMarker)>, ExpectedMetadataError> {
    let bytes = match fs::read(drop_commit_path(base_dir, project_hash, session_id)) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(ExpectedMetadataError::Io(e)),
    };
    let commit: DropRecordCommit = serde_json::from_slice(&bytes)?;
    let transaction_bytes = match fs::read(drop_transaction_path(
        base_dir,
        project_hash,
        &commit.drop_commit_id,
    )) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(ExpectedMetadataError::Io(e)),
    };
    let transaction: DropRecordTransaction = serde_json::from_slice(&transaction_bytes)?;
    let generation_transaction_bytes = match fs::read(drop_generation_transaction_path(
        base_dir,
        &commit.drop_commit_id,
    )) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(ExpectedMetadataError::Io(e)),
    };
    let generation_transaction: DropRecordGenerationTransaction =
        serde_json::from_slice(&generation_transaction_bytes)?;
    let now_ms = now_epoch_ms();
    let expected_hash = commit.metadata.wav_hash.as_deref().unwrap_or_default();
    let invalid = commit.schema_version != DROP_COMMIT_SCHEMA
        || commit.drop_commit_id.trim().is_empty()
        || commit.project_hash != project_hash
        || commit.record_session_id != session_id
        || marker.session_id != session_id
        || !commit.capture_generation_id.is_empty()
        || !transaction.capture_generation_id.is_empty()
        || commit.generation_started_at_ms != 0
        || transaction.generation_started_at_ms != 0
        || (require_open_marker && marker.closed_at_ms.is_some())
        || commit.created_at_ms < marker.claimed_at_ms
        || commit.created_at_ms > now_ms.saturating_add(EXPECTED_METADATA_FUTURE_SKEW_MS)
        || now_ms.saturating_sub(commit.created_at_ms) > EXPECTED_METADATA_MAX_AGE_MS
        || commit.metadata.created_at_ms != commit.created_at_ms
        || !commit.metadata.is_fresh_complete_for_arm(now_ms)
        || transaction.schema_version != DROP_TRANSACTION_SCHEMA
        || transaction.drop_commit_id != commit.drop_commit_id
        || transaction.project_hash != project_hash
        || transaction.created_at_ms != commit.created_at_ms
        || transaction.bounce_id != commit.metadata.bounce_id
        || transaction.wav_hash != expected_hash
        || transaction.record_session_ids.is_empty()
        || !transaction
            .record_session_ids
            .iter()
            .any(|id| id == session_id)
        || transaction
            .record_session_ids
            .iter()
            .any(|id| id.trim().is_empty())
        || generation_transaction.schema_version != DROP_GENERATION_TRANSACTION_SCHEMA
        || generation_transaction.drop_commit_id != commit.drop_commit_id
        || generation_transaction.capture_generation_id != commit.capture_generation_id
        || generation_transaction.generation_started_at_ms != commit.generation_started_at_ms
        || generation_transaction.created_at_ms != commit.created_at_ms
        || generation_transaction.bounce_id != commit.metadata.bounce_id
        || generation_transaction.wav_hash != expected_hash
        || !generation_transaction_authorizes_project(
            &generation_transaction,
            &transaction,
            project_hash,
        )
        || marker
            .metadata
            .as_ref()
            .is_some_and(|existing| existing != &commit.metadata);
    if invalid {
        return Ok(None);
    }
    Ok(Some((commit.metadata.without_consumed_marker(), marker)))
}

fn generation_transaction_authorizes_project(
    generation: &DropRecordGenerationTransaction,
    project_transaction: &DropRecordTransaction,
    project_hash: &str,
) -> bool {
    if generation.projects.is_empty()
        || !generation
            .projects
            .windows(2)
            .all(|pair| pair[0].project_hash < pair[1].project_hash)
    {
        return false;
    }
    let mut matched = None;
    for project in &generation.projects {
        if project.project_hash.trim().is_empty()
            || project.record_session_ids.is_empty()
            || project
                .record_session_ids
                .iter()
                .any(|session_id| session_id.trim().is_empty())
            || !project
                .record_session_ids
                .windows(2)
                .all(|pair| pair[0] < pair[1])
        {
            return false;
        }
        if project.project_hash == project_hash {
            matched = Some(project);
        }
    }
    matched
        .is_some_and(|project| project.record_session_ids == project_transaction.record_session_ids)
}

#[cfg(test)]
#[path = "record_drop_commit_tests.rs"]
mod tests;
