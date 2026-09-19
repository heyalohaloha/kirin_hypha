use std::collections::VecDeque;
use std::sync::atomic::Ordering;

use super::{SpectrumRuntime, NO_PRESENTATION_POSITION};
use crate::perceptual::PerceptualFrame;
use crate::spectrum::{AnalysisViewMode, SpectrumChannelMode, SpectrumFrame};

pub const SPECTRUM_HISTORY_CAPACITY: usize = 8;
pub const PERCEPTUAL_HISTORY_CAPACITY: usize = 16;

#[derive(Clone, Debug, Default, PartialEq)]
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
        if !frame.has_valid_layout() {
            return;
        }
        if self.frames.back().is_some_and(|newest| {
            !newest.same_analysis_layout(&frame) || newest.generation != frame.generation
        }) {
            self.frames.clear();
        }
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
    pub analyzed_absolute_frames: u64,
    pub analyzed_mid_side_frames: u64,
}

impl SpectrumRuntime {
    pub fn try_history(&self) -> Option<SpectrumHistory> {
        let stream = self.stream_generation.load(Ordering::Acquire);
        let history = self.history.try_lock().ok()?.clone();
        let empty = history.newest().is_none();
        (stream != 0
            && stream == self.stream_generation.load(Ordering::Acquire)
            && (empty || stream == self.history_stream_generation.load(Ordering::Acquire)))
        .then_some(history)
    }

    pub fn try_perceptual_history(&self) -> Option<PerceptualHistory> {
        let stream = self.stream_generation.load(Ordering::Acquire);
        let history = self.perceptual_history.try_lock().ok()?.clone();
        let empty = history.newest().is_none();
        (stream != 0
            && stream == self.stream_generation.load(Ordering::Acquire)
            && (empty
                || stream
                    == self
                        .perceptual_history_stream_generation
                        .load(Ordering::Acquire)))
        .then_some(history)
    }

    pub fn try_absolute_history(&self) -> Option<crate::AbsoluteTimeline> {
        let stream = self.stream_generation.load(Ordering::Acquire);
        let history = self.absolute_history.try_lock().ok()?.clone();
        let empty = history.newest().is_none();
        (stream != 0
            && stream == self.stream_generation.load(Ordering::Acquire)
            && (empty
                || stream
                    == self
                        .absolute_history_stream_generation
                        .load(Ordering::Acquire)))
        .then_some(history)
    }

    pub fn try_mid_side_frame(&self) -> Option<Option<crate::MidSideSpectrumFrame>> {
        let stream = self.stream_generation.load(Ordering::Acquire);
        let frame = self.latest_mid_side.try_lock().ok()?.clone();
        let empty = frame.is_none();
        (stream != 0
            && stream == self.stream_generation.load(Ordering::Acquire)
            && (empty || stream == self.mid_side_stream_generation.load(Ordering::Acquire)))
        .then_some(frame)
    }

    pub fn stats(&self) -> SpectrumRuntimeStats {
        SpectrumRuntimeStats {
            enabled: self.enabled.load(Ordering::Acquire),
            worker_running: self.worker_running.load(Ordering::Acquire),
            analysis_mode: self.analysis_mode(),
            channel_mode: self.channel_mode(),
            channels: self.num_channels as u8,
            pushed_blocks: self.pushed_blocks.load(Ordering::Relaxed),
            dropped_blocks: self.dropped_blocks.load(Ordering::Relaxed),
            analyzed_frames: self.analyzed_frames.load(Ordering::Relaxed),
            analyzed_perceptual_frames: self.analyzed_perceptual_frames.load(Ordering::Relaxed),
            analyzed_absolute_frames: self.analyzed_absolute_frames.load(Ordering::Relaxed),
            analyzed_mid_side_frames: self.analyzed_mid_side_frames.load(Ordering::Relaxed),
        }
    }

    pub fn latest_presentation_end(&self) -> Option<i64> {
        let value = self.latest_presentation_end.load(Ordering::Acquire);
        (value != NO_PRESENTATION_POSITION).then_some(value)
    }

    pub fn perceptual_state_epoch(&self) -> Option<i64> {
        let value = self
            .requested_perceptual_state_epoch
            .load(Ordering::Acquire);
        (value != NO_PRESENTATION_POSITION).then_some(value)
    }

    pub fn applied_selection_generation(&self) -> Option<u64> {
        let encoded = self.applied_selection.load(Ordering::Acquire);
        super::AnalysisSelection::decode(encoded, self.layout).map(|selection| selection.generation)
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
        let previous = self
            .requested_perceptual_state_epoch
            .load(Ordering::Acquire);
        if previous != encoded {
            if epoch.is_some() && self.analysis_mode() != AnalysisViewMode::Perceptual {
                return false;
            }
            if self.update_perceptual_epoch(epoch) != Some(true) {
                return false;
            }
            self.perceptual_rearm_required
                .store(false, Ordering::Release);
            if let Ok(mut history) = self.perceptual_history.lock() {
                *history = PerceptualHistory::with_capacity();
            }
            if let Ok(mut history) = self.absolute_history.lock() {
                history.clear();
            }
            self.wake.1.notify_all();
        }
        true
    }

    pub fn take_perceptual_rearm_required(&self) -> bool {
        self.perceptual_rearm_required.swap(false, Ordering::AcqRel)
    }

    /// Control/worker thread only. Spectrum, Perceptual, and Absolute analysis are exclusive.
    pub fn set_analysis_mode(&self, mode: AnalysisViewMode) -> bool {
        let Some(changed) = self.update_selection(|current| {
            Some(super::AnalysisSelection {
                mode,
                mid_side: current.mid_side && mode == AnalysisViewMode::Spectrum,
                ..current
            })
        }) else {
            return false;
        };
        if changed {
            self.perceptual_rearm_required
                .store(false, Ordering::Release);
            if let Ok(mut history) = self.history.lock() {
                *history = SpectrumHistory::with_capacity();
            }
            if let Ok(mut history) = self.perceptual_history.lock() {
                *history = PerceptualHistory::with_capacity();
            }
            if let Ok(mut history) = self.absolute_history.lock() {
                history.clear();
            }
            self.clear_mid_side_frame();
            self.wake.1.notify_all();
        }
        true
    }
}
