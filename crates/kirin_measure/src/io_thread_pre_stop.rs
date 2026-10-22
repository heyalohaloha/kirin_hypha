//! Authorized PRE stop handling.
use super::*;

#[allow(clippy::too_many_arguments)]
pub(super) fn poll_all_stop_signal_in_projects_at(
    base: &Path,
    project_hashes: &[String],
    record_sm: &Arc<RecordStateMachine>,
    recording: &Arc<AtomicBool>,
    record_acknowledged: &Arc<AtomicBool>,
    partner: &mut Option<PartnerInfo>,
    now: chrono::DateTime<chrono::Utc>,

    record_ingress: &crate::RecordIngress,
) -> bool {
    if !record_sm.is_recording() {
        return false;
    }

    let Some((project_hash, originator_iid, stop_started_at, partner_iid, session_id)) =
        partner.as_ref().and_then(|p| {
            let record_started_at_ms = record_sm.record_started_at_ms();
            project_hashes.iter().find_map(|project_hash| {
                all_stop_signal::read_current_stop_broadcast(base, project_hash)
                    .into_iter()
                    .find_map(|(originator_iid, broadcast)| {
                        if all_stop_authorizes_pre_stop(
                            base,
                            &broadcast,
                            p,
                            record_started_at_ms,
                            now,
                        ) {
                            Some((
                                project_hash.clone(),
                                originator_iid,
                                broadcast.started_at,
                                p.post_instance_id.clone(),
                                p.session_id.clone(),
                            ))
                        } else {
                            None
                        }
                    })
            })
        })
    else {
        return false;
    };

    crate::cleanup::exit_record_releasing_unattached_lane(record_sm, record_ingress);
    recording.store(false, Ordering::Relaxed);
    record_acknowledged.store(false, Ordering::Relaxed);
    *partner = None;
    log::info!(
        "[all_stop PRE] authorized broadcast, PRE exiting Record \
         (project_hash={}, originator={}, partner={}, session={}, started_at={})",
        project_hash,
        originator_iid,
        partner_iid,
        session_id,
        stop_started_at
    );
    true
}

#[allow(clippy::too_many_arguments)]
pub(super) fn poll_all_stop_signal_at(
    base: &Path,
    project_hash: &str,
    record_sm: &Arc<RecordStateMachine>,
    recording: &Arc<AtomicBool>,
    record_acknowledged: &Arc<AtomicBool>,
    partner: &mut Option<PartnerInfo>,
    now: chrono::DateTime<chrono::Utc>,

    record_ingress: &crate::RecordIngress,
) -> bool {
    if !record_sm.is_recording() {
        return false;
    }
    let Some(active_partner) = partner.as_ref() else {
        return false;
    };
    let project_hashes = all_stop_project_hashes(project_hash, active_partner);
    poll_all_stop_signal_in_projects_at(
        base,
        &project_hashes,
        record_sm,
        recording,
        record_acknowledged,
        partner,
        now,
        record_ingress,
    )
}

#[allow(clippy::too_many_arguments)]
pub(super) fn poll_all_stop_signal_for_active_writer_at(
    base: &Path,
    scope: Option<&ActiveWriterStopScope>,
    record_sm: &Arc<RecordStateMachine>,
    recording: &Arc<AtomicBool>,
    record_acknowledged: &Arc<AtomicBool>,
    partner: &mut Option<PartnerInfo>,
    now: chrono::DateTime<chrono::Utc>,

    record_ingress: &crate::RecordIngress,
) -> bool {
    if !record_sm.is_recording() {
        return false;
    }
    let Some(scope) = scope else {
        return false;
    };

    let Some((originator_iid, stop_started_at)) =
        all_stop_signal::read_current_stop_broadcast(base, &scope.project_hash)
            .into_iter()
            .find_map(|(originator_iid, broadcast)| {
                all_stop_authorizes_active_writer_stop(base, &broadcast, scope, now)
                    .then_some((originator_iid, broadcast.started_at))
            })
    else {
        return false;
    };

    crate::cleanup::exit_record_releasing_unattached_lane(record_sm, record_ingress);
    recording.store(false, Ordering::Relaxed);
    record_acknowledged.store(false, Ordering::Relaxed);
    *partner = None;
    log::info!(
        "[all_stop PRE] active writer fallback authorized broadcast, PRE exiting Record \
         (project_hash={}, originator={}, paired_post={:?}, session={:?}, started_at={})",
        scope.project_hash,
        originator_iid,
        scope.paired_post_instance_id.as_deref(),
        scope.record_session_id.as_deref(),
        stop_started_at
    );
    true
}

pub(super) fn poll_all_stop_signal_for_active_writer(
    base: &Path,
    ctx: Option<&RecordingCtx>,
    record_sm: &Arc<RecordStateMachine>,
    recording: &Arc<AtomicBool>,
    record_acknowledged: &Arc<AtomicBool>,
    partner: &mut Option<PartnerInfo>,

    record_ingress: &crate::RecordIngress,
) -> bool {
    let scope = active_writer_stop_scope(ctx);
    poll_all_stop_signal_for_active_writer_at(
        base,
        scope.as_ref(),
        record_sm,
        recording,
        record_acknowledged,
        partner,
        chrono::Utc::now(),
        record_ingress,
    )
}

pub(super) fn poll_all_stop_signal(
    base: &Path,
    project_hash: &str,
    record_sm: &Arc<RecordStateMachine>,
    recording: &Arc<AtomicBool>,
    record_acknowledged: &Arc<AtomicBool>,
    partner: &mut Option<PartnerInfo>,

    record_ingress: &crate::RecordIngress,
) -> bool {
    poll_all_stop_signal_at(
        base,
        project_hash,
        record_sm,
        recording,
        record_acknowledged,
        partner,
        chrono::Utc::now(),
        record_ingress,
    )
}
