//! Exact writer readiness and failure-only inspection of the same claim.
use super::*;

/// Failure-only, non-RT observation. These reads never participate in the readiness decision.
#[allow(clippy::too_many_arguments)]
pub(crate) fn writer_claim_readiness_diagnostic(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
    host_process_id: u32,
) -> serde_json::Value {
    let path = writer_claim_path(base_dir, project_hash, session_id, role, instance_id);
    match read_claim_snapshot(&path) {
        Err(error) => serde_json::json!({"read_error": error.to_string()}),
        Ok(None) => serde_json::json!({"claim_present": false}),
        Ok(Some(claim)) => {
            let age_ms = now_epoch_ms().saturating_sub(claim.heartbeat_at_ms);
            serde_json::json!({
                "claim_present": true,
                "project_matches": claim.project_hash == project_hash,
                "session_matches": claim.record_session_id == session_id,
                "role_matches": claim.role == role.dir_name(),
                "instance_matches": claim.instance_id == instance_id,
                "instance_nonempty": !instance_id.trim().is_empty(),
                "artifact_ready": claim.ready_at_ms.is_some(),
                "closed": claim.closed_at_ms.is_some(),
                "generation_matches": claim.capture_generation_id.as_deref() == Some(capture_generation_id),
                "started_at_matches": claim.generation_started_at_ms == Some(generation_started_at_ms),
                "host_process_matches": claim.host_process_id == host_process_id,
                "host_process_alive": process_is_alive(claim.host_process_id),
                "heartbeat_current": age_ms <= WRITER_CLAIM_STALE_MS,
                "heartbeat_age_ms": age_ms,
                "heartbeat_limit_ms": WRITER_CLAIM_STALE_MS,
                "claim_generation": claim.capture_generation_id,
                "claim_started_at_ms": claim.generation_started_at_ms,
                "claim_host_process_id": claim.host_process_id,
            })
        }
    }
}

/// Returns true only for a live owner that has completed artifact creation and initial flush.
/// This is the producer barrier used by capture generation publication.
/// Exact producer activation barrier. A current session/role/iid claim from another generation
/// is stale data, not readiness, even if its mutable state says `ready`.
#[allow(clippy::too_many_arguments)]
pub(crate) fn writer_claim_ready_for_generation(
    base_dir: &Path,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
    host_process_id: u32,
) -> Result<bool, WriterClaimError> {
    let path = writer_claim_path(base_dir, project_hash, session_id, role, instance_id);
    let Some(existing) = read_claim_snapshot(&path)? else {
        return Ok(false);
    };
    if !snapshot_matches_writer(&existing, project_hash, session_id, role, instance_id)
        || existing.ready_at_ms.is_none()
        || existing.closed_at_ms.is_some()
        || existing.capture_generation_id.as_deref() != Some(capture_generation_id)
        || existing.generation_started_at_ms != Some(generation_started_at_ms)
        || existing.host_process_id != host_process_id
        || !process_is_alive(existing.host_process_id)
    {
        return Ok(false);
    }
    let age_ms = now_epoch_ms().saturating_sub(existing.heartbeat_at_ms);
    Ok(age_ms <= WRITER_CLAIM_STALE_MS)
}
