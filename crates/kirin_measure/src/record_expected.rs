//! Expected WAV metadata bridge for PairRecordSession.
//!
//! Kirin OS owns the dropped/exported WAV header truth. Hypha snapshots that
//! truth from `plugin_data/{project_hash}/record_expected/current.json` into
//! `record_signal`, then into final plugin_data artifacts when available.
//! `current.json` is never consumed by claim; each `session_id` gets an immutable
//! claim snapshot so close-time recovery cannot be pulled onto a later bounce.
//! Missing metadata does not block Keep; final artifacts without this trust
//! anchor are explicitly marked as usable fallback rather than complete.

use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

pub const EXPECTED_SUBDIR: &str = "record_expected";
pub const EXPECTED_FILENAME: &str = "current.json";
pub const EXPECTED_METADATA_MAX_AGE_MS: i64 = 10 * 60 * 1_000;
const EXPECTED_METADATA_FUTURE_SKEW_MS: i64 = 60 * 1_000;
const EXPECTED_CLAIMS_SUBDIR: &str = "claims";
const EXPECTED_CLAIMS_BY_SESSION_SUBDIR: &str = "by_session";
const EXPECTED_CLAIM_SCHEMA: &str = "1.0";
#[cfg(test)]
const EXPECTED_METADATA_SESSION_SEPARATOR: char = '\n';

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ExpectedWavMetadata {
    pub expected_duration_samples: u64,
    pub expected_sample_rate: u32,
    pub wav_path: String,
    #[serde(default)]
    pub bounce_id: String,
    #[serde(default)]
    pub created_at_ms: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub wav_file_size: Option<u64>,
    #[serde(default)]
    pub wav_mtime_ms: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub wav_hash: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub consumed_at_ms: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub consumed_by_session_id: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
struct ExpectedWavClaimMarker {
    schema_version: String,
    session_id: String,
    bounce_id: String,
    created_at_ms: i64,
    wav_hash: String,
    claimed_at_ms: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    metadata: Option<ExpectedWavMetadata>,
}

impl ExpectedWavMetadata {
    pub fn is_usable(&self) -> bool {
        self.is_complete() && self.consumed_at_ms.is_none() && self.consumed_by_session_id.is_none()
    }

    pub fn is_complete(&self) -> bool {
        self.expected_duration_samples > 0
            && self.expected_sample_rate > 0
            && !self.wav_path.trim().is_empty()
            && !self.bounce_id.trim().is_empty()
            && self.created_at_ms > 0
            && self.wav_file_size.is_some_and(|size| size > 0)
            && self.wav_mtime_ms > 0
            && self
                .wav_hash
                .as_ref()
                .is_some_and(|hash| !hash.trim().is_empty())
    }

    pub fn is_fresh_for_arm(&self, now_ms: i64) -> bool {
        if !self.is_usable() {
            return false;
        }
        self.is_fresh_complete_for_arm(now_ms)
    }

    fn is_fresh_complete_for_arm(&self, now_ms: i64) -> bool {
        if !self.is_complete() {
            return false;
        }
        if self.created_at_ms > now_ms.saturating_add(EXPECTED_METADATA_FUTURE_SKEW_MS) {
            return false;
        }
        now_ms.saturating_sub(self.created_at_ms) <= EXPECTED_METADATA_MAX_AGE_MS
    }

    fn without_consumed_marker(mut self) -> Self {
        self.consumed_at_ms = None;
        self.consumed_by_session_id = None;
        self
    }
}

#[derive(Debug)]
pub enum ExpectedMetadataError {
    Io(io::Error),
    Serde(serde_json::Error),
    Invalid,
    Stale,
    /// Legacy/current metadata was explicitly unavailable. New claim paths keep
    /// `current.json` immutable and should not emit this for claim races.
    Consumed,
}

impl std::fmt::Display for ExpectedMetadataError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "IO error: {e}"),
            Self::Serde(e) => write!(f, "JSON error: {e}"),
            Self::Invalid => write!(f, "expected WAV metadata is incomplete"),
            Self::Stale => write!(f, "expected WAV metadata is stale"),
            Self::Consumed => write!(f, "expected WAV metadata has already been consumed"),
        }
    }
}

impl std::error::Error for ExpectedMetadataError {}

