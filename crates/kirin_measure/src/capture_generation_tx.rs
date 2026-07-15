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
use crate::record_signal::read_signal;

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

    /// Move producer readiness waiting off the host message thread.
    ///
    /// Keep itself only stages immutable intent. This short-lived control worker owns the
    /// transaction until every exact writer has reached the barrier or the bounded timeout
    /// aborts it. It never touches the Audio Thread or audio buffers.
    pub fn commit_when_ready_async(
        mut self,
        timeout: Duration,
        completed: impl FnOnce(Result<(), CaptureGenerationError>) + Send + 'static,
    ) -> Result<(), CaptureGenerationError> {
        thread::Builder::new()
            .name("hypha-keep-barrier".to_string())
            .spawn(move || {
                let result = self.commit_when_ready(timeout);
                completed(result);
            })
            .map(|_| ())
            .map_err(CaptureGenerationError::Io)
    }

    /// Private final pointer promotion. The only production entry is
    /// [`Self::commit_when_ready`], so no caller can bypass writer readiness.
    fn commit(&mut self) -> Result<(), CaptureGenerationError> {
        self.commit_observing_active(|| {})
    }

    /// The observer exists so the release test can inspect the only formerly unsafe promotion
    /// instant: active has changed and preparation has not yet been retired. Production always
    /// passes a zero-cost no-op through [`Self::commit`].
    fn commit_observing_active(
        &mut self,
        after_active_publish: impl FnOnce(),
    ) -> Result<(), CaptureGenerationError> {
        if !self.staged {
            return Err(CaptureGenerationError::Io(io::Error::new(
                io::ErrorKind::InvalidInput,
                "capture generation was not staged",
            )));
        }
        crate::capture_generation_lifecycle::with_generation_lifecycle_lock(
            &self.base_dir,
            &self.generation.capture_generation_id,
            || {
                match crate::capture_generation_lifecycle::read_generation_terminal_or_quarantine_locked(
                    &self.base_dir,
                    &self.generation.capture_generation_id,
                    self.generation.started_at_ms,
                ) {
                    Ok(None) => {}
                    Ok(Some(_)) => {
                        return Err(CaptureGenerationError::Io(io::Error::new(
                            io::ErrorKind::AlreadyExists,
                            "terminal capture generation cannot be committed",
                        )))
                    }
                    Err(error) => {
                        return Err(CaptureGenerationError::Io(io::Error::new(
                            io::ErrorKind::InvalidData,
                            format!("capture generation terminal state unreadable: {error}"),
                        )))
                    }
                }
                // Readiness is revalidated inside the same lifecycle critical section as active
                // publication. A writer that closed or was replaced after the outer polling loop
                // cannot lend its stale `ready` state to this commit.
                if !generation_producers_ready(&self.base_dir, &self.generation) {
                    return Err(CaptureGenerationError::Io(io::Error::new(
                        io::ErrorKind::TimedOut,
                        "capture generation exact producer attestation changed before commit",
                    )));
                }
                let json = serde_json::to_vec(&self.generation)?;
                // Persist the exact 1–12 member roster before advancing its discovery pointer.
                // Later Keeps may overwrite `active/current.json`; Drop follows this immutable
                // generation address and therefore never loses an earlier legitimate WAV.
                crate::capture_generation::archive_generation(&self.base_dir, &self.generation)?;
                // `preparing` is the Stop authority until the consumer barrier points at this
                // exact generation. Publish the new active pointer first, then compare-remove
                // preparation. At every observable filesystem state Stop therefore resolves the
                // new generation; there is no gap in which retained old active can win.
                crate::atomic_file::write_bytes_atomic(
                    &active_generation_path(&self.base_dir),
                    &json,
                )?;
                after_active_publish();
                remove_preparing_if_current(&self.base_dir, &self.generation);
                Ok(())
            },
        )?;
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
    // Close start authority before touching member signals. A worker already inside the
    // generation lock may finish publishing Pending first; the following release then advances
    // it to the absorbing state. A later worker observes terminality and cannot arm.
    let _ = crate::capture_generation_lifecycle::mark_generation_terminal(
        base_dir,
        generation,
        crate::capture_generation_lifecycle::GenerationTerminalReason::Aborted,
    );
    for member in &generation.members {
        let owned_signal = read_signal(base_dir, &member.project_hash, &member.post_instance_id)
            .filter(|signal| {
                signal.capture_generation_id == generation.capture_generation_id
                    && signal.generation_started_at_ms == generation.started_at_ms
                    && signal.session_id == member.record_session_id
            });
        if let Some(signal) = owned_signal {
            let _ = crate::record_signal::mark_released_with_reason_if_current(
                base_dir,
                &member.project_hash,
                &member.post_instance_id,
                &signal,
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
        let _ = crate::all_stop_signal::write_stop_broadcast_for_generation(
            base_dir,
            project_hash,
            &generation.originator_post_instance_id,
            generation.daw_session_id.clone(),
            generation.host_process_id,
            generation,
        );
    }
}

fn generation_producers_ready(base_dir: &Path, generation: &CaptureGeneration) -> bool {
    if !process_is_alive(generation.host_process_id) {
        return false;
    }
    generation.members.iter().all(|member| {
        !member.pre_instance_id.trim().is_empty()
            && crate::record_writer_claim::writer_claim_ready_for_generation(
                base_dir,
                &member.project_hash,
                &member.record_session_id,
                Role::Pre,
                &member.pre_instance_id,
                &generation.capture_generation_id,
                generation.started_at_ms,
                generation.host_process_id,
            )
            .unwrap_or(false)
            && crate::record_writer_claim::writer_claim_ready_for_generation(
                base_dir,
                &member.project_hash,
                &member.record_session_id,
                Role::Post,
                &member.post_instance_id,
                &generation.capture_generation_id,
                generation.started_at_ms,
                generation.host_process_id,
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
    crate::capture_generation_lifecycle::with_generation_lifecycle_lock(
        base_dir,
        &generation.capture_generation_id,
        || -> Result<bool, crate::capture_generation_lifecycle::GenerationLifecycleError> {
            // Malformed terminal residue is quarantined under the same lock that serializes a
            // legitimate terminal publication. It is evidence, not execution authority.
            let _ =
                crate::capture_generation_lifecycle::read_generation_terminal_or_quarantine_locked(
                    base_dir,
                    &generation.capture_generation_id,
                    generation.started_at_ms,
                )?;
            Ok(generation.members.iter().any(|member| {
                [
                    (Role::Pre, member.pre_instance_id.as_str()),
                    (Role::Post, member.post_instance_id.as_str()),
                ]
                .into_iter()
                .any(|(role, instance_id)| {
                    match crate::record_writer_claim::writer_claim_execution_active_for_generation(
                        base_dir,
                        &member.project_hash,
                        &member.record_session_id,
                        role,
                        instance_id,
                        &generation.capture_generation_id,
                        generation.started_at_ms,
                        generation.host_process_id,
                    ) {
                        Ok(active) => active,
                        // A malformed claim cannot prove immutable identity, liveness, or
                        // renewal. Real filesystem I/O failure remains conservative.
                        Err(crate::record_writer_claim::WriterClaimError::Io(_)) => true,
                        Err(_) => false,
                    }
                })
            }))
        },
    )
    // Filesystem permission/I/O failure is still conservative. Only malformed JSON is removed as
    // a permanent blocker by the quarantine path above.
    .unwrap_or(true)
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
