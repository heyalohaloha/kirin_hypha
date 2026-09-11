use std::sync::TryLockError;

use super::{cleanup_owned_request, SpectrumCoordinator, SpectrumViewStatus};
use crate::MidSideSpectrumFrame;

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
        let mut view = self.mid_side_view.lock().unwrap_or_else(|p| p.into_inner());
        *view = MidSideSpectrumViewSnapshot {
            status,
            channels: self.runtime.num_channels() as u8,
            frame,
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
        let status = if frame.is_some() {
            SpectrumViewStatus::Active
        } else {
            SpectrumViewStatus::WarmingUp
        };
        self.store_mid_side_view(status, frame);
        true
    }
}
