//! Cross-process transaction for publishing one capture generation.
//!
//! Project-local pointers and Record/broadcast inboxes are staged while one
//! installation-wide file lock is held. The active pointer is written last and
//! is the only commit barrier consumers may trust.

use std::collections::{BTreeMap, BTreeSet};
use std::fs::{File, OpenOptions, TryLockError};
use std::io;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::{Duration, Instant};

use crate::capture_generation::{
    active_generation_path, current_generation_path, preparing_generation_path,
    publish_current_generation, read_active_generation, read_current_generation,
    read_preparing_generation, CaptureGeneration, CaptureGenerationError,
    CAPTURE_GENERATION_SUBDIR,
};
use crate::plugin_data::Role;
use crate::record_signal::{read_signal, SignalStatus};

const PUBLISH_LOCK_FILENAME: &str = ".publish.lock";

/// Holds the installation-wide generation lock from roster staging through
/// active-pointer commit. Dropping an uncommitted transaction releases its exact
/// signals, wakes its exact member shelves, and restores the previous pointers.
pub struct CaptureGenerationTransaction {
    base_dir: PathBuf,
    generation: CaptureGeneration,
    lock: File,
    staged: bool,
    committed: bool,
    previous_project_generations: BTreeMap<String, Option<CaptureGeneration>>,
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
            // An open generation has exactly one producer transaction. Re-entering even with the
            // same immutable id would give the second guard rollback authority over the active
            // writers when it drops. A new transaction is therefore allowed only after every
            // exact member has closed (or the producer process is gone).
            if generation_has_open_member(base_dir, &active) {
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
            committed: false,
            previous_project_generations: BTreeMap::new(),
        })
    }

    /// Writes every project pointer, then publishes the producer-only preparation barrier while
    /// keeping the consumer-authoritative active pointer unchanged. Callers may now stage exact
    /// member inboxes; Kirin OS still sees only the previously committed generation.
    pub fn stage(&mut self) -> Result<(), CaptureGenerationError> {
        if self.staged {
            return Err(CaptureGenerationError::Io(io::Error::new(
                io::ErrorKind::AlreadyExists,
                "capture generation was already staged",
            )));
        }
        // Own cleanup before the first filesystem mutation. If any later write fails, Drop can
        // remove the exact partial roster instead of leaving a misleading project pointer.
        self.staged = true;
        let projects = self
            .generation
            .members
            .iter()
            .map(|member| member.project_hash.as_str())
            .collect::<BTreeSet<_>>();
        for project_hash in projects {
            let previous = read_current_generation(&self.base_dir, project_hash)?;
            self.previous_project_generations
                .insert(project_hash.to_string(), previous);
            publish_current_generation(&self.base_dir, project_hash, &self.generation)?;
        }
        let json = serde_json::to_vec(&self.generation)?;
        crate::atomic_file::write_bytes_atomic(&preparing_generation_path(&self.base_dir), &json)?;
        Ok(())
    }

    /// Waits for both PRE and POST writers of every immutable roster member to own their exact
    /// Record session, then promotes the active pointer. This runs only on a user/control thread;
    /// it performs no audio-thread work and never scans directories or historical sessions.
    pub fn commit_when_ready(&mut self, timeout: Duration) -> Result<(), CaptureGenerationError> {
        if !self.staged {
            return Err(CaptureGenerationError::Io(io::Error::new(
                io::ErrorKind::InvalidInput,
                "capture generation was not staged",
            )));
        }
        let deadline = Instant::now() + timeout;
        loop {
            if generation_producers_ready(&self.base_dir, &self.generation) {
                return self.commit();
            }
            if Instant::now() >= deadline {
                return Err(CaptureGenerationError::Io(io::Error::new(
                    io::ErrorKind::TimedOut,
                    "capture generation producers did not become ready",
                )));
            }
            thread::sleep(Duration::from_millis(10));
        }
    }

    /// Private final pointer promotion. The only production entry is
    /// [`Self::commit_when_ready`], so no caller can bypass writer readiness.
    fn commit(&mut self) -> Result<(), CaptureGenerationError> {
        if !self.staged {
            return Err(CaptureGenerationError::Io(io::Error::new(
                io::ErrorKind::InvalidInput,
                "capture generation was not staged",
            )));
        }
        let json = serde_json::to_vec(&self.generation)?;
        remove_preparing_if_current(&self.base_dir, &self.generation);
        crate::atomic_file::write_bytes_atomic(&active_generation_path(&self.base_dir), &json)?;
        self.committed = true;
        Ok(())
    }
}

