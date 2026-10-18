//! Whether the Observatory is actually advancing: LIVE only while the meter advances, and a
//! matched pair whose LEVEL Δ waits is either still measuring or stalled until the user acts.
use super::*;

/// POST audio a matched pair may measure without a common window before the wait needs action.
const JOIN_STALL_AFTER_SECONDS: u64 = 3;

/// How far into one wait for a matched LEVEL window the POST has measured. UI poll thread only.
#[derive(Debug, Default)]
pub(crate) struct ComparisonWait {
    since: Option<(WaitKey, u64)>,
}

/// One comparison binding on one meter run; a new one restarts the wait.
type WaitKey = (u64, u64, u64);

impl ComparisonWait {
    /// `observed_frames` advances only while the POST measures, so a frozen POST never escalates
    /// here (that is a stalled meter). True once the wait spans the stall bound of POST audio.
    pub(crate) fn observe(
        &mut self,
        waiting: bool,
        key: WaitKey,
        observed_frames: u64,
        sample_rate: u32,
    ) -> bool {
        if !waiting {
            self.since = None;
            return false;
        }
        let start = match self.since {
            Some((current, start)) if current == key && observed_frames >= start => start,
            _ => {
                self.since = Some((key, observed_frames));
                observed_frames
            }
        };
        observed_frames - start >= JOIN_STALL_AFTER_SECONDS * u64::from(sample_rate.max(1))
    }
}

/// A matched pair that keeps measuring without a common window is a stall that needs the user,
/// not the progressing "MEASURING" wait. The reason changes; the comparison state does not.
pub(crate) fn qualify_wait(
    engine: &KirinHyphaEngine,
    comparison: &mut comparison_abi::ComparisonProjection,
    active: bool,
    snapshot: &MeterSessionSnapshot,
) {
    let waiting = active && comparison.reason == KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT;
    let key = (
        comparison.identity,
        snapshot.generation,
        snapshot.measurement_epoch,
    );
    let Ok(mut wait) = engine.comparison_wait.try_lock() else {
        return; // Another poll owns the wait; this frame keeps the plain reason.
    };
    if wait.observe(waiting, key, snapshot.observed_frames, snapshot.sample_rate) {
        comparison.reason = KIRIN_COMPARISON_REASON_JOIN_STALLED;
        comparison.generation = comparison.generation.saturating_add(1);
    }
}

/// An ACTIVE meter session that has stopped advancing while the input is ACTIVE.
pub(crate) fn stalled_meter(
    engine: &KirinHyphaEngine,
    snapshot: &MeterSessionSnapshot,
    signal: u8,
) -> bool {
    signal == KIRIN_SIGNAL_STATE_ACTIVE
        && snapshot.state == MeterSessionState::Active
        && engine
            .meter_session_publication
            .as_ref()
            .is_some_and(|publication| publication.stalled_at(std::time::Instant::now()))
}

#[cfg(test)]
mod tests {
    use super::ComparisonWait;

    #[test]
    fn a_matched_wait_needs_action_only_after_three_seconds_of_measured_post_audio() {
        let mut wait = ComparisonWait::default();
        let key = (7, 1, 1);
        assert!(!wait.observe(true, key, 48_000, 48_000));
        assert!(
            !wait.observe(true, key, 48_000 + 143_999, 48_000),
            "still measuring"
        );
        assert!(
            wait.observe(true, key, 48_000 + 144_000, 48_000),
            "3 s without a window"
        );
        // A frozen POST adds no frames, so it never reaches the bound by waiting alone.
        let mut frozen = ComparisonWait::default();
        for _ in 0..1_000 {
            assert!(!frozen.observe(true, key, 10, 48_000));
        }
        // A matched value, a new binding or a new meter run restarts the wait.
        assert!(!wait.observe(false, key, 300_000, 48_000));
        assert!(!wait.observe(true, key, 300_000, 48_000));
        assert!(!wait.observe(true, (8, 1, 1), 444_000, 48_000));
        assert!(!wait.observe(true, (8, 2, 1), 600_000, 48_000));
    }
}
