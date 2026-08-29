//! Process-wide ownership for at most two optional POST Analysis workers.
//!
//! Each slot has its own stable per-process file that is intentionally retained after release.
//! Removing a lock file can race a new owner that already opened the old inode, allowing an extra
//! owner on a replacement file. The kernel releases every held slot automatically on close or
//! process crash.

use std::fs::{File, OpenOptions, TryLockError};
use std::io;
use std::path::PathBuf;

#[cfg(not(test))]
use crate::storage::PlatformPaths;

#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;

#[derive(Debug)]
pub(super) struct AnalysisLease {
    paths: Vec<PathBuf>,
    held: Option<(usize, File)>,
}

impl AnalysisLease {
    #[cfg(not(test))]
    pub(super) fn for_current_process() -> Self {
        let root = PlatformPaths::current_kirin_tmp_root().join("analysis");
        Self::at_paths([
            root.join(format!("{}.0.lease", std::process::id())),
            root.join(format!("{}.1.lease", std::process::id())),
        ])
    }

    #[cfg(test)]
    pub(super) fn at_path(path: PathBuf) -> Self {
        Self::at_paths([path])
    }

    pub(super) fn at_paths(paths: impl IntoIterator<Item = PathBuf>) -> Self {
        Self {
            paths: paths.into_iter().collect(),
            held: None,
        }
    }

    pub(super) fn try_acquire(&mut self) -> io::Result<bool> {
        if self.held.is_some() {
            return Ok(true);
        }
        if self.paths.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "analysis lease has no slots",
            ));
        }
        let mut first_error = None;
        for (index, path) in self.paths.iter().enumerate() {
            let Some(parent) = path.parent() else {
                first_error.get_or_insert_with(|| {
                    io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "analysis lease has no parent directory",
                    )
                });
                continue;
            };
            if let Err(error) = std::fs::create_dir_all(parent) {
                first_error.get_or_insert(error);
                continue;
            }
            let mut options = OpenOptions::new();
            options.read(true).write(true).create(true);
            #[cfg(unix)]
            options.mode(0o600);
            let file = match options.open(path) {
                Ok(file) => file,
                Err(error) => {
                    first_error.get_or_insert(error);
                    continue;
                }
            };
            match file.try_lock() {
                Ok(()) => {
                    self.held = Some((index, file));
                    return Ok(true);
                }
                Err(TryLockError::WouldBlock) => {}
                Err(TryLockError::Error(error)) => {
                    first_error.get_or_insert(error);
                }
            }
        }
        match first_error {
            Some(error) => Err(error),
            None => Ok(false),
        }
    }

    pub(super) fn release(&mut self) {
        if let Some((_, file)) = self.held.take() {
            let _ = file.unlock();
        }
    }

    #[cfg(test)]
    pub(super) fn held_slot(&self) -> Option<usize> {
        self.held.as_ref().map(|(index, _)| *index)
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
    fn two_owners_hold_distinct_stable_kernel_slots_and_third_waits() {
        let temp = tempfile::tempdir().unwrap();
        let paths = [
            temp.path().join("analysis.0.lease"),
            temp.path().join("analysis.1.lease"),
        ];
        let make_lease = || AnalysisLease::at_paths(paths.clone());
        let mut first = make_lease();
        let mut second = make_lease();
        let mut third = make_lease();
        assert!(first.try_acquire().unwrap());
        assert!(second.try_acquire().unwrap());
        assert_eq!(first.held_slot(), Some(0));
        assert_eq!(second.held_slot(), Some(1));
        assert!(!third.try_acquire().unwrap());
        first.release();
        assert!(third.try_acquire().unwrap());
        assert_eq!(third.held_slot(), Some(0));
    }

    #[test]
    fn invalid_parent_fails_without_claiming() {
        let temp = tempfile::tempdir().unwrap();
        let blocker = temp.path().join("file");
        std::fs::write(&blocker, b"not a directory").unwrap();
        let mut lease = AnalysisLease::at_path(blocker.join("analysis.lease"));
        assert!(lease.try_acquire().is_err());
    }

    #[test]
    fn one_broken_slot_does_not_hide_an_available_valid_slot() {
        let temp = tempfile::tempdir().unwrap();
        let blocker = temp.path().join("file");
        std::fs::write(&blocker, b"not a directory").unwrap();
        let valid = temp.path().join("analysis.1.lease");
        let mut lease = AnalysisLease::at_paths([blocker.join("analysis.0.lease"), valid]);
        assert!(lease.try_acquire().unwrap());
        assert_eq!(lease.held_slot(), Some(1));
    }
}