impl Drop for CaptureGenerationTransaction {
    fn drop(&mut self) {
        if self.staged && !self.committed {
            // Rollback is part of the transaction contract, not a caller convention. Release only
            // signals that still identify this immutable generation, then notify the exact member
            // shelves so already-started remote state machines exit Record as well.
            abort_uncommitted_generation(&self.base_dir, &self.generation);
            remove_preparing_if_current(&self.base_dir, &self.generation);
            for (project_hash, previous) in &self.previous_project_generations {
                restore_project_pointer_if_current(
                    &self.base_dir,
                    project_hash,
                    &self.generation,
                    previous.as_ref(),
                );
            }
        }
        let _ = self.lock.unlock();
    }
}

fn abort_uncommitted_generation(base_dir: &Path, generation: &CaptureGeneration) {
    for member in &generation.members {
        let owns_signal = read_signal(base_dir, &member.project_hash, &member.post_instance_id)
            .is_some_and(|signal| {
                signal.capture_generation_id == generation.capture_generation_id
                    && signal.generation_started_at_ms == generation.started_at_ms
                    && signal.session_id == member.record_session_id
            });
        if owns_signal {
            let _ = crate::record_signal::mark_released_with_reason(
                base_dir,
                &member.project_hash,
                &member.post_instance_id,
                crate::record_signal::ReleaseReason::ManualStop,
            );
        }
    }

    for project_hash in generation
        .members
        .iter()
        .map(|member| member.project_hash.as_str())
        .collect::<BTreeSet<_>>()
    {
        let _ = crate::all_stop_signal::write_stop_broadcast_with_scope(
            base_dir,
            project_hash,
            &generation.originator_post_instance_id,
            generation.daw_session_id.clone(),
            generation.host_process_id,
        );
    }
}

fn generation_producers_ready(base_dir: &Path, generation: &CaptureGeneration) -> bool {
    generation.members.iter().all(|member| {
        !member.pre_instance_id.trim().is_empty()
            && crate::record_writer_claim::writer_claim_ready(
                base_dir,
                &member.project_hash,
                &member.record_session_id,
                Role::Pre,
                &member.pre_instance_id,
            )
            .unwrap_or(false)
            && crate::record_writer_claim::writer_claim_ready(
                base_dir,
                &member.project_hash,
                &member.record_session_id,
                Role::Post,
                &member.post_instance_id,
            )
            .unwrap_or(false)
    })
}

fn remove_preparing_if_current(base_dir: &Path, generation: &CaptureGeneration) {
    if read_preparing_generation(base_dir)
        .ok()
        .flatten()
        .as_ref()
        .is_some_and(|current| same_generation(current, generation))
    {
        let _ = std::fs::remove_file(preparing_generation_path(base_dir));
    }
}

fn restore_project_pointer_if_current(
    base_dir: &Path,
    project_hash: &str,
    generation: &CaptureGeneration,
    previous: Option<&CaptureGeneration>,
) {
    if read_current_generation(base_dir, project_hash)
        .ok()
        .flatten()
        .as_ref()
        .is_some_and(|current| same_generation(current, generation))
    {
        if let Some(previous) = previous {
            let _ = publish_current_generation(base_dir, project_hash, previous);
        } else {
            let _ = std::fs::remove_file(current_generation_path(base_dir, project_hash));
        }
    }
}

fn same_generation(left: &CaptureGeneration, right: &CaptureGeneration) -> bool {
    left.capture_generation_id == right.capture_generation_id
        && left.started_at_ms == right.started_at_ms
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
#[path = "capture_generation_tx_tests.rs"]
mod tests;
