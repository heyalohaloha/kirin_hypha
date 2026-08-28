use super::{SpectrumCoordinator, SpectrumViewSnapshot, SpectrumViewStatus};
use crate::perceptual::PerceptualDifference;
use crate::perceptual_difference_timeline::PerceptualDifferenceTimeline;
use crate::spectrum::{AnalysisViewMode, SpectrumDifference};
use std::sync::TryLockError;

impl SpectrumCoordinator {
    pub fn try_view(&self) -> Option<SpectrumViewSnapshot> {
        match self.view.try_lock() {
            Ok(view) => Some(view.clone()),
            Err(TryLockError::WouldBlock) => None,
            Err(TryLockError::Poisoned(poisoned)) => Some(poisoned.into_inner().clone()),
        }
    }

    pub(super) fn store_view(
        &self,
        status: SpectrumViewStatus,
        difference: Option<SpectrumDifference>,
        perceptual_difference: Option<PerceptualDifference>,
    ) {
        let mut view = match self.view.lock() {
            Ok(view) => view,
            Err(poisoned) => poisoned.into_inner(),
        };
        *view = SpectrumViewSnapshot {
            status,
            analysis_mode: self.runtime.analysis_mode(),
            channel_mode: self.runtime.channel_mode(),
            channels: self.runtime.num_channels() as u8,
            difference,
            perceptual_difference,
            perceptual_timeline: PerceptualDifferenceTimeline::default(),
        };
    }

    pub(super) fn store_perceptual_view(
        &self,
        status: SpectrumViewStatus,
        differences: &[PerceptualDifference],
    ) {
        let mut view = match self.view.lock() {
            Ok(view) => view,
            Err(poisoned) => poisoned.into_inner(),
        };
        let mut timeline = if status == SpectrumViewStatus::Active
            && view.status == SpectrumViewStatus::Active
            && view.analysis_mode == AnalysisViewMode::Perceptual
        {
            std::mem::take(&mut view.perceptual_timeline)
        } else {
            PerceptualDifferenceTimeline::default()
        };
        if status == SpectrumViewStatus::Active {
            for difference in differences {
                timeline.push(*difference);
            }
        } else {
            timeline.clear();
        }
        let perceptual_difference = timeline.newest().copied();
        *view = SpectrumViewSnapshot {
            status,
            analysis_mode: self.runtime.analysis_mode(),
            channel_mode: self.runtime.channel_mode(),
            channels: self.runtime.num_channels() as u8,
            difference: None,
            perceptual_difference,
            perceptual_timeline: timeline,
        };
    }
}
