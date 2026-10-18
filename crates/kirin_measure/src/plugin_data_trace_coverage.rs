//! How much of its WAV a latency-mapped Record TRACE covers, in complete 100 ms windows.
use super::*;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct LatencyMappedTraceCoverage {
    pub(super) expected_frame_count: u64,
    pub(super) measured_frame_count: u64,
    pub(super) missing_slots: u64,
}

pub(super) fn uses_latency_mapped_wav_grid(data: &PluginDataFile) -> bool {
    data.trace_clock_resolution.as_deref() == Some("producer_plus_output_latency")
}

fn latency_mapped_source_times(data: &PluginDataFile) -> Option<Vec<u64>> {
    if data.trace_clock_observations.is_empty() {
        return None;
    }
    let origin = data
        .expected_wav
        .as_ref()
        .and_then(|expected| expected.wav_time_reference_samples)
        .and_then(|origin| i64::try_from(origin).ok())
        .or_else(|| {
            data.trace_clock
                .as_ref()
                .map(|clock| clock.origin_position_samples)
        })?;
    let mut source_times_by_wav_slot = HashMap::new();
    for observation in &data.trace_clock_observations {
        let (Some(producer_position), Some(output_latency)) = (
            observation.producer_position_samples,
            observation.output_presentation_latency_samples,
        ) else {
            continue;
        };
        let aligned = producer_position.checked_add(i64::from(output_latency))?;
        let wav_slot = aligned.checked_sub(origin)?;
        if !data.trace_slot_positions.contains(&wav_slot) {
            continue;
        }
        if source_times_by_wav_slot
            .insert(wav_slot, observation.frame.t_ms)
            .is_some()
        {
            return None;
        }
    }
    let source_times = data
        .trace_slot_positions
        .iter()
        .map(|slot| source_times_by_wav_slot.get(slot).copied())
        .collect::<Option<Vec<_>>>()?;
    source_times
        .windows(2)
        .all(|pair| pair[0] < pair[1])
        .then_some(source_times)
}

/// Count only complete 100 ms windows that can exist on this side's factual latency-mapped
/// lattice. A non-zero presentation-latency phase normally leaves complementary partial ranges at
/// the WAV head and tail; those two edge remainders are not a missing measurement. Missing first,
/// middle, or last lattice positions remain explicit gaps.
pub(super) fn latency_mapped_trace_coverage(
    data: &PluginDataFile,
    duration_samples: u64,
    sample_rate: u32,
) -> Option<LatencyMappedTraceCoverage> {
    if !uses_latency_mapped_wav_grid(data)
        || sample_rate == 0
        || data.frames.is_empty()
        || data.trace_slot_positions.len() != data.frames.len()
    {
        return None;
    }
    let slot_samples = i64::from(sample_rate / 10).max(1);
    let half_slot = (slot_samples / 2).max(1);
    let duration = i64::try_from(duration_samples).ok()?;
    let first = *data.trace_slot_positions.first()?;
    let last = *data.trace_slot_positions.last()?;
    if first < slot_samples || last > duration {
        return None;
    }
    if data
        .trace_slot_positions
        .iter()
        .zip(&data.frames)
        .any(|(slot, frame)| {
            u64::try_from(*slot).ok().is_none_or(|slot| {
                frame.t_ms != slot.saturating_mul(1_000) / u64::from(sample_rate)
            })
        })
    {
        return None;
    }
    if !data
        .trace_slot_positions
        .windows(2)
        .all(|pair| pair[0] < pair[1])
    {
        return None;
    }

    // The earliest complete window on any phase ends in [slot, 2*slot). Every full slot beyond
    // that boundary is a real missing head window, not an arbitrary phase remainder.
    let mut missing_slots = u64::try_from(first / slot_samples).ok()?.saturating_sub(1);
    if !data.trace_clock_observations.is_empty() {
        // Presentation latency may change by more than half a 100 ms interval between epochs.
        // Count internal gaps on the untouched producer journal, not by rounding the shifted WAV
        // distance. This preserves every factual latency change without hiding a missing frame.
        for pair in latency_mapped_source_times(data)?.windows(2) {
            let delta_ms = pair[1].checked_sub(pair[0])?;
            if delta_ms < TRACE_FRAME_INTERVAL_MS || delta_ms % TRACE_FRAME_INTERVAL_MS != 0 {
                return None;
            }
            missing_slots = missing_slots.checked_add(
                delta_ms
                    .checked_div(TRACE_FRAME_INTERVAL_MS)?
                    .saturating_sub(1),
            )?;
        }
    } else if let Some(diagnostics) = data.trace_diagnostics.as_ref().filter(|diagnostics| {
        diagnostics.measured_frame_count == data.frames.len() as u64
            && diagnostics.expected_frame_count
                == diagnostics
                    .measured_frame_count
                    .saturating_add(diagnostics.missing_slots)
    }) {
        // The journal is deliberately removed after commit. Its already-derived counts remain the
        // signed fact; re-inferring from shifted distances could disagree with the commit decision.
        return Some(LatencyMappedTraceCoverage {
            expected_frame_count: diagnostics.expected_frame_count,
            measured_frame_count: diagnostics.measured_frame_count,
            missing_slots: diagnostics.missing_slots,
        });
    } else {
        for pair in data.trace_slot_positions.windows(2) {
            let delta = pair[1].checked_sub(pair[0])?;
            if delta < half_slot {
                return None;
            }
            // Compatibility for an unsigned in-memory fixture with no producer journal or
            // diagnostics. Committed current artifacts always use one of the factual paths above.
            let cadence_count = delta.checked_add(half_slot)?.checked_div(slot_samples)?;
            missing_slots = missing_slots
                .checked_add(u64::try_from(cadence_count.max(1)).ok()?.saturating_sub(1))?;
        }
    }
    let tail_samples = duration.checked_sub(last)?;
    missing_slots = missing_slots.checked_add(u64::try_from(tail_samples / slot_samples).ok()?)?;
    let measured_frame_count = u64::try_from(data.frames.len()).ok()?;
    Some(LatencyMappedTraceCoverage {
        expected_frame_count: measured_frame_count.checked_add(missing_slots)?,
        measured_frame_count,
        missing_slots,
    })
}
