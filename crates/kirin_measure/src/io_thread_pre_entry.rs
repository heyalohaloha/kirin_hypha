//! Exact PRE preparation, entry and measurement readiness.
use super::*;

pub(super) fn enter_pre_record_if_barrier_ready(
    ctx: PreRecordEntryContext<'_>,
    record_sm: &Arc<RecordStateMachine>,
    recording: &Arc<AtomicBool>,
    license: &License,
    signal: &record_signal::RecordSignal,
    record_ingress: &crate::RecordIngress,
) -> bool {
    if record_sm.is_recording() {
        let generation = record_sm.generation();
        if wait_for_entry_measure(&ctx, record_sm, signal, generation) {
            recording.store(true, Ordering::Relaxed);
            return true;
        }
        log::warn!(
            "[signal] PRE Record measure-ready timeout before ACK \
             (generation={}, pre_iid={}, session={})",
            generation,
            ctx.pre_instance_id,
            signal.session_id
        );
        return false;
    }
    if !is_os_license(license) {
        return false;
    }
    let started_at_ms = signal_started_at_ms(signal);
    if started_at_ms <= 0 {
        log::warn!(
            "[signal] PRE Record start withheld: invalid started_at (pre_iid={}, session={})",
            ctx.pre_instance_id,
            signal.session_id
        );
        return false;
    }
    let now_ms = crate::record_writer::now_epoch_ms();
    if now_ms < started_at_ms {
        return false;
    }
    if crate::record_writer::record_session_closed_for_role_instance(
        ctx.base,
        ctx.project_hash,
        ctx.pre_instance_id,
        PluginDataRole::Pre,
        &signal.session_id,
    ) {
        log::warn!(
            "[signal] PRE Record start withheld: session already closed on disk \
             (pre_iid={}, session={})",
            ctx.pre_instance_id,
            signal.session_id
        );
        return false;
    }
    if crate::record_writer_claim::writer_claim_active(
        ctx.base,
        ctx.project_hash,
        &signal.session_id,
        PluginDataRole::Pre,
        ctx.pre_instance_id,
    )
    .unwrap_or(false)
    {
        log::warn!(
            "[signal] PRE Record start withheld: writer already active \
             (pre_iid={}, session={})",
            ctx.pre_instance_id,
            signal.session_id
        );
        return false;
    }
    // The in-memory lane must be reusable before taking the durable entry claim. Otherwise an
    // immediate second Keep can leave a fresh session's claim behind while the prior generation
    // is still draining, and every later poll would reject itself as AlreadyActive forever.
    let next_generation = record_sm.generation().saturating_add(1);
    if !record_ingress.prepare_for_generation(next_generation) {
        log::warn!(
            "[signal] PRE Record start withheld: Record ingest lane is not drained/prepared \
             (generation={}, pre_iid={}, session={})",
            next_generation,
            ctx.pre_instance_id,
            signal.session_id
        );
        return false;
    }
    match crate::record_entry_lock::claim_record_entry(
        ctx.base,
        ctx.project_hash,
        &signal.session_id,
        PluginDataRole::Pre,
        ctx.pre_instance_id,
    ) {
        Ok(()) => {}
        Err(crate::record_entry_lock::RecordEntryLockError::AlreadyActive { .. }) => {
            log::warn!(
                "[signal] PRE Record start withheld: record entry already owned \
                 (pre_iid={}, session={})",
                ctx.pre_instance_id,
                signal.session_id
            );
            return false;
        }
        Err(e) => {
            log::warn!(
                "[signal] PRE Record start withheld: record entry claim failed \
                 (pre_iid={}, session={}): {}",
                ctx.pre_instance_id,
                signal.session_id,
                e
            );
            return false;
        }
    }
    match record_sm.try_enter_record_started_at_clock_window_transaction(
        *license,
        started_at_ms,
        signal.started_at_position_samples,
        signal.expected_end_position_samples_for_sample_rate(ctx.sample_rate),
        signal.session_id.clone(),
    ) {
        Ok(()) => {
            recording.store(true, Ordering::Relaxed);
            let generation = record_sm.generation();
            if !wait_for_entry_measure(&ctx, record_sm, signal, generation) {
                log::warn!(
                    "[signal] PRE Record measure-ready timeout before ACK (entered) \
                     (generation={}, pre_iid={}, session={})",
                    generation,
                    ctx.pre_instance_id,
                    signal.session_id
                );
                return false;
            }
            log::info!(
                "[signal] PRE entered Record at barrier (pre_iid={}, session={}, started_at={})",
                ctx.pre_instance_id,
                signal.session_id,
                signal.started_at
            );
            true
        }
        Err(crate::record::TransitionError::AlreadyRecording) => {
            crate::cleanup::retire_failed_record_entry(record_sm, record_ingress, next_generation);
            let entered = record_sm.is_recording();
            recording.store(entered, Ordering::Relaxed);
            entered
        }
        Err(e) => {
            crate::cleanup::retire_failed_record_entry(record_sm, record_ingress, next_generation);
            log::warn!("[signal] PRE Record barrier enter rejected: {:?}", e);
            false
        }
    }
}

fn wait_for_entry_measure(
    ctx: &PreRecordEntryContext<'_>,
    record_sm: &RecordStateMachine,
    signal: &record_signal::RecordSignal,
    generation: u64,
) -> bool {
    let ready = wait_for_measure_ready(record_sm, generation);
    if record_sm.is_recording()
        && record_sm.generation() == generation
        && record_sm.record_session_id().as_deref() == Some(signal.session_id.as_str())
    {
        if let Err(error) = crate::record_entry_lock::publish_measure_preparation(
            ctx.base,
            ctx.project_hash,
            crate::plugin_data::Role::Pre,
            ctx.pre_instance_id,
            signal,
            generation,
            record_sm.measure_preparation_samples(generation),
        ) {
            log::warn!("PRE preparation progress could not be published: {error}");
        }
    }
    ready
}
