//! Durable terminal state for producer-owned capture generations.
//!
//! `active/current.json` is intentionally retained after Stop/Drop because it is the exact
//! roster Kirin OS follows.  It therefore cannot also mean "open".  This module stores the
//! orthogonal lifecycle fact at an exact generation-addressed path.  Producers read one known
//! file; no history scan or wall-clock expiry participates in authorization.

use serde::{Deserialize, Serialize};
use std::fs::{self, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use crate::capture_generation::{CaptureGeneration, CAPTURE_GENERATION_SUBDIR};

pub const GENERATION_TERMINAL_SCHEMA: &str = "capture_generation_terminal.v1";
const GENERATION_TERMINAL_SUBDIR: &str = "terminal";
const GENERATION_TERMINAL_QUARANTINE_SUBDIR: &str = "quarantine";
static QUARANTINE_SEQUENCE: AtomicU64 = AtomicU64::new(0);

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum GenerationTerminalReason {
    AllStop,
    DropCommitted,
    Aborted,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CaptureGenerationTerminal {
    pub schema_version: String,
    pub capture_generation_id: String,
    pub generation_started_at_ms: i64,
    pub terminal_reason: GenerationTerminalReason,
    pub terminal_at_ms: i64,
}

impl CaptureGenerationTerminal {
    fn is_valid_for(&self, capture_generation_id: &str, generation_started_at_ms: i64) -> bool {
        self.schema_version == GENERATION_TERMINAL_SCHEMA
            && self.capture_generation_id == capture_generation_id
            && self.generation_started_at_ms == generation_started_at_ms
            && self.terminal_at_ms > 0
    }
}

#[derive(Debug)]
pub enum GenerationLifecycleError {
    Io(io::Error),
    Serde(serde_json::Error),
    Invalid,
}

impl std::fmt::Display for GenerationLifecycleError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "IO error: {error}"),
            Self::Serde(error) => write!(formatter, "JSON error: {error}"),
            Self::Invalid => write!(formatter, "invalid capture generation terminal state"),
        }
    }
}

impl std::error::Error for GenerationLifecycleError {}

impl From<io::Error> for GenerationLifecycleError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

impl From<serde_json::Error> for GenerationLifecycleError {
    fn from(error: serde_json::Error) -> Self {
        Self::Serde(error)
    }
}

pub fn generation_terminal_path(base_dir: &Path, capture_generation_id: &str) -> PathBuf {
    let generation_id = crate::path_identity::guard_path_component(
        capture_generation_id,
        "capture_generation_terminal.generation_id",
    );
    base_dir
        .join(CAPTURE_GENERATION_SUBDIR)
        .join(GENERATION_TERMINAL_SUBDIR)
        .join(format!("{generation_id}.json"))
}

fn generation_lifecycle_lock_path(base_dir: &Path, capture_generation_id: &str) -> PathBuf {
    generation_terminal_path(base_dir, capture_generation_id).with_extension("lock")
}

/// Serializes the only two competing producer mutations for one immutable generation: arming a
/// member and publishing terminality. It runs exclusively on control/IO threads (never Audio).
pub(crate) fn with_generation_lifecycle_lock<T, E>(
    base_dir: &Path,
    capture_generation_id: &str,
    action: impl FnOnce() -> Result<T, E>,
) -> Result<T, E>
where
    E: From<io::Error>,
{
    let lock_path = generation_lifecycle_lock_path(base_dir, capture_generation_id);
    if let Some(parent) = lock_path.parent() {
        crate::atomic_file::create_private_dir_all(parent).map_err(E::from)?;
    }
    let mut options = OpenOptions::new();
    options.read(true).write(true).create(true).truncate(false);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let lock = options.open(lock_path).map_err(E::from)?;
    lock.lock().map_err(E::from)?;
    let result = action();
    let _ = lock.unlock();
    result
}

pub fn mark_generation_terminal(
    base_dir: &Path,
    generation: &CaptureGeneration,
    reason: GenerationTerminalReason,
) -> Result<CaptureGenerationTerminal, GenerationLifecycleError> {
    if !generation.is_valid() {
        return Err(GenerationLifecycleError::Invalid);
    }
    mark_generation_terminal_by_identity(
        base_dir,
        &generation.capture_generation_id,
        generation.started_at_ms,
        reason,
    )
}

