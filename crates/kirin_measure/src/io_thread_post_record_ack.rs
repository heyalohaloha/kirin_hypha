//! POST Record acknowledgement and preset polling.

use super::format_pair_label;
use crate::plugin_data::Role as PluginDataRole;
use crate::record::RecordStateMachine;
use crate::record_signal::{self, SignalStatus};
use crate::record_writer::parse_iso8601_to_epoch_ms;
use crate::storage::StoragePaths;
use std::fs;
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

/// B-023 段階 4: record_signal の Acknowledged を検知して pair_label を更新。
///
/// `record_sm.is_recording()` でガードし、Stop 後の poll で削除前の Acknowledged
/// signal を読んで pair_label が復活する race を構造的に防ぐ。
/// 値変化時のみ書込（無音 idempotent / R-28 機能的沈黙）。
pub(super) fn poll_record_signal_ack(
    project_hash: &str,
    instance_id: &str,
    sample_rate: u32,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
    record_ingress: &crate::RecordIngress,
) {
    let base = match StoragePaths::default_platform() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_record_signal_ack_with_base(
        &base,
        project_hash,
        instance_id,
        sample_rate,
        record_sm,
        pair_label,
        record_ingress,
    );
}

pub(super) fn poll_record_signal_ack_with_base(
    base: &Path,
    project_hash: &str,
    instance_id: &str,
    sample_rate: u32,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
    record_ingress: &crate::RecordIngress,
) {
    let Some(signal) = record_signal::read_signal(base, project_hash, instance_id) else {
        return;
    };
    if signal.status != SignalStatus::Acknowledged {
        return;
    }
    if signal.expected_wav.is_some()
        && !signal
            .expected_wav
            .as_ref()
            .is_some_and(crate::record_expected::ExpectedWavMetadata::is_usable)
    {
        log::warn!(
            "[IOThread POST] ACK has invalid expected WAV metadata; Record may publish with \
             degraded integrity \
             (post_iid={})",
            instance_id
        );
    }
    if !record_sm.is_recording() {
        // A durable ACK is historical state, not permission to start a producer after an IO/DAW
        // restart. Only the exact live preparation lease may re-arm this POST. Once a generation
        // is committed, `preparing.json` is retired and an already-running writer continues from
        // memory; a fresh state machine must remain in Watch.
        if !post_ack_generation_is_authorized(base, project_hash, instance_id, &signal) {
            return;
        }
        let Some(started_at_ms) = parse_iso8601_to_epoch_ms(&signal.started_at) else {
            log::warn!(
                "[IOThread POST] ACK ignored: started_at invalid (post_iid={}, started_at={:?})",
                instance_id,
                signal.started_at
            );
            return;
        };
        let now_ms = crate::record_writer::now_epoch_ms();
        if now_ms < started_at_ms {
            return;
        }
        if crate::record_writer::record_session_closed_for_role_instance(
            base,
            project_hash,
            instance_id,
            PluginDataRole::Post,
            &signal.session_id,
        ) {
            log::warn!(
                "[IOThread POST] ACK ignored: session already closed on disk \
                 (session={}, post_iid={})",
                signal.session_id,
                instance_id
            );
            return;
        }
        if crate::record_writer_claim::writer_claim_active(
            base,
            project_hash,
            &signal.session_id,
            PluginDataRole::Post,
            instance_id,
        )
        .unwrap_or(false)
        {
            log::warn!(
                "[IOThread POST] ACK ignored: writer already active \
                 (session={}, post_iid={})",
                signal.session_id,
                instance_id
            );
            return;
        }
        // Prepare the reusable in-memory lane before claiming this session on disk. A second Keep
        // may arrive while the prior Record consumer is completing its drain; that transient must
        // remain retryable and must not strand a durable entry claim.
        let next_generation = record_sm.generation().saturating_add(1);
        if !record_ingress.prepare_for_generation(next_generation) {
            log::warn!(
                "[IOThread POST] ACK ignored: Record ingest lane is not drained/prepared \
                 (generation={}, session={}, post_iid={})",
                next_generation,
                signal.session_id,
                instance_id
            );
            return;
        }
        match crate::record_entry_lock::claim_record_entry(
            base,
            project_hash,
            &signal.session_id,
            PluginDataRole::Post,
            instance_id,
        ) {
            Ok(()) => {}
            Err(crate::record_entry_lock::RecordEntryLockError::AlreadyActive { .. }) => {
                log::warn!(
                    "[IOThread POST] ACK ignored: record entry already owned \
                     (session={}, post_iid={})",
                    signal.session_id,
                    instance_id
                );
                return;
            }
            Err(e) => {
                log::warn!(
                    "[IOThread POST] ACK ignored: record entry claim failed \
                     (session={}, post_iid={}): {}",
                    signal.session_id,
                    instance_id,
                    e
                );
                return;
            }
        }
        match record_sm.try_enter_record_started_at_clock_window_transaction(
            crate::License::Os,
            started_at_ms,
            signal.started_at_position_samples,
            signal.expected_end_position_samples_for_sample_rate(sample_rate),
            signal.session_id.clone(),
        ) {
            Ok(()) => log::info!(
                "[IOThread POST] ACK received; POST entered Record (session={}, post_iid={})",
                signal.session_id,
                instance_id
            ),
            Err(crate::record::TransitionError::AlreadyRecording) => {}
            Err(e) => {
                log::warn!("[IOThread POST] ACK Record enter rejected: {:?}", e);
                return;
            }
        }
    }
    let new_label = format_pair_label(&signal.paired_pre_name, &signal.target_pre_instance_id);
    if let Ok(mut g) = pair_label.lock() {
        if *g != new_label {
            log::info!(
                "[IOThread POST] pair_label updated: {} (paired_pre_name={:?})",
                new_label,
                signal.paired_pre_name
            );
            *g = new_label;
        }
    }
}

