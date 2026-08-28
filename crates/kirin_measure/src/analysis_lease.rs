//! Process-wide ownership for the one optional POST Analysis worker.
//!
//! The stable per-process file is intentionally retained after release. Removing a lock file can
//! race a new owner that already opened the old inode, allowing a third owner on a replacement
//! file. The kernel releases the lock automatically on close or process crash.

use std::fs::{File, OpenOptions, TryLockError};
use std::io;
use std::path::PathBuf;

#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;

#[derive(Debug)]
pub(super) struct AnalysisLease {
    path: PathBuf,
    file: Option<File>,
}

impl AnalysisLease {
    #[cfg(not(test))]
    pub(super) fn for_current_process() -> Self {
        Self::at_path(
            std::env::temp_dir()
                .join("kirin")
                .join("analysis")
                .join(format!("{}.lease", std::process::id())),
        )
    }

    pub(super) fn at_path(path: PathBuf) -> Self {
        Self { path, file: None }
    }

    pub(super) fn try_acquire(&mut self) -> io::Result<bool> {
        if self.file.is_some() {
            return Ok(true);
        }
        let Some(parent) = self.path.parent() else {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "analysis lease has no parent directory",
            ));
        };
        std::fs::create_dir_all(parent)?;
        let mut options = OpenOptions::new();
        options.read(true).write(true).create(true);
        #[cfg(unix)]
        options.mode(0o600);
        let file = options.open(&self.path)?;
        match file.try_lock() {
            Ok(()) => {
                self.file = Some(file);
                Ok(true)
            }
            Err(TryLockError::WouldBlock) => Ok(false),
            Err(TryLockError::Error(error)) => Err(error),
        }
    }

    pub(super) fn release(&mut self) {
        if let Some(file) = self.file.take() {
            let _ = file.unlock();
        }
    }
}

impl Drop for AnalysisLease {
    fn drop(&mut self) {
        self.release();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exactly_one_owner_holds_a_stable_kernel_lock() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("one-analysis.lease");
        let mut first = AnalysisLease::at_path(path.clone());
        let mut second = AnalysisLease::at_path(path);
        assert!(first.try_acquire().unwrap());
        assert!(!second.try_acquire().unwrap());
        first.release();
        assert!(second.try_acquire().unwrap());
    }

    #[test]
    fn invalid_parent_fails_without_claiming() {
        let temp = tempfile::tempdir().unwrap();
        let blocker = temp.path().join("file");
        std::fs::write(&blocker, b"not a directory").unwrap();
        let mut lease = AnalysisLease::at_path(blocker.join("analysis.lease"));
        assert!(lease.try_acquire().is_err());
    }
}
