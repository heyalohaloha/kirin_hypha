//! Record take duration tracker.
//!
//! Audio Thread owns the writes through atomics only. IO Thread reads a snapshot
//! when Record closes and uses it as the clean bounce take duration. The tracker
//! deliberately measures the WAV/native clock span when the host exposes one,
//! and keeps the raw render span as a lower-trust fallback.

use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, Ordering};
use std::sync::Arc;

pub const RECORD_TAKE_SOURCE_RENDER_CLOCK: &str = "render_clock_native";
pub const RECORD_TAKE_SOURCE_WAV_CLOCK: &str = "wav_clock_native";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecordTakeBlock {
    pub generation: u64,
    pub recording: bool,
    pub rendered: bool,
    pub playing: bool,
    pub offline: bool,
    pub position_valid: bool,
    pub position_samples: i64,
    pub num_frames: u64,
    pub clock_start_samples: i64,
    pub clock_end_samples: Option<i64>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecordTakeSnapshot {
    pub generation: u64,
    pub duration_samples: u64,
    pub source: &'static str,
}

#[derive(Debug)]
pub struct RecordTakeTracker {
    render_active: AtomicBool,
    render_epoch: AtomicU64,
    render_frames: AtomicU64,
    render_start_valid: AtomicBool,
    render_start_position: AtomicI64,
    render_last_end_valid: AtomicBool,
    render_last_end_position: AtomicI64,
    record_generation: AtomicU64,
    record_render_epoch: AtomicU64,
    record_bounded_duration_samples: AtomicU64,
    record_unbounded_duration_samples: AtomicU64,
    previous_generation: AtomicU64,
    previous_bounded_duration_samples: AtomicU64,
    previous_unbounded_duration_samples: AtomicU64,
}

impl Default for RecordTakeTracker {
    fn default() -> Self {
        Self::new()
    }
}

impl RecordTakeTracker {
    pub fn new() -> Self {
        Self {
            render_active: AtomicBool::new(false),
            render_epoch: AtomicU64::new(0),
            render_frames: AtomicU64::new(0),
            render_start_valid: AtomicBool::new(false),
            render_start_position: AtomicI64::new(i64::MIN),
            render_last_end_valid: AtomicBool::new(false),
            render_last_end_position: AtomicI64::new(i64::MIN),
            record_generation: AtomicU64::new(0),
            record_render_epoch: AtomicU64::new(0),
            record_bounded_duration_samples: AtomicU64::new(0),
            record_unbounded_duration_samples: AtomicU64::new(0),
            previous_generation: AtomicU64::new(0),
            previous_bounded_duration_samples: AtomicU64::new(0),
            previous_unbounded_duration_samples: AtomicU64::new(0),
        }
    }

    /// Audio-thread note. This is atomics only: no allocation, lock, filesystem,
    /// logging, or blocking call.
    pub fn note_block(&self, block: RecordTakeBlock) {
        if block.recording && block.generation > 0 {
            self.ensure_record_generation(block.generation);
        }

        let render_eligible = block.rendered
            && block.num_frames > 0
            && block.recording
            && block.position_valid
            && (block.offline || block.playing);

        if !render_eligible {
            self.render_active.store(false, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
            return;
        }

        let reset_epoch = !self.render_active.load(Ordering::Acquire)
            || self.position_discontinuity(block.position_valid, block.position_samples);
        let epoch = if reset_epoch {
            self.render_frames.store(0, Ordering::Release);
            self.render_start_position
                .store(block.position_samples, Ordering::Release);
            self.render_start_valid.store(true, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
            let next = self.render_epoch.fetch_add(1, Ordering::AcqRel) + 1;
            self.render_active.store(true, Ordering::Release);
            next
        } else {
            self.render_epoch.load(Ordering::Acquire)
        };

        self.render_frames
            .fetch_add(block.num_frames, Ordering::AcqRel);

        self.render_last_end_position.store(
            block
                .position_samples
                .saturating_add(block.num_frames as i64),
            Ordering::Release,
        );
        self.render_last_end_valid.store(true, Ordering::Release);

        if block.recording && block.generation > 0 {
            let current_epoch = self.record_render_epoch.load(Ordering::Acquire);
            if current_epoch != epoch {
                self.record_render_epoch.store(epoch, Ordering::Release);
            }
            if let Some(duration) = bounded_duration_from_block(&block) {
                self.record_bounded_duration_samples
                    .fetch_max(duration, Ordering::AcqRel);
            }
            if let Some(duration) = self.render_duration_from_position_span() {
                self.record_unbounded_duration_samples
                    .fetch_max(duration, Ordering::AcqRel);
            }
        }
    }

    pub fn snapshot(&self, expected_generation: u64) -> Option<RecordTakeSnapshot> {
        if expected_generation == 0 {
            return None;
        }
        let generation = self.record_generation.load(Ordering::Acquire);
        let bounded_duration_samples = self.record_bounded_duration_samples.load(Ordering::Acquire);
        let unbounded_duration_samples = self
            .record_unbounded_duration_samples
            .load(Ordering::Acquire);
        let epoch = self.record_render_epoch.load(Ordering::Acquire);
        if generation == expected_generation && epoch > 0 {
            snapshot_from_durations(
                generation,
                bounded_duration_samples,
                unbounded_duration_samples,
            )
        } else if self.previous_generation.load(Ordering::Acquire) == expected_generation {
            let bounded_duration_samples = self
                .previous_bounded_duration_samples
                .load(Ordering::Acquire);
            let unbounded_duration_samples = self
                .previous_unbounded_duration_samples
                .load(Ordering::Acquire);
            snapshot_from_durations(
                expected_generation,
                bounded_duration_samples,
                unbounded_duration_samples,
            )
        } else {
            None
        }
    }

    fn ensure_record_generation(&self, generation: u64) {
        let current_generation = self.record_generation.load(Ordering::Acquire);
        if current_generation == generation {
            return;
        }
        self.preserve_current_record_snapshot();
        if current_generation > 0 {
            self.render_active.store(false, Ordering::Release);
            self.render_frames.store(0, Ordering::Release);
            self.render_start_valid.store(false, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
        }
        self.record_render_epoch.store(0, Ordering::Release);
        self.record_bounded_duration_samples
            .store(0, Ordering::Release);
        self.record_unbounded_duration_samples
            .store(0, Ordering::Release);
        self.record_generation.store(generation, Ordering::Release);
    }

    fn preserve_current_record_snapshot(&self) {
        let generation = self.record_generation.load(Ordering::Acquire);
        if generation == 0 {
            return;
        }
        let epoch = self.record_render_epoch.load(Ordering::Acquire);
        let bounded_duration_samples = self.record_bounded_duration_samples.load(Ordering::Acquire);
        let unbounded_duration_samples = self
            .record_unbounded_duration_samples
            .load(Ordering::Acquire);
        if epoch > 0 && (bounded_duration_samples > 0 || unbounded_duration_samples > 0) {
            self.previous_bounded_duration_samples
                .store(bounded_duration_samples, Ordering::Release);
            self.previous_unbounded_duration_samples
                .store(unbounded_duration_samples, Ordering::Release);
            self.previous_generation
                .store(generation, Ordering::Release);
        }
    }

    fn position_discontinuity(&self, position_valid: bool, position_samples: i64) -> bool {
        if !position_valid || !self.render_last_end_valid.load(Ordering::Acquire) {
            return false;
        }
        let previous_end = self.render_last_end_position.load(Ordering::Acquire);
        position_samples < previous_end.saturating_sub(1)
    }

    fn render_duration_from_position_span(&self) -> Option<u64> {
        if !self.render_start_valid.load(Ordering::Acquire)
            || !self.render_last_end_valid.load(Ordering::Acquire)
        {
            return None;
        }
        let start = self.render_start_position.load(Ordering::Acquire);
        let end = self.render_last_end_position.load(Ordering::Acquire);
        if end >= start {
            Some((end - start) as u64)
        } else {
            None
        }
    }
}

fn bounded_duration_from_block(block: &RecordTakeBlock) -> Option<u64> {
    let end = block.clock_end_samples?;
    if end > block.clock_start_samples {
        Some(end.saturating_sub(block.clock_start_samples) as u64)
    } else {
        None
    }
}

fn snapshot_from_durations(
    generation: u64,
    bounded_duration_samples: u64,
    unbounded_duration_samples: u64,
) -> Option<RecordTakeSnapshot> {
    if bounded_duration_samples > 0 {
        Some(RecordTakeSnapshot {
            generation,
            duration_samples: bounded_duration_samples,
            source: RECORD_TAKE_SOURCE_WAV_CLOCK,
        })
    } else if unbounded_duration_samples > 0 {
        Some(RecordTakeSnapshot {
            generation,
            duration_samples: unbounded_duration_samples,
            source: RECORD_TAKE_SOURCE_RENDER_CLOCK,
        })
    } else {
        None
    }
}

pub fn new_record_take_tracker() -> Arc<RecordTakeTracker> {
    Arc::new(RecordTakeTracker::new())
}

#[cfg(test)]
mod tests {
    use super::{
        RecordTakeBlock, RecordTakeTracker, RECORD_TAKE_SOURCE_RENDER_CLOCK,
        RECORD_TAKE_SOURCE_WAV_CLOCK,
    };

    fn block(generation: u64, position_samples: i64, num_frames: u64) -> RecordTakeBlock {
        RecordTakeBlock {
            generation,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples,
            num_frames,
            clock_start_samples: 0,
            clock_end_samples: None,
        }
    }

    fn bounded_block(
        generation: u64,
        position_samples: i64,
        num_frames: u64,
        clock_start_samples: i64,
        clock_end_samples: i64,
    ) -> RecordTakeBlock {
        RecordTakeBlock {
            generation,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples,
            num_frames,
            clock_start_samples,
            clock_end_samples: Some(clock_end_samples),
        }
    }

    #[test]
    fn offline_render_before_record_edge_does_not_pollute_record_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            recording: false,
            ..block(0, 0, 512)
        });
        tracker.note_block(block(7, 512, 512));

        let snap = tracker.snapshot(7).expect("clean take");
        assert_eq!(snap.duration_samples, 512);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_RENDER_CLOCK);
    }

    #[test]
    fn realtime_playback_before_keep_does_not_pollute_record_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            recording: false,
            playing: true,
            offline: false,
            ..block(0, 0, 48_000)
        });
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(3, 48_000, 1_024)
        });

        assert_eq!(tracker.snapshot(3).unwrap().duration_samples, 1_024);
    }

    #[test]
    fn stopped_tail_after_record_does_not_extend_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(9, 0, 44_100)
        });
        tracker.note_block(RecordTakeBlock {
            rendered: false,
            offline: false,
            ..block(9, 44_100, 44_100)
        });

        assert_eq!(tracker.snapshot(9).unwrap().duration_samples, 44_100);
    }

    #[test]
    fn invalid_position_never_becomes_clean_take_duration() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            position_valid: false,
            ..block(19, i64::MIN, 1_440_000)
        });

        assert_eq!(tracker.snapshot(19), None);
    }

    #[test]
    fn position_rewind_starts_a_new_render_epoch() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(4, 10_000, 1_000));
        tracker.note_block(block(4, 0, 2_000));

        assert_eq!(tracker.snapshot(4).unwrap().duration_samples, 2_000);
    }

    #[test]
    fn position_span_prevents_duplicate_offline_prefix_overcount() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            recording: false,
            ..block(0, 0, 1_000)
        });
        tracker.note_block(RecordTakeBlock {
            recording: false,
            ..block(0, 0, 1_000)
        });
        tracker.note_block(block(11, 1_000, 1_000));

        assert_eq!(tracker.snapshot(11).unwrap().duration_samples, 1_000);
    }

    #[test]
    fn snapshot_retains_previous_generation_after_next_record_starts() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(21, 0, 44_100)
        });
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(22, 44_100, 1_024)
        });

        assert_eq!(tracker.snapshot(21).unwrap().duration_samples, 44_100);
        assert_eq!(tracker.snapshot(22).unwrap().duration_samples, 1_024);
    }

    #[test]
    fn later_short_fragment_does_not_shrink_same_record_generation() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(31, 0, 1_440_000));
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(31, 0, 2_048)
        });

        assert_eq!(tracker.snapshot(31).unwrap().duration_samples, 1_440_000);
    }

    #[test]
    fn bounded_wav_clock_wins_over_long_process_tail() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(bounded_block(41, 0, 1_440_000, 0, 1_440_000));
        tracker.note_block(block(41, 1_440_000, 20_224));

        let snap = tracker.snapshot(41).expect("bounded take");
        assert_eq!(snap.duration_samples, 1_440_000);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_WAV_CLOCK);
    }

    #[test]
    fn nonzero_wav_clock_start_reports_wav_duration_not_position_span() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(bounded_block(42, 96_000, 512, 96_000, 1_536_000));

        let snap = tracker.snapshot(42).expect("bounded take");
        assert_eq!(snap.duration_samples, 1_440_000);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_WAV_CLOCK);
    }
}
