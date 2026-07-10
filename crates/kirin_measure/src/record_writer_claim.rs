//! B-330: cross-process writer ownership for one Record session side.
//!
//! A DAW can duplicate or reconstruct a plugin instance while preserving the
//! persisted `instance_id`. In-memory `RecordStateMachine` guards do not cross
//! that boundary, so the writer itself owns a disk-backed claim:
//! `{project_hash}/record_writer_claim/{session_id}/{role}__{instance_id}.json`.
//! The claim is created with tempfile + hard_link, matching the reservation
//! module's atomic claim primitive.

use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use crate::plugin_data::Role;

pub(crate) const WRITER_CLAIM_SUBDIR: &str = "record_writer_claim";
pub(crate) const WRITER_CLAIM_SCHEMA: &str = "record_writer_claim.v1";
pub(crate) const WRITER_CLAIM_STALE_MS: i64 = 15 * 60 * 1_000;

#[cfg(test)]
static TEMP_SEQ: AtomicU64 = AtomicU64::new(0);
static OWNER_SEQ: AtomicU64 = AtomicU64::new(0);

#[derive(Debug, Clone, Serialize, Deserialize)]
struct WriterClaimFile {
    schema_version: String,
    project_hash: String,
    record_session_id: String,
    instance_id: String,
    role: String,
    owner_id: String,
    host_process_id: u32,
    claimed_at_ms: i64,
    heartbeat_at_ms: i64,
    closed_at_ms: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct WriterClaimStateFile {
    schema_version: String,
    owner_id: String,
    heartbeat_at_ms: i64,
    closed_at_ms: Option<i64>,
}

#[derive(Debug, Clone)]
struct WriterClaimSnapshot {
    owner_id: String,
    heartbeat_at_ms: i64,
    closed_at_ms: Option<i64>,
}

#[derive(Debug)]
pub(crate) enum WriterClaimError {
    Io(io::Error),
    Serde(serde_json::Error),
    AlreadyActive {
        path: PathBuf,
        owner_id: Option<String>,
        heartbeat_at_ms: Option<i64>,
    },
    AlreadyClosed {
        path: PathBuf,
        closed_at_ms: Option<i64>,
    },
}

impl std::fmt::Display for WriterClaimError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "{e}"),
            Self::Serde(e) => write!(f, "{e}"),
            Self::AlreadyActive {
                path,
                owner_id,
                heartbeat_at_ms,
            } => write!(
                f,
                "writer claim already active at {} owner={:?} heartbeat_at_ms={:?}",
                path.display(),
                owner_id,
                heartbeat_at_ms
            ),
            Self::AlreadyClosed { path, closed_at_ms } => write!(
                f,
                "writer claim already closed at {} closed_at_ms={:?}",
                path.display(),
                closed_at_ms
            ),
        }
    }
}

impl std::error::Error for WriterClaimError {}

impl From<io::Error> for WriterClaimError {
    fn from(value: io::Error) -> Self {
        Self::Io(value)
    }
}

impl From<serde_json::Error> for WriterClaimError {
    fn from(value: serde_json::Error) -> Self {
        Self::Serde(value)
    }
}

#[derive(Debug)]
pub(crate) struct WriterClaimGuard {
    path: PathBuf,
    claim: WriterClaimFile,
}

impl WriterClaimGuard {
    pub(crate) fn heartbeat(&mut self) -> Result<(), WriterClaimError> {
        ensure_current_owner(&self.path, &self.claim.owner_id)?;
        let now = now_epoch_ms();
        self.claim.heartbeat_at_ms = now;
        write_state_atomic(
            &self.path,
            &self.claim.owner_id,
            now,
            self.claim.closed_at_ms,
        )?;
        Ok(())
    }

    pub(crate) fn mark_closed(&mut self) -> Result<(), WriterClaimError> {
        ensure_current_owner(&self.path, &self.claim.owner_id)?;
        let now = now_epoch_ms();
        self.claim.heartbeat_at_ms = now;
        self.claim.closed_at_ms = Some(now);
        write_state_atomic(
            &self.path,
            &self.claim.owner_id,
            now,
            self.claim.closed_at_ms,
        )?;
        Ok(())
    }

    pub(crate) fn abandon(self) -> Result<(), WriterClaimError> {
        release_if_current_owner(&self.path, &self.claim.owner_id)?;
        Ok(())
    }
}

