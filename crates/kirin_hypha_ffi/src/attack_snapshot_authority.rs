//! Shared DRUM control authority. Read each lock separately; never join on the GUI thread.
use crate::pair_binding::PairObservationAuthority;
use crate::KirinHyphaEngine;
use kirin_measure::spectrum_exchange::AttackObservationView;
use kirin_measure::PluginDataRole;

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct AttackSnapshotAuthority {
    pub pair: PairObservationAuthority,
    pub role: Option<PluginDataRole>,
    pub post_id: String,
    pub project_hash: String,
    pub owner: String,
    pub claim: u64,
    pub signal: u8,
}

impl AttackSnapshotAuthority {
    pub fn read(engine: &KirinHyphaEngine) -> Option<Self> {
        let pair = engine.pair_binding.try_observation_snapshot()?;
        let role = *engine.write_role.try_lock().ok()?;
        let post_id = engine.identity.try_lock().ok()?.instance_id.clone();
        let project_hash = engine.project_hash_cell.try_read().ok()?.clone();
        let claim = engine.pair_claimed_at.try_read().ok()?.to_bits();
        Some(Self {
            pair,
            role,
            post_id,
            project_hash,
            owner: engine.pair_owner.owner_id().into(),
            claim,
            signal: engine.signal_state_abi(),
        })
    }

    pub fn matches_view(&self, view: &AttackObservationView) -> bool {
        let Some(exact) = self.pair.exact.as_ref() else {
            return false;
        };
        let Some(origin) = view.origin.as_ref() else {
            return false;
        };
        self.pair.selection_intent
            && view.authority_revision == self.pair.generation
            && origin.generation == exact.generation
            && origin.project_hash == exact.project_hash
            && origin.project_hash == self.project_hash
            && origin.pre_instance_id == exact.pre_instance_id
            && origin.post_instance_id == self.post_id
            && origin.owner_id == self.owner
            && origin.claimed_at_bits == self.claim
    }
}
