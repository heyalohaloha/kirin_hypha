//! Pure policy decisions used by the POST IO coordinator.
//!
//! This module owns stop/keep ordering, Record idle expiry, Drop evidence matching,
//! and the repeated-observation gate for releasing a conflicting self-check claim.

use std::path::Path;
use std::time::Duration;

use crate::all_stop_signal;
use crate::record_signal::{self, SignalStatus};
use crate::RecordTakeTracker;

/// Record idle auto-stop default threshold (10 minutes).
const RECORD_IDLE_TIMEOUT_DEFAULT_SECS: u64 = 600;
pub(super) const SELF_CHECK_RELEASE_CONFIRMATIONS: u8 = 3;

#[derive(Debug, Default)]
pub(super) struct SelfCheckReleaseGate {
    candidate: Option<SelfCheckReleaseCandidate>,
}

#[derive(Debug)]
struct SelfCheckReleaseCandidate {
    pair_key: String,
    pair_claimed_at: f64,
    confirmations: u8,
}

impl SelfCheckReleaseGate {
    pub(super) fn reset(&mut self) {
        self.candidate = None;
    }

    pub(super) fn observe_conflict(&mut self, pair_key: &str, pair_claimed_at: f64) -> bool {
        if pair_key.is_empty() {
            self.reset();
            return false;
        }

        match self.candidate.as_mut() {
            Some(candidate)
                if candidate.pair_key == pair_key
                    && candidate.pair_claimed_at == pair_claimed_at =>
            {
                candidate.confirmations = candidate.confirmations.saturating_add(1);
                candidate.confirmations >= SELF_CHECK_RELEASE_CONFIRMATIONS
            }
            _ => {
                self.candidate = Some(SelfCheckReleaseCandidate {
                    pair_key: pair_key.to_string(),
                    pair_claimed_at,
                    confirmations: 1,
                });
                false
            }
        }
    }
}

/// Resolve the Record idle timeout from its optional environment override.
pub(super) fn parse_idle_timeout(raw: Option<String>) -> Duration {
    let secs = raw
        .and_then(|s| s.trim().parse::<u64>().ok())
        .filter(|&s| s >= 5)
        .unwrap_or(RECORD_IDLE_TIMEOUT_DEFAULT_SECS);
    Duration::from_secs(secs)
}

/// Resolve the idle timeout once when the IO thread starts.
pub(super) fn record_idle_timeout() -> Option<Duration> {
    Some(parse_idle_timeout(
        std::env::var("KIRIN_RECORD_IDLE_TIMEOUT_SECS").ok(),
    ))
}

#[inline]
pub(super) fn idle_autostop_due(
    is_recording: bool,
    is_active: bool,
    idle_elapsed: Duration,
    timeout: Option<Duration>,
) -> bool {
    timeout.is_some_and(|timeout| is_recording && !is_active && idle_elapsed >= timeout)
}

pub(super) fn drop_commit_matches_observed_capture(
    expected: &crate::record_expected::ExpectedWavMetadata,
    tracker: &RecordTakeTracker,
    generation: u64,
) -> bool {
    let bwf_matches = expected
        .wav_time_reference_samples
        .and_then(|start| {
            let start = i64::try_from(start).ok()?;
            let duration = i64::try_from(expected.expected_duration_samples).ok()?;
            Some((start, start.checked_add(duration)?))
        })
        .is_some_and(|(start, end)| tracker.observed_content_range(start, end));
    if bwf_matches {
        return true;
    }

    expected.wav_time_reference_samples.is_none()
        && tracker.snapshot(generation).is_some_and(|snapshot| {
            snapshot.generation == generation
                && snapshot.duration_samples == expected.expected_duration_samples
                && snapshot
                    .host_start_position_samples
                    .zip(snapshot.host_end_position_samples)
                    .and_then(|(start, end)| end.checked_sub(start))
                    == i64::try_from(expected.expected_duration_samples).ok()
        })
}

/// A newer/equal All Stop is a filesystem-level barrier for an older All Keep.
#[inline]
pub(super) fn keep_broadcast_blocked_by_stop(
    keep_started_at: &str,
    latest_stop_started_at: Option<&str>,
) -> bool {
    latest_stop_started_at.is_some_and(|stop_started_at| keep_started_at <= stop_started_at)
}

#[inline]
pub(super) fn remember_latest_started_at(latest: &mut Option<String>, candidate: &str) {
    if latest
        .as_deref()
        .map(|existing| candidate > existing)
        .unwrap_or(true)
    {
        *latest = Some(candidate.to_string());
    }
}

pub(super) fn generation_stop_authorizes_post(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    broadcast: &all_stop_signal::AllStopBroadcast,
) -> bool {
    if !broadcast.has_generation() {
        return true;
    }
    let terminal = crate::capture_generation_lifecycle::read_generation_terminal(
        base_dir,
        &broadcast.capture_generation_id,
        broadcast.generation_started_at_ms,
    );
    if !matches!(terminal, Ok(Some(_))) {
        return false;
    }
    record_signal::read_signal(base_dir, project_hash, post_instance_id).is_some_and(|signal| {
        signal.status != SignalStatus::Released
            && signal.capture_generation_id == broadcast.capture_generation_id
            && signal.generation_started_at_ms == broadcast.generation_started_at_ms
    })
}
