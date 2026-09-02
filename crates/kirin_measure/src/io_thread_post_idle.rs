//! POST-owned idle Record stop policy and its explicit user-facing result.

use std::sync::atomic::AtomicU8;
use std::sync::{Arc, Mutex, RwLock};
use std::time::{Duration, Instant};

use crate::cleanup::exit_record_preserve_pair;
use crate::record::RecordStateMachine;
use crate::storage::StoragePaths;
use crate::{load_signal_state, record_signal, SignalState};

use super::liveness::release_record_reservation;
use super::policy::idle_autostop_due;

pub(super) struct IdleRecordStop {
    anchor: Instant,
    timeout: Option<Duration>,
}

impl IdleRecordStop {
    pub(super) fn new(now: Instant, timeout: Option<Duration>) -> Self {
        Self {
            anchor: now,
            timeout,
        }
    }

    fn observe(&mut self, now: Instant, is_recording: bool, is_active: bool) -> Option<Duration> {
        if !is_recording || is_active {
            self.anchor = now;
            return None;
        }
        let elapsed = now.saturating_duration_since(self.anchor);
        let timeout = self.timeout?;
        if !idle_autostop_due(true, false, elapsed, Some(timeout)) {
            return None;
        }
        self.anchor = now;
        Some(timeout)
    }

    #[allow(clippy::too_many_arguments)]
    pub(super) fn service(
        &mut self,
        now: Instant,
        record_sm: &Arc<RecordStateMachine>,
        signal_state: &Arc<AtomicU8>,
        writer_exists: bool,
        record_error_message: &Arc<RwLock<Option<String>>>,
        paired_pre_target: &Arc<Mutex<Option<String>>>,
        project_hash: &str,
        post_instance_id: &str,
    ) {
        let Some(timeout) = self.observe(
            now,
            record_sm.is_recording(),
            load_signal_state(signal_state) == SignalState::Active,
        ) else {
            return;
        };

        log::info!(
            "[IOThread POST] idle auto-stop: no Active signal for {:?} (post_iid={})",
            timeout,
            post_instance_id
        );
        if let Ok(paths) = StoragePaths::default_platform() {
            let base = paths.plugin_data_dir();
            release_record_reservation(
                &base,
                project_hash,
                post_instance_id,
                paired_pre_target,
                "idle_timeout",
            );
            let _ = record_signal::mark_released_with_reason(
                &base,
                project_hash,
                post_instance_id,
                record_signal::ReleaseReason::IdleTimeout,
            );
        }
        exit_record_preserve_pair(record_sm);
        if let Ok(mut message) = record_error_message.write() {
            *message = Some(idle_stop_message(timeout, writer_exists));
        }
    }
}

fn idle_stop_message(timeout: Duration, writer_exists: bool) -> String {
    let seconds = timeout.as_secs();
    let duration = if seconds.is_multiple_of(60) {
        format!("{} min", seconds / 60)
    } else {
        format!("{seconds} sec")
    };
    if writer_exists {
        format!("Auto-stopped after {duration} idle. Take saved.")
    } else {
        format!("Auto-stopped after {duration} idle.")
    }
}

#[cfg(test)]
#[path = "io_thread_post_idle_tests.rs"]
mod tests;
