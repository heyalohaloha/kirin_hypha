//! Record clock helpers shared by the PRE/POST audio-thread entry points.
//!
//! The host may call the plugin before the exported WAV starts, or on full
//! process-block boundaries around a shorter export range. Hypha's Record
//! timeline must not use "all process() calls" as the take clock. This helper
//! keeps the audio-thread decision small and deterministic: count only the part
//! of the buffer that belongs to the WAV/native Record clock.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecordWindow {
    pub start_frame: usize,
    pub end_frame: usize,
    pub position_valid: bool,
    pub position_samples: i64,
    pub num_frames: u64,
    pub clock_start_samples: i64,
    pub clock_end_samples: Option<i64>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecordClockBounds {
    pub start_samples: i64,
    pub end_samples: Option<i64>,
    pub requires_position: bool,
}

impl RecordClockBounds {
    pub fn wav_zero() -> Self {
        Self {
            start_samples: 0,
            end_samples: None,
            requires_position: false,
        }
    }

    pub fn from_loop_range(loop_range_samples: Option<(i64, i64)>) -> Self {
        match loop_range_samples {
            Some((start, end)) if end > start => Self {
                start_samples: start,
                end_samples: Some(end),
                requires_position: true,
            },
            Some((start, _)) => Self {
                start_samples: start,
                end_samples: Some(start),
                requires_position: true,
            },
            None => Self::wav_zero(),
        }
    }
}

impl RecordWindow {
    pub fn full(num_frames: usize, position_samples: i64) -> Self {
        Self {
            start_frame: 0,
            end_frame: num_frames,
            position_valid: position_samples != i64::MIN,
            position_samples,
            num_frames: num_frames as u64,
            clock_start_samples: 0,
            clock_end_samples: None,
        }
    }

    fn empty_at(
        position_samples: i64,
        clock_start_samples: i64,
        clock_end_samples: Option<i64>,
    ) -> Self {
        Self {
            start_frame: 0,
            end_frame: 0,
            position_valid: true,
            position_samples,
            num_frames: 0,
            clock_start_samples,
            clock_end_samples,
        }
    }

    fn empty_unpositioned(clock_start_samples: i64, clock_end_samples: Option<i64>) -> Self {
        Self {
            start_frame: 0,
            end_frame: 0,
            position_valid: false,
            position_samples: i64::MIN,
            num_frames: 0,
            clock_start_samples,
            clock_end_samples,
        }
    }
}

pub fn record_clock_bounds_for_record(
    loop_range_samples: Option<(i64, i64)>,
    record_start_samples: Option<i64>,
    record_end_samples: Option<i64>,
) -> RecordClockBounds {
    let mut bounds = RecordClockBounds::from_loop_range(loop_range_samples);
    if let Some(start) = record_start_samples {
        bounds.start_samples = bounds.start_samples.max(start);
        bounds.requires_position = true;
    }
    if let Some(end) = record_end_samples {
        bounds.end_samples = Some(bounds.end_samples.map_or(end, |existing| existing.min(end)));
        bounds.requires_position = true;
    }
    bounds
}

pub fn record_window_for_buffer(
    num_frames: usize,
    position_samples: i64,
    loop_range_samples: Option<(i64, i64)>,
) -> RecordWindow {
    record_window_for_buffer_with_bounds(
        num_frames,
        position_samples,
        RecordClockBounds::from_loop_range(loop_range_samples),
    )
}

pub fn record_window_for_buffer_with_bounds(
    num_frames: usize,
    position_samples: i64,
    bounds: RecordClockBounds,
) -> RecordWindow {
    if position_samples == i64::MIN {
        if bounds.requires_position {
            return RecordWindow::empty_unpositioned(bounds.start_samples, bounds.end_samples);
        }
        return RecordWindow::full(num_frames, position_samples);
    }

    let clock_start = bounds.start_samples;
    let clock_end = bounds.end_samples;

    let block_start = position_samples;
    let block_end = position_samples.saturating_add(num_frames as i64);
    let clipped_start = block_start.max(clock_start);
    let clipped_end = clock_end.map_or(block_end, |end| block_end.min(end));
    if clipped_end <= clipped_start {
        return RecordWindow::empty_at(clipped_start, clock_start, clock_end);
    }

    let start_frame = clipped_start.saturating_sub(block_start) as usize;
    let end_frame = clipped_end.saturating_sub(block_start) as usize;
    RecordWindow {
        start_frame,
        end_frame,
        position_valid: true,
        position_samples: clipped_start,
        num_frames: clipped_end.saturating_sub(clipped_start) as u64,
        clock_start_samples: clock_start,
        clock_end_samples: clock_end,
    }
}

#[cfg(test)]
mod tests {
    use super::{
        record_clock_bounds_for_record, record_window_for_buffer,
        record_window_for_buffer_with_bounds,
    };

    #[test]
    fn clips_negative_preroll_to_wav_zero_without_loop_range() {
        let window = record_window_for_buffer(1_024, -256, None);
        assert_eq!(window.start_frame, 256);
        assert_eq!(window.end_frame, 1_024);
        assert_eq!(window.position_samples, 0);
        assert_eq!(window.num_frames, 768);
        assert_eq!(window.clock_start_samples, 0);
        assert_eq!(window.clock_end_samples, None);
    }

