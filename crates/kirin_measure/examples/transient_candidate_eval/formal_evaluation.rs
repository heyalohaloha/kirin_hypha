use kirin_measure::{SuperFluxAnalyzer, SuperFluxConfig, TransientOdfFrame};

use super::LoadedTrack;
use crate::contract::{FormalAnalyzer, PeakRule};
use crate::metrics::FormalEvaluationReport;

#[allow(dead_code)] // Source pin and context-guarded scoring intentionally block this entrypoint.
pub(crate) fn evaluate_formal_tracks(
    _tracks: &[LoadedTrack],
    _analyzer: FormalAnalyzer,
    _rule: PeakRule,
) -> Result<FormalEvaluationReport, String> {
    Err("formal evaluation blocked: not_ready_context_guard_unimplemented".to_string())
}

#[allow(dead_code)] // Algorithm fixture; formal scoring remains context-guard blocked.
pub(super) fn analyze_superflux_frames(
    samples: &[f32],
    sample_rate: u32,
    config: SuperFluxConfig,
) -> Result<Vec<TransientOdfFrame>, String> {
    let mut analyzer = SuperFluxAnalyzer::new(sample_rate, config).map_err(str::to_string)?;
    let window = analyzer.layout().window_samples;
    let hop = analyzer.layout().hop_samples;
    let lag = analyzer.layout().spectral_lag_frames;
    let mut buffer = vec![0.0_f32; window];
    for warmup in (1..=lag).rev() {
        let center = -((warmup * hop) as i64);
        let support_start = center - (window / 2) as i64;
        analyzer
            .analyze_window(&buffer, None, support_start)
            .map_err(str::to_string)?;
    }
    if samples.is_empty() {
        return Ok(Vec::new());
    }
    let flush_end = samples
        .len()
        .checked_add(window / 2)
        .ok_or("SuperFlux analysis support end overflow")?;
    let mut frames = Vec::with_capacity(flush_end.div_ceil(hop));
    for center in (0..flush_end).step_by(hop) {
        buffer.fill(0.0);
        let support_start = center as i64 - (window / 2) as i64;
        for (window_index, value) in buffer.iter_mut().enumerate() {
            let sample_index = support_start + window_index as i64;
            if let Ok(sample_index) = usize::try_from(sample_index) {
                if let Some(sample) = samples.get(sample_index) {
                    *value = *sample;
                }
            }
        }
        if let Some(frame) = analyzer
            .analyze_window(&buffer, None, support_start)
            .map_err(str::to_string)?
        {
            frames.push(TransientOdfFrame {
                support_start_samples: frame.support_start_samples,
                support_end_samples: frame.support_end_samples,
                event_sample: frame.event_sample,
                mel_flux: 0.0,
                complex_odf: 0.0,
                value: frame.value,
            });
        }
    }
    Ok(frames)
}

#[cfg(test)]
#[path = "formal_evaluation_tests.rs"]
mod tests;
