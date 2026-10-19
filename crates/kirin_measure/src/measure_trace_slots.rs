//! One Record TRACE slot is the native-rate core (EBU) result joined with the Phase D result of
//! the same 100 ms, measured on the 48 kHz stream. Feeding Phase D slots, joining both halves and
//! emitting a core slot without Phase D.
use super::*;

#[derive(Debug, Clone)]
pub(super) struct PendingRecordCore {
    pub(super) frames_48k: u64,
    pub(super) native_frames: u64,
    pub(super) position_samples: Option<i64>,
    pub(super) raw_host_position_samples: Option<i64>,
    pub(super) capture_epoch: Option<u64>,
    pub(super) clock_source: CaptureClockSource,
    pub(super) presentation_latency: PresentationLatencySamples,
    pub(super) observed_silence_floor: bool,
    pub(super) result: MeasureResult,
}

pub(super) fn merge_phase_d_fields(
    result: &mut MeasureResult,
    pd: Option<&crate::phase_d::stream::PhaseDResult>,
) {
    if let Some(pd_r) = pd {
        result.n_prime_total = Some(pd_r.loudness);
        result.sharpness = Some(pd_r.sharpness);
        result.psb_summary = Some(compute_psb_summary(
            &pd_r.psb,
            &pd_r.psb_bark21_24,
            pd_r.psb_high_ext_15_5k_20k,
        ));
        result.n_prime = Some(pd_r.n_prime);
        result.psb_bark = Some(pd_r.psb);
    }
}

pub(super) fn clear_phase_d_fields(result: &mut MeasureResult) {
    result.n_prime_total = None;
    result.sharpness = None;
    result.psb_summary = None;
    result.n_prime = None;
    result.psb_bark = None;
}

#[allow(clippy::too_many_arguments)]
pub(super) fn feed_phase_d_slots(
    input_48k: &[f64],
    n_channels: usize,
    slot_pending: &mut Vec<f64>,
    phase_d: &mut PhaseDChannelStream,
    latest_pd: &mut Option<crate::phase_d::stream::PhaseDResult>,
    record_phase_pending: &mut VecDeque<crate::phase_d::stream::PhaseDResult>,
    is_recording: bool,
) {
    slot_pending.extend_from_slice(input_48k);
    let slot_samples = ENGINE_SR as usize / 10 * n_channels;
    let complete_samples = slot_pending.len() / slot_samples * slot_samples;
    for slot in slot_pending[..complete_samples].chunks_exact(slot_samples) {
        if let Some(result) = phase_d.push_interleaved_slot(slot) {
            *latest_pd = Some(result.clone());
            if is_recording {
                record_phase_pending.push_back(result);
            }
        }
    }
    slot_pending.drain(..complete_samples);
}

pub(super) fn join_record_trace_slots(
    trace: &mut RecordTraceDrain<'_>,
    sample_rate: u32,
    core_pending: &mut VecDeque<PendingRecordCore>,
    phase_pending: &mut VecDeque<crate::phase_d::stream::PhaseDResult>,
) {
    while !core_pending.is_empty() && !phase_pending.is_empty() {
        let core = core_pending.pop_front().expect("checked non-empty");
        let phase = phase_pending.pop_front().expect("checked non-empty");
        let mut result = core.result;
        merge_phase_d_fields(&mut result, Some(&phase));
        let _ = maybe_push_record_trace(
            trace.queue,
            trace.timeline,
            &mut trace.cursor,
            sample_rate,
            RecordTraceObserved {
                frames_48k: core.frames_48k,
                native_frames: Some(core.native_frames),
                position_samples: core.position_samples,
                raw_host_position_samples: core.raw_host_position_samples,
                capture_epoch: core.capture_epoch,
                clock_source: core.clock_source,
                presentation_latency: core.presentation_latency,
                observed_silence_floor: core.observed_silence_floor,
            },
            &result,
        );
    }
}

pub(super) fn flush_record_core_without_phase_d(
    trace: &mut RecordTraceDrain<'_>,
    sample_rate: u32,
    core_pending: &mut VecDeque<PendingRecordCore>,
) {
    while let Some(core) = core_pending.pop_front() {
        let _ = maybe_push_record_trace(
            trace.queue,
            trace.timeline,
            &mut trace.cursor,
            sample_rate,
            RecordTraceObserved {
                frames_48k: core.frames_48k,
                native_frames: Some(core.native_frames),
                position_samples: core.position_samples,
                raw_host_position_samples: core.raw_host_position_samples,
                capture_epoch: core.capture_epoch,
                clock_source: core.clock_source,
                presentation_latency: core.presentation_latency,
                observed_silence_floor: core.observed_silence_floor,
            },
            &core.result,
        );
    }
}