    #[test]
    fn drops_block_before_wav_zero_without_loop_range() {
        let window = record_window_for_buffer(512, -1_024, None);
        assert_eq!(window.start_frame, 0);
        assert_eq!(window.end_frame, 0);
        assert_eq!(window.position_samples, 0);
        assert_eq!(window.num_frames, 0);
        assert_eq!(window.clock_start_samples, 0);
        assert_eq!(window.clock_end_samples, None);
    }

    #[test]
    fn keeps_positive_block_without_loop_range() {
        let window = record_window_for_buffer(512, 96_000, None);
        assert_eq!(window.start_frame, 0);
        assert_eq!(window.end_frame, 512);
        assert_eq!(window.position_samples, 96_000);
        assert_eq!(window.num_frames, 512);
        assert_eq!(window.clock_start_samples, 0);
        assert_eq!(window.clock_end_samples, None);
    }

    #[test]
    fn loop_range_clips_start_and_end_when_host_supplies_it() {
        let window = record_window_for_buffer(512, 95_900, Some((96_000, 96_250)));
        assert_eq!(window.start_frame, 100);
        assert_eq!(window.end_frame, 350);
        assert_eq!(window.position_samples, 96_000);
        assert_eq!(window.num_frames, 250);
        assert_eq!(window.clock_start_samples, 96_000);
        assert_eq!(window.clock_end_samples, Some(96_250));
    }

    #[test]
    fn loop_range_drops_tail_after_export_end() {
        let window = record_window_for_buffer(512, 97_000, Some((96_000, 97_000)));
        assert_eq!(window.start_frame, 0);
        assert_eq!(window.end_frame, 0);
        assert_eq!(window.position_samples, 97_000);
        assert_eq!(window.num_frames, 0);
        assert_eq!(window.clock_start_samples, 96_000);
        assert_eq!(window.clock_end_samples, Some(97_000));
    }

    #[test]
    fn record_start_barrier_clips_preroll_without_loop_range() {
        let bounds = record_clock_bounds_for_record(None, Some(96_000), None);
        let window = record_window_for_buffer_with_bounds(512, 95_900, bounds);
        assert_eq!(window.start_frame, 100);
        assert_eq!(window.end_frame, 512);
        assert_eq!(window.position_samples, 96_000);
        assert_eq!(window.num_frames, 412);
        assert_eq!(window.clock_start_samples, 96_000);
        assert_eq!(window.clock_end_samples, None);
    }

    #[test]
    fn record_expected_end_drops_tail_without_loop_range() {
        let bounds = record_clock_bounds_for_record(None, Some(96_000), Some(97_000));
        let window = record_window_for_buffer_with_bounds(512, 97_000, bounds);
        assert_eq!(window.start_frame, 0);
        assert_eq!(window.end_frame, 0);
        assert_eq!(window.position_samples, 97_000);
        assert_eq!(window.num_frames, 0);
        assert_eq!(window.clock_start_samples, 96_000);
        assert_eq!(window.clock_end_samples, Some(97_000));
    }

    #[test]
    fn record_bounds_drop_unpositioned_buffer_instead_of_full_capture() {
        let bounds = record_clock_bounds_for_record(None, Some(96_000), Some(97_000));
        let window = record_window_for_buffer_with_bounds(512, i64::MIN, bounds);
        assert_eq!(window.start_frame, 0);
        assert_eq!(window.end_frame, 0);
        assert!(!window.position_valid);
        assert_eq!(window.position_samples, i64::MIN);
        assert_eq!(window.num_frames, 0);
        assert_eq!(window.clock_start_samples, 96_000);
        assert_eq!(window.clock_end_samples, Some(97_000));
    }

    #[test]
    fn unbounded_unpositioned_buffer_keeps_legacy_full_capture_fallback() {
        let bounds = record_clock_bounds_for_record(None, None, None);
        let window = record_window_for_buffer_with_bounds(512, i64::MIN, bounds);
        assert_eq!(window.start_frame, 0);
        assert_eq!(window.end_frame, 512);
        assert!(!window.position_valid);
        assert_eq!(window.position_samples, i64::MIN);
        assert_eq!(window.num_frames, 512);
        assert_eq!(window.clock_start_samples, 0);
        assert_eq!(window.clock_end_samples, None);
    }

    #[test]
    fn host_loop_and_record_bounds_intersect() {
        let bounds =
            record_clock_bounds_for_record(Some((95_000, 98_000)), Some(96_000), Some(97_000));
        let window = record_window_for_buffer_with_bounds(2_000, 95_500, bounds);
        assert_eq!(window.start_frame, 500);
        assert_eq!(window.end_frame, 1_500);
        assert_eq!(window.position_samples, 96_000);
        assert_eq!(window.num_frames, 1_000);
        assert_eq!(window.clock_start_samples, 96_000);
        assert_eq!(window.clock_end_samples, Some(97_000));
    }
}
