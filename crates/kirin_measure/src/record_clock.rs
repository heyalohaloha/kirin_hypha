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
}

pub fn record_window_for_buffer(
    num_frames: usize,
    position_samples: i64,
    loop_range_samples: Option<(i64, i64)>,
) -> RecordWindow {
    if position_samples == i64::MIN {
        return RecordWindow::full(num_frames, position_samples);
    }

    let (clock_start, clock_end) = match loop_range_samples {
        Some((start, end)) if end > start => (start, Some(end)),
        Some((start, _)) => return RecordWindow::empty_at(start, start, None),
        None => (0, None),
    };

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
    use super::record_window_for_buffer;

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
}
