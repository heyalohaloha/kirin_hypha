//! Canonical Record timeline selection and measured slot coverage.
use super::*;

pub(super) fn bake_continuous_record_timeline(ctx: &mut RecordingCtx) {
    deduplicate_replayed_trace_samples(&mut ctx.trace_samples);
    let clock_observations = ctx
        .trace_samples
        .iter()
        .filter(|sample| sample.generation == ctx.record_generation)
        .filter_map(trace_clock_observation)
        .collect();
    ctx.writer.set_trace_clock_observations(clock_observations);
    let has_epoch_samples = ctx
        .trace_samples
        .iter()
        .any(|sample| sample.capture_epoch.is_some());
    let selected_trace_samples: Vec<&RecordTraceSample> = ctx
        .trace_samples
        .iter()
        .filter(|sample| !has_epoch_samples || belongs_to_selected_take(ctx, sample))
        .collect();
    let mut seen_presentation = BTreeSet::new();
    let mut presentation_latency_observations = Vec::new();
    for latency in ctx
        .trace_samples
        .iter()
        .map(|sample| sample.presentation_latency)
        .chain(std::iter::once(ctx.presentation_latency_at_close))
    {
        let Some(source) = latency.source.as_str() else {
            continue;
        };
        if latency.input.is_none() && latency.output.is_none() {
            continue;
        }
        let observation = HostPresentationLatencyObservation {
            source: Some(source.to_string()),
            input_samples: latency.input,
            output_samples: latency.output,
        };
        if seen_presentation.insert(observation.clone()) {
            presentation_latency_observations.push(observation);
        }
    }
    ctx.writer
        .set_host_presentation_latency_observations(presentation_latency_observations);
    // Current producers publish only the exact Record-lane take. `trace_context_*` remains in the
    // schema solely so old staging files deserialize; it must never carry Watch history or be used
    // as a second chance to repair an incomplete current take after close.
    ctx.writer.set_trace_context(Vec::new(), Vec::new());
    if has_epoch_samples && ctx.selected_capture_epoch.is_none() {
        ctx.writer.clear_frames();
        ctx.writer.set_trace_slot_positions(Vec::new());
        ctx.writer.set_trace_clock(None);
        ctx.writer.set_trace_time_axis(None);
        ctx.writer.mark_integrity_degraded();
        ctx.writer
            .add_integrity_reason("missing_selected_capture_epoch");
        return;
    }

    let duration_ms = record_duration_ms(ctx);
    if duration_ms == 0 {
        return;
    }
    if !should_bake_continuous_record_timeline(ctx) {
        return;
    }
    if selected_trace_samples.is_empty() && ctx.writer.data().frames.is_empty() {
        return;
    }
    if !ctx.trace_samples.is_empty() {
        // The queue counter includes replayed observations drained before a Measure restart.
        // Keep public diagnostics on the same deduplicated population used for clock selection.
        ctx.trace_sample_count = selected_trace_samples.len();
    } else if ctx.trace_sample_count == 0 {
        ctx.trace_sample_count = ctx.writer.data().frames.len();
    }
    if ctx.raw_trace_timeline_covers_duration.is_none() {
        ctx.raw_trace_timeline_covers_duration =
            Some(raw_trace_timeline_covers_duration(ctx, duration_ms));
    }

    let slots = continuous_timeline_slots(duration_ms);
    if slots.is_empty() {
        return;
    }
    let sample_rate = ctx.writer.data().sample_rate;
    let slot_frames = (sample_rate as i64 / 10).max(1);
    let producer_take_slots = producer_take_slot_positions(ctx, slots.len(), slot_frames);

    let mut best_by_ms: BTreeMap<u64, TraceBakeCandidate> = BTreeMap::new();
    let mut best_by_position: BTreeMap<i64, TraceBakeCandidate> = BTreeMap::new();
    let mut clock_sources = BTreeSet::new();
    for frame in &ctx.writer.data().frames {
        if frame.t_ms <= duration_ms && slots.binary_search(&frame.t_ms).is_ok() {
            insert_best_trace_candidate(
                &mut best_by_ms,
                frame.t_ms,
                TraceBakeCandidate {
                    result: measure_from_frame(frame),
                    raw_host_position_samples: None,
                    raw_host_clock_is_known: false,
                },
            );
        }
    }
    for sample in &selected_trace_samples {
        let Some(result) = measured_trace_result_for_bake(sample) else {
            continue;
        };
        // Only the selected Record audio take enters this map. The producer take grid below chooses
        // the exact WAV-range positions; callbacks outside that grid never become publish input.
        if let (Some(position), Some(source)) =
            (sample.position_samples, sample.clock_source.as_str())
        {
            insert_best_trace_candidate(
                &mut best_by_position,
                position,
                TraceBakeCandidate::from_sample(sample, result.clone()),
            );
            clock_sources.insert(source.to_string());
        }
        if sample.t_ms <= duration_ms && slots.binary_search(&sample.t_ms).is_ok() {
            insert_best_trace_candidate(
                &mut best_by_ms,
                sample.t_ms,
                TraceBakeCandidate::from_sample(sample, result),
            );
        }
    }

    let mut psb_by_ms: BTreeMap<u64, MeasureResult> = BTreeMap::new();
    for sample in &selected_trace_samples {
        if sample.position_samples.is_some()
            && sample.include_psb
            && sample.t_ms <= duration_ms
            && sample.result.psb_bark.is_some()
        {
            psb_by_ms.insert(sample.t_ms, sample.result.clone());
        }
    }
    ctx.writer.clear_psb_snapshots();
    for (t_ms, result) in psb_by_ms {
        let _ = writer_append_psb(ctx, t_ms, &result);
    }

    ctx.writer.clear_frames();
    ctx.first_frame_logged = false;
    let mut trace_slot_positions = Vec::with_capacity(slots.len());
    let mut raw_host_slot_positions = Vec::with_capacity(slots.len());
    let mut raw_host_slots_complete = true;
    let expected_frame_count = slots.len() as u64;
    let mut missing_slots = 0_u64;
    if let Some(producer_take_slots) = producer_take_slots.as_ref() {
        for (index, position) in producer_take_slots.iter().enumerate() {
            let Some(candidate) = best_by_position.get(position) else {
                missing_slots = missing_slots.saturating_add(1);
                continue;
            };
            let t_ms = (index as u64 + 1).saturating_mul(FRAME_INTERVAL_MS);
            if writer_append_trace_frame(ctx, t_ms, &candidate.result) {
                trace_slot_positions.push(*position);
                append_raw_host_slot(
                    candidate,
                    &mut raw_host_slot_positions,
                    &mut raw_host_slots_complete,
                );
            } else {
                missing_slots = missing_slots.saturating_add(1);
            }
        }
    } else if !has_epoch_samples && !best_by_position.is_empty() {
        // Legacy artifacts have no epoch proof. Preserve their prior provisional path for read
        // compatibility, but new producers can only enter the exact range above.
        for (index, (position, candidate)) in best_by_position
            .iter()
            .take(expected_frame_count as usize)
            .enumerate()
        {
            let t_ms = (index as u64 + 1).saturating_mul(FRAME_INTERVAL_MS);
            if writer_append_trace_frame(ctx, t_ms, &candidate.result) {
                trace_slot_positions.push(*position);
                append_raw_host_slot(
                    candidate,
                    &mut raw_host_slot_positions,
                    &mut raw_host_slots_complete,
                );
            }
        }
        missing_slots = expected_frame_count.saturating_sub(trace_slot_positions.len() as u64);
    } else {
        for t_ms in slots {
            if let Some(candidate) = best_by_ms.get(&t_ms) {
                if writer_append_trace_frame(ctx, t_ms, &candidate.result) {
                    append_raw_host_slot(
                        candidate,
                        &mut raw_host_slot_positions,
                        &mut raw_host_slots_complete,
                    );
                } else {
                    missing_slots = missing_slots.saturating_add(1);
                }
            } else {
                missing_slots = missing_slots.saturating_add(1);
            }
        }
    }
    let measured_frame_count = ctx.writer.data().frames.len() as u64;
    let explicit_silence_frame_count = ctx
        .writer
        .data()
        .frames
        .iter()
        .filter(|frame| {
            frame.lufs_m <= TRACE_SILENCE_LUFS + 0.1
                && frame.true_peak <= TRACE_SILENCE_TRUE_PEAK_DBTP + 0.1
                && frame.crest.abs() <= 0.1
        })
        .count() as u64;
    ctx.writer.set_trace_diagnostics(TraceDiagnostics {
        raw_trace_count: ctx.trace_sample_count as u64,
        expected_frame_count,
        measured_frame_count,
        missing_slots,
        explicit_silence_frame_count,
    });
    let positions_are_factual = trace_slot_positions.len() == ctx.writer.data().frames.len()
        && trace_slot_positions
            .windows(2)
            .all(|window| window[0] < window[1]);
    let positions_are_dense = positions_are_factual
        && trace_slot_positions
            .windows(2)
            .all(|window| window[1].saturating_sub(window[0]) == slot_frames);
    let canonical_clock = trace_slot_positions.len() as u64 == expected_frame_count
        && positions_are_dense
        && !clock_sources.is_empty()
        && sample_rate > 0;
    let trace_clock = canonical_clock.then(|| {
        let origin_position_samples = trace_slot_positions
            .first()
            .copied()
            .expect("canonical clock has slots")
            .saturating_sub(slot_frames);
        TraceClock {
            basis: crate::trace_alignment::TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples,
            end_position_samples: *trace_slot_positions
                .last()
                .expect("canonical clock has slots"),
            sample_rate,
            sources: clock_sources.into_iter().collect(),
        }
    });
    // Preserve every factual producer-owned slot identity, including a sparse take. A late WAV
    // Drop may translate these measured absolute positions to WAV-relative samples, but may never
    // search surrounding history or synthesize a missing slot.
    ctx.writer
        .set_trace_slot_positions(if positions_are_factual {
            trace_slot_positions
        } else {
            Vec::new()
        });
    let raw_host_slots_are_factual = raw_host_slots_complete
        && raw_host_slot_positions.len() == ctx.writer.data().frames.len()
        && raw_host_slot_positions
            .windows(2)
            .all(|window| window[0] < window[1])
        && ctx.writer.data().raw_host_clock_range.is_some_and(|range| {
            raw_host_slot_positions.first().is_some_and(|first| {
                *first >= range.start_position_samples
                    && raw_host_slot_positions
                        .last()
                        .is_some_and(|last| *last <= range.end_position_samples)
            })
        });
    ctx.writer
        .set_trace_raw_host_slot_positions(if raw_host_slots_are_factual {
            raw_host_slot_positions
        } else {
            Vec::new()
        });
    ctx.writer.set_trace_time_axis(
        canonical_clock.then(|| crate::trace_alignment::TRACE_HOST_TIME_AXIS.to_string()),
    );
    ctx.writer.set_trace_clock(trace_clock);
    if missing_slots > 0 {
        ctx.writer.mark_integrity_degraded();
        ctx.writer.add_integrity_reason("missing_trace_slots");
    }
}

