//! Bounded observational counterpart of try_observe_pair_claim. Never creates a lock/claim.
use super::*;
use crate::pre_candidates::pair_preview::budget::{Budget, Stop};
use std::fs::TryLockError;

pub(crate) fn preview_owner(
    root: &Path,
    host: u32,
    pre: &str,
    budget: &mut Budget<'_>,
) -> Result<Option<String>, Stop> {
    let lock_path = lock_path(root, host, pre).map_err(|_| Stop::Uncertain)?;
    let claim_path = claim_path(root, host, pre).map_err(|_| Stop::Uncertain)?;
    let lock = budget.open_existing(&lock_path, true)?;
    if let Some(file) = &lock {
        match budget.operation(|| Ok(file.try_lock()))? {
            Ok(()) => (),
            Err(TryLockError::WouldBlock) | Err(TryLockError::Error(_)) => {
                return Err(Stop::Uncertain)
            }
        }
    }
    // Holding the opened file keeps the transaction stable, including early error returns.
    let bytes = budget.json(&claim_path)?;
    let Some(bytes) = bytes else {
        return Ok(None);
    };
    if lock.is_none() {
        return Err(Stop::Uncertain);
    }
    let claim: PairClaim = serde_json::from_slice(&bytes).map_err(|_| Stop::Uncertain)?;
    if !claim_is_well_formed(&claim)
        || claim.schema != PAIR_CLAIM_SCHEMA
        || claim.host_process_id != host
        || claim.pre_instance_id != pre
    {
        return Err(Stop::Uncertain);
    }
    let instance = root.join(&claim.project_hash).join(&claim.post_instance_id);
    let marker = crate::pair_ownership_marker::owner_marker_path(
        &instance,
        &claim.pair_owner_id,
        pre,
        claim.pair_claimed_at_bits,
    )
    .ok_or(Stop::Uncertain)?;
    let proof = crate::pair_ownership_marker::engine_binding_proof_path(
        root,
        &instance,
        &claim.pair_owner_id,
        host,
        pre,
        claim.pair_claimed_at_bits,
    )
    .ok_or(Stop::Uncertain)?;
    let owned = budget.locked(&marker)? || budget.locked(&proof)?;
    Ok(owned.then_some(claim.post_instance_id))
}
