//! Transient acquisition reservations only; disk claims remain ownership authority.
use super::*;
use std::collections::HashSet;
use std::sync::{Mutex, OnceLock};

static ACQUIRING: OnceLock<Mutex<HashSet<PathBuf>>> = OnceLock::new();

pub(super) struct Reservation(PathBuf);

pub(super) fn reserve(path: &Path) -> Result<Reservation, RecordEntryLockError> {
    // The parent was created by the caller. Canonicalize outside the table lock
    // so relative paths and symlink aliases cannot reserve the same entry twice.
    let parent = path
        .parent()
        .ok_or_else(|| io::Error::other("entry parent missing"))?;
    let name = path
        .file_name()
        .ok_or_else(|| io::Error::other("entry name missing"))?;
    let key = fs::canonicalize(parent)?.join(name);
    let mut acquiring = ACQUIRING
        .get_or_init(Default::default)
        .lock()
        .map_err(|_| io::Error::other("entry acquisition table poisoned"))?;
    if !acquiring.insert(key.clone()) {
        return Err(RecordEntryLockError::AlreadyActive {
            path: path.to_path_buf(),
            owner_id: None,
            heartbeat_at_ms: None,
        });
    }
    Ok(Reservation(key))
}

impl Drop for Reservation {
    fn drop(&mut self) {
        // Cleanup is safe even after unrelated panic; no user code runs under lock.
        ACQUIRING
            .get_or_init(Default::default)
            .lock()
            .unwrap_or_else(|poison| poison.into_inner())
            .remove(&self.0);
    }
}
