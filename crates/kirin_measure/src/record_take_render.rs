//! Producer-owned render epoch selection and duration publication.
use super::*;

#[cfg(test)]
#[path = "record_take_empty_callback_tests.rs"]
mod empty_callback_tests;

impl RecordTakeTracker {
    /// Audio-thread note. This is atomics only: no allocation, lock, filesystem,
    /// logging, or blocking call.
    pub fn note_block(&self, block: RecordTakeBlock) {
        let watch_offline =
            !block.recording && block.offline && block.position_valid && block.num_frames > 0;
        let watch_offline_active = self.watch_offline_active.load(Ordering::Acquire);
        if watch_offline {
            if !watch_offline_active {
                self.watch_offline_candidate_epoch
                    .store(0, Ordering::Release);
                self.watch_offline_force_pending
                    .store(true, Ordering::Release);
                self.watch_offline_active.store(true, Ordering::Release);
            }
        } else if !(block.recording && block.offline && watch_offline_active) {
            self.watch_offline_active.store(false, Ordering::Release);
            self.watch_offline_force_pending
                .store(false, Ordering::Release);
            self.watch_offline_candidate_epoch
                .store(0, Ordering::Release);
        }
        self.capture_block_offline
            .store(block.offline, Ordering::Release);

        self.mark_version.fetch_add(1, Ordering::AcqRel);
        self.mark_capture_armed.store(
            block.recording
                && block.rendered
                && block.generation > 0
                && block.position_valid
                && block.num_frames > 0,
            Ordering::Relaxed,
        );
        self.mark_generation
            .store(block.generation, Ordering::Relaxed);
        self.mark_position_valid.store(false, Ordering::Relaxed);
        self.mark_source
            .store(CaptureClockSource::Unknown as u8, Ordering::Relaxed);
        self.mark_version.fetch_add(1, Ordering::Release);
        if block.recording && block.generation > 0 {
            self.ensure_record_generation(block.generation);
        }

        let render_eligible =
            block.rendered && block.num_frames > 0 && block.recording && block.position_valid;

        if block.recording && block.playing && block.num_frames == 0 {
            // No audio cannot end a still-playing take. Retained producer evidence can service
            // a Measure request without adding frames or changing the render epoch.
            self.apply_take_promotion(
                &block,
                self.render_epoch.load(Ordering::Acquire),
                !self.render_active.load(Ordering::Acquire),
            );
            return;
        }
        if !render_eligible {
            self.render_active.store(false, Ordering::Release);
            self.render_epoch_priority
                .store(TakeEpochPriority::None as u8, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
            self.apply_take_promotion(&block, self.render_epoch.load(Ordering::Acquire), true);
            return;
        }

        let candidate_priority = TakeEpochPriority::for_block(&block);
        let reset_epoch = !self.render_active.load(Ordering::Acquire)
            || self.position_discontinuity(block.position_valid, block.position_samples)
            || TakeEpochPriority::from_abi(self.render_epoch_priority.load(Ordering::Acquire))
                != candidate_priority;
        let epoch = if reset_epoch {
            self.render_frames.store(0, Ordering::Release);
            self.render_start_position
                .store(block.position_samples, Ordering::Release);
            self.render_start_valid.store(true, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
            let next = self.render_epoch.fetch_add(1, Ordering::AcqRel) + 1;
            self.render_epoch_priority
                .store(candidate_priority as u8, Ordering::Release);
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

        let selected_priority =
            TakeEpochPriority::from_abi(self.record_epoch_priority.load(Ordering::Acquire));
        let selected_epoch = self.record_render_epoch.load(Ordering::Acquire);
        if selected_epoch == 0 || candidate_priority > selected_priority {
            self.select_record_epoch(epoch, candidate_priority);
        }
        self.apply_take_promotion(&block, epoch, reset_epoch);
        if self.record_render_epoch.load(Ordering::Acquire) != epoch {
            return;
        }

        if candidate_priority == TakeEpochPriority::NativeRange {
            // A host native range is a complete take declaration, not a running maximum. The
            // first declaration for this immutable epoch wins; later callbacks cannot stretch it.
            if self.record_bounded_duration_samples.load(Ordering::Acquire) == 0 {
                if let Some(duration) = bounded_duration_from_block(&block) {
                    self.record_bounded_range_valid
                        .store(false, Ordering::Release);
                    self.record_bounded_start_position
                        .store(block.clock_start_samples, Ordering::Relaxed);
                    self.record_bounded_end_position.store(
                        block.clock_end_samples.unwrap_or(i64::MIN),
                        Ordering::Relaxed,
                    );
                    self.record_bounded_duration_samples
                        .store(duration, Ordering::Release);
                    self.record_bounded_range_valid
                        .store(true, Ordering::Release);
                }
            }
        } else if let Some(duration) = self.render_duration_from_position_span() {
            // Duration may grow only while the selected epoch itself remains active. A later
            // rewind/post-bounce epoch never competes by length.
            let previous = self
                .record_unbounded_duration_samples
                .load(Ordering::Acquire);
            if duration > previous {
                self.record_unbounded_range_valid
                    .store(false, Ordering::Release);
                let start = self.render_start_position.load(Ordering::Acquire);
                let end = self.render_last_end_position.load(Ordering::Acquire);
                self.record_unbounded_start_position
                    .store(start, Ordering::Relaxed);
                self.record_unbounded_end_position
                    .store(end, Ordering::Relaxed);
                self.record_unbounded_duration_samples
                    .store(duration, Ordering::Release);
                if end.checked_sub(start) == i64::try_from(duration).ok() {
                    self.record_unbounded_range_valid
                        .store(true, Ordering::Release);
                }
            }
        }
    }

    pub(super) fn select_record_epoch(&self, epoch: u64, priority: TakeEpochPriority) {
        self.record_capture_epoch.store(0, Ordering::Release);
        self.record_bounded_duration_samples
            .store(0, Ordering::Release);
        self.record_bounded_range_valid
            .store(false, Ordering::Release);
        self.record_unbounded_duration_samples
            .store(0, Ordering::Release);
        self.record_unbounded_range_valid
            .store(false, Ordering::Release);
        self.record_epoch_priority
            .store(priority as u8, Ordering::Release);
        self.record_render_epoch.store(epoch, Ordering::Release);
    }
}
