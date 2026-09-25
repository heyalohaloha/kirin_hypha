//! Maps the Meter Session's fixed 100 ms observations back to DAW presentation sample time.

use std::collections::VecDeque;

use crate::{
    AuxiliaryClockSamples, AuxiliaryClockSource, CaptureClockSource, PresentationLatencySamples,
};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct MeterClockStart {
    pub position_samples: Option<i64>,
    pub epoch: Option<u64>,
    pub source: CaptureClockSource,
    pub auxiliary: AuxiliaryClockSamples,
    pub presentation_latency: PresentationLatencySamples,
}

impl MeterClockStart {
    pub fn unknown() -> Self {
        Self::default()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct MeterObservationClock {
    pub run_id: u64,
    pub timeline_endpoint_samples: Option<i64>,
    pub timeline_source: CaptureClockSource,
    pub auxiliary_endpoint_samples: Option<i64>,
    pub auxiliary_source: AuxiliaryClockSource,
    pub presentation_latency: PresentationLatencySamples,
    /// False only when one observation straddles incompatible clock runs.
    pub usable_for_history: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ClockKind {
    Unknown,
    Exact {
        epoch: u64,
        source: CaptureClockSource,
        auxiliary_source: AuxiliaryClockSource,
        presentation_latency: PresentationLatencySamples,
    },
}

#[derive(Debug, Clone, Copy)]
struct PendingSpan {
    remaining_frames: u64,
    next_position_samples: Option<i64>,
    next_auxiliary_samples: Option<i64>,
    kind: ClockKind,
    run_id: u64,
}

#[derive(Debug, Clone, Copy)]
struct LastInputClock {
    end_position_samples: Option<i64>,
    end_auxiliary_samples: Option<i64>,
    kind: ClockKind,
    run_id: u64,
}

pub(crate) struct MeterClockTracker {
    pending: VecDeque<PendingSpan>,
    last_input: Option<LastInputClock>,
    next_run_id: u64,
}

impl MeterClockTracker {
    pub fn new() -> Self {
        Self {
            pending: VecDeque::new(),
            last_input: None,
            next_run_id: 1,
        }
    }

    pub fn push_span(&mut self, frames: u64, start: MeterClockStart) {
        if frames == 0 {
            return;
        }
        let exact = start
            .position_samples
            .zip(start.epoch)
            .and_then(|(position, epoch)| {
                (start.source != CaptureClockSource::Unknown).then_some((position, epoch))
            });
        let (position, kind) = exact.map_or((None, ClockKind::Unknown), |(position, epoch)| {
            (
                Some(position),
                ClockKind::Exact {
                    epoch,
                    source: start.source,
                    auxiliary_source: start.auxiliary.source,
                    presentation_latency: start.presentation_latency,
                },
            )
        });
        let continuation = self.last_input.is_some_and(|last| {
            last.kind == kind
                && match kind {
                    ClockKind::Unknown => true,
                    ClockKind::Exact { .. } => {
                        last.end_position_samples == position
                            && last.end_auxiliary_samples == start.auxiliary.samples
                    }
                }
        });
        let run_id = if continuation {
            self.last_input.map_or(1, |last| last.run_id)
        } else {
            let id = self.next_run_id;
            self.next_run_id = self.next_run_id.wrapping_add(1).max(1);
            id
        };
        let end_position_samples = position.and_then(|value| {
            i64::try_from(frames)
                .ok()
                .and_then(|frames| value.checked_add(frames))
        });
        let end_auxiliary_samples = start.auxiliary.samples.and_then(|value| {
            i64::try_from(frames)
                .ok()
                .and_then(|frames| value.checked_add(frames))
        });
        self.pending.push_back(PendingSpan {
            remaining_frames: frames,
            next_position_samples: position,
            next_auxiliary_samples: start.auxiliary.samples,
            kind,
            run_id,
        });
        self.last_input = Some(LastInputClock {
            end_position_samples,
            end_auxiliary_samples,
            kind,
            run_id,
        });
    }

    /// The next DSP segment may cross callback spans in the same clock run, but must end at
    /// a content-grid boundary or before a provenance transition. This is only a processing
    /// boundary; it does not grant comparison authority to the clock source.
    pub fn frames_to_content_boundary(&self, maximum: u64, step: u64) -> u64 {
        let Some(first) = self.pending.front() else {
            return maximum;
        };
        let mut same_run = 0_u64;
        for span in &self.pending {
            if span.run_id != first.run_id || span.kind != first.kind {
                break;
            }
            same_run = same_run.saturating_add(span.remaining_frames);
            if same_run >= maximum {
                break;
            }
        }
        let grid = first.next_auxiliary_samples.and_then(|auxiliary| {
            let step = i64::try_from(step).ok().filter(|step| *step > 0)?;
            let remainder = auxiliary.rem_euclid(step);
            u64::try_from(if remainder == 0 {
                step
            } else {
                step - remainder
            })
            .ok()
        });
        maximum.min(same_run).min(grid.unwrap_or(maximum)).max(1)
    }

    pub fn consume_observation(&mut self, frames: u64) -> MeterObservationClock {
        let mut remaining = frames;
        let mut run_id = None;
        let mut kind = None;
        let mut compatible = frames > 0;
        let mut timeline_endpoint_samples = None;
        let mut auxiliary_endpoint_samples = None;

        while remaining > 0 {
            let Some(front) = self.pending.front_mut() else {
                compatible = false;
                break;
            };
            if run_id.is_some_and(|id| id != front.run_id)
                || kind.is_some_and(|value| value != front.kind)
            {
                compatible = false;
            }
            run_id.get_or_insert(front.run_id);
            kind.get_or_insert(front.kind);
            let consumed = remaining.min(front.remaining_frames);
            if let Some(position) = front.next_position_samples {
                timeline_endpoint_samples = i64::try_from(consumed)
                    .ok()
                    .and_then(|frames| position.checked_add(frames));
                front.next_position_samples = timeline_endpoint_samples;
            } else {
                timeline_endpoint_samples = None;
            }
            if let Some(position) = front.next_auxiliary_samples {
                auxiliary_endpoint_samples = i64::try_from(consumed)
                    .ok()
                    .and_then(|frames| position.checked_add(frames));
                front.next_auxiliary_samples = auxiliary_endpoint_samples;
            } else {
                auxiliary_endpoint_samples = None;
            }
            front.remaining_frames -= consumed;
            remaining -= consumed;
            if front.remaining_frames == 0 {
                self.pending.pop_front();
            }
        }

        MeterObservationClock {
            run_id: run_id.unwrap_or(0),
            timeline_endpoint_samples: compatible.then_some(timeline_endpoint_samples).flatten(),
            timeline_source: if compatible {
                match kind {
                    Some(ClockKind::Exact { source, .. }) => source,
                    _ => CaptureClockSource::Unknown,
                }
            } else {
                CaptureClockSource::Unknown
            },
            auxiliary_endpoint_samples: compatible.then_some(auxiliary_endpoint_samples).flatten(),
            auxiliary_source: if compatible {
                match kind {
                    Some(ClockKind::Exact {
                        auxiliary_source, ..
                    }) => auxiliary_source,
                    _ => AuxiliaryClockSource::Unknown,
                }
            } else {
                AuxiliaryClockSource::Unknown
            },
            presentation_latency: if compatible {
                match kind {
                    Some(ClockKind::Exact {
                        presentation_latency,
                        ..
                    }) => presentation_latency,
                    _ => PresentationLatencySamples::default(),
                }
            } else {
                PresentationLatencySamples::default()
            },
            usable_for_history: compatible && remaining == 0,
        }
    }

    pub fn reset(&mut self) {
        self.pending.clear();
        self.last_input = None;
        self.next_run_id = 1;
    }

    /// Stop/resume is a provenance boundary, not a reset of session statistics.
    /// Keep queued partial observations so their mixed windows are rejected when consumed.
    pub fn break_continuity(&mut self) {
        self.last_input = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn exact(position_samples: i64, epoch: u64) -> MeterClockStart {
        MeterClockStart {
            position_samples: Some(position_samples),
            epoch: Some(epoch),
            source: CaptureClockSource::ProjectTimeline,
            ..MeterClockStart::default()
        }
    }

    #[test]
    fn arbitrary_callback_chunks_map_one_exact_observation_endpoint() {
        let mut tracker = MeterClockTracker::new();
        tracker.push_span(1_200, exact(10_000, 7));
        tracker.push_span(3_600, exact(11_200, 7));
        let point = tracker.consume_observation(4_800);
        assert!(point.usable_for_history);
        assert_eq!(point.run_id, 1);
        assert_eq!(point.timeline_endpoint_samples, Some(14_800));
        assert_eq!(point.timeline_source, CaptureClockSource::ProjectTimeline);
    }

    #[test]
    fn observation_crossing_a_transport_jump_is_not_given_a_false_endpoint() {
        let mut tracker = MeterClockTracker::new();
        tracker.push_span(2_400, exact(10_000, 1));
        tracker.push_span(2_400, exact(30_000, 2));
        let mixed = tracker.consume_observation(4_800);
        assert!(!mixed.usable_for_history);
        assert_eq!(mixed.timeline_endpoint_samples, None);
        assert_eq!(mixed.timeline_source, CaptureClockSource::Unknown);

        tracker.push_span(4_800, exact(32_400, 2));
        let resumed = tracker.consume_observation(4_800);
        assert!(resumed.usable_for_history);
        assert_eq!(resumed.run_id, 2);
        assert_eq!(resumed.timeline_endpoint_samples, Some(37_200));
    }

    #[test]
    fn unknown_host_clock_keeps_session_relative_history_usable() {
        let mut tracker = MeterClockTracker::new();
        tracker.push_span(4_800, MeterClockStart::unknown());
        let point = tracker.consume_observation(4_800);
        assert!(point.usable_for_history);
        assert_eq!(point.run_id, 1);
        assert_eq!(point.timeline_endpoint_samples, None);
    }

    #[test]
    fn studio_pro_pdc_zero_clamp_does_not_create_a_false_exact_observation() {
        // Windows Studio Pro 8.1.2, 48 kHz, 528-frame callbacks, 4096-sample delay.
        // Observed POST project starts: eight zeros, then 128, 656, ... . Raw
        // continuous time starts at -4096, but is not yet an admission authority.
        let mut tracker = MeterClockTracker::new();
        for _ in 0..8 {
            tracker.push_span(528, exact(0, 1));
        }
        for index in 0..20 {
            tracker.push_span(528, exact(128 + index * 528, 1));
        }
        let mixed = tracker.consume_observation(4_800);
        assert!(!mixed.usable_for_history);
        assert_eq!(mixed.timeline_endpoint_samples, None);
        assert_eq!(mixed.timeline_source, CaptureClockSource::Unknown);
        let contiguous = tracker.consume_observation(4_800);
        assert!(contiguous.usable_for_history);
        assert_eq!(contiguous.timeline_endpoint_samples, Some(9_600 - 4_096));
        // This only establishes a contiguous 100 ms point, NOT a 400 ms window
        // or common PRE/POST occurrence. Those remain separate requirements.
    }

    #[test]
    fn studio_pro_loop_wrap_is_a_boundary_even_without_an_epoch_change() {
        // Real PRE loop transition: a 528-frame block at 1_151_568 straddles
        // the 1_152_000-frame cycle, followed by project 96. Project alone
        // cannot describe the samples inside that straddling block.
        let mut tracker = MeterClockTracker::new();
        tracker.push_span(528, exact(1_151_568, 1));
        tracker.push_span(528, exact(96, 1));
        let mixed = tracker.consume_observation(1_056);
        assert!(!mixed.usable_for_history);
        assert_eq!(mixed.timeline_endpoint_samples, None);
    }
}
