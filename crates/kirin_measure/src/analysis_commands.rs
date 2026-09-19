//! Fixed-capacity prepared state accompanying an analysis selection generation.
//!
//! Values that do not fit in the selection word are written before that word is published.
//! The worker accepts a block only when the exact generation still names the prepared slot.

use std::sync::atomic::{AtomicI64, AtomicU64, Ordering};

use super::NO_PRESENTATION_POSITION;

const ANALYSIS_COMMAND_CAPACITY: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct AnalysisCommand {
    pub perceptual_state_epoch: Option<i64>,
}

struct AnalysisCommandSlot {
    generation: AtomicU64,
    perceptual_state_epoch: AtomicI64,
}

pub(super) struct AnalysisCommands {
    slots: [AnalysisCommandSlot; ANALYSIS_COMMAND_CAPACITY],
}

impl AnalysisCommands {
    pub fn new(initial_generation: u64) -> Self {
        let commands = Self {
            slots: std::array::from_fn(|_| AnalysisCommandSlot {
                generation: AtomicU64::new(0),
                perceptual_state_epoch: AtomicI64::new(NO_PRESENTATION_POSITION),
            }),
        };
        commands.prepare(initial_generation, None);
        commands
    }

    /// Control thread only, serialized by `SpectrumRuntime::selection_update`.
    pub fn prepare(&self, generation: u64, perceptual_state_epoch: Option<i64>) {
        let slot = &self.slots[generation as usize % ANALYSIS_COMMAND_CAPACITY];
        slot.perceptual_state_epoch.store(
            perceptual_state_epoch.unwrap_or(NO_PRESENTATION_POSITION),
            Ordering::Relaxed,
        );
        slot.generation.store(generation, Ordering::Release);
    }

    pub fn for_generation(&self, generation: u64) -> Option<AnalysisCommand> {
        let slot = &self.slots[generation as usize % ANALYSIS_COMMAND_CAPACITY];
        (slot.generation.load(Ordering::Acquire) == generation).then(|| {
            let epoch = slot.perceptual_state_epoch.load(Ordering::Acquire);
            AnalysisCommand {
                perceptual_state_epoch: (epoch != NO_PRESENTATION_POSITION).then_some(epoch),
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_overwritten_slot_never_impersonates_an_old_generation() {
        let commands = AnalysisCommands::new(1);
        commands.prepare(2, Some(4_800));
        assert_eq!(
            commands.for_generation(2),
            Some(AnalysisCommand {
                perceptual_state_epoch: Some(4_800)
            })
        );

        commands.prepare(2 + ANALYSIS_COMMAND_CAPACITY as u64, Some(9_600));
        assert_eq!(commands.for_generation(2), None);
        assert_eq!(
            commands.for_generation(2 + ANALYSIS_COMMAND_CAPACITY as u64),
            Some(AnalysisCommand {
                perceptual_state_epoch: Some(9_600)
            })
        );
    }
}
