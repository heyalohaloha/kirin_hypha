//! Generation-scoped progress before the Record engines can accept input.
use super::*;

#[derive(Debug, Default)]
pub(super) struct MeasurePreparation {
    generation: AtomicU64,
    samples: AtomicU64,
}

impl RecordStateMachine {
    /// Single Measure writer; publish the zeroed counter before its generation becomes visible.
    pub(crate) fn begin_measure_preparation(&self, generation: u64) {
        if self.measure_preparation.generation.load(Ordering::Acquire) == generation {
            return;
        }
        self.measure_preparation.samples.store(0, Ordering::Release);
        self.measure_preparation
            .generation
            .store(generation, Ordering::Release);
    }

    pub(crate) fn note_measure_preparation_samples(&self, samples: usize) {
        self.measure_preparation
            .samples
            .fetch_add(samples as u64, Ordering::Release);
    }

    #[doc(hidden)]
    pub fn measure_preparation_samples(&self, generation: u64) -> u64 {
        let state = &self.measure_preparation;
        let before = state.generation.load(Ordering::Acquire);
        let samples = state.samples.load(Ordering::Acquire);
        let after = state.generation.load(Ordering::Acquire);
        if generation != 0 && before == generation && after == generation {
            samples
        } else {
            0
        }
    }
}

#[cfg(test)]
#[path = "record_measure_preparation_tests.rs"]
mod tests;
