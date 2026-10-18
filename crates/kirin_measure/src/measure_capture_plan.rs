//! Immutable bridge from accepted capture spans to Measure Thread chunks.

use crate::record_take::{
    CaptureClockPoint, CaptureClockSource, KeepChunk, PresentationLatencySamples, RecordTakeTracker,
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
    /// The chunk begins its span, and the span began with the host's own LOOP wrap.
    pub(super) host_loop_wrap: bool,
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

    /// The Keep-pass account of this chunk once `frames` of it were consumed.
    pub(super) fn keep_chunk(
        self,
        generation: u64,
        frames: u64,
        route: RecordChunkRoute,
        in_summary: bool,
    ) -> KeepChunk {
        KeepChunk {
            generation,
            raw_host_start: self.raw_host_position_start_samples,
            auxiliary: self.auxiliary,
            frames,
            in_trace: route == RecordChunkRoute::Take,
            in_summary,
            // A chunk without a capture span names no pass; continuity alone decides its reason.
            after_offline_take: route == RecordChunkRoute::LiveOnly && self.capture_epoch.is_some(),
            host_loop_wrap: self.host_loop_wrap,
        }
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

/// What one captured Record-lane chunk may feed. The live meters always follow the audio
/// (INV-S15); only the Record outputs depend on which pass the chunk belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum RecordChunkRoute {
    /// The selected take, its admitted epoch or a latency continuation: every Record output.
    Take,
    /// A later realtime pass of the same realtime Keep (a LOOP wrap, a seek, a restart after a
    /// stop): the Keep summary as well, never the position-bound TRACE of the selected take.
    KeepPass,
    /// Any other pass, typically the DAW's position jump after an offline bounce: live only.
    LiveOnly,
}

/// Routes one Record-lane chunk against the selected take and its admitted epoch.
impl RecordChunkRoute {
    pub(super) fn live_only(self) -> bool {
        self == Self::LiveOnly
    }
}

pub(super) fn chunk_route(
    tracker: &RecordTakeTracker,
    selected_epoch: Option<u64>,
    admitted_epoch: Option<u64>,
    plan: CaptureChunkPlan,
    latency_epoch_transition: bool,
) -> RecordChunkRoute {
    let realtime_passes =
        selected_epoch
            .zip(plan.capture_epoch)
            .is_some_and(|(selected, candidate)| {
                tracker.capture_epochs_are_realtime_passes(selected, candidate)
            });
    record_chunk_route(
        selected_epoch,
        admitted_epoch,
        plan.capture_epoch,
        latency_epoch_transition,
        realtime_passes,
    )
}

/// The chunk moves the take from its current epoch to the next through a latency or
/// auxiliary-only continuation: still the same take, for the main loop and the STOP tail alike.
pub(super) fn take_epoch_transition(
    tracker: &RecordTakeTracker,
    take_epoch: Option<u64>,
    plan: CaptureChunkPlan,
    generation: u64,
) -> bool {
    take_epoch
        .zip(plan.capture_epoch)
        .is_some_and(|(previous, next)| {
            previous != next
                && plan.capture_generation == Some(generation)
                && tracker.capture_epochs_are_latency_continuation(previous, next)
        })
}

pub(super) fn record_chunk_route(
    selected_epoch: Option<u64>,
    admitted_epoch: Option<u64>,
    candidate_epoch: Option<u64>,
    latency_epoch_transition: bool,
    realtime_passes: bool,
) -> RecordChunkRoute {
    if selected_epoch.is_none()
        || candidate_epoch == selected_epoch
        || candidate_epoch == admitted_epoch
        || latency_epoch_transition
    {
        RecordChunkRoute::Take
    } else if realtime_passes {
        RecordChunkRoute::KeepPass
    } else {
        RecordChunkRoute::LiveOnly
    }
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
        host_loop_wrap: span.host_loop_wrap && span.capture_start_frame == captured_frames,
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

#[cfg(test)]
mod tests {
    use super::{record_chunk_route as route, RecordChunkRoute::*};

    #[test]
    fn realtime_keep_passes_reach_the_summary_but_never_the_selected_take() {
        // A LOOP wrap, seek or restart inside a realtime Keep is a later pass of the same Keep.
        assert_eq!(route(Some(4), Some(4), Some(5), false, true), KeepPass);
        assert_eq!(route(Some(4), Some(4), Some(9), false, true), KeepPass);
        // The take itself and a latency continuation keep every Record output.
        assert_eq!(route(Some(4), Some(4), Some(4), false, true), Take);
        assert_eq!(route(Some(4), Some(4), Some(5), true, true), Take);
        assert_eq!(route(None, None, Some(5), false, false), Take);
    }

    #[test]
    fn admitted_latency_epoch_keeps_every_following_chunk() {
        assert_eq!(route(Some(10), Some(10), Some(11), true, false), Take);
        assert_eq!(route(Some(10), Some(11), Some(11), false, false), Take);
        assert_eq!(route(Some(10), Some(11), Some(12), false, false), LiveOnly);
    }

    #[test]
    fn a_pass_after_an_offline_take_reaches_only_the_live_meters() {
        assert_eq!(route(Some(4), Some(4), Some(5), false, false), LiveOnly);
        assert_eq!(route(Some(4), Some(4), None, false, false), LiveOnly);
    }

    #[test]
    fn only_the_chunk_that_begins_a_host_loop_wrap_carries_it() {
        use crate::record_take::{CaptureClockSource, PresentationLatencySamples};
        use crate::{AuxiliaryClockSamples, AuxiliaryClockSource};
        let tracker = crate::record_take::RecordTakeTracker::new();
        let callback = |position: i64, aux: i64, wrap: bool| {
            tracker.stage_host_loop_wrap(wrap);
            tracker.note_capture_window_with_clocks_boundary(
                true,
                position,
                512,
                CaptureClockSource::ProjectTimeline,
                PresentationLatencySamples::default(),
                AuxiliaryClockSamples {
                    source: AuxiliaryClockSource::Vst3Continuous,
                    samples: Some(aux),
                },
                false,
            );
        };
        callback(95_488, 1_000, false);
        // The host's loop range names the return; the next callback continues the new pass.
        callback(0, 1_512, true);
        callback(512, 2_024, false);
        // A seek back is not named by the loop.
        callback(40_000, 2_536, false);
        let plan = |frame: u64, samples: usize| {
            super::capture_chunk_plan(&tracker, frame, samples, 2, 48_000)
        };
        assert!(!plan(0, 1_024).host_loop_wrap);
        assert!(plan(512, 2_048).host_loop_wrap, "the wrap's first chunk");
        let wrap = plan(512, 2_048);
        assert!(
            !plan(512 + 256, 2_048).host_loop_wrap,
            "inside the wrap's span"
        );
        assert!(!plan(1_536, 1_024).host_loop_wrap, "the seek's span");
        // The flag reaches the pass journal through the chunk.
        let chunk = wrap.keep_chunk(1, 1_024, super::RecordChunkRoute::KeepPass, true);
        assert!(chunk.host_loop_wrap && chunk.raw_host_start == Some(0));
    }
}
