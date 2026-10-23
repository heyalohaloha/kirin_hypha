//! Read limits in the Watch ring's lifetime sample coordinates.
use crate::watch_playback_pass::watch_ring_cursor_samples_for_pass;
use std::sync::atomic::AtomicU64;

pub(super) fn samples_until_cursor_limit(consumed_samples: u64, cursor_samples: u64) -> usize {
    cursor_samples
        .saturating_sub(consumed_samples)
        .min(usize::MAX as u64) as usize
}

pub(super) fn available_watch_samples(
    epoch: &AtomicU64,
    cursor_pass: &AtomicU64,
    cursor_samples: &AtomicU64,
    playback_pass: u64,
    consumed_samples: u64,
    queued_samples: usize,
) -> usize {
    // A contended publication or a different pass is not permission to read its PCM.
    let pushed =
        watch_ring_cursor_samples_for_pass(epoch, cursor_pass, cursor_samples, playback_pass)
            .unwrap_or(consumed_samples);
    queued_samples.min(samples_until_cursor_limit(consumed_samples, pushed))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_uncommitted_or_different_pass_never_authorizes_queued_pcm() {
        let epoch = AtomicU64::new(1);
        let pass = AtomicU64::new(9);
        let samples = AtomicU64::new(64);
        assert_eq!(
            available_watch_samples(&epoch, &pass, &samples, 9, 32, 32),
            0
        );
        epoch.store(2, std::sync::atomic::Ordering::Release);
        assert_eq!(
            available_watch_samples(&epoch, &pass, &samples, 8, 32, 32),
            0
        );
        assert_eq!(
            available_watch_samples(&epoch, &pass, &samples, 9, 32, 32),
            32
        );
        assert_eq!(
            available_watch_samples(&epoch, &pass, &samples, 9, 64, 32),
            0
        );
        assert_eq!(
            available_watch_samples(&epoch, &pass, &samples, 9, 80, 32),
            0
        );
    }
}

#[cfg(test)]
mod cursor_progress_tests {
    use super::*;
    use crate::watch_playback_pass::*;
    use std::sync::atomic::Ordering;

    #[test]
    fn parked_watch_cursor_remains_valid_after_record_loops() {
        let pass = AtomicU64::new(7);
        let epoch = AtomicU64::new(0);
        let cursor_pass = AtomicU64::new(7);
        let pushed = AtomicU64::new(0);
        let cutover = AtomicU64::new(0);
        let (mut producer, mut consumer) = rtrb::RingBuffer::new(32);
        for _ in 0..32 {
            producer.push(0.25_f32).unwrap();
        }
        add_watch_ring_cursor_samples(&epoch, &cursor_pass, &pushed, 7, 32);
        let mut consumed = 0;
        while consumer.pop().is_ok() {
            consumed += 1;
        }
        // Keep parks the empty Watch lane and its consumed count. Its Record lane has
        // separate PCM; several transport wraps still advance the shared playback pass.
        for _ in 0..3 {
            publish_watch_playback_pass_boundary(&pass, &epoch, &cursor_pass, &pushed, &cutover);
        }
        for _ in 0..8 {
            producer.push(0.5).unwrap();
        }
        let current_pass = pass.load(Ordering::Acquire);
        add_watch_ring_cursor_samples(&epoch, &cursor_pass, &pushed, current_pass, 8);
        let available = super::available_watch_samples(
            &epoch,
            &cursor_pass,
            &pushed,
            current_pass,
            consumed,
            consumer.slots(),
        );
        assert_eq!(available, 8, "all resumed Watch PCM is readable");
        for _ in 0..available {
            assert_eq!(consumer.pop().unwrap(), 0.5);
            consumed += 1;
        }
        assert_eq!(consumer.slots(), 0);
        assert_eq!(consumed, pushed.load(Ordering::Acquire));
    }

    #[test]
    fn skipped_pass_boundaries_keep_the_entire_queued_tail_addressable() {
        let pass = AtomicU64::new(7);
        let epoch = AtomicU64::new(0);
        let cursor_pass = AtomicU64::new(7);
        let pushed = AtomicU64::new(32);
        let cutover = AtomicU64::new(0);
        for _ in 0..2 {
            let next = publish_watch_playback_pass_boundary(
                &pass,
                &epoch,
                &cursor_pass,
                &pushed,
                &cutover,
            );
            add_watch_ring_cursor_samples(&epoch, &cursor_pass, &pushed, next, 8);
        }
        // A delayed consumer already popped 16 samples of pass 7. The newest boundary
        // follows the rest of pass 7 and all of pass 8, without resetting its coordinates.
        assert_eq!(cutover.load(Ordering::Acquire).saturating_sub(16), 24);
        assert_eq!(pushed.load(Ordering::Acquire).saturating_sub(16), 32);
    }

    #[test]
    fn replacing_the_ring_retires_its_old_sample_coordinates() {
        let epoch = AtomicU64::new(0);
        let pass = AtomicU64::new(7);
        let samples = AtomicU64::new(512);
        reset_watch_ring_cursor(&epoch, &pass, &samples, 9);
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass, &samples, 7),
            None
        );
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass, &samples, 9),
            Some(0)
        );
        add_watch_ring_cursor_samples(&epoch, &pass, &samples, 9, 8);
        assert_eq!(
            watch_ring_cursor_samples_for_pass(&epoch, &pass, &samples, 9),
            Some(8)
        );
    }
}
