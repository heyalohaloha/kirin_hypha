//! RT ingress clock authority is private; local observations never acquire pair alignment.
use super::{AnalysisSelection, AnalysisViewMode, Ordering, SpectrumIngressBlock, SpectrumRuntime};
use crate::PresentationLatencySource;

const LOCAL_CLOCK: u64 = 1 << 63;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SpectrumInputClock {
    /// Existing internal callers already supply an output-presentation coordinate.
    LegacyPresentation(i64),
    Presentation {
        start_samples: i64,
        source: PresentationLatencySource,
        output_latency_samples: u32,
    },
    LocalProject(i64),
    LocalRender(i64),
}

impl SpectrumInputClock {
    fn coordinate_and_definition(self) -> Option<(i64, u64)> {
        match self {
            Self::LegacyPresentation(start) => Some((start, 0)),
            Self::Presentation {
                start_samples,
                source,
                output_latency_samples,
            } if matches!(
                source,
                PresentationLatencySource::Vst3 | PresentationLatencySource::AudioUnitV2
            ) =>
            {
                Some((
                    start_samples,
                    ((source as u64) << 32) | u64::from(output_latency_samples),
                ))
            }
            Self::LocalProject(start) => Some((start, LOCAL_CLOCK | 1)),
            Self::LocalRender(start) => Some((start, LOCAL_CLOCK | 2)),
            _ => None,
        }
    }
}

pub(super) fn definition_is_aligned(definition: u64) -> bool {
    definition & LOCAL_CLOCK == 0
}

impl SpectrumRuntime {
    /// Existing callers retain their output-presentation clock contract.
    pub fn push_block_from_audio(
        &self,
        interleaved: &[f32],
        num_channels: usize,
        presentation_start_samples: Option<i64>,
    ) -> bool {
        self.push_block_from_audio_with_clock(
            interleaved,
            num_channels,
            presentation_start_samples.map(SpectrumInputClock::LegacyPresentation),
        )
    }

    /// Audio Thread only: no allocation, lock, I/O, sleep, or analysis.
    pub fn push_block_from_audio_with_clock(
        &self,
        interleaved: &[f32],
        num_channels: usize,
        clock: Option<SpectrumInputClock>,
    ) -> bool {
        if !self.enabled.load(Ordering::Relaxed)
            || self.stream_generation.load(Ordering::Acquire) == 0
        {
            return false;
        }
        let Some((presentation_start_samples, definition)) =
            clock.and_then(SpectrumInputClock::coordinate_and_definition)
        else {
            self.note_drop();
            return false;
        };
        // Carry the same selection checked here into the descriptor. A concurrent mode change
        // cannot admit a local coordinate to a state-epoch Perceptual/Absolute assembler.
        let selection = self.selection.load(Ordering::Acquire);
        if !definition_is_aligned(definition)
            && AnalysisSelection::decode(selection, self.layout)
                .is_none_or(|selection| selection.mode != AnalysisViewMode::Spectrum)
        {
            self.note_drop();
            return false;
        }
        if num_channels != self.num_channels
            || interleaved.is_empty()
            || !interleaved.len().is_multiple_of(num_channels)
        {
            self.note_drop();
            return false;
        }
        let frames = interleaved.len() / num_channels;
        let Ok(frames_u32) = u32::try_from(frames) else {
            self.note_drop();
            return false;
        };
        let Some(presentation_end_samples) =
            presentation_start_samples.checked_add(i64::from(frames_u32))
        else {
            self.note_drop();
            return false;
        };
        if self.clock_definition.load(Ordering::Acquire) != definition {
            // Invalidate before changing authority; no block for this intermediate generation
            // has been published. Snapshot identity checks reject every previous frame.
            if !self.advance_stream_generation() {
                return false;
            }
            self.clock_definition.store(definition, Ordering::Release);
        }
        let stream_generation = self.stream_generation.load(Ordering::Acquire);
        self.latest_presentation_end
            .store(presentation_end_samples, Ordering::Release);
        // SAFETY: both rings have the one Audio Thread as their sole producer.
        let sample_producer = unsafe { &mut *self.sample_producer.get() };
        // SAFETY: same sole-producer contract.
        let block_producer = unsafe { &mut *self.block_producer.get() };
        if sample_producer.slots() < interleaved.len() || block_producer.slots() == 0 {
            self.note_drop();
            return false;
        }
        // Samples are committed before their descriptor, as in the existing ingress contract.
        for sample in interleaved {
            let _ = sample_producer.push(*sample);
        }
        let _ = block_producer.push(SpectrumIngressBlock {
            frames: frames_u32,
            channels: num_channels as u8,
            presentation_start_samples,
            selection,
            stream_generation,
        });
        self.pushed_blocks.fetch_add(1, Ordering::Relaxed);
        true
    }

    fn advance_stream_generation(&self) -> bool {
        let current = self.stream_generation.load(Ordering::Relaxed);
        let next = if current == 0 {
            0
        } else {
            current.checked_add(1).unwrap_or(0)
        };
        self.stream_generation
            .compare_exchange(current, next, Ordering::AcqRel, Ordering::Relaxed)
            .is_ok()
            && next != 0
    }

    pub(super) fn note_drop(&self) {
        self.dropped_blocks.fetch_add(1, Ordering::Relaxed);
        self.advance_stream_generation();
        if self.analysis_mode() == AnalysisViewMode::Perceptual {
            self.perceptual_rearm_required
                .store(true, Ordering::Release);
        }
    }
}
