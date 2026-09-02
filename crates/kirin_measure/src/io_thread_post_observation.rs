//! One ordered POST observation cycle and its engine-lifetime state.

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU8};
use std::sync::{Arc, Mutex, RwLock};
use std::time::Instant;

use crate::delta::DeltaResult;
use crate::pairing_scope::LatchedPre;
use crate::pre_discovery::PostDiscoveryState;
use crate::record::RecordStateMachine;
use crate::storage::{PlatformPaths, StoragePaths};
use crate::{MeterDeltaHistoryExchange, PairOwnershipLease, SpectrumCoordinator};

use super::analysis::service_post_analysis_endpoints;
use super::identity::{
    read_daw_session_id_arc, read_instance_id_arc, read_project_hash_arc, snapshot_pair_pre_name,
};
use super::pair_claim::service_pair_claim;
use super::reservation::ReservationLeaseRefresh;
use super::self_check::PairSelfCheckState;
use super::tick::run_tick;
use super::{PairBindingGenerationFn, ReleasePairBindingIfCurrentFn};

pub(super) struct PostObservationTick {
    pub(super) project_hash: String,
    pub(super) instance_id: String,
    pub(super) pair_pre_name: String,
}

pub(super) struct PostObservation {
    kirin_root: PathBuf,
    instance_id: Arc<RwLock<String>>,
    project_hash: Arc<RwLock<String>>,
    daw_session_id: Arc<RwLock<String>>,
    record_sm: Arc<RecordStateMachine>,
    post_result: Arc<Mutex<crate::MeasureResult>>,
    delta_result: Arc<Mutex<DeltaResult>>,
    signal_state: Arc<AtomicU8>,
    is_playing: Arc<AtomicBool>,
    paired_pre_target: Arc<Mutex<Option<String>>>,
    pair_pre_name: Arc<RwLock<String>>,
    pair_binding_generation: PairBindingGenerationFn,
    release_pair_binding_if_current: ReleasePairBindingIfCurrentFn,
    pair_claimed_at: Arc<RwLock<f64>>,
    pair_release_notice: Arc<RwLock<Option<String>>>,
    pair_owner: Arc<PairOwnershipLease>,
    latched_pre: Arc<Mutex<Option<LatchedPre>>>,
    spectrum: Option<Arc<SpectrumCoordinator>>,
    meter_history: Option<Arc<MeterDeltaHistoryExchange>>,
    pair_self_check: PairSelfCheckState,
    reservation_lease_refresh: ReservationLeaseRefresh,
    discovery: PostDiscoveryState,
    watch_lease: crate::watch_snapshot_lease::WatchSnapshotLease,
    owned_pair_claim: Option<crate::pair_claim_index::PairClaim>,
    next_pair_claim_publish: Instant,
}

impl PostObservation {
    #[allow(clippy::too_many_arguments)]
    pub(super) fn new(
        instance_id: Arc<RwLock<String>>,
        project_hash: Arc<RwLock<String>>,
        daw_session_id: Arc<RwLock<String>>,
        record_sm: Arc<RecordStateMachine>,
        post_result: Arc<Mutex<crate::MeasureResult>>,
        delta_result: Arc<Mutex<DeltaResult>>,
        signal_state: Arc<AtomicU8>,
        is_playing: Arc<AtomicBool>,
        paired_pre_target: Arc<Mutex<Option<String>>>,
        pair_pre_name: Arc<RwLock<String>>,
        pair_binding_generation: PairBindingGenerationFn,
        release_pair_binding_if_current: ReleasePairBindingIfCurrentFn,
        pair_claimed_at: Arc<RwLock<f64>>,
        pair_release_notice: Arc<RwLock<Option<String>>>,
        pair_owner: Arc<PairOwnershipLease>,
        latched_pre: Arc<Mutex<Option<LatchedPre>>>,
        spectrum: Option<Arc<SpectrumCoordinator>>,
        meter_history: Option<Arc<MeterDeltaHistoryExchange>>,
        now: Instant,
    ) -> Self {
        crate::path_identity::normalize_observation_cell(
            &instance_id,
            "io_thread_post.instance_id",
        );
        crate::path_identity::normalize_observation_cell(
            &project_hash,
            "io_thread_post.project_hash",
        );
        crate::path_identity::normalize_restore_cell(
            &daw_session_id,
            "io_thread_post.daw_session_id",
            None,
        );

        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let initial_project_hash = read_project_hash_arc(&project_hash);
        let initial_instance_id = read_instance_id_arc(&instance_id);
        let plugin_data_dir = StoragePaths::default_platform()
            .map(|paths| paths.plugin_data_dir().display().to_string())
            .unwrap_or_else(|_| "<unresolved>".to_string());
        log::info!(
            "[IOThread POST] started: instance_id={} project_hash={} plugin_data_dir={} (lazy-read instance_id/project_hash/daw_session_id, initial project_dir_hint={}, kirin_root={})",
            initial_instance_id,
            initial_project_hash,
            plugin_data_dir,
            kirin_root.join(&initial_project_hash).display(),
            kirin_root.display()
        );

        Self {
            kirin_root,
            instance_id,
            project_hash,
            daw_session_id,
            record_sm,
            post_result,
            delta_result,
            signal_state,
            is_playing,
            paired_pre_target,
            pair_pre_name,
            pair_binding_generation,
            release_pair_binding_if_current,
            pair_claimed_at,
            pair_release_notice,
            pair_owner,
            latched_pre,
            spectrum,
            meter_history,
            pair_self_check: PairSelfCheckState::new(now),
            reservation_lease_refresh: ReservationLeaseRefresh::new(now),
            discovery: PostDiscoveryState::new(),
            watch_lease: crate::watch_snapshot_lease::WatchSnapshotLease::new(),
            owned_pair_claim: None,
            next_pair_claim_publish: now,
        }
    }

