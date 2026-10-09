//! Selected Record take initialization and immutable WAV mapping.
use super::*;

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
            capture_last_auxiliary_valid: AtomicBool::new(false),
            capture_last_auxiliary_end_samples: AtomicI64::new(i64::MIN),
            capture_last_auxiliary_source: AtomicU8::new(AuxiliaryClockSource::Unknown as u8),
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
            record_audio_mapping: clock_capture::RecordAudioMapping::new(),
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
        self.record_audio_span_for_epoch(selected)
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
        let span = self.record_audio_span_for_epoch(epoch)?;
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
        let span = self.record_audio_span_for_epoch(epoch)?;
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

    fn record_audio_span_for_epoch(&self, epoch: u64) -> Option<CaptureClockSpan> {
        // Use the longer factual prefix without changing Watch/pair span lookup or its TTL.
        let ring = self.capture_span_for_epoch(epoch);
        let prefix = self.record_audio_mapping.read(epoch);
        match (ring, prefix) {
            (Some(ring), Some(prefix)) if ring.capture_end_frame >= prefix.capture_end_frame => {
                Some(ring)
            }
            (_, Some(prefix)) => Some(prefix),
            (ring, None) => ring,
        }
    }

    pub(crate) fn record_audio_prefix(
        &self,
        epoch: u64,
        generation: u64,
    ) -> Option<crate::capture_clock::RecordAudioPrefixProof> {
        self.record_audio_mapping
            .read_proof(epoch)
            .filter(|proof| generation > 0 && proof.span.generation == generation)
    }
}
