use std::sync::TryLockError;
use std::time::{Duration, Instant};

use super::{cleanup_owned_request, SpectrumCoordinator, SpectrumViewStatus};
use crate::MidSideSpectrumFrame;

const PRESENTATION_HOLD: Duration = Duration::from_millis(250);

#[derive(Default)]
pub(super) struct MidSidePresentation {
    endpoint: Option<i64>,
    advanced_at: Option<Instant>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{SpectrumAnalyzer, SpectrumChannelMode, SpectrumRuntime, SPECTRUM_WINDOW_SIZE};
    use std::sync::Arc;

    fn frame(endpoint: i64) -> MidSideSpectrumFrame {
        let left = vec![0.25; SPECTRUM_WINDOW_SIZE];
        let right = vec![-0.125; SPECTRUM_WINDOW_SIZE];
        let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
        let mid = analyzer
            .analyze_mode(&left, Some(&right), SpectrumChannelMode::Mid, endpoint, 1)
            .unwrap();
        let side = analyzer
            .analyze_mode(&left, Some(&right), SpectrumChannelMode::Side, endpoint, 1)
            .unwrap();
        MidSideSpectrumFrame::from_frames(mid, side).unwrap()
    }

    #[test]
    fn repeated_endpoint_expires_until_a_new_frame_arrives() {
        let runtime = SpectrumRuntime::new(48_000, 2);
        let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
        let now = Instant::now();
        let first = frame(4_800);
        coordinator.store_mid_side_runtime_frame(Some(first.clone()), now);
        assert_eq!(
            coordinator.try_mid_side_view().unwrap().status,
            SpectrumViewStatus::Active
        );
        coordinator.store_mid_side_runtime_frame(
            Some(first),
            now + PRESENTATION_HOLD + Duration::from_millis(1),
        );
        let stale = coordinator.try_mid_side_view().unwrap();
        assert_eq!(stale.status, SpectrumViewStatus::WarmingUp);
        assert!(stale.frame.is_none());
        coordinator.store_mid_side_runtime_frame(
            Some(frame(6_400)),
            now + PRESENTATION_HOLD + Duration::from_millis(2),
        );
        assert_eq!(
            coordinator.try_mid_side_view().unwrap().status,
            SpectrumViewStatus::Active
        );
        coordinator.shutdown();
        runtime.shutdown_and_join();
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct MidSideSpectrumViewSnapshot {
    pub status: SpectrumViewStatus,
    pub channels: u8,
    pub frame: Option<MidSideSpectrumFrame>,
    pub analysis_owner_names: [String; crate::ANALYSIS_SLOT_COUNT],
}

impl SpectrumCoordinator {
    pub fn try_mid_side_view(&self) -> Option<MidSideSpectrumViewSnapshot> {
        match self.mid_side_view.try_lock() {
            Ok(view) => Some(view.clone()),
            Err(TryLockError::WouldBlock) => None,
            Err(TryLockError::Poisoned(poisoned)) => Some(poisoned.into_inner().clone()),
        }
    }

    pub(super) fn store_mid_side_view(
        &self,
        status: SpectrumViewStatus,
        frame: Option<MidSideSpectrumFrame>,
    ) {
        if frame.is_none() {
            *self
                .mid_side_presentation
                .lock()
                .unwrap_or_else(|p| p.into_inner()) = MidSidePresentation::default();
        }
        let mut view = self.mid_side_view.lock().unwrap_or_else(|p| p.into_inner());
        *view = MidSideSpectrumViewSnapshot {
            status,
            channels: self.runtime.num_channels() as u8,
            frame,
            analysis_owner_names: Default::default(),
        };
    }

    fn store_mid_side_runtime_frame(&self, frame: Option<MidSideSpectrumFrame>, now: Instant) {
        let mut presentation = self
            .mid_side_presentation
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        let advanced = frame
            .as_ref()
            .map(MidSideSpectrumFrame::presentation_end_samples)
            != presentation.endpoint;
        if advanced {
            presentation.endpoint = frame
                .as_ref()
                .map(MidSideSpectrumFrame::presentation_end_samples);
            presentation.advanced_at = frame.as_ref().map(|_| now);
        }
        let fresh = frame.is_some()
            && presentation.advanced_at.is_some_and(|advanced_at| {
                now.saturating_duration_since(advanced_at) < PRESENTATION_HOLD
            });
        drop(presentation);
        let mut view = self.mid_side_view.lock().unwrap_or_else(|p| p.into_inner());
        *view = MidSideSpectrumViewSnapshot {
            status: if fresh {
                SpectrumViewStatus::Active
            } else {
                SpectrumViewStatus::WarmingUp
            },
            channels: self.runtime.num_channels() as u8,
            frame: if fresh { frame } else { None },
            analysis_owner_names: Default::default(),
        };
    }

    pub(super) fn store_mid_side_in_use(
        &self,
        analysis_owner_names: [String; crate::ANALYSIS_SLOT_COUNT],
    ) {
        let mut view = self.mid_side_view.lock().unwrap_or_else(|p| p.into_inner());
        *view = MidSideSpectrumViewSnapshot {
            status: SpectrumViewStatus::InUse,
            channels: self.runtime.num_channels() as u8,
            frame: None,
            analysis_owner_names,
        };
    }

    pub(super) fn post_mid_side_tick(&self) -> bool {
        let retired = match self.post_session.try_lock() {
            Ok(mut slot) => slot
                .take()
                .map(|session| (session.target, session.request_id)),
            Err(TryLockError::WouldBlock) => return false,
            Err(TryLockError::Poisoned(poisoned)) => poisoned
                .into_inner()
                .take()
                .map(|session| (session.target, session.request_id)),
        };
        if let Some((target, request_id)) = retired {
            cleanup_owned_request(target.as_ref(), request_id);
        }
        if !self.set_active_runtime_enabled(crate::AnalysisViewMode::Spectrum, true) {
            self.store_mid_side_view(SpectrumViewStatus::Unavailable, None);
            return false;
        }
        let Some(frame) = self.runtime.try_mid_side_frame() else {
            return true;
        };
        self.store_mid_side_runtime_frame(frame, Instant::now());
        true
    }
}
