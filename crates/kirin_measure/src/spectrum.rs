//! On-demand Spectrum analysis shared by the PRE producer and POST difference view.
//!
//! This module owns no thread, filesystem path, UI state, or audio output. Callers provide one
//! exact, contiguous presentation-time window. The analyzer keeps its FFT plan and scratch
//! storage, while every published frame has a fixed-size payload.

use std::fmt;
use std::sync::Arc;

use rustfft::num_complex::Complex32;
use rustfft::{Fft, FftPlanner};

pub const SPECTRUM_SCHEMA_VERSION: u16 = 2;
pub const SPECTRUM_WINDOW_SIZE: usize = 4_096;
pub const SPECTRUM_FFT_SIZE: usize = 8_192;
pub const SPECTRUM_BAND_COUNT: usize = 256;
pub const SPECTRUM_PRESENTATION_HZ: u32 = 30;
pub const SPECTRUM_MIN_HZ: f32 = 10.0;
pub const SPECTRUM_MAX_HZ: f32 = 22_000.0;
pub const SPECTRUM_FLOOR_DBFS: f32 = -144.0;
pub const SPECTRUM_DISPLAY_FLOOR_START_DBFS: f32 = -120.0;
pub const SPECTRUM_DISPLAY_FLOOR_END_DBFS: f32 = -96.0;
pub const SPECTRUM_DIFF_RANGE_DB: f32 = 18.0;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum AnalysisViewMode {
    #[default]
    Spectrum = 0,
    Perceptual = 1,
}

impl TryFrom<u8> for AnalysisViewMode {
    type Error = ();

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Spectrum),
            1 => Ok(Self::Perceptual),
            _ => Err(()),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum SpectrumChannelMode {
    #[default]
    Lr = 0,
    Mid = 1,
    Side = 2,
}

impl TryFrom<u8> for SpectrumChannelMode {
    type Error = ();

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Lr),
            1 => Ok(Self::Mid),
            2 => Ok(Self::Side),
            _ => Err(()),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct SpectrumFrame {
    pub schema_version: u16,
    pub sample_rate: u32,
    pub fft_size: u32,
    pub band_count: u16,
    pub presentation_end_samples: i64,
    pub generation: u64,
    pub channel_mode: SpectrumChannelMode,
    pub channels: u8,
    /// First frequency backed by a real FFT bin. A renderer must not invent points below it.
    pub min_hz: f32,
    pub max_hz: f32,
    pub dbfs: [f32; SPECTRUM_BAND_COUNT],
}

