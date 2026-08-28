//! Exact-aperture psychoacoustic observations for the optional POST Perceptual Delta page.
//!
//! Every frame is derived from one non-overlapping 100 ms presentation-time aperture. The
//! Phase D state and optional sample-rate converter are reset at each aperture, so a PRE/POST
//! difference never depends on which plug-in noticed the on-demand request first. This is a
//! display measurement only; it never feeds the audio path or the existing Record result.

use std::fmt;

use crate::phase_d::channels::PhaseDSharpnessChannelStream;
use crate::phase_d::tables::FieldType;
use crate::resampler::ResamplerTo48k;
use crate::spectrum::SpectrumChannelMode;

pub const PERCEPTUAL_SCHEMA_VERSION: u16 = 1;
pub const PERCEPTUAL_PRESENTATION_HZ: u32 = 10;

#[derive(Clone, Debug, PartialEq)]
pub struct PerceptualFrame {
    pub schema_version: u16,
    pub sample_rate: u32,
    pub aperture_samples: u32,
    pub presentation_end_samples: i64,
    pub generation: u64,
    pub channel_mode: SpectrumChannelMode,
    pub channels: u8,
    /// DIN 45692:2009 Widmann sharpness for this exact aperture [acum].
    pub sharpness: f64,
}

impl PerceptualFrame {
    pub fn compatible_with(&self, other: &Self) -> bool {
        self.schema_version == other.schema_version
            && self.sample_rate == other.sample_rate
            && self.aperture_samples == other.aperture_samples
            && self.presentation_end_samples == other.presentation_end_samples
            && self.channel_mode == other.channel_mode
            && self.channels == other.channels
            && self.sharpness.is_finite()
            && other.sharpness.is_finite()
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PerceptualDifference {
    pub presentation_end_samples: i64,
    pub sample_rate: u32,
    pub aperture_samples: u32,
    pub channel_mode: SpectrumChannelMode,
    pub channels: u8,
    pub pre_sharpness: f64,
    pub post_sharpness: f64,
    /// Signed POST - PRE sharpness [acum]. This fact is never clipped.
    pub delta_sharpness: f64,
}

pub fn difference_post_minus_pre(
    post: &PerceptualFrame,
    pre: &PerceptualFrame,
) -> Option<PerceptualDifference> {
    if !post.compatible_with(pre) {
        return None;
    }
    Some(PerceptualDifference {
        presentation_end_samples: post.presentation_end_samples,
        sample_rate: post.sample_rate,
        aperture_samples: post.aperture_samples,
        channel_mode: post.channel_mode,
        channels: post.channels,
        pre_sharpness: pre.sharpness,
        post_sharpness: post.sharpness,
        delta_sharpness: post.sharpness - pre.sharpness,
    })
}

#[derive(Debug)]
pub enum PerceptualError {
    InvalidSampleRate,
    WrongApertureLength,
    NonFiniteInput,
    SideRequiresStereo,
    ResamplerUnavailable,
    AnalysisUnavailable,
}

impl fmt::Display for PerceptualError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::InvalidSampleRate => "invalid Perceptual Delta sample rate",
            Self::WrongApertureLength => "Perceptual Delta requires one exact 100 ms aperture",
            Self::NonFiniteInput => "Perceptual Delta input contains a non-finite sample",
            Self::SideRequiresStereo => "Perceptual Delta SIDE requires stereo input",
            Self::ResamplerUnavailable => "Perceptual Delta resampling failed",
            Self::AnalysisUnavailable => "Perceptual Delta analysis produced no frame",
        })
    }
}

impl std::error::Error for PerceptualError {}

pub struct SharpnessApertureAnalyzer {
    sample_rate: u32,
    input_channels: usize,
    lr_samples: Vec<f64>,
    mono_samples: Vec<f64>,
    resampled_lr: Vec<f64>,
    resampled_mono: Vec<f64>,
    lr_resampler: Option<ResamplerTo48k>,
    mono_resampler: Option<ResamplerTo48k>,
    lr_stream: PhaseDSharpnessChannelStream,
    mono_stream: PhaseDSharpnessChannelStream,
}

