//! Shared POST observation inputs with one identity snapshot per ordered cycle.

use std::sync::atomic::{AtomicBool, AtomicU8};
use std::sync::{Arc, Mutex, RwLock};

use crate::delta::DeltaResult;
use crate::record::RecordStateMachine;
use crate::storage::StoragePaths;

use crate::io_thread_post::identity::{
    read_daw_session_id_arc, read_instance_id_arc, read_project_hash_arc,
};

pub(in crate::io_thread_post) struct PostObservationRuntime {
    pub(in crate::io_thread_post) instance_id: Arc<RwLock<String>>,
    pub(in crate::io_thread_post) project_hash: Arc<RwLock<String>>,
    pub(in crate::io_thread_post) daw_session_id: Arc<RwLock<String>>,
    pub(in crate::io_thread_post) record_sm: Arc<RecordStateMachine>,
    pub(in crate::io_thread_post) post_result: Arc<Mutex<crate::MeasureResult>>,
    pub(in crate::io_thread_post) delta_result: Arc<Mutex<DeltaResult>>,
    pub(in crate::io_thread_post) signal_state: Arc<AtomicU8>,
    pub(in crate::io_thread_post) is_playing: Arc<AtomicBool>,
    pub(in crate::io_thread_post) reference_audition_active: Arc<AtomicBool>,
}

pub(super) struct PostObservationIdentity {
    pub(super) instance_id: String,
    pub(super) project_hash: String,
    pub(super) daw_session_id: String,
}

impl PostObservationRuntime {
    pub(super) fn prepare_startup(&self, kirin_root: &std::path::Path) {
        crate::path_identity::normalize_observation_cell(
            &self.instance_id,
            "io_thread_post.instance_id",
        );
        crate::path_identity::normalize_observation_cell(
            &self.project_hash,
            "io_thread_post.project_hash",
        );
        crate::path_identity::normalize_restore_cell(
            &self.daw_session_id,
            "io_thread_post.daw_session_id",
            None,
        );

        let identity = self.identity_snapshot();
        let plugin_data_dir = StoragePaths::default_platform()
            .map(|paths| paths.plugin_data_dir().display().to_string())
            .unwrap_or_else(|_| "<unresolved>".to_string());
        log::info!(
            "[IOThread POST] started: instance_id={} project_hash={} plugin_data_dir={} (lazy-read instance_id/project_hash/daw_session_id, initial project_dir_hint={}, kirin_root={})",
            identity.instance_id,
            identity.project_hash,
            plugin_data_dir,
            kirin_root.join(&identity.project_hash).display(),
            kirin_root.display()
        );
    }

    pub(super) fn identity_snapshot(&self) -> PostObservationIdentity {
        PostObservationIdentity {
            instance_id: read_instance_id_arc(&self.instance_id),
            project_hash: read_project_hash_arc(&self.project_hash),
            daw_session_id: read_daw_session_id_arc(&self.daw_session_id),
        }
    }

    pub(super) fn stable_record_generation(&self) -> (bool, Option<u64>) {
        let before = self.record_sm.generation();
        let recording = self.record_sm.is_recording();
        let after = self.record_sm.generation();
        (
            recording,
            stable_record_generation(before, recording, after),
        )
    }
}

pub(super) fn stable_record_generation(before: u64, recording: bool, after: u64) -> Option<u64> {
    (recording && before == after).then_some(after)
}
