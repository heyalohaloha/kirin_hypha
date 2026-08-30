//! Allocation-free-per-frame ODF candidates used by ATTACK Phase 2.

use std::f32::consts::PI;
use std::sync::Arc;

use rustfft::num_complex::Complex32;
use rustfft::{Fft, FftPlanner};

use crate::transient_layout::{
    TransientLayout, TransientOdfKind, TRANSIENT_MAX_HZ, TRANSIENT_MIN_HZ, TRANSIENT_POWER_EPSILON,
};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TransientOdfFrame {
    pub support_start_samples: i64,
    pub support_end_samples: i64,
    pub event_sample: i64,
    pub mel_flux: f32,
    pub complex_odf: f32,
    pub value: f32,
}

pub struct TransientCandidateAnalyzer {
    layout: TransientLayout,
    kind: TransientOdfKind,
    fft: Arc<dyn Fft<f32>>,
    window: Vec<f32>,
    window_energy: f32,
    fft_buffer: Vec<Complex32>,
    current_power: Vec<f32>,
    current_magnitude: Vec<f32>,
    current_phase: Vec<f32>,
    previous_magnitude: Vec<f32>,
    previous_phase: Vec<f32>,
    previous_previous_phase: Vec<f32>,
    mel_weights: Vec<f32>,
    current_mel: Vec<f32>,
    previous_mel: Vec<f32>,
    frames_seen: usize,
}

impl TransientCandidateAnalyzer {
    pub fn new(sample_rate: u32, kind: TransientOdfKind) -> Result<Self, &'static str> {
        let layout = TransientLayout::for_rate(sample_rate, kind)?;
        let mut planner = FftPlanner::<f32>::new();
        let fft = planner.plan_fft_forward(layout.fft_size);
        let window = periodic_hann(layout.window_samples);
        let window_energy = window.iter().map(|value| value * value).sum::<f32>();
        if !window_energy.is_finite() || window_energy <= 0.0 {
            return Err("invalid transient Hann energy");
        }
        let bins = layout.fft_size / 2 + 1;
        let mel_weights = mel_weights(sample_rate, layout.fft_size, 40)?;
        Ok(Self {
            kind,
            fft,
            window,
            window_energy,
            fft_buffer: vec![Complex32::ZERO; layout.fft_size],
            current_power: vec![0.0; bins],
            current_magnitude: vec![0.0; bins],
            current_phase: vec![0.0; bins],
            previous_magnitude: vec![0.0; bins],
            previous_phase: vec![0.0; bins],
            previous_previous_phase: vec![0.0; bins],
            mel_weights,
            current_mel: vec![0.0; 40],
            previous_mel: vec![0.0; 40],
            frames_seen: 0,
            layout,
        })
    }

    pub fn layout(&self) -> &TransientLayout {
        &self.layout
    }

    pub fn reset(&mut self) {
        self.fft_buffer.fill(Complex32::ZERO);
        self.current_power.fill(0.0);
        self.current_magnitude.fill(0.0);
        self.current_phase.fill(0.0);
        self.previous_magnitude.fill(0.0);
        self.previous_phase.fill(0.0);
        self.previous_previous_phase.fill(0.0);
        self.current_mel.fill(0.0);
        self.previous_mel.fill(0.0);
        self.frames_seen = 0;
    }

    pub fn analyze_window(
        &mut self,
        samples: &[f32],
        support_start_samples: i64,
    ) -> Result<Option<TransientOdfFrame>, &'static str> {
        if samples.len() != self.layout.window_samples
            || samples.iter().any(|sample| !sample.is_finite())
        {
            return Err("invalid transient analysis window");
        }
        self.fft_buffer.fill(Complex32::ZERO);
        for ((bin, sample), window) in self.fft_buffer.iter_mut().zip(samples).zip(&self.window) {
            bin.re = sample * window;
        }
        self.fft.process(&mut self.fft_buffer);
        self.update_spectrum();
        self.update_mel();

        let mel_flux = self.mel_flux();
        let complex_odf = self.complex_odf();
        let value = match self.kind {
            TransientOdfKind::Mel32 | TransientOdfKind::Mel40 => mel_flux,
            TransientOdfKind::Complex => complex_odf,
            TransientOdfKind::Hybrid => mel_flux + 4.0 * (1.0 + 100.0 * complex_odf).ln(),
        };
        let warm = self.frames_seen
            >= match self.kind {
                TransientOdfKind::Complex | TransientOdfKind::Hybrid => 2,
                TransientOdfKind::Mel32 | TransientOdfKind::Mel40 => 1,
            };
        self.previous_previous_phase
            .copy_from_slice(&self.previous_phase);
        self.previous_phase.copy_from_slice(&self.current_phase);
        self.previous_magnitude
            .copy_from_slice(&self.current_magnitude);
        self.previous_mel.copy_from_slice(&self.current_mel);
        self.frames_seen += 1;
        if !warm || !value.is_finite() {
            return Ok(None);
        }
        let support_end_samples = support_start_samples
            .checked_add(self.layout.window_samples as i64)
            .ok_or("transient support endpoint overflow")?;
        let event_sample = support_start_samples
            .checked_add((self.layout.window_samples / 2) as i64)
            .ok_or("transient event sample overflow")?;
        Ok(Some(TransientOdfFrame {
            support_start_samples,
            support_end_samples,
            event_sample,
            mel_flux,
            complex_odf,
            value,
        }))
    }

    fn update_spectrum(&mut self) {
        let nyquist = self.layout.fft_size / 2;
        for (index, complex) in self.fft_buffer.iter().take(nyquist + 1).enumerate() {
            let one_sided = if index == 0 || index == nyquist {
                1.0
            } else {
                2.0
            };
            let power = one_sided * complex.norm_sqr() / self.window_energy;
            self.current_power[index] = power.max(0.0);
            self.current_magnitude[index] = self.current_power[index].sqrt();
            self.current_phase[index] = complex.arg();
        }
    }

    fn update_mel(&mut self) {
        let bands = self.kind.mel_bands();
        if bands == 0 {
            return;
        }
        let bins = self.current_power.len();
        let weight_offset = if bands == 32 { 0 } else { 32 * bins };
        for band in 0..bands {
            let weights =
                &self.mel_weights[weight_offset + band * bins..weight_offset + (band + 1) * bins];
            let power = self
                .current_power
                .iter()
                .zip(weights)
                .map(|(value, weight)| value * weight)
                .sum::<f32>();
            self.current_mel[band] = 10.0 * power.max(TRANSIENT_POWER_EPSILON).log10();
        }
    }

    fn mel_flux(&self) -> f32 {
        let bands = self.kind.mel_bands();
        if bands == 0 || self.frames_seen == 0 {
            return 0.0;
        }
        self.current_mel[..bands]
            .iter()
            .zip(&self.previous_mel[..bands])
            .map(|(current, previous)| (current - previous).max(0.0))
            .sum::<f32>()
            / bands as f32
    }

    fn complex_odf(&self) -> f32 {
        if self.frames_seen < 2 {
            return 0.0;
        }
        let mut sum = 0.0;
        for index in 1..self.current_magnitude.len() {
            let predicted_phase =
                2.0 * self.previous_phase[index] - self.previous_previous_phase[index];
            let phase_error = self.current_phase[index] - predicted_phase;
            let current = self.current_magnitude[index];
            let previous = self.previous_magnitude[index];
            let squared = current * current + previous * previous
                - 2.0 * current * previous * phase_error.cos();
            sum += squared.max(0.0).sqrt();
        }
        sum / (self.current_magnitude.len() - 1) as f32
    }
}

