//! Close the old TRACE before establishing the selected take's own sample axis.
use super::capture_plan::CaptureChunkPlan;
use super::*;

pub(super) struct TraceTakeBoundary<'a> {
    pub queue: &'a RecordTraceQueue,
    pub tracker: &'a RecordTakeTracker,
    pub generation: u64,
    pub sample_rate: u32,
    pub n_channels: usize,
    pub origin_frames: &'a mut u64,
    pub origin_native_frames: &'a mut u64,
    pub origin_position: &'a mut Option<i64>,
    pub anchor_epoch: &'a mut Option<u64>,
    pub capture_epoch: &'a mut Option<u64>,
    pub offset_frames: &'a mut u64,
    pub offset_native_frames: &'a mut u64,
    pub next_trace_ms: &'a mut u64,
    pub next_psb_ms: &'a mut u64,
    pub grid_cursor: &'a mut Option<i64>,
    pub next_grid_end: &'a mut Option<i64>,
    pub resampler: &'a mut TraceResampler,
    pub phase_pending: &'a mut Vec<f64>,
    pub phase_d: &'a mut PhaseDChannelStream,
    pub latest_pd: &'a mut Option<crate::phase_d::stream::PhaseDResult>,
    pub core: &'a mut VecDeque<PendingRecordCore>,
    pub phases: &'a mut VecDeque<crate::phase_d::stream::PhaseDResult>,
}

impl TraceTakeBoundary<'_> {
    fn timeline(&self) -> RecordTraceTimeline {
        RecordTraceTimeline {
            generation: self.generation,
            origin_frames_48k: *self.origin_frames,
            origin_native_frames: *self.origin_native_frames,
            offset_frames_48k: *self.offset_frames,
            offset_native_frames: *self.offset_native_frames,
            origin_position_samples: *self.origin_position,
        }
    }

    pub(super) fn prepare(
        &mut self,
        recording: bool,
        take: bool,
        selected: Option<u64>,
        plan: CaptureChunkPlan,
        captured_before: u64,
    ) -> RecordTraceTimeline {
        if self.anchor_epoch.is_none() {
            *self.anchor_epoch = selected;
        }
        let replacing = recording
            && take
            && selected.is_some()
            && plan.capture_epoch == selected
            && *self.anchor_epoch != selected;
        let new_pass = plan.position_start_samples.is_some()
            && plan.position_start_samples != *self.grid_cursor;
        let pass_left = self.grid_cursor.is_some() || !self.core.is_empty();
        if recording && (take && (new_pass || replacing) || !take && pass_left) {
            let timeline = self.timeline();
            *self.grid_cursor = None;
            let mut trace = RecordTraceDrain {
                queue: self.queue,
                timeline,
                cursor: RecordTraceCursor {
                    next_trace_ms: self.next_trace_ms,
                    next_psb_ms: self.next_psb_ms,
                },
            };
            close_trace_pass(
                self.resampler,
                self.n_channels,
                self.phase_pending,
                self.phase_d,
                self.latest_pd,
                self.phases,
                self.core,
                Some(&mut trace),
                self.sample_rate,
            );
        }
        if replacing {
            self.tracker.replace_trace_pass(
                self.generation,
                self.tracker.trace_replacement_reason(selected.unwrap_or(0)),
            );
            *self.origin_frames = native_frames_to_48k(captured_before, self.sample_rate);
            *self.origin_native_frames = captured_before;
            *self.origin_position = plan.position_start_samples;
            *self.offset_frames = 0;
            *self.offset_native_frames = 0;
            *self.next_trace_ms = 0;
            *self.next_psb_ms = 0;
            *self.next_grid_end = None;
            *self.capture_epoch = selected;
            *self.anchor_epoch = selected;
        }
        self.timeline()
    }
}

#[cfg(test)]
#[path = "measure_offline_take_tests.rs"]
mod tests;

/// Hold the first new pass in the Record consumer until its producer has handled the request.
pub(super) fn wait_for_short_take(
    tracker: &RecordTakeTracker,
    generation: u64,
    sample_rate: u32,
    route: RecordChunkRoute,
    selected: Option<u64>,
    candidate: Option<u64>,
) -> bool {
    if tracker.take_promotion_pending(generation) {
        return true;
    }
    if tracker.selected_capture_epoch(generation) != selected {
        return true;
    }
    if route != RecordChunkRoute::KeepPass {
        return false;
    }
    let Some((from, to)) = selected.zip(candidate) else {
        return false;
    };
    if tracker.claim_short_first_trace(generation, u64::from(sample_rate)) {
        tracker.request_take_promotion(generation, from, to)
    } else {
        false
    }
}

#[cfg(test)]
#[path = "measure_short_take_tests.rs"]
mod short_take_tests;
