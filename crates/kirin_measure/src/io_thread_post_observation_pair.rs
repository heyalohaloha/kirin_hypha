//! Exact pair lifecycle owned by the ordered POST observation cycle.

use std::path::Path;
use std::sync::{Arc, Mutex, RwLock};
use std::time::Instant;

use crate::pairing_scope::LatchedPre;
use crate::{PairClaim, PairOwnershipLease};

use crate::io_thread_post::identity::snapshot_pair_pre_name;
use crate::io_thread_post::pair_claim::service_pair_claim;
use crate::io_thread_post::reservation::ReservationLeaseRefresh;
use crate::io_thread_post::self_check::PairSelfCheckState;
use crate::io_thread_post::{PairBindingGenerationFn, ReleasePairBindingIfCurrentFn};

use super::runtime::{PostObservationIdentity, PostObservationRuntime};

pub(in crate::io_thread_post) struct PostPairObservationDeps {
    pub(in crate::io_thread_post) paired_pre_target: Arc<Mutex<Option<String>>>,
    pub(in crate::io_thread_post) pair_pre_name: Arc<RwLock<String>>,
    pub(in crate::io_thread_post) pair_binding_generation: PairBindingGenerationFn,
    pub(in crate::io_thread_post) release_pair_binding_if_current: ReleasePairBindingIfCurrentFn,
    pub(in crate::io_thread_post) pair_claimed_at: Arc<RwLock<f64>>,
    pub(in crate::io_thread_post) pair_release_notice: Arc<RwLock<Option<String>>>,
    pub(in crate::io_thread_post) pair_owner: Arc<PairOwnershipLease>,
    pub(in crate::io_thread_post) latched_pre: Arc<Mutex<Option<LatchedPre>>>,
}

pub(super) struct PostPairSnapshot {
    pub(super) name: String,
    pub(super) claimed_at: f64,
}

pub(super) struct PostPairObservation {
    deps: PostPairObservationDeps,
    self_check: PairSelfCheckState,
    reservation: ReservationLeaseRefresh,
    owned_claim: Option<PairClaim>,
    next_claim_publish: Instant,
}

impl PostPairObservation {
    pub(super) fn new(deps: PostPairObservationDeps, now: Instant) -> Self {
        Self {
            deps,
            self_check: PairSelfCheckState::new(now),
            reservation: ReservationLeaseRefresh::new(now),
            owned_claim: None,
            next_claim_publish: now,
        }
    }

    pub(super) fn refresh_reservation(
        &mut self,
        cycle_now: Instant,
        runtime: &PostObservationRuntime,
        identity: &PostObservationIdentity,
    ) {
        self.reservation.service(
            cycle_now,
            runtime.record_sm.is_recording(),
            &self.deps.paired_pre_target,
            &identity.project_hash,
            &identity.instance_id,
        );
    }

    pub(super) fn observe_binding(
        &mut self,
        cycle_now: Instant,
        kirin_root: &Path,
        runtime: &PostObservationRuntime,
        identity: &PostObservationIdentity,
    ) -> PostPairSnapshot {
        let pair_name = snapshot_pair_pre_name(&self.deps.pair_pre_name);
        let paired_pre = crate::paired_pre_instance_id(&self.deps.latched_pre);
        let generation = (self.deps.pair_binding_generation)();
        let claimed_at = self.claimed_at();
        self.self_check.service(
            cycle_now,
            kirin_root,
            &runtime.record_sm,
            &runtime.is_playing,
            &runtime.signal_state,
            paired_pre.as_deref(),
            &pair_name,
            generation,
            &identity.project_hash,
            &identity.instance_id,
            self.deps.pair_owner.owner_id(),
            claimed_at,
            &self.deps.release_pair_binding_if_current,
            &self.deps.pair_claimed_at,
            &self.deps.pair_release_notice,
            &runtime.delta_result,
        );

        // Self-check may release the binding. Publication must use the state after that transition.
        PostPairSnapshot {
            name: snapshot_pair_pre_name(&self.deps.pair_pre_name),
            claimed_at: self.claimed_at(),
        }
    }

    pub(super) fn publish_claim(
        &mut self,
        kirin_root: &Path,
        instance_dir: &Path,
        snapshot_written: bool,
        identity: &PostObservationIdentity,
    ) {
        let current_pre = crate::paired_pre_instance_id(&self.deps.latched_pre);
        let current_claimed_at = self.claimed_at();
        service_pair_claim(
            kirin_root,
            instance_dir,
            snapshot_written,
            current_pre.as_deref(),
            &identity.project_hash,
            &identity.instance_id,
            current_claimed_at,
            &self.deps.pair_owner,
            &mut self.owned_claim,
            &mut self.next_claim_publish,
            |expected_pre, expected_claimed_at| {
                crate::paired_pre_instance_id(&self.deps.latched_pre).as_deref() == expected_pre
                    && self
                        .deps
                        .pair_claimed_at
                        .read()
                        .map(|value| value.to_bits() == expected_claimed_at.to_bits())
                        .unwrap_or(false)
            },
        );
    }

    pub(super) fn latched_pre(&self) -> &Arc<Mutex<Option<LatchedPre>>> {
        &self.deps.latched_pre
    }

    fn claimed_at(&self) -> f64 {
        self.deps
            .pair_claimed_at
            .read()
            .map(|value| *value)
            .unwrap_or(0.0)
    }
}
