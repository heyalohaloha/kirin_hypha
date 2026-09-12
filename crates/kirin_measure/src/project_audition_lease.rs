//! Project-scoped exclusion between comparison audition and Record admission.
//!
//! The stable kernel lock is shared by every host process. Acquisition and Record reservation
//! both serialize through the same project registry lock, closing the start/start race without
//! relying on a host PID or optional host document APIs.

use chrono::Utc;
use std::fs::{File, OpenOptions, TryLockError};
use std::io;
use std::path::{Path, PathBuf};

use crate::reservation::{
    reclaim_stale_project_reservations_unlocked, reservation_dir, ProjectLock,
};

const AUDITION_LOCK_FILE: &str = ".audition.lock";

#[derive(Debug)]
pub struct ProjectAuditionLease {
    base_dir: PathBuf,
    project_hash: String,
    held: Option<File>,
}

impl ProjectAuditionLease {
    pub fn new(base_dir: &Path, project_hash: &str) -> Self {
        Self {
            base_dir: base_dir.to_path_buf(),
            project_hash: project_hash.to_string(),
            held: None,
        }
    }

    pub fn try_acquire(&mut self) -> io::Result<bool> {
        if self.held.is_some() {
            return Ok(true);
        }
        let dir = reservation_dir(&self.base_dir, &self.project_hash);
        let _registry = ProjectLock::acquire(&dir)?;
        reclaim_stale_project_reservations_unlocked(&self.base_dir, &self.project_hash, Utc::now());
        if project_has_record_reservation(&dir)? {
            return Ok(false);
        }
        let file = open_lock(&dir)?;
        match file.try_lock() {
            Ok(()) => {
                self.held = Some(file);
                Ok(true)
            }
            Err(TryLockError::WouldBlock) => Ok(false),
            Err(TryLockError::Error(error)) => Err(error),
        }
    }

    pub fn release(&mut self) {
        if let Some(file) = self.held.take() {
            let _ = file.unlock();
        }
    }

    pub(crate) fn active_while_registry_locked(dir: &Path) -> io::Result<bool> {
        let file = open_lock(dir)?;
        match file.try_lock() {
            Ok(()) => {
                file.unlock()?;
                Ok(false)
            }
            Err(TryLockError::WouldBlock) => Ok(true),
            Err(TryLockError::Error(error)) => Err(error),
        }
    }
}

impl Drop for ProjectAuditionLease {
    fn drop(&mut self) {
        self.release();
    }
}

fn open_lock(dir: &Path) -> io::Result<File> {
    std::fs::create_dir_all(dir)?;
    OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(dir.join(AUDITION_LOCK_FILE))
}

fn project_has_record_reservation(dir: &Path) -> io::Result<bool> {
    Ok(std::fs::read_dir(dir)?
        .flatten()
        .any(|entry| entry.path().extension().and_then(|value| value.to_str()) == Some("json")))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::reservation::{release_pairing, reserve_pairing, ReserveOutcome};

    #[test]
    fn audition_and_record_are_mutually_exclusive_across_instances() {
        let temp = tempfile::tempdir().unwrap();
        let mut audition = ProjectAuditionLease::new(temp.path(), "project");
        assert!(audition.try_acquire().unwrap());
        assert_eq!(
            reserve_pairing(temp.path(), "project", "pre", "post").unwrap(),
            ReserveOutcome::AuditionInUse
        );
        audition.release();
        assert_eq!(
            reserve_pairing(temp.path(), "project", "pre", "post").unwrap(),
            ReserveOutcome::Created
        );
        assert!(!audition.try_acquire().unwrap());
        release_pairing(temp.path(), "project", "pre", "post");
        assert!(audition.try_acquire().unwrap());
    }

    #[test]
    fn projects_do_not_block_each_other() {
        let temp = tempfile::tempdir().unwrap();
        let mut first = ProjectAuditionLease::new(temp.path(), "first");
        let mut second = ProjectAuditionLease::new(temp.path(), "second");
        assert!(first.try_acquire().unwrap());
        assert!(second.try_acquire().unwrap());
    }
}
