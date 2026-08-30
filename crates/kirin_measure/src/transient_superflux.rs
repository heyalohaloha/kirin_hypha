//! Fixed-scale SuperFlux-style onset strength for the ATTACK evaluator.
//!
//! This is deliberately separate from the frozen B-546 [`TransientOdfKind`](crate::TransientOdfKind)
//! candidates. Construction may allocate; [`SuperFluxAnalyzer::analyze_window`] does not.

use std::f32::consts::PI;
use std::sync::Arc;

use rustfft::num_complex::Complex32;
use rustfft::{Fft, FftPlanner};

#[path = "transient_superflux_bank.rs"]
mod bank;
use bank::{build_filterbank, definition_hash, verify_runtime_coefficients};

pub const SUPERFLUX_ALGORITHM_VERSION: &str = "kirin-superflux-fixed-scale-v1";
pub const SUPERFLUX_DEFINITION_VERSION: &str = "kirin-superflux-definition-v2";
pub const SUPERFLUX_REFERENCE_RATE: u32 = 48_000;
pub const SUPERFLUX_REFERENCE_HOP: u32 = 256;
pub(crate) const SUPERFLUX_MIN_MILLIHZ: u32 = 30_000;
pub(crate) const SUPERFLUX_MAX_MILLIHZ: u32 = 17_000_000;
pub const SUPERFLUX_MIN_HZ: f64 = SUPERFLUX_MIN_MILLIHZ as f64 / 1_000.0;
pub const SUPERFLUX_MAX_HZ: f64 = SUPERFLUX_MAX_MILLIHZ as f64 / 1_000.0;
pub const SUPERFLUX_SUPPORTED_RATES: [u32; 6] = [44_100, 48_000, 88_200, 96_000, 176_400, 192_000];

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SuperFluxChannelMode {
    Lr,
    Mid,
    Side,
}

