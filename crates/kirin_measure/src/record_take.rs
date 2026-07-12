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
    pub position_samples: i64,
    pub source: CaptureClockSource,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct CaptureClockSpan {
    pub capture_start_frame: u64,
    pub capture_end_frame: u64,
    pub position_start_samples: Option<i64>,
    pub source: CaptureClockSource,
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
}

#[derive(Debug)]
struct CaptureClockSlot {
    version: AtomicU64,
    sequence: AtomicU64,
    capture_start_frame: AtomicU64,
    capture_end_frame: AtomicU64,
    position_valid: AtomicBool,
    position_start_samples: AtomicI64,
    source: AtomicU8,
}

impl CaptureClockSlot {
    fn new() -> Self {
        Self {
            version: AtomicU64::new(0),
            sequence: AtomicU64::new(0),
            capture_start_frame: AtomicU64::new(0),
            capture_end_frame: AtomicU64::new(0),
            position_valid: AtomicBool::new(false),
            position_start_samples: AtomicI64::new(i64::MIN),
            source: AtomicU8::new(CaptureClockSource::Unknown as u8),
        }
    }

    fn publish(
        &self,
        sequence: u64,
        capture_start_frame: u64,
        capture_end_frame: u64,
        position_start_samples: Option<i64>,
        source: CaptureClockSource,
    ) {
        self.version.fetch_add(1, Ordering::AcqRel);
        self.sequence.store(sequence, Ordering::Relaxed);
        self.capture_start_frame
            .store(capture_start_frame, Ordering::Relaxed);
        self.capture_end_frame
            .store(capture_end_frame, Ordering::Relaxed);
        self.position_valid
            .store(position_start_samples.is_some(), Ordering::Relaxed);
        self.position_start_samples.store(
            position_start_samples.unwrap_or(i64::MIN),
            Ordering::Relaxed,
        );
        self.source.store(source as u8, Ordering::Relaxed);
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

    fn read(&self, sequence: u64) -> Option<CaptureClockSpan> {
        for _ in 0..4 {
            let before = self.version.load(Ordering::Acquire);
            if before & 1 != 0 || self.sequence.load(Ordering::Acquire) != sequence {
                continue;
            }
            let capture_start_frame = self.capture_start_frame.load(Ordering::Relaxed);
            let capture_end_frame = self.capture_end_frame.load(Ordering::Relaxed);
            let position_start_samples = self
                .position_valid
                .load(Ordering::Relaxed)
                .then(|| self.position_start_samples.load(Ordering::Relaxed));
            let source = CaptureClockSource::from_abi(self.source.load(Ordering::Relaxed));
            let after = self.version.load(Ordering::Acquire);
            if before == after && after & 1 == 0 {
                return Some(CaptureClockSpan {
                    capture_start_frame,
                    capture_end_frame,
                    position_start_samples,
                    source,
                });
            }
        }
        None
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
}

#[derive(Debug)]
pub struct RecordTakeTracker {
    capture_frames_total: AtomicU64,
    capture_span_sequence: AtomicU64,
    capture_last_span_sequence: AtomicU64,
    capture_last_position_valid: AtomicBool,
    capture_last_position_end_samples: AtomicI64,
    capture_last_source: AtomicU8,
    presentation_version: AtomicU64,
    presentation_source: AtomicU8,
    input_presentation_samples: AtomicU64,
    output_presentation_samples: AtomicU64,
    capture_clock_slots: Box<[CaptureClockSlot]>,
    render_active: AtomicBool,
    render_epoch: AtomicU64,
    render_frames: AtomicU64,
    render_start_valid: AtomicBool,
    render_start_position: AtomicI64,
    render_last_end_valid: AtomicBool,
    render_last_end_position: AtomicI64,
    record_generation: AtomicU64,
    record_render_epoch: AtomicU64,
    record_bounded_duration_samples: AtomicU64,
    record_unbounded_duration_samples: AtomicU64,
    previous_generation: AtomicU64,
    previous_bounded_duration_samples: AtomicU64,
    previous_unbounded_duration_samples: AtomicU64,
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
            capture_last_span_sequence: AtomicU64::new(0),
            capture_last_position_valid: AtomicBool::new(false),
            capture_last_position_end_samples: AtomicI64::new(i64::MIN),
            capture_last_source: AtomicU8::new(CaptureClockSource::Unknown as u8),
            presentation_version: AtomicU64::new(0),
            presentation_source: AtomicU8::new(PresentationLatencySource::Unknown as u8),
            input_presentation_samples: AtomicU64::new(u64::MAX),
            output_presentation_samples: AtomicU64::new(u64::MAX),
            capture_clock_slots: (0..CAPTURE_CLOCK_SPAN_CAPACITY)
                .map(|_| CaptureClockSlot::new())
                .collect::<Vec<_>>()
                .into_boxed_slice(),
            render_active: AtomicBool::new(false),
            render_epoch: AtomicU64::new(0),
            render_frames: AtomicU64::new(0),
            render_start_valid: AtomicBool::new(false),
            render_start_position: AtomicI64::new(i64::MIN),
            render_last_end_valid: AtomicBool::new(false),
            render_last_end_position: AtomicI64::new(i64::MIN),
            record_generation: AtomicU64::new(0),
            record_render_epoch: AtomicU64::new(0),
            record_bounded_duration_samples: AtomicU64::new(0),
            record_unbounded_duration_samples: AtomicU64::new(0),
            previous_generation: AtomicU64::new(0),
            previous_bounded_duration_samples: AtomicU64::new(0),
            previous_unbounded_duration_samples: AtomicU64::new(0),
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
        // VST3 defines input presentation latency as the elapsed samples from
        // generation/acquisition until this plug-in input receives the audio.
        // Therefore a block received at host position P contains content from
        // P - input_latency. Normalise here, before any measurement frame is
        // formed, so PRE and POST share content coordinates without comparing
        // their metric shapes later.
        let content_position_samples = position_valid
            .then(|| content_position_samples(position_samples, presentation_latency.input));
        let capture_start_frame = self.capture_frames_total.load(Ordering::Acquire);
        let frames_end = self
            .capture_frames_total
            .fetch_add(num_frames, Ordering::AcqRel)
            .saturating_add(num_frames);
        let previous_sequence = self.capture_last_span_sequence.load(Ordering::Acquire);
        let position_contiguous = content_position_samples.is_some()
            && source != CaptureClockSource::Unknown
            && self.capture_last_position_valid.load(Ordering::Acquire)
            && self.capture_last_source.load(Ordering::Acquire) == source as u8
            && self
                .capture_last_position_end_samples
                .load(Ordering::Acquire)
                == content_position_samples.unwrap_or(i64::MIN);
        let extended = if previous_sequence > 0 && position_contiguous {
            let index = previous_sequence as usize % self.capture_clock_slots.len();
            self.capture_clock_slots[index].extend(previous_sequence, frames_end)
        } else {
            false
        };
        if !extended {
            let sequence = self
                .capture_span_sequence
                .fetch_add(1, Ordering::AcqRel)
                .saturating_add(1);
            let index = sequence as usize % self.capture_clock_slots.len();
            self.capture_clock_slots[index].publish(
                sequence,
                capture_start_frame,
                frames_end,
                content_position_samples,
                source,
            );
            self.capture_last_span_sequence
                .store(sequence, Ordering::Release);
        }
        self.capture_last_position_valid
            .store(content_position_samples.is_some(), Ordering::Release);
        self.capture_last_position_end_samples.store(
            content_position_samples
                .unwrap_or(i64::MIN)
                .saturating_add(num_frames as i64),
            Ordering::Release,
        );
        self.capture_last_source
            .store(source as u8, Ordering::Release);
    }

    /// Latest optional host callback values. This is deliberately independent of capture-clock
    /// span construction so a diagnostic-only host feature cannot alter existing TRACE positions.
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

    pub fn clock_point_for_captured_frame(
        &self,
        captured_frames: u64,
    ) -> Option<CaptureClockPoint> {
        let span = self.capture_span_for_frame(captured_frames)?;
        let position_samples = span.position_for_captured_frame(captured_frames)?;
        (span.source != CaptureClockSource::Unknown).then_some(CaptureClockPoint {
            position_samples,
            source: span.source,
        })
    }

    /// Start the capture clock for a replacement SPSC ring.
    ///
    /// The Measure Thread consuming the new ring starts its native frame cursor at zero. The
    /// Audio Thread must therefore reset this producer-side mapping in the same callback that
    /// installs the replacement Producer. Atomics only; no allocation, lock, logging, or I/O.
    pub fn reset_capture_clock(&self) {
        self.capture_frames_total.store(0, Ordering::Release);
        self.capture_span_sequence.store(0, Ordering::Release);
        self.capture_last_span_sequence.store(0, Ordering::Release);
        self.capture_last_position_valid
            .store(false, Ordering::Release);
        self.capture_last_position_end_samples
            .store(i64::MIN, Ordering::Release);
        self.capture_last_source
            .store(CaptureClockSource::Unknown as u8, Ordering::Release);
    }

    pub(crate) fn capture_span_for_frame(&self, captured_frame: u64) -> Option<CaptureClockSpan> {
        if captured_frame == 0 {
            return None;
        }
        let latest = self.capture_span_sequence.load(Ordering::Acquire);
        let earliest = latest
            .saturating_sub(self.capture_clock_slots.len() as u64)
            .saturating_add(1)
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

    /// Audio-thread note. This is atomics only: no allocation, lock, filesystem,
    /// logging, or blocking call.
    pub fn note_block(&self, block: RecordTakeBlock) {
        if block.recording && block.generation > 0 {
            self.ensure_record_generation(block.generation);
        }

        let render_eligible =
            block.rendered && block.num_frames > 0 && block.recording && block.position_valid;

        if !render_eligible {
            self.render_active.store(false, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
            return;
        }

        let reset_epoch = !self.render_active.load(Ordering::Acquire)
            || self.position_discontinuity(block.position_valid, block.position_samples);
        let epoch = if reset_epoch {
            self.render_frames.store(0, Ordering::Release);
            self.render_start_position
                .store(block.position_samples, Ordering::Release);
            self.render_start_valid.store(true, Ordering::Release);
            self.render_last_end_valid.store(false, Ordering::Release);
            let next = self.render_epoch.fetch_add(1, Ordering::AcqRel) + 1;
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

        if block.recording && block.generation > 0 {
            let current_epoch = self.record_render_epoch.load(Ordering::Acquire);
            if current_epoch != epoch {
                self.record_render_epoch.store(epoch, Ordering::Release);
            }
            if let Some(duration) = bounded_duration_from_block(&block) {
                self.record_bounded_duration_samples
                    .fetch_max(duration, Ordering::AcqRel);
            }
            if let Some(duration) = self.render_duration_from_position_span() {
                self.record_unbounded_duration_samples
                    .fetch_max(duration, Ordering::AcqRel);
            }
        }
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
            snapshot_from_durations(
                generation,
                bounded_duration_samples,
                unbounded_duration_samples,
            )
        } else if self.previous_generation.load(Ordering::Acquire) == expected_generation {
            let bounded_duration_samples = self
                .previous_bounded_duration_samples
                .load(Ordering::Acquire);
            let unbounded_duration_samples = self
                .previous_unbounded_duration_samples
                .load(Ordering::Acquire);
            snapshot_from_durations(
                expected_generation,
                bounded_duration_samples,
                unbounded_duration_samples,
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
        self.record_bounded_duration_samples
            .store(0, Ordering::Release);
        self.record_unbounded_duration_samples
            .store(0, Ordering::Release);
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
            self.previous_bounded_duration_samples
                .store(bounded_duration_samples, Ordering::Release);
            self.previous_unbounded_duration_samples
                .store(unbounded_duration_samples, Ordering::Release);
            self.previous_generation
                .store(generation, Ordering::Release);
        }
    }

    fn position_discontinuity(&self, position_valid: bool, position_samples: i64) -> bool {
        if !position_valid || !self.render_last_end_valid.load(Ordering::Acquire) {
            return false;
        }
        let previous_end = self.render_last_end_position.load(Ordering::Acquire);
        position_samples < previous_end.saturating_sub(1)
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
            // Transport position can jump forward without delivering the skipped samples.
            // The fallback clock must never demand more TRACE data than Hypha actually saw.
            Some(((end - start) as u64).min(rendered_frames))
        } else {
            None
        }
    }
}

/// Convert a host callback position to the sample position of the audio
/// content present at the plug-in input.
#[inline]
pub fn content_position_samples(
    host_position_samples: i64,
    input_presentation_samples: Option<u32>,
) -> i64 {
    host_position_samples.saturating_sub(i64::from(input_presentation_samples.unwrap_or(0)))
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
) -> Option<RecordTakeSnapshot> {
    if bounded_duration_samples > 0 {
        Some(RecordTakeSnapshot {
            generation,
            duration_samples: bounded_duration_samples,
            source: RECORD_TAKE_SOURCE_WAV_CLOCK,
        })
    } else if unbounded_duration_samples > 0 {
        Some(RecordTakeSnapshot {
            generation,
            duration_samples: unbounded_duration_samples,
            source: RECORD_TAKE_SOURCE_RENDER_CLOCK,
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
        content_position_samples, CaptureClockSource, PresentationLatencySamples,
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
    fn presentation_latency_changes_split_content_clock_spans() {
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
        assert_eq!(
            tracker
                .capture_span_sequence
                .load(std::sync::atomic::Ordering::Acquire),
            3,
            "each input-latency generation owns its own content-clock span"
        );
    }

    #[test]
    fn pre_and_delayed_post_map_the_same_content_position() {
        assert_eq!(content_position_samples(48_000, Some(0)), 48_000);
        assert_eq!(content_position_samples(48_256, Some(256)), 48_000);

        let pre = RecordTakeTracker::new();
        pre.note_capture_window_with_presentation(
            true,
            48_000,
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
            48_256,
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

        assert_eq!(tracker.snapshot(9).unwrap().duration_samples, 44_100);
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
        assert_eq!(
            tracker.position_samples_for_captured_frame(512),
            Some(48_512)
        );

        tracker.reset_capture_clock();
        assert_eq!(tracker.position_samples_for_captured_frame(512), None);

        tracker.note_capture_window(true, 96_000, 256);
        assert_eq!(
            tracker.position_samples_for_captured_frame(256),
            Some(96_256)
        );
    }

    #[test]
    fn position_rewind_starts_a_new_render_epoch() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(4, 10_000, 1_000));
        tracker.note_block(block(4, 0, 2_000));

        assert_eq!(tracker.snapshot(4).unwrap().duration_samples, 2_000);
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
        assert_eq!(snap.duration_samples, 2_000);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_RENDER_CLOCK);
    }

    #[test]
    fn continuous_render_clock_keeps_position_span_when_it_matches_rendered_frames() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(block(13, 96_000, 1_024));
        tracker.note_block(block(13, 97_024, 2_048));

        let snap = tracker.snapshot(13).expect("continuous render fallback");
        assert_eq!(snap.duration_samples, 3_072);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_RENDER_CLOCK);
    }

    #[test]
    fn snapshot_retains_previous_generation_after_next_record_starts() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(21, 0, 44_100)
        });
        tracker.note_block(RecordTakeBlock {
            playing: true,
            offline: false,
            ..block(22, 44_100, 1_024)
        });

        assert_eq!(tracker.snapshot(21).unwrap().duration_samples, 44_100);
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
    }

    #[test]
    fn nonzero_wav_clock_start_reports_wav_duration_not_position_span() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(bounded_block(42, 96_000, 512, 96_000, 1_536_000));

        let snap = tracker.snapshot(42).expect("bounded take");
        assert_eq!(snap.duration_samples, 1_440_000);
        assert_eq!(snap.source, RECORD_TAKE_SOURCE_WAV_CLOCK);
    }
}
