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
        self.store_view_with_spectrum_boundary(status, difference, perceptual_difference, false);
    }

    /// Replaces the short UI-recovery timeline at one confirmed transport boundary.
    ///
    /// A lower presentation endpoint can be either a late worker result or a real backwards
    /// transport move. The join layer calls this path only after the newest verified PRE and POST
    /// endpoints have both crossed below the last published endpoint. They may be one cadence
    /// apart; the boundary itself still starts on an exact shared endpoint.
    pub(super) fn store_spectrum_boundary(&self, difference: SpectrumDifference) {
        self.store_view_with_spectrum_boundary(
            SpectrumViewStatus::Active,
            Some(difference),
            None,
            true,
        );
    }

    fn store_view_with_spectrum_boundary(
        &self,
        status: SpectrumViewStatus,
        difference: Option<SpectrumDifference>,
        perceptual_difference: Option<PerceptualDifference>,
        reset_spectrum_timeline: bool,
    ) {
        let mut view = match self.view.lock() {
            Ok(view) => view,
            Err(poisoned) => poisoned.into_inner(),
        };
        let continuing_spectrum = !reset_spectrum_timeline
            && status == SpectrumViewStatus::Active
            && view.status == SpectrumViewStatus::Active
            && view.analysis_mode == crate::AnalysisViewMode::Spectrum;
        let previous_difference = if continuing_spectrum {
            view.difference.take()
        } else {
            None
        };
        let mut spectrum_timeline = if continuing_spectrum {
            std::mem::take(&mut view.spectrum_timeline)
        } else {
            Default::default()
        };
        let mut published_difference = difference;
        if let Some(frame) = published_difference.as_ref() {
            let result = spectrum_timeline.push(frame);
            if matches!(
                result,
                crate::SpectrumTimelinePushResult::DuplicateIgnored
                    | crate::SpectrumTimelinePushResult::StaleIgnored
            ) {
                // A presentation endpoint is immutable. Repeated or stale worker results must
                // never replace the exact fact that the UI has already observed.
                published_difference = previous_difference;
            }
        } else {
            spectrum_timeline.clear();
        }
        *view = SpectrumViewSnapshot {
            status,
            analysis_mode: self.runtime.analysis_mode(),
            channel_mode: self.runtime.channel_mode(),
            channels: self.runtime.num_channels() as u8,
            difference: published_difference,
            spectrum_timeline,
            perceptual_difference,
            perceptual_timeline: PerceptualDifferenceTimeline::default(),
            absolute_timeline: Default::default(),
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
            spectrum_timeline: Default::default(),
            perceptual_difference,
            perceptual_timeline: timeline,
            absolute_timeline: Default::default(),
        };
    }

    pub(super) fn store_absolute_view(
        &self,
        status: SpectrumViewStatus,
        timeline: crate::AbsoluteTimeline,
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
            difference: None,
            spectrum_timeline: Default::default(),
            perceptual_difference: None,
            perceptual_timeline: Default::default(),
            absolute_timeline: timeline,
        };
    }
}
