//! Bounded All Stop / All Keep reception for the POST IO coordinator.

use std::path::Path;
use std::sync::{Arc, RwLock};

use crate::all_keep_signal::{self, ALL_KEEP_BROADCAST_STALE_SECS};
use crate::all_stop_signal::{self, ALL_STOP_BROADCAST_STALE_SECS};
use crate::broadcast_edge::BroadcastEdgeMemory;
use crate::record::RecordStateMachine;

use super::{TriggerPairResolutionFn, TriggerStopResolutionFn};

#[allow(clippy::too_many_arguments)]
pub(super) fn poll_post_broadcasts(
    base_dir: &Path,
    project_hash: &str,
    instance_id: &str,
    daw_session_id: &Arc<RwLock<String>>,
    record_sm: &RecordStateMachine,
    processed_keep: &mut BroadcastEdgeMemory,
    processed_stop: &mut BroadcastEdgeMemory,
    trigger_pair_resolution: &TriggerPairResolutionFn,
    trigger_stop_resolution: &TriggerStopResolutionFn,
) {
    // Stop is deliberately observed first. A fresh legacy Stop becomes a barrier for an older or
    // equal legacy Keep in this same poll.
    let mut latest_fresh_stop_started_at: Option<String> = None;
    let now_chrono = chrono::Utc::now();
    let daw_session_id_snapshot = super::identity::read_daw_session_id_arc(daw_session_id);
    let host_process_id_snapshot = crate::current_host_process_id();
    let stop_broadcasts = all_stop_signal::read_current_stop_broadcast(base_dir, project_hash);
    for (originator_iid, broadcast) in stop_broadcasts.into_iter().take(1) {
        let stop_key = if broadcast.has_generation() {
            broadcast.capture_generation_id.clone()
        } else {
            format!("legacy:{}", broadcast.started_at)
        };
        if !super::identity::broadcast_scope_or_same_project_host_matches(
            &daw_session_id_snapshot,
            host_process_id_snapshot,
            &broadcast.daw_session_id,
            broadcast.host_process_id,
        ) {
            continue;
        }
        if broadcast.has_generation()
            && !super::policy::generation_stop_authorizes_post(
                base_dir,
                project_hash,
                instance_id,
                &broadcast,
            )
        {
            continue;
        }
        if !broadcast.has_generation()
            && all_stop_signal::is_stop_broadcast_stale(
                &broadcast,
                now_chrono,
                ALL_STOP_BROADCAST_STALE_SECS,
            )
        {
            processed_stop.remember(&originator_iid, &stop_key);
            log::debug!(
                "[all_stop] stale broadcast cached without fire: originator={}",
                originator_iid
            );
            continue;
        }
        if !broadcast.has_generation() {
            super::policy::remember_latest_started_at(
                &mut latest_fresh_stop_started_at,
                &broadcast.started_at,
            );
        }
        if originator_iid == instance_id {
            continue;
        }
        if processed_stop.contains(&originator_iid, &stop_key) {
            continue;
        }
        processed_stop.remember(&originator_iid, &stop_key);
        let scan_dir = all_stop_signal::stop_signals_dir(base_dir, project_hash);
        log::info!(
            "[all_stop] new broadcast detected: originator={} started_at={} scan_dir={}",
            originator_iid,
            broadcast.started_at,
            scan_dir.display()
        );
        trigger_stop_resolution(&originator_iid, &broadcast.started_at);
    }

    let now_chrono = chrono::Utc::now();
    let daw_session_id_snapshot = super::identity::read_daw_session_id_arc(daw_session_id);
    let host_process_id_snapshot = crate::current_host_process_id();
    let broadcasts = all_keep_signal::read_current_broadcast(base_dir, project_hash);
    for (originator_iid, broadcast) in broadcasts.into_iter().take(1) {
        let broadcast_key = if broadcast.capture_generation_id.trim().is_empty() {
            format!("legacy:{}", broadcast.started_at)
        } else {
            broadcast.capture_generation_id.clone()
        };
        if !super::identity::broadcast_scope_or_same_project_host_matches(
            &daw_session_id_snapshot,
            host_process_id_snapshot,
            &broadcast.daw_session_id,
            broadcast.host_process_id,
        ) {
            continue;
        }
        if originator_iid == instance_id {
            processed_keep.remember(&originator_iid, &broadcast_key);
            continue;
        }
        if processed_keep.contains(&originator_iid, &broadcast_key) {
            continue;
        }
        let broadcast_is_stale = all_keep_signal::is_broadcast_stale(
            &broadcast,
            now_chrono,
            ALL_KEEP_BROADCAST_STALE_SECS,
        );
        if broadcast_is_stale && broadcast.capture_generation_id.trim().is_empty() {
            processed_keep.remember(&originator_iid, &broadcast_key);
            log::debug!(
                "[all_keep] stale broadcast cached without fire: originator={}, started_at={}",
                originator_iid,
                broadcast.started_at
            );
            continue;
        }
        if super::policy::keep_broadcast_blocked_by_stop(
            &broadcast.started_at,
            latest_fresh_stop_started_at.as_deref(),
        ) {
            processed_keep.remember(&originator_iid, &broadcast_key);
            log::info!(
                "[all_keep] keep broadcast suppressed by newer/equal all_stop: originator={} keep_started_at={} stop_started_at={}",
                originator_iid,
                broadcast.started_at,
                latest_fresh_stop_started_at.as_deref().unwrap_or("")
            );
            continue;
        }
        if broadcast.capture_generation_id.trim().is_empty()
            || broadcast.generation_started_at_ms <= 0
        {
            processed_keep.remember(&originator_iid, &broadcast_key);
            log::debug!(
                "[all_keep] legacy broadcast skipped without generation: originator={}",
                originator_iid
            );
            continue;
        }
        let generation = match crate::capture_generation::read_producer_authorized_generation(
            base_dir,
            project_hash,
            &broadcast.capture_generation_id,
            broadcast.generation_started_at_ms,
        ) {
            Ok(Some(project_generation))
                if project_generation
                    .member(project_hash, instance_id)
                    .is_some() =>
            {
                project_generation
            }
            _ if broadcast_is_stale => {
                processed_keep.remember(&originator_iid, &broadcast_key);
                continue;
            }
            _ => continue,
        };
        let entered = trigger_pair_resolution(&originator_iid, &broadcast.started_at, &generation);
        // A failed explicit arm remains retryable. Only success or an already-recording restart
        // consumes this immutable generation edge.
        if entered || record_sm.is_recording() {
            if entered {
                log::info!(
                    "[all_keep] generation member armed: originator={} generation={}",
                    originator_iid,
                    generation.capture_generation_id
                );
            }
            processed_keep.remember(&originator_iid, &broadcast_key);
        }
    }
}