pub(crate) fn claim_writer(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> Result<WriterClaimGuard, WriterClaimError> {
    let dir = writer_claim_dir(base_dir, project_hash, session_id);
    fs::create_dir_all(&dir)?;
    let path = writer_claim_path(base_dir, project_hash, session_id, role, instance_id);
    let owner_id = next_owner_id();
    let now = now_epoch_ms();
    let claim = WriterClaimFile {
        schema_version: WRITER_CLAIM_SCHEMA.to_string(),
        project_hash: project_hash.to_string(),
        record_session_id: session_id.to_string(),
        instance_id: instance_id.to_string(),
        role: role.dir_name().to_string(),
        owner_id,
        host_process_id: std::process::id(),
        claimed_at_ms: now,
        heartbeat_at_ms: now,
        closed_at_ms: None,
    };
    let bytes = serde_json::to_vec(&claim)?;

    match link_claim(&dir, &path, role, instance_id, &bytes)? {
        LinkOutcome::Created => Ok(WriterClaimGuard { path, claim }),
        LinkOutcome::AlreadyExists => reclaim_or_reject_existing(path, claim, role, &bytes),
    }
}

pub(crate) fn writer_claim_closed(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> Result<bool, WriterClaimError> {
    let path = writer_claim_path(base_dir, project_hash, session_id, role, instance_id);
    let Some(existing) = read_claim_snapshot(&path)? else {
        return Ok(false);
    };
    Ok(existing.closed_at_ms.is_some())
}

pub(crate) fn writer_claim_active(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> Result<bool, WriterClaimError> {
    let path = writer_claim_path(base_dir, project_hash, session_id, role, instance_id);
    let Some(existing) = read_claim_snapshot(&path)? else {
        return Ok(false);
    };
    if existing.closed_at_ms.is_some() {
        return Ok(false);
    }
    let age_ms = now_epoch_ms().saturating_sub(existing.heartbeat_at_ms);
    Ok(age_ms <= WRITER_CLAIM_STALE_MS)
}

fn reclaim_or_reject_existing(
    path: PathBuf,
    claim: WriterClaimFile,
    role: Role,
    bytes: &[u8],
) -> Result<WriterClaimGuard, WriterClaimError> {
    let existing = read_claim_snapshot(&path)?;
    if let Some(existing) = existing {
        if existing.closed_at_ms.is_some() {
            return Err(WriterClaimError::AlreadyClosed {
                path,
                closed_at_ms: existing.closed_at_ms,
            });
        }
        let age_ms = now_epoch_ms().saturating_sub(existing.heartbeat_at_ms);
        if age_ms <= WRITER_CLAIM_STALE_MS {
            return Err(WriterClaimError::AlreadyActive {
                path,
                owner_id: Some(existing.owner_id),
                heartbeat_at_ms: Some(existing.heartbeat_at_ms),
            });
        }
    }

    let _ = fs::remove_file(&path);
    let _ = fs::remove_file(claim_state_path(&path));
    let dir = path
        .parent()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "claim dir missing"))?;
    match link_claim(dir, &path, role, &claim.instance_id, bytes)? {
        LinkOutcome::Created => Ok(WriterClaimGuard { path, claim }),
        LinkOutcome::AlreadyExists => Err(WriterClaimError::AlreadyActive {
            path,
            owner_id: None,
            heartbeat_at_ms: None,
        }),
    }
}

enum LinkOutcome {
    Created,
    AlreadyExists,
}

fn link_claim(
    dir: &Path,
    path: &Path,
    role: Role,
    instance_id: &str,
    bytes: &[u8],
) -> Result<LinkOutcome, WriterClaimError> {
    let temp = build_temp_claim(dir, role, instance_id, bytes)?;
    match crate::atomic_claim::link_claim(&temp, path)? {
        crate::atomic_claim::ClaimOutcome::Created => Ok(LinkOutcome::Created),
        crate::atomic_claim::ClaimOutcome::AlreadyClaimed => Ok(LinkOutcome::AlreadyExists),
    }
}

fn build_temp_claim(
    dir: &Path,
    role: Role,
    instance_id: &str,
    bytes: &[u8],
) -> io::Result<PathBuf> {
    let iid = crate::path_identity::guard_path_component(
        instance_id,
        "record_writer_claim.temp.instance_id",
    );
    crate::atomic_claim::build_claim_temp(dir, &format!("{}__{}", role.dir_name(), iid), bytes)
}

fn write_state_atomic(
    claim_path: &Path,
    owner_id: &str,
    heartbeat_at_ms: i64,
    closed_at_ms: Option<i64>,
) -> Result<(), WriterClaimError> {
    let state = WriterClaimStateFile {
        schema_version: WRITER_CLAIM_SCHEMA.to_string(),
        owner_id: owner_id.to_string(),
        heartbeat_at_ms,
        closed_at_ms,
    };
    let bytes = serde_json::to_vec(&state)?;
    crate::atomic_file::write_bytes_atomic(&claim_state_path(claim_path), &bytes)?;
    Ok(())
}

fn read_claim_snapshot(path: &Path) -> Result<Option<WriterClaimSnapshot>, WriterClaimError> {
    let Some(claim) = read_existing_claim(path)? else {
        return Ok(None);
    };
    let state = read_existing_state(path)?.filter(|state| {
        state.owner_id == claim.owner_id && state.schema_version == WRITER_CLAIM_SCHEMA
    });
    let heartbeat_at_ms = state
        .as_ref()
        .map(|state| state.heartbeat_at_ms)
        .unwrap_or(claim.heartbeat_at_ms);
    let closed_at_ms = state
        .as_ref()
        .and_then(|state| state.closed_at_ms)
        .or(claim.closed_at_ms);
    Ok(Some(WriterClaimSnapshot {
        owner_id: claim.owner_id,
        heartbeat_at_ms,
        closed_at_ms,
    }))
}

fn read_existing_claim(path: &Path) -> Result<Option<WriterClaimFile>, WriterClaimError> {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(e.into()),
    };
    let claim: WriterClaimFile = serde_json::from_slice(&bytes)?;
    if claim.schema_version != WRITER_CLAIM_SCHEMA {
        return Ok(None);
    }
    Ok(Some(claim))
}

