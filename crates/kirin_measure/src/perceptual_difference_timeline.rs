use std::collections::VecDeque;

use crate::perceptual::PerceptualDifference;

/// Six seconds are visible in the POST Sharpness page. Keep one extra 100 ms frame on each side
/// so a delayed UI poll can recover the complete factual window without interpolation.
pub const PERCEPTUAL_DIFFERENCE_TIMELINE_CAPACITY: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TimelinePushResult {
    Appended,
    DuplicateIgnored,
    StaleIgnored,
    DefinitionReset,
}

#[derive(Clone, Debug, PartialEq)]
pub struct PerceptualDifferenceTimeline {
    frames: VecDeque<PerceptualDifference>,
}

impl Default for PerceptualDifferenceTimeline {
    fn default() -> Self {
        Self {
            frames: VecDeque::with_capacity(PERCEPTUAL_DIFFERENCE_TIMELINE_CAPACITY),
        }
    }
}

impl PerceptualDifferenceTimeline {
    pub fn push(&mut self, frame: PerceptualDifference) -> TimelinePushResult {
        let mut result = TimelinePushResult::Appended;
        if let Some(newest) = self.frames.back() {
            if same_definition(newest, &frame)
                && newest.presentation_end_samples == frame.presentation_end_samples
            {
                return TimelinePushResult::DuplicateIgnored;
            }
            if same_definition(newest, &frame)
                && frame.presentation_end_samples < newest.presentation_end_samples
            {
                return TimelinePushResult::StaleIgnored;
            }
            if !same_definition(newest, &frame) {
                self.frames.clear();
                result = TimelinePushResult::DefinitionReset;
            }
        }
        if self.frames.len() == PERCEPTUAL_DIFFERENCE_TIMELINE_CAPACITY {
            self.frames.pop_front();
        }
        self.frames.push_back(frame);
        result
    }

    pub fn clear(&mut self) {
        self.frames.clear();
    }

    pub fn newest(&self) -> Option<&PerceptualDifference> {
        self.frames.back()
    }

    pub fn frames(
        &self,
    ) -> impl DoubleEndedIterator<Item = &PerceptualDifference> + ExactSizeIterator {
        self.frames.iter()
    }
}

fn same_definition(left: &PerceptualDifference, right: &PerceptualDifference) -> bool {
    left.sample_rate == right.sample_rate
        && left.aperture_samples == right.aperture_samples
        && left.state_epoch_samples == right.state_epoch_samples
        && left.channel_mode == right.channel_mode
        && left.channels == right.channels
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::spectrum::SpectrumChannelMode;

    fn difference(endpoint: i64, epoch: i64) -> PerceptualDifference {
        PerceptualDifference {
            presentation_end_samples: endpoint,
            sample_rate: 48_000,
            aperture_samples: 4_800,
            state_epoch_samples: epoch,
            channel_mode: SpectrumChannelMode::Lr,
            channels: 2,
            pre_sharpness: 1.0,
            post_sharpness: 1.2,
            delta_sharpness: 0.2,
        }
    }

    #[test]
    fn retains_a_complete_six_second_window_and_ignores_repeated_exchange_ticks() {
        let mut timeline = PerceptualDifferenceTimeline::default();
        for index in 1..=70 {
            assert_ne!(
                timeline.push(difference(index * 4_800, 0)),
                TimelinePushResult::DuplicateIgnored
            );
        }
        assert_eq!(
            timeline.frames().len(),
            PERCEPTUAL_DIFFERENCE_TIMELINE_CAPACITY
        );
        assert_eq!(
            timeline.frames().next().unwrap().presentation_end_samples,
            33_600
        );
        assert_eq!(
            timeline.push(difference(70 * 4_800, 0)),
            TimelinePushResult::DuplicateIgnored
        );
        assert_eq!(
            timeline.push(difference(69 * 4_800, 0)),
            TimelinePushResult::StaleIgnored
        );
        assert_eq!(
            timeline.frames().len(),
            PERCEPTUAL_DIFFERENCE_TIMELINE_CAPACITY
        );
    }

    #[test]
    fn a_new_state_epoch_cannot_share_the_previous_timeline() {
        let mut timeline = PerceptualDifferenceTimeline::default();
        timeline.push(difference(4_800, 0));
        assert_eq!(
            timeline.push(difference(14_400, 9_600)),
            TimelinePushResult::DefinitionReset
        );
        assert_eq!(timeline.frames().len(), 1);
    }

    #[test]
    fn repeated_sixteen_frame_exchange_windows_accumulate_without_rewinding() {
        let mut timeline = PerceptualDifferenceTimeline::default();
        for newest in 1..=70 {
            let oldest = (newest - 15).max(1);
            for index in oldest..=newest {
                timeline.push(difference(index * 4_800, 0));
            }
        }
        assert_eq!(
            timeline.frames().len(),
            PERCEPTUAL_DIFFERENCE_TIMELINE_CAPACITY
        );
        assert_eq!(
            timeline.frames().next().unwrap().presentation_end_samples,
            33_600
        );
        assert_eq!(timeline.newest().unwrap().presentation_end_samples, 336_000);
    }
}
