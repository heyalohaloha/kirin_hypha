//! Process-local own-entry bookkeeping. Only IO/control threads call this module.
//! Progress is not terminal authority: ready/lifecycle files remain the authorities.
use super::*;
use std::collections::HashMap;
use std::sync::{Mutex, MutexGuard, OnceLock};

pub(in crate::record_entry_lock) struct Owned {
    base: PathBuf,
    claim: RecordEntryLockFile,
    preparation: Option<EntryPreparation>,
    stopped: bool,
    synced: bool,
}

type Owners = HashMap<PathBuf, Owned>;
static OWNERS: OnceLock<Mutex<Owners>> = OnceLock::new();

pub(in crate::record_entry_lock) fn owners(
) -> Result<MutexGuard<'static, Owners>, RecordEntryLockError> {
    OWNERS
        .get_or_init(|| Mutex::new(HashMap::new()))
        .lock()
        .map_err(|_| io::Error::other("entry preparation owner state poisoned").into())
}

pub(in crate::record_entry_lock) fn register(
    owners: &mut Owners,
    base: &Path,
    path: PathBuf,
    claim: RecordEntryLockFile,
) {
    owners.insert(
        path,
        Owned {
            base: base.to_path_buf(),
            claim,
            preparation: None,
            stopped: false,
            synced: false,
        },
    );
}

fn same_owner(expected: &RecordEntryLockFile, actual: &RecordEntryLockFile) -> bool {
    actual.schema_version == RECORD_ENTRY_LOCK_SCHEMA
        && actual.host_process_id == std::process::id()
        && actual.owner_id == expected.owner_id
        && actual.project_hash == expected.project_hash
        && actual.record_session_id == expected.record_session_id
        && actual.role == expected.role
        && actual.instance_id == expected.instance_id
        && actual.claimed_at_ms == expected.claimed_at_ms
}

fn role(value: &RecordEntryLockFile) -> Role {
    if value.role == Role::Pre.dir_name() {
        Role::Pre
    } else {
        Role::Post
    }
}

fn ready(owned: &Owned, preparation: &EntryPreparation) -> bool {
    crate::record_writer_claim::writer_claim_ready_for_generation(
        &owned.base,
        &owned.claim.project_hash,
        &owned.claim.record_session_id,
        role(&owned.claim),
        &owned.claim.instance_id,
        &preparation.capture_generation_id,
        preparation.generation_started_at_ms,
        std::process::id(),
    )
    .unwrap_or(false)
}

pub(super) fn before_progress(
    owners: &mut Owners,
    path: &Path,
    lock: &RecordEntryLockFile,
    incoming: &EntryPreparation,
) -> Result<bool, RecordEntryLockError> {
    let Some(owned) = owners.get_mut(path) else {
        return Ok(false);
    };
    if !same_owner(&owned.claim, lock) {
        return Ok(false);
    }
    let terminal = crate::capture_generation_lifecycle::read_generation_terminal(
        &owned.base,
        &incoming.capture_generation_id,
        incoming.generation_started_at_ms,
    )
    .map_err(|e| io::Error::other(e.to_string()))?
    .is_some();
    if owned.stopped || terminal || ready(owned, incoming) {
        owned.stopped = true;
        finish_one(path, owned)?;
        return Ok(false);
    }
    Ok(true)
}

pub(super) fn published(owners: &mut Owners, path: &Path, preparation: EntryPreparation) {
    if let Some(owned) = owners.get_mut(path) {
        owned.preparation = Some(preparation);
    }
}

fn finish_one(path: &Path, owned: &mut Owned) -> Result<(), RecordEntryLockError> {
    if owned.synced {
        return Ok(());
    }
    let Some(last) = &owned.preparation else {
        return Ok(());
    };
    let current: RecordEntryLockFile = serde_json::from_slice(&fs::read(path)?)?;
    if !same_owner(&owned.claim, &current) || current.measure_preparation.as_ref() != Some(last) {
        // Replacement/foreign mutation is not permission to write on its behalf.
        return Ok(());
    }
    crate::atomic_file::write_bytes_atomic_durable(path, &serde_json::to_vec(&current)?)?;
    owned.synced = true;
    Ok(())
}

/// Called after the actual writer ready publication. No path scan or foreign owner writes.
pub(crate) fn finish_ready_preparation(writer_path: &Path) {
    let result = (|| -> Result<(), RecordEntryLockError> {
        let mut owners = owners()?;
        for (path, owned) in owners.iter_mut() {
            if crate::record_writer_claim::writer_claim_path(
                &owned.base,
                &owned.claim.project_hash,
                &owned.claim.record_session_id,
                role(&owned.claim),
                &owned.claim.instance_id,
            ) != writer_path
            {
                continue;
            }
            if owned.preparation.as_ref().is_some_and(|p| ready(owned, p)) {
                owned.stopped = true;
                finish_one(path, owned)?;
            }
        }
        Ok(())
    })();
    if let Err(error) = result {
        log::warn!("entry preparation completion sync failed: {error}");
    }
}

/// Called after successful (including repeated) lifecycle publication, even for by-identity
/// endpoints with no roster. Only entries this process actually claimed and published qualify.
pub(crate) fn finish_terminal_preparations(base: &Path, generation: &str, started: i64) {
    let result = (|| -> Result<(), RecordEntryLockError> {
        let mut owners = owners()?;
        let mut first_error = None;
        for (path, owned) in owners.iter_mut() {
            if owned.base != base
                || !owned.preparation.as_ref().is_some_and(|p| {
                    p.capture_generation_id == generation && p.generation_started_at_ms == started
                })
            {
                continue;
            }
            owned.stopped = true;
            if let Err(error) = finish_one(path, owned) {
                first_error.get_or_insert(error);
            }
        }
        if let Some(error) = first_error {
            return Err(error);
        }
        Ok(())
    })();
    if let Err(error) = result {
        log::warn!("entry preparation terminal sync failed: {error}");
    }
}
