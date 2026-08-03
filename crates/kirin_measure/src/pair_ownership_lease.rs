//! Engine-lifetime ownership for one POST's exact PRE binding.
//!
//! Pair ownership belongs to the POST engine, independently of restartable Watch/IO workers.

use std::io;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use uuid::Uuid;

pub(crate) use crate::pair_ownership_marker::pair_owner_claim_is_live;
use crate::pair_ownership_marker::{
    acquire_marker, desired_engine_binding_proof_path, desired_marker_path, LeaseMarker,
    MarkerPathOwnership,
};

#[path = "pair_ownership_lease_control.rs"]
mod pair_ownership_lease_control;
pub use pair_ownership_lease_control::PairOwnershipBinding;
#[path = "pair_ownership_publication.rs"]
mod pair_ownership_publication;
use pair_ownership_publication::{
    release_claims_before_proofs, retire_previous_binding, BindingPublication, OwnedPairClaim,
    RetiredOwnedBinding,
};

/// Resolve the normalized POST instance shelf used by the IO worker.
pub fn pair_owner_instance_dir(
    kirin_root: &Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Option<PathBuf> {
    if !crate::path_identity::is_path_safe_component(project_hash)
        || !crate::path_identity::is_path_safe_component(post_instance_id)
    {
        return None;
    }
    Some(kirin_root.join(project_hash).join(post_instance_id))
}

#[derive(Debug)]
pub struct PairOwnershipLease {
    owner_id: String,
    /// Serializes the complete marker + canonical claim transaction.
    control: Mutex<()>,
    state: Mutex<LeaseState>,
}

#[derive(Debug)]
struct LeaseState {
    marker: Option<LeaseMarker>,
    engine_binding_proof: Option<LeaseMarker>,
    active_claim: Option<OwnedPairClaim>,
    retired_bindings: Vec<RetiredOwnedBinding>,
}

impl PairOwnershipLease {
    pub fn new() -> Self {
        Self {
            owner_id: Uuid::new_v4().to_string(),
            control: Mutex::new(()),
            state: Mutex::new(LeaseState {
                marker: None,
                engine_binding_proof: None,
                active_claim: None,
                retired_bindings: Vec::new(),
            }),
        }
    }

    pub fn owner_id(&self) -> &str {
        &self.owner_id
    }

    /// Idempotent direct marker bind used by setup and focused tests.
    pub fn bind(
        &self,
        instance_dir: &Path,
        pre_instance_id: Option<&str>,
        pair_claimed_at: f64,
    ) -> io::Result<()> {
        self.commit_binding(Some(instance_dir), pre_instance_id, pair_claimed_at, || {
            Some(())
        })
        .map(|_| ())
    }

    /// Commit one marker-only pair transition under the same control lock used by claimed binds.
    pub fn commit_binding<R>(
        &self,
        instance_dir: Option<&Path>,
        pre_instance_id: Option<&str>,
        pair_claimed_at: f64,
        commit: impl FnOnce() -> Option<R>,
    ) -> io::Result<Option<R>> {
        self.commit_binding_with_publication(
            instance_dir,
            pre_instance_id,
            pair_claimed_at,
            None,
            || true,
            || Ok(Some(BindingPublication::None)),
            commit,
        )
    }

    /// Commit one exact marker and canonical per-PRE claim as a single serialized transaction.
    ///
    /// New proofs and claim bytes are prepared while the previous proofs remain live. A rejected
    /// proposal rolls the claim bytes back before releasing the per-PRE lock. A successful move
    /// commits the new claim before retiring the previous proof, so readers cannot observe an
    /// unowned gap.
    #[allow(clippy::too_many_arguments)]
    pub fn commit_claimed_binding_if<R>(
        &self,
        kirin_root: &Path,
        instance_dir: Option<&Path>,
        pre_instance_id: Option<&str>,
        project_hash: &str,
        post_instance_id: &str,
        pair_claimed_at: f64,
        validate: impl FnOnce() -> bool,
        commit: impl FnOnce() -> Option<R>,
    ) -> io::Result<Option<R>> {
        if pre_instance_id.is_some() {
            let expected = pair_owner_instance_dir(kirin_root, project_hash, post_instance_id)
                .ok_or_else(|| {
                    io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "pair claim identity cannot resolve an engine instance directory",
                    )
                })?;
            if instance_dir != Some(expected.as_path()) {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "pair claim identity does not match its engine instance directory",
                ));
            }
        }

        let desired_engine_proof = desired_engine_binding_proof_path(
            kirin_root,
            instance_dir,
            &self.owner_id,
            crate::post_candidates::current_host_process_id(),
            pre_instance_id,
            pair_claimed_at,
        )?;
        self.commit_binding_with_publication(
            instance_dir,
            pre_instance_id,
            pair_claimed_at,
            desired_engine_proof,
            validate,
            || {
                let Some(pre_instance_id) = pre_instance_id else {
                    return Ok(Some(BindingPublication::None));
                };
                let prepared = crate::pair_claim_index::prepare_pair_claim(
                    kirin_root,
                    pre_instance_id,
                    project_hash,
                    post_instance_id,
                    &self.owner_id,
                    crate::post_candidates::current_host_process_id(),
                    pair_claimed_at,
                )?;
                if prepared.outcome()
                    == crate::pair_claim_index::PublishPairClaimOutcome::OwnedByOther
                {
                    return Ok(None);
                }
                Ok(Some(BindingPublication::PairClaim {
                    kirin_root: kirin_root.to_path_buf(),
                    prepared: Box::new(prepared),
                }))
            },
            commit,
        )
    }

    #[allow(clippy::too_many_arguments)]
    fn commit_binding_with_publication<R>(
        &self,
        instance_dir: Option<&Path>,
        pre_instance_id: Option<&str>,
        pair_claimed_at: f64,
        desired_engine_proof: Option<PathBuf>,
        validate: impl FnOnce() -> bool,
        prepare_publication: impl FnOnce() -> io::Result<Option<BindingPublication>>,
        commit: impl FnOnce() -> Option<R>,
    ) -> io::Result<Option<R>> {
        let desired = desired_marker_path(
            instance_dir,
            &self.owner_id,
            pre_instance_id,
            pair_claimed_at,
        )?;
        let _control = self
            .control
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if !validate() {
            return Ok(None);
        }

        let (needs_marker, needs_engine_proof) = {
            let state = self
                .state
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            (
                marker_needs_replacement(state.marker.as_ref(), desired.as_ref())?,
                marker_needs_replacement(
                    state.engine_binding_proof.as_ref(),
                    desired_engine_proof.as_ref(),
                )?,
            )
        };

        let mut prepared_marker = if needs_marker {
            desired.as_deref().map(acquire_marker).transpose()?
        } else {
            None
        };
        let mut prepared_engine_proof = if needs_engine_proof {
            desired_engine_proof
                .as_deref()
                .map(acquire_marker)
                .transpose()?
        } else {
            None
        };
        let Some(publication) = prepare_publication()? else {
            return Ok(None);
        };
        let Some(result) = commit() else {
            drop(publication);
            return Ok(None);
        };

        let next_claim = publication.proposed_claim();
        let (old_marker, old_engine_proof, old_claim) = {
            let mut state = self
                .state
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            let old_marker = needs_marker
                .then(|| std::mem::replace(&mut state.marker, prepared_marker.take()))
                .flatten();
            let old_engine_proof = needs_engine_proof
                .then(|| {
                    std::mem::replace(
                        &mut state.engine_binding_proof,
                        prepared_engine_proof.take(),
                    )
                })
                .flatten();
            let old_claim = std::mem::replace(&mut state.active_claim, next_claim.clone());
            (old_marker, old_engine_proof, old_claim)
        };

        publication.commit();
        retire_previous_binding(
            self,
            old_claim,
            next_claim.as_ref(),
            old_marker,
            old_engine_proof,
        );
        Ok(Some(result))
    }
}

fn marker_needs_replacement(
    current: Option<&LeaseMarker>,
    desired: Option<&PathBuf>,
) -> io::Result<bool> {
    match current {
        Some(marker) if Some(&marker.path) == desired => match marker.path_ownership() {
            MarkerPathOwnership::Owned => Ok(false),
            MarkerPathOwnership::ReplacedOrMissing => Ok(true),
            MarkerPathOwnership::Indeterminate(error) => Err(error),
        },
        None if desired.is_none() => Ok(false),
        Some(_) | None => Ok(true),
    }
}

impl Default for PairOwnershipLease {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for PairOwnershipLease {
    fn drop(&mut self) {
        let state = self
            .state
            .get_mut()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        release_claims_before_proofs(state);
    }
}

#[cfg(test)]
#[path = "pair_ownership_lease_tests.rs"]
mod pair_ownership_lease_tests;

#[cfg(test)]
#[path = "pair_ownership_lease_control_tests.rs"]
mod pair_ownership_lease_control_tests;
