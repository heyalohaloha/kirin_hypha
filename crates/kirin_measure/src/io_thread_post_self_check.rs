//! Confirmed conflict handling for one engine-owned POST pair binding.

use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Mutex, RwLock};
use std::time::{Duration, Instant};

use crate::delta::DeltaResult;
use crate::record::RecordStateMachine;
use crate::{load_signal_state, SignalState};

use super::policy::SelfCheckReleaseGate;
use super::ReleasePairBindingIfCurrentFn;

pub(super) struct PairSelfCheckState {
    last_check_at: Instant,
    release_gate: SelfCheckReleaseGate,
}

impl PairSelfCheckState {
    pub(super) fn new(now: Instant) -> Self {
        Self {
            last_check_at: now - Duration::from_secs(2),
            release_gate: SelfCheckReleaseGate::default(),
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub(super) fn service(
        &mut self,
        now: Instant,
        kirin_root: &Path,
        record_sm: &RecordStateMachine,
        is_playing: &AtomicBool,
        signal_state: &AtomicU8,
        paired_pre_instance_id: Option<&str>,
        pair_pre_name: &str,
        pair_binding_generation: u64,
        project_hash: &str,
        post_instance_id: &str,
        pair_owner_id: &str,
        pair_claimed_at: f64,
        release_pair_binding_if_current: &ReleasePairBindingIfCurrentFn,
        shared_pair_claimed_at: &RwLock<f64>,
        pair_release_notice: &RwLock<Option<String>>,
        delta_result: &Mutex<DeltaResult>,
    ) {
        let transport_playing = is_playing.load(Ordering::Relaxed);
        let release_allowed = !record_sm.is_recording()
            && !transport_playing
            && load_signal_state(signal_state) != SignalState::Active;
        let Some(exact_pre) = paired_pre_instance_id.filter(|_| release_allowed) else {
            self.release_gate.reset();
            return;
        };
        if now.duration_since(self.last_check_at) < Duration::from_secs(1) {
            return;
        }
        self.last_check_at = now;

        let conflict = crate::pair_claim_index::live_claim_owned_by_other(
            kirin_root,
            exact_pre,
            project_hash,
            post_instance_id,
            pair_owner_id,
            pair_claimed_at,
        );
        if !conflict {
            self.release_gate.reset();
            return;
        }
        if !self
            .release_gate
            .observe_conflict(exact_pre, pair_claimed_at)
        {
            return;
        }

        // Re-check Record state and the selector generation inside the owner transition. A stale
        // observation must never release a later rename or re-Keep.
        let released = !record_sm.is_recording()
            && release_pair_binding_if_current(pair_pre_name, pair_binding_generation);
        if released {
            log::info!(
                "[POST self_check] reject pair: instance_id={} pair_pre_name={} paired_pre_instance_id={:?} (PRE owned by another POST)",
                post_instance_id,
                pair_pre_name,
                paired_pre_instance_id
            );
            if let Ok(mut claimed_at) = shared_pair_claimed_at.write() {
                *claimed_at = 0.0;
            }
            if let Ok(mut notice) = pair_release_notice.write() {
                *notice = Some("PRE already in use".to_string());
            }
            *crate::sync_recovery::lock_recover(delta_result, "POST self_check delta") =
                DeltaResult::default();
        }
        self.release_gate.reset();
    }
}