pub(super) fn post_ack_generation_is_authorized(
    base: &Path,
    project_hash: &str,
    post_instance_id: &str,
    signal: &record_signal::RecordSignal,
) -> bool {
    // Legacy signals retain their old compatibility path, but every signal emitted by the
    // current producer carries an immutable generation and is governed by the strict lease.
    if signal.capture_generation_id.trim().is_empty() {
        return true;
    }
    let Ok(Some(generation)) = crate::capture_generation::read_producer_authorized_generation(
        base,
        project_hash,
        &signal.capture_generation_id,
        signal.generation_started_at_ms,
    ) else {
        return false;
    };
    // The immutable roster was assembled only after operation-group resolution and is already
    // bound to one live host process plus exact project/POST/session/PRE identities below. Some
    // hosts assign distinct non-empty DAW ids to AU and VST3 instances in the same document; a
    // second DAW-id equality check here would acknowledge the PRE but strand the corresponding
    // POST forever. No scope is widened: a signal absent from the exact roster still fails.
    if generation.host_process_id != crate::current_host_process_id() {
        return false;
    }
    generation
        .member(project_hash, post_instance_id)
        .is_some_and(|member| {
            member.record_session_id == signal.session_id
                && member.pre_instance_id == signal.target_pre_instance_id
        })
}

// ── preset/ poller ──────────────────────────────────────────────────────────

pub(super) fn current_preset_exists(preset_dir: &Path) -> bool {
    fs::metadata(preset_dir.join("current.json"))
        .map(|metadata| metadata.is_file())
        .unwrap_or(false)
}

pub(super) fn poll_preset_availability(
    project_hash: &str,
    preset_available: &Arc<AtomicBool>,
    last_seen: &mut Option<bool>,
) {
    let preset_dir = match StoragePaths::default_platform() {
        // B-128 (G-115-370): within-base wall（preset availability read。preset_dir 同等の inline 構築）。
        Ok(paths) => paths
            .plugin_data_dir()
            .join(&*crate::path_identity::guard_path_component(
                project_hash,
                "io_thread_post.poll_preset.project_hash",
            ))
            .join(crate::preset::PRESET_SUBDIR),
        Err(_) => {
            if *last_seen != Some(false) {
                log::info!("[preset] unavailable");
                *last_seen = Some(false);
            }
            preset_available.store(false, Ordering::Relaxed);
            return;
        }
    };
    let available = current_preset_exists(&preset_dir);
    preset_available.store(available, Ordering::Relaxed);

    if *last_seen != Some(available) {
        if available {
            log::info!("[preset] available");
        } else {
            log::info!("[preset] unavailable");
        }
        *last_seen = Some(available);
    }
}
