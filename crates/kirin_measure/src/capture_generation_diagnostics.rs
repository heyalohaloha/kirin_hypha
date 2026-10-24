//! Failure-only producer barrier observations, collected before transaction rollback.
use super::*;

pub(super) fn producer_readiness_diagnostic(
    base: &Path,
    generation: &CaptureGeneration,
    report: &ready::ProducerWaitReport,
) -> serde_json::Value {
    let mut entry_claims = 0;
    let mut acknowledgements = 0;
    let mut preparation_samples = 0_usize;
    let members: Vec<_> = generation.members.iter().map(|member| {
        let entered = crate::record_entry_lock::record_entry_claimed(
            base, &member.project_hash, &member.record_session_id, Role::Pre, &member.pre_instance_id,
        );
        let signal = read_signal(base, &member.project_hash, &member.post_instance_id);
        let acknowledged = signal.as_ref().is_some_and(|signal| {
            signal.status == crate::record_signal::SignalStatus::Acknowledged
                && signal.capture_generation_id == generation.capture_generation_id
                && signal.generation_started_at_ms == generation.started_at_ms
                && signal.session_id == member.record_session_id
        });
        entry_claims += usize::from(entered);
        acknowledgements += usize::from(acknowledged);
        let pre_samples = crate::record_entry_lock::measure_preparation_samples(base, generation, member, Role::Pre);
        let post_samples = crate::record_entry_lock::measure_preparation_samples(base, generation, member, Role::Post);
        let samples = pre_samples.saturating_add(post_samples);
        preparation_samples = preparation_samples.saturating_add(samples);
        let writer = |role, iid: &str| crate::record_writer_claim::writer_claim_readiness_diagnostic(
            base, &member.project_hash, &member.record_session_id, role, iid,
            &generation.capture_generation_id, generation.started_at_ms, generation.host_process_id,
        );
        serde_json::json!({
            "project": member.project_hash,
            "session": member.record_session_id,
            "pre_instance": member.pre_instance_id,
            "post_instance": member.post_instance_id,
            "pre_entry_claim": entered,
            "acknowledged": acknowledged,
            "measure_preparation_samples": samples,
            "pre_measure_preparation_samples": pre_samples,
            "post_measure_preparation_samples": post_samples,
            "signal_status": signal.as_ref().map(|signal| format!("{:?}", signal.status)),
            "signal_generation": signal.as_ref().map(|signal| &signal.capture_generation_id),
            "signal_started_at_ms": signal.as_ref().map(|signal| signal.generation_started_at_ms),
            "signal_session": signal.as_ref().map(|signal| &signal.session_id),
            "pre_writer": writer(Role::Pre, &member.pre_instance_id),
            "post_writer": writer(Role::Post, &member.post_instance_id),
        })
    }).collect();
    serde_json::json!({
        "capture_generation": generation.capture_generation_id,
        "started_at_ms": generation.started_at_ms,
        "host_process_id": generation.host_process_id,
        "host_process_alive": process_is_alive(generation.host_process_id),
        "wait": report,
        "snapshot_after_last_poll": true,
        "entry_claims": entry_claims,
        "acknowledgements": acknowledgements,
        "measure_preparation_samples": preparation_samples,
        "members": members,
    })
}

/// Concise failure reason. Full per-member predicates stay in the log; detailed worker/lane
/// snapshots belong to the host fixture's failure report.
pub(super) fn failure_reason(snapshot: &serde_json::Value) -> String {
    let missing = snapshot["members"]
        .as_array()
        .into_iter()
        .flatten()
        .find_map(|member| {
            let mut reasons = Vec::new();
            if member["acknowledged"] == false {
                reasons.push("PRE measure/ACK pending".to_string());
            }
            for (role, key) in [("PRE", "pre_writer"), ("POST", "post_writer")] {
                let writer = &member[key];
                let cause = if writer["read_error"].is_string() {
                    Some("claim unreadable")
                } else if writer["claim_present"] == false {
                    Some("writer missing")
                } else if writer["closed"] == true {
                    Some("writer closed")
                } else {
                    [
                        ("generation_matches", "generation mismatch"),
                        ("started_at_matches", "start mismatch"),
                        ("host_process_matches", "owner mismatch"),
                        ("host_process_alive", "owner stopped"),
                        ("heartbeat_current", "heartbeat stale"),
                        ("artifact_ready", "artifacts pending"),
                    ]
                    .into_iter()
                    .find_map(|(predicate, reason)| (writer[predicate] == false).then_some(reason))
                };
                if let Some(cause) = cause {
                    reasons.push(format!("{role} {cause}"));
                }
            }
            (!reasons.is_empty()).then(|| reasons.join(", "))
        })
        .unwrap_or_else(|| "readiness changed after the last observation".into());
    let wait = &snapshot["wait"];
    format!("capture generation producers did not become ready: generation={} ready={}/{} PRE-entry={} ACK={} Watch-samples={} no-progress={}ms safety-expired={} ({missing})",
        snapshot["capture_generation"].as_str().unwrap_or("unknown"),
        wait["ready_count"], wait["total"], snapshot["entry_claims"], snapshot["acknowledgements"],
        snapshot["measure_preparation_samples"], wait["since_progress_ms"], wait["safety_window_expired"])
}

#[cfg(test)]
#[path = "capture_generation_diagnostics_tests.rs"]
mod tests;
