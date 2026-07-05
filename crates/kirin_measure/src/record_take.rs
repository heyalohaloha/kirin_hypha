//! Record take duration tracker.
//!
//! Audio Thread owns the writes through atomics only. IO Thread reads a snapshot
//! when Record closes and uses it as the clean bounce take duration. The tracker
//! deliberately measures the render span, not the manual Keep/Stop span.

use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, Ordering};
use std::sync::Arc;

pub const RECORD_TAKE_SOURCE_RENDER_CLOCK: &str = "render_clock_native";

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
    record_duration_samples: AtomicU64,
    previous_generation: AtomicU64,
    previous_duration_samples: AtomicU64,
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
            record_duration_samples: AtomicU64::new(0),
            previous_generation: AtomicU64::new(0),
            previous_duration_samples: AtomicU64::new(0),
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
            if let Some(duration) = self.render_duration_from_position_span() {
                self.record_duration_samples
                    .fetch_max(duration, Ordering::AcqRel);
            }
        }
    }

    pub fn snapshot(&self, expected_generation: u64) -> Option<RecordTakeSnapshot> {
        if expected_generation == 0 {
            return None;
        }
        let generation = self.record_generation.load(Ordering::Acquire);
        let duration_samples = self.record_duration_samples.load(Ordering::Acquire);
        let epoch = self.record_render_epoch.load(Ordering::Acquire);
        if generation == expected_generation && duration_samples > 0 && epoch > 0 {
            Some(RecordTakeSnapshot {
                generation,
                duration_samples,
                source: RECORD_TAKE_SOURCE_RENDER_CLOCK,
            })
        } else if self.previous_generation.load(Ordering::Acquire) == expected_generation {
            let duration_samples = self.previous_duration_samples.load(Ordering::Acquire);
            if duration_samples > 0 {
                Some(RecordTakeSnapshot {
                    generation: expected_generation,
                    duration_samples,
                    source: RECORD_TAKE_SOURCE_RENDER_CLOCK,
                })
            } else {
                None
            }
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
        self.record_duration_samples.store(0, Ordering::Release);
        self.record_generation.store(generation, Ordering::Release);
    }

    fn preserve_current_record_snapshot(&self) {
        let generation = self.record_generation.load(Ordering::Acquire);
        if generation == 0 {
            return;
        }
        let epoch = self.record_render_epoch.load(Ordering::Acquire);
        let duration_samples = self.record_duration_samples.load(Ordering::Acquire);
        if epoch > 0 && duration_samples > 0 {
            self.previous_duration_samples
                .store(duration_samples, Ordering::Release);
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

pub fn new_record_take_tracker() -> Arc<RecordTakeTracker> {
    Arc::new(RecordTakeTracker::new())
}

#[cfg(test)]
mod tests {
    use super::{RecordTakeBlock, RecordTakeTracker, RECORD_TAKE_SOURCE_RENDER_CLOCK};

    #[test]
    fn offline_render_before_record_edge_does_not_pollute_record_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 0,
            recording: false,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 0,
            num_frames: 512,
        });
        tracker.note_block(RecordTakeBlock {
            generation: 7,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 512,
            num_frames: 512,
        });

        let snap = tracker.snapshot(7).expect("clean take");
        assert_eq!(snap.duration_samples, 512);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_RENDER_CLOCK);
    }

    #[test]
    fn realtime_playback_before_keep_does_not_pollute_record_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 0,
            recording: false,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: 0,
            num_frames: 48_000,
        });
        tracker.note_block(RecordTakeBlock {
            generation: 3,
            recording: true,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: 48_000,
            num_frames: 1_024,
        });

        assert_eq!(tracker.snapshot(3).unwrap().duration_samples, 1_024);
    }

    #[test]
    fn stopped_tail_after_record_does_not_extend_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 9,
            recording: true,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: 0,
            num_frames: 44_100,
        });
        tracker.note_block(RecordTakeBlock {
            generation: 9,
            recording: true,
            rendered: false,
            playing: false,
            offline: false,
            position_valid: true,
            position_samples: 44_100,
            num_frames: 44_100,
        });

        assert_eq!(tracker.snapshot(9).unwrap().duration_samples, 44_100);
    }

    #[test]
    fn invalid_position_never_becomes_clean_take_duration() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 19,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: false,
            position_samples: i64::MIN,
            num_frames: 1_440_000,
        });

        assert_eq!(tracker.snapshot(19), None);
    }

    #[test]
    fn position_rewind_starts_a_new_render_epoch() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 4,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 10_000,
            num_frames: 1_000,
        });
        tracker.note_block(RecordTakeBlock {
            generation: 4,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 0,
            num_frames: 2_000,
        });

        assert_eq!(tracker.snapshot(4).unwrap().duration_samples, 2_000);
    }

    #[test]
    fn position_span_prevents_duplicate_offline_prefix_overcount() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 0,
            recording: false,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 0,
            num_frames: 1_000,
        });
        tracker.note_block(RecordTakeBlock {
            generation: 0,
            recording: false,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 0,
            num_frames: 1_000,
        });
        tracker.note_block(RecordTakeBlock {
            generation: 11,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 1_000,
            num_frames: 1_000,
        });

        assert_eq!(tracker.snapshot(11).unwrap().duration_samples, 1_000);
    }

    #[test]
    fn snapshot_retains_previous_generation_after_next_record_starts() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 21,
            recording: true,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: 0,
            num_frames: 44_100,
        });
        tracker.note_block(RecordTakeBlock {
            generation: 22,
            recording: true,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: 44_100,
            num_frames: 1_024,
        });

        assert_eq!(tracker.snapshot(21).unwrap().duration_samples, 44_100);
        assert_eq!(tracker.snapshot(22).unwrap().duration_samples, 1_024);
    }

    #[test]
    fn later_short_fragment_does_not_shrink_same_record_generation() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 31,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 0,
            num_frames: 1_440_000,
        });
        tracker.note_block(RecordTakeBlock {
            generation: 31,
            recording: true,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: 0,
            num_frames: 2_048,
        });

        assert_eq!(tracker.snapshot(31).unwrap().duration_samples, 1_440_000);
    }
}
