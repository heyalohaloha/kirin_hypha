//! Immutable audio-callback clock spans shared by Watch, Record, and exact pair observation.

use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, AtomicU8, Ordering};

pub(crate) const CAPTURE_CLOCK_SPAN_CAPACITY: usize = 4_096;

/// Exact host content-timeline provenance.
#[repr(u8)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub enum CaptureClockSource {
    #[default]
    Unknown = 0,
    ProjectTimeline = 1,
    AudioRenderTimeline = 2,
}

/// Format-owned clock that can distinguish repeated project positions inside one playback run.
/// It is evidence only; it becomes pair authority only after the join also proves PDC.
#[repr(u8)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub enum AuxiliaryClockSource {
    #[default]
    Unknown = 0,
    Vst3Continuous = 1,
    AudioUnitRender = 2,
    AaxNative = 3,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub struct AuxiliaryClockSamples {
    pub source: AuxiliaryClockSource,
    pub samples: Option<i64>,
}

/// Plug-in format that supplied the optional host presentation-latency callback.
#[repr(u8)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub enum PresentationLatencySource {
    #[default]
    Unknown = 0,
    Vst3 = 1,
    AudioUnitV2 = 2,
}

/// Host-supplied cumulative presentation latency for the active main buses.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub struct PresentationLatencySamples {
    pub source: PresentationLatencySource,
    pub input: Option<u32>,
    pub output: Option<u32>,
}

impl AuxiliaryClockSource {
    pub fn from_abi(value: u8) -> Self {
        match value {
            1 => Self::Vst3Continuous,
            2 => Self::AudioUnitRender,
            3 => Self::AaxNative,
            _ => Self::Unknown,
        }
    }
}

impl PresentationLatencySource {
    pub fn from_abi(value: u8) -> Self {
        match value {
            1 => Self::Vst3,
            2 => Self::AudioUnitV2,
            _ => Self::Unknown,
        }
    }

    pub fn as_str(self) -> Option<&'static str> {
        match self {
            Self::Unknown => None,
            Self::Vst3 => Some("vst3"),
            Self::AudioUnitV2 => Some("audio_unit_v2"),
        }
    }
}

impl CaptureClockSource {
    pub fn from_abi(value: u8) -> Self {
        match value {
            1 => Self::ProjectTimeline,
            2 => Self::AudioRenderTimeline,
            _ => Self::Unknown,
        }
    }