impl SuperFluxChannelMode {
    const fn as_str(self) -> &'static str {
        match self {
            Self::Lr => "lr-power-mean",
            Self::Mid => "mid-waveform",
            Self::Side => "side-waveform",
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SuperFluxConfig {
    pub reference_window_samples: u32,
    pub bands_per_octave: u32,
    pub maximum_filter_radius: usize,
    pub reference_dbfs: i32,
    pub channel_mode: SuperFluxChannelMode,
    pub channel_count: usize,
}

impl SuperFluxConfig {
    pub const fn new(
        reference_window_samples: u32,
        bands_per_octave: u32,
        maximum_filter_radius: usize,
        reference_dbfs: i32,
        channel_mode: SuperFluxChannelMode,
        channel_count: usize,
    ) -> Self {
        Self {
            reference_window_samples,
            bands_per_octave,
            maximum_filter_radius,
            reference_dbfs,
            channel_mode,
            channel_count,
        }
    }

    fn validate(self) -> Result<(), &'static str> {
        if !matches!(self.reference_window_samples, 1_024 | 2_048) {
            return Err("unsupported SuperFlux reference window");
        }
        if !matches!(self.bands_per_octave, 12 | 24) {
            return Err("unsupported SuperFlux bands per octave");
        }
        if self.maximum_filter_radius > 1 {
            return Err("unsupported SuperFlux maximum-filter radius");
        }
        if !matches!(self.reference_dbfs, -80 | -70 | -60 | -50) {
            return Err("unsupported SuperFlux amplitude reference");
        }
        if !matches!(self.channel_count, 1 | 2) {
            return Err("unsupported SuperFlux channel count");
        }
        if self.channel_mode == SuperFluxChannelMode::Side && self.channel_count == 1 {
            return Err("SuperFlux SIDE requires stereo input");
        }
        Ok(())
    }

    fn amplitude_reference(self) -> f32 {
        match self.reference_dbfs {
            -80 => 0.000_1,
            -70 => 0.000_316_227_76,
            -60 => 0.001,
            -50 => 0.003_162_277_6,
            _ => unreachable!("validated by SuperFluxConfig::validate"),
        }
    }
}

#[derive(Clone, Debug)]
struct SuperFluxBand {
    bins: [usize; 3],
    weights: Vec<f32>,
}

#[derive(Clone, Debug)]
pub struct SuperFluxLayout {
    pub config: SuperFluxConfig,
    pub sample_rate: u32,
    pub window_samples: usize,
    pub hop_samples: usize,
    pub fft_size: usize,
    pub spectral_lag_frames: usize,
    pub band_count: usize,
    pub channel_count: usize,
    pub window_energy: f32,
    pub full_scale_gain: f32,
    pub definition_hash: [u8; 32],
    window: Vec<f32>,
    bands: Vec<SuperFluxBand>,
}

impl SuperFluxLayout {
    pub fn for_rate(sample_rate: u32, config: SuperFluxConfig) -> Result<Self, &'static str> {
        config.validate()?;
        if !SUPERFLUX_SUPPORTED_RATES.contains(&sample_rate) {
            return Err("unsupported SuperFlux sample rate");
        }
        let window_samples =
            rounded_reference_samples(sample_rate, config.reference_window_samples);
        let hop_samples = rounded_reference_samples(sample_rate, SUPERFLUX_REFERENCE_HOP);
        let fft_size = window_samples
            .checked_next_power_of_two()
            .ok_or("SuperFlux FFT size overflow")?;
        let spectral_lag_frames = (config.reference_window_samples / 1_024) as usize;
        let window = periodic_hann(window_samples);
        let window_sum = window.iter().map(|&value| value as f64).sum::<f64>() as f32;
        let window_energy = window
            .iter()
            .map(|&value| f64::from(value) * f64::from(value))
            .sum::<f64>() as f32;
        if !(window_sum > 0.0 && window_energy > 0.0) {
            return Err("invalid SuperFlux Hann compensation");
        }
        let full_scale_gain = window_sum / (2.0 * window_energy).sqrt();
        let bands = build_filterbank(sample_rate, fft_size, config.bands_per_octave)?;
        let definition_hash = definition_hash(
            sample_rate,
            window_samples,
            hop_samples,
            fft_size,
            spectral_lag_frames,
            config,
            &bands,
        );
        Ok(Self {
            config,
            sample_rate,
            window_samples,
            hop_samples,
            fft_size,
            spectral_lag_frames,
            band_count: bands.len(),
            channel_count: config.channel_count,
            window_energy,
            full_scale_gain,
            definition_hash,
            window,
            bands,
        })
    }

    pub fn definition_hex(&self) -> String {
        hex::encode(self.definition_hash)
    }

