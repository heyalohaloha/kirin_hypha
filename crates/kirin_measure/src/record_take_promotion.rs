//! One measurement request, applied only by the producer using retained callback evidence.
use super::*;

const COMPLETED_RENDER_EPOCH: u64 = u64::MAX;

#[derive(Debug, Default)]
pub(super) struct TakePromotionRequest {
    generation: AtomicU64,
    from: AtomicU64,
    to: AtomicU64,
}

impl TakePromotionRequest {
    fn apply(&self, operation: impl FnOnce(u64, u64, u64)) {
        let generation = self.generation.load(Ordering::Acquire);
        if generation == 0 {
            return;
        }
        let from = self.from.load(Ordering::Acquire);
        let target = self.to.load(Ordering::Acquire);
        if self.generation.load(Ordering::Acquire) != generation {
            return;
        }
        operation(generation, from, target);
        let _ =
            self.generation
                .compare_exchange(generation, 0, Ordering::Release, Ordering::Relaxed);
    }
}

impl RecordTakeTracker {
    pub(crate) fn request_take_promotion(&self, generation: u64, from: u64, to: u64) -> bool {
        if generation == 0
            || from == 0
            || to <= from
            || self.record_generation.load(Ordering::Acquire) != generation
            || self.record_capture_epoch.load(Ordering::Acquire) != from
        {
            return false;
        }
        self.take_promotion.generation.store(0, Ordering::Release);
        self.take_promotion.from.store(from, Ordering::Relaxed);
        self.take_promotion.to.store(to, Ordering::Relaxed);
        self.take_promotion
            .generation
            .store(generation, Ordering::Release);
        true
    }

    pub(crate) fn take_promotion_pending(&self, generation: u64) -> bool {
        generation != 0 && self.take_promotion.generation.load(Ordering::Acquire) == generation
    }

    pub(crate) fn trace_replacement_reason(&self, selected: u64) -> &'static str {
        if self.take_promotion.to.load(Ordering::Acquire) == selected {
            crate::record_take::KEEP_PASS_SHORT_TRACE_REPLACED
        } else {
            crate::record_take::KEEP_PASS_REPLACED_BY_OFFLINE_TAKE
        }
    }

    pub(super) fn apply_take_promotion(&self, block: &RecordTakeBlock, epoch: u64, reset: bool) {
        self.take_promotion.apply(|generation, from, target| {
            if generation != block.generation
                || self.record_capture_epoch.load(Ordering::Acquire) != from
                || TakeEpochPriority::from_abi(self.record_epoch_priority.load(Ordering::Acquire))
                    != TakeEpochPriority::Rendered
            {
                return;
            }
            let Some((prefix, last)) = self.promotion_prefix(generation, target) else {
                return;
            };
            let Some(start) = prefix.raw_host_position_start_samples else {
                return;
            };
            let frames = prefix
                .capture_end_frame
                .saturating_sub(prefix.capture_start_frame);
            let Some(end) = i64::try_from(frames)
                .ok()
                .and_then(|frames| start.checked_add(frames))
            else {
                return;
            };
            if frames == 0 {
                return;
            }
            // An already-completed requested pass stays frozen while later realtime passes run.
            // The reserved token cannot equal an ordinary producer render epoch.
            let current = !reset && last == self.capture_last_span_sequence.load(Ordering::Acquire);
            let render = if current {
                epoch
            } else {
                COMPLETED_RENDER_EPOCH
            };
            self.select_record_epoch(render, TakeEpochPriority::Rendered);
            self.record_capture_epoch.store(target, Ordering::Release);
            self.record_unbounded_start_position
                .store(start, Ordering::Relaxed);
            self.record_unbounded_end_position
                .store(end, Ordering::Relaxed);
            self.record_unbounded_duration_samples
                .store(frames, Ordering::Release);
            self.record_unbounded_range_valid
                .store(true, Ordering::Release);
            self.record_audio_mapping.seed_proven_prefix(prefix, last);
        });
    }

    /// Bounded atomic reads of the requested pass, including qualified auxiliary-only cuts.
    /// A retired or discontinuous prefix is never reconstructed from positions alone.
    fn promotion_prefix(&self, generation: u64, target: u64) -> Option<(CaptureClockSpan, u64)> {
        let mut prefix = self.capture_span_for_epoch(target)?;
        if prefix.generation != generation || prefix.offline {
            return None;
        }
        let mut last = target;
        let latest = self.capture_last_span_sequence.load(Ordering::Acquire);
        for _ in 0..CAPTURE_CLOCK_SPAN_CAPACITY {
            if last >= latest {
                break;
            }
            let next = self.capture_span_for_epoch(last.checked_add(1)?)?;
            if next.generation != generation
                || next.offline
                || !next.auxiliary_only_cut
                || !self.capture_epochs_are_latency_continuation(last, next.epoch)
            {
                break;
            }
            prefix.capture_end_frame = next.capture_end_frame;
            last = next.epoch;
        }
        Some((prefix, last))
    }
}

#[cfg(test)]
#[path = "record_take_promotion_tests.rs"]
mod tests;
