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
/// A Record writer renews on every 30-second flush. Generation replacement accepts at most four
/// missed renewals as execution evidence; a live DAW PID by itself cannot strand `Another Keep`.
pub(crate) const WRITER_EXECUTION_LEASE_STALE_MS: i64 = 2 * 60 * 1_000;

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
    /// Immutable producer generation attestation. Legacy/no-generation writers keep both fields
    /// absent and can own a session, but can never satisfy the consumer activation barrier.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    capture_generation_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    generation_started_at_ms: Option<i64>,
    claimed_at_ms: i64,
    heartbeat_at_ms: i64,
    #[serde(default)]
    ready_at_ms: Option<i64>,
    closed_at_ms: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct WriterClaimStateFile {
    schema_version: String,
    owner_id: String,
    heartbeat_at_ms: i64,
    #[serde(default)]
    ready_at_ms: Option<i64>,
    closed_at_ms: Option<i64>,
}

#[derive(Debug, Clone)]
struct WriterClaimSnapshot {
    project_hash: String,
    record_session_id: String,
    instance_id: String,
    role: String,
    owner_id: String,
    host_process_id: u32,
    capture_generation_id: Option<String>,
    generation_started_at_ms: Option<i64>,
    heartbeat_at_ms: i64,
    ready_at_ms: Option<i64>,
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
    InvalidGenerationAttestation,
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
            Self::InvalidGenerationAttestation => {
                write!(f, "writer claim generation attestation is invalid")
            }
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct WriterGenerationAttestation {
    pub capture_generation_id: String,
    pub generation_started_at_ms: i64,
}

impl WriterGenerationAttestation {
    fn new(capture_generation_id: &str, generation_started_at_ms: i64) -> Option<Self> {
        let capture_generation_id = capture_generation_id.trim();
        (uuid::Uuid::parse_str(capture_generation_id).is_ok() && generation_started_at_ms > 0).then(
            || Self {
                capture_generation_id: capture_generation_id.to_string(),
                generation_started_at_ms,
            },
        )
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
    finalized: bool,
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
            self.claim.ready_at_ms,
            self.claim.closed_at_ms,
        )?;
        Ok(())
    }

    /// Publishes producer readiness only after the Record artifact exists and its initial flush
    /// completed. Ownership alone is deliberately not readiness: generation publication must not
    /// race ahead of writer construction.
    pub(crate) fn mark_ready(&mut self) -> Result<(), WriterClaimError> {
        ensure_current_owner(&self.path, &self.claim.owner_id)?;
        let now = now_epoch_ms();
        self.claim.heartbeat_at_ms = now;
        self.claim.ready_at_ms = Some(now);
        write_state_atomic(
            &self.path,
            &self.claim.owner_id,
            now,
            self.claim.ready_at_ms,
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
            self.claim.ready_at_ms,
            self.claim.closed_at_ms,
        )?;
        self.finalized = true;
        Ok(())
    }

    pub(crate) fn abandon(mut self) -> Result<(), WriterClaimError> {
        release_if_current_owner(&self.path, &self.claim.owner_id)?;
        self.finalized = true;
        Ok(())
    }
}

impl Drop for WriterClaimGuard {
    fn drop(&mut self) {
        if self.finalized {
            return;
        }
        // An unwinding IO thread must not strand an "active" disk claim for fifteen minutes.
        // Ownership is rechecked inside release_if_current_owner, so a stale guard can never
        // remove a claim already reclaimed by another writer.
        let _ = release_if_current_owner(&self.path, &self.claim.owner_id);
    }
}

