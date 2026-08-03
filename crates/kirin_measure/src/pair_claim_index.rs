//! Exact POST ownership index for one PRE runtime.
//!
//! A POST publishes one fixed claim while committing its exact engine-owned PRE binding. The
//! later `post.json` publication is freshness/measurement evidence, not the exclusivity point.
//! Directory enumeration is deliberately absent.

use serde::{Deserialize, Serialize};
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use crate::post_candidates::{current_host_process_id, PostTmpJson};
use crate::pre_discovery::DISCOVERY_STALE_SECS;

pub const PAIR_CLAIM_SCHEMA: &str = "pair_claim.v2";
const LEGACY_PAIR_CLAIM_SCHEMA: &str = "pair_claim.v1";
const PAIR_CLAIM_SUBDIR: &str = "pair_target";

#[path = "pair_claim_transaction.rs"]
mod pair_claim_transaction;
pub use pair_claim_transaction::publish_pair_claim;
use pair_claim_transaction::ClaimLock;
pub(crate) use pair_claim_transaction::{prepare_pair_claim, PreparedPairClaim};

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct PairClaim {
    pub schema: String,
    pub pre_instance_id: String,
    pub project_hash: String,
    pub post_instance_id: String,
    /// Legacy v1 owner identity. v1 tied pair ownership to the restartable Watch writer.
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub post_watch_owner_id: String,
    /// v2 engine-lifetime pair owner identity. Watch/IO restarts never change this value.
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub pair_owner_id: String,
    pub host_process_id: u32,
    pub pair_claimed_at_bits: u64,
}

impl PairClaim {
    pub fn pair_claimed_at(&self) -> f64 {
        f64::from_bits(self.pair_claimed_at_bits)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PublishPairClaimOutcome {
    Published,
    AlreadyOwner,
    OwnedByOther,
}

/// One nonblocking, transaction-stable view of the canonical claim.
///
/// `Contended` means a writer still owns the per-PRE commit proof. Callers must retain their last
/// known presentation state and must not inspect the canonical bytes outside this API.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) enum StablePairClaimObservation {
    Contended,
    Stable {
        claim: Option<PairClaim>,
        owned: bool,
    },
}

fn valid_component(value: &str) -> io::Result<&str> {
    if crate::path_identity::is_path_safe_component(value) {
        Ok(value)
    } else {
        Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "pair claim contains an unsafe identity",
        ))
    }
}

fn host_dir(kirin_root: &Path, host_process_id: u32) -> PathBuf {
    kirin_root
        .join(PAIR_CLAIM_SUBDIR)
        .join(host_process_id.to_string())
}

fn claim_path(
    kirin_root: &Path,
    host_process_id: u32,
    pre_instance_id: &str,
) -> io::Result<PathBuf> {
    Ok(host_dir(kirin_root, host_process_id)
        .join(format!("{}.json", valid_component(pre_instance_id)?)))
}

fn lock_path(
    kirin_root: &Path,
    host_process_id: u32,
    pre_instance_id: &str,
) -> io::Result<PathBuf> {
    Ok(host_dir(kirin_root, host_process_id)
        .join(format!(".{}.lock", valid_component(pre_instance_id)?)))
}

fn read_claim_path(path: &Path) -> Option<PairClaim> {
    let bytes = fs::read(path).ok()?;
    let claim: PairClaim = serde_json::from_slice(&bytes).ok()?;
    claim_is_well_formed(&claim).then_some(claim)
}

fn claim_is_well_formed(claim: &PairClaim) -> bool {
    let owner_is_well_formed = match claim.schema.as_str() {
        PAIR_CLAIM_SCHEMA => {
            claim.post_watch_owner_id.is_empty() && valid_component(&claim.pair_owner_id).is_ok()
        }
        LEGACY_PAIR_CLAIM_SCHEMA => {
            valid_component(&claim.post_watch_owner_id).is_ok() && claim.pair_owner_id.is_empty()
        }
        _ => false,
    };
    owner_is_well_formed
        && claim.host_process_id != 0
        && valid_component(&claim.pre_instance_id).is_ok()
        && valid_component(&claim.project_hash).is_ok()
        && valid_component(&claim.post_instance_id).is_ok()
        && claim.pair_claimed_at().is_finite()
        && claim.pair_claimed_at() > 0.0
}

pub fn read_pair_claim(
    kirin_root: &Path,
    host_process_id: u32,
    pre_instance_id: &str,
) -> Option<PairClaim> {
    let path = claim_path(kirin_root, host_process_id, pre_instance_id).ok()?;
    let claim = read_claim_path(&path)?;
    (claim.host_process_id == host_process_id && claim.pre_instance_id == pre_instance_id)
        .then_some(claim)
}

