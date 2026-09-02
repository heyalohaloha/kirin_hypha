//! Bounded POST IO shutdown and exact lifecycle release.

use std::sync::{Arc, Mutex, RwLock};

use crate::all_keep_signal;
use crate::all_stop_signal;
use crate::engine::SessionSummary;
use crate::record::RecordStateMachine;
use crate::record_signal;
use crate::record_writer::{
    apply_record_take_snapshot, take_session_summary, writer_close_with_summary_and_marks,
    RecordingCtx,
};
use crate::storage::StoragePaths;
use crate::RecordTakeTracker;

#[allow(clippy::too_many_arguments)]
pub(super) fn shutdown_post_io(
    recording: Option<RecordingCtx>,
    record_sm: &RecordStateMachine,
    session_summary: &Arc<Mutex<Option<SessionSummary>>>,
    record_take_tracker: &RecordTakeTracker,
    record_mark_queue: &crate::record_mark::RecordMarkQueue,
    instance_id: &Arc<RwLock<String>>,
    project_hash: &Arc<RwLock<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
) {
    if let Some(mut ctx) = recording {
        // The Measure Thread is also tearing down, so this is an instant check with no wait.
        let sealed = record_sm.seal() > ctx.seal_at_start;
        let summary = take_session_summary(session_summary);
        ctx.writer.add_integrity_reason("lifecycle_shutdown");
        if !sealed {
            ctx.writer.mark_integrity_degraded();
        }
        apply_record_take_snapshot(&mut ctx, Some(record_take_tracker));
        writer_close_with_summary_and_marks(ctx, summary, record_mark_queue);
    }

    let final_iid = super::identity::read_instance_id_arc(instance_id);
    let final_project_hash = super::identity::read_project_hash_arc(project_hash);
    // Do not delete post.json, temp siblings, or the instance directory here.
    // Pluginval and some DAWs can tear down and recreate the same restored instance_id quickly;
    // deleting this path can remove the replacement IO thread's live write.
    // Dropping this thread's unique WatchSnapshotLease makes the old snapshot immediately
    // invisible, while legacy readers/snapshots keep the mtime expiry path.

    match StoragePaths::default_platform() {
        Ok(paths) => {
            super::liveness::release_record_reservation(
                &paths.plugin_data_dir(),
                &final_project_hash,
                &final_iid,
                paired_pre_target,
                "cleanup #4",
            );
            let base = paths.plugin_data_dir();
            let owned_session = record_sm
                .record_session_id()
                .or_else(|| record_sm.last_closed_session_id());
            let release_result = record_signal::read_signal(&base, &final_project_hash, &final_iid)
                .filter(|signal| owned_session.as_deref() == Some(signal.session_id.as_str()))
                .map_or(Ok(false), |expected| {
                    record_signal::mark_released_if_current(
                        &base,
                        &final_project_hash,
                        &final_iid,
                        &expected,
                    )
                });
            match release_result {
                Ok(true) => log::info!("[POST cleanup #4] mark_released ok"),
                Ok(false) => log::info!("[POST cleanup #4] no signal to release"),
                Err(error) => {
                    log::warn!("[POST cleanup #4] mark_released failed: {:?}", error)
                }
            }

            match all_keep_signal::delete_broadcast(
                &paths.plugin_data_dir(),
                &final_project_hash,
                &final_iid,
            ) {
                Ok(()) => log::info!(
                    "[POST shutdown #4 broadcast] delete_broadcast succeeded: instance={}",
                    final_iid
                ),
                Err(error) => log::warn!(
                    "[POST shutdown #4 broadcast] delete_broadcast failed: {:?}",
                    error
                ),
            }

            match all_stop_signal::delete_stop_broadcast(
                &paths.plugin_data_dir(),
                &final_project_hash,
                &final_iid,
            ) {
                Ok(()) => log::info!(
                    "[POST shutdown #4 stop_broadcast] delete_stop_broadcast succeeded: instance={}",
                    final_iid
                ),
                Err(error) => log::warn!(
                    "[POST shutdown #4 stop_broadcast] delete_stop_broadcast failed: {:?}",
                    error
                ),
            }
        }
        Err(error) => log::warn!("[POST cleanup #4] StoragePaths error: {:?}", error),
    }

    log::info!("[IOThread POST] terminated");
}
