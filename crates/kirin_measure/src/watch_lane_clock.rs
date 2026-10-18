//! The Watch lane's capture-frame index.
//!
//! Every frame the Audio Thread captures, on the Watch or the Record lane, receives the next
//! global capture frame. The Measure Thread names the samples it pops through that index (the
//! TIME endpoints that PRE and POST join on), so the index must follow the audio exactly:
//! a discarded Watch sample still advances it, and Watch resumes after a Keep at the frame after
//! the Keep's Record frames. A drifted index silently labels windows with other instants, so the
//! two sides join different windows (a swinging ΔTP/ΔCREST) or never join (Δ stays MEASURING).

use crate::record_spool::MeasureSampleConsumer;

/// Pops queued Watch samples without measuring them (stop, bypass, a playback-pass cutover).
/// `until` stops at that per-pass sample count; `None` drains what is queued. Only whole frames
/// leave: the Audio Thread pushes one sample at a time, so the queue may end inside a frame, and
/// splitting it would shift the index and swap the channels of every later frame. A pending skip
/// past the last Keep applies to the discarded frames too.
pub(super) fn discard_watch_samples(
    consumer: &mut MeasureSampleConsumer,
    consumed_samples: &mut u64,
    native_frames_total: &mut u64,
    n_channels: usize,
    until: Option<u64>,
    resume: &mut Option<WatchResume>,
) -> usize {
    let queued = consumer.slots();
    let limit = until.map_or(queued, |cutover| {
        cutover.saturating_sub(*consumed_samples).min(queued as u64) as usize
    });
    let limit = limit - limit % n_channels.max(1);
    let mut drained = 0_usize;
    while drained < limit && consumer.pop().is_ok() {
        *consumed_samples = (*consumed_samples).saturating_add(1);
        drained += 1;
    }
    *native_frames_total =
        (*native_frames_total).saturating_add((drained / n_channels.max(1)) as u64);
    resume_watch_index(resume, native_frames_total);
    drained
}

/// Watch frames queued before a Keep end at the Keep's capture origin; the next Watch frame is
/// the first one after the Keep's Record frames.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct WatchResume {
    pub(super) record_origin: u64,
    pub(super) record_end: u64,
}

impl WatchResume {
    /// The Keep's Record lane began at `record_origin` and took `record_frames` capture frames.
    pub(super) fn after_keep(record_origin: u64, record_frames: u64) -> Self {
        Self {
            record_origin,
            record_end: record_origin.saturating_add(record_frames),
        }
    }

    /// Applies the skip once the Watch index reaches the Keep. Returns false when done.
    pub(super) fn apply(self, native_frames_total: &mut u64) -> bool {
        if *native_frames_total < self.record_origin {
            return true;
        }
        *native_frames_total = self
            .record_end
            .saturating_add(*native_frames_total - self.record_origin);
        false
    }
}

/// The skip past a Keep whose Record lane began at `origin` and popped `consumed_samples` with
/// `remaining_samples` still queued (read before a failed lane is dropped). A skip beyond the
/// frames captured so far belongs to a capture clock that was reset (a Measure restart) and is
/// discarded rather than applied to the new coordinates.
pub(super) fn after_keep_resume(
    origin: Option<u64>,
    consumed_samples: u64,
    remaining_samples: usize,
    n_channels: usize,
    captured_frames_total: u64,
) -> Option<WatchResume> {
    let record_frames =
        consumed_samples.saturating_add(remaining_samples as u64) / n_channels.max(1) as u64;
    origin
        .map(|origin| WatchResume::after_keep(origin, record_frames))
        .filter(|resume| resume.record_end <= captured_frames_total)
}

/// Samples one Watch chunk may take: never across a pending Keep's origin, where the index jumps.
pub(super) fn watch_chunk_limit(
    resume: &Option<WatchResume>,
    native_frames_total: u64,
    n_channels: usize,
) -> usize {
    resume
        .filter(|resume| native_frames_total < resume.record_origin)
        .map_or(usize::MAX, |resume| {
            usize::try_from(resume.record_origin - native_frames_total)
                .unwrap_or(usize::MAX)
                .saturating_mul(n_channels.max(1))
        })
}

