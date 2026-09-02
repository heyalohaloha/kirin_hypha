//! Exact stopped-session Drop recovery with one bounded polling address.

use std::path::Path;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::record::RecordStateMachine;
use crate::record_signal::{self, SignalStatus};
use crate::storage::StoragePaths;

const POLL_INTERVAL: Duration = Duration::from_secs(1);

pub(super) struct ClosedDropRecovery {
    next_poll: Instant,
    completed_session: Option<String>,
}

impl ClosedDropRecovery {
    pub(super) fn new(now: Instant) -> Self {
        Self {
            next_poll: now,
            completed_session: None,
        }
    }

    fn service_with<F, C>(
        &mut self,
        now: Instant,
        is_recording: bool,
        recover: F,
        completion_clock: C,
    ) where
        F: FnOnce(Option<&str>) -> Option<String>,
        C: FnOnce() -> Instant,
    {
        if is_recording || now < self.next_poll {
            return;
        }
        if let Some(session_id) = recover(self.completed_session.as_deref()) {
            self.completed_session = Some(session_id);
        }
        self.next_poll = completion_clock() + POLL_INTERVAL;
    }

    pub(super) fn service(
        &mut self,
        now: Instant,
        record_sm: &Arc<RecordStateMachine>,
        paired_pre_target: &Arc<Mutex<Option<String>>>,
        project_hash: &str,
        post_instance_id: &str,
    ) {
        self.service_with(
            now,
            record_sm.is_recording(),
            |completed_session| {
                let Ok(paths) = StoragePaths::default_platform() else {
                    return None;
                };
                let memory_session = record_sm.last_closed_session_id();
                let memory_pre = paired_pre_target
                    .lock()
                    .ok()
                    .and_then(|guard| guard.clone());
                reconcile_closed_drop_target(
                    &paths.plugin_data_dir(),
                    project_hash,
                    post_instance_id,
                    memory_session.as_deref(),
                    memory_pre.as_deref(),
                    completed_session,
                )
            },
            Instant::now,
        );
    }
}

/// Resolve one stopped-session Drop address without scanning project history.
fn resolve_closed_drop_target(
    base: &Path,
    project_hash: &str,
    post_instance_id: &str,
    memory_session_id: Option<&str>,
    memory_pre_instance_id: Option<&str>,
) -> Option<(String, String)> {
    let safe = |value: &str| crate::path_identity::is_path_safe_component(value);
    if let Some(signal) = record_signal::read_signal(base, project_hash, post_instance_id) {
        if signal.status == SignalStatus::Released
            && signal.requested_by == post_instance_id
            && safe(&signal.session_id)
            && safe(&signal.target_pre_instance_id)
        {
            return Some((signal.session_id, signal.target_pre_instance_id));
        }
    }
    let session_id = memory_session_id.filter(|value| safe(value))?;
    let pre_instance_id = memory_pre_instance_id.filter(|value| safe(value))?;
    Some((session_id.to_string(), pre_instance_id.to_string()))
}

/// Reconcile one exact stopped session and return its id only after durable completion exists.
fn reconcile_closed_drop_target(
    base: &Path,
    project_hash: &str,
    post_instance_id: &str,
    memory_session_id: Option<&str>,
    memory_pre_instance_id: Option<&str>,
    completed_session_id: Option<&str>,
) -> Option<String> {
    let (session_id, pre_instance_id) = resolve_closed_drop_target(
        base,
        project_hash,
        post_instance_id,
        memory_session_id,
        memory_pre_instance_id,
    )?;
    if completed_session_id == Some(session_id.as_str()) {
        return None;
    }

    let reconciled = crate::plugin_data::reconcile_drop_committed_closed_session(
        base,
        project_hash,
        &session_id,
        &pre_instance_id,
        post_instance_id,
    );
    (reconciled > 0
        || crate::plugin_data::pair_record_session_manifest_exists(base, project_hash, &session_id))
    .then_some(session_id)
}

#[cfg(test)]
#[path = "io_thread_post_closed_drop_tests.rs"]
mod tests;
