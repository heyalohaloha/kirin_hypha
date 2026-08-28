use super::{SpectrumCoordinator, SpectrumViewSnapshot, SpectrumViewStatus};
use crate::perceptual::PerceptualDifference;
use crate::perceptual_difference_timeline::PerceptualDifferenceTimeline;
use crate::spectrum::{AnalysisViewMode, SpectrumDifference};

impl SpectrumCoordinator {
    pub fn try_view(&self) -> Option<SpectrumViewSnapshot> {
        self.view.try_lock().ok().map(|view| view.clone())
    }

    pub(super) fn store_view(
        &self,
        status: SpectrumViewStatus,
        difference: Option<SpectrumDifference>,
        perceptual_difference: Option<PerceptualDifference>,
    ) {
        if let Ok(mut view) = self.view.lock() {
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
    }

    pub(super) fn store_perceptual_view(
        &self,
        status: SpectrumViewStatus,
        differences: &[PerceptualDifference],
    ) {
        if let Ok(mut view) = self.view.lock() {
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
}
