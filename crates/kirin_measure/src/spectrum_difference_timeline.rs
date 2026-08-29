use crate::spectrum::{SpectrumChannelMode, SpectrumDifference, SPECTRUM_BAND_COUNT};

/// A short recovery window for UI scheduling stalls. It reuses already-computed exact differences;
/// no FFT, interpolation, filesystem operation, or Audio Thread work is added.
pub const SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY: usize = 8;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SpectrumTimelinePushResult {
    Appended,
    DuplicateIgnored,
    StaleIgnored,
    DefinitionReset,
}

#[derive(Clone, Debug, PartialEq)]
pub struct SpectrumDifferenceTimeline {
    frames: [Option<SpectrumTimelineFrame>; SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY],
    start: usize,
    count: usize,
}

/// Presentation-only subset. The unclipped `raw_db` remains in the current measurement fact and is
/// deliberately not duplicated into the recovery ring.
#[derive(Clone, Debug, PartialEq)]
pub struct SpectrumTimelineFrame {
    pub presentation_end_samples: i64,
    pub sample_rate: u32,
    pub min_hz: f32,
    pub max_hz: f32,
    pub channel_mode: SpectrumChannelMode,
    pub channels: u8,
    pub pre_dbfs: [f32; SPECTRUM_BAND_COUNT],
    pub post_dbfs: [f32; SPECTRUM_BAND_COUNT],
    pub display_db: [f32; SPECTRUM_BAND_COUNT],
}

impl From<&SpectrumDifference> for SpectrumTimelineFrame {
    fn from(frame: &SpectrumDifference) -> Self {
        Self {
            presentation_end_samples: frame.presentation_end_samples,
            sample_rate: frame.sample_rate,
            min_hz: frame.min_hz,
            max_hz: frame.max_hz,
            channel_mode: frame.channel_mode,
            channels: frame.channels,
            pre_dbfs: frame.pre_dbfs,
            post_dbfs: frame.post_dbfs,
            display_db: frame.display_db,
        }
    }
}

impl Default for SpectrumDifferenceTimeline {
    fn default() -> Self {
        Self {
            frames: std::array::from_fn(|_| None),
            start: 0,
            count: 0,
        }
    }
}

impl SpectrumDifferenceTimeline {
    pub fn push(&mut self, difference: &SpectrumDifference) -> SpectrumTimelinePushResult {
        let frame = SpectrumTimelineFrame::from(difference);
        let mut result = SpectrumTimelinePushResult::Appended;
        if let Some(newest) = self.newest() {
            if same_definition(newest, &frame)
                && newest.presentation_end_samples == frame.presentation_end_samples
            {
                return SpectrumTimelinePushResult::DuplicateIgnored;
            }
            if same_definition(newest, &frame)
                && frame.presentation_end_samples < newest.presentation_end_samples
            {
                return SpectrumTimelinePushResult::StaleIgnored;
            }
            if !same_definition(newest, &frame) {
                self.clear();
                result = SpectrumTimelinePushResult::DefinitionReset;
            }
        }

        let destination = if self.count < SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY {
            (self.start + self.count) % SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY
        } else {
            self.start
        };
        self.frames[destination] = Some(frame);
        if self.count < SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY {
            self.count += 1;
        } else {
            self.start = (self.start + 1) % SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY;
        }
        result
    }

    pub fn clear(&mut self) {
        self.frames.fill(None);
        self.start = 0;
        self.count = 0;
    }

    pub fn newest(&self) -> Option<&SpectrumTimelineFrame> {
        self.count
            .checked_sub(1)
            .and_then(|index| self.frame_at(index))
    }

    pub fn frames(&self) -> impl Iterator<Item = &SpectrumTimelineFrame> {
        (0..self.count).filter_map(|index| self.frame_at(index))
    }

    fn frame_at(&self, index: usize) -> Option<&SpectrumTimelineFrame> {
        (index < self.count)
            .then(|| (self.start + index) % SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY)
            .and_then(|physical| self.frames[physical].as_ref())
    }
}

fn same_definition(left: &SpectrumTimelineFrame, right: &SpectrumTimelineFrame) -> bool {
    left.sample_rate == right.sample_rate
        && left.channel_mode == right.channel_mode
        && left.channels == right.channels
        && left.min_hz.to_bits() == right.min_hz.to_bits()
        && left.max_hz.to_bits() == right.max_hz.to_bits()
}

#[cfg(test)]
mod tests {
    use super::*;
    fn difference(endpoint: i64) -> SpectrumDifference {
        SpectrumDifference {
            presentation_end_samples: endpoint,
            sample_rate: 48_000,
            min_hz: 10.0,
            max_hz: 22_000.0,
            channel_mode: SpectrumChannelMode::Lr,
            channels: 2,
            pre_dbfs: [-30.0; SPECTRUM_BAND_COUNT],
            post_dbfs: [-27.0; SPECTRUM_BAND_COUNT],
            raw_db: [3.0; SPECTRUM_BAND_COUNT],
            display_db: [3.0; SPECTRUM_BAND_COUNT],
        }
    }

    #[test]
    fn fixed_eight_frame_window_recovers_short_ui_stalls_without_reanalysis() {
        let mut timeline = SpectrumDifferenceTimeline::default();
        for index in 1..=10 {
            assert_eq!(
                timeline.push(&difference(index * 1_600)),
                SpectrumTimelinePushResult::Appended
            );
        }
        let endpoints = timeline
            .frames()
            .map(|frame| frame.presentation_end_samples)
            .collect::<Vec<_>>();
        assert_eq!(
            endpoints,
            (3..=10).map(|index| index * 1_600).collect::<Vec<_>>()
        );
        assert_eq!(
            timeline.push(&difference(10 * 1_600)),
            SpectrumTimelinePushResult::DuplicateIgnored
        );
        assert_eq!(
            timeline.push(&difference(9 * 1_600)),
            SpectrumTimelinePushResult::StaleIgnored
        );
    }

    #[test]
    fn a_changed_frequency_definition_starts_one_clean_recovery_window() {
        let mut timeline = SpectrumDifferenceTimeline::default();
        timeline.push(&difference(1_600));
        let mut changed = difference(3_200);
        changed.channel_mode = SpectrumChannelMode::Mid;
        assert_eq!(
            timeline.push(&changed),
            SpectrumTimelinePushResult::DefinitionReset
        );
        assert_eq!(timeline.frames().count(), 1);
        assert_eq!(
            timeline.newest().unwrap().channel_mode,
            SpectrumChannelMode::Mid
        );
    }

    #[test]
    fn storage_is_fixed_and_has_no_growth_path() {
        assert!(std::mem::size_of::<SpectrumDifferenceTimeline>() < 32 * 1024);
    }
}
