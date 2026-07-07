use std::sync::atomic::{AtomicU64, Ordering};

/// Minimum real-time process callback gap that can indicate a transport stop
/// for hosts that stop calling `process()` while stopped.
pub const WATCH_PLAYBACK_PASS_MIN_GAP_SECS: f64 = 0.25;

/// Continuous playback should not reset just because the user chose a large
/// buffer size. Treat only gaps clearly larger than the current block duration
/// as a new playback pass.
pub const WATCH_PLAYBACK_PASS_BLOCK_MULTIPLIER: f64 = 2.0;
const WATCH_RING_CURSOR_COUNT_BITS: u32 = 48;
const WATCH_RING_CURSOR_COUNT_MASK: u64 = (1_u64 << WATCH_RING_CURSOR_COUNT_BITS) - 1;
const WATCH_RING_CURSOR_PASS_SHIFT: u32 = WATCH_RING_CURSOR_COUNT_BITS;
const WATCH_RING_CURSOR_PASS_MASK: u64 = !WATCH_RING_CURSOR_COUNT_MASK;

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
    loop {
        let current = counter.load(Ordering::Acquire);
        let next = next_watch_playback_pass_id(current);
        if counter
            .compare_exchange(current, next, Ordering::AcqRel, Ordering::Acquire)
            .is_ok()
        {
            return next;
        }
    }
}

#[inline]
pub fn next_watch_playback_pass_id(current: u64) -> u64 {
    let next = current.wrapping_add(1);
    if next == 0 {
        1
    } else {
        next
    }
}

#[inline]
pub fn watch_ring_cursor(pass_id: u64, pushed_samples: u64) -> u64 {
    (watch_ring_pass_token(pass_id) << WATCH_RING_CURSOR_PASS_SHIFT)
        | pushed_samples.min(WATCH_RING_CURSOR_COUNT_MASK)
}

#[inline]
pub fn watch_ring_cursor_samples_for_pass(cursor: u64, pass_id: u64) -> Option<u64> {
    if (cursor & WATCH_RING_CURSOR_PASS_MASK)
        == (watch_ring_pass_token(pass_id) << WATCH_RING_CURSOR_PASS_SHIFT)
    {
        Some(cursor & WATCH_RING_CURSOR_COUNT_MASK)
    } else {
        None
    }
}

#[inline]
pub fn add_watch_ring_cursor_samples(cursor: &AtomicU64, pass_id: u64, pushed_samples: u64) {
    if pushed_samples == 0 {
        return;
    }
    let token = watch_ring_pass_token(pass_id);
    let _ = cursor.fetch_update(Ordering::AcqRel, Ordering::Acquire, |current| {
        let current_token = current >> WATCH_RING_CURSOR_PASS_SHIFT;
        let current_samples = if current_token == token {
            current & WATCH_RING_CURSOR_COUNT_MASK
        } else {
            0
        };
        Some(
            (token << WATCH_RING_CURSOR_PASS_SHIFT)
                | current_samples
                    .saturating_add(pushed_samples)
                    .min(WATCH_RING_CURSOR_COUNT_MASK),
        )
    });
}

#[inline]
pub fn publish_watch_playback_pass_boundary(
    pass_id: &AtomicU64,
    ring_cursor: &AtomicU64,
    cutover_samples: &AtomicU64,
) -> u64 {
    let current_pass = pass_id.load(Ordering::Acquire);
    let current_cursor = ring_cursor.load(Ordering::Acquire);
    let cutover = watch_ring_cursor_samples_for_pass(current_cursor, current_pass).unwrap_or(0);
    let next_pass = next_watch_playback_pass_id(current_pass);
    cutover_samples.store(cutover, Ordering::Release);
    ring_cursor.store(watch_ring_cursor(next_pass, 0), Ordering::Release);
    pass_id.store(next_pass, Ordering::Release);
    next_pass
}

#[inline]
fn watch_ring_pass_token(pass_id: u64) -> u64 {
    pass_id & (u64::MAX >> WATCH_RING_CURSOR_COUNT_BITS)
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
        assert_eq!(counter.load(Ordering::Acquire), 1);
    }

    #[test]
    fn ring_cursor_separates_equal_sample_counts_by_pass_token() {
        let old_cursor = watch_ring_cursor(7, 512);
        assert_eq!(watch_ring_cursor_samples_for_pass(old_cursor, 7), Some(512));
        assert_eq!(watch_ring_cursor_samples_for_pass(old_cursor, 8), None);
    }

    #[test]
    fn ring_cursor_add_resets_count_when_pass_changes() {
        let cursor = AtomicU64::new(watch_ring_cursor(7, 512));
        add_watch_ring_cursor_samples(&cursor, 8, 128);
        let snapshot = cursor.load(Ordering::Acquire);
        assert_eq!(watch_ring_cursor_samples_for_pass(snapshot, 7), None);
        assert_eq!(watch_ring_cursor_samples_for_pass(snapshot, 8), Some(128));
    }

    #[test]
    fn publish_boundary_stores_cutover_before_new_pass() {
        let pass_id = AtomicU64::new(7);
        let cursor = AtomicU64::new(watch_ring_cursor(7, 512));
        let cutover = AtomicU64::new(0);

        assert_eq!(
            publish_watch_playback_pass_boundary(&pass_id, &cursor, &cutover),
            8
        );
        assert_eq!(cutover.load(Ordering::Acquire), 512);
        assert_eq!(pass_id.load(Ordering::Acquire), 8);
        assert_eq!(
            watch_ring_cursor_samples_for_pass(cursor.load(Ordering::Acquire), 8),
            Some(0)
        );
    }
}
