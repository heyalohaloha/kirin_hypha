//! One atomic identity for optional analysis input, mode, and generation.
//!
//! The audio thread copies the encoded word into each ingress block. The worker derives every
//! input position, channel count, and frame label from that captured word; it never combines a
//! current control value with an older block.

use std::sync::atomic::Ordering;

use super::SpectrumRuntime;
use crate::channel_layout::{ChannelLayout, SpectrumView};
use crate::spectrum::{AnalysisViewMode, SpectrumChannelMode};

const VIEW_MASK: u64 = 0xff;
const MODE_SHIFT: u32 = 8;
const MODE_MASK: u64 = 0x3;
const MID_SIDE_SHIFT: u32 = 10;
const GENERATION_SHIFT: u32 = 11;
pub(super) const MAX_SELECTION_GENERATION: u64 = u64::MAX >> GENERATION_SHIFT;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct AnalysisSelection {
    pub generation: u64,
    pub mode: AnalysisViewMode,
    pub view: SpectrumView,
    pub mid_side: bool,
}

impl AnalysisSelection {
    pub fn initial(layout: ChannelLayout) -> Self {
        Self {
            generation: 1,
            mode: AnalysisViewMode::Spectrum,
            view: SpectrumView::default_for(layout),
            mid_side: false,
        }
    }

    pub fn encode(self) -> u64 {
        debug_assert!(self.generation > 0 && self.generation <= MAX_SELECTION_GENERATION);
        (self.generation << GENERATION_SHIFT)
            | ((self.mid_side as u64) << MID_SIDE_SHIFT)
            | ((self.mode as u64) << MODE_SHIFT)
            | u64::from(self.view.to_abi())
    }

    pub fn decode(encoded: u64, layout: ChannelLayout) -> Option<Self> {
        let generation = encoded >> GENERATION_SHIFT;
        let mode = AnalysisViewMode::try_from(((encoded >> MODE_SHIFT) & MODE_MASK) as u8).ok()?;
        let view = SpectrumView::from_abi((encoded & VIEW_MASK) as u8)?;
        let mid_side = ((encoded >> MID_SIDE_SHIFT) & 1) != 0;
        let selection = Self {
            generation,
            mode,
            view,
            mid_side,
        };
        (generation != 0 && selection.is_valid_for(layout)).then_some(selection)
    }

    pub fn is_valid_for(self, layout: ChannelLayout) -> bool {
        self.view.is_available_in(layout)
            && (!self.mid_side
                || (self.mode == AnalysisViewMode::Spectrum
                    && self.view.analysis_channels(layout) == 2))
    }

    pub fn next_generation(self) -> Option<Self> {
        (self.generation < MAX_SELECTION_GENERATION).then_some(Self {
            generation: self.generation + 1,
            ..self
        })
    }

    pub fn same_configuration(self, other: Self) -> bool {
        self.mode == other.mode && self.view == other.view && self.mid_side == other.mid_side
    }

    pub fn channel_mode(self) -> SpectrumChannelMode {
        match self.view {
            SpectrumView::Mid => SpectrumChannelMode::Mid,
            SpectrumView::Side => SpectrumChannelMode::Side,
            _ => SpectrumChannelMode::Lr,
        }
    }

    pub fn analysis_channels(self, layout: ChannelLayout) -> usize {
        self.view.analysis_channels(layout)
    }
}

impl SpectrumRuntime {
    pub(super) fn selection(&self) -> AnalysisSelection {
        AnalysisSelection::decode(self.selection.load(Ordering::Acquire), self.layout)
            .expect("SpectrumRuntime always owns a valid encoded selection")
    }

    /// Publish a new configuration and generation in one CAS. `None` is a rejected request;
    /// `Some(false)` is an already-current request; `Some(true)` is a newly-published selection.
    pub(super) fn update_selection(
        &self,
        revise: impl Fn(AnalysisSelection) -> Option<AnalysisSelection>,
    ) -> Option<bool> {
        self.publish_selection(revise, None, false)
    }

    pub(super) fn advance_selection_generation(&self) -> bool {
        self.publish_selection(Some, None, true) == Some(true)
    }

    pub(super) fn update_perceptual_epoch(&self, epoch: Option<i64>) -> Option<bool> {
        self.publish_selection(Some, Some(epoch), true)
    }