pub(crate) fn try_observe_pair_claim(
    kirin_root: &Path,
    host_process_id: u32,
    pre_instance_id: &str,
) -> io::Result<StablePairClaimObservation> {
    let path = claim_path(kirin_root, host_process_id, pre_instance_id)?;
    let Some(_lock) =
        ClaimLock::try_acquire(&lock_path(kirin_root, host_process_id, pre_instance_id)?)?
    else {
        return Ok(StablePairClaimObservation::Contended);
    };
    let claim = read_claim_path(&path).filter(|claim| {
        claim.host_process_id == host_process_id && claim.pre_instance_id == pre_instance_id
    });
    let owned = claim
        .as_ref()
        .is_some_and(|claim| pair_claim_is_owned(kirin_root, claim));
    Ok(StablePairClaimObservation::Stable { claim, owned })
}

fn post_path_for_claim(kirin_root: &Path, claim: &PairClaim) -> Option<PathBuf> {
    claim_is_well_formed(claim).then(|| {
        kirin_root
            .join(&claim.project_hash)
            .join(&claim.post_instance_id)
            .join("post.json")
    })
}

fn read_fresh_post_snapshot(path: &Path) -> Option<PostTmpJson> {
    // Read and stat one opened inode. POST publication uses atomic rename, so this prevents an old
    // claim body from being combined with a newer writer's path metadata during reselection.
    let mut file = File::open(path).ok()?;
    let modified = file.metadata().ok()?.modified().ok()?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes).ok()?;
    let fresh = SystemTime::now()
        .duration_since(modified)
        .map(|age| age <= Duration::from_secs(DISCOVERY_STALE_SECS))
        .unwrap_or(true);
    if !fresh {
        return None;
    }
    serde_json::from_slice(&bytes).ok()
}

fn post_snapshot_matches_claim(post: &PostTmpJson, claim: &PairClaim) -> bool {
    let exact_owner = match claim.schema.as_str() {
        // v2 ownership is already proven by the engine marker before this health check. Keep the
        // POST snapshot schema unchanged so pair hardening does not widen into IO/TRACE formats.
        PAIR_CLAIM_SCHEMA => true,
        LEGACY_PAIR_CLAIM_SCHEMA => post.watch_owner_id == claim.post_watch_owner_id,
        _ => false,
    };
    exact_owner
        && post.v == 2
        && post.role == "POST"
        && post.instance_id == claim.post_instance_id
        && post.host_process_id == claim.host_process_id
        && post.paired_pre_instance_id == claim.pre_instance_id
        && post.pair_claimed_at.to_bits() == claim.pair_claimed_at_bits
}

pub fn pair_claim_is_live(kirin_root: &Path, claim: &PairClaim) -> bool {
    if !pair_claim_is_owned(kirin_root, claim) {
        return false;
    }
    let Some(post_path) = post_path_for_claim(kirin_root, claim) else {
        return false;
    };
    let Some(post) = read_fresh_post_snapshot(&post_path) else {
        return false;
    };
    post_snapshot_matches_claim(&post, claim)
        && !post.watch_owner_id.is_empty()
        && crate::watch_snapshot_lease::snapshot_owner_is_live(
            post_path
                .parent()
                .expect("post.json has an instance directory"),
            &post.watch_owner_id,
        )
}

/// Whether the exact pair relationship is still owned by its POST engine.
///
/// v2 claims use only the dedicated engine pair marker. A matching `post.json` is never an
/// ownership fallback: after reselection or Drop it may remain fresh briefly and must not revive a
/// retired claim. Legacy v1 claims have no engine marker, so rolling upgrades retain their exact,
/// fresh and live Watch-snapshot proof.
pub fn pair_claim_is_owned(kirin_root: &Path, claim: &PairClaim) -> bool {
    if claim.host_process_id != current_host_process_id() || !claim_is_well_formed(claim) {
        return false;
    }
    let Some(post_path) = post_path_for_claim(kirin_root, claim) else {
        return false;
    };
    let instance_dir = post_path
        .parent()
        .expect("post.json has an instance directory");
    match claim.schema.as_str() {
        PAIR_CLAIM_SCHEMA => {
            return crate::pair_ownership_lease::pair_owner_claim_is_live(
                instance_dir,
                &claim.pair_owner_id,
                &claim.pre_instance_id,
                claim.pair_claimed_at_bits,
            ) || crate::pair_ownership_marker::engine_binding_claim_is_live(
                kirin_root,
                instance_dir,
                &claim.pair_owner_id,
                claim.host_process_id,
                &claim.pre_instance_id,
                claim.pair_claimed_at_bits,
            );
        }
        LEGACY_PAIR_CLAIM_SCHEMA => {}
        _ => return false,
    }

    let Some(post) = read_fresh_post_snapshot(&post_path) else {
        return false;
    };
    post_snapshot_matches_claim(&post, claim)
        && crate::watch_snapshot_lease::snapshot_owner_is_live(
            instance_dir,
            &claim.post_watch_owner_id,
        )
}