pub(crate) fn claim_writer(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> Result<WriterClaimGuard, WriterClaimError> {
    claim_writer_with_attestation(base_dir, project_hash, session_id, role, instance_id, None)
}

/// Claims one writer while binding it to the immutable generation carried by its Record signal.
/// The attestation is written once into the ownership file; heartbeat/ready state can never
/// replace it with a later generation that happens to reuse the same session/role/iid path.
pub(crate) fn claim_writer_for_generation(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
) -> Result<WriterClaimGuard, WriterClaimError> {
    let attestation =
        WriterGenerationAttestation::new(capture_generation_id, generation_started_at_ms)
            .ok_or(WriterClaimError::InvalidGenerationAttestation)?;
    claim_writer_with_attestation(
        base_dir,
        project_hash,
        session_id,
        role,
        instance_id,
        Some(attestation),
    )
}

fn claim_writer_with_attestation(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
    attestation: Option<WriterGenerationAttestation>,
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
        capture_generation_id: attestation
            .as_ref()
            .map(|attestation| attestation.capture_generation_id.clone()),
        generation_started_at_ms: attestation
            .as_ref()
            .map(|attestation| attestation.generation_started_at_ms),
        claimed_at_ms: now,
        heartbeat_at_ms: now,
        ready_at_ms: None,
        closed_at_ms: None,
    };
    let bytes = serde_json::to_vec(&claim)?;

    match link_claim(&dir, &path, role, instance_id, &bytes)? {
        LinkOutcome::Created => Ok(WriterClaimGuard {
            path,
            claim,
            finalized: false,
        }),
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
    if !snapshot_matches_writer(&existing, project_hash, session_id, role, instance_id)
        || existing.closed_at_ms.is_some()
        || !process_is_alive(existing.host_process_id)
    {
        return Ok(false);
    }
    let age_ms = now_epoch_ms().saturating_sub(existing.heartbeat_at_ms);
    Ok(age_ms <= WRITER_CLAIM_STALE_MS)
}

/// Returns true only for a live owner that has completed artifact creation and initial flush.
/// This is the producer barrier used by capture generation publication.
/// Exact producer activation barrier. A current session/role/iid claim from another generation
/// is stale data, not readiness, even if its mutable state says `ready`.
#[allow(clippy::too_many_arguments)]
pub(crate) fn writer_claim_ready_for_generation(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
    host_process_id: u32,
) -> Result<bool, WriterClaimError> {
    let path = writer_claim_path(base_dir, project_hash, session_id, role, instance_id);
    let Some(existing) = read_claim_snapshot(&path)? else {
        return Ok(false);
    };
    if !snapshot_matches_writer(&existing, project_hash, session_id, role, instance_id)
        || existing.ready_at_ms.is_none()
        || existing.closed_at_ms.is_some()
        || existing.capture_generation_id.as_deref() != Some(capture_generation_id)
        || existing.generation_started_at_ms != Some(generation_started_at_ms)
        || existing.host_process_id != host_process_id
        || !process_is_alive(existing.host_process_id)
    {
        return Ok(false);
    }
    let age_ms = now_epoch_ms().saturating_sub(existing.heartbeat_at_ms);
    Ok(age_ms <= WRITER_CLAIM_STALE_MS)
}

/// Exact execution authority for generation replacement. Readiness is intentionally not
/// required here: a writer between claim and initial flush is already executing and must not be
/// overlapped. The immutable generation and live renewing owner are required.
#[allow(clippy::too_many_arguments)]
pub(crate) fn writer_claim_execution_active_for_generation(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
    host_process_id: u32,
) -> Result<bool, WriterClaimError> {
    let path = writer_claim_path(base_dir, project_hash, session_id, role, instance_id);
    let Some(existing) = read_claim_snapshot(&path)? else {
        return Ok(false);
    };
    if !snapshot_matches_writer(&existing, project_hash, session_id, role, instance_id)
        || existing.closed_at_ms.is_some()
        || existing.capture_generation_id.as_deref() != Some(capture_generation_id)
        || existing.generation_started_at_ms != Some(generation_started_at_ms)
        || existing.host_process_id != host_process_id
        || !process_is_alive(existing.host_process_id)
    {
        return Ok(false);
    }
    let age_ms = now_epoch_ms().saturating_sub(existing.heartbeat_at_ms);
    Ok(age_ms <= WRITER_EXECUTION_LEASE_STALE_MS)
}

fn snapshot_matches_writer(
    snapshot: &WriterClaimSnapshot,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> bool {
    snapshot.project_hash == project_hash
        && snapshot.record_session_id == session_id
        && snapshot.instance_id == instance_id
        && snapshot.role == role.dir_name()
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
        if age_ms <= WRITER_CLAIM_STALE_MS && process_is_alive(existing.host_process_id) {
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
        LinkOutcome::Created => Ok(WriterClaimGuard {
            path,
            claim,
            finalized: false,
        }),
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
    ready_at_ms: Option<i64>,
    closed_at_ms: Option<i64>,
) -> Result<(), WriterClaimError> {
    let state = WriterClaimStateFile {
        schema_version: WRITER_CLAIM_SCHEMA.to_string(),
        owner_id: owner_id.to_string(),
        heartbeat_at_ms,
        ready_at_ms,
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
    let ready_at_ms = state
        .as_ref()
        .and_then(|state| state.ready_at_ms)
        .or(claim.ready_at_ms);
    Ok(Some(WriterClaimSnapshot {
        project_hash: claim.project_hash,
        record_session_id: claim.record_session_id,
        instance_id: claim.instance_id,
        role: claim.role,
        owner_id: claim.owner_id,
        host_process_id: claim.host_process_id,
        capture_generation_id: claim.capture_generation_id,
        generation_started_at_ms: claim.generation_started_at_ms,
        heartbeat_at_ms,
        ready_at_ms,
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

#[cfg(unix)]
fn process_is_alive(process_id: u32) -> bool {
    if process_id == 0 || process_id > i32::MAX as u32 {
        return false;
    }
    // SAFETY: signal 0 performs only a kernel liveness probe. EPERM also proves existence.
    let result = unsafe { libc::kill(process_id as i32, 0) };
    result == 0 || io::Error::last_os_error().raw_os_error() == Some(libc::EPERM)
}

#[cfg(not(unix))]
fn process_is_alive(process_id: u32) -> bool {
    process_id != 0
}

#[cfg(test)]
#[path = "record_writer_claim_tests.rs"]
mod tests;