    fn publish_selection(
        &self,
        revise: impl Fn(AnalysisSelection) -> Option<AnalysisSelection>,
        perceptual_epoch_override: Option<Option<i64>>,
        force_generation: bool,
    ) -> Option<bool> {
        let _update = self.selection_update.lock().ok()?;
        let encoded = self.selection.load(Ordering::Acquire);
        let current = AnalysisSelection::decode(encoded, self.layout)?;
        let revised = revise(current)?;
        if current.same_configuration(revised) && !force_generation {
            return Some(false);
        }
        let next = AnalysisSelection {
            generation: current.next_generation()?.generation,
            ..revised
        };
        if !next.is_valid_for(self.layout) {
            return None;
        }
        let current_epoch = self
            .requested_perceptual_state_epoch
            .load(Ordering::Acquire);
        let epoch = perceptual_epoch_override.unwrap_or_else(|| {
            (next.mode == AnalysisViewMode::Perceptual
                && current_epoch != super::NO_PRESENTATION_POSITION)
                .then_some(current_epoch)
        });
        self.analysis_commands.prepare(next.generation, epoch);
        self.selection
            .compare_exchange(encoded, next.encode(), Ordering::AcqRel, Ordering::Acquire)
            .ok()?;
        self.requested_perceptual_state_epoch.store(
            epoch.unwrap_or(super::NO_PRESENTATION_POSITION),
            Ordering::Release,
        );
        Some(true)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::channel_layout::{ChannelRole, LayoutId};

    #[test]
    fn every_valid_selection_round_trips_as_one_word() {
        for layout in [
            ChannelLayout::mono(),
            ChannelLayout::stereo(),
            ChannelLayout::by_id(LayoutId::Surround5_1),
            ChannelLayout::by_id(LayoutId::Surround7_1_4),
        ] {
            for view in SpectrumView::available_in(layout) {
                for mode in [
                    AnalysisViewMode::Spectrum,
                    AnalysisViewMode::Perceptual,
                    AnalysisViewMode::Absolute,
                    AnalysisViewMode::Attack,
                ] {
                    let selection = AnalysisSelection {
                        generation: 91,
                        mode,
                        view,
                        mid_side: false,
                    };
                    assert_eq!(
                        AnalysisSelection::decode(selection.encode(), layout),
                        Some(selection)
                    );
                }
            }
        }
    }

    #[test]
    fn derived_channel_mode_and_mid_side_validation_have_one_source() {
        let stereo = ChannelLayout::stereo();
        for (view, expected) in [
            (SpectrumView::Lr, SpectrumChannelMode::Lr),
            (SpectrumView::Mid, SpectrumChannelMode::Mid),
            (SpectrumView::Side, SpectrumChannelMode::Side),
            (
                SpectrumView::Channel(ChannelRole::Right),
                SpectrumChannelMode::Lr,
            ),
        ] {
            let selection = AnalysisSelection {
                generation: 1,
                mode: AnalysisViewMode::Spectrum,
                view,
                mid_side: false,
            };
            assert_eq!(selection.channel_mode(), expected);
            assert_eq!(
                selection.analysis_channels(stereo),
                view.analysis_channels(stereo)
            );
        }

        let mid_side = AnalysisSelection {
            generation: 1,
            mode: AnalysisViewMode::Spectrum,
            view: SpectrumView::Lr,
            mid_side: true,
        };
        assert_eq!(
            AnalysisSelection::decode(mid_side.encode(), stereo),
            Some(mid_side)
        );
        assert!(AnalysisSelection::decode(mid_side.encode(), ChannelLayout::mono()).is_none());
    }

    #[test]
    fn generation_never_wraps_or_reuses_zero() {
        let at_limit = AnalysisSelection {
            generation: MAX_SELECTION_GENERATION,
            mode: AnalysisViewMode::Spectrum,
            view: SpectrumView::Lr,
            mid_side: false,
        };
        assert!(at_limit.next_generation().is_none());
        assert!(AnalysisSelection::decode(at_limit.encode(), ChannelLayout::stereo()).is_some());
        assert!(AnalysisSelection::decode(0, ChannelLayout::stereo()).is_none());
    }

    #[test]
    fn runtime_rejects_a_change_when_the_generation_space_is_exhausted() {
        let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
        let at_limit = AnalysisSelection {
            generation: MAX_SELECTION_GENERATION,
            mode: AnalysisViewMode::Spectrum,
            view: SpectrumView::Lr,
            mid_side: false,
        };
        runtime
            .selection
            .store(at_limit.encode(), Ordering::Release);

        assert!(!runtime.set_view(SpectrumView::Mid));
        assert_eq!(runtime.selection(), at_limit);
    }
}