impl From<io::Error> for ExpectedMetadataError {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl From<serde_json::Error> for ExpectedMetadataError {
    fn from(e: serde_json::Error) -> Self {
        Self::Serde(e)
    }
}

pub fn expected_dir(base_dir: &Path, project_hash: &str) -> PathBuf {
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "record_expected.expected_dir.project_hash",
    );
    base_dir.join(&*ph).join(EXPECTED_SUBDIR)
}

pub fn expected_path(base_dir: &Path, project_hash: &str) -> PathBuf {
    expected_dir(base_dir, project_hash).join(EXPECTED_FILENAME)
}

pub fn read_expected_metadata(
    base_dir: &Path,
    project_hash: &str,
) -> Result<ExpectedWavMetadata, ExpectedMetadataError> {
    let path = expected_path(base_dir, project_hash);
    let bytes = fs::read(path)?;
    let metadata: ExpectedWavMetadata = serde_json::from_slice(&bytes)?;
    if !metadata.is_complete() {
        Err(ExpectedMetadataError::Invalid)
    } else if !metadata.is_fresh_complete_for_arm(now_epoch_ms()) {
        Err(ExpectedMetadataError::Stale)
    } else {
        Ok(metadata.without_consumed_marker())
    }
}

pub fn write_expected_metadata(
    base_dir: &Path,
    project_hash: &str,
    metadata: &ExpectedWavMetadata,
) -> Result<(), ExpectedMetadataError> {
    if !metadata.is_usable() {
        return Err(ExpectedMetadataError::Invalid);
    }
    let path = expected_path(base_dir, project_hash);
    let json = serde_json::to_vec(metadata)?;
    crate::atomic_file::write_bytes_atomic(&path, &json)?;
    Ok(())
}

pub fn claim_expected_metadata_for_session(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
) -> Result<ExpectedWavMetadata, ExpectedMetadataError> {
    let session_id = session_id.trim();
    if session_id.is_empty() {
        return Err(ExpectedMetadataError::Invalid);
    }
    if let Some(metadata) = read_claimed_metadata_for_session(base_dir, project_hash, session_id)? {
        return Ok(metadata);
    }
    let path = expected_path(base_dir, project_hash);
    let bytes = fs::read(&path)?;
    let metadata: ExpectedWavMetadata = serde_json::from_slice(&bytes)?;
    if !metadata.is_complete() {
        return Err(ExpectedMetadataError::Invalid);
    }
    let now_ms = now_epoch_ms();
    if !metadata.is_fresh_complete_for_arm(now_ms) {
        return Err(ExpectedMetadataError::Stale);
    }
    let artifact_metadata = metadata.clone().without_consumed_marker();
    write_claim_marker(base_dir, project_hash, &metadata, session_id, now_ms)?;
    Ok(artifact_metadata)
}

pub fn mark_expected_metadata_consumed(
    base_dir: &Path,
    project_hash: &str,
    bounce_id: &str,
    session_id: &str,
) -> Result<bool, ExpectedMetadataError> {
    if bounce_id.trim().is_empty() || session_id.trim().is_empty() {
        return Ok(false);
    }
    let path = expected_path(base_dir, project_hash);
    let bytes = fs::read(&path)?;
    let metadata: ExpectedWavMetadata = serde_json::from_slice(&bytes)?;
    if metadata.bounce_id != bounce_id {
        return Ok(false);
    }
    write_claim_marker(
        base_dir,
        project_hash,
        &metadata,
        session_id.trim(),
        now_epoch_ms(),
    )?;
    Ok(true)
}

fn now_epoch_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

fn expected_claims_generation_dir(
    base_dir: &Path,
    project_hash: &str,
    metadata: &ExpectedWavMetadata,
) -> PathBuf {
    let bounce = crate::path_identity::guard_path_component(
        &metadata.bounce_id,
        "record_expected.claims.bounce_id",
    );
    expected_dir(base_dir, project_hash)
        .join(EXPECTED_CLAIMS_SUBDIR)
        .join(format!("{}-{}", &*bounce, metadata.created_at_ms))
}

fn expected_claim_marker_path(
    base_dir: &Path,
    project_hash: &str,
    metadata: &ExpectedWavMetadata,
    session_id: &str,
) -> PathBuf {
    let session =
        crate::path_identity::guard_path_component(session_id, "record_expected.claims.session_id");
    expected_claims_generation_dir(base_dir, project_hash, metadata).join(format!("{session}.json"))
}