impl SharpnessApertureAnalyzer {
    pub fn new(sample_rate: u32, input_channels: usize) -> Result<Self, PerceptualError> {
        if !(8_000..=384_000).contains(&sample_rate)
            || !sample_rate.is_multiple_of(PERCEPTUAL_PRESENTATION_HZ)
            || !(1..=2).contains(&input_channels)
        {
            return Err(PerceptualError::InvalidSampleRate);
        }
        let aperture = (sample_rate / PERCEPTUAL_PRESENTATION_HZ) as usize;
        let resampler = |channels| {
            (sample_rate != 48_000)
                .then(|| ResamplerTo48k::new(sample_rate, channels).ok())
                .flatten()
        };
        let lr_resampler = resampler(input_channels);
        let mono_resampler = resampler(1);
        if sample_rate != 48_000 && (lr_resampler.is_none() || mono_resampler.is_none()) {
            return Err(PerceptualError::ResamplerUnavailable);
        }
        Ok(Self {
            sample_rate,
            input_channels,
            lr_samples: Vec::with_capacity(aperture * input_channels),
            mono_samples: Vec::with_capacity(aperture),
            resampled_lr: Vec::with_capacity(4_800 * input_channels),
            resampled_mono: Vec::with_capacity(4_800),
            lr_resampler,
            mono_resampler,
            lr_stream: PhaseDSharpnessChannelStream::new(FieldType::Free, input_channels),
            mono_stream: PhaseDSharpnessChannelStream::new(FieldType::Free, 1),
        })
    }

    pub fn aperture_samples(&self) -> usize {
        (self.sample_rate / PERCEPTUAL_PRESENTATION_HZ) as usize
    }

    pub fn analyze(
        &mut self,
        interleaved: &[f32],
        channel_mode: SpectrumChannelMode,
        presentation_end_samples: i64,
        generation: u64,
    ) -> Result<PerceptualFrame, PerceptualError> {
        let expected = self.aperture_samples() * self.input_channels;
        if interleaved.len() != expected {
            return Err(PerceptualError::WrongApertureLength);
        }
        if interleaved.iter().any(|sample| !sample.is_finite()) {
            return Err(PerceptualError::NonFiniteInput);
        }
        if channel_mode == SpectrumChannelMode::Side && self.input_channels != 2 {
            return Err(PerceptualError::SideRequiresStereo);
        }
        let sharpness = match channel_mode {
            SpectrumChannelMode::Lr => self.analyze_lr(interleaved)?,
            SpectrumChannelMode::Mid | SpectrumChannelMode::Side => {
                self.analyze_mono(interleaved, channel_mode)?
            }
        };
        Ok(PerceptualFrame {
            schema_version: PERCEPTUAL_SCHEMA_VERSION,
            sample_rate: self.sample_rate,
            aperture_samples: self.aperture_samples() as u32,
            presentation_end_samples,
            generation,
            channel_mode,
            channels: self.input_channels as u8,
            sharpness,
        })
    }

    fn analyze_lr(&mut self, input: &[f32]) -> Result<f64, PerceptualError> {
        self.lr_samples.clear();
        self.lr_samples
            .extend(input.iter().map(|sample| f64::from(*sample)));
        self.lr_stream.reset();
        if let Some(resampler) = self.lr_resampler.as_mut() {
            self.resampled_lr.clear();
            resampler.reset();
            resampler
                .process(&self.lr_samples, &mut self.resampled_lr)
                .map_err(|_| PerceptualError::ResamplerUnavailable)?;
            resampler
                .finish(&mut self.resampled_lr)
                .map_err(|_| PerceptualError::ResamplerUnavailable)?;
            self.lr_stream
                .push_interleaved_slot(&self.resampled_lr)
                .ok_or(PerceptualError::AnalysisUnavailable)
        } else {
            self.lr_stream
                .push_interleaved_slot(&self.lr_samples)
                .ok_or(PerceptualError::AnalysisUnavailable)
        }
    }

    fn analyze_mono(
        &mut self,
        input: &[f32],
        channel_mode: SpectrumChannelMode,
    ) -> Result<f64, PerceptualError> {
        self.mono_samples.clear();
        if self.input_channels == 1 {
            self.mono_samples
                .extend(input.iter().map(|sample| f64::from(*sample)));
        } else {
            let polarity = if channel_mode == SpectrumChannelMode::Mid {
                1.0_f64
            } else {
                -1.0_f64
            };
            let (frames, remainder) = input.as_chunks::<2>();
            debug_assert!(remainder.is_empty());
            self.mono_samples.extend(
                frames
                    .iter()
                    .map(|frame| (f64::from(frame[0]) + polarity * f64::from(frame[1])) * 0.5),
            );
        }
        self.mono_stream.reset();
        if let Some(resampler) = self.mono_resampler.as_mut() {
            self.resampled_mono.clear();
            resampler.reset();
            resampler
                .process(&self.mono_samples, &mut self.resampled_mono)
                .map_err(|_| PerceptualError::ResamplerUnavailable)?;
            resampler
                .finish(&mut self.resampled_mono)
                .map_err(|_| PerceptualError::ResamplerUnavailable)?;
            self.mono_stream
                .push_interleaved_slot(&self.resampled_mono)
                .ok_or(PerceptualError::AnalysisUnavailable)
        } else {
            self.mono_stream
                .push_interleaved_slot(&self.mono_samples)
                .ok_or(PerceptualError::AnalysisUnavailable)
        }
    }
}

#[cfg(test)]
#[path = "perceptual_tests.rs"]
mod tests;
