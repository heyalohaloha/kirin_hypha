//! The producer barrier of a capture generation: which PRE/POST writers are ready, and the
//! observable progress of a slow host before they are.

use super::*;

/// The producer barrier: true once every one of `total` producers is ready. `observe` returns the
/// ready producers and the progress made so far (ready producers plus earlier observable steps,
/// such as a PRE that acknowledged the Keep and is opening its writer). Ends after `timeout`
/// without progress. Each new milestone also renews the safety window, so its initial deadline
/// never rejects a roster that is still progressing.
#[cfg(test)]
pub(super) fn wait_for_producers(
    timeout: Duration,
    observe: impl FnMut() -> (usize, usize),
    total: usize,
    now: impl FnMut() -> Instant,
    pause: impl FnMut(),
) -> bool {
    wait_for_producers_report(timeout, observe, total, now, pause).ready
}

#[derive(Debug, Default, serde::Serialize)]
pub(super) struct ProducerWaitReport {
    pub ready: bool,
    pub ready_count: usize,
    pub last_progress: usize,
    pub best_progress: usize,
    pub total: usize,
    pub polls: u64,
    pub elapsed_ms: Option<u128>,
    pub since_progress_ms: Option<u128>,
    pub no_progress_timeout_ms: u128,
    pub safety_window_ms: u128,
    pub no_progress_expired: bool,
    pub safety_window_expired: bool,
}

/// Same barrier decisions and clock reads; the extra fields describe only the observation that
/// ended the wait. A long scheduling gap can cross both deadlines, so both flags are retained.
pub(super) fn wait_for_producers_report(
    timeout: Duration,
    mut observe: impl FnMut() -> (usize, usize),
    total: usize,
    mut now: impl FnMut() -> Instant,
    mut pause: impl FnMut(),
) -> ProducerWaitReport {
    let started = now();
    let safety_window = timeout.max(crate::capture_contract::CAPTURE_PRODUCER_READY_LIMIT);
    let mut limit = started + safety_window;
    let mut no_progress_deadline = started + timeout;
    let mut best = 0;
    let mut last_progress_at = started;
    let mut polls = 0;
    loop {
        let (count, progress) = observe();
        polls += 1;
        if total > 0 && count >= total {
            return ProducerWaitReport {
                ready: true,
                ready_count: count,
                total,
                polls,
                ..Default::default()
            };
        }
        let at = now();
        let progress = progress.max(count);
        if progress > best {
            best = progress;
            last_progress_at = at;
            no_progress_deadline = at + timeout;
            limit = at + safety_window;
        }
        if at >= no_progress_deadline || at >= limit {
            return ProducerWaitReport {
                ready: false,
                ready_count: count,
                last_progress: progress,
                best_progress: best,
                total,
                polls,
                elapsed_ms: Some(at.saturating_duration_since(started).as_millis()),
                since_progress_ms: Some(at.saturating_duration_since(last_progress_at).as_millis()),
                no_progress_timeout_ms: timeout.as_millis(),
                safety_window_ms: safety_window.as_millis(),
                no_progress_expired: at >= no_progress_deadline,
                safety_window_expired: at >= limit,
            };
        }
        pause();
    }
}

/// Steps each roster member's PRE has taken into this exact generation before its writer is ready:
/// it took the Record entry claim, then acknowledged (its Record and Measure are up and its writer
/// is being opened). Progress only; never readiness.
pub(super) fn acknowledged_member_count(base_dir: &Path, generation: &CaptureGeneration) -> usize {
    generation
        .members
        .iter()
        .map(|member| {
            let entered = crate::record_entry_lock::record_entry_claimed(
                base_dir,
                &member.project_hash,
                &member.record_session_id,
                Role::Pre,
                &member.pre_instance_id,
            );
            let acknowledged =
                read_signal(base_dir, &member.project_hash, &member.post_instance_id).is_some_and(
                    |signal| {
                        signal.status == crate::record_signal::SignalStatus::Acknowledged
                            && signal.capture_generation_id == generation.capture_generation_id
                            && signal.generation_started_at_ms == generation.started_at_ms
                            && signal.session_id == member.record_session_id
                    },
                );
            usize::from(entered) + usize::from(acknowledged)
        })
        .sum()
}

pub(super) fn generation_producers_ready(base_dir: &Path, generation: &CaptureGeneration) -> bool {
    !generation.members.is_empty()
        && ready_producer_count(base_dir, generation) == generation.members.len() * 2
}

pub(super) fn producer_readiness_observation(
    base: &Path,
    generation: &CaptureGeneration,
) -> (usize, usize) {
    let ready = ready_producer_count(base, generation);
    // Either role can own its lane while draining a finite Watch tail before its writer opens.
    // Count completed input from the exact session/generation; an IO heartbeat is not work.
    let samples = generation.members.iter().fold(0_usize, |total, member| {
        [Role::Pre, Role::Post]
            .into_iter()
            .fold(total, |total, role| {
                total.saturating_add(crate::record_entry_lock::measure_preparation_samples(
                    base, generation, member, role,
                ))
            })
    });
    (
        ready,
        ready
            .saturating_add(acknowledged_member_count(base, generation))
            .saturating_add(samples),
    )
}

/// PRE and POST writers of the roster that own their exact Record session and are ready.
pub(super) fn ready_producer_count(base_dir: &Path, generation: &CaptureGeneration) -> usize {
    if !process_is_alive(generation.host_process_id) {
        return 0;
    }
    let ready = |member: &crate::capture_generation::CaptureGenerationMember,
                 role: Role,
                 instance_id: &str| {
        !instance_id.trim().is_empty()
            && crate::record_writer_claim::writer_claim_ready_for_generation(
                base_dir,
                &member.project_hash,
                &member.record_session_id,
                role,
                instance_id,
                &generation.capture_generation_id,
                generation.started_at_ms,
                generation.host_process_id,
            )
            .unwrap_or(false)
    };
    generation
        .members
        .iter()
        .map(|member| {
            usize::from(ready(member, Role::Pre, &member.pre_instance_id))
                + usize::from(ready(member, Role::Post, &member.post_instance_id))
        })
        .sum()
}
