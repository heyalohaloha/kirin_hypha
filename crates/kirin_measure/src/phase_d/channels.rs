//! Channel-safe adapter for the mono ISO 532-1 Phase D stream.
//!
//! Each input channel is measured independently. Combining audio samples before
//! the nonlinear loudness pipeline would make anti-correlated stereo cancel to
//! silence even though both channels contain measurable audio.

use super::stream::{PhaseDResult, PhaseDStream};
use super::tables::{FieldType, N_BARK, N_SPEC_BINS};

/// Runs one [`PhaseDStream`] per input channel and combines the measured facts.
///
/// Kirin's channel aggregation is the arithmetic mean of independently measured
/// channel results. This keeps mono and identical dual-mono on the same scale,
/// while making the measurement invariant to a channel's polarity. It is an
/// explicit product aggregation; samples are never summed before measurement.
pub struct PhaseDChannelStream {
    streams: Vec<PhaseDStream>,
    channel_samples: Vec<Vec<f64>>,
}

/// Sharpness-only channel adapter for the isolated on-demand Perceptual Delta worker.
/// Record keeps using [`PhaseDChannelStream`] and its complete result unchanged.
pub struct PhaseDSharpnessChannelStream {
    streams: Vec<PhaseDStream>,
    channel_samples: Vec<Vec<f64>>,
}

impl PhaseDSharpnessChannelStream {
    pub fn new(field_type: FieldType, n_channels: usize) -> Self {
        assert!(n_channels > 0, "Phase D requires at least one channel");
        Self {
            streams: (0..n_channels)
                .map(|_| PhaseDStream::new(field_type))
                .collect(),
            channel_samples: (0..n_channels).map(|_| Vec::new()).collect(),
        }
    }

    pub fn push_interleaved_slot(&mut self, input: &[f64]) -> Option<f64> {
        let n_channels = self.streams.len();
        if input.is_empty() || !input.len().is_multiple_of(n_channels) {
            return None;
        }
        let frames = input.len() / n_channels;
        for samples in &mut self.channel_samples {
            samples.clear();
            samples.reserve(frames);
        }
        for frame in input.chunks_exact(n_channels) {
            for (channel, &sample) in frame.iter().enumerate() {
                self.channel_samples[channel].push(sample);
            }
        }
        let mut sum = 0.0;
        for (stream, samples) in self.streams.iter_mut().zip(&self.channel_samples) {
            sum += stream.push_sharpness_only(samples)?;
        }
        Some(sum / n_channels as f64)
    }

    pub fn reset(&mut self) {
        for stream in &mut self.streams {
            stream.reset();
        }
        for samples in &mut self.channel_samples {
            samples.clear();
        }
    }
}

impl PhaseDChannelStream {
    pub fn new(field_type: FieldType, n_channels: usize) -> Self {
        assert!(n_channels > 0, "Phase D requires at least one channel");
        Self {
            streams: (0..n_channels)
                .map(|_| PhaseDStream::new(field_type))
                .collect(),
            channel_samples: (0..n_channels).map(|_| Vec::new()).collect(),
        }
    }

    /// Measures one interleaved slot and returns the final Phase D frame.
    ///
    /// Callers provide complete interleaved frames. An incomplete trailing frame
    /// is rejected rather than silently assigning samples to the wrong channel.
    pub fn push_interleaved_slot(&mut self, input: &[f64]) -> Option<PhaseDResult> {
        let n_channels = self.streams.len();
        if input.is_empty() || !input.len().is_multiple_of(n_channels) {
            return None;
        }

        let frames = input.len() / n_channels;
        for samples in &mut self.channel_samples {
            samples.clear();
            samples.reserve(frames);
        }
        for frame in input.chunks_exact(n_channels) {
            for (channel, &sample) in frame.iter().enumerate() {
                self.channel_samples[channel].push(sample);
            }
        }

        let mut results = Vec::with_capacity(n_channels);
        for (stream, samples) in self.streams.iter_mut().zip(&self.channel_samples) {
            results.push(stream.push(samples).last().cloned()?);
        }
        average_results(&results)
    }

    pub fn reset(&mut self) {
        for stream in &mut self.streams {
            stream.reset();
        }
        for samples in &mut self.channel_samples {
            samples.clear();
        }
    }
}