/// Phase D's own resampler for the TRACE at a rate other than 48 kHz, built like the live
/// engine's. A TRACE pass end finishes and restarts it (`close_trace_pass`); the live engine
/// behind the Watch and Keep displays keeps its own, so its stream runs on through the pass end as
/// in 1.1.51 (a restart there would cut it and move its true peak).
pub(super) fn new_trace_resampler(
    sample_rate: u32,
    n_channels: usize,
) -> Result<Option<ResamplerTo48k>, rubato::ResamplerConstructionError> {
    (sample_rate != ENGINE_SR)
        .then(|| ResamplerTo48k::new(sample_rate, n_channels))
        .transpose()
}

/// The live engine's resampler and Phase D's restart together wherever 1.1.51 restarted the one
/// they shared (Record start, a Watch pass or position change, a return to Active).
pub(super) fn reset_resamplers(
    live: &mut Option<ResamplerTo48k>,
    trace: &mut Option<ResamplerTo48k>,
) {
    for resampler in [live, trace].into_iter().flatten() {
        resampler.reset();
    }
}

/// One native chunk's 48 kHz audio for Phase D, through Phase D's own resampler at another rate.
/// A resampler failure leaves the chunk out of Phase D only.
pub(super) fn trace_48k<'a>(
    resampler: &mut Option<ResamplerTo48k>,
    out: &'a mut Vec<f64>,
    chunk: &'a [f64],
) -> &'a [f64] {
    let Some(resampler) = resampler.as_mut() else {
        return chunk;
    };
    out.clear();
    if let Err(error) = resampler.process(chunk, out) {
        log::warn!("[MeasureThread] TRACE resample error: {error:?}; Phase D skips the chunk");
        out.clear();
    }
    out
}

/// The TRACE pass of the selected take ends: a later pass of the Keep begins, the take jumps, or
/// the Record stops. At a rate other than 48 kHz Phase D's resampler still holds the pass's last
/// samples, so the Phase D half of its last slots is incomplete. Pump them out into Phase D, join
/// every slot, emit the core slots left without Phase D, and start Phase D's resampler and Phase D
/// afresh for what follows. Call it before the next chunk enters that resampler; never with the
/// live engine's. False when the resampler failed; the slots are emitted all the same.
#[allow(clippy::too_many_arguments)]
pub(super) fn close_trace_pass(
    resampler: &mut Option<ResamplerTo48k>,
    resampled_buf: &mut Vec<f64>,
    n_channels: usize,
    phase_d_slot_pending: &mut Vec<f64>,
    phase_d: &mut PhaseDChannelStream,
    latest_pd: &mut Option<crate::phase_d::stream::PhaseDResult>,
    record_phase_pending: &mut VecDeque<crate::phase_d::stream::PhaseDResult>,
    record_core_pending: &mut VecDeque<PendingRecordCore>,
    trace: Option<&mut RecordTraceDrain<'_>>,
    sample_rate: u32,
) -> bool {
    let mut flushed = true;
    if let Some(resampler) = resampler.as_mut() {
        resampled_buf.clear();
        match resampler.finish(resampled_buf) {
            Ok(_) => feed_phase_d_slots(
                resampled_buf,
                n_channels,
                phase_d_slot_pending,
                phase_d,
                latest_pd,
                record_phase_pending,
                true,
            ),
            Err(error) => {
                log::warn!("[MeasureThread] TRACE pass tail flush failed: {:?}", error);
                flushed = false;
            }
        }
        resampler.reset();
    }
    if let Some(trace) = trace {
        join_record_trace_slots(
            trace,
            sample_rate,
            record_core_pending,
            record_phase_pending,
        );
        flush_record_core_without_phase_d(trace, sample_rate, record_core_pending);
    }
    record_core_pending.clear();
    record_phase_pending.clear();
    phase_d_slot_pending.clear();
    phase_d.reset();
    *latest_pd = None;
    flushed
}

#[cfg(test)]
#[path = "measure_trace_resampler_tests.rs"]
mod resampler_tests;
