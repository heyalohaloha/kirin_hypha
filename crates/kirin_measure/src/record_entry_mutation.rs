//! IO-only, non-waiting disk exclusion shared by separately linked plugin binaries.
use super::*;
use std::fs::{File, OpenOptions, TryLockError};

// Keep the sibling inode. Unlinking it while an opener holds it could create two locks.
pub(super) fn try_acquire(entry: &Path) -> Result<File, RecordEntryLockError> {
    let name = entry
        .file_name()
        .ok_or_else(|| io::Error::other("entry name missing"))?;
    let path = entry.with_file_name(format!("{}.lock", name.to_string_lossy().to_lowercase()));
    let mut options = OpenOptions::new();
    options.read(true).write(true).create(true).truncate(false);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let file = options.open(path)?;
    match file.try_lock() {
        Ok(()) => Ok(file),
        Err(TryLockError::WouldBlock) => Err(RecordEntryLockError::AlreadyActive {
            path: entry.to_path_buf(),
            owner_id: None,
            heartbeat_at_ms: None,
        }),
        Err(TryLockError::Error(error)) => Err(error.into()),
    }
}
