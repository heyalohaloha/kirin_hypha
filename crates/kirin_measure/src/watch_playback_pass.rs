use std::hint::spin_loop;
use std::sync::atomic::{AtomicU64, Ordering};

/// Minimum real-time process callback gap that can indicate a transport stop
/// for hosts that stop calling `process()` while stopped.
pub const WATCH_PLAYBACK_PASS_MIN_GAP_SECS: f64 = 0.25;

/// Continuous playback should not reset just because the user chose a large
/// buffer size. Treat only gaps clearly larger than the current block duration
/// as a new playback pass.
pub const WATCH_PLAYBACK_PASS_BLOCK_MULTIPLIER: f64 = 2.0;
const WATCH_RING_CURSOR_READ_ATTEMPTS: usize = 8;

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct WatchRingCursorSnapshot {
    pub pass_id: u64,
    pub pushed_samples: u64,
}

#[inline]
pub fn reset_watch_ring_cursor(
    cursor_epoch: &AtomicU64,
    cursor_pass_id: &AtomicU64,
    cursor_samples: &AtomicU64,
    pass_id: u64,
) {
    write_watch_ring_cursor(cursor_epoch, cursor_pass_id, cursor_samples, pass_id, 0);
}

#[inline]
pub fn add_watch_ring_cursor_samples(
    cursor_epoch: &AtomicU64,
    cursor_pass_id: &AtomicU64,
    cursor_samples: &AtomicU64,
    pass_id: u64,
    pushed_samples: u64,
) {
    if pushed_samples == 0 {
        return;
    }
    begin_watch_ring_cursor_write(cursor_epoch);
    let current_pass_id = cursor_pass_id.load(Ordering::Relaxed);
    let current_samples = if current_pass_id == pass_id {
        cursor_samples.load(Ordering::Relaxed)
    } else {
        0
    };
    cursor_pass_id.store(pass_id, Ordering::Relaxed);
    cursor_samples.store(
        current_samples.saturating_add(pushed_samples),
        Ordering::Relaxed,
    );
    end_watch_ring_cursor_write(cursor_epoch);
}

#[inline]
pub fn watch_ring_cursor_samples_for_pass(
    cursor_epoch: &AtomicU64,
    cursor_pass_id: &AtomicU64,
    cursor_samples: &AtomicU64,
    pass_id: u64,
) -> Option<u64> {
    let snapshot = read_watch_ring_cursor(cursor_epoch, cursor_pass_id, cursor_samples)?;
    (snapshot.pass_id == pass_id).then_some(snapshot.pushed_samples)
}

#[inline]
pub fn publish_watch_playback_pass_boundary(
    pass_id: &AtomicU64,
    cursor_epoch: &AtomicU64,
    cursor_pass_id: &AtomicU64,
    cursor_samples: &AtomicU64,
    cutover_samples: &AtomicU64,
) -> u64 {
    let current_pass = pass_id.load(Ordering::Acquire);
    let cutover = watch_ring_cursor_samples_for_pass(
        cursor_epoch,
        cursor_pass_id,
        cursor_samples,
        current_pass,
    )
    .unwrap_or(0);
    let next_pass = next_watch_playback_pass_id(current_pass);
    cutover_samples.store(cutover, Ordering::Release);
    reset_watch_ring_cursor(cursor_epoch, cursor_pass_id, cursor_samples, next_pass);
    pass_id.store(next_pass, Ordering::Release);
    next_pass
}

#[inline]
pub fn read_watch_ring_cursor(
    cursor_epoch: &AtomicU64,
    cursor_pass_id: &AtomicU64,
    cursor_samples: &AtomicU64,
) -> Option<WatchRingCursorSnapshot> {
    for _ in 0..WATCH_RING_CURSOR_READ_ATTEMPTS {
        let start_epoch = cursor_epoch.load(Ordering::Acquire);
        if start_epoch & 1 == 1 {
            spin_loop();
            continue;
        }
        let pass_id = cursor_pass_id.load(Ordering::Acquire);
        let pushed_samples = cursor_samples.load(Ordering::Acquire);
        let end_epoch = cursor_epoch.load(Ordering::Acquire);
        if start_epoch == end_epoch && end_epoch & 1 == 0 {
            return Some(WatchRingCursorSnapshot {
                pass_id,
                pushed_samples,
            });
        }
        spin_loop();
    }
    None
}

#[inline]
fn write_watch_ring_cursor(
    cursor_epoch: &AtomicU64,
    cursor_pass_id: &AtomicU64,
    cursor_samples: &AtomicU64,
    pass_id: u64,
    pushed_samples: u64,
) {
    begin_watch_ring_cursor_write(cursor_epoch);
    cursor_pass_id.store(pass_id, Ordering::Relaxed);
    cursor_samples.store(pushed_samples, Ordering::Relaxed);
    end_watch_ring_cursor_write(cursor_epoch);
}

#[inline]
fn begin_watch_ring_cursor_write(cursor_epoch: &AtomicU64) {
    cursor_epoch.fetch_add(1, Ordering::AcqRel);
}

#[inline]
fn end_watch_ring_cursor_write(cursor_epoch: &AtomicU64) {
    cursor_epoch.fetch_add(1, Ordering::Release);
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
    fn ring_cursor_separates_equal_sample_counts_by_full_pass_id() {
        let epoch = AtomicU64::new(0);
        let pass_id = AtomicU64::new(65_543);
        let samples = AtomicU64::new(512);
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass_id, &samples, 65_543),
            Some(512)
        );
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass_id, &samples, 7),
            None
        );
    }

    #[test]
    fn ring_cursor_add_resets_count_when_pass_changes() {
        let epoch = AtomicU64::new(0);
        let pass_id = AtomicU64::new(7);
        let samples = AtomicU64::new(512);
        add_watch_ring_cursor_samples(&epoch, &pass_id, &samples, 8, 128);
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass_id, &samples, 7),
            None
        );
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass_id, &samples, 8),
            Some(128)
        );
    }

    #[test]
    fn publish_boundary_stores_cutover_before_new_pass() {
        let pass_id = AtomicU64::new(7);
        let cursor_epoch = AtomicU64::new(0);
        let cursor_pass_id = AtomicU64::new(7);
        let cursor_samples = AtomicU64::new(512);
        let cutover = AtomicU64::new(0);

        assert_eq!(
            publish_watch_playback_pass_boundary(
                &pass_id,
                &cursor_epoch,
                &cursor_pass_id,
                &cursor_samples,
                &cutover
            ),
            8
        );
        assert_eq!(cutover.load(Ordering::Acquire), 512);
        assert_eq!(pass_id.load(Ordering::Acquire), 8);
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&cursor_epoch, &cursor_pass_id, &cursor_samples, 8),
            Some(0)
        );
    }

    #[test]
    fn ring_cursor_does_not_alias_after_sixteen_bit_pass_wrap() {
        let epoch = AtomicU64::new(0);
        let pass_id = AtomicU64::new(65_543);
        let samples = AtomicU64::new(256);
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass_id, &samples, 7),
            None
        );
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass_id, &samples, 65_543),
            Some(256)
        );
    }
}