    pub fn band_triplets(&self) -> impl ExactSizeIterator<Item = [usize; 3]> + '_ {
        self.bands.iter().map(|band| band.bins)
    }

    pub fn verify_runtime_coefficients(
        &self,
    ) -> Result<SuperFluxRuntimeVerification, &'static str> {
        verify_runtime_coefficients(
            &self.window,
            self.window_samples,
            self.fft_size,
            self.window_energy,
            self.full_scale_gain,
            &self.bands,
        )
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SuperFluxRuntimeVerification {
    pub window_sum: f32,
    pub window_energy: f32,
    pub full_scale_gain: f32,
    pub band_weight_count: usize,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SuperFluxFrame {
    pub support_start_samples: i64,
    pub support_end_samples: i64,
    pub event_sample: i64,
    pub value: f32,
}

pub struct SuperFluxAnalyzer {
    layout: SuperFluxLayout,
    fft: Arc<dyn Fft<f32>>,
    fft_buffer: Vec<Complex32>,
    fft_scratch: Vec<Complex32>,
    power: Vec<f32>,
    magnitude: Vec<f32>,
    current_log_bands: Vec<f32>,
    log_history: Vec<f32>,
    frames_seen: usize,
}

impl SuperFluxAnalyzer {
    pub fn new(sample_rate: u32, config: SuperFluxConfig) -> Result<Self, &'static str> {
        let layout = SuperFluxLayout::for_rate(sample_rate, config)?;
        let mut planner = FftPlanner::<f32>::new();
        let fft = planner.plan_fft_forward(layout.fft_size);
        let bins = layout.fft_size / 2 + 1;
        let band_count = layout.band_count;
        let lag = layout.spectral_lag_frames;
        let scratch_len = fft.get_inplace_scratch_len();
        Ok(Self {
            fft,
            fft_buffer: vec![Complex32::ZERO; layout.fft_size],
            fft_scratch: vec![Complex32::ZERO; scratch_len],
            power: vec![0.0; bins],
            magnitude: vec![0.0; bins],
            current_log_bands: vec![0.0; band_count],
            log_history: vec![0.0; band_count * lag],
            frames_seen: 0,
            layout,
        })
    }

    pub fn layout(&self) -> &SuperFluxLayout {
        &self.layout
    }

    pub fn reset(&mut self) {
        self.fft_buffer.fill(Complex32::ZERO);
        self.fft_scratch.fill(Complex32::ZERO);
        self.power.fill(0.0);
        self.magnitude.fill(0.0);
        self.current_log_bands.fill(0.0);
        self.log_history.fill(0.0);
        self.frames_seen = 0;
    }

    pub fn analyze_window(
        &mut self,
        left: &[f32],
        right: Option<&[f32]>,
        support_start_samples: i64,
    ) -> Result<Option<SuperFluxFrame>, &'static str> {
        self.validate_input(left, right)?;
        let support_end_samples = support_start_samples
            .checked_add(self.layout.window_samples as i64)
            .ok_or("SuperFlux support endpoint overflow")?;
        let event_sample = support_start_samples
            .checked_add((self.layout.window_samples / 2) as i64)
            .ok_or("SuperFlux event sample overflow")?;
        if self.frames_seen == usize::MAX {
            return Err("SuperFlux frame counter overflow");
        }

        self.update_power(left, right);
        for (magnitude, power) in self.magnitude.iter_mut().zip(&self.power) {
            *magnitude = power.sqrt() / self.layout.full_scale_gain;
        }
        let amplitude_reference = self.layout.config.amplitude_reference();
        for (value, band) in self.current_log_bands.iter_mut().zip(&self.layout.bands) {
            let amplitude = self.magnitude[band.bins[0]..=band.bins[2]]
                .iter()
                .zip(&band.weights)
                .map(|(magnitude, weight)| magnitude * weight)
                .sum::<f32>();
            *value = (1.0 + amplitude / amplitude_reference).log10();
        }
        if self
            .current_log_bands
            .iter()
            .any(|value| !value.is_finite())
        {
            return Err("non-finite SuperFlux spectrum");
        }

        let lag = self.layout.spectral_lag_frames;
        let history_slot = self.frames_seen % lag;
        let value = (self.frames_seen >= lag).then(|| self.odf_value(history_slot));
        let offset = history_slot * self.layout.band_count;
        self.log_history[offset..offset + self.layout.band_count]
            .copy_from_slice(&self.current_log_bands);
        self.frames_seen += 1;
        Ok(value.map(|value| SuperFluxFrame {
            support_start_samples,
            support_end_samples,
            event_sample,
            value,
        }))
    }

    fn validate_input(&self, left: &[f32], right: Option<&[f32]>) -> Result<(), &'static str> {
        if left.len() != self.layout.window_samples || left.iter().any(|value| !value.is_finite()) {
            return Err("invalid SuperFlux left window");
        }
        match (self.layout.channel_count, right) {
            (1, None) => {}
            (2, Some(right))
                if right.len() == left.len() && right.iter().all(|value| value.is_finite()) => {}
            (1, Some(_)) | (2, None) => return Err("SuperFlux channel topology changed"),
            (2, Some(_)) => return Err("invalid SuperFlux right window"),
            _ => unreachable!("validated SuperFlux channel count"),
        }
        Ok(())
    }

    fn update_power(&mut self, left: &[f32], right: Option<&[f32]>) {
        match self.layout.config.channel_mode {
            SuperFluxChannelMode::Lr => {
                load_windowed(&mut self.fft_buffer, &self.layout.window, left, None, 1.0);
                self.run_fft();
                write_power(
                    &self.fft_buffer,
                    self.layout.window_energy,
                    &mut self.power,
                    false,
                );
                if let Some(right) = right {
                    load_windowed(&mut self.fft_buffer, &self.layout.window, right, None, 1.0);
                    self.run_fft();
                    write_power(
                        &self.fft_buffer,
                        self.layout.window_energy,
                        &mut self.power,
                        true,
                    );
                }
            }
            SuperFluxChannelMode::Mid => {
                load_windowed(&mut self.fft_buffer, &self.layout.window, left, right, 1.0);
                self.run_fft();
                write_power(
                    &self.fft_buffer,
                    self.layout.window_energy,
                    &mut self.power,
                    false,
                );
            }
            SuperFluxChannelMode::Side => {
                load_windowed(&mut self.fft_buffer, &self.layout.window, left, right, -1.0);
                self.run_fft();
                write_power(
                    &self.fft_buffer,
                    self.layout.window_energy,
                    &mut self.power,
                    false,
                );
            }
        }
    }

    fn run_fft(&mut self) {
        self.fft
            .process_with_scratch(&mut self.fft_buffer, &mut self.fft_scratch);
    }

    fn odf_value(&self, history_slot: usize) -> f32 {
        let offset = history_slot * self.layout.band_count;
        let reference = &self.log_history[offset..offset + self.layout.band_count];
        let radius = self.layout.config.maximum_filter_radius;
        let mut sum = 0.0;
        for (band, current) in self.current_log_bands.iter().enumerate() {
            let start = band.saturating_sub(radius);
            let end = (band + radius + 1).min(reference.len());
            let maximum = reference[start..end].iter().copied().fold(0.0, f32::max);
            sum += (current - maximum).max(0.0);
        }
        sum / self.layout.band_count as f32
    }
}