/// Publishes an absorbing terminal fact. Equal retries return the first fact unchanged; a later
/// Stop/Drop cannot make the same immutable generation open again or rewrite its provenance.
pub fn mark_generation_terminal_by_identity(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
    reason: GenerationTerminalReason,
) -> Result<CaptureGenerationTerminal, GenerationLifecycleError> {
    if uuid::Uuid::parse_str(capture_generation_id.trim()).is_err() || generation_started_at_ms <= 0
    {
        return Err(GenerationLifecycleError::Invalid);
    }
    with_generation_lifecycle_lock(base_dir, capture_generation_id, || {
        if let Some(existing) = read_generation_terminal_or_quarantine_locked(
            base_dir,
            capture_generation_id,
            generation_started_at_ms,
        )? {
            return Ok(existing);
        }
        let terminal = CaptureGenerationTerminal {
            schema_version: GENERATION_TERMINAL_SCHEMA.to_string(),
            capture_generation_id: capture_generation_id.to_string(),
            generation_started_at_ms,
            terminal_reason: reason,
            terminal_at_ms: chrono::Utc::now().timestamp_millis(),
        };
        let json = serde_json::to_vec(&terminal)?;
        crate::atomic_file::write_bytes_atomic(
            &generation_terminal_path(base_dir, capture_generation_id),
            &json,
        )?;
        Ok(terminal)
    })
}

/// Reads one terminal fact while holding its lifecycle lock. Malformed JSON is not execution
/// authority: it is atomically moved out of the terminal address, preserving evidence for
/// diagnosis while allowing the exact live writer lease to decide whether work is still active.
pub(crate) fn read_generation_terminal_recovering(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
) -> Result<Option<CaptureGenerationTerminal>, GenerationLifecycleError> {
    with_generation_lifecycle_lock(base_dir, capture_generation_id, || {
        read_generation_terminal_or_quarantine_locked(
            base_dir,
            capture_generation_id,
            generation_started_at_ms,
        )
    })
}

/// Caller must hold `with_generation_lifecycle_lock` for this exact generation.
pub(crate) fn read_generation_terminal_or_quarantine_locked(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
) -> Result<Option<CaptureGenerationTerminal>, GenerationLifecycleError> {
    match read_generation_terminal(base_dir, capture_generation_id, generation_started_at_ms) {
        Ok(terminal) => Ok(terminal),
        Err(GenerationLifecycleError::Serde(_)) => {
            quarantine_terminal_locked(base_dir, capture_generation_id, generation_started_at_ms)?;
            Ok(None)
        }
        Err(GenerationLifecycleError::Invalid) => {
            // A well-formed terminal at the correct UUID path but with another start time is not
            // malformed residue. Preserve it and reject the mismatched caller; otherwise a stale
            // signal could quarantine the legitimate terminal fact for that UUID.
            if terminal_has_valid_path_identity_with_other_start(
                base_dir,
                capture_generation_id,
                generation_started_at_ms,
            )? {
                return Err(GenerationLifecycleError::Invalid);
            }
            quarantine_terminal_locked(base_dir, capture_generation_id, generation_started_at_ms)?;
            Ok(None)
        }
        Err(error) => Err(error),
    }
}

fn terminal_has_valid_path_identity_with_other_start(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
) -> Result<bool, GenerationLifecycleError> {
    let bytes = match fs::read(generation_terminal_path(base_dir, capture_generation_id)) {
        Ok(bytes) => bytes,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(false),
        Err(error) => return Err(error.into()),
    };
    let terminal: CaptureGenerationTerminal = match serde_json::from_slice(&bytes) {
        Ok(terminal) => terminal,
        Err(_) => return Ok(false),
    };
    Ok(terminal.schema_version == GENERATION_TERMINAL_SCHEMA
        && terminal.capture_generation_id == capture_generation_id
        && terminal.generation_started_at_ms > 0
        && terminal.generation_started_at_ms != generation_started_at_ms
        && terminal.terminal_at_ms > 0)
}

fn quarantine_terminal_locked(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
) -> Result<(), GenerationLifecycleError> {
    let source = generation_terminal_path(base_dir, capture_generation_id);
    let quarantine_dir = base_dir
        .join(CAPTURE_GENERATION_SUBDIR)
        .join(GENERATION_TERMINAL_SUBDIR)
        .join(GENERATION_TERMINAL_QUARANTINE_SUBDIR);
    crate::atomic_file::create_private_dir_all(&quarantine_dir)?;
    let generation_id = crate::path_identity::guard_path_component(
        capture_generation_id,
        "capture_generation_terminal.quarantine.generation_id",
    );
    let sequence = QUARANTINE_SEQUENCE.fetch_add(1, Ordering::Relaxed);
    let destination = quarantine_dir.join(format!(
        "{generation_id}.{generation_started_at_ms}.invalid.{}.{}.json",
        std::process::id(),
        sequence
    ));
    match fs::rename(source, destination) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(error.into()),
    }
}

