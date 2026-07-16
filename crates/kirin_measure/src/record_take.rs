//! Record take duration tracker.
//!
//! Audio Thread owns the writes through atomics only. IO Thread reads a snapshot
//! when Record closes and uses it as the clean bounce take duration. The tracker
//! deliberately measures the WAV/native clock span when the host exposes one,
//! and keeps the raw render span as a lower-trust fallback.

use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, AtomicU8, Ordering};
use std::sync::Arc;

pub const RECORD_TAKE_SOURCE_RENDER_CLOCK: &str = "render_clock_native";
pub const RECORD_TAKE_SOURCE_WAV_CLOCK: &str = "wav_clock_native";

const CAPTURE_CLOCK_SPAN_CAPACITY: usize = 4_096;

/// Host sample clock provenance carried from the audio callback to the completed TRACE.
///
/// Both variants are exact sample clocks. `ProjectTimeline` is the DAW transport timeline
/// (VST3 projectTimeSamples / AU host transport callback). `AudioRenderTimeline` is the AU
/// render timestamp used when a host omits its optional transport callback.
#[repr(u8)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub enum CaptureClockSource {
    #[default]
    Unknown = 0,
    ProjectTimeline = 1,
    AudioRenderTimeline = 2,
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
/// `None` means the format wrapper never received the optional host callback; `Some(0)` preserves
/// the standard's intentionally ambiguous zero without pretending it was absent.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub struct PresentationLatencySamples {
    pub source: PresentationLatencySource,
    pub input: Option<u32>,
    pub output: Option<u32>,
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
    /// Canonical WAV presentation-sample boundary. This is the only position downstream TRACE
    /// code may use for alignment.
    pub position_samples: i64,
    /// Unmodified host project/render position retained for diagnostics and epoch proof.
    pub raw_host_position_samples: i64,
    /// One producer-owned contiguous transport epoch. A rewind, forward jump, clock-source
    /// change, or presentation-latency change starts a new epoch.
    pub epoch: u64,
    pub source: CaptureClockSource,
    pub presentation_latency: PresentationLatencySamples,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct CaptureClockSpan {
    pub epoch: u64,
    /// Record generation that owns this span. Ordinary Watch history stays generation zero and
    /// can never satisfy a later WAV take merely because the project position repeats. The one
    /// exception is the explicitly isolated offline-start span, which may be promoted once when
    /// the first Record callback proves exact position/source/latency continuity.
    pub generation: u64,
    pub capture_start_frame: u64,
    pub capture_end_frame: u64,
    /// Canonical WAV presentation-sample start.
    pub position_start_samples: Option<i64>,
    pub raw_host_position_start_samples: Option<i64>,
    pub source: CaptureClockSource,
    pub presentation_latency: PresentationLatencySamples,
}

impl CaptureClockSpan {
    pub(crate) fn position_for_captured_frame(self, captured_frame: u64) -> Option<i64> {
        if captured_frame <= self.capture_start_frame || captured_frame > self.capture_end_frame {
            return None;
        }
        self.position_start_samples.map(|position| {
            position.saturating_add(captured_frame.saturating_sub(self.capture_start_frame) as i64)
        })
    }

    pub(crate) fn position_at_capture_boundary(self, captured_frames: u64) -> Option<i64> {
        if captured_frames < self.capture_start_frame || captured_frames >= self.capture_end_frame {
            return None;
        }
        self.position_start_samples.map(|position| {
            position.saturating_add(captured_frames.saturating_sub(self.capture_start_frame) as i64)
        })
    }

    pub(crate) fn raw_host_position_at_capture_boundary(self, captured_frames: u64) -> Option<i64> {
        if captured_frames < self.capture_start_frame || captured_frames >= self.capture_end_frame {
            return None;
        }
        self.raw_host_position_start_samples.map(|position| {
            position.saturating_add(captured_frames.saturating_sub(self.capture_start_frame) as i64)
        })
    }
}

#[derive(Debug)]
struct CaptureClockSlot {
    version: AtomicU64,
    sequence: AtomicU64,
    generation: AtomicU64,
    capture_start_frame: AtomicU64,
    capture_end_frame: AtomicU64,
    position_valid: AtomicBool,
    position_start_samples: AtomicI64,
    raw_host_position_valid: AtomicBool,
    raw_host_position_start_samples: AtomicI64,
    source: AtomicU8,
    presentation_source: AtomicU8,
    input_presentation_samples: AtomicU64,
    output_presentation_samples: AtomicU64,
}

impl CaptureClockSlot {
    fn new() -> Self {
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
            source: AtomicU8::new(CaptureClockSource::Unknown as u8),
            presentation_source: AtomicU8::new(PresentationLatencySource::Unknown as u8),
            input_presentation_samples: AtomicU64::new(u64::MAX),
            output_presentation_samples: AtomicU64::new(u64::MAX),
        }
    }

    fn publish(&self, span: CaptureClockSpan) {
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
        self.source.store(span.source as u8, Ordering::Relaxed);
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

    fn extend(&self, sequence: u64, capture_end_frame: u64) -> bool {
        if self.sequence.load(Ordering::Acquire) != sequence {
            return false;
        }
        // All fields except the end are immutable for a contiguous span. Updating the monotonic
        // end directly avoids making Measure Thread readers race a seqlock on every audio block.
        self.capture_end_frame
            .fetch_max(capture_end_frame, Ordering::Release);
        true
    }

    fn promote_watch_offline_generation(&self, sequence: u64, generation: u64) -> bool {
        generation > 0
            && self.sequence.load(Ordering::Acquire) == sequence
            && self
                .generation
                .compare_exchange(0, generation, Ordering::AcqRel, Ordering::Acquire)
                .is_ok()
    }

    fn read(&self, sequence: u64) -> Option<CaptureClockSpan> {
        for _ in 0..4 {
            let before = self.version.load(Ordering::Acquire);
            if before & 1 != 0 || self.sequence.load(Ordering::Acquire) != sequence {
                continue;
            }
            let capture_start_frame = self.capture_start_frame.load(Ordering::Relaxed);
            let capture_end_frame = self.capture_end_frame.load(Ordering::Relaxed);
            let generation = self.generation.load(Ordering::Relaxed);
            let position_start_samples = self
                .position_valid
                .load(Ordering::Relaxed)
                .then(|| self.position_start_samples.load(Ordering::Relaxed));
            let raw_host_position_start_samples = self
                .raw_host_position_valid
                .load(Ordering::Relaxed)
                .then(|| self.raw_host_position_start_samples.load(Ordering::Relaxed));
            let source = CaptureClockSource::from_abi(self.source.load(Ordering::Relaxed));
            let presentation_source = PresentationLatencySource::from_abi(
                self.presentation_source.load(Ordering::Relaxed),
            );
            let input_presentation_samples =
                self.input_presentation_samples.load(Ordering::Relaxed);
            let output_presentation_samples =
                self.output_presentation_samples.load(Ordering::Relaxed);
            let after = self.version.load(Ordering::Acquire);
            if before == after && after & 1 == 0 {
                return Some(CaptureClockSpan {
                    epoch: sequence,
                    generation,
                    capture_start_frame,
                    capture_end_frame,
                    position_start_samples,
                    raw_host_position_start_samples,
                    source,
                    presentation_latency: PresentationLatencySamples {
                        source: presentation_source,
                        input: u32::try_from(input_presentation_samples).ok(),
                        output: u32::try_from(output_presentation_samples).ok(),
                    },
                });
            }
        }
        None
    }
}

/// Producer-owned preference for one immutable take epoch.
///
/// The numeric order is part of the lock-free selection rule: a producer may upgrade to a more
/// authoritative clock, but a later epoch of the same or lower authority can never replace the
/// already selected take merely because it is longer.
#[repr(u8)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
enum TakeEpochPriority {
    #[default]
    None = 0,
    Rendered = 1,
    Offline = 2,
    NativeRange = 3,
}

impl TakeEpochPriority {
    fn from_abi(value: u8) -> Self {
        match value {
            1 => Self::Rendered,
            2 => Self::Offline,
            3 => Self::NativeRange,
            _ => Self::None,
        }
    }