fn same_owner(left: &PairClaim, right: &PairClaim) -> bool {
    left.schema == right.schema
        && left.pre_instance_id == right.pre_instance_id
        && left.project_hash == right.project_hash
        && left.post_instance_id == right.post_instance_id
        && left.post_watch_owner_id == right.post_watch_owner_id
        && left.pair_owner_id == right.pair_owner_id
        && left.host_process_id == right.host_process_id
        && left.pair_claimed_at_bits == right.pair_claimed_at_bits
}

pub fn release_pair_claim(kirin_root: &Path, owned: &PairClaim) -> io::Result<bool> {
    if !claim_is_well_formed(owned) {
        return Ok(false);
    }
    let path = claim_path(kirin_root, owned.host_process_id, &owned.pre_instance_id)?;
    let _lock = ClaimLock::acquire(&lock_path(
        kirin_root,
        owned.host_process_id,
        &owned.pre_instance_id,
    )?)?;
    if read_claim_path(&path).is_some_and(|current| same_owner(&current, owned)) {
        match fs::remove_file(path) {
            Ok(()) => return Ok(true),
            Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(false),
            Err(error) => return Err(error),
        }
    }
    Ok(false)
}

/// CAS-remove only the v2 claim for one exact engine binding.
///
/// Drop/reselection callers provide their immutable engine identity rather than reconstructing a
/// schema object. A later binding (including one from the same POST engine with a new timestamp)
/// cannot be removed by an older cleanup callback.
#[allow(clippy::too_many_arguments)]
pub fn release_pair_claim_for_engine_binding(
    kirin_root: &Path,
    pre_instance_id: &str,
    project_hash: &str,
    post_instance_id: &str,
    pair_owner_id: &str,
    host_process_id: u32,
    pair_claimed_at: f64,
) -> io::Result<bool> {
    let path = claim_path(kirin_root, host_process_id, pre_instance_id)?;
    let _lock = ClaimLock::acquire(&lock_path(kirin_root, host_process_id, pre_instance_id)?)?;
    let Some(claim) = read_claim_path(&path) else {
        return Ok(false);
    };
    if claim.schema != PAIR_CLAIM_SCHEMA
        || claim.host_process_id != host_process_id
        || claim.pre_instance_id != pre_instance_id
        || claim.project_hash != project_hash
        || claim.post_instance_id != post_instance_id
        || claim.pair_owner_id != pair_owner_id
        || claim.pair_claimed_at_bits != pair_claimed_at.to_bits()
    {
        return Ok(false);
    }
    match fs::remove_file(path) {
        Ok(()) => Ok(true),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(false),
        Err(error) => Err(error),
    }
}

/// Whether one live claim conflicts with the caller's exact current binding.
///
/// v2 compares the engine-lifetime pair owner in addition to the restored stable identity. Legacy
/// v1 claims have no such field and retain their project/POST/timestamp comparison.
pub fn live_claim_owned_by_other(
    kirin_root: &Path,
    pre_instance_id: &str,
    project_hash: &str,
    post_instance_id: &str,
    expected_pair_owner_id: &str,
    pair_claimed_at: f64,
) -> bool {
    let host_process_id = current_host_process_id();
    let Ok(StablePairClaimObservation::Stable {
        claim: Some(claim),
        owned: true,
    }) = try_observe_pair_claim(kirin_root, host_process_id, pre_instance_id)
    else {
        return false;
    };
    claim.project_hash != project_hash
        || claim.post_instance_id != post_instance_id
        // A recreated POST can restore the same stable IDs and even the same persisted
        // timestamp. For v2, only the engine-lifetime owner UUID distinguishes that foreign
        // runtime from this selector. Legacy v1 has no pair owner UUID; retain its exact
        // project/POST/timestamp compatibility check until old claims age out naturally.
        || (claim.schema == PAIR_CLAIM_SCHEMA
            && claim.pair_owner_id != expected_pair_owner_id)
        || claim.pair_claimed_at_bits != pair_claimed_at.to_bits()
}

pub fn pair_claim_owned_by_other_post(
    kirin_root: &Path,
    pre_instance_id: &str,
    project_hash: &str,
    post_instance_id: &str,
) -> bool {
    let Ok(StablePairClaimObservation::Stable {
        claim: Some(claim),
        owned: true,
    }) = try_observe_pair_claim(kirin_root, current_host_process_id(), pre_instance_id)
    else {
        return false;
    };
    claim.project_hash != project_hash || claim.post_instance_id != post_instance_id
}

#[cfg(test)]
#[path = "pair_claim_index_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "pair_claim_index_transition_tests.rs"]
mod transition_tests;

#[cfg(test)]
#[path = "pair_claim_linearization_tests.rs"]
mod linearization_tests;

#[cfg(test)]
#[path = "pair_claim_index_release_tests.rs"]
mod release_tests;