pub(super) fn raw_trace_timeline_covers_duration(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let has_epoch_samples = ctx
        .trace_samples
        .iter()
        .any(|sample| sample.capture_epoch.is_some());
    let mut times = Vec::with_capacity(
        ctx.trace_samples
            .len()
            .saturating_add(ctx.writer.data().frames.len()),
    );
    if !has_epoch_samples {
        for frame in &ctx.writer.data().frames {
            if frame.t_ms <= duration_ms {
                times.push(frame.t_ms);
            }
        }
    }
    for sample in &ctx.trace_samples {
        let selected_epoch = !has_epoch_samples || belongs_to_selected_take(ctx, sample);
        if selected_epoch && sample.t_ms <= duration_ms && sample.has_measured_core() {
            times.push(sample.t_ms);
        }
    }
    timeline_times_cover_duration(&mut times, duration_ms)
}

fn belongs_to_selected_take(ctx: &RecordingCtx, sample: &RecordTraceSample) -> bool {
    let Some((observed, selected)) = sample.capture_epoch.zip(ctx.selected_capture_epoch) else {
        return false;
    };
    if observed == selected {
        return true;
    } // Preserve the original same-epoch contract.
    let Some(proof) = ctx.record_audio_prefix else {
        return false;
    };
    let Some((position_samples, raw_host_position_samples)) = sample
        .position_samples
        .zip(sample.raw_host_position_samples)
    else {
        return false;
    };
    sample.generation == ctx.record_generation
        && proof.span.epoch == selected
        && proof.matches(crate::capture_clock::CaptureClockPoint {
            position_samples,
            raw_host_position_samples,
            epoch: observed,
            source: sample.clock_source,
            presentation_latency: sample.presentation_latency,
        })
}
