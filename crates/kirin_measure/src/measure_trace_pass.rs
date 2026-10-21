//! Close a TRACE pass without restarting the live measurement stream.
use super::trace_slots::flush_record_core_without_phase_d;
use super::*;

/// Phase D's own resampler for the TRACE at a rate other than 48 kHz (none at 48 kHz), with its
/// output. A TRACE pass end finishes and restarts it (`close_trace_pass`); the live engine behind
/// the Watch and Keep displays keeps its own, so its stream runs on through the pass end as in
/// 1.1.51 (a restart there would cut it and move its true peak). Its own type, so neither the pass
/// end nor the Stop tail can be handed the live engine's resampler.
pub(super) struct TraceResampler {
    resampler: Option<ResamplerTo48k>,
    out: Vec<f64>,
}

impl TraceResampler {
    pub(super) fn new(
        sample_rate: u32,
        n_channels: usize,
    ) -> Result<Self, rubato::ResamplerConstructionError> {
        Ok(Self {
            resampler: (sample_rate != ENGINE_SR)
                .then(|| ResamplerTo48k::new(sample_rate, n_channels))
                .transpose()?,
            out: Vec::new(),
        })
    }

    pub(super) fn reset(&mut self) {
        if let Some(resampler) = self.resampler.as_mut() {
            resampler.reset();
        }
    }

    /// One native chunk's 48 kHz audio for Phase D (the chunk itself at 48 kHz).
    pub(super) fn process<'a>(
        &'a mut self,
        chunk: &'a [f64],
    ) -> Result<&'a [f64], rubato::ResampleError> {
        let Some(resampler) = self.resampler.as_mut() else {
            return Ok(chunk);
        };
        self.out.clear();
        resampler.process(chunk, &mut self.out)?;
        Ok(&self.out)
    }
}

/// The TRACE pass of the selected take ends: a later pass of the Keep begins, the take jumps, or
/// the Record stops. At a rate other than 48 kHz Phase D's resampler still holds the pass's last
/// samples, so the Phase D half of its last slots is incomplete. Pump them out into Phase D, join
/// every slot, emit the core slots left without Phase D, and start Phase D's resampler and Phase D
/// afresh for what follows. Call it before the next chunk enters that resampler; never with the
/// live engine's. False when the resampler failed; the slots are emitted all the same.
#[allow(clippy::too_many_arguments)]
pub(super) fn close_trace_pass(
    phase_resampler: &mut TraceResampler,
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
    let TraceResampler { resampler, out } = phase_resampler;
    if let Some(resampler) = resampler.as_mut() {
        out.clear();
        match resampler.finish(out) {
            Ok(_) => feed_phase_d_slots(
                out,
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
