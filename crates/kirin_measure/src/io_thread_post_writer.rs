//! POST adapter for the shared Record writer lifecycle.

use std::sync::atomic::AtomicU64;
use std::sync::{Arc, Mutex};

use crate::engine::SessionSummary;
use crate::plugin_data::Role as PluginDataRole;
use crate::record::RecordStateMachine;
use crate::record_writer::{
    run_record_tick_with_pair_names_require_session_and_marks, RecordingCtx,
};
use crate::storage::StoragePaths;
use crate::{MeasureResult, RecordTakeTracker, RecordTraceQueue};

fn paired_pre_target_snapshot(target: &Arc<Mutex<Option<String>>>) -> Option<String> {
    target.lock().ok().and_then(|guard| guard.clone())
}

#[allow(clippy::too_many_arguments)]
pub(super) fn service_post_record_writer(
    record_sm: &Arc<RecordStateMachine>,
    sample_rate: u32,
    project_hash: &str,
    post_instance_id: &str,
    pair_pre_name: &str,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    post_result: &Arc<Mutex<MeasureResult>>,
    recording: &mut Option<RecordingCtx>,
    session_summary: &Arc<Mutex<Option<SessionSummary>>>,
    overflow: &Arc<AtomicU64>,
    oversized_drop: &Arc<AtomicU64>,
    record_trace_queue: &RecordTraceQueue,
    record_take_tracker: &Arc<RecordTakeTracker>,
    record_mark_queue: &crate::record_mark::RecordMarkQueue,
) {
    let resolver = || match StoragePaths::default_platform() {
        Ok(paths) => crate::record_writer::resolve_started_at_ms(
            &paths.plugin_data_dir(),
            project_hash,
            post_instance_id,
        ),
        Err(_) => crate::record_writer::now_epoch_ms(),
    };
    let paired_pre = Arc::clone(paired_pre_target);
    let paired_pre_resolver = move || paired_pre_target_snapshot(&paired_pre);
    let paired_post_resolver = || None::<String>;
    let pair_name = pair_pre_name.to_string();
    let pair_pre_name = pair_pre_name.to_string();
    let pair_name_resolver = move || Some(pair_name);
    let pair_pre_name_resolver = move || Some(pair_pre_name);
    let record_session_id = record_sm.record_session_id();

    if let Err(error) = run_record_tick_with_pair_names_require_session_and_marks(
        record_sm,
        PluginDataRole::Post,
        sample_rate,
        project_hash,
        post_instance_id,
        resolver,
        paired_pre_resolver,
        paired_post_resolver,
        pair_name_resolver,
        pair_pre_name_resolver,
        move || record_session_id,
        post_result,
        recording,
        Some(session_summary),
        overflow,
        oversized_drop,
        Some(record_trace_queue),
        Some(record_take_tracker),
        record_mark_queue,
    ) {
        log::warn!("[writer] tick error: {}", error);
    }
}

#[cfg(test)]
#[path = "io_thread_post_writer_tests.rs"]
mod tests;