fn expected_session_claim_marker_path(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
) -> PathBuf {
    let session =
        crate::path_identity::guard_path_component(session_id, "record_expected.claims.session_id");
    expected_dir(base_dir, project_hash)
        .join(EXPECTED_CLAIMS_SUBDIR)
        .join(EXPECTED_CLAIMS_BY_SESSION_SUBDIR)
        .join(format!("{session}.json"))
}

fn read_claimed_metadata_for_session(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
) -> Result<Option<ExpectedWavMetadata>, ExpectedMetadataError> {
    let path = expected_session_claim_marker_path(base_dir, project_hash, session_id);
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(ExpectedMetadataError::Io(e)),
    };
    let marker: ExpectedWavClaimMarker = serde_json::from_slice(&bytes)?;
    if marker.schema_version != EXPECTED_CLAIM_SCHEMA || marker.session_id != session_id {
        return Ok(None);
    }
    let Some(metadata) = marker.metadata else {
        return Ok(None);
    };
    let metadata = metadata.without_consumed_marker();
    let expected_hash = metadata.wav_hash.as_deref().unwrap_or("");
    if !metadata.is_complete()
        || marker.bounce_id != metadata.bounce_id
        || marker.created_at_ms != metadata.created_at_ms
        || marker.wav_hash != expected_hash
    {
        return Ok(None);
    }
    Ok(Some(metadata))
}

fn write_claim_marker(
    base_dir: &Path,
    project_hash: &str,
    metadata: &ExpectedWavMetadata,
    session_id: &str,
    claimed_at_ms: i64,
) -> Result<(), ExpectedMetadataError> {
    let marker = ExpectedWavClaimMarker {
        schema_version: EXPECTED_CLAIM_SCHEMA.to_string(),
        session_id: session_id.to_string(),
        bounce_id: metadata.bounce_id.clone(),
        created_at_ms: metadata.created_at_ms,
        wav_hash: metadata.wav_hash.clone().unwrap_or_default(),
        claimed_at_ms,
        metadata: Some(metadata.clone().without_consumed_marker()),
    };
    let json = serde_json::to_vec(&marker)?;
    let session_path = expected_session_claim_marker_path(base_dir, project_hash, session_id);
    crate::atomic_file::write_bytes_atomic(&session_path, &json)?;
    let path = expected_claim_marker_path(base_dir, project_hash, metadata, session_id);
    crate::atomic_file::write_bytes_atomic(&path, &json)?;
    Ok(())
}

#[cfg(test)]
fn claimed_session_ids(metadata: &ExpectedWavMetadata) -> Vec<String> {
    metadata
        .consumed_by_session_id
        .as_deref()
        .unwrap_or("")
        .split(EXPECTED_METADATA_SESSION_SEPARATOR)
        .map(str::trim)
        .filter(|session| !session.is_empty())
        .map(str::to_string)
        .collect()
}

#[cfg(test)]
fn claimed_session_ids_for_metadata(
    base_dir: &Path,
    project_hash: &str,
    metadata: &ExpectedWavMetadata,
) -> Result<Vec<String>, ExpectedMetadataError> {
    let mut session_ids = claimed_session_ids(metadata);
    let dir = expected_claims_generation_dir(base_dir, project_hash, metadata);
    let entries = match fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(sorted_unique(session_ids)),
        Err(e) => return Err(ExpectedMetadataError::Io(e)),
    };
    let expected_hash = metadata.wav_hash.as_deref().unwrap_or("");
    for entry in entries.flatten() {
        let Ok(file_type) = entry.file_type() else {
            continue;
        };
        if !file_type.is_file() {
            continue;
        }
        let Ok(bytes) = fs::read(entry.path()) else {
            continue;
        };
        let Ok(marker) = serde_json::from_slice::<ExpectedWavClaimMarker>(&bytes) else {
            continue;
        };
        if marker.schema_version == EXPECTED_CLAIM_SCHEMA
            && marker.bounce_id == metadata.bounce_id
            && marker.created_at_ms == metadata.created_at_ms
            && marker.wav_hash == expected_hash
            && !marker.session_id.trim().is_empty()
        {
            session_ids.push(marker.session_id);
        }
    }
    Ok(sorted_unique(session_ids))
}

#[cfg(test)]
fn sorted_unique(mut session_ids: Vec<String>) -> Vec<String> {
    session_ids.sort();
    session_ids.dedup();
    session_ids
}

#[cfg(test)]
#[path = "record_expected_tests.rs"]
mod tests;
