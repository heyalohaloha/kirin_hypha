//! Process-local own-entry bookkeeping. Only IO/control threads call this module.
//! The table protects bookkeeping only; an entry token serializes its IO without waiting.
use super::*;
use std::collections::HashMap;
use std::sync::{Mutex, MutexGuard, OnceLock};

#[derive(Clone)]
pub(in crate::record_entry_lock) struct Owned {
    base: PathBuf,
    claim: RecordEntryLockFile,
    preparation: Option<EntryPreparation>,
    pending: Option<EntryPreparation>,
    stopped: bool,
    busy: bool,
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
            pending: None,
            stopped: false,
            busy: false,
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

/// While this token exists no other local IO for this owner may start. Stop only marks it.
pub(in crate::record_entry_lock) struct Work {
    path: PathBuf,
    owned: Owned,
    active: bool,
}
impl Drop for Work {
    fn drop(&mut self) {
        if !self.active {
            return;
        }
        if let Ok(mut table) = owners() {
            if let Some(current) = table.get_mut(&self.path) {
                if same_owner(&self.owned.claim, &current.claim) {
                    current.busy = false;
                    current.pending = None;
                }
            }
        }
    }
}
pub(in crate::record_entry_lock) fn acquire(
    path: &Path,
    incoming: Option<&EntryPreparation>,
) -> Result<Option<Work>, RecordEntryLockError> {
    let mut table = owners()?;
    let Some(owned) = table.get_mut(path) else {
        return Ok(None);
    };
    if owned.busy {
        return Ok(None);
    }
    owned.busy = true;
    owned.pending = incoming.cloned();
    Ok(Some(Work {
        path: path.to_path_buf(),
        owned: owned.clone(),
        active: true,
    }))
}
impl Work {
    pub(super) fn before_progress(
        &mut self,
        lock: &RecordEntryLockFile,
        incoming: &EntryPreparation,
    ) -> Result<bool, RecordEntryLockError> {
        if !same_owner(&self.owned.claim, lock) {
            self.retire()?;
            return Ok(false);
        }
        let terminal = crate::capture_generation_lifecycle::read_generation_terminal(
            &self.owned.base,
            &incoming.capture_generation_id,
            incoming.generation_started_at_ms,
        )
        .map_err(|e| io::Error::other(e.to_string()))?
        .is_some();
        let stopped = owners()?
            .get(&self.path)
            .is_some_and(|o| same_owner(&self.owned.claim, &o.claim) && o.stopped);
        if stopped || terminal || ready(&self.owned, incoming) {
            self.mark_stopped()?;
            self.finish()?;
            return Ok(false);
        }
        Ok(true)
    }
    fn mark_stopped(&mut self) -> Result<(), RecordEntryLockError> {
        self.owned.stopped = true;
        if let Some(o) = owners()?.get_mut(&self.path) {
            if same_owner(&self.owned.claim, &o.claim) {
                o.stopped = true;
            }
        }
        Ok(())
    }
    fn retire(&self) -> Result<(), RecordEntryLockError> {
        let mut table = owners()?;
        if table.get(&self.path).is_some_and(|o| {
            same_owner(&self.owned.claim, &o.claim) && o.preparation == self.owned.preparation
        }) {
            table.remove(&self.path);
        }
        Ok(())
    }
    pub(super) fn published(
        &mut self,
        incoming: EntryPreparation,
    ) -> Result<(), RecordEntryLockError> {
        self.owned.preparation = Some(incoming.clone());
        {
            let mut table = owners()?;
            let Some(o) = table.get_mut(&self.path) else {
                return Ok(());
            };
            if !same_owner(&self.owned.claim, &o.claim) {
                return Ok(());
            }
            o.preparation = Some(incoming.clone());
            self.owned.stopped = o.stopped;
        }
        // A ready/terminal publication may have arrived while this writer held its token.
        let terminal = crate::capture_generation_lifecycle::read_generation_terminal(
            &self.owned.base,
            &incoming.capture_generation_id,
            incoming.generation_started_at_ms,
        )
        .map_err(|e| io::Error::other(e.to_string()))?
        .is_some();
        if terminal || ready(&self.owned, &incoming) {
            self.mark_stopped()?;
        }
        // Linearize completion against a concurrent Stop: either it marks this token,
        // or it sees idle and takes its own token. Drop cannot release a later token.
        let finish = {
            let mut table = owners()?;
            match table.get_mut(&self.path) {
                Some(o) if same_owner(&self.owned.claim, &o.claim) => {
                    if o.stopped {
                        true
                    } else {
                        o.busy = false;
                        o.pending = None;
                        self.active = false;
                        false
                    }
                }
                _ => {
                    self.active = false;
                    false
                }
            }
        };
        if finish {
            self.finish()?;
        }
        Ok(())
    }
    fn finish(&mut self) -> Result<(), RecordEntryLockError> {
        let Some(last) = &self.owned.preparation else {
            return self.retire();
        };
        let current: RecordEntryLockFile = serde_json::from_slice(&fs::read(&self.path)?)?;
        if same_owner(&self.owned.claim, &current)
            && current.measure_preparation.as_ref() == Some(last)
        {
            crate::atomic_file::write_bytes_atomic_durable(
                &self.path,
                &serde_json::to_vec(&current)?,
            )?;
        }
        // Replaced/finished ownership is not a reason to retain this process-local entry.
        self.retire()
    }
}
pub(crate) fn finish_ready_preparation(writer_path: &Path) {
    let result = (|| -> Result<(), RecordEntryLockError> {
        let candidates: Vec<_> = owners()?
            .iter()
            .filter(|(_, o)| {
                crate::record_writer_claim::writer_claim_path(
                    &o.base,
                    &o.claim.project_hash,
                    &o.claim.record_session_id,
                    role(&o.claim),
                    &o.claim.instance_id,
                ) == writer_path
            })
            .map(|(p, o)| (p.clone(), o.clone()))
            .collect();
        for (path, snapshot) in candidates {
            let preparation = snapshot.pending.as_ref().or(snapshot.preparation.as_ref());
            if preparation.is_none() {
                if let Some(work) = acquire(&path, None)? {
                    if same_owner(&snapshot.claim, &work.owned.claim) {
                        work.retire()?;
                    }
                }
                continue;
            }
            if !preparation.is_some_and(|p| ready(&snapshot, p)) {
                continue;
            }
            {
                let mut table = owners()?;
                let Some(o) = table.get_mut(&path) else {
                    continue;
                };
                if !same_owner(&snapshot.claim, &o.claim) {
                    continue;
                }
                o.stopped = true;
            }
            if let Some(mut work) = acquire(&path, None)? {
                if same_owner(&snapshot.claim, &work.owned.claim) && work.owned.stopped {
                    work.finish()?;
                }
            }
        }
        Ok(())
    })();
    if let Err(error) = result {
        log::warn!("entry preparation completion sync failed: {error}");
    }
}
pub(crate) fn finish_terminal_preparations(base: &Path, generation: &str, started: i64) {
    let result = (|| -> Result<(), RecordEntryLockError> {
        let paths = {
            let mut table = owners()?;
            let mut paths = Vec::new();
            for (path, o) in table.iter_mut() {
                let matches = |p: &EntryPreparation| {
                    p.capture_generation_id == generation && p.generation_started_at_ms == started
                };
                if o.base == base
                    && (o.preparation.as_ref().is_some_and(matches)
                        || o.pending.as_ref().is_some_and(matches))
                {
                    o.stopped = true;
                    if !o.busy {
                        paths.push((path.clone(), o.claim.clone()));
                    }
                }
            }
            paths
        };
        let mut first_error = None;
        for (path, expected) in paths {
            if let Some(mut work) = acquire(&path, None)? {
                if same_owner(&expected, &work.owned.claim) && work.owned.stopped {
                    if let Err(error) = work.finish() {
                        first_error.get_or_insert(error);
                    }
                }
            }
        }
        first_error.map_or(Ok(()), Err)
    })();
    if let Err(error) = result {
        log::warn!("entry preparation terminal sync failed: {error}");
    }
}