    pub(super) fn service(&mut self) -> PostObservationTick {
        let instance_id = read_instance_id_arc(&self.instance_id);
        let project_hash = read_project_hash_arc(&self.project_hash);
        let daw_session_id = read_daw_session_id_arc(&self.daw_session_id);

        self.reservation_lease_refresh.service(
            Instant::now(),
            self.record_sm.is_recording(),
            &self.paired_pre_target,
            &project_hash,
            &instance_id,
        );

        let guarded_project = crate::path_identity::guard_path_component(
            &project_hash,
            "io_thread_post.post_json.project_hash",
        );
        let guarded_instance = crate::path_identity::guard_path_component(
            &instance_id,
            "io_thread_post.post_json.instance_id",
        );
        let project_dir_hint = self.kirin_root.join(&*guarded_project);
        let instance_dir = project_dir_hint.join(&*guarded_instance);
        let post_file = instance_dir.join("post.json");
        if let Err(error) = self.watch_lease.bind(&instance_dir) {
            log::warn!("[IOThread POST] watch lease bind error: {}", error);
        }

        let pair_pre_name = snapshot_pair_pre_name(&self.pair_pre_name);
        let paired_pre_instance_id = crate::paired_pre_instance_id(&self.latched_pre);
        let pair_binding_generation = (self.pair_binding_generation)();
        let pair_claimed_at = self
            .pair_claimed_at
            .read()
            .map(|value| *value)
            .unwrap_or(0.0);
        self.pair_self_check.service(
            Instant::now(),
            &self.kirin_root,
            &self.record_sm,
            &self.is_playing,
            &self.signal_state,
            paired_pre_instance_id.as_deref(),
            &pair_pre_name,
            pair_binding_generation,
            &project_hash,
            &instance_id,
            self.pair_owner.owner_id(),
            pair_claimed_at,
            &self.release_pair_binding_if_current,
            &self.pair_claimed_at,
            &self.pair_release_notice,
            &self.delta_result,
        );

        // Self-check may release the binding, so the published snapshot is always read afterwards.
        let pair_pre_name = snapshot_pair_pre_name(&self.pair_pre_name);
        let pair_claimed_at = self
            .pair_claimed_at
            .read()
            .map(|value| *value)
            .unwrap_or(0.0);
        let generation_before = self.record_sm.generation();
        let recording = self.record_sm.is_recording();
        let generation_after = self.record_sm.generation();
        let stable_generation =
            stable_record_generation(generation_before, recording, generation_after);

        let post_snapshot_written = match run_tick(
            &project_dir_hint,
            &self.kirin_root,
            &mut self.discovery,
            &instance_dir,
            &post_file,
            &instance_id,
            self.watch_lease.owner_id(),
            &self.post_result,
            &self.delta_result,
            &self.signal_state,
            &pair_pre_name,
            pair_claimed_at,
            &project_hash,
            &daw_session_id,
            recording,
            &self.latched_pre,
        ) {
            Ok(()) => true,
            Err(error) => {
                log::warn!("[IOThread POST] tick error: {}", error);
                false
            }
        };

        if post_snapshot_written {
            if let Some(generation) = stable_generation {
                let delta = crate::sync_recovery::lock_recover(
                    &self.delta_result,
                    "POST Record display delta",
                )
                .clone();
                self.record_sm.publish_record_display_delta(
                    generation,
                    delta,
                    crate::paired_pre_instance_id(&self.latched_pre),
                );
            }
        }

        service_post_analysis_endpoints(
            self.spectrum.as_ref(),
            self.meter_history.as_ref(),
            &self.latched_pre,
            &instance_id,
            &pair_pre_name,
        );

        let current_pre = crate::paired_pre_instance_id(&self.latched_pre);
        let current_claimed_at = self
            .pair_claimed_at
            .read()
            .map(|value| *value)
            .unwrap_or(0.0);
        service_pair_claim(
            &self.kirin_root,
            &instance_dir,
            post_snapshot_written,
            current_pre.as_deref(),
            &project_hash,
            &instance_id,
            current_claimed_at,
            &self.pair_owner,
            &mut self.owned_pair_claim,
            &mut self.next_pair_claim_publish,
            |expected_pre, expected_claimed_at| {
                crate::paired_pre_instance_id(&self.latched_pre).as_deref() == expected_pre
                    && self
                        .pair_claimed_at
                        .read()
                        .map(|value| value.to_bits() == expected_claimed_at.to_bits())
                        .unwrap_or(false)
            },
        );

        PostObservationTick {
            project_hash,
            instance_id,
            pair_pre_name,
        }
    }
}

fn stable_record_generation(before: u64, recording: bool, after: u64) -> Option<u64> {
    (recording && before == after).then_some(after)
}

#[cfg(test)]
mod tests {
    use super::stable_record_generation;

    #[test]
    fn record_display_requires_one_unchanged_record_generation() {
        assert_eq!(stable_record_generation(7, true, 7), Some(7));
        assert_eq!(stable_record_generation(7, true, 8), None);
        assert_eq!(stable_record_generation(7, false, 7), None);
    }
}
