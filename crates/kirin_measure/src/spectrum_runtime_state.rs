use std::collections::VecDeque;

use crate::perceptual::PerceptualFrame;
use crate::spectrum::{AnalysisViewMode, SpectrumChannelMode, SpectrumFrame};

pub const SPECTRUM_HISTORY_CAPACITY: usize = 8;
pub const PERCEPTUAL_HISTORY_CAPACITY: usize = 16;

#[derive(Clone, Debug, Default)]
pub struct SpectrumHistory {
    frames: VecDeque<SpectrumFrame>,
}

#[derive(Clone, Debug, Default)]
pub struct PerceptualHistory {
    frames: VecDeque<PerceptualFrame>,
}

impl PerceptualHistory {
    pub(crate) fn with_capacity() -> Self {
        Self {
            frames: VecDeque::with_capacity(PERCEPTUAL_HISTORY_CAPACITY),
        }
    }

    pub(crate) fn push(&mut self, frame: PerceptualFrame) {
        if self.frames.len() == PERCEPTUAL_HISTORY_CAPACITY {
            self.frames.pop_front();
        }
        self.frames.push_back(frame);
    }

    pub fn newest(&self) -> Option<&PerceptualFrame> {
        self.frames.back()
    }

    pub fn matching_presentation_end(
        &self,
        presentation_end_samples: i64,
    ) -> Option<&PerceptualFrame> {
        self.frames
            .iter()
            .rev()
            .find(|frame| frame.presentation_end_samples == presentation_end_samples)
    }

    pub fn frames(&self) -> impl DoubleEndedIterator<Item = &PerceptualFrame> + ExactSizeIterator {
        self.frames.iter()
    }
}

impl SpectrumHistory {
    pub(crate) fn with_capacity() -> Self {
        Self {
            frames: VecDeque::with_capacity(SPECTRUM_HISTORY_CAPACITY),
        }
    }

    pub(crate) fn push(&mut self, frame: SpectrumFrame) {
        if self.frames.len() == SPECTRUM_HISTORY_CAPACITY {
            self.frames.pop_front();
        }
        self.frames.push_back(frame);
    }

    pub fn newest(&self) -> Option<&SpectrumFrame> {
        self.frames.back()
    }

    pub fn matching_presentation_end(
        &self,
        presentation_end_samples: i64,
    ) -> Option<&SpectrumFrame> {
        self.frames
            .iter()
            .rev()
            .find(|frame| frame.presentation_end_samples == presentation_end_samples)
    }

    pub fn frames(&self) -> impl DoubleEndedIterator<Item = &SpectrumFrame> + ExactSizeIterator {
        self.frames.iter()
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct SpectrumRuntimeStats {
    pub enabled: bool,
    pub worker_running: bool,
    pub analysis_mode: AnalysisViewMode,
    pub channel_mode: SpectrumChannelMode,
    pub channels: u8,
    pub pushed_blocks: u64,
    pub dropped_blocks: u64,
    pub analyzed_frames: u64,
    pub analyzed_perceptual_frames: u64,
}
