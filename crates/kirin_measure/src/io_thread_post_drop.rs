//! Exact-session Drop commit acceptance for an open POST Record.

use std::path::Path;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use crate::cleanup::exit_record_preserve_pair;
use crate::record::RecordStateMachine;
use crate::storage::StoragePaths;
use crate::{record_signal, RecordTakeTracker};

use super::liveness::release_record_reservation;
use super::policy::drop_commit_matches_observed_capture;

pub(super) fn service_open_drop_commit(
    record_sm: &Arc<RecordStateMachine>,
    record_take_tracker: &Arc<RecordTakeTracker>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    project_hash: &str,
    post_instance_id: &str,
) -> bool {
    service_open_drop_commit_with_base(
        record_sm,
        record_take_tracker,
        paired_pre_target,
        project_hash,
        post_instance_id,
        || {
            StoragePaths::default_platform()
                .ok()
                .map(|paths| paths.plugin_data_dir())
        },
    )
}

fn service_open_drop_commit_with_base<F>(
    record_sm: &Arc<RecordStateMachine>,
    record_take_tracker: &Arc<RecordTakeTracker>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    project_hash: &str,
    post_instance_id: &str,
    resolve_base: F,
) -> bool
where
    F: FnOnce() -> Option<PathBuf>,
{
    if !record_sm.is_recording() {
        return false;
    }
    let Some(base) = resolve_base() else {
        return false;
    };
    service_open_drop_commit_at(
        &base,
        record_sm,
        record_take_tracker,
        paired_pre_target,
        project_hash,
        post_instance_id,
    )
}

fn service_open_drop_commit_at(
    base: &Path,
    record_sm: &Arc<RecordStateMachine>,
    record_take_tracker: &Arc<RecordTakeTracker>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    project_hash: &str,
    post_instance_id: &str,
) -> bool {
    if !record_sm.is_recording() {
        return false;
    }
    let Some(session_id) = record_sm.record_session_id() else {
        return false;
    };
    let expected = match crate::record_drop_commit::inspect_drop_commit_for_open_session(
        base,
        project_hash,
        &session_id,
    ) {
        Ok(Some(expected)) => expected,
        Ok(None) | Err(_) => return false,
    };
    if !drop_commit_matches_observed_capture(&expected, record_take_tracker, record_sm.generation())
    {
        return false;
    }
    let Ok(Some(expected)) = crate::record_drop_commit::bind_drop_commit_for_open_session(
        base,
        project_hash,
        &session_id,
    ) else {
        return false;
    };

    release_record_reservation(
        base,
        project_hash,
        post_instance_id,
        paired_pre_target,
        "drop_committed",
    );
    let _ = record_signal::mark_released_with_reason(
        base,
        project_hash,
        post_instance_id,
        record_signal::ReleaseReason::DropCommitted,
    );
    log::info!(
        "[IOThread POST] Drop committed exact session: session={} bounce={} post_iid={}",
        session_id,
        expected.bounce_id,
        post_instance_id
    );
    exit_record_preserve_pair(record_sm);
    true
}

#[cfg(test)]
#[path = "io_thread_post_drop_tests.rs"]
mod tests;
