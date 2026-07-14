//! Cross-process transaction for publishing one capture generation.
//!
//! Project-local pointers and Record/broadcast inboxes are staged while one
//! installation-wide file lock is held. The active pointer is written last and
//! is the only commit barrier consumers may trust.

use std::collections::BTreeSet;
use std::fs::{File, OpenOptions, TryLockError};
use std::io;
use std::path::{Path, PathBuf};

use crate::capture_generation::{
    active_generation_path, publish_current_generation, read_active_generation, CaptureGeneration,
    CaptureGenerationError, CAPTURE_GENERATION_SUBDIR,
};
use crate::record_signal::{read_signal, SignalStatus};

const PUBLISH_LOCK_FILENAME: &str = ".publish.lock";

/// Holds the installation-wide generation lock from roster staging through
/// active-pointer commit. Dropping an uncommitted transaction leaves staged
/// files non-authoritative and therefore harmless.
pub struct CaptureGenerationTransaction {
    base_dir: PathBuf,
    generation: CaptureGeneration,
    lock: File,
    staged: bool,
}

impl CaptureGenerationTransaction {
    pub fn begin(
        base_dir: &Path,
        generation: &CaptureGeneration,
    ) -> Result<Self, CaptureGenerationError> {
        if !generation.is_valid() {
            return Err(CaptureGenerationError::Invalid);
        }

        let lock_path = base_dir
            .join(CAPTURE_GENERATION_SUBDIR)
            .join(PUBLISH_LOCK_FILENAME);
        if let Some(parent) = lock_path.parent() {
            crate::atomic_file::create_private_dir_all(parent)?;
        }
        let mut options = OpenOptions::new();
        options.read(true).write(true).create(true).truncate(false);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let lock = options.open(lock_path)?;
        match lock.try_lock() {
            Ok(()) => {}
            Err(TryLockError::WouldBlock) => {
                return Err(CaptureGenerationError::Io(io::Error::new(
                    io::ErrorKind::WouldBlock,
                    "capture generation publication is already in progress",
                )));
            }
            Err(TryLockError::Error(error)) => return Err(error.into()),
        }

        if let Some(active) = read_active_generation(base_dir)? {
            let same_generation = active.capture_generation_id == generation.capture_generation_id
                && active.started_at_ms == generation.started_at_ms;
            if !same_generation && generation_has_open_member(base_dir, &active) {
                return Err(CaptureGenerationError::Io(io::Error::new(
                    io::ErrorKind::WouldBlock,
                    "another capture generation is still active",
                )));
            }
        }

        Ok(Self {
            base_dir: base_dir.to_path_buf(),
            generation: generation.clone(),
            lock,
            staged: false,
        })
    }

    /// Writes every project pointer while keeping the global active pointer
    /// unchanged. Callers may now stage all exact member inboxes.
    pub fn stage(&mut self) -> Result<(), CaptureGenerationError> {
        let projects = self
            .generation
            .members
            .iter()
            .map(|member| member.project_hash.as_str())
            .collect::<BTreeSet<_>>();
        for project_hash in projects {
            publish_current_generation(&self.base_dir, project_hash, &self.generation)?;
        }
        self.staged = true;
        Ok(())
    }

    /// Commits the generation only after every producer inbox has been staged.
    pub fn commit(&mut self) -> Result<(), CaptureGenerationError> {
        if !self.staged {
            return Err(CaptureGenerationError::Io(io::Error::new(
                io::ErrorKind::InvalidInput,
                "capture generation was not staged",
            )));
        }
        let json = serde_json::to_vec(&self.generation)?;
        crate::atomic_file::write_bytes_atomic(&active_generation_path(&self.base_dir), &json)?;
        Ok(())
    }
}

impl Drop for CaptureGenerationTransaction {
    fn drop(&mut self) {
        let _ = self.lock.unlock();
    }
}

fn generation_has_open_member(base_dir: &Path, generation: &CaptureGeneration) -> bool {
    if !process_is_alive(generation.host_process_id) {
        return false;
    }
    generation.members.iter().any(|member| {
        read_signal(base_dir, &member.project_hash, &member.post_instance_id).is_some_and(
            |signal| {
                signal.status != SignalStatus::Released
                    && signal.capture_generation_id == generation.capture_generation_id
                    && signal.generation_started_at_ms == generation.started_at_ms
                    && signal.session_id == member.record_session_id
            },
        )
    })
}

