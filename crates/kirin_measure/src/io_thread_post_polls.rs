//! Bounded periodic control-plane polls for the POST IO worker.

use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex, RwLock};
use std::time::{Duration, Instant};

use crate::pairing_scope::LatchedPre;
use crate::record::RecordStateMachine;
use crate::storage::StoragePaths;

use super::{TriggerPairResolutionFn, TriggerStopResolutionFn};

const ONE_SECOND: Duration = Duration::from_secs(1);
const IO_TICK: Duration = Duration::from_millis(100);

struct PollDeadline {
    next: Instant,
    interval: Duration,
}

impl PollDeadline {
    fn new(now: Instant, interval: Duration) -> Self {
        Self {
            next: now,
            interval,
        }
    }

    fn is_due(&self, now: Instant) -> bool {
        now >= self.next
    }

    fn complete(&mut self, now: Instant) {
        self.next = now + self.interval;
    }
}

pub(super) struct PostControlPolls {
    last_preset_available: Option<bool>,
    preset: PollDeadline,
    ack_timeout: PollDeadline,
    pair_label: PollDeadline,
    pre_liveness: PollDeadline,
    broadcasts: PollDeadline,
    processed_keep: crate::broadcast_edge::BroadcastEdgeMemory,
    processed_stop: crate::broadcast_edge::BroadcastEdgeMemory,
}

impl PostControlPolls {
    pub(super) fn new(now: Instant) -> Self {
        Self {
            last_preset_available: None,
            preset: PollDeadline::new(now, ONE_SECOND),
            ack_timeout: PollDeadline::new(now, ONE_SECOND),
            pair_label: PollDeadline::new(now, IO_TICK),
            pre_liveness: PollDeadline::new(now, ONE_SECOND),
            broadcasts: PollDeadline::new(now, IO_TICK),
            processed_keep: crate::broadcast_edge::BroadcastEdgeMemory::default(),
            processed_stop: crate::broadcast_edge::BroadcastEdgeMemory::default(),
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub(super) fn service(
        &mut self,
        project_hash: &str,
        post_instance_id: &str,
        sample_rate: u32,
        preset_available: &Arc<AtomicBool>,
        record_sm: &Arc<RecordStateMachine>,
        pair_label: &Arc<Mutex<String>>,
        paired_pre_target: &Arc<Mutex<Option<String>>>,
        record_ingress: &Arc<crate::RecordIngress>,
        latched_pre: &Arc<Mutex<Option<LatchedPre>>>,
        daw_session_id: &Arc<RwLock<String>>,
        trigger_pair_resolution: &TriggerPairResolutionFn,
        trigger_stop_resolution: &TriggerStopResolutionFn,
    ) {
        if self.preset.is_due(Instant::now()) {
            super::record_ack::poll_preset_availability(
                project_hash,
                preset_available,
                &mut self.last_preset_available,
            );
            self.preset.complete(Instant::now());
        }

        if self.ack_timeout.is_due(Instant::now()) {
            super::liveness::poll_ack_timeout(
                project_hash,
                post_instance_id,
                record_sm,
                pair_label,
                paired_pre_target,
            );
            self.ack_timeout.complete(Instant::now());
        }

        if self.pair_label.is_due(Instant::now()) {
            super::record_ack::poll_record_signal_ack(
                project_hash,
                post_instance_id,
                sample_rate,
                record_sm,
                pair_label,
                record_ingress,
            );
            self.pair_label.complete(Instant::now());
        }

        if self.pre_liveness.is_due(Instant::now()) {
            super::liveness::poll_latched_pre_liveness(post_instance_id, record_sm, latched_pre);
            self.pre_liveness.complete(Instant::now());
        }

        if self.broadcasts.is_due(Instant::now()) {
            if let Ok(paths) = StoragePaths::default_platform() {
                super::broadcast::poll_post_broadcasts(
                    &paths.plugin_data_dir(),
                    project_hash,
                    post_instance_id,
                    daw_session_id,
                    record_sm,
                    &mut self.processed_keep,
                    &mut self.processed_stop,
                    trigger_pair_resolution,
                    trigger_stop_resolution,
                );
            } else {
                log::warn!("[all_keep] StoragePaths::default_platform() failed; skipping tick");
            }
            self.broadcasts.complete(Instant::now());
        }
    }
}

#[cfg(test)]
#[path = "io_thread_post_polls_tests.rs"]
mod tests;
