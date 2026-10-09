//! Callback clock publication and non-RT continuity checks for one captured take.
use super::*;

impl RecordTakeTracker {
    /// Full callback-local clock transaction. Auxiliary samples remain raw host evidence; they
    /// are never substituted for the established Record/WAV position mapping.
    #[allow(clippy::too_many_arguments)]
    pub fn note_capture_window_with_clocks_boundary(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
        auxiliary: AuxiliaryClockSamples,
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
        let auxiliary_contiguous = match auxiliary.samples {
            Some(samples) => {
                auxiliary.source != AuxiliaryClockSource::Unknown
                    && self.capture_last_auxiliary_valid.load(Ordering::Acquire)
                    && self.capture_last_auxiliary_source.load(Ordering::Acquire)
                        == auxiliary.source as u8
                    && self
                        .capture_last_auxiliary_end_samples
                        .load(Ordering::Acquire)
                        == samples
            }
            None => !self.capture_last_auxiliary_valid.load(Ordering::Acquire),
        };
        let audio_mapping_contiguous = presentation_position_samples.is_some()
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
        let mapping_contiguous = audio_mapping_contiguous && auxiliary_contiguous;

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
                auxiliary_only_cut: previous_sequence > 0
                    && previous_generation == capture_generation
                    && audio_mapping_contiguous
                    && presentation_latency.source != PresentationLatencySource::Unknown
                    && presentation_latency.input.is_some()
                    && presentation_latency.output.is_some()
                    && !auxiliary_contiguous
                    && !force_new_epoch
                    && !force_watch_offline_epoch,
                epoch: sequence,
                generation: capture_generation,
                capture_start_frame,
                capture_end_frame: frames_end,
                position_start_samples: presentation_position_samples,
                raw_host_position_start_samples: position_valid.then_some(position_samples),
                auxiliary_start_samples: auxiliary.samples,
                source,
                auxiliary_source: auxiliary.source,
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
        self.capture_last_auxiliary_valid
            .store(auxiliary.samples.is_some(), Ordering::Release);
        self.capture_last_auxiliary_end_samples.store(
            auxiliary
                .samples
                .unwrap_or(i64::MIN)
                .saturating_add(num_frames as i64),
            Ordering::Release,
        );
        self.capture_last_auxiliary_source
            .store(auxiliary.source as u8, Ordering::Release);
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

    /// A latency callback or producer-qualified auxiliary-only cut may split the clock mapping while the captured audio and
    /// raw host stream remain contiguous. Auxiliary evidence still starts a new pair epoch;
    /// this exception belongs only to Record audio admission and the immutable WAV mapping. Measure Thread must keep measuring across that boundary;
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
            && raw_shift.is_some_and(|shift| {
                if next.auxiliary_only_cut {
                    shift == 0 && previous.presentation_latency == next.presentation_latency
                } else {
                    previous.presentation_latency != next.presentation_latency
                        && presentation_latency_shift_matches(
                            shift,
                            previous.presentation_latency,
                            next.presentation_latency,
                        )
                }
            })
    }
}

#[cfg(test)]
#[path = "record_take_auxiliary_cut_tests.rs"]
mod tests;