#[cfg(unix)]
fn process_is_alive(process_id: u32) -> bool {
    if process_id == 0 || process_id > i32::MAX as u32 {
        return false;
    }
    // SAFETY: kill(pid, 0) sends no signal; it only asks the kernel whether the
    // process exists. EPERM is also positive liveness evidence.
    let result = unsafe { libc::kill(process_id as i32, 0) };
    result == 0 || std::io::Error::last_os_error().raw_os_error() == Some(libc::EPERM)
}

#[cfg(not(unix))]
fn process_is_alive(process_id: u32) -> bool {
    // Without a portable non-enumerating process probe, retain ownership
    // conservatively. The shipping macOS path above has crash recovery.
    process_id != 0
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::capture_generation::{
        read_active_generation, read_current_generation, CaptureGenerationMember,
    };

    fn generation() -> CaptureGeneration {
        CaptureGeneration::new_for_members(
            "post-a".into(),
            "daw-a".into(),
            std::process::id(),
            vec![CaptureGenerationMember {
                project_hash: "project-a".into(),
                post_instance_id: "post-a".into(),
                pre_instance_id: "pre-a".into(),
                record_session_id: String::new(),
            }],
        )
    }

    #[test]
    fn staged_roster_is_not_active_until_commit() {
        let temp = tempfile::tempdir().unwrap();
        let generation = generation();
        let mut transaction =
            CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();

        transaction.stage().unwrap();

        assert_eq!(
            read_current_generation(temp.path(), "project-a")
                .unwrap()
                .unwrap(),
            generation
        );
        assert!(read_active_generation(temp.path()).unwrap().is_none());

        transaction.commit().unwrap();
        assert_eq!(
            read_active_generation(temp.path()).unwrap().unwrap(),
            generation
        );
    }

    #[test]
    fn commit_without_stage_is_rejected() {
        let temp = tempfile::tempdir().unwrap();
        let generation = generation();
        let mut transaction =
            CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();

        assert!(transaction.commit().is_err());
        assert!(read_active_generation(temp.path()).unwrap().is_none());
    }

    #[test]
    fn concurrent_publisher_is_rejected_without_waiting() {
        let temp = tempfile::tempdir().unwrap();
        let first = generation();
        let first_transaction = CaptureGenerationTransaction::begin(temp.path(), &first).unwrap();
        let second = generation();

        let error = CaptureGenerationTransaction::begin(temp.path(), &second)
            .err()
            .expect("the publication lock must be non-blocking");

        assert!(matches!(
            error,
            CaptureGenerationError::Io(ref error)
                if error.kind() == io::ErrorKind::WouldBlock
        ));
        drop(first_transaction);
    }

    #[test]
    fn live_open_generation_cannot_be_replaced() {
        let temp = tempfile::tempdir().unwrap();
        let first = generation();
        let mut first_transaction =
            CaptureGenerationTransaction::begin(temp.path(), &first).unwrap();
        first_transaction.stage().unwrap();
        crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
            temp.path(),
            "project-a",
            "post-a",
            "pre-a".into(),
            "daw-a".into(),
            None,
            &first,
        )
        .unwrap();
        first_transaction.commit().unwrap();
        drop(first_transaction);

        let second = generation();
        let error = CaptureGenerationTransaction::begin(temp.path(), &second)
            .err()
            .expect("a live open generation must retain ownership");
        assert!(matches!(
            error,
            CaptureGenerationError::Io(ref error)
                if error.kind() == io::ErrorKind::WouldBlock
        ));
        assert_eq!(read_active_generation(temp.path()).unwrap().unwrap(), first);
    }

    #[test]
    fn dead_producer_generation_does_not_block_restart() {
        let temp = tempfile::tempdir().unwrap();
        let mut abandoned = generation();
        abandoned.host_process_id = i32::MAX as u32;
        let mut abandoned_transaction =
            CaptureGenerationTransaction::begin(temp.path(), &abandoned).unwrap();
        abandoned_transaction.stage().unwrap();
        crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
            temp.path(),
            "project-a",
            "post-a",
            "pre-a".into(),
            "daw-a".into(),
            None,
            &abandoned,
        )
        .unwrap();
        abandoned_transaction.commit().unwrap();
        drop(abandoned_transaction);

        let replacement = generation();
        let transaction = CaptureGenerationTransaction::begin(temp.path(), &replacement)
            .expect("a crashed producer must not own the next Keep");
        drop(transaction);
    }
}