pub fn read_generation_terminal(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
) -> Result<Option<CaptureGenerationTerminal>, GenerationLifecycleError> {
    if uuid::Uuid::parse_str(capture_generation_id.trim()).is_err() || generation_started_at_ms <= 0
    {
        return Ok(None);
    }
    let bytes = match fs::read(generation_terminal_path(base_dir, capture_generation_id)) {
        Ok(bytes) => bytes,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error.into()),
    };
    let terminal: CaptureGenerationTerminal = serde_json::from_slice(&bytes)?;
    if !terminal.is_valid_for(capture_generation_id, generation_started_at_ms) {
        return Err(GenerationLifecycleError::Invalid);
    }
    Ok(Some(terminal))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn terminal_fact_is_exact_durable_and_absorbing() {
        let base = tempfile::tempdir().unwrap();
        let generation = CaptureGeneration::new_single(
            "project-a".into(),
            "post-a".into(),
            "pre-a".into(),
            "daw-a".into(),
            std::process::id(),
        );
        let first =
            mark_generation_terminal(base.path(), &generation, GenerationTerminalReason::AllStop)
                .unwrap();
        let retry = mark_generation_terminal(
            base.path(),
            &generation,
            GenerationTerminalReason::DropCommitted,
        )
        .unwrap();

        assert_eq!(retry, first, "the first terminal transition is absorbing");
        assert!(read_generation_terminal(
            base.path(),
            &generation.capture_generation_id,
            generation.started_at_ms
        )
        .unwrap()
        .is_some());
        assert!(read_generation_terminal(
            base.path(),
            &generation.capture_generation_id,
            generation.started_at_ms + 1
        )
        .is_err());
    }

    #[test]
    fn malformed_terminal_is_quarantined_once_and_does_not_block_publication() {
        let base = tempfile::tempdir().unwrap();
        let generation = CaptureGeneration::new_single(
            "project-a".into(),
            "post-a".into(),
            "pre-a".into(),
            "daw-a".into(),
            std::process::id(),
        );
        let terminal_path =
            generation_terminal_path(base.path(), &generation.capture_generation_id);
        crate::atomic_file::create_private_dir_all(terminal_path.parent().unwrap()).unwrap();
        std::fs::write(&terminal_path, b"{malformed").unwrap();

        assert!(read_generation_terminal_recovering(
            base.path(),
            &generation.capture_generation_id,
            generation.started_at_ms,
        )
        .unwrap()
        .is_none());
        assert!(!terminal_path.exists());
        let quarantine_dir = base
            .path()
            .join(CAPTURE_GENERATION_SUBDIR)
            .join(GENERATION_TERMINAL_SUBDIR)
            .join(GENERATION_TERMINAL_QUARANTINE_SUBDIR);
        assert_eq!(std::fs::read_dir(&quarantine_dir).unwrap().count(), 1);

        let terminal =
            mark_generation_terminal(base.path(), &generation, GenerationTerminalReason::AllStop)
                .unwrap();
        assert_eq!(
            read_generation_terminal(
                base.path(),
                &generation.capture_generation_id,
                generation.started_at_ms,
            )
            .unwrap(),
            Some(terminal),
        );
    }

    #[test]
    fn wrong_start_caller_cannot_quarantine_a_valid_terminal() {
        let base = tempfile::tempdir().unwrap();
        let generation = CaptureGeneration::new_single(
            "project-a".into(),
            "post-a".into(),
            "pre-a".into(),
            "daw-a".into(),
            std::process::id(),
        );
        mark_generation_terminal(base.path(), &generation, GenerationTerminalReason::AllStop)
            .unwrap();
        let terminal_path =
            generation_terminal_path(base.path(), &generation.capture_generation_id);

        assert!(matches!(
            read_generation_terminal_recovering(
                base.path(),
                &generation.capture_generation_id,
                generation.started_at_ms + 1,
            ),
            Err(GenerationLifecycleError::Invalid)
        ));
        assert!(terminal_path.exists());
    }
}
