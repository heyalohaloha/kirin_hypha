//! RT ingress identity retains exact clock definitions; FREQ joins only matching clock kinds.
use super::{AnalysisSelection, AnalysisViewMode, Ordering, SpectrumIngressBlock, SpectrumRuntime};
use crate::PresentationLatencySource;

const LOCAL_CLOCK: u64 = 1 << 63;
const VERIFIED_AAX_CLOCK: u64 = 1 << 62;
const VERIFIED_VST3_CLOCK: u64 = 1 << 61;
const VERIFIED_AUDIO_UNIT_CLOCK: u64 = 1 << 60;

/// Coordinate meaning carried by the private PRE Spectrum snapshot.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum SpectrumClockKind {
    Presentation = 1,
    ProjectTimeline = 2,
    AaxEngine = 3,
    Vst3Content = 4,
    AudioUnitRender = 5,
}

impl SpectrumClockKind {
    pub(crate) fn decode(value: u16) -> Option<Self> {
        match value {
            1 => Some(Self::Presentation),
            2 => Some(Self::ProjectTimeline),
            3 => Some(Self::AaxEngine),
            4 => Some(Self::Vst3Content),
            5 => Some(Self::AudioUnitRender),
            _ => None,
        }
    }
}

pub(super) fn definition_clock_kind(definition: u64) -> Option<SpectrumClockKind> {
    if definition & VERIFIED_AAX_CLOCK != 0 && definition_is_aligned(definition) {
        Some(SpectrumClockKind::AaxEngine)
    } else if definition & VERIFIED_VST3_CLOCK != 0 && definition_is_aligned(definition) {
        Some(SpectrumClockKind::Vst3Content)
    } else if definition & VERIFIED_AUDIO_UNIT_CLOCK != 0 && definition_is_aligned(definition) {
        Some(SpectrumClockKind::AudioUnitRender)
    } else if definition_is_aligned(definition) {
        Some(SpectrumClockKind::Presentation)
    } else if definition == (LOCAL_CLOCK | 1) {
        Some(SpectrumClockKind::ProjectTimeline)
    } else {
        None // Render-only coordinates do not establish a common project timeline.
    }
}

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
    /// Certified VST3 content clock with the current LISTEN pair proof on POST.
    VerifiedVst3 {
        start_samples: i64,
        proof_revision: u64,
    },
    /// AU render coordinate; POST requires the callback's PRE lifetime/generation/K proof.
    VerifiedAudioUnit {
        start_samples: i64,
        proof_revision: u64,
    },
    /// Native wrapper proved continuous AddClock, and for POST differences the exact pair K.
    /// This is a separate analysis authority; it is never a zero presentation-latency report.
    VerifiedAax {
        start_samples: i64,
        proof_revision: u64,
    },
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
            Self::VerifiedVst3 {
                start_samples,
                proof_revision,
            } if (1..VERIFIED_VST3_CLOCK).contains(&proof_revision) => {
                Some((start_samples, VERIFIED_VST3_CLOCK | proof_revision))
            }
            Self::VerifiedAudioUnit {
                start_samples,
                proof_revision,
            } if (1..VERIFIED_AUDIO_UNIT_CLOCK).contains(&proof_revision) => {
                Some((start_samples, VERIFIED_AUDIO_UNIT_CLOCK | proof_revision))
            }
            Self::VerifiedAax {
                start_samples,
                proof_revision,
            } if (1..VERIFIED_AAX_CLOCK).contains(&proof_revision) => {
                Some((start_samples, VERIFIED_AAX_CLOCK | proof_revision))
            }
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
            || self.clock_revision.load(Ordering::Acquire) == 0
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
        // cannot admit a local coordinate to the paired Perceptual assembler.
        let selection = self.selection.load(Ordering::Acquire);
        let decoded = AnalysisSelection::decode(selection, self.layout);
        if definition & VERIFIED_AAX_CLOCK != 0
            && decoded.is_none_or(|selection| {
                !matches!(
                    selection.mode,
                    AnalysisViewMode::Perceptual | AnalysisViewMode::Absolute
                )
            })
        {
            self.note_drop();
            return false;
        }
        if !definition_is_aligned(definition) {
            // Absolute needs a continuous local coordinate, not PRE/POST audio alignment.
            let rejected_perceptual =
                decoded.is_some_and(|selection| selection.mode == AnalysisViewMode::Perceptual);
            self.output_latency_unavailable.store(
                if rejected_perceptual { selection } else { 0 },
                Ordering::Release,
            );
            if decoded.is_none_or(|selection| {
                !matches!(
                    selection.mode,
                    AnalysisViewMode::Spectrum | AnalysisViewMode::Absolute
                )
            }) {
                self.note_drop();
                return false;
            }
        } else {
            self.output_latency_unavailable.store(0, Ordering::Release);
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
            let current = self.clock_revision.load(Ordering::Relaxed);
            let next = current.checked_add(1).unwrap_or(0);
            self.clock_revision.store(next, Ordering::Release);
            self.clock_definition.store(definition, Ordering::Release);
            if next == 0 {
                return false;
            }
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

    /// Audio Thread: a certified clock's temporary proof gap revokes facts and waits for re-arm.
    /// Host support is unchanged; Watch/Record ingress is independent.
    pub fn note_analysis_clock_drop_from_audio(&self) {
        self.output_latency_unavailable.store(0, Ordering::Release);
        self.note_drop();
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
