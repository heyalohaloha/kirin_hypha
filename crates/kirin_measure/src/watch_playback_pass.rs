use std::sync::atomic::{AtomicU64, Ordering};

/// Minimum real-time process callback gap that can indicate a transport stop
/// for hosts that stop calling `process()` while stopped.
pub const WATCH_PLAYBACK_PASS_MIN_GAP_SECS: f64 = 0.25;

/// Continuous playback should not reset just because the user chose a large
/// buffer size. Treat only gaps clearly larger than the current block duration
/// as a new playback pass.
pub const WATCH_PLAYBACK_PASS_BLOCK_MULTIPLIER: f64 = 2.0;

#[inline]
pub fn watch_playback_block_duration_secs(frames: usize, sample_rate_hz: f64) -> f64 {
    if frames == 0 || !sample_rate_hz.is_finite() || sample_rate_hz <= 0.0 {
        return 0.0;
    }
    frames as f64 / sample_rate_hz
}

#[inline]
pub fn watch_playback_pass_should_start(
    playing: bool,
    previous_playing: bool,
    callback_gap_secs: Option<f64>,
    block_duration_secs: f64,
) -> bool {
    if !playing {
        return false;
    }
    if !previous_playing {
        return true;
    }
    let Some(gap) = callback_gap_secs.filter(|gap| gap.is_finite() && *gap > 0.0) else {
        return false;
    };
    let block_gap = if block_duration_secs.is_finite() && block_duration_secs > 0.0 {
        block_duration_secs * WATCH_PLAYBACK_PASS_BLOCK_MULTIPLIER
    } else {
        0.0
    };
    let threshold = WATCH_PLAYBACK_PASS_MIN_GAP_SECS.max(block_gap);
    gap > threshold
}

#[inline]
pub fn advance_watch_playback_pass_id(counter: &AtomicU64) -> u64 {
    let next = counter.fetch_add(1, Ordering::Relaxed).wrapping_add(1);
    if next == 0 {
        counter.store(1, Ordering::Relaxed);
        1
    } else {
        next
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn starts_on_playing_edge() {
        assert!(watch_playback_pass_should_start(
            true,
            false,
            Some(0.01),
            0.01
        ));
        assert!(!watch_playback_pass_should_start(
            false,
            true,
            Some(1.0),
            0.01
        ));
    }

    #[test]
    fn large_buffer_continuous_playback_does_not_false_reset() {
        let block_secs = watch_playback_block_duration_secs(16_384, 44_100.0);
        assert!(block_secs > 0.37);
        assert!(!watch_playback_pass_should_start(
            true,
            true,
            Some(block_secs * 1.5),
            block_secs,
        ));
    }

    #[test]
    fn process_stopped_gap_starts_new_pass_even_if_playing_was_frozen() {
        let block_secs = watch_playback_block_duration_secs(512, 48_000.0);
        assert!(watch_playback_pass_should_start(
            true,
            true,
            Some(0.35),
            block_secs,
        ));
    }

    #[test]
    fn pass_id_never_returns_zero_on_wrap() {
        let counter = AtomicU64::new(u64::MAX);
        assert_eq!(advance_watch_playback_pass_id(&counter), 1);
        assert_eq!(counter.load(Ordering::Relaxed), 1);
    }
}
