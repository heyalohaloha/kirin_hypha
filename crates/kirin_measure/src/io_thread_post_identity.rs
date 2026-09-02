//! Runtime identity snapshots and exact POST pairing/broadcast predicates.

use std::sync::{Arc, RwLock};

/// Read the latest persisted identity without allowing a poisoned lock to stop IO.
pub(crate) fn read_instance_id_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// Read the latest project shelf identity after a chunk restore.
pub(super) fn read_project_hash_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// Read the engine-scoped DAW session identity, never the process-global fallback.
pub(super) fn read_daw_session_id_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// Snapshot the user's current PRE selector. Poisoning falls back to no selection.
pub(super) fn snapshot_pair_pre_name(arc: &Arc<RwLock<String>>) -> String {
    arc.read().map(|g| g.clone()).unwrap_or_default()
}

pub(super) fn pair_claim_matches_desired_binding(
    claim: &crate::PairClaim,
    pre_instance_id: Option<&str>,
    project_hash: &str,
    post_instance_id: &str,
    pair_owner_id: &str,
    pair_claimed_at: f64,
) -> bool {
    pre_instance_id == Some(claim.pre_instance_id.as_str())
        && claim.project_hash == project_hash
        && claim.post_instance_id == post_instance_id
        && claim.pair_owner_id == pair_owner_id
        && claim.pair_claimed_at_bits == pair_claimed_at.to_bits()
}

fn same_project_host_broadcast_matches(
    local_host_process_id: u32,
    remote_host_process_id: u32,
) -> bool {
    local_host_process_id != 0
        && remote_host_process_id != 0
        && local_host_process_id == remote_host_process_id
}

pub(super) fn broadcast_scope_or_same_project_host_matches(
    local_daw_session_id: &str,
    local_host_process_id: u32,
    remote_daw_session_id: &str,
    remote_host_process_id: u32,
) -> bool {
    crate::broadcast_scope_ids_match(
        local_daw_session_id,
        local_host_process_id,
        remote_daw_session_id,
        remote_host_process_id,
    ) || same_project_host_broadcast_matches(local_host_process_id, remote_host_process_id)
}
