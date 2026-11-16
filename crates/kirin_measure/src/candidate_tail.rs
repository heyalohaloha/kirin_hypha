//! Non-RT, per-POST exclusion while downstream comparison audio may remain.
//! Stable kernel locks cross plug-in formats and release on process exit.
use std::fs::{File, OpenOptions, TryLockError};
use std::io;
use std::path::Path;

use crate::capture_generation::CaptureGeneration;
use crate::path_identity::guard_path_component;

pub struct TailLease(File);

impl TailLease {
    pub fn acquire(base: &Path, project: &str, post: &str) -> io::Result<Option<Self>> {
        if project.is_empty() || post.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "missing POST identity",
            ));
        }
        let dir = base
            .join("candidate_tail")
            .join(guard_path_component(project, "tail.project").as_ref());
        crate::atomic_file::durable::create_private_dir_all(&dir)?;
        let name = format!("{}.lock", guard_path_component(post, "tail.post"));
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(dir.join(name))?;
        match file.try_lock() {
            Ok(()) => Ok(Some(Self(file))),
            Err(TryLockError::WouldBlock) => Ok(None),
            Err(TryLockError::Error(error)) => Err(error),
        }
    }
}

impl Drop for TailLease {
    fn drop(&mut self) {
        let _ = self.0.unlock();
    }
}

/// Retain every member's lease through the active-generation publication.
pub fn lock_generation(
    base: &Path,
    generation: &CaptureGeneration,
) -> io::Result<Option<Vec<TailLease>>> {
    let mut leases = Vec::with_capacity(generation.members.len());
    for member in &generation.members {
        match TailLease::acquire(base, &member.project_hash, &member.post_instance_id)? {
            Some(lease) => leases.push(lease),
            None => return Ok(None),
        }
    }
    Ok(Some(leases))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::capture_generation::{CaptureGeneration, CaptureGenerationMember};
    use crate::capture_generation_tx::CaptureGenerationTransaction;
    use crate::plugin_data::Role;
    use std::time::Duration;

    #[test]
    fn tail_cannot_start_between_staging_and_generation_commit() {
        let temp = tempfile::tempdir().unwrap();
        let generation = CaptureGeneration::new_for_members(
            "post".into(),
            "daw".into(),
            std::process::id(),
            vec![CaptureGenerationMember {
                project_hash: "project".into(),
                post_instance_id: "post".into(),
                pre_instance_id: "pre".into(),
                record_session_id: String::new(),
            }],
        );
        let mut transaction =
            CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
        transaction.stage().unwrap();
        assert!(TailLease::acquire(temp.path(), "project", "post")
            .unwrap()
            .is_none());
        let member = &generation.members[0];
        let mut writers = Vec::new();
        for (role, id) in [(Role::Pre, "pre"), (Role::Post, "post")] {
            let mut writer = crate::record_writer_claim::claim_writer_for_generation(
                temp.path(),
                &member.project_hash,
                &member.record_session_id,
                role,
                id,
                &generation.capture_generation_id,
                generation.started_at_ms,
            )
            .unwrap();
            writer.mark_ready().unwrap();
            writers.push(writer);
        }
        transaction
            .commit_when_ready(Duration::from_secs(1))
            .unwrap();
        assert!(TailLease::acquire(temp.path(), "project", "post")
            .unwrap()
            .is_none());
        drop(transaction);
        assert!(TailLease::acquire(temp.path(), "project", "post")
            .unwrap()
            .is_some());
    }
}
