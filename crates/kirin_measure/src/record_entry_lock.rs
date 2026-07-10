//! Cross-process Record entry ownership.
//!
//! `RecordStateMachine` is intentionally in-process. DAWs can duplicate or
//! rebuild a plugin instance while preserving `instance_id`, so ACK polling must
//! also take a disk-backed entry claim before it flips Watch -> Record. This
//! lock covers the small but important gap before `writer_start()` owns the
//! stronger `record_writer_claim`.

use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use crate::plugin_data::Role;

pub(crate) const RECORD_ENTRY_LOCK_SUBDIR: &str = "record_entry_lock";
pub(crate) const RECORD_ENTRY_LOCK_SCHEMA: &str = "record_entry_lock.v1";
pub(crate) const RECORD_ENTRY_LOCK_STALE_MS: i64 = 15 * 60 * 1_000;

static OWNER_SEQ: AtomicU64 = AtomicU64::new(0);

#[derive(Debug, Clone, Serialize, Deserialize)]
struct RecordEntryLockFile {
    schema_version: String,
    project_hash: String,
    record_session_id: String,
    role: String,
    instance_id: String,
    owner_id: String,
    host_process_id: u32,
    claimed_at_ms: i64,
    heartbeat_at_ms: i64,
}

#[derive(Debug, Clone)]
struct RecordEntrySnapshot {
    owner_id: String,
    heartbeat_at_ms: i64,
}

#[derive(Debug)]
pub(crate) enum RecordEntryLockError {
    Io(io::Error),
    Serde(serde_json::Error),
    InvalidSession,
    AlreadyActive {
        path: PathBuf,
        owner_id: Option<String>,
        heartbeat_at_ms: Option<i64>,
    },
}

impl std::fmt::Display for RecordEntryLockError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "{e}"),
            Self::Serde(e) => write!(f, "{e}"),
            Self::InvalidSession => write!(f, "record entry lock requires a session id"),
            Self::AlreadyActive {
                path,
                owner_id,
                heartbeat_at_ms,
            } => write!(
                f,
                "record entry already active at {} owner={:?} heartbeat_at_ms={:?}",
                path.display(),
                owner_id,
                heartbeat_at_ms
            ),
        }
    }
}

impl std::error::Error for RecordEntryLockError {}

impl From<io::Error> for RecordEntryLockError {
    fn from(value: io::Error) -> Self {
        Self::Io(value)
    }
}

impl From<serde_json::Error> for RecordEntryLockError {
    fn from(value: serde_json::Error) -> Self {
        Self::Serde(value)
    }
}

pub(crate) fn claim_record_entry(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> Result<(), RecordEntryLockError> {
    let session_id = session_id.trim();
    if session_id.is_empty() {
        return Err(RecordEntryLockError::InvalidSession);
    }

    let dir = entry_lock_dir(base_dir, project_hash, session_id);
    fs::create_dir_all(&dir)?;
    let path = entry_lock_path(base_dir, project_hash, session_id, role, instance_id);
    let now = now_epoch_ms();
    let lock = RecordEntryLockFile {
        schema_version: RECORD_ENTRY_LOCK_SCHEMA.to_string(),
        project_hash: project_hash.to_string(),
        record_session_id: session_id.to_string(),
        role: role.dir_name().to_string(),
        instance_id: instance_id.to_string(),
        owner_id: next_owner_id(),
        host_process_id: std::process::id(),
        claimed_at_ms: now,
        heartbeat_at_ms: now,
    };
    let bytes = serde_json::to_vec(&lock)?;

    match link_entry_claim(&dir, &path, role, instance_id, &bytes)? {
        LinkOutcome::Created => Ok(()),
        LinkOutcome::AlreadyExists => reclaim_or_reject_existing(path, role, instance_id, &bytes),
    }
}

fn reclaim_or_reject_existing(
    path: PathBuf,
    role: Role,
    instance_id: &str,
    bytes: &[u8],
) -> Result<(), RecordEntryLockError> {
    if let Some(existing) = read_entry_snapshot(&path)? {
        let age_ms = now_epoch_ms().saturating_sub(existing.heartbeat_at_ms);
        if age_ms <= RECORD_ENTRY_LOCK_STALE_MS {
            return Err(RecordEntryLockError::AlreadyActive {
                path,
                owner_id: Some(existing.owner_id),
                heartbeat_at_ms: Some(existing.heartbeat_at_ms),
            });
        }
    }

    let _ = fs::remove_file(&path);
    let dir = path
        .parent()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "entry lock dir missing"))?;
    match link_entry_claim(dir, &path, role, instance_id, bytes)? {
        LinkOutcome::Created => Ok(()),
        LinkOutcome::AlreadyExists => Err(RecordEntryLockError::AlreadyActive {
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

fn link_entry_claim(
    dir: &Path,
    path: &Path,
    role: Role,
    instance_id: &str,
    bytes: &[u8],
) -> Result<LinkOutcome, RecordEntryLockError> {
    let temp = build_temp_entry_claim(dir, role, instance_id, bytes)?;
    match crate::atomic_claim::link_claim(&temp, path)? {
        crate::atomic_claim::ClaimOutcome::Created => Ok(LinkOutcome::Created),
        crate::atomic_claim::ClaimOutcome::AlreadyClaimed => Ok(LinkOutcome::AlreadyExists),
    }
}

fn build_temp_entry_claim(
    dir: &Path,
    role: Role,
    instance_id: &str,
    bytes: &[u8],
) -> io::Result<PathBuf> {
    let iid =
        crate::path_identity::guard_path_component(instance_id, "record_entry_lock.temp.instance");
    crate::atomic_claim::build_claim_temp(dir, &format!("{}__{}", role.dir_name(), iid), bytes)
}

fn read_entry_snapshot(path: &Path) -> Result<Option<RecordEntrySnapshot>, RecordEntryLockError> {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(e.into()),
    };
    let lock: RecordEntryLockFile = serde_json::from_slice(&bytes)?;
    if lock.schema_version != RECORD_ENTRY_LOCK_SCHEMA {
        return Ok(None);
    }
    Ok(Some(RecordEntrySnapshot {
        owner_id: lock.owner_id,
        heartbeat_at_ms: lock.heartbeat_at_ms,
    }))
}

fn entry_lock_dir(base_dir: &Path, project_hash: &str, session_id: &str) -> PathBuf {
    let ph =
        crate::path_identity::guard_path_component(project_hash, "record_entry_lock.project_hash");
    let sid =
        crate::path_identity::guard_path_component(session_id, "record_entry_lock.session_id");
    base_dir
        .join(&*ph)
        .join(RECORD_ENTRY_LOCK_SUBDIR)
        .join(&*sid)
}

fn entry_lock_path(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> PathBuf {
    let iid =
        crate::path_identity::guard_path_component(instance_id, "record_entry_lock.instance_id");
    entry_lock_dir(base_dir, project_hash, session_id).join(format!(
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
#[path = "record_entry_lock_tests.rs"]
mod tests;