fn average_results(results: &[PhaseDResult]) -> Option<PhaseDResult> {
    if results.is_empty() {
        return None;
    }
    let divisor = results.len() as f64;
    let mut combined = PhaseDResult {
        loudness: 0.0,
        sharpness: 0.0,
        n_specific: [0.0; N_SPEC_BINS],
        psb: [0.0; N_BARK],
        n_prime: [0.0; N_BARK],
        psb_bark21_24: [0.0; 4],
        psb_high_ext_15_5k_20k: 0.0,
    };

    for result in results {
        combined.loudness += result.loudness;
        combined.sharpness += result.sharpness;
        for (target, value) in combined.n_specific.iter_mut().zip(result.n_specific) {
            *target += value;
        }
        for (target, value) in combined.psb.iter_mut().zip(result.psb) {
            *target += value;
        }
        for (target, value) in combined.n_prime.iter_mut().zip(result.n_prime) {
            *target += value;
        }
        for (target, value) in combined.psb_bark21_24.iter_mut().zip(result.psb_bark21_24) {
            *target += value;
        }
        combined.psb_high_ext_15_5k_20k += result.psb_high_ext_15_5k_20k;
    }

    combined.loudness /= divisor;
    combined.sharpness /= divisor;
    for value in &mut combined.n_specific {
        *value /= divisor;
    }
    for value in &mut combined.psb {
        *value /= divisor;
    }
    for value in &mut combined.n_prime {
        *value /= divisor;
    }
    for value in &mut combined.psb_bark21_24 {
        *value /= divisor;
    }
    combined.psb_high_ext_15_5k_20k /= divisor;

    Some(combined)
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE_RATE: f64 = 48_000.0;
    const SLOT_FRAMES: usize = 4_800;

    fn sine_slot() -> Vec<f64> {
        (0..SLOT_FRAMES)
            .map(|index| {
                0.25 * (index as f64 * 1_000.0 * std::f64::consts::TAU / SAMPLE_RATE).sin()
            })
            .collect()
    }

    fn stereo_slot(mono: &[f64], right_polarity: f64) -> Vec<f64> {
        mono.iter()
            .flat_map(|&sample| [sample, sample * right_polarity])
            .collect()
    }

    fn n_prime_sum(result: &PhaseDResult) -> f64 {
        result.n_prime.iter().sum()
    }

    #[test]
    fn mono_and_identical_stereo_keep_the_same_scale() {
        let mono = sine_slot();
        let mut mono_stream = PhaseDChannelStream::new(FieldType::Free, 1);
        let mut stereo_stream = PhaseDChannelStream::new(FieldType::Free, 2);

        let mono_result = mono_stream
            .push_interleaved_slot(&mono)
            .expect("mono result");
        let stereo_result = stereo_stream
            .push_interleaved_slot(&stereo_slot(&mono, 1.0))
            .expect("stereo result");

        for (mono_value, stereo_value) in
            mono_result.n_prime.iter().zip(stereo_result.n_prime.iter())
        {
            assert!((mono_value - stereo_value).abs() < 1e-12);
        }
        assert!((mono_result.loudness - stereo_result.loudness).abs() < 1e-12);
    }

    #[test]
    fn anti_phase_stereo_does_not_cancel_specific_loudness() {
        let mono = sine_slot();
        let mut in_phase = PhaseDChannelStream::new(FieldType::Free, 2);
        let mut anti_phase = PhaseDChannelStream::new(FieldType::Free, 2);

        let in_phase_result = in_phase
            .push_interleaved_slot(&stereo_slot(&mono, 1.0))
            .expect("in-phase result");
        let anti_phase_result = anti_phase
            .push_interleaved_slot(&stereo_slot(&mono, -1.0))
            .expect("anti-phase result");

        assert!(n_prime_sum(&anti_phase_result) > 0.0);
        for (in_phase_value, anti_phase_value) in in_phase_result
            .n_prime
            .iter()
            .zip(anti_phase_result.n_prime.iter())
        {
            assert!((in_phase_value - anti_phase_value).abs() < 1e-12);
        }
        assert!((in_phase_result.loudness - anti_phase_result.loudness).abs() < 1e-12);
    }

    #[test]
    fn incomplete_interleaved_frame_is_rejected() {
        let mut stream = PhaseDChannelStream::new(FieldType::Free, 2);
        assert!(stream.push_interleaved_slot(&[0.1, 0.2, 0.3]).is_none());
    }

    #[test]
    fn sharpness_only_adapter_matches_the_complete_fresh_slot() {
        let mono = sine_slot();
        let input = stereo_slot(&mono, -1.0);
        let mut complete = PhaseDChannelStream::new(FieldType::Free, 2);
        let mut sharpness_only = PhaseDSharpnessChannelStream::new(FieldType::Free, 2);
        let expected = complete.push_interleaved_slot(&input).unwrap().sharpness;
        let observed = sharpness_only.push_interleaved_slot(&input).unwrap();
        assert!((observed - expected).abs() < 1.0e-12);
    }
}