fn load_windowed(
    fft_buffer: &mut [Complex32],
    window: &[f32],
    left: &[f32],
    right: Option<&[f32]>,
    right_sign: f32,
) {
    fft_buffer.fill(Complex32::ZERO);
    match right {
        Some(right) => {
            for (((bin, left), right), window) in
                fft_buffer.iter_mut().zip(left).zip(right).zip(window)
            {
                bin.re = 0.5 * (left + right_sign * right) * window;
            }
        }
        None => {
            for ((bin, sample), window) in fft_buffer.iter_mut().zip(left).zip(window) {
                bin.re = sample * window;
            }
        }
    }
}

fn write_power(fft: &[Complex32], window_energy: f32, power: &mut [f32], average: bool) {
    let nyquist = power.len() - 1;
    for (index, (complex, output)) in fft.iter().zip(power).enumerate() {
        let one_sided = if index == 0 || index == nyquist {
            1.0
        } else {
            2.0
        };
        let value = one_sided * complex.norm_sqr() / window_energy;
        *output = if average {
            0.5 * (*output + value)
        } else {
            value
        };
    }
}

fn periodic_hann(len: usize) -> Vec<f32> {
    (0..len)
        .map(|index| 0.5 - 0.5 * (2.0 * PI * index as f32 / len as f32).cos())
        .collect()
}

fn rounded_reference_samples(sample_rate: u32, reference_samples: u32) -> usize {
    let numerator = u64::from(sample_rate) * u64::from(reference_samples);
    ((numerator + u64::from(SUPERFLUX_REFERENCE_RATE / 2)) / u64::from(SUPERFLUX_REFERENCE_RATE))
        as usize
}

#[cfg(test)]
#[path = "transient_superflux_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "transient_superflux_definition_tests.rs"]
mod definition_tests;
