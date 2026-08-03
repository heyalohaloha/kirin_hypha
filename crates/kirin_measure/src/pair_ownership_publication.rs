//! Claim/proof retirement for atomic pair commits.

use std::path::PathBuf;

use super::{LeaseMarker, LeaseState, PairOwnershipLease};

pub(super) enum BindingPublication {
    None,
    PairClaim {
        kirin_root: PathBuf,
        prepared: Box<crate::pair_claim_index::PreparedPairClaim>,
    },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct OwnedPairClaim {
    pub(super) kirin_root: PathBuf,
    pub(super) claim: crate::pair_claim_index::PairClaim,
}

#[derive(Debug)]
pub(super) struct RetiredOwnedBinding {
    pub(super) claim: OwnedPairClaim,
    _marker: Option<LeaseMarker>,
    _engine_binding_proof: Option<LeaseMarker>,
}

impl BindingPublication {
    pub(super) fn proposed_claim(&self) -> Option<OwnedPairClaim> {
        match self {
            Self::None => None,
            Self::PairClaim {
                kirin_root,
                prepared,
            } => Some(OwnedPairClaim {
                kirin_root: kirin_root.clone(),
                claim: prepared.proposed().clone(),
            }),
        }
    }

    pub(super) fn commit(self) {
        match self {
            Self::None => {}
            Self::PairClaim { prepared, .. } => {
                (*prepared).commit();
            }
        }
    }
}

pub(super) fn retire_previous_binding(
    lease: &PairOwnershipLease,
    old_claim: Option<OwnedPairClaim>,
    next_claim: Option<&OwnedPairClaim>,
    old_marker: Option<LeaseMarker>,
    old_engine_proof: Option<LeaseMarker>,
) {
    let Some(old_claim) = old_claim else {
        return;
    };
    if next_claim == Some(&old_claim) {
        return;
    }
    if let Err(error) =
        crate::pair_claim_index::release_pair_claim(&old_claim.kirin_root, &old_claim.claim)
    {
        log::warn!("[pair claim] exact retirement deferred: {error}");
        lease
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .retired_bindings
            .push(RetiredOwnedBinding {
                claim: old_claim,
                _marker: old_marker,
                _engine_binding_proof: old_engine_proof,
            });
    }
}

pub(super) fn release_claims_before_proofs(state: &mut LeaseState) {
    if let Some(active) = state.active_claim.take() {
        let _ = crate::pair_claim_index::release_pair_claim(&active.kirin_root, &active.claim);
    }
    for retired in &state.retired_bindings {
        let _ = crate::pair_claim_index::release_pair_claim(
            &retired.claim.kirin_root,
            &retired.claim.claim,
        );
    }
    // Dropping a retired proof is safe only after every exact claim CAS above has completed.
    state.retired_bindings.clear();
    drop(state.engine_binding_proof.take());
    drop(state.marker.take());
}