fn periodic_hann(len: usize) -> Vec<f32> {
    (0..len)
        .map(|index| 0.5 - 0.5 * (2.0 * PI * index as f32 / len as f32).cos())
        .collect()
}

fn mel_weights(
    sample_rate: u32,
    fft_size: usize,
    maximum_bands: usize,
) -> Result<Vec<f32>, &'static str> {
    let bins = fft_size / 2 + 1;
    let mut all = Vec::with_capacity((32 + maximum_bands) * bins);
    for bands in [32, maximum_bands] {
        let min_mel = hz_to_slaney_mel(TRANSIENT_MIN_HZ);
        let max_hz = TRANSIENT_MAX_HZ.min(sample_rate as f32 * 0.5);
        let max_mel = hz_to_slaney_mel(max_hz);
        if max_mel <= min_mel {
            return Err("invalid transient Mel range");
        }
        let points = (0..bands + 2)
            .map(|index| {
                let mel = min_mel + (max_mel - min_mel) * index as f32 / (bands + 1) as f32;
                slaney_mel_to_hz(mel)
            })
            .collect::<Vec<_>>();
        for band in 0..bands {
            let left = points[band];
            let center = points[band + 1];
            let right = points[band + 2];
            for bin in 0..bins {
                let hz = bin as f32 * sample_rate as f32 / fft_size as f32;
                let rising = (hz - left) / (center - left);
                let falling = (right - hz) / (right - center);
                all.push(rising.min(falling).clamp(0.0, 1.0));
            }
        }
    }
    Ok(all)
}

