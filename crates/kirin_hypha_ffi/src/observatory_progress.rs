//! Whether the Observatory is actually advancing: LIVE only while the meter advances, and a
//! matched pair whose LEVEL Δ waits is either still measuring or stalled until the user acts.
use super::*;

/// Audio both sides of a matched pair may measure without a common window before the wait needs
/// action.
const JOIN_STALL_AFTER_SECONDS: u64 = 3;

/// How far into one wait for a matched LEVEL window each side has measured. UI poll thread only.
#[derive(Debug, Default)]
pub(crate) struct ComparisonWait {
    since: Option<WaitStart>,
}

#[derive(Debug, Clone, Copy)]
struct WaitStart {
    key: WaitKey,
    post: u64,
    /// PRE's published meter progress when this wait first read it.
    pre: Option<u64>,
}

/// One comparison binding on one meter run; a new one restarts the wait.
type WaitKey = (u64, u64, u64);

impl ComparisonWait {
    /// `post_frames` advances only while the POST measures and `pre_frames` only while the PRE
    /// publishes newly measured audio (None when this poll could not read it). A frozen POST, or a
    /// PRE that is still catching up on a slow host, is a wait that resolves by itself; only a pair
    /// whose both sides measured the stall bound without a common window needs the user.
    pub(crate) fn observe(
        &mut self,
        waiting: bool,
        key: WaitKey,
        post_frames: u64,
        pre_frames: Option<u64>,
        sample_rate: u32,
    ) -> bool {
        if !waiting {
            self.since = None;
            return false;
        }
        let start = match self.since {
            Some(start) if start.key == key && post_frames >= start.post => start,
            _ => WaitStart {
                key,
                post: post_frames,
                pre: None,
            },
        };
        // PRE's count starts at its first reading in this wait and again after its meter restarts;
        // a busy read neither starts nor clears it.
        let pre = match (start.pre, pre_frames) {
            (Some(first), Some(now)) if now < first => Some(now),
            (Some(first), _) => Some(first),
            (None, now) => now,
        };
        self.since = Some(WaitStart { pre, ..start });
        let bound = JOIN_STALL_AFTER_SECONDS * u64::from(sample_rate.max(1));
        post_frames - start.post >= bound
            && pre_frames
                .zip(pre)
                .is_some_and(|(now, first)| now - first >= bound)
    }
}

/// A matched pair whose both sides keep measuring without a common window is a stall that needs
/// the user, not the progressing "MEASURING" wait. The reason changes; the comparison state does
/// not.
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
    let pre_frames = engine
        .meter_delta_history
        .as_ref()
        .and_then(|exchange| exchange.comparison_pre_observed());
    if wait.observe(
        waiting,
        key,
        snapshot.observed_frames,
        pre_frames,
        snapshot.sample_rate,
    ) {
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

    const RATE: u32 = 48_000;
    const KEY: (u64, u64, u64) = (7, 1, 1);

    #[test]
    fn a_matched_wait_needs_action_only_after_three_seconds_of_measured_audio_on_both_sides() {
        let mut wait = ComparisonWait::default();
        assert!(!wait.observe(true, KEY, 48_000, Some(10_000), RATE));
        assert!(
            !wait.observe(true, KEY, 48_000 + 143_999, Some(10_000 + 143_999), RATE),
            "still measuring"
        );
        assert!(
            wait.observe(true, KEY, 48_000 + 144_000, Some(10_000 + 144_000), RATE),
            "both sides measured 3 s without a window"
        );
        // A frozen POST adds no frames, so it never reaches the bound by waiting alone.
        let mut frozen = ComparisonWait::default();
        for pre in 0..1_000 {
            assert!(!frozen.observe(true, KEY, 10, Some(pre * 480), RATE));
        }
        // A matched value, a new binding or a new meter run restarts the wait.
        assert!(!wait.observe(false, KEY, 300_000, Some(300_000), RATE));
        assert!(!wait.observe(true, KEY, 300_000, Some(300_000), RATE));
        assert!(!wait.observe(true, (8, 1, 1), 444_000, Some(444_000), RATE));
        assert!(!wait.observe(true, (8, 2, 1), 600_000, Some(600_000), RATE));
    }

    #[test]
    fn a_lagging_pre_is_a_progressing_wait_not_a_stall() {
        let mut wait = ComparisonWait::default();
        assert!(!wait.observe(true, KEY, 0, Some(0), RATE));
        // POST measured 3 s; a slow host left PRE 2 s behind: waiting resolves it.
        assert!(!wait.observe(true, KEY, 144_000, Some(48_000), RATE));
        assert!(!wait.observe(true, KEY, 200_000, Some(143_999), RATE));
        // PRE measured its 3 s as well and still no common window: the user must act.
        assert!(wait.observe(true, KEY, 240_000, Some(144_000), RATE));
    }

    #[test]
    fn an_unread_or_restarted_pre_never_proves_its_progress() {
        let mut busy = ComparisonWait::default();
        assert!(!busy.observe(true, KEY, 0, None, RATE));
        // A busy read neither starts nor clears PRE's count.
        assert!(!busy.observe(true, KEY, 144_000, None, RATE));
        assert!(!busy.observe(true, KEY, 150_000, Some(5_000), RATE));
        assert!(!busy.observe(true, KEY, 160_000, None, RATE));
        assert!(busy.observe(true, KEY, 170_000, Some(149_000), RATE));
        // A PRE meter that restarted counts again from its new reading.
        let mut restarted = ComparisonWait::default();
        assert!(!restarted.observe(true, KEY, 0, Some(900_000), RATE));
        assert!(!restarted.observe(true, KEY, 150_000, Some(1_000), RATE));
        assert!(!restarted.observe(true, KEY, 160_000, Some(144_999), RATE));
        assert!(restarted.observe(true, KEY, 170_000, Some(145_000), RATE));
    }
}