    fn for_block(block: &RecordTakeBlock) -> Self {
        if bounded_duration_from_block(block).is_some() {
            Self::NativeRange
        } else if block.offline {
            Self::Offline
        } else {
            Self::Rendered
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecordTakeBlock {
    pub generation: u64,
    pub recording: bool,
    pub rendered: bool,
    pub playing: bool,
    pub offline: bool,
    pub position_valid: bool,
    pub position_samples: i64,
    pub num_frames: u64,
    pub clock_start_samples: i64,
    pub clock_end_samples: Option<i64>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecordTakeSnapshot {
    pub generation: u64,
    pub duration_samples: u64,
    pub source: &'static str,
    /// Exact host/render range observed for this take. These remain `None` when the host clock
    /// jumped or the selected duration cannot be represented by one contiguous range.
    pub host_start_position_samples: Option<i64>,
    pub host_end_position_samples: Option<i64>,
}

#[derive(Debug)]
pub struct RecordTakeTracker {
    capture_frames_total: AtomicU64,
    capture_span_sequence: AtomicU64,
    capture_clock_floor_sequence: AtomicU64,
    capture_last_span_sequence: AtomicU64,
    capture_last_generation: AtomicU64,
    capture_last_position_valid: AtomicBool,
    capture_last_position_end_samples: AtomicI64,
    capture_last_raw_host_position_valid: AtomicBool,
    capture_last_raw_host_position_end_samples: AtomicI64,
    capture_last_source: AtomicU8,
    capture_last_presentation_source: AtomicU8,
    capture_last_input_presentation_samples: AtomicU64,
    capture_last_output_presentation_samples: AtomicU64,
    presentation_version: AtomicU64,
    presentation_source: AtomicU8,
    input_presentation_samples: AtomicU64,
    output_presentation_samples: AtomicU64,
    capture_clock_slots: Box<[CaptureClockSlot]>,
    mark_version: AtomicU64,
    mark_capture_armed: AtomicBool,
    mark_generation: AtomicU64,
    mark_position_valid: AtomicBool,
    mark_position_samples: AtomicI64,
    mark_raw_host_position_samples: AtomicI64,
    mark_epoch: AtomicU64,
    mark_source: AtomicU8,
    mark_presentation_source: AtomicU8,
    mark_input_presentation_samples: AtomicU64,
    mark_output_presentation_samples: AtomicU64,
    capture_block_offline: AtomicBool,
    watch_offline_active: AtomicBool,
    watch_offline_force_pending: AtomicBool,
    watch_offline_candidate_epoch: AtomicU64,
    render_active: AtomicBool,
    render_epoch: AtomicU64,
    render_epoch_priority: AtomicU8,
    render_frames: AtomicU64,
    render_start_valid: AtomicBool,
    render_start_position: AtomicI64,
    render_last_end_valid: AtomicBool,
    render_last_end_position: AtomicI64,
    record_generation: AtomicU64,
    record_render_epoch: AtomicU64,
    record_epoch_priority: AtomicU8,
    record_capture_epoch: AtomicU64,
    record_bounded_duration_samples: AtomicU64,
    record_bounded_range_valid: AtomicBool,
    record_bounded_start_position: AtomicI64,
    record_bounded_end_position: AtomicI64,
    record_unbounded_duration_samples: AtomicU64,
    record_unbounded_range_valid: AtomicBool,
    record_unbounded_start_position: AtomicI64,
    record_unbounded_end_position: AtomicI64,
    previous_generation: AtomicU64,
    previous_capture_epoch: AtomicU64,
    previous_bounded_duration_samples: AtomicU64,
    previous_unbounded_duration_samples: AtomicU64,
    previous_range_valid: AtomicBool,
    previous_start_position: AtomicI64,
    previous_end_position: AtomicI64,
}

impl Default for RecordTakeTracker {
    fn default() -> Self {
        Self::new()
    }
}

impl RecordTakeTracker {
    pub fn new() -> Self {
        Self {
            capture_frames_total: AtomicU64::new(0),
            capture_span_sequence: AtomicU64::new(0),
            capture_clock_floor_sequence: AtomicU64::new(1),
            capture_last_span_sequence: AtomicU64::new(0),
            capture_last_generation: AtomicU64::new(0),
            capture_last_position_valid: AtomicBool::new(false),
            capture_last_position_end_samples: AtomicI64::new(i64::MIN),
            capture_last_raw_host_position_valid: AtomicBool::new(false),
            capture_last_raw_host_position_end_samples: AtomicI64::new(i64::MIN),
            capture_last_source: AtomicU8::new(CaptureClockSource::Unknown as u8),
            capture_last_presentation_source: AtomicU8::new(
                PresentationLatencySource::Unknown as u8,
            ),
            capture_last_input_presentation_samples: AtomicU64::new(u64::MAX),
            capture_last_output_presentation_samples: AtomicU64::new(u64::MAX),
            presentation_version: AtomicU64::new(0),
            presentation_source: AtomicU8::new(PresentationLatencySource::Unknown as u8),
            input_presentation_samples: AtomicU64::new(u64::MAX),
            output_presentation_samples: AtomicU64::new(u64::MAX),
            capture_clock_slots: (0..CAPTURE_CLOCK_SPAN_CAPACITY)
                .map(|_| CaptureClockSlot::new())
                .collect::<Vec<_>>()
                .into_boxed_slice(),
            mark_version: AtomicU64::new(0),
            mark_capture_armed: AtomicBool::new(false),
            mark_generation: AtomicU64::new(0),
            mark_position_valid: AtomicBool::new(false),
            mark_position_samples: AtomicI64::new(i64::MIN),
            mark_raw_host_position_samples: AtomicI64::new(i64::MIN),
            mark_epoch: AtomicU64::new(0),
            mark_source: AtomicU8::new(CaptureClockSource::Unknown as u8),
            mark_presentation_source: AtomicU8::new(PresentationLatencySource::Unknown as u8),
            mark_input_presentation_samples: AtomicU64::new(u64::MAX),
            mark_output_presentation_samples: AtomicU64::new(u64::MAX),
            capture_block_offline: AtomicBool::new(false),
            watch_offline_active: AtomicBool::new(false),
            watch_offline_force_pending: AtomicBool::new(false),
            watch_offline_candidate_epoch: AtomicU64::new(0),
            render_active: AtomicBool::new(false),
            render_epoch: AtomicU64::new(0),
            render_epoch_priority: AtomicU8::new(TakeEpochPriority::None as u8),
            render_frames: AtomicU64::new(0),
            render_start_valid: AtomicBool::new(false),
            render_start_position: AtomicI64::new(i64::MIN),
            render_last_end_valid: AtomicBool::new(false),
            render_last_end_position: AtomicI64::new(i64::MIN),
            record_generation: AtomicU64::new(0),
            record_render_epoch: AtomicU64::new(0),
            record_epoch_priority: AtomicU8::new(TakeEpochPriority::None as u8),
            record_capture_epoch: AtomicU64::new(0),
            record_bounded_duration_samples: AtomicU64::new(0),
            record_bounded_range_valid: AtomicBool::new(false),
            record_bounded_start_position: AtomicI64::new(i64::MIN),
            record_bounded_end_position: AtomicI64::new(i64::MIN),
            record_unbounded_duration_samples: AtomicU64::new(0),
            record_unbounded_range_valid: AtomicBool::new(false),
            record_unbounded_start_position: AtomicI64::new(i64::MIN),
            record_unbounded_end_position: AtomicI64::new(i64::MIN),
            previous_generation: AtomicU64::new(0),
            previous_capture_epoch: AtomicU64::new(0),
            previous_bounded_duration_samples: AtomicU64::new(0),
            previous_unbounded_duration_samples: AtomicU64::new(0),
            previous_range_valid: AtomicBool::new(false),
            previous_start_position: AtomicI64::new(i64::MIN),
            previous_end_position: AtomicI64::new(i64::MIN),
        }
    }

    /// Audio-thread capture clock note. Call immediately before the corresponding ring push,
    /// so Measure Thread can map its consumed native frame count back to the host transport
    /// sample clock. Any push overflow invalidates normal publication through integrity counters.
    pub fn note_capture_window(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
    ) {
        self.note_capture_window_with_source(
            position_valid,
            position_samples,
            num_frames,
            CaptureClockSource::ProjectTimeline,
        );
    }

    pub fn note_capture_window_with_source(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        source: CaptureClockSource,
    ) {
        self.note_capture_window_with_presentation(
            position_valid,
            position_samples,
            num_frames,
            source,
            PresentationLatencySamples::default(),
        );
    }

    pub fn note_capture_window_with_presentation(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
    ) {
        self.note_capture_window_with_presentation_boundary(
            position_valid,
            position_samples,
            num_frames,
            source,
            presentation_latency,
            false,
        );
    }

    /// Publish a captured callback and optionally force a producer TakeStart epoch.
    ///
    /// `force_new_epoch` is normalized by the host adapter from first Record window,
    /// realtime→offline render entry, or native range entry. It is not inferred downstream from
    /// curve shape or mutable host state. The Audio Thread path remains atomics only.
    pub fn note_capture_window_with_presentation_boundary(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
        force_new_epoch: bool,
    ) {
        self.presentation_version.fetch_add(1, Ordering::AcqRel);
        self.presentation_source
            .store(presentation_latency.source as u8, Ordering::Relaxed);
        self.input_presentation_samples.store(
            presentation_latency.input.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.output_presentation_samples.store(
            presentation_latency.output.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.presentation_version.fetch_add(1, Ordering::Release);
        if num_frames == 0 {
            return;
        }
        // Convert exactly once at the Audio-Thread producer boundary. Studio One reports each
        // plug-in node's project position ahead by that node's downstream output-presentation
        // latency. Subtracting that latency restores the shared content sample seen by every node.
        // Downstream code receives both facts but must never apply or choose a latency again.
        let presentation_position_samples =
            position_valid
                .then_some(position_samples)
                .and_then(|raw_position| {
                    wav_presentation_position_samples(raw_position, presentation_latency.output)
                });
        let capture_start_frame = self.capture_frames_total.load(Ordering::Acquire);
        let frames_end = self
            .capture_frames_total
            .fetch_add(num_frames, Ordering::AcqRel)
            .saturating_add(num_frames);
        let record_generation = self.record_generation.load(Ordering::Acquire);
        let capture_generation = (self.mark_capture_armed.load(Ordering::Acquire)
            && self.mark_generation.load(Ordering::Acquire) == record_generation)
            .then_some(record_generation)
            .filter(|generation| *generation > 0)
            .unwrap_or(0);
        let previous_sequence = self.capture_last_span_sequence.load(Ordering::Acquire);
        let previous_generation = self.capture_last_generation.load(Ordering::Acquire);
        let mapping_contiguous = presentation_position_samples.is_some()
            && source != CaptureClockSource::Unknown
            && self.capture_last_position_valid.load(Ordering::Acquire)
            && self
                .capture_last_raw_host_position_valid
                .load(Ordering::Acquire)
            && self.capture_last_source.load(Ordering::Acquire) == source as u8
            && self
                .capture_last_presentation_source
                .load(Ordering::Acquire)
                == presentation_latency.source as u8
            && self
                .capture_last_input_presentation_samples
                .load(Ordering::Acquire)
                == presentation_latency.input.map_or(u64::MAX, u64::from)
            && self
                .capture_last_output_presentation_samples
                .load(Ordering::Acquire)
                == presentation_latency.output.map_or(u64::MAX, u64::from)
            && self
                .capture_last_position_end_samples
                .load(Ordering::Acquire)
                == presentation_position_samples.unwrap_or(i64::MIN)
            && self
                .capture_last_raw_host_position_end_samples
                .load(Ordering::Acquire)
                == position_samples;

        // Some hosts enter the offline render before PRE has acknowledged the Keep generation.
        // The audio is already the first WAV audio in that interval, but it is still tagged as
        // Watch (generation zero). Force a fresh epoch at the offline edge so no ordinary Watch
        // history can precede it. When the first armed Record callback is exactly contiguous in
        // producer coordinates, promote that one epoch to the Record generation instead of
        // splitting it at the acknowledgement edge. This is a one-way producer fact: a normal
        // realtime Watch span, a discontinuity, or a source/latency change can never be promoted.
        let watch_offline_active = self.watch_offline_active.load(Ordering::Acquire);
        let capture_block_offline = self.capture_block_offline.load(Ordering::Acquire);
        let watch_offline_capture =
            capture_generation == 0 && watch_offline_active && capture_block_offline;
        let force_watch_offline_epoch = watch_offline_capture
            && self
                .watch_offline_force_pending
                .swap(false, Ordering::AcqRel);
        let watch_candidate = self.watch_offline_candidate_epoch.load(Ordering::Acquire);
        let promote_watch_offline = capture_generation > 0
            && watch_offline_active
            && capture_block_offline
            && previous_generation == 0
            && previous_sequence > 0
            && watch_candidate == previous_sequence
            && mapping_contiguous;

        let extended = if promote_watch_offline {
            let index = previous_sequence as usize % self.capture_clock_slots.len();
            self.capture_clock_slots[index]
                .promote_watch_offline_generation(previous_sequence, capture_generation)
                && self.capture_clock_slots[index].extend(previous_sequence, frames_end)
        } else if !force_new_epoch
            && !force_watch_offline_epoch
            && previous_sequence > 0
            && previous_generation == capture_generation
            && mapping_contiguous
        {
            let index = previous_sequence as usize % self.capture_clock_slots.len();
            self.capture_clock_slots[index].extend(previous_sequence, frames_end)
        } else {
            false
        };
        let mut published_sequence = None;
        if !extended {
            let sequence = self
                .capture_span_sequence
                .fetch_add(1, Ordering::AcqRel)
                .saturating_add(1);
            let index = sequence as usize % self.capture_clock_slots.len();
            self.capture_clock_slots[index].publish(CaptureClockSpan {
                epoch: sequence,
                generation: capture_generation,
                capture_start_frame,
                capture_end_frame: frames_end,
                position_start_samples: presentation_position_samples,
                raw_host_position_start_samples: position_valid.then_some(position_samples),
                source,
                presentation_latency,
            });
            self.capture_last_span_sequence
                .store(sequence, Ordering::Release);
            published_sequence = Some(sequence);
        }
        let epoch = self.capture_last_span_sequence.load(Ordering::Acquire);
        if watch_offline_capture {
            if let Some(sequence) = published_sequence {
                self.watch_offline_candidate_epoch
                    .store(sequence, Ordering::Release);
            }
        } else if capture_generation > 0 {
            // Promotion is permitted only at the first armed Record callback. Regardless of
            // whether the mapping was contiguous, consume the offline edge here so a later Record
            // callback can never reach back and relabel unrelated Watch audio.
            self.watch_offline_active.store(false, Ordering::Release);
            self.watch_offline_force_pending
                .store(false, Ordering::Release);
            self.watch_offline_candidate_epoch
                .store(0, Ordering::Release);
        }
        if self.mark_capture_armed.load(Ordering::Acquire)
            && capture_generation > 0
            && capture_generation == record_generation
            && self.record_render_epoch.load(Ordering::Acquire)
                == self.render_epoch.load(Ordering::Acquire)
        {
            let _ = self.record_capture_epoch.compare_exchange(
                0,
                epoch,
                Ordering::AcqRel,
                Ordering::Acquire,
            );
        }
        self.publish_record_mark_point(
            presentation_position_samples,
            position_valid.then_some(position_samples),
            num_frames,
            epoch,
            source,
            presentation_latency,
        );
        self.capture_last_position_valid
            .store(presentation_position_samples.is_some(), Ordering::Release);
        self.capture_last_position_end_samples.store(
            presentation_position_samples
                .unwrap_or(i64::MIN)
                .saturating_add(num_frames as i64),
            Ordering::Release,
        );
        self.capture_last_raw_host_position_valid
            .store(position_valid, Ordering::Release);
        self.capture_last_raw_host_position_end_samples.store(
            position_samples.saturating_add(num_frames as i64),
            Ordering::Release,
        );
        self.capture_last_source
            .store(source as u8, Ordering::Release);
        self.capture_last_generation
            .store(capture_generation, Ordering::Release);
        self.capture_last_presentation_source
            .store(presentation_latency.source as u8, Ordering::Release);
        self.capture_last_input_presentation_samples.store(
            presentation_latency.input.map_or(u64::MAX, u64::from),
            Ordering::Release,
        );
        self.capture_last_output_presentation_samples.store(
            presentation_latency.output.map_or(u64::MAX, u64::from),
            Ordering::Release,
        );
    }

    /// Latest raw host callback values for diagnostics. Historical values are also retained in
    /// each immutable capture-clock epoch; downstream alignment never reads this "latest" value.
    pub fn presentation_latency(&self) -> PresentationLatencySamples {
        for _ in 0..4 {
            let before = self.presentation_version.load(Ordering::Acquire);
            if before & 1 != 0 {
                continue;
            }
            let source = self.presentation_source.load(Ordering::Relaxed);
            let input = self.input_presentation_samples.load(Ordering::Relaxed);
            let output = self.output_presentation_samples.load(Ordering::Relaxed);
            let after = self.presentation_version.load(Ordering::Acquire);
            if before == after {
                return PresentationLatencySamples {
                    source: PresentationLatencySource::from_abi(source),
                    input: u32::try_from(input).ok(),
                    output: u32::try_from(output).ok(),
                };
            }
        }
        PresentationLatencySamples::default()
    }

    /// Map a Measure Thread consumed native frame count onto the host transport
    /// sample clock, when the host exposed a position for the captured block.
    pub fn position_samples_for_captured_frame(&self, captured_frames: u64) -> Option<i64> {
        self.clock_point_for_captured_frame(captured_frames)
            .map(|point| point.position_samples)
    }

    /// Total native frames published to the capture clock. Audio adapters use this immediately
    /// after publishing a TakeStart window to bind the separate Record SPSC lane to the same
    /// immutable clock origin. Atomic read only.
    pub fn captured_frames_total(&self) -> u64 {
        self.capture_frames_total.load(Ordering::Acquire)
    }

    /// Audio Thread が実際に観測した1本の連続content-clock spanが、指定したWAV範囲を
    /// 丸ごと含むか。Drop commitを別projectのKeepへbroadcastしないためのproducer側scope
    /// proofとしてIO Threadだけが読む。Atomic readのみでAudio Threadを止めない。
    pub fn observed_content_range(&self, start_samples: i64, end_samples: i64) -> bool {
        if end_samples <= start_samples {
            return false;
        }
        let latest = self.capture_span_sequence.load(Ordering::Acquire);
        let first = latest
            .saturating_sub(self.capture_clock_slots.len() as u64)
            .saturating_add(1)
            .max(self.capture_clock_floor_sequence.load(Ordering::Acquire))
            .max(1);
        (first..=latest).any(|sequence| {
            let index = sequence as usize % self.capture_clock_slots.len();
            let Some(span) = self.capture_clock_slots[index].read(sequence) else {
                return false;
            };
            let Some(span_start) = span.position_start_samples else {
                return false;
            };
            if span.source == CaptureClockSource::Unknown {
                return false;
            }
            let span_len = span
                .capture_end_frame
                .saturating_sub(span.capture_start_frame);
            let Ok(span_len) = i64::try_from(span_len) else {
                return false;
            };
            span_start <= start_samples
                && span_start
                    .checked_add(span_len)
                    .is_some_and(|end| end >= end_samples)
        })
    }

    pub fn clock_point_for_captured_frame(
        &self,
        captured_frames: u64,
    ) -> Option<CaptureClockPoint> {
        let span = self.capture_span_for_frame(captured_frames)?;
        let position_samples = span.position_for_captured_frame(captured_frames)?;
        let raw_host_position_samples = span
            .raw_host_position_start_samples?
            .saturating_add(captured_frames.saturating_sub(span.capture_start_frame) as i64);
        (span.source != CaptureClockSource::Unknown).then_some(CaptureClockPoint {
            position_samples,
            raw_host_position_samples,
            epoch: span.epoch,
            source: span.source,
            presentation_latency: span.presentation_latency,
        })
    }

    /// Resolve one raw host boundary onto the immutable producer presentation clock. The newest
    /// containing epoch wins, which is exactly the callback pass that owns the current Keep edge.
    /// This performs no latency selection: the epoch already stores the value used at production.
    pub fn clock_point_for_raw_host_position(
        &self,
        raw_host_position_samples: i64,
    ) -> Option<CaptureClockPoint> {
        let latest = self.capture_span_sequence.load(Ordering::Acquire);
        let earliest = latest
            .saturating_sub(self.capture_clock_slots.len() as u64)
            .saturating_add(1)
            .max(self.capture_clock_floor_sequence.load(Ordering::Acquire))
            .max(1);
        for sequence in (earliest..=latest).rev() {
            let index = sequence as usize % self.capture_clock_slots.len();
            let Some(span) = self.capture_clock_slots[index].read(sequence) else {
                continue;
            };
            let (Some(raw_start), Some(presentation_start)) = (
                span.raw_host_position_start_samples,
                span.position_start_samples,
            ) else {
                continue;
            };
            let span_len = span
                .capture_end_frame
                .saturating_sub(span.capture_start_frame);
            let Ok(span_len) = i64::try_from(span_len) else {
                continue;
            };
            let Some(raw_end) = raw_start.checked_add(span_len) else {
                continue;
            };
            if raw_host_position_samples < raw_start || raw_host_position_samples > raw_end {
                continue;
            }
            let delta = raw_host_position_samples.saturating_sub(raw_start);
            return (span.source != CaptureClockSource::Unknown).then_some(CaptureClockPoint {
                position_samples: presentation_start.saturating_add(delta),
                raw_host_position_samples,
                epoch: span.epoch,
                source: span.source,
                presentation_latency: span.presentation_latency,
            });
        }
        None
    }

    /// Capture epoch that fully owns an exact raw render range. A range spanning a transport jump
    /// intentionally has no answer and therefore cannot be baked as one WAV take.
    pub fn capture_epoch_for_raw_host_range(
        &self,
        raw_start_samples: i64,
        raw_end_samples: i64,
    ) -> Option<u64> {
        if raw_end_samples <= raw_start_samples {
            return None;
        }
        let start = self.clock_point_for_raw_host_position(raw_start_samples)?;
        let end = self.clock_point_for_raw_host_position(raw_end_samples.checked_sub(1)?)?;
        (start.epoch == end.epoch).then_some(start.epoch)
    }

    pub fn presentation_range_for_raw_host_range(
        &self,
        raw_start_samples: i64,
        raw_end_samples: i64,
    ) -> Option<(u64, i64, i64)> {
        if raw_end_samples <= raw_start_samples {
            return None;
        }
        let start = self.clock_point_for_raw_host_position(raw_start_samples)?;
        let end_last = self.clock_point_for_raw_host_position(raw_end_samples.checked_sub(1)?)?;
        if start.epoch != end_last.epoch {
            return None;
        }
        Some((
            start.epoch,
            start.position_samples,
            end_last.position_samples.checked_add(1)?,
        ))
    }

    /// Select the producer epoch that owns the exact Broadcast-Wave presentation range.
    ///
    /// `bwf_start_samples` is already on the exported WAV presentation axis and the duration is
    /// expressed in the same native sample rate. No host position or latency is consulted here:
    /// each capture span stored the one producer conversion used when the audio was copied. Watch
    /// history (generation zero) and other Keep generations are ineligible even when their project
    /// positions repeat.
    pub fn capture_epoch_containing_presentation_range(
        &self,
        expected_generation: u64,
        bwf_start_samples: i64,
        duration_samples: u64,
    ) -> Option<u64> {
        if expected_generation == 0 || duration_samples == 0 {
            return None;
        }
        let duration_samples = i64::try_from(duration_samples).ok()?;
        let bwf_end_samples = bwf_start_samples.checked_add(duration_samples)?;
        let contains = |span: CaptureClockSpan| {
            if span.generation != expected_generation || span.source == CaptureClockSource::Unknown
            {
                return false;
            }
            let Some(span_start) = span.position_start_samples else {
                return false;
            };
            let Ok(span_len) = i64::try_from(
                span.capture_end_frame
                    .saturating_sub(span.capture_start_frame),
            ) else {
                return false;
            };
            span_start <= bwf_start_samples
                && span_start
                    .checked_add(span_len)
                    .is_some_and(|span_end| span_end >= bwf_end_samples)
        };

        // Measure binds samples to this immutable epoch while Record is running. Writer close is
        // too late to upgrade to a later epoch because those samples have already been rejected by
        // the Record consumer. The producer must therefore make the first selected epoch correct
        // (including ACK pre-roll promotion); BWF is a final exact-containment proof, never an
        // alternate epoch selector.
        let selected = self.selected_capture_epoch(expected_generation)?;
        let index = selected as usize % self.capture_clock_slots.len();
        self.capture_clock_slots[index]
            .read(selected)
            .is_some_and(contains)
            .then_some(selected)
    }

    pub fn selected_capture_epoch(&self, expected_generation: u64) -> Option<u64> {
        if expected_generation == 0 {
            return None;
        }
        if self.record_generation.load(Ordering::Acquire) == expected_generation {
            return Some(self.record_capture_epoch.load(Ordering::Acquire))
                .filter(|epoch| *epoch > 0);
        }
        (self.previous_generation.load(Ordering::Acquire) == expected_generation)
            .then(|| self.previous_capture_epoch.load(Ordering::Acquire))
            .filter(|epoch| *epoch > 0)
    }

    pub fn presentation_range_for_capture_epoch(
        &self,
        epoch: u64,
        raw_start_samples: i64,
        raw_end_samples: i64,
    ) -> Option<(i64, i64)> {
        if epoch == 0 || raw_end_samples <= raw_start_samples {
            return None;
        }
        let index = epoch as usize % self.capture_clock_slots.len();
        let span = self.capture_clock_slots[index].read(epoch)?;
        let (raw_start, presentation_start) = (
            span.raw_host_position_start_samples?,
            span.position_start_samples?,
        );
        let span_len = i64::try_from(
            span.capture_end_frame
                .saturating_sub(span.capture_start_frame),
        )
        .ok()?;
        let span_raw_end = raw_start.checked_add(span_len)?;
        if raw_start_samples < raw_start || raw_end_samples > span_raw_end {
            return None;
        }
        let offset = raw_start_samples.checked_sub(raw_start)?;
        let presentation_range_start = presentation_start.checked_add(offset)?;
        let duration = raw_end_samples.checked_sub(raw_start_samples)?;
        Some((
            presentation_range_start,
            presentation_range_start.checked_add(duration)?,
        ))
    }

    /// Map one producer take across a factual chain of presentation-latency epochs.
    ///
    /// A DAW may change its reported presentation latency during one render. Each mapping remains
    /// immutable, but the exported audio and presentation clock can stay continuous across the
    /// boundary. Only adjacent epochs that pass the latency-continuation proof and meet exactly on
    /// the producer presentation axis may contribute to the requested take range.
    pub fn presentation_range_for_latency_epoch_chain(
        &self,
        first_epoch: u64,
        raw_start_samples: i64,
        duration_samples: u64,
    ) -> Option<(i64, i64)> {
        if first_epoch == 0 || duration_samples == 0 {
            return None;
        }
        let mut span = self.capture_span_for_epoch(first_epoch)?;
        let span_raw_start = span.raw_host_position_start_samples?;
        let offset = raw_start_samples.checked_sub(span_raw_start)?;
        let offset = u64::try_from(offset).ok()?;
        let span_len = span
            .capture_end_frame
            .checked_sub(span.capture_start_frame)?;
        if offset >= span_len {
            return None;
        }
        let presentation_start = span
            .position_start_samples?
            .checked_add(i64::try_from(offset).ok()?)?;
        let capture_start = span.capture_start_frame.checked_add(offset)?;
        let capture_end = capture_start.checked_add(duration_samples)?;

        while span.capture_end_frame < capture_end {
            let next_epoch = span.epoch.checked_add(1)?;
            let next = self.capture_span_for_epoch(next_epoch)?;
            if !self.capture_epochs_are_latency_continuation(span.epoch, next.epoch) {
                return None;
            }
            let span_presentation_end = span.position_start_samples?.checked_add(
                i64::try_from(
                    span.capture_end_frame
                        .checked_sub(span.capture_start_frame)?,
                )
                .ok()?,
            )?;
            if next.position_start_samples != Some(span_presentation_end) {
                return None;
            }
            span = next;
        }

        Some((
            presentation_start,
            presentation_start.checked_add(i64::try_from(duration_samples).ok()?)?,
        ))
    }

    /// Recover the raw host diagnostic interval corresponding to an already selected producer
    /// presentation interval. This is an offset within one immutable mapping; it does not inspect,
    /// choose, add, or subtract a latency value downstream.
    pub fn raw_host_range_for_capture_epoch_presentation_range(
        &self,
        epoch: u64,
        presentation_start_samples: i64,
        presentation_end_samples: i64,
    ) -> Option<(i64, i64)> {
        if epoch == 0 || presentation_end_samples <= presentation_start_samples {
            return None;
        }
        let index = epoch as usize % self.capture_clock_slots.len();
        let span = self.capture_clock_slots[index].read(epoch)?;
        let (span_presentation_start, span_raw_start) = (
            span.position_start_samples?,
            span.raw_host_position_start_samples?,
        );
        let span_len = i64::try_from(
            span.capture_end_frame
                .saturating_sub(span.capture_start_frame),
        )
        .ok()?;
        let span_presentation_end = span_presentation_start.checked_add(span_len)?;
        if presentation_start_samples < span_presentation_start
            || presentation_end_samples > span_presentation_end
        {
            return None;
        }
        let offset = presentation_start_samples.checked_sub(span_presentation_start)?;
        let raw_start = span_raw_start.checked_add(offset)?;
        Some((
            raw_start,
            raw_start
                .checked_add(presentation_end_samples.checked_sub(presentation_start_samples)?)?,
        ))
    }

    /// Start the capture clock for a replacement SPSC ring.
    ///
    /// The Measure Thread consuming the new ring starts its native frame cursor at zero. The
    /// Audio Thread must therefore reset this producer-side mapping in the same callback that
    /// installs the replacement Producer. Atomics only; no allocation, lock, logging, or I/O.
    pub fn reset_capture_clock(&self) {
        self.capture_frames_total.store(0, Ordering::Release);
        // Epoch IDs remain monotonic across SPSC replacement. A previous Keep may still be closing
        // on IO Thread and holds its selected epoch; reusing sequence 1 would silently redirect it
        // to the replacement ring. Current-frame lookups are isolated by the floor while direct
        // selected-epoch lookups can still finish the previous generation from immutable slots.
        self.capture_clock_floor_sequence.store(
            self.capture_span_sequence
                .load(Ordering::Acquire)
                .saturating_add(1),
            Ordering::Release,
        );
        self.capture_last_span_sequence.store(0, Ordering::Release);
        self.capture_last_generation.store(0, Ordering::Release);
        self.capture_last_position_valid
            .store(false, Ordering::Release);
        self.capture_last_position_end_samples
            .store(i64::MIN, Ordering::Release);
        self.capture_last_raw_host_position_valid
            .store(false, Ordering::Release);
        self.capture_last_raw_host_position_end_samples
            .store(i64::MIN, Ordering::Release);
        self.capture_last_source
            .store(CaptureClockSource::Unknown as u8, Ordering::Release);
        self.capture_last_presentation_source
            .store(PresentationLatencySource::Unknown as u8, Ordering::Release);
        self.capture_last_input_presentation_samples
            .store(u64::MAX, Ordering::Release);
        self.capture_last_output_presentation_samples
            .store(u64::MAX, Ordering::Release);
        self.capture_block_offline.store(false, Ordering::Release);
        self.watch_offline_active.store(false, Ordering::Release);
        self.watch_offline_force_pending
            .store(false, Ordering::Release);
        self.watch_offline_candidate_epoch
            .store(0, Ordering::Release);
    }

    pub(crate) fn capture_span_for_frame(&self, captured_frame: u64) -> Option<CaptureClockSpan> {
        if captured_frame == 0 {
            return None;
        }
        let latest = self.capture_span_sequence.load(Ordering::Acquire);
        let earliest = latest
            .saturating_sub(self.capture_clock_slots.len() as u64)
            .saturating_add(1)
            .max(self.capture_clock_floor_sequence.load(Ordering::Acquire))
            .max(1);
        for sequence in (earliest..=latest).rev() {
            let index = sequence as usize % self.capture_clock_slots.len();
            let Some(span) = self.capture_clock_slots[index].read(sequence) else {
                continue;
            };
            if captured_frame > span.capture_start_frame && captured_frame <= span.capture_end_frame
            {
                return Some(span);
            }
        }
        None
    }

    /// A presentation-latency callback may split the clock mapping while the captured audio and
    /// raw host stream remain contiguous. Measure Thread must keep measuring across that boundary;
    /// only the late WAV resolver chooses the correct mapping model.
    pub(crate) fn capture_epochs_are_latency_continuation(
        &self,
        previous_epoch: u64,
        next_epoch: u64,
    ) -> bool {
        let (Some(previous), Some(next)) = (
            self.capture_span_for_epoch(previous_epoch),
            self.capture_span_for_epoch(next_epoch),
        ) else {
            return false;
        };
        let previous_frames = previous
            .capture_end_frame
            .saturating_sub(previous.capture_start_frame);
        let raw_shift = previous
            .raw_host_position_start_samples
            .and_then(|start| i64::try_from(previous_frames).ok()?.checked_add(start))
            .zip(next.raw_host_position_start_samples)
            .map(|(previous_end, next_start)| next_start.saturating_sub(previous_end));
        previous.epoch != next.epoch
            && previous.generation == next.generation
            && previous.capture_end_frame == next.capture_start_frame
            && previous.source == next.source
            && previous.presentation_latency != next.presentation_latency
            && raw_shift.is_some_and(|shift| {
                presentation_latency_shift_matches(
                    shift,
                    previous.presentation_latency,
                    next.presentation_latency,
                )
            })
    }

    fn capture_span_for_epoch(&self, epoch: u64) -> Option<CaptureClockSpan> {
        if epoch == 0 {
            return None;
        }
        let index = epoch as usize % self.capture_clock_slots.len();
        self.capture_clock_slots[index].read(epoch)
    }

    /// Audio-thread note. This is atomics only: no allocation, lock, filesystem,
    /// logging, or blocking call.
    pub fn note_block(&self, block: RecordTakeBlock) {
        let watch_offline =
            !block.recording && block.offline && block.position_valid && block.num_frames > 0;
        let watch_offline_active = self.watch_offline_active.load(Ordering::Acquire);
        if watch_offline {
            if !watch_offline_active {
                self.watch_offline_candidate_epoch
                    .store(0, Ordering::Release);
                self.watch_offline_force_pending
                    .store(true, Ordering::Release);
                self.watch_offline_active.store(true, Ordering::Release);
            }
        } else if !(block.recording && block.offline && watch_offline_active) {
            self.watch_offline_active.store(false, Ordering::Release);
            self.watch_offline_force_pending
                .store(false, Ordering::Release);
            self.watch_offline_candidate_epoch
                .store(0, Ordering::Release);
        }
        self.capture_block_offline
            .store(block.offline, Ordering::Release);

        self.mark_version.fetch_add(1, Ordering::AcqRel);
        self.mark_capture_armed.store(
            block.recording
                && block.rendered
                && block.generation > 0
                && block.position_valid
                && block.num_frames > 0,
            Ordering::Relaxed,
        );
        self.mark_generation
            .store(block.generation, Ordering::Relaxed);
        self.mark_position_valid.store(false, Ordering::Relaxed);
        self.mark_source
            .store(CaptureClockSource::Unknown as u8, Ordering::Relaxed);
        self.mark_version.fetch_add(1, Ordering::Release);
        if block.recording && block.generation > 0 {
            self.ensure_record_generation(block.generation);
        }

        let render_eligible =
            block.rendered && block.num_frames > 0 && block.recording && block.position_valid;

        if !render_eligible {
            self.render_active.store(false, Ordering::Release);
            self.render_epoch_priority
                .store(TakeEpochPriority::None as u8, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
            return;
        }

        let candidate_priority = TakeEpochPriority::for_block(&block);
        let reset_epoch = !self.render_active.load(Ordering::Acquire)
            || self.position_discontinuity(block.position_valid, block.position_samples)
            || TakeEpochPriority::from_abi(self.render_epoch_priority.load(Ordering::Acquire))
                != candidate_priority;
        let epoch = if reset_epoch {
            self.render_frames.store(0, Ordering::Release);
            self.render_start_position
                .store(block.position_samples, Ordering::Release);
            self.render_start_valid.store(true, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
            let next = self.render_epoch.fetch_add(1, Ordering::AcqRel) + 1;
            self.render_epoch_priority
                .store(candidate_priority as u8, Ordering::Release);
            self.render_active.store(true, Ordering::Release);
            next
        } else {
            self.render_epoch.load(Ordering::Acquire)
        };

        self.render_frames
            .fetch_add(block.num_frames, Ordering::AcqRel);

        self.render_last_end_position.store(
            block
                .position_samples
                .saturating_add(block.num_frames as i64),
            Ordering::Release,
        );
        self.render_last_end_valid.store(true, Ordering::Release);

        let selected_priority =
            TakeEpochPriority::from_abi(self.record_epoch_priority.load(Ordering::Acquire));
        let selected_epoch = self.record_render_epoch.load(Ordering::Acquire);
        if selected_epoch == 0 || candidate_priority > selected_priority {
            self.select_record_epoch(epoch, candidate_priority);
        }
        if self.record_render_epoch.load(Ordering::Acquire) != epoch {
            return;
        }

        if candidate_priority == TakeEpochPriority::NativeRange {
            // A host native range is a complete take declaration, not a running maximum. The
            // first declaration for this immutable epoch wins; later callbacks cannot stretch it.
            if self.record_bounded_duration_samples.load(Ordering::Acquire) == 0 {
                if let Some(duration) = bounded_duration_from_block(&block) {
                    self.record_bounded_range_valid
                        .store(false, Ordering::Release);
                    self.record_bounded_start_position
                        .store(block.clock_start_samples, Ordering::Relaxed);
                    self.record_bounded_end_position.store(
                        block.clock_end_samples.unwrap_or(i64::MIN),
                        Ordering::Relaxed,
                    );
                    self.record_bounded_duration_samples
                        .store(duration, Ordering::Release);
                    self.record_bounded_range_valid
                        .store(true, Ordering::Release);
                }
            }
        } else if let Some(duration) = self.render_duration_from_position_span() {
            // Duration may grow only while the selected epoch itself remains active. A later
            // rewind/post-bounce epoch never competes by length.
            let previous = self
                .record_unbounded_duration_samples
                .load(Ordering::Acquire);
            if duration > previous {
                self.record_unbounded_range_valid
                    .store(false, Ordering::Release);
                let start = self.render_start_position.load(Ordering::Acquire);
                let end = self.render_last_end_position.load(Ordering::Acquire);
                self.record_unbounded_start_position
                    .store(start, Ordering::Relaxed);
                self.record_unbounded_end_position
                    .store(end, Ordering::Relaxed);
                self.record_unbounded_duration_samples
                    .store(duration, Ordering::Release);
                if end.checked_sub(start) == i64::try_from(duration).ok() {
                    self.record_unbounded_range_valid
                        .store(true, Ordering::Release);
                }
            }
        }
    }

    fn select_record_epoch(&self, epoch: u64, priority: TakeEpochPriority) {
        self.record_capture_epoch.store(0, Ordering::Release);
        self.record_bounded_duration_samples
            .store(0, Ordering::Release);
        self.record_bounded_range_valid
            .store(false, Ordering::Release);
        self.record_unbounded_duration_samples
            .store(0, Ordering::Release);
        self.record_unbounded_range_valid
            .store(false, Ordering::Release);
        self.record_epoch_priority
            .store(priority as u8, Ordering::Release);
        self.record_render_epoch.store(epoch, Ordering::Release);
    }

    fn publish_record_mark_point(
        &self,
        presentation_position_samples: Option<i64>,
        raw_host_position_samples: Option<i64>,
        num_frames: u64,
        epoch: u64,
        source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
    ) {
        if !self.mark_capture_armed.load(Ordering::Acquire)
            || presentation_position_samples.is_none()
            || raw_host_position_samples.is_none()
            || num_frames == 0
            || epoch == 0
            || source == CaptureClockSource::Unknown
        {
            return;
        }
        self.mark_version.fetch_add(1, Ordering::AcqRel);
        self.mark_position_samples.store(
            presentation_position_samples
                .unwrap_or(i64::MIN)
                .saturating_add(num_frames as i64),
            Ordering::Relaxed,
        );
        self.mark_raw_host_position_samples.store(
            raw_host_position_samples
                .unwrap_or(i64::MIN)
                .saturating_add(num_frames as i64),
            Ordering::Relaxed,
        );
        self.mark_epoch.store(epoch, Ordering::Relaxed);
        self.mark_source.store(source as u8, Ordering::Relaxed);
        self.mark_presentation_source
            .store(presentation_latency.source as u8, Ordering::Relaxed);
        self.mark_input_presentation_samples.store(
            presentation_latency.input.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.mark_output_presentation_samples.store(
            presentation_latency.output.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.mark_position_valid.store(true, Ordering::Relaxed);
        self.mark_version.fetch_add(1, Ordering::Release);
    }

    /// Latest exact producer boundary for the current Record generation.
    pub fn record_mark_point(&self, expected_generation: u64) -> Option<CaptureClockPoint> {
        if expected_generation == 0 {
            return None;
        }
        for _ in 0..4 {
            let before = self.mark_version.load(Ordering::Acquire);
            if before & 1 != 0 {
                continue;
            }
            let armed = self.mark_capture_armed.load(Ordering::Relaxed);
            let generation = self.mark_generation.load(Ordering::Relaxed);
            let valid = self.mark_position_valid.load(Ordering::Relaxed);
            let position_samples = self.mark_position_samples.load(Ordering::Relaxed);
            let raw_host_position_samples =
                self.mark_raw_host_position_samples.load(Ordering::Relaxed);
            let epoch = self.mark_epoch.load(Ordering::Relaxed);
            let source = CaptureClockSource::from_abi(self.mark_source.load(Ordering::Relaxed));
            let presentation_source = PresentationLatencySource::from_abi(
                self.mark_presentation_source.load(Ordering::Relaxed),
            );
            let input_presentation_samples =
                self.mark_input_presentation_samples.load(Ordering::Relaxed);
            let output_presentation_samples = self
                .mark_output_presentation_samples
                .load(Ordering::Relaxed);
            let after = self.mark_version.load(Ordering::Acquire);
            if before == after
                && after & 1 == 0
                && armed
                && valid
                && generation == expected_generation
                && epoch > 0
                && source != CaptureClockSource::Unknown
            {
                return Some(CaptureClockPoint {
                    position_samples,
                    raw_host_position_samples,
                    epoch,
                    source,
                    presentation_latency: PresentationLatencySamples {
                        source: presentation_source,
                        input: u32::try_from(input_presentation_samples).ok(),
                        output: u32::try_from(output_presentation_samples).ok(),
                    },
                });
            }
        }
        None
    }

    pub fn snapshot(&self, expected_generation: u64) -> Option<RecordTakeSnapshot> {
        if expected_generation == 0 {
            return None;
        }
        let generation = self.record_generation.load(Ordering::Acquire);
        let bounded_duration_samples = self.record_bounded_duration_samples.load(Ordering::Acquire);
        let unbounded_duration_samples = self
            .record_unbounded_duration_samples
            .load(Ordering::Acquire);
        let epoch = self.record_render_epoch.load(Ordering::Acquire);
        if generation == expected_generation && epoch > 0 {
            let range =
                self.current_exact_host_range(bounded_duration_samples, unbounded_duration_samples);
            snapshot_from_durations(
                generation,
                bounded_duration_samples,
                unbounded_duration_samples,
                range,
            )
        } else if self.previous_generation.load(Ordering::Acquire) == expected_generation {
            let bounded_duration_samples = self
                .previous_bounded_duration_samples
                .load(Ordering::Acquire);
            let unbounded_duration_samples = self
                .previous_unbounded_duration_samples
                .load(Ordering::Acquire);
            let range = self.previous_range_valid.load(Ordering::Acquire).then(|| {
                (
                    self.previous_start_position.load(Ordering::Acquire),
                    self.previous_end_position.load(Ordering::Acquire),
                )
            });
            snapshot_from_durations(
                expected_generation,
                bounded_duration_samples,
                unbounded_duration_samples,
                range,
            )
        } else {
            None
        }
    }

    fn ensure_record_generation(&self, generation: u64) {
        let current_generation = self.record_generation.load(Ordering::Acquire);
        if current_generation == generation {
            return;
        }
        self.preserve_current_record_snapshot();
        if current_generation > 0 {
            self.render_active.store(false, Ordering::Release);
            self.render_frames.store(0, Ordering::Release);
            self.render_start_valid.store(false, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
        }
        self.record_render_epoch.store(0, Ordering::Release);
        self.record_epoch_priority
            .store(TakeEpochPriority::None as u8, Ordering::Release);
        self.record_capture_epoch.store(0, Ordering::Release);
        self.record_bounded_duration_samples
            .store(0, Ordering::Release);
        self.record_bounded_range_valid
            .store(false, Ordering::Release);
        self.record_unbounded_duration_samples
            .store(0, Ordering::Release);
        self.record_unbounded_range_valid
            .store(false, Ordering::Release);
        self.record_generation.store(generation, Ordering::Release);
    }

    fn preserve_current_record_snapshot(&self) {
        let generation = self.record_generation.load(Ordering::Acquire);
        if generation == 0 {
            return;
        }
        let epoch = self.record_render_epoch.load(Ordering::Acquire);
        let bounded_duration_samples = self.record_bounded_duration_samples.load(Ordering::Acquire);
        let unbounded_duration_samples = self
            .record_unbounded_duration_samples
            .load(Ordering::Acquire);
        if epoch > 0 && (bounded_duration_samples > 0 || unbounded_duration_samples > 0) {
            let range =
                self.current_exact_host_range(bounded_duration_samples, unbounded_duration_samples);
            self.previous_range_valid.store(false, Ordering::Release);
            if let Some((start, end)) = range {
                self.previous_start_position.store(start, Ordering::Relaxed);
                self.previous_end_position.store(end, Ordering::Relaxed);
                self.previous_range_valid.store(true, Ordering::Release);
            }
            self.previous_bounded_duration_samples
                .store(bounded_duration_samples, Ordering::Release);
            self.previous_unbounded_duration_samples
                .store(unbounded_duration_samples, Ordering::Release);
            self.previous_capture_epoch.store(
                self.record_capture_epoch.load(Ordering::Acquire),
                Ordering::Release,
            );
            self.previous_generation
                .store(generation, Ordering::Release);
        }
    }

    fn position_discontinuity(&self, position_valid: bool, position_samples: i64) -> bool {
        if !position_valid || !self.render_last_end_valid.load(Ordering::Acquire) {
            return false;
        }
        let previous_end = self.render_last_end_position.load(Ordering::Acquire);
        position_samples != previous_end
    }

    fn render_duration_from_position_span(&self) -> Option<u64> {
        if !self.render_start_valid.load(Ordering::Acquire)
            || !self.render_last_end_valid.load(Ordering::Acquire)
        {
            return None;
        }
        let rendered_frames = self.render_frames.load(Ordering::Acquire);
        if rendered_frames == 0 {
            return None;
        }
        let start = self.render_start_position.load(Ordering::Acquire);
        let end = self.render_last_end_position.load(Ordering::Acquire);
        if end >= start {
            Some(((end - start) as u64).min(rendered_frames))
        } else {
            None
        }
    }

    fn current_exact_host_range(
        &self,
        bounded_duration_samples: u64,
        unbounded_duration_samples: u64,
    ) -> Option<(i64, i64)> {
        let (start, end, duration) = if bounded_duration_samples > 0 {
            if !self.record_bounded_range_valid.load(Ordering::Acquire) {
                return None;
            }
            (
                self.record_bounded_start_position.load(Ordering::Acquire),
                self.record_bounded_end_position.load(Ordering::Acquire),
                bounded_duration_samples,
            )
        } else {
            if unbounded_duration_samples == 0
                || !self.record_unbounded_range_valid.load(Ordering::Acquire)
            {
                return None;
            }
            (
                self.record_unbounded_start_position.load(Ordering::Acquire),
                self.record_unbounded_end_position.load(Ordering::Acquire),
                unbounded_duration_samples,
            )
        };
        let duration = i64::try_from(duration).ok()?;
        end.checked_sub(start)
            .filter(|observed| *observed == duration)
            .map(|_| (start, end))
    }
}

fn presentation_latency_shift_matches(
    raw_shift: i64,
    previous: PresentationLatencySamples,
    next: PresentationLatencySamples,
) -> bool {
    let changed_by = |left: Option<u32>, right: Option<u32>| {
        left.zip(right)
            .map(|(left, right)| i64::from(right).saturating_sub(i64::from(left)))
    };
    let output_shift = changed_by(previous.output, next.output);
    let input_shift = changed_by(previous.input, next.input);
    let combined_shift = output_shift
        .zip(input_shift)
        .map(|(output, input)| [output.saturating_add(input), output.saturating_sub(input)]);
    raw_shift == 0
        || output_shift.is_some_and(|shift| raw_shift == shift || raw_shift == -shift)
        || input_shift.is_some_and(|shift| raw_shift == shift || raw_shift == -shift)
        || combined_shift.is_some_and(|shifts| {
            shifts
                .iter()
                .any(|shift| raw_shift == *shift || raw_shift == -*shift)
        })
}

/// Convert the raw processing position to the sample presented on the exported WAV timeline.
/// This function is intentionally producer-only; downstream code receives the converted sample
/// plus the separately retained raw facts and must not apply latency again.
#[inline]
pub fn wav_presentation_position_samples(
    host_position_samples: i64,
    output_presentation_samples: Option<u32>,
) -> Option<i64> {
    host_position_samples.checked_sub(i64::from(output_presentation_samples.unwrap_or(0)))
}

fn bounded_duration_from_block(block: &RecordTakeBlock) -> Option<u64> {
    let end = block.clock_end_samples?;
    if end > block.clock_start_samples {
        Some(end.saturating_sub(block.clock_start_samples) as u64)
    } else {
        None
    }
}

fn snapshot_from_durations(
    generation: u64,
    bounded_duration_samples: u64,
    unbounded_duration_samples: u64,
    host_range: Option<(i64, i64)>,
) -> Option<RecordTakeSnapshot> {
    let (host_start_position_samples, host_end_position_samples) = host_range.unzip();
    if bounded_duration_samples > 0 {
        Some(RecordTakeSnapshot {
            generation,
            duration_samples: bounded_duration_samples,
            source: RECORD_TAKE_SOURCE_WAV_CLOCK,
            host_start_position_samples,
            host_end_position_samples,
        })
    } else if unbounded_duration_samples > 0 {
        Some(RecordTakeSnapshot {
            generation,
            duration_samples: unbounded_duration_samples,
            source: RECORD_TAKE_SOURCE_RENDER_CLOCK,
            host_start_position_samples,
            host_end_position_samples,
        })
    } else {
        None
    }
}

pub fn new_record_take_tracker() -> Arc<RecordTakeTracker> {
    Arc::new(RecordTakeTracker::new())
}

#[cfg(test)]
mod tests {
    use super::{
        wav_presentation_position_samples, CaptureClockSource, PresentationLatencySamples,
        PresentationLatencySource, RecordTakeBlock, RecordTakeTracker,
        RECORD_TAKE_SOURCE_RENDER_CLOCK, RECORD_TAKE_SOURCE_WAV_CLOCK,
    };

    fn block(generation: u64, position_samples: i64, num_frames: u64) -> RecordTakeBlock {
        RecordTakeBlock {
            generation,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples,
            num_frames,
            clock_start_samples: 0,
            clock_end_samples: None,
        }
    }

    fn bounded_block(
        generation: u64,
        position_samples: i64,
        num_frames: u64,
        clock_start_samples: i64,
        clock_end_samples: i64,
    ) -> RecordTakeBlock {
        RecordTakeBlock {
            generation,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples,
            num_frames,
            clock_start_samples,
            clock_end_samples: Some(clock_end_samples),
        }
    }

    #[test]
    fn presentation_latency_changes_split_immutable_presentation_epochs() {
        let tracker = RecordTakeTracker::new();
        assert_eq!(
            tracker.presentation_latency(),
            PresentationLatencySamples::default()
        );
        let first = PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(9_600),
        };
        tracker.note_capture_window_with_presentation(
            true,
            1_000,
            256,
            CaptureClockSource::ProjectTimeline,
            first,
        );
        tracker.note_capture_window_with_presentation(
            true,
            1_256,
            256,
            CaptureClockSource::ProjectTimeline,
            first,
        );
        assert_eq!(tracker.presentation_latency(), first);
        let first_epoch = tracker.clock_point_for_captured_frame(512).unwrap().epoch;

        let changed = PresentationLatencySamples {
            source: PresentationLatencySource::AudioUnitV2,
            input: Some(9_600),
            output: Some(0),
        };
        tracker.note_capture_window_with_presentation(
            true,
            1_512,
            256,
            CaptureClockSource::ProjectTimeline,
            changed,
        );
        assert_eq!(tracker.presentation_latency(), changed);
        let changed_epoch = tracker.clock_point_for_captured_frame(768).unwrap().epoch;
        assert!(tracker.capture_epochs_are_latency_continuation(first_epoch, changed_epoch));
        let boundary = PresentationLatencySamples {
            source: PresentationLatencySource::AudioUnitV2,
            input: Some(u32::MAX),
            output: None,
        };
        tracker.note_capture_window_with_presentation(
            true,
            1_768,
            256,
            CaptureClockSource::ProjectTimeline,
            boundary,
        );
        assert_eq!(tracker.presentation_latency(), boundary);
        let boundary_epoch = tracker.clock_point_for_captured_frame(1_024).unwrap().epoch;
        assert!(tracker.capture_epochs_are_latency_continuation(changed_epoch, boundary_epoch));
        assert_eq!(
            tracker
                .capture_span_sequence
                .load(std::sync::atomic::Ordering::Acquire),
            3,
            "each latency mapping owns one immutable presentation epoch"
        );
    }

    #[test]
    fn presentation_range_crosses_only_contiguous_latency_epoch_chain() {
        let tracker = RecordTakeTracker::new();
        let first_latency = PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(256),
        };
        tracker.note_block(block(91, 100_256, 4_800));
        tracker.note_capture_window_with_presentation_boundary(
            true,
            100_256,
            4_800,
            CaptureClockSource::ProjectTimeline,
            first_latency,
            true,
        );
        let first_epoch = tracker.selected_capture_epoch(91).unwrap();

        let changed_latency = PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(512),
        };
        tracker.note_block(block(91, 105_312, 4_800));
        tracker.note_capture_window_with_presentation_boundary(
            true,
            105_312,
            4_800,
            CaptureClockSource::ProjectTimeline,
            changed_latency,
            false,
        );

        assert_eq!(
            tracker.presentation_range_for_latency_epoch_chain(first_epoch, 100_256, 9_600,),
            Some((100_000, 109_600))
        );

        tracker.note_block(block(91, 110_112, 4_800));
        tracker.note_capture_window_with_presentation_boundary(
            true,
            110_112,
            4_800,
            CaptureClockSource::ProjectTimeline,
            changed_latency,
            true,
        );
        assert_eq!(
            tracker.presentation_range_for_latency_epoch_chain(first_epoch, 100_256, 14_400,),
            None,
            "an explicit take boundary must not be stitched into one WAV range"
        );
    }

    #[test]
    fn explicit_take_start_splits_even_a_contiguous_host_position() {
        let tracker = RecordTakeTracker::new();
        let latency = PresentationLatencySamples {
            source: PresentationLatencySource::AudioUnitV2,
            input: Some(0),
            output: Some(7_676),
        };
        tracker.note_capture_window_with_presentation_boundary(
            true,
            0,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            false,
        );
        let watch_epoch = tracker.clock_point_for_captured_frame(512).unwrap().epoch;
        tracker.note_capture_window_with_presentation_boundary(
            true,
            512,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            true,
        );
        let take_start = tracker.clock_point_for_captured_frame(513).unwrap();

        assert_ne!(take_start.epoch, watch_epoch);
        assert_eq!(take_start.raw_host_position_samples, 513);
        assert_eq!(take_start.position_samples, -7_163);
        assert_eq!(take_start.presentation_latency, latency);
    }

    #[test]
    fn output_presentation_latency_rebases_pre_and_post_to_one_wav_sample() {
        assert_eq!(
            wav_presentation_position_samples(48_000, Some(0)),
            Some(48_000)
        );
        assert_eq!(
            wav_presentation_position_samples(48_000, Some(256)),
            Some(47_744)
        );

        let pre = RecordTakeTracker::new();
        pre.note_capture_window_with_presentation(
            true,
            48_256,
            512,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(256),
            },
        );
        let post = RecordTakeTracker::new();
        post.note_capture_window_with_presentation(
            true,
            48_000,
            512,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(256),
                output: Some(0),
            },
        );
        assert_eq!(pre.position_samples_for_captured_frame(512), Some(48_512));
        assert_eq!(post.position_samples_for_captured_frame(512), Some(48_512));
        let pre_point = pre.clock_point_for_captured_frame(512).unwrap();
        let post_point = post.clock_point_for_captured_frame(512).unwrap();
        assert_eq!(pre_point.position_samples, post_point.position_samples);
        assert_eq!(pre_point.raw_host_position_samples, 48_768);
        assert_eq!(post_point.raw_host_position_samples, 48_512);
        assert_eq!(pre_point.presentation_latency.output, Some(256));
        assert_eq!(post_point.presentation_latency.output, Some(0));
    }

    #[test]
    fn au_and_vst3_7676_sample_observations_share_one_wav_axis() {
        let pre = RecordTakeTracker::new();
        pre.note_capture_window_with_presentation(
            true,
            7_676,
            9_600,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples {
                source: PresentationLatencySource::AudioUnitV2,
                input: Some(0),
                output: Some(7_676),
            },
        );
        let post = RecordTakeTracker::new();
        post.note_capture_window_with_presentation(
            true,
            0,
            9_600,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(7_676),
                output: Some(0),
            },
        );

        let pre_point = pre.clock_point_for_captured_frame(9_600).unwrap();
        let post_point = post.clock_point_for_captured_frame(9_600).unwrap();
        assert_eq!(pre_point.position_samples, 9_600);
        assert_eq!(pre_point.position_samples, post_point.position_samples);
        assert_ne!(
            pre_point.raw_host_position_samples,
            post_point.raw_host_position_samples
        );
    }

    #[test]
    fn studio_one_six_role_capture_rebases_to_one_content_range() {
        let observations = [
            (6_478_013, 7_938_237, 16_630),
            (6_477_492, 7_937_716, 16_109),
            (6_473_347, 7_933_571, 11_964),
            (6_465_671, 7_925_895, 4_288),
            (6_475_257, 7_935_481, 13_874),
            (6_473_347, 7_933_571, 11_964),
        ];

        for (raw_start, raw_end, output_latency) in observations {
            assert_eq!(
                wav_presentation_position_samples(raw_start, Some(output_latency)),
                Some(6_461_383)
            );
            assert_eq!(
                wav_presentation_position_samples(raw_end, Some(output_latency)),
                Some(7_921_607)
            );
        }
    }

    #[test]
    fn presentation_conversion_never_saturates_at_i64_boundary() {
        assert_eq!(wav_presentation_position_samples(i64::MIN, Some(1)), None);
    }

    #[test]
    fn offline_render_before_record_edge_does_not_pollute_record_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            recording: false,
            ..block(0, 0, 512)
        });
        tracker.note_block(block(7, 512, 512));

        let snap = tracker.snapshot(7).expect("clean take");
        assert_eq!(snap.duration_samples, 512);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_RENDER_CLOCK);
    }

    #[test]
    fn realtime_playback_before_keep_does_not_pollute_record_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            recording: false,
            playing: true,
            offline: false,
            ..block(0, 0, 48_000)
        });
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(3, 48_000, 1_024)
        });

        assert_eq!(tracker.snapshot(3).unwrap().duration_samples, 1_024);
    }

    #[test]
    fn capture_clock_preserves_repeated_host_range_as_distinct_passes() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 0, 100);
        tracker.note_capture_window(true, 100, 100);
        tracker.note_capture_window(true, 0, 100);

        assert_eq!(tracker.position_samples_for_captured_frame(50), Some(50));
        assert_eq!(tracker.position_samples_for_captured_frame(150), Some(150));
        assert_eq!(tracker.position_samples_for_captured_frame(250), Some(50));
        let repeated = tracker.capture_span_for_frame(201).expect("second pass");
        assert_eq!(repeated.capture_start_frame, 200);
        assert_eq!(repeated.position_start_samples, Some(0));
    }

    #[test]
    fn capture_clock_preserves_au_render_fallback_provenance() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window_with_source(
            true,
            48_000,
            512,
            CaptureClockSource::AudioRenderTimeline,
        );
        let point = tracker
            .clock_point_for_captured_frame(512)
            .expect("render clock point");
        assert_eq!(point.position_samples, 48_512);
        assert_eq!(point.source, CaptureClockSource::AudioRenderTimeline);
    }

    #[test]
    fn capture_clock_source_change_never_merges_two_clock_domains() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window_with_source(true, 0, 256, CaptureClockSource::ProjectTimeline);
        tracker.note_capture_window_with_source(
            true,
            256,
            256,
            CaptureClockSource::AudioRenderTimeline,
        );
        assert_eq!(
            tracker.clock_point_for_captured_frame(256).unwrap().source,
            CaptureClockSource::ProjectTimeline
        );
        assert_eq!(
            tracker.clock_point_for_captured_frame(512).unwrap().source,
            CaptureClockSource::AudioRenderTimeline
        );
    }

    #[test]
    fn capture_clock_aggregates_contiguous_blocks_without_losing_boundaries() {
        let tracker = RecordTakeTracker::new();
        for block in 0..10_000_i64 {
            tracker.note_capture_window(true, block * 32, 32);
        }
        let span = tracker
            .capture_span_for_frame(320_000)
            .expect("aggregated span");
        assert_eq!(span.capture_start_frame, 0);
        assert_eq!(span.capture_end_frame, 320_000);
        assert_eq!(span.position_for_captured_frame(320_000), Some(320_000));
    }

    #[test]
    fn observed_content_range_requires_one_complete_contiguous_span() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 96_000, 48_000);
        tracker.note_capture_window(true, 144_000, 48_000);

        assert!(tracker.observed_content_range(96_000, 192_000));
        assert!(tracker.observed_content_range(100_000, 180_000));
        assert!(!tracker.observed_content_range(95_999, 192_000));
        assert!(!tracker.observed_content_range(96_000, 192_001));
    }

    #[test]
    fn observed_content_range_rejects_gaps_and_unknown_clocks() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 0, 48_000);
        tracker.note_capture_window(true, 96_000, 48_000);
        tracker.note_capture_window_with_source(true, 144_000, 48_000, CaptureClockSource::Unknown);

        assert!(!tracker.observed_content_range(0, 144_000));
        assert!(!tracker.observed_content_range(144_000, 192_000));
    }

    #[test]
    fn later_bwf_containing_epoch_never_replaces_selected_take_epoch() {
        let tracker = RecordTakeTracker::new();
        let latency = PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(100),
        };

        tracker.note_block(block(73, 1_000, 500));
        tracker.note_capture_window_with_presentation_boundary(
            true,
            1_000,
            500,
            CaptureClockSource::ProjectTimeline,
            latency,
            true,
        );
        let initially_selected = tracker.selected_capture_epoch(73).unwrap();

        // A later same-priority epoch cannot replace the take at writer close, even if its clock
        // happens to contain the BWF interval. Measure has already accepted only the immutable
        // selected epoch and discarded this post-bounce pass.
        tracker.note_block(block(73, 0, 1_000));
        tracker.note_capture_window_with_presentation_boundary(
            true,
            0,
            1_000,
            CaptureClockSource::ProjectTimeline,
            latency,
            true,
        );
        let later_epoch = tracker
            .capture_span_for_frame(1_500)
            .expect("later epoch")
            .epoch;

        assert_eq!(tracker.selected_capture_epoch(73), Some(initially_selected));
        assert_ne!(later_epoch, initially_selected);
        assert_eq!(
            tracker.capture_epoch_containing_presentation_range(73, 100, 1_000),
            None,
            "BWF proof cannot resurrect a later epoch whose samples were already dropped"
        );
    }

    #[test]
    fn watch_offline_epoch_promotes_into_first_record_callback() {
        let tracker = RecordTakeTracker::new();
        let latency = PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(100),
        };

        // The host begins the exported offline pass before the Keep acknowledgement reaches the
        // producer. These callbacks are raw pre-roll for this exact pass, not normal Watch history.
        tracker.note_block(RecordTakeBlock {
            recording: false,
            ..block(0, 200, 512)
        });
        tracker.note_capture_window_with_presentation_boundary(
            true,
            200,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            false,
        );
        let offline_start_epoch = tracker
            .capture_span_for_frame(512)
            .expect("offline start")
            .epoch;

        tracker.note_block(RecordTakeBlock {
            recording: false,
            ..block(0, 712, 512)
        });
        tracker.note_capture_window_with_presentation_boundary(
            true,
            712,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            false,
        );

        // force_new_epoch is true at the Record acknowledgement edge, but exact producer
        // continuity proves that this is still the same WAV pass and therefore the same epoch.
        tracker.note_block(block(80, 1_224, 512));
        tracker.note_capture_window_with_presentation_boundary(
            true,
            1_224,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            true,
        );

        let promoted = tracker.capture_span_for_frame(1_536).expect("record tail");
        assert_eq!(promoted.epoch, offline_start_epoch);
        assert_eq!(promoted.generation, 80);
        assert_eq!(promoted.capture_start_frame, 0);
        assert_eq!(promoted.capture_end_frame, 1_536);
        assert_eq!(tracker.selected_capture_epoch(80), Some(promoted.epoch));
        assert_eq!(
            tracker.capture_epoch_containing_presentation_range(80, 100, 1_536),
            Some(promoted.epoch)
        );
        assert_eq!(
            tracker
                .raw_host_range_for_capture_epoch_presentation_range(promoted.epoch, 100, 1_636,),
            Some((200, 1_736))
        );
    }

    #[test]
    fn normal_realtime_watch_history_never_promotes_into_record() {
        let tracker = RecordTakeTracker::new();
        let latency = PresentationLatencySamples {
            source: PresentationLatencySource::AudioUnitV2,
            input: Some(0),
            output: Some(100),
        };

        tracker.note_block(RecordTakeBlock {
            recording: false,
            playing: true,
            offline: false,
            ..block(0, 200, 512)
        });
        tracker.note_capture_window_with_presentation_boundary(
            true,
            200,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            false,
        );
        let watch_epoch = tracker.capture_span_for_frame(512).expect("watch").epoch;

        tracker.note_block(block(81, 712, 512));
        tracker.note_capture_window_with_presentation_boundary(
            true,
            712,
            512,
            CaptureClockSource::ProjectTimeline,
            latency,
            true,
        );
        let record_span = tracker.capture_span_for_frame(1_024).expect("record");

        assert_ne!(record_span.epoch, watch_epoch);
        assert_eq!(record_span.generation, 81);
        assert_eq!(record_span.capture_start_frame, 512);
        assert_eq!(
            tracker.capture_epoch_containing_presentation_range(81, 100, 1_024),
            None,
            "generation changes cannot relabel ordinary realtime Watch history"
        );
        assert_eq!(
            tracker.capture_epoch_containing_presentation_range(81, 612, 512),
            Some(record_span.epoch)
        );
    }

    #[test]
    fn watch_offline_promotion_requires_exact_position_and_latency_continuity() {
        let watch_latency = PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(100),
        };
        for (generation, record_position, record_latency) in [
            (82, 1_024, watch_latency),
            (
                83,
                512,
                PresentationLatencySamples {
                    output: Some(101),
                    ..watch_latency
                },
            ),
        ] {
            let tracker = RecordTakeTracker::new();
            tracker.note_block(RecordTakeBlock {
                recording: false,
                ..block(0, 0, 512)
            });
            tracker.note_capture_window_with_presentation_boundary(
                true,
                0,
                512,
                CaptureClockSource::ProjectTimeline,
                watch_latency,
                false,
            );
            let watch_epoch = tracker.capture_span_for_frame(512).unwrap().epoch;

            tracker.note_block(block(generation, record_position, 512));
            tracker.note_capture_window_with_presentation_boundary(
                true,
                record_position,
                512,
                CaptureClockSource::ProjectTimeline,
                record_latency,
                true,
            );
            let record_epoch = tracker.capture_span_for_frame(1_024).unwrap().epoch;

            assert_ne!(record_epoch, watch_epoch);
            assert_eq!(
                tracker.selected_capture_epoch(generation),
                Some(record_epoch)
            );
            assert_eq!(
                tracker.capture_epoch_containing_presentation_range(generation, 100, 1_024,),
                None,
                "discontinuous Watch audio cannot be relabeled to fill the BWF prefix"
            );
        }
    }

    #[test]
    fn bwf_presentation_range_outside_record_epoch_returns_none() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(74, 0, 1_000));
        tracker.note_capture_window_with_presentation_boundary(
            true,
            0,
            1_000,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples {
                source: PresentationLatencySource::AudioUnitV2,
                input: Some(0),
                output: Some(100),
            },
            true,
        );

        assert_eq!(
            tracker.capture_epoch_containing_presentation_range(74, 99, 1_000),
            None
        );
        assert_eq!(
            tracker.capture_epoch_containing_presentation_range(74, 100, 1_001),
            None
        );
        assert_eq!(
            tracker.capture_epoch_containing_presentation_range(75, 100, 1_000),
            None,
            "another generation cannot reuse the same project range"
        );
    }

    #[test]
    fn stopped_tail_after_record_does_not_extend_take() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(9, 0, 44_100)
        });
        tracker.note_block(RecordTakeBlock {
            rendered: false,
            offline: false,
            ..block(9, 44_100, 44_100)
        });

        let snapshot = tracker.snapshot(9).unwrap();
        assert_eq!(snapshot.duration_samples, 44_100);
        assert_eq!(snapshot.host_start_position_samples, Some(0));
        assert_eq!(snapshot.host_end_position_samples, Some(44_100));
    }

    #[test]
    fn rendered_capture_block_counts_even_when_host_flags_are_idle() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            playing: false,
            offline: false,
            ..block(14, 0, 512)
        });

        assert_eq!(tracker.snapshot(14).unwrap().duration_samples, 512);
    }

    #[test]
    fn invalid_position_never_becomes_clean_take_duration() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            position_valid: false,
            ..block(19, i64::MIN, 1_440_000)
        });

        assert_eq!(tracker.snapshot(19), None);
    }

    #[test]
    fn capture_clock_maps_consumed_frames_to_transport_position() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 96_000, 512);
        tracker.note_capture_window(true, 96_512, 512);

        assert_eq!(
            tracker.position_samples_for_captured_frame(512),
            Some(96_512)
        );
        assert_eq!(
            tracker.position_samples_for_captured_frame(1_024),
            Some(97_024)
        );
    }

    #[test]
    fn capture_clock_drops_mapping_after_invalid_position() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 96_000, 512);
        assert!(tracker.position_samples_for_captured_frame(512).is_some());

        tracker.note_capture_window(false, i64::MIN, 512);

        assert_eq!(tracker.position_samples_for_captured_frame(1_024), None);
    }

    #[test]
    fn capture_clock_reset_rebases_replacement_ring_cursor() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 48_000, 512);
        let old_epoch = tracker
            .capture_span_for_frame(512)
            .expect("old ring epoch")
            .epoch;
        assert_eq!(
            tracker.position_samples_for_captured_frame(512),
            Some(48_512)
        );

        tracker.reset_capture_clock();
        assert_eq!(tracker.position_samples_for_captured_frame(512), None);

        tracker.note_capture_window(true, 96_000, 256);
        let replacement_epoch = tracker
            .capture_span_for_frame(256)
            .expect("replacement ring epoch")
            .epoch;
        assert_ne!(replacement_epoch, old_epoch);
        assert_eq!(
            tracker.position_samples_for_captured_frame(256),
            Some(96_256)
        );
        assert_eq!(
            tracker.presentation_range_for_capture_epoch(old_epoch, 48_000, 48_512),
            Some((48_000, 48_512)),
            "an IO close racing ring replacement keeps its immutable old epoch"
        );
    }

    #[test]
    fn later_longer_offline_epoch_does_not_replace_first_offline_epoch() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(4, 10_000, 1_000));
        tracker.note_block(block(4, 0, 2_000));

        let snapshot = tracker.snapshot(4).unwrap();
        assert_eq!(snapshot.duration_samples, 1_000);
        assert_eq!(snapshot.host_start_position_samples, Some(10_000));
        assert_eq!(snapshot.host_end_position_samples, Some(11_000));
    }

    #[test]
    fn epoch_authority_upgrades_rendered_then_offline_then_native_only_once() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            offline: false,
            ..block(5, 0, 400)
        });
        tracker.note_block(block(5, 400, 400));
        tracker.note_block(bounded_block(5, 800, 100, 100, 1_100));
        tracker.note_block(bounded_block(5, 0, 5_000, 0, 5_000));

        let snapshot = tracker.snapshot(5).expect("native take");
        assert_eq!(snapshot.source, RECORD_TAKE_SOURCE_WAV_CLOCK);
        assert_eq!(snapshot.duration_samples, 1_000);
        assert_eq!(snapshot.host_start_position_samples, Some(100));
        assert_eq!(snapshot.host_end_position_samples, Some(1_100));
    }

    #[test]
    fn position_span_prevents_duplicate_offline_prefix_overcount() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            recording: false,
            ..block(0, 0, 1_000)
        });
        tracker.note_block(RecordTakeBlock {
            recording: false,
            ..block(0, 0, 1_000)
        });
        tracker.note_block(block(11, 1_000, 1_000));

        assert_eq!(tracker.snapshot(11).unwrap().duration_samples, 1_000);
    }

    #[test]
    fn render_clock_fallback_never_exceeds_rendered_frames() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(12, 0, 1_000));
        tracker.note_block(block(12, 10_000, 1_000));

        let snap = tracker.snapshot(12).expect("render fallback");
        assert_eq!(snap.duration_samples, 1_000);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_RENDER_CLOCK);
        assert_eq!(snap.host_start_position_samples, Some(0));
        assert_eq!(snap.host_end_position_samples, Some(1_000));
    }

    #[test]
    fn continuous_render_clock_keeps_position_span_when_it_matches_rendered_frames() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(13, 96_000, 1_024));
        tracker.note_block(block(13, 97_024, 2_048));

        let snap = tracker.snapshot(13).expect("continuous render fallback");
        assert_eq!(snap.duration_samples, 3_072);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_RENDER_CLOCK);
        assert_eq!(snap.host_start_position_samples, Some(96_000));
        assert_eq!(snap.host_end_position_samples, Some(99_072));
    }

    #[test]
    fn snapshot_retains_previous_generation_after_next_record_starts() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(21, 0, 44_100)
        });
        tracker.note_capture_window_with_presentation_boundary(
            true,
            0,
            44_100,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples::default(),
            true,
        );
        let first_capture_epoch = tracker.selected_capture_epoch(21).unwrap();
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(22, 44_100, 1_024)
        });

        assert_eq!(tracker.snapshot(21).unwrap().duration_samples, 44_100);
        assert_eq!(
            tracker.selected_capture_epoch(21),
            Some(first_capture_epoch),
            "immutable selection survives IO close racing the next Keep generation"
        );
        assert_eq!(tracker.snapshot(22).unwrap().duration_samples, 1_024);
    }

    #[test]
    fn later_short_fragment_does_not_shrink_same_record_generation() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(31, 0, 1_440_000));
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(31, 0, 2_048)
        });

        assert_eq!(tracker.snapshot(31).unwrap().duration_samples, 1_440_000);
    }

    #[test]
    fn bounded_wav_clock_wins_over_long_process_tail() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(bounded_block(41, 0, 1_440_000, 0, 1_440_000));
        tracker.note_block(block(41, 1_440_000, 20_224));

        let snap = tracker.snapshot(41).expect("bounded take");
        assert_eq!(snap.duration_samples, 1_440_000);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_WAV_CLOCK);
        assert_eq!(snap.host_start_position_samples, Some(0));
        assert_eq!(snap.host_end_position_samples, Some(1_440_000));
    }

    #[test]
    fn nonzero_wav_clock_start_reports_wav_duration_not_position_span() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(bounded_block(42, 96_000, 512, 96_000, 1_536_000));

        let snap = tracker.snapshot(42).expect("bounded take");
        assert_eq!(snap.duration_samples, 1_440_000);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_WAV_CLOCK);
        assert_eq!(snap.host_start_position_samples, Some(96_000));
        assert_eq!(snap.host_end_position_samples, Some(1_536_000));
    }
}