fn hz_to_slaney_mel(hz: f32) -> f32 {
    const F_SP: f32 = 200.0 / 3.0;
    const MIN_LOG_HZ: f32 = 1_000.0;
    const MIN_LOG_MEL: f32 = MIN_LOG_HZ / F_SP;
    const LOG_STEP: f32 = 0.068_751_78;
    if hz < MIN_LOG_HZ {
        hz / F_SP
    } else {
        MIN_LOG_MEL + (hz / MIN_LOG_HZ).ln() / LOG_STEP
    }
}

fn slaney_mel_to_hz(mel: f32) -> f32 {
    const F_SP: f32 = 200.0 / 3.0;
    const MIN_LOG_HZ: f32 = 1_000.0;
    const MIN_LOG_MEL: f32 = MIN_LOG_HZ / F_SP;
    const LOG_STEP: f32 = 0.068_751_78;
    if mel < MIN_LOG_MEL {
        mel * F_SP
    } else {
        MIN_LOG_HZ * (LOG_STEP * (mel - MIN_LOG_MEL)).exp()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn deterministic_noise(layout: &TransientLayout, amplitude: f32) -> Vec<f32> {
        let mut state = 0x1234_5678_u32;
        (0..layout.window_samples)
            .map(|_| {
                state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
                let unit = (state >> 8) as f32 / 0x00ff_ffff_u32 as f32;
                amplitude * (2.0 * unit - 1.0)
            })
            .collect()
    }

    #[test]
    fn silence_is_finite_and_zero_after_warmup() {
        for kind in TransientOdfKind::ALL {
            let mut analyzer = TransientCandidateAnalyzer::new(48_000, kind).unwrap();
            let silence = vec![0.0; analyzer.layout().window_samples];
            let mut last = None;
            for index in 0..4 {
                last = analyzer
                    .analyze_window(&silence, index * analyzer.layout().hop_samples as i64)
                    .unwrap();
            }
            assert_eq!(last.unwrap().value, 0.0, "{}", kind.as_str());
        }
    }

    #[test]
    fn a_new_impulse_produces_a_positive_candidate() {
        for kind in TransientOdfKind::ALL {
            let mut analyzer = TransientCandidateAnalyzer::new(48_000, kind).unwrap();
            let silence = vec![0.0; analyzer.layout().window_samples];
            for index in 0..3 {
                analyzer
                    .analyze_window(&silence, index * analyzer.layout().hop_samples as i64)
                    .unwrap();
            }
            let mut impulse = silence;
            let middle = impulse.len() / 2;
            impulse[middle] = 1.0;
            let frame = analyzer.analyze_window(&impulse, 1_024).unwrap().unwrap();
            assert!(frame.value > 0.0, "{} = {}", kind.as_str(), frame.value);
        }
    }

    #[test]
    fn log_mel_flux_is_invariant_to_a_fixed_scalar_gain_above_floor() {
        for kind in [TransientOdfKind::Mel32, TransientOdfKind::Mel40] {
            let mut unity = TransientCandidateAnalyzer::new(48_000, kind).unwrap();
            let mut attenuated = TransientCandidateAnalyzer::new(48_000, kind).unwrap();
            let first = deterministic_noise(unity.layout(), 0.1);
            let second = first.iter().map(|value| value * 2.0).collect::<Vec<_>>();
            let first_quiet = first.iter().map(|value| value * 0.25).collect::<Vec<_>>();
            let second_quiet = second.iter().map(|value| value * 0.25).collect::<Vec<_>>();
            unity.analyze_window(&first, 0).unwrap();
            attenuated.analyze_window(&first_quiet, 0).unwrap();
            let loud = unity.analyze_window(&second, 256).unwrap().unwrap().value;
            let quiet = attenuated
                .analyze_window(&second_quiet, 256)
                .unwrap()
                .unwrap()
                .value;
            assert!((loud - quiet).abs() < 1.0e-3, "{loud} vs {quiet}");
        }
    }

    #[test]
    fn invalid_windows_fail_without_advancing_state() {
        let mut analyzer =
            TransientCandidateAnalyzer::new(48_000, TransientOdfKind::Mel40).unwrap();
        assert!(analyzer.analyze_window(&[0.0; 8], 0).is_err());
        let silence = vec![0.0; analyzer.layout().window_samples];
        assert!(analyzer.analyze_window(&silence, 0).unwrap().is_none());
    }
}
