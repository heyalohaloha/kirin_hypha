//! Immutable bridge from accepted capture spans to Measure Thread chunks.

use crate::record_take::{
    CaptureClockPoint, CaptureClockSource, PresentationLatencySamples, RecordTakeTracker,
};
use crate::{AuxiliaryClockSamples, MeterClockStart};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(super) struct CaptureChunkPlan {
    pub(super) sample_count: usize,
    pub(super) position_start_samples: Option<i64>,
    pub(super) position_end_samples: Option<i64>,
    pub(super) raw_host_position_start_samples: Option<i64>,
    pub(super) capture_epoch: Option<u64>,
    pub(super) capture_generation: Option<u64>,
    pub(super) clock_source: CaptureClockSource,
    pub(super) presentation_latency: PresentationLatencySamples,
    pub(super) auxiliary: AuxiliaryClockSamples,
}

impl CaptureChunkPlan {
    fn clock_source(self) -> Option<CaptureClockSource> {
        (self.clock_source != CaptureClockSource::Unknown).then_some(self.clock_source)
    }

    fn start_clock_point(self) -> Option<CaptureClockPoint> {
        Some(CaptureClockPoint {
            position_samples: self.position_start_samples?,
            raw_host_position_samples: self.raw_host_position_start_samples?,
            epoch: self.capture_epoch?,
            source: self.clock_source()?,
            presentation_latency: self.presentation_latency,
        })
    }

    fn clock_point_at(self, position_samples: i64) -> Option<CaptureClockPoint> {
        let start = self.start_clock_point()?;
        let end = self.position_end_samples?;
        let delta = position_samples.checked_sub(start.position_samples)?;
        (delta >= 0 && position_samples <= end).then_some(CaptureClockPoint {
            position_samples,
            raw_host_position_samples: start.raw_host_position_samples.saturating_add(delta),
            ..start
        })
    }

    pub(super) fn meter_clock_start(self, frame_offset: u64) -> MeterClockStart {
        let offset = i64::try_from(frame_offset).ok();
        MeterClockStart {
            position_samples: self
                .position_start_samples
                .zip(offset)
                .and_then(|(position, offset)| position.checked_add(offset)),
            epoch: self.capture_epoch,
            source: self.clock_source,
            auxiliary: AuxiliaryClockSamples {
                source: self.auxiliary.source,
                samples: self
                    .auxiliary
                    .samples
                    .zip(offset)
                    .and_then(|(position, offset)| position.checked_add(offset)),
            },
            presentation_latency: self.presentation_latency,
        }
    }
}

pub(super) fn should_drop_unselected_record_epoch(
    selected_epoch: Option<u64>,
    admitted_epoch: Option<u64>,
    candidate_epoch: Option<u64>,
    latency_epoch_transition: bool,
) -> bool {
    selected_epoch.is_some()
        && candidate_epoch != selected_epoch
        && candidate_epoch != admitted_epoch
        && !latency_epoch_transition
}

/// Resolve one MeasureEngine observer endpoint back to the exact captured audio boundary.
/// Engine priming is excluded before the capture delta is applied.
pub(super) fn observed_capture_endpoint(
    plan: CaptureChunkPlan,
    tracker: &RecordTakeTracker,
    captured_frames_before: u64,
    engine_frames_before: u64,
    engine_pending_frames_before: u64,
    native_engine_frames: u64,
    chunk_native_frames: u64,
) -> (u64, Option<CaptureClockPoint>) {
    let observed_delta = native_engine_frames
        .saturating_sub(engine_frames_before)
        .saturating_sub(engine_pending_frames_before)
        .min(chunk_native_frames);
    let captured_frame = captured_frames_before.saturating_add(observed_delta);
    let plan_point = plan
        .position_start_samples
        .and_then(|start| i64::try_from(observed_delta).ok()?.checked_add(start))
        .and_then(|position| plan.clock_point_at(position));
    (
        captured_frame,
        plan_point.or_else(|| tracker.clock_point_for_captured_frame(captured_frame)),
    )
}

pub(super) fn capture_chunk_plan(
    tracker: &RecordTakeTracker,
    captured_frames: u64,
    available_samples: usize,
    n_channels: usize,
    sample_rate: u32,
) -> CaptureChunkPlan {
    if n_channels == 0 || available_samples < n_channels {
        return CaptureChunkPlan::default();
    }
    let available_frames =
        ((available_samples / n_channels) as u64).min((sample_rate as u64 / 10).max(1));
    let Some(span) = tracker.capture_span_for_frame(captured_frames.saturating_add(1)) else {
        return CaptureChunkPlan {
            sample_count: available_frames as usize * n_channels,
            ..CaptureChunkPlan::default()
        };
    };
    let frames = available_frames.min(span.capture_end_frame.saturating_sub(captured_frames));
    let position_start_samples = span.position_at_capture_boundary(captured_frames);
    CaptureChunkPlan {
        sample_count: frames as usize * n_channels,
        position_start_samples,
        position_end_samples: position_start_samples
            .map(|position| position.saturating_add(frames as i64)),
        raw_host_position_start_samples: span
            .raw_host_position_at_capture_boundary(captured_frames),
        capture_epoch: Some(span.epoch),
        capture_generation: Some(span.generation),
        clock_source: span.source,
        presentation_latency: span.presentation_latency,
        auxiliary: AuxiliaryClockSamples {
            source: span.auxiliary_source,
            samples: span.auxiliary_at_capture_boundary(captured_frames),
        },
    }
}

#[inline]
pub(super) fn trusted_pre_roll_epoch(plan: CaptureChunkPlan) -> Option<u64> {
    (plan.clock_source != CaptureClockSource::Unknown
        && plan.position_start_samples.is_some()
        && plan.position_end_samples.is_some())
    .then_some(plan.capture_epoch)
    .flatten()
}
