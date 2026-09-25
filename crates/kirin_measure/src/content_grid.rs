//! A content-clock aperture alongside, not in place of, the Meter Session's 100 ms history.
//! PCM is filtered once by the owning MeasureEngine. This type only tracks coverage and the
//! per-segment true peaks needed to read the same 400 ms interval on two independent instances.

use std::collections::VecDeque;

use crate::meter_clock::{ClockRunOrigin, MeterClockTracker, MeterObservationClock};
use crate::{
    AuxiliaryClockSource, CaptureClockSource, MeterClockStart, PresentationLatencySamples,
};

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct ContentWindowObservation {
    pub run_id: u64,
    pub run_origin: ClockRunOrigin,
    pub observed_frames: u64,
    pub endpoint_samples: i64,
    pub timeline_endpoint_samples: Option<i64>,
    pub timeline_source: CaptureClockSource,
    pub auxiliary_source: AuxiliaryClockSource,
    pub presentation_latency: PresentationLatencySamples,
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
}

#[derive(Clone, Copy)]
struct PeakSegment {
    cell_end: i64,
    linear_peak: f64,
}

pub(crate) struct ContentGrid {
    clock: MeterClockTracker,
    step: u64,
    window: u64,
    run: Option<u64>,
    last_end: Option<i64>,
    coverage: u64,
    peaks: VecDeque<PeakSegment>,
}

impl ContentGrid {
    pub(crate) fn new(sample_rate: u32) -> Option<Self> {
        if sample_rate == 0 || !sample_rate.is_multiple_of(10) {
            return None;
        }
        let step = u64::from(sample_rate / 10);
        Some(Self {
            clock: MeterClockTracker::new(),
            step,
            window: step * 4,
            run: None,
            last_end: None,
            coverage: 0,
            peaks: VecDeque::with_capacity(8),
        })
    }

    pub(crate) fn push_span(&mut self, frames: u64, start: MeterClockStart) {
        self.clock.push_span(frames, start);
    }

    pub(crate) fn segment_frames(&self, maximum: u64) -> u64 {
        self.clock.frames_to_content_boundary(maximum, self.step)
    }

    pub(crate) fn break_continuity(&mut self) {
        self.clock.break_continuity();
    }

    pub(crate) fn reset(&mut self) {
        self.clock.reset();
        self.clear_window();
    }

    /// Called after the segment has been submitted to the one EBU filter. `None` means this
    /// is not a complete, unbroken content window; no substitute zero or old value is emitted.
    pub(crate) fn accept_segment(
        &mut self,
        frames: u64,
        observed_frames: u64,
        linear_peak: Option<f64>,
    ) -> Option<ContentWindowObservation> {
        let clock = self.clock.consume_observation(frames);
        self.accept_clock_segment(clock, frames, observed_frames, linear_peak)
    }

