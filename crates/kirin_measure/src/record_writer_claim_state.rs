//! Claim state transitions and snapshot reading. Schema and owner checks are unchanged.
use super::*;

impl WriterClaimGuard {
    pub(crate) fn heartbeat(&mut self) -> Result<(), WriterClaimError> {
        ensure_current_owner(&self.path, &self.claim.owner_id)?;
        let now = now_epoch_ms();
        self.claim.heartbeat_at_ms = now;
        write_state_atomic(
            &self.path,
            &self.claim.owner_id,
            now,
            self.claim.ready_at_ms,
            self.claim.closed_at_ms,
        )?;
        Ok(())
    }

    /// Publishes producer readiness only after the Record artifact exists and its initial flush
    /// completed. Ownership alone is deliberately not readiness: generation publication must not
    /// race ahead of writer construction.
    pub(crate) fn mark_ready(&mut self) -> Result<(), WriterClaimError> {
        ensure_current_owner(&self.path, &self.claim.owner_id)?;
        let now = now_epoch_ms();
        write_state_durable(
            &self.path,
            &self.claim.owner_id,
            now,
            Some(now),
            self.claim.closed_at_ms,
        )?;
        // Do not let a failed transition leak through a later plain heartbeat.
        self.claim.heartbeat_at_ms = now;
        self.claim.ready_at_ms = Some(now);
        Ok(())
    }

    pub(crate) fn mark_closed(&mut self) -> Result<(), WriterClaimError> {
        ensure_current_owner(&self.path, &self.claim.owner_id)?;
        let now = now_epoch_ms();
        write_state_durable(
            &self.path,
            &self.claim.owner_id,
            now,
            self.claim.ready_at_ms,
            Some(now),
        )?;
        self.claim.heartbeat_at_ms = now;
        self.claim.closed_at_ms = Some(now);
        self.finalized = true;
        Ok(())
    }

    pub(crate) fn abandon(mut self) -> Result<(), WriterClaimError> {
        release_if_current_owner(&self.path, &self.claim.owner_id)?;
        self.finalized = true;
        Ok(())
    }
}

pub(super) fn write_state_atomic(
    claim_path: &Path,
    owner_id: &str,
    heartbeat_at_ms: i64,
    ready_at_ms: Option<i64>,
    closed_at_ms: Option<i64>,
) -> Result<(), WriterClaimError> {
    write_state(
        claim_path,
        owner_id,
        heartbeat_at_ms,
        ready_at_ms,
        closed_at_ms,
        false,
    )
}

fn write_state_durable(
    claim_path: &Path,
    owner_id: &str,
    heartbeat_at_ms: i64,
    ready_at_ms: Option<i64>,
    closed_at_ms: Option<i64>,
) -> Result<(), WriterClaimError> {
    write_state(
        claim_path,
        owner_id,
        heartbeat_at_ms,
        ready_at_ms,
        closed_at_ms,
        true,
    )
}

fn write_state(
    claim_path: &Path,
    owner_id: &str,
    heartbeat_at_ms: i64,
    ready_at_ms: Option<i64>,
    closed_at_ms: Option<i64>,
    durable: bool,
) -> Result<(), WriterClaimError> {
    let state = WriterClaimStateFile {
        schema_version: WRITER_CLAIM_SCHEMA.to_string(),
        owner_id: owner_id.to_string(),
        heartbeat_at_ms,
        ready_at_ms,
        closed_at_ms,
    };
    let bytes = serde_json::to_vec(&state)?;
    // Low-frequency readiness/closure is durable; the 30-second heartbeat stays
    // asynchronous after its per-write sync cost exceeded the accepted budget.
    if durable {
        crate::atomic_file::write_bytes_atomic_durable(&claim_state_path(claim_path), &bytes)?;
    } else {
        crate::atomic_file::write_bytes_atomic(&claim_state_path(claim_path), &bytes)?;
    }
    Ok(())
}

pub(super) fn read_claim_snapshot(
    path: &Path,
) -> Result<Option<WriterClaimSnapshot>, WriterClaimError> {
    let Some(claim) = read_existing_claim(path)? else {
        return Ok(None);
    };
    // A damaged/unreadable mutable sidecar is not fresh execution/readiness.
    // Retain only the immutable owner's original heartbeat for execution/readiness.
    // Acquisition separately considers body/sidecar mtimes for recovery, including
    // a readable body with a damaged sidecar; that does not invent a fresh snapshot.
    let state = read_existing_state(path).ok().flatten().filter(|state| {
        state.owner_id == claim.owner_id && state.schema_version == WRITER_CLAIM_SCHEMA
    });
    let heartbeat_at_ms = state
        .as_ref()
        .map(|state| state.heartbeat_at_ms)
        .unwrap_or(claim.heartbeat_at_ms);
    let closed_at_ms = state
        .as_ref()
        .and_then(|state| state.closed_at_ms)
        .or(claim.closed_at_ms);
    let ready_at_ms = state
        .as_ref()
        .and_then(|state| state.ready_at_ms)
        .or(claim.ready_at_ms);
    Ok(Some(WriterClaimSnapshot {
        project_hash: claim.project_hash,
        record_session_id: claim.record_session_id,
        instance_id: claim.instance_id,
        role: claim.role,
        owner_id: claim.owner_id,
        host_process_id: claim.host_process_id,
        capture_generation_id: claim.capture_generation_id,
        generation_started_at_ms: claim.generation_started_at_ms,
        heartbeat_at_ms,
        ready_at_ms,
        closed_at_ms,
    }))
}

pub(super) fn read_existing_claim(
    path: &Path,
) -> Result<Option<WriterClaimFile>, WriterClaimError> {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(e.into()),
    };
    let claim: WriterClaimFile = serde_json::from_slice(&bytes)?;
    if claim.schema_version != WRITER_CLAIM_SCHEMA {
        return Ok(None);
    }
    Ok(Some(claim))
}

fn read_existing_state(path: &Path) -> Result<Option<WriterClaimStateFile>, WriterClaimError> {
    let state_path = claim_state_path(path);
    let bytes = match fs::read(state_path) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(e.into()),
    };
    Ok(Some(serde_json::from_slice(&bytes)?))
}

pub(super) fn claim_state_path(path: &Path) -> PathBuf {
    let mut file_name = path
        .file_name()
        .map(|name| name.to_os_string())
        .unwrap_or_else(|| "claim.json".into());
    file_name.push(".state");
    path.with_file_name(file_name)
}

pub(super) fn snapshot_matches_writer(
    snapshot: &WriterClaimSnapshot,
    project_hash: &str,
    session_id: &str,
    role: Role,
    instance_id: &str,
) -> bool {
    snapshot.project_hash == project_hash
        && snapshot.record_session_id == session_id
        && snapshot.instance_id == instance_id
        && snapshot.role == role.dir_name()
}
