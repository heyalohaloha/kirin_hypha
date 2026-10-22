//! Preparation progress on the existing exact-session PRE entry claim.
use super::*;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub(super) struct EntryPreparation {
    capture_generation_id: String,
    generation_started_at_ms: i64,
    record_generation: u64,
    samples: u64,
}

#[cfg(test)]
#[path = "record_entry_preparation_tests.rs"]
mod tests;

fn matches(lock: &RecordEntryLockFile, project: &str, session: &str, pre: &str, pid: u32) -> bool {
    lock.schema_version == RECORD_ENTRY_LOCK_SCHEMA
        && lock.project_hash == project
        && lock.record_session_id == session
        && lock.role == Role::Pre.dir_name()
        && lock.instance_id == pre
        && lock.host_process_id == pid
}

/// IO writer only. Heartbeats, retries and idle wakes never increment this counter; only
/// completed Watch chunks supplied by the exact Record generation do. Never creates a claim.
pub(crate) fn publish_measure_preparation(
    base: &Path,
    project: &str,
    pre: &str,
    signal: &crate::record_signal::RecordSignal,
    record_generation: u64,
    samples: u64,
) -> Result<(), RecordEntryLockError> {
    if samples == 0
        || record_generation == 0
        || signal.capture_generation_id.is_empty()
        || signal.generation_started_at_ms <= 0
        || signal.target_pre_instance_id != pre
    {
        return Ok(());
    }
    let path = entry_lock_path(base, project, &signal.session_id, Role::Pre, pre);
    let mut lock: RecordEntryLockFile = serde_json::from_slice(&fs::read(&path)?)?;
    if !matches(&lock, project, &signal.session_id, pre, std::process::id()) {
        return Ok(());
    }
    if let Some(prior) = &lock.measure_preparation {
        if prior.capture_generation_id != signal.capture_generation_id
            || prior.generation_started_at_ms != signal.generation_started_at_ms
            || prior.record_generation != record_generation
            || samples <= prior.samples
        {
            return Ok(());
        }
    }
    lock.measure_preparation = Some(EntryPreparation {
        capture_generation_id: signal.capture_generation_id.clone(),
        generation_started_at_ms: signal.generation_started_at_ms,
        record_generation,
        samples,
    });
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&lock)?)?;
    Ok(())
}

/// Reading progress never grants producer readiness. Old Record/session/PID evidence cannot
/// renew the current generation's no-progress window.
pub(crate) fn measure_preparation_samples(
    base: &Path,
    generation: &crate::capture_generation::CaptureGeneration,
    member: &crate::capture_generation::CaptureGenerationMember,
) -> usize {
    let path = entry_lock_path(
        base,
        &member.project_hash,
        &member.record_session_id,
        Role::Pre,
        &member.pre_instance_id,
    );
    let Some(lock) = fs::read(path)
        .ok()
        .and_then(|bytes| serde_json::from_slice::<RecordEntryLockFile>(&bytes).ok())
    else {
        return 0;
    };
    if !matches(
        &lock,
        &member.project_hash,
        &member.record_session_id,
        &member.pre_instance_id,
        generation.host_process_id,
    ) {
        return 0;
    }
    lock.measure_preparation
        .filter(|p| {
            p.capture_generation_id == generation.capture_generation_id
                && p.generation_started_at_ms == generation.started_at_ms
                && p.record_generation > 0
        })
        .map_or(0, |p| usize::try_from(p.samples).unwrap_or(usize::MAX))
}