fn read_existing_state(path: &Path) -> Result<Option<WriterClaimStateFile>, WriterClaimError> {
    let state_path = claim_state_path(path);
    let bytes = match fs::read(state_path) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(e.into()),
    };
    Ok(Some(serde_json::from_slice(&bytes)?))
}

fn ensure_current_owner(path: &Path, owner_id: &str) -> Result<(), WriterClaimError> {
    let Some(existing) = read_existing_claim(path)? else {
        return Err(WriterClaimError::AlreadyActive {
            path: path.to_path_buf(),
            owner_id: None,
            heartbeat_at_ms: None,
        });
    };
    if existing.owner_id == owner_id {
        return Ok(());
    }
    Err(WriterClaimError::AlreadyActive {
        path: path.to_path_buf(),
        owner_id: Some(existing.owner_id),
        heartbeat_at_ms: Some(existing.heartbeat_at_ms),
    })
}

fn release_if_current_owner(path: &Path, owner_id: &str) -> Result<(), WriterClaimError> {
    ensure_current_owner(path, owner_id)?;
    let _ = fs::remove_file(claim_state_path(path));
    match fs::remove_file(path) {
        Ok(()) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(e) => Err(e.into()),
    }
}

fn claim_state_path(path: &Path) -> PathBuf {
    let mut file_name = path
        .file_name()
        .map(|name| name.to_os_string())
        .unwrap_or_else(|| "claim.json".into());
    file_name.push(".state");
    path.with_file_name(file_name)
}

fn writer_claim_dir(base_dir: &Path, project_hash: &str, session_id: &str) -> PathBuf {
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "record_writer_claim.project_hash",
    );
    let sid =
        crate::path_identity::guard_path_component(session_id, "record_writer_claim.session_id");
    base_dir.join(&*ph).join(WRITER_CLAIM_SUBDIR).join(&*sid)
}

fn writer_claim_path(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> PathBuf {
    let iid =
        crate::path_identity::guard_path_component(instance_id, "record_writer_claim.instance_id");
    writer_claim_dir(base_dir, project_hash, session_id).join(format!(
        "{}__{}.json",
        role.dir_name(),
        iid
    ))
}

fn next_owner_id() -> String {
    let seq = OWNER_SEQ.fetch_add(1, Ordering::Relaxed);
    format!("{}-{seq}", std::process::id())
}

fn now_epoch_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

#[cfg(test)]
#[path = "record_writer_claim_tests.rs"]
mod tests;