impl SpectrumFrame {
    pub fn compatible_with(&self, other: &Self) -> bool {
        self.schema_version == other.schema_version
            && self.sample_rate == other.sample_rate
            && self.fft_size == other.fft_size
            && self.band_count == other.band_count
            && self.presentation_end_samples == other.presentation_end_samples
            && self.channel_mode == other.channel_mode
            && self.channels == other.channels
            && self.min_hz.to_bits() == other.min_hz.to_bits()
            && self.max_hz.to_bits() == other.max_hz.to_bits()
            && self.dbfs.iter().all(|value| value.is_finite())
            && other.dbfs.iter().all(|value| value.is_finite())
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct SpectrumDifference {
    pub presentation_end_samples: i64,
    pub sample_rate: u32,
    pub min_hz: f32,
    pub max_hz: f32,
    pub channel_mode: SpectrumChannelMode,
    pub channels: u8,
    /// Exact PRE magnitude used for this difference. Presentation only; never fed back to DSP.
    pub pre_dbfs: [f32; SPECTRUM_BAND_COUNT],
    /// Exact POST magnitude used for this difference. Presentation only; never fed back to DSP.
    pub post_dbfs: [f32; SPECTRUM_BAND_COUNT],
    /// Signed POST - PRE difference. This raw fact is never clipped.
    pub raw_db: [f32; SPECTRUM_BAND_COUNT],
    /// Display-only floor confidence. The raw difference above remains untouched.
    pub display_db: [f32; SPECTRUM_BAND_COUNT],
}

pub fn difference_post_minus_pre(
    post: &SpectrumFrame,
    pre: &SpectrumFrame,
) -> Option<SpectrumDifference> {
    if !post.compatible_with(pre) {
        return None;
    }
    let mut raw_db = [0.0; SPECTRUM_BAND_COUNT];
    let mut display_db = [0.0; SPECTRUM_BAND_COUNT];
    for index in 0..SPECTRUM_BAND_COUNT {
        raw_db[index] = post.dbfs[index] - pre.dbfs[index];
        let audible = post.dbfs[index].max(pre.dbfs[index]);
        let confidence = ((audible - SPECTRUM_DISPLAY_FLOOR_START_DBFS)
            / (SPECTRUM_DISPLAY_FLOOR_END_DBFS - SPECTRUM_DISPLAY_FLOOR_START_DBFS))
            .clamp(0.0, 1.0);
        display_db[index] = raw_db[index] * confidence;
    }
    Some(SpectrumDifference {
        presentation_end_samples: post.presentation_end_samples,
        sample_rate: post.sample_rate,
        min_hz: post.min_hz,
        max_hz: post.max_hz,
        channel_mode: post.channel_mode,
        channels: post.channels,
        pre_dbfs: pre.dbfs,
        post_dbfs: post.dbfs,
        raw_db,
        display_db,
    })
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum SpectrumError {
    InvalidSampleRate,
    WrongWindowLength,
    NonFiniteInput,
    SideRequiresStereo,
}

impl fmt::Display for SpectrumError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let message = match self {
            Self::InvalidSampleRate => "invalid Spectrum sample rate",
            Self::WrongWindowLength => "Spectrum window must contain exactly 4096 samples",
            Self::NonFiniteInput => "Spectrum input contains a non-finite sample",
            Self::SideRequiresStereo => "Spectrum SIDE requires stereo input",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for SpectrumError {}

#[derive(Clone, Copy)]
enum BandPlan {
    Interpolate {
        lower: usize,
        upper: usize,
        mix: f32,
    },
    Average {
        first: usize,
        last: usize,
    },
}

pub struct SpectrumAnalyzer {
    sample_rate: u32,
    min_hz: f32,
    max_hz: f32,
    fft: Arc<dyn Fft<f32>>,
    window: Vec<f32>,
    fft_buffer: Vec<Complex32>,
    fft_scratch: Vec<Complex32>,
    left_power: Vec<f32>,
    right_power: Vec<f32>,
    combined: Vec<f32>,
    bands: [BandPlan; SPECTRUM_BAND_COUNT],
    amplitude_scale: f32,
}

impl SpectrumAnalyzer {
    pub fn new(sample_rate: u32) -> Result<Self, SpectrumError> {
        if !(8_000..=384_000).contains(&sample_rate) {
            return Err(SpectrumError::InvalidSampleRate);
        }
        let mut planner = FftPlanner::<f32>::new();
        let fft = planner.plan_fft_forward(SPECTRUM_FFT_SIZE);
        let window = (0..SPECTRUM_WINDOW_SIZE)
            .map(|index| {
                let phase = std::f32::consts::TAU * index as f32
                    / (SPECTRUM_WINDOW_SIZE.saturating_sub(1)) as f32;
                0.5 - 0.5 * phase.cos()
            })
            .collect::<Vec<_>>();
        let amplitude_scale = 2.0 / window.iter().sum::<f32>();
        let bin_hz = sample_rate as f32 / SPECTRUM_FFT_SIZE as f32;
        let min_hz = SPECTRUM_MIN_HZ.max(bin_hz);
        let max_hz = SPECTRUM_MAX_HZ.min(sample_rate as f32 * 0.5 - bin_hz);
        if !(max_hz > min_hz && max_hz.is_finite()) {
            return Err(SpectrumError::InvalidSampleRate);
        }
        let bands = std::array::from_fn(|index| {
            band_plan(index, min_hz, max_hz, bin_hz, SPECTRUM_FFT_SIZE / 2 - 1)
        });
        Ok(Self {
            sample_rate,
            min_hz,
            max_hz,
            fft_scratch: vec![Complex32::ZERO; fft.get_inplace_scratch_len()],
            fft_buffer: vec![Complex32::ZERO; SPECTRUM_FFT_SIZE],
            left_power: vec![0.0; SPECTRUM_FFT_SIZE / 2],
            right_power: vec![0.0; SPECTRUM_FFT_SIZE / 2],
            combined: vec![0.0; SPECTRUM_WINDOW_SIZE],
            fft,
            window,
            bands,
            amplitude_scale,
        })
    }

    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    pub fn analyze(
        &mut self,
        left: &[f32],
        right: Option<&[f32]>,
        presentation_end_samples: i64,
        generation: u64,
    ) -> Result<SpectrumFrame, SpectrumError> {
        self.analyze_mode(
            left,
            right,
            SpectrumChannelMode::Lr,
            presentation_end_samples,
            generation,
        )
    }

    pub fn analyze_mode(
        &mut self,
        left: &[f32],
        right: Option<&[f32]>,
        channel_mode: SpectrumChannelMode,
        presentation_end_samples: i64,
        generation: u64,
    ) -> Result<SpectrumFrame, SpectrumError> {
        if left.len() != SPECTRUM_WINDOW_SIZE
            || right.is_some_and(|samples| samples.len() != SPECTRUM_WINDOW_SIZE)
        {
            return Err(SpectrumError::WrongWindowLength);
        }
        if left
            .iter()
            .chain(right.into_iter().flatten())
            .any(|value| !value.is_finite())
        {
            return Err(SpectrumError::NonFiniteInput);
        }
        let channels = if right.is_some() { 2 } else { 1 };
        match (channel_mode, right) {
            (SpectrumChannelMode::Side, None) => return Err(SpectrumError::SideRequiresStereo),
            (SpectrumChannelMode::Mid | SpectrumChannelMode::Side, Some(right)) => {
                let polarity = if channel_mode == SpectrumChannelMode::Mid {
                    1.0
                } else {
                    -1.0
                };
                for ((combined, left), right) in self.combined.iter_mut().zip(left).zip(right) {
                    *combined = (*left + polarity * *right) * 0.5;
                }
                Self::transform_channel(
                    &self.fft,
                    &self.window,
                    self.amplitude_scale,
                    &mut self.fft_buffer,
                    &mut self.fft_scratch,
                    &mut self.left_power,
                    &self.combined,
                );
            }
            (SpectrumChannelMode::Mid, None) | (SpectrumChannelMode::Lr, None) => {
                Self::transform_channel(
                    &self.fft,
                    &self.window,
                    self.amplitude_scale,
                    &mut self.fft_buffer,
                    &mut self.fft_scratch,
                    &mut self.left_power,
                    left,
                );
            }
            (SpectrumChannelMode::Lr, Some(right)) => {
                Self::transform_channel(
                    &self.fft,
                    &self.window,
                    self.amplitude_scale,
                    &mut self.fft_buffer,
                    &mut self.fft_scratch,
                    &mut self.left_power,
                    left,
                );
                Self::transform_channel(
                    &self.fft,
                    &self.window,
                    self.amplitude_scale,
                    &mut self.fft_buffer,
                    &mut self.fft_scratch,
                    &mut self.right_power,
                    right,
                );
                for (left, right) in self.left_power.iter_mut().zip(&self.right_power) {
                    *left = (*left + *right) * 0.5;
                }
            }
        }
        let floor_power = 10.0_f32.powf(SPECTRUM_FLOOR_DBFS / 10.0);
        let dbfs = std::array::from_fn(|index| {
            let power = match self.bands[index] {
                BandPlan::Interpolate { lower, upper, mix } => {
                    self.left_power[lower] * (1.0 - mix) + self.left_power[upper] * mix
                }
                BandPlan::Average { first, last } => {
                    let values = &self.left_power[first..=last];
                    values.iter().sum::<f32>() / values.len() as f32
                }
            };
            10.0 * power.max(floor_power).log10()
        });
        Ok(SpectrumFrame {
            schema_version: SPECTRUM_SCHEMA_VERSION,
            sample_rate: self.sample_rate,
            fft_size: SPECTRUM_FFT_SIZE as u32,
            band_count: SPECTRUM_BAND_COUNT as u16,
            presentation_end_samples,
            generation,
            channel_mode,
            channels,
            min_hz: self.min_hz,
            max_hz: self.max_hz,
            dbfs,
        })
    }

    #[allow(clippy::too_many_arguments)]
    fn transform_channel(
        fft: &Arc<dyn Fft<f32>>,
        window: &[f32],
        amplitude_scale: f32,
        fft_buffer: &mut [Complex32],
        fft_scratch: &mut [Complex32],
        power: &mut [f32],
        samples: &[f32],
    ) {
        fft_buffer.fill(Complex32::ZERO);
        for ((slot, sample), window) in fft_buffer.iter_mut().zip(samples).zip(window) {
            *slot = Complex32::new(*sample * *window, 0.0);
        }
        fft.process_with_scratch(fft_buffer, fft_scratch);
        for (target, value) in power.iter_mut().zip(fft_buffer.iter()) {
            *target = value.norm_sqr() * amplitude_scale * amplitude_scale;
        }
    }
}

fn band_plan(index: usize, min_hz: f32, max_hz: f32, bin_hz: f32, max_bin: usize) -> BandPlan {
    let ratio = max_hz / min_hz;
    let edge =
        |offset: usize| min_hz * ratio.powf((index + offset) as f32 / SPECTRUM_BAND_COUNT as f32);
    let low = edge(0);
    let high = edge(1);
    let first = (low / bin_hz).ceil().max(1.0) as usize;
    let last = (high / bin_hz).floor().min(max_bin as f32) as usize;
    if last >= first {
        return BandPlan::Average { first, last };
    }
    let center = (low * high).sqrt() / bin_hz;
    let lower = (center.floor() as usize).clamp(1, max_bin.saturating_sub(1));
    BandPlan::Interpolate {
        lower,
        upper: lower + 1,
        mix: (center - lower as f32).clamp(0.0, 1.0),
    }
}

#[cfg(test)]
#[path = "spectrum_tests.rs"]
mod tests;