/// Every place that advances the Watch index (measuring, discarding while stopped or bypassed, a
/// playback-pass cutover, the raw pre-roll drain before the next Keep) applies a pending skip, so
/// a Keep that ends while the transport is stopped never leaves the index behind.
pub(super) fn resume_watch_index(resume: &mut Option<WatchResume>, native_frames_total: &mut u64) {
    if let Some(pending) = *resume {
        if !pending.apply(native_frames_total) {
            *resume = None;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn queued(samples: &[f32]) -> MeasureSampleConsumer {
        let (mut producer, consumer) = rtrb::RingBuffer::new(16);
        for &sample in samples {
            producer.push(sample).unwrap();
        }
        MeasureSampleConsumer::watch(consumer)
    }

    #[test]
    fn a_discarded_watch_sample_still_advances_the_capture_index() {
        // Two stereo frames queued at a stop, then a cutover that keeps the next pass's frame.
        let mut consumer = queued(&[1.0, 1.0, 2.0, 2.0, 9.0, 9.0]);
        let (mut consumed, mut native) = (0_u64, 480_u64);
        assert_eq!(
            discard_watch_samples(
                &mut consumer,
                &mut consumed,
                &mut native,
                2,
                Some(4),
                &mut None
            ),
            4
        );
        assert_eq!((consumed, native), (4, 482));
        assert_eq!(consumer.pop().unwrap(), 9.0);
        let mut consumer = queued(&[1.0, 1.0, 2.0, 2.0]);
        assert_eq!(
            discard_watch_samples(
                &mut consumer,
                &mut consumed,
                &mut native,
                2,
                None,
                &mut None
            ),
            4
        );
        assert_eq!((consumed, native), (8, 484));
    }

    #[test]
    fn a_discard_never_splits_a_frame_the_audio_thread_is_still_pushing() {
        // The Audio Thread pushes one sample at a time: a stop can see 1.5 stereo frames queued.
        let (mut producer, consumer) = rtrb::RingBuffer::new(16);
        let mut consumer = MeasureSampleConsumer::watch(consumer);
        for sample in [1.0, 1.0, 2.0] {
            producer.push(sample).unwrap();
        }
        let (mut consumed, mut native) = (0_u64, 100_u64);
        let drained = discard_watch_samples(
            &mut consumer,
            &mut consumed,
            &mut native,
            2,
            None,
            &mut None,
        );
        assert_eq!((drained, consumed, native), (2, 2, 101));
        // The rest of that frame arrives: the next discard starts at its left sample.
        producer.push(2.5).unwrap();
        // A cutover beyond what is queued stops at the last whole frame as well.
        producer.push(3.0).unwrap();
        let drained = discard_watch_samples(
            &mut consumer,
            &mut consumed,
            &mut native,
            2,
            Some(10),
            &mut None,
        );
        assert_eq!((drained, consumed, native), (2, 4, 102));
        assert_eq!(consumer.pop().unwrap(), 3.0);
    }

    #[test]
    fn a_keep_that_ends_while_stopped_still_skips_its_frames() {
        // Watch frames queued before the Keep are discarded while stopped; the skip applies there.
        let mut consumer = queued(&[1.0, 1.0, 2.0, 2.0]);
        let (mut consumed, mut native) = (0_u64, 998_u64);
        let mut resume = Some(WatchResume::after_keep(1_000, 8_000));
        discard_watch_samples(
            &mut consumer,
            &mut consumed,
            &mut native,
            2,
            None,
            &mut resume,
        );
        assert_eq!((native, resume), (9_000, None));
        // A Keep whose Record lane never took a frame leaves the index where it is.
        let mut none = Some(WatchResume::after_keep(9_000, 0));
        resume_watch_index(&mut none, &mut native);
        assert_eq!((native, none), (9_000, None));
    }

    #[test]
    fn the_skip_is_read_before_the_lane_retires_and_never_crosses_a_reset() {
        // 9 popped + 3 queued stereo frames from origin 1_000; 2_000 frames captured so far.
        let resume = after_keep_resume(Some(1_000), 18, 6, 2, 2_000).unwrap();
        assert_eq!((resume.record_origin, resume.record_end), (1_000, 1_012));
        assert_eq!(
            after_keep_resume(None, 18, 6, 2, 2_000),
            None,
            "no Record audio"
        );
        assert_eq!(
            after_keep_resume(Some(1_000), 18, 6, 2, 40),
            None,
            "reset clock"
        );
        // A chunk stops at the origin; past it, or without a skip, it is unbounded.
        assert_eq!(watch_chunk_limit(&Some(resume), 990, 2), 20);
        assert_eq!(watch_chunk_limit(&Some(resume), 1_000, 2), usize::MAX);
        assert_eq!(watch_chunk_limit(&None, 0, 2), usize::MAX);
    }

    #[test]
    fn watch_resumes_after_the_keep_frames() {
        let resume = WatchResume {
            record_origin: 1_000,
            record_end: 9_000,
        };
        // Frames queued before the Keep keep their own index; the next frame follows the Keep.
        let mut native = 990;
        assert!(resume.apply(&mut native));
        assert_eq!(native, 990);
        native = 1_000;
        assert!(!resume.apply(&mut native));
        assert_eq!(native, 9_000);
    }
}