    pub fn as_str(self) -> Option<&'static str> {
        match self {
            Self::Unknown => None,
            Self::ProjectTimeline => Some("project_timeline"),
            Self::AudioRenderTimeline => Some("audio_render_timeline"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CaptureClockPoint {
    pub position_samples: i64,
    pub raw_host_position_samples: i64,
    pub epoch: u64,
    pub source: CaptureClockSource,
    pub presentation_latency: PresentationLatencySamples,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct CaptureClockSpan {
    pub epoch: u64,
    pub generation: u64,
    pub capture_start_frame: u64,
    pub capture_end_frame: u64,
    pub position_start_samples: Option<i64>,
    pub raw_host_position_start_samples: Option<i64>,
    pub auxiliary_start_samples: Option<i64>,
    pub source: CaptureClockSource,
    pub auxiliary_source: AuxiliaryClockSource,
    pub presentation_latency: PresentationLatencySamples,
}

impl CaptureClockSpan {
    pub(crate) fn position_for_captured_frame(self, frame: u64) -> Option<i64> {
        self.offset_at(frame, false, self.position_start_samples)
    }

    pub(crate) fn position_at_capture_boundary(self, frame: u64) -> Option<i64> {
        self.offset_at(frame, true, self.position_start_samples)
    }

    pub(crate) fn raw_host_position_at_capture_boundary(self, frame: u64) -> Option<i64> {
        self.offset_at(frame, true, self.raw_host_position_start_samples)
    }

    pub(crate) fn auxiliary_at_capture_boundary(self, frame: u64) -> Option<i64> {
        self.offset_at(frame, true, self.auxiliary_start_samples)
    }

    fn offset_at(self, frame: u64, include_start: bool, start: Option<i64>) -> Option<i64> {
        let inside = if include_start {
            frame >= self.capture_start_frame && frame < self.capture_end_frame
        } else {
            frame > self.capture_start_frame && frame <= self.capture_end_frame
        };
        inside.then_some(())?;
        start?.checked_add(i64::try_from(frame.checked_sub(self.capture_start_frame)?).ok()?)
    }
}

#[derive(Debug)]
pub(crate) struct CaptureClockSlot {
    version: AtomicU64,
    sequence: AtomicU64,
    generation: AtomicU64,
    capture_start_frame: AtomicU64,
    capture_end_frame: AtomicU64,
    position_valid: AtomicBool,
    position_start_samples: AtomicI64,
    raw_host_position_valid: AtomicBool,
    raw_host_position_start_samples: AtomicI64,
    auxiliary_valid: AtomicBool,
    auxiliary_start_samples: AtomicI64,
    source: AtomicU8,
    auxiliary_source: AtomicU8,
    presentation_source: AtomicU8,
    input_presentation_samples: AtomicU64,
    output_presentation_samples: AtomicU64,
}

impl CaptureClockSlot {
    pub(crate) fn new() -> Self {
        Self {
            version: AtomicU64::new(0),
            sequence: AtomicU64::new(0),
            generation: AtomicU64::new(0),
            capture_start_frame: AtomicU64::new(0),
            capture_end_frame: AtomicU64::new(0),
            position_valid: AtomicBool::new(false),
            position_start_samples: AtomicI64::new(i64::MIN),
            raw_host_position_valid: AtomicBool::new(false),
            raw_host_position_start_samples: AtomicI64::new(i64::MIN),
            auxiliary_valid: AtomicBool::new(false),
            auxiliary_start_samples: AtomicI64::new(i64::MIN),
            source: AtomicU8::new(CaptureClockSource::Unknown as u8),
            auxiliary_source: AtomicU8::new(AuxiliaryClockSource::Unknown as u8),
            presentation_source: AtomicU8::new(PresentationLatencySource::Unknown as u8),
            input_presentation_samples: AtomicU64::new(u64::MAX),
            output_presentation_samples: AtomicU64::new(u64::MAX),
        }
    }

    pub(crate) fn publish(&self, span: CaptureClockSpan) {
        self.version.fetch_add(1, Ordering::AcqRel);
        self.sequence.store(span.epoch, Ordering::Relaxed);
        self.generation.store(span.generation, Ordering::Relaxed);
        self.capture_start_frame
            .store(span.capture_start_frame, Ordering::Relaxed);
        self.capture_end_frame
            .store(span.capture_end_frame, Ordering::Relaxed);
        self.position_valid
            .store(span.position_start_samples.is_some(), Ordering::Relaxed);
        self.position_start_samples.store(
            span.position_start_samples.unwrap_or(i64::MIN),
            Ordering::Relaxed,
        );
        self.raw_host_position_valid.store(
            span.raw_host_position_start_samples.is_some(),
            Ordering::Relaxed,
        );
        self.raw_host_position_start_samples.store(
            span.raw_host_position_start_samples.unwrap_or(i64::MIN),
            Ordering::Relaxed,
        );
        self.auxiliary_valid
            .store(span.auxiliary_start_samples.is_some(), Ordering::Relaxed);
        self.auxiliary_start_samples.store(
            span.auxiliary_start_samples.unwrap_or(i64::MIN),
            Ordering::Relaxed,
        );
        self.source.store(span.source as u8, Ordering::Relaxed);
        self.auxiliary_source
            .store(span.auxiliary_source as u8, Ordering::Relaxed);
        self.presentation_source
            .store(span.presentation_latency.source as u8, Ordering::Relaxed);
        self.input_presentation_samples.store(
            span.presentation_latency.input.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.output_presentation_samples.store(
            span.presentation_latency.output.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.version.fetch_add(1, Ordering::Release);
    }

    pub(crate) fn extend(&self, sequence: u64, end: u64) -> bool {
        if self.sequence.load(Ordering::Acquire) != sequence {
            return false;
        }
        self.capture_end_frame.fetch_max(end, Ordering::Release);
        true
    }

    pub(crate) fn promote_watch_offline_generation(&self, sequence: u64, generation: u64) -> bool {
        generation > 0
            && self.sequence.load(Ordering::Acquire) == sequence
            && self
                .generation
                .compare_exchange(0, generation, Ordering::AcqRel, Ordering::Acquire)
                .is_ok()
    }

    pub(crate) fn read(&self, sequence: u64) -> Option<CaptureClockSpan> {
        for _ in 0..4 {
            let before = self.version.load(Ordering::Acquire);
            if before & 1 != 0 || self.sequence.load(Ordering::Acquire) != sequence {
                continue;
            }
            let optional_i64 = |valid: &AtomicBool, value: &AtomicI64| {
                valid
                    .load(Ordering::Relaxed)
                    .then(|| value.load(Ordering::Relaxed))
            };
            let span = CaptureClockSpan {
                epoch: sequence,
                generation: self.generation.load(Ordering::Relaxed),
                capture_start_frame: self.capture_start_frame.load(Ordering::Relaxed),
                capture_end_frame: self.capture_end_frame.load(Ordering::Relaxed),
                position_start_samples: optional_i64(
                    &self.position_valid,
                    &self.position_start_samples,
                ),
                raw_host_position_start_samples: optional_i64(
                    &self.raw_host_position_valid,
                    &self.raw_host_position_start_samples,
                ),
                auxiliary_start_samples: optional_i64(
                    &self.auxiliary_valid,
                    &self.auxiliary_start_samples,
                ),
                source: CaptureClockSource::from_abi(self.source.load(Ordering::Relaxed)),
                auxiliary_source: AuxiliaryClockSource::from_abi(
                    self.auxiliary_source.load(Ordering::Relaxed),
                ),
                presentation_latency: PresentationLatencySamples {
                    source: PresentationLatencySource::from_abi(
                        self.presentation_source.load(Ordering::Relaxed),
                    ),
                    input: u32::try_from(self.input_presentation_samples.load(Ordering::Relaxed))
                        .ok(),
                    output: u32::try_from(self.output_presentation_samples.load(Ordering::Relaxed))
                        .ok(),
                },
            };
            let after = self.version.load(Ordering::Acquire);
            if before == after && after & 1 == 0 {
                return Some(span);
            }
        }
        None
    }
}