    fn accept_clock_segment(
        &mut self,
        clock: MeterObservationClock,
        frames: u64,
        observed_frames: u64,
        linear_peak: Option<f64>,
    ) -> Option<ContentWindowObservation> {
        let Some((end, length)) = clock
            .auxiliary_endpoint_samples
            .zip(i64::try_from(frames).ok())
        else {
            self.clear_window();
            return None;
        };
        let Some(start) = end.checked_sub(length) else {
            self.clear_window();
            return None;
        };
        if !clock.usable_for_history
            || clock.run_id == 0
            || clock.auxiliary_source == AuxiliaryClockSource::Unknown
            || !linear_peak.is_some_and(|peak| peak.is_finite() && peak >= 0.0)
        {
            self.clear_window();
            return None;
        }
        if self.run != Some(clock.run_id) || self.last_end != Some(start) {
            self.clear_window();
            self.run = Some(clock.run_id);
        }
        self.last_end = Some(end);
        self.coverage = self.coverage.saturating_add(frames).min(self.window);
        // The DSP feeder splits at every common-grid boundary. Reduce all host-sized
        // fragments in a 100 ms cell to one TP maximum; 1-frame callbacks must not grow
        // this queue to 19,200 entries or allocate on every observation.
        let step = i64::try_from(self.step).ok()?;
        let cell_end = end
            .checked_sub(1)?
            .div_euclid(step)
            .checked_add(1)?
            .checked_mul(step)?;
        if start < cell_end.checked_sub(step)? {
            self.clear_window();
            return None;
        }
        if let Some(last) = self
            .peaks
            .back_mut()
            .filter(|last| last.cell_end == cell_end)
        {
            last.linear_peak = last.linear_peak.max(linear_peak.unwrap_or(0.0));
        } else {
            self.peaks.push_back(PeakSegment {
                cell_end,
                linear_peak: linear_peak.unwrap_or(0.0),
            });
        }
        let window_start = end.checked_sub(i64::try_from(self.window).ok()?)?;
        while self
            .peaks
            .front()
            .is_some_and(|peak| peak.cell_end <= window_start)
        {
            self.peaks.pop_front();
        }
        if self.coverage < self.window || end.rem_euclid(self.step as i64) != 0 {
            return None;
        }
        let peak = self
            .peaks
            .iter()
            .map(|segment| segment.linear_peak)
            .fold(0.0_f64, f64::max);
        Some(ContentWindowObservation {
            run_id: clock.run_id,
            run_origin: clock.run_origin,
            observed_frames,
            endpoint_samples: end,
            timeline_endpoint_samples: clock.timeline_endpoint_samples,
            timeline_source: clock.timeline_source,
            auxiliary_source: clock.auxiliary_source,
            presentation_latency: clock.presentation_latency,
            lufs_m: None,
            true_peak: (peak > 0.0).then(|| 20.0 * peak.log10()),
        })
    }

    fn clear_window(&mut self) {
        self.run = None;
        self.last_end = None;
        self.coverage = 0;
        self.peaks.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{AuxiliaryClockSamples, PresentationLatencySource};

    fn exact(auxiliary: i64) -> MeterClockStart {
        MeterClockStart {
            position_samples: Some(auxiliary),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            auxiliary: AuxiliaryClockSamples {
                source: AuxiliaryClockSource::Vst3Continuous,
                samples: Some(auxiliary),
            },
            presentation_latency: PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: None,
                output: Some(0),
            },
        }
    }

    #[test]
    fn negative_auxiliary_position_uses_euclidean_grid() {
        let mut grid = ContentGrid::new(48_000).unwrap();
        grid.push_span(528, exact(-4_096));
        assert_eq!(grid.segment_frames(528), 528);
        let _ = grid.accept_segment(528, 528, Some(0.5));
        grid.push_span(528, exact(-3_568));
        assert_eq!(grid.segment_frames(528), 528);
        let _ = grid.accept_segment(528, 1_056, Some(0.5));
        grid.push_span(4_000, exact(-3_040));
        assert_eq!(grid.segment_frames(4_000), 3_040);
    }

    #[test]
    fn a_gap_cannot_be_filled_with_a_previous_peak() {
        let mut grid = ContentGrid::new(48_000).unwrap();
        for i in 0..4 {
            grid.push_span(4_800, exact(i * 4_800));
            let point = grid.accept_segment(4_800, (i + 1) as u64 * 4_800, Some(0.5));
            assert_eq!(point.is_some(), i == 3);
        }
        grid.break_continuity();
        grid.push_span(4_800, exact(5 * 4_800));
        assert!(grid.accept_segment(4_800, 5 * 4_800, Some(0.7)).is_none());
    }

    #[test]
    fn one_frame_callbacks_keep_only_four_peak_cells() {
        let mut grid = ContentGrid::new(48_000).unwrap();
        let mut last = None;
        for frame in 0..24_000_i64 {
            grid.push_span(1, exact(frame));
            assert_eq!(grid.segment_frames(1), 1);
            last = grid
                .accept_segment(
                    1,
                    (frame + 1) as u64,
                    Some(if frame == 18_999 { 0.9 } else { 0.5 }),
                )
                .or(last);
            assert!(grid.peaks.len() <= 5);
            assert_eq!(grid.peaks.capacity(), 8);
        }
        let point = last.unwrap();
        assert_eq!(point.endpoint_samples, 24_000);
        assert!((point.true_peak.unwrap() - 20.0 * 0.9_f64.log10()).abs() < 1e-9);
    }
}
