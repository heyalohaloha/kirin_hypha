//! POST snapshot publication, Watch ownership, and Record-display publication.

use std::path::{Path, PathBuf};

use crate::pre_discovery::PostDiscoveryState;
use crate::storage::PlatformPaths;

use crate::io_thread_post::tick::run_tick;

use super::pair::PostPairSnapshot;
use super::runtime::{PostObservationIdentity, PostObservationRuntime};

pub(super) struct PostSnapshotLocation {
    project_dir: PathBuf,
    pub(super) instance_dir: PathBuf,
    post_file: PathBuf,
}

pub(super) struct PostSnapshotPublisher {
    kirin_root: PathBuf,
    discovery: PostDiscoveryState,
    watch_lease: crate::watch_snapshot_lease::WatchSnapshotLease,
}

impl PostSnapshotPublisher {
    pub(super) fn new() -> Self {
        Self {
            kirin_root: PlatformPaths::current_kirin_tmp_root(),
            discovery: PostDiscoveryState::new(),
            watch_lease: crate::watch_snapshot_lease::WatchSnapshotLease::new(),
        }
    }

    pub(super) fn kirin_root(&self) -> &Path {
        &self.kirin_root
    }

    pub(super) fn prepare(&mut self, identity: &PostObservationIdentity) -> PostSnapshotLocation {
        let guarded_project = crate::path_identity::guard_path_component(
            &identity.project_hash,
            "io_thread_post.post_json.project_hash",
        );
        let guarded_instance = crate::path_identity::guard_path_component(
            &identity.instance_id,
            "io_thread_post.post_json.instance_id",
        );
        let project_dir = self.kirin_root.join(&*guarded_project);
        let instance_dir = project_dir.join(&*guarded_instance);
        let post_file = instance_dir.join("post.json");
        if let Err(error) = self.watch_lease.bind(&instance_dir) {
            log::warn!("[IOThread POST] watch lease bind error: {}", error);
        }
        PostSnapshotLocation {
            project_dir,
            instance_dir,
            post_file,
        }
    }

    pub(super) fn publish(
        &mut self,
        location: &PostSnapshotLocation,
        runtime: &PostObservationRuntime,
        identity: &PostObservationIdentity,
        pair: &PostPairSnapshot,
        latched_pre: &std::sync::Arc<std::sync::Mutex<Option<crate::LatchedPre>>>,
    ) -> bool {
        let (recording, stable_generation) = runtime.stable_record_generation();
        let written = match run_tick(
            &location.project_dir,
            &self.kirin_root,
            &mut self.discovery,
            &location.instance_dir,
            &location.post_file,
            &identity.instance_id,
            self.watch_lease.owner_id(),
            &runtime.post_result,
            &runtime.delta_result,
            &runtime.signal_state,
            &pair.name,
            pair.claimed_at,
            &identity.project_hash,
            &identity.daw_session_id,
            recording,
            latched_pre,
        ) {
            Ok(()) => true,
            Err(error) => {
                log::warn!("[IOThread POST] tick error: {}", error);
                false
            }
        };

        if written {
            if let Some(generation) = stable_generation {
                let delta = crate::sync_recovery::lock_recover(
                    &runtime.delta_result,
                    "POST Record display delta",
                )
                .clone();
                runtime.record_sm.publish_record_display_delta(
                    generation,
                    delta,
                    crate::paired_pre_instance_id(latched_pre),
                );
            }
        }

        written
    }
}
