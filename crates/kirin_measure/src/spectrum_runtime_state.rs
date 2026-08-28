use std::collections::VecDeque;
use std::sync::atomic::Ordering;

use super::{SpectrumRuntime, NO_PRESENTATION_POSITION};
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

impl SpectrumRuntime {
    pub fn latest_presentation_end(&self) -> Option<i64> {
        let value = self.latest_presentation_end.load(Ordering::Acquire);
        (value != NO_PRESENTATION_POSITION).then_some(value)
    }

    pub fn perceptual_state_epoch(&self) -> Option<i64> {
        let value = self.perceptual_state_epoch.load(Ordering::Acquire);
        (value != NO_PRESENTATION_POSITION).then_some(value)
    }

    /// Control/worker thread only. `None` arms ingress without allowing stateful analysis.
    pub fn set_perceptual_state_epoch(&self, epoch: Option<i64>) -> bool {
        let encoded = epoch.unwrap_or(NO_PRESENTATION_POSITION);
        if epoch.is_some_and(|value| {
            let aperture = i64::from(self.sample_rate / crate::PERCEPTUAL_PRESENTATION_HZ);
            aperture <= 0 || value.rem_euclid(aperture) != 0
        }) {
            return false;
        }
        let previous = self.perceptual_state_epoch.swap(encoded, Ordering::AcqRel);
        if previous != encoded {
            self.generation.fetch_add(1, Ordering::AcqRel);
            self.perceptual_rearm_required
                .store(false, Ordering::Release);
            if let Ok(mut history) = self.perceptual_history.lock() {
                *history = PerceptualHistory::with_capacity();
            }
            self.wake.1.notify_all();
        }
        true
    }

    pub fn take_perceptual_rearm_required(&self) -> bool {
        self.perceptual_rearm_required.swap(false, Ordering::AcqRel)
    }

    /// Control/worker thread only. Spectrum and Perceptual analysis are intentionally exclusive.
    pub fn set_analysis_mode(&self, mode: AnalysisViewMode) -> bool {
        let previous = self.analysis_mode.swap(mode as u8, Ordering::AcqRel);
        if previous != mode as u8 {
            self.generation.fetch_add(1, Ordering::AcqRel);
            if mode != AnalysisViewMode::Perceptual {
                self.perceptual_state_epoch
                    .store(NO_PRESENTATION_POSITION, Ordering::Release);
            }
            self.perceptual_rearm_required
                .store(false, Ordering::Release);
            if let Ok(mut history) = self.history.lock() {
                *history = SpectrumHistory::with_capacity();
            }
            if let Ok(mut history) = self.perceptual_history.lock() {
                *history = PerceptualHistory::with_capacity();
            }
            self.wake.1.notify_all();
        }
        true
    }
}
