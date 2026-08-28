//! Continuous-state psychoacoustic observations for the optional POST Perceptual Delta page.
//!
//! PRE and POST arm one shared presentation-time epoch before analysis starts. Phase D and the
//! optional sample-rate converter are reset once at that epoch, then remain continuous across the
//! non-overlapping 100 ms presentation endpoints. This is a display measurement only; it never
//! feeds the audio path or the existing Record result.

use std::collections::VecDeque;
use std::fmt;

use crate::phase_d::channels::PhaseDSharpnessChannelStream;
use crate::phase_d::tables::FieldType;
use crate::resampler::ResamplerTo48k;
use crate::spectrum::SpectrumChannelMode;

pub const PERCEPTUAL_SCHEMA_VERSION: u16 = 2;
pub const PERCEPTUAL_PRESENTATION_HZ: u32 = 10;
const PHASE_D_APERTURE_FRAMES: usize = 48_000 / PERCEPTUAL_PRESENTATION_HZ as usize;

#[derive(Clone, Debug, PartialEq)]
pub struct PerceptualFrame {
    pub schema_version: u16,
    pub sample_rate: u32,
    pub aperture_samples: u32,
    pub presentation_end_samples: i64,
    /// Shared PRE/POST reset boundary for every stateful stage in this observation sequence.
    pub state_epoch_samples: i64,
    pub generation: u64,
    pub channel_mode: SpectrumChannelMode,
    pub channels: u8,
    /// DIN 45692:2009 Widmann sharpness at this exact continuous-state endpoint [acum].
    pub sharpness: f64,
}

impl PerceptualFrame {
    pub fn compatible_with(&self, other: &Self) -> bool {
        self.schema_version == other.schema_version
            && self.sample_rate == other.sample_rate
            && self.aperture_samples == other.aperture_samples
            && self.presentation_end_samples == other.presentation_end_samples
            && self.state_epoch_samples == other.state_epoch_samples
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
    pub state_epoch_samples: i64,
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
        state_epoch_samples: post.state_epoch_samples,
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
    InvalidStateEpoch,
    DefinitionChanged,
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
            Self::InvalidStateEpoch => "Perceptual Delta state epoch is invalid",
            Self::DefinitionChanged => "Perceptual Delta definition changed without a new epoch",
            Self::NonFiniteInput => "Perceptual Delta input contains a non-finite sample",
            Self::SideRequiresStereo => "Perceptual Delta SIDE requires stereo input",
            Self::ResamplerUnavailable => "Perceptual Delta resampling failed",
            Self::AnalysisUnavailable => "Perceptual Delta analysis produced no frame",
        })
    }
}

impl std::error::Error for PerceptualError {}

pub struct SharpnessContinuousAnalyzer {
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
    state_epoch_samples: Option<i64>,
    active_mode: Option<SpectrumChannelMode>,
    pending_endpoints: VecDeque<i64>,
    output_frames: Vec<PerceptualFrame>,
}

impl SharpnessContinuousAnalyzer {
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
            state_epoch_samples: None,
            active_mode: None,
            pending_endpoints: VecDeque::with_capacity(4),
            output_frames: Vec::with_capacity(2),
        })
    }

    pub fn aperture_samples(&self) -> usize {
        (self.sample_rate / PERCEPTUAL_PRESENTATION_HZ) as usize
    }

    pub(crate) fn output_frames(&self) -> &[PerceptualFrame] {
        &self.output_frames
    }

    /// Reset every stateful stage once at a shared PRE/POST presentation boundary.
    pub fn reset_at_epoch(&mut self, state_epoch_samples: i64) -> Result<(), PerceptualError> {
        if state_epoch_samples.rem_euclid(self.aperture_samples() as i64) != 0 {
            return Err(PerceptualError::InvalidStateEpoch);
        }
        self.lr_stream.reset();
        self.mono_stream.reset();
        if let Some(resampler) = self.lr_resampler.as_mut() {
            resampler.reset();
        }
        if let Some(resampler) = self.mono_resampler.as_mut() {
            resampler.reset();
        }
        self.lr_samples.clear();
        self.mono_samples.clear();
        self.resampled_lr.clear();
        self.resampled_mono.clear();
        self.pending_endpoints.clear();
        self.output_frames.clear();
        self.active_mode = None;
        self.state_epoch_samples = Some(state_epoch_samples);
        Ok(())
    }

    pub fn analyze_aperture(
        &mut self,
        interleaved: &[f32],
        channel_mode: SpectrumChannelMode,
        presentation_end_samples: i64,
        generation: u64,
    ) -> Result<&[PerceptualFrame], PerceptualError> {
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
        let state_epoch_samples = self
            .state_epoch_samples
            .ok_or(PerceptualError::InvalidStateEpoch)?;
        if presentation_end_samples <= state_epoch_samples
            || presentation_end_samples.rem_euclid(self.aperture_samples() as i64) != 0
        {
            return Err(PerceptualError::InvalidStateEpoch);
        }
        if self.active_mode.is_some_and(|mode| mode != channel_mode) {
            return Err(PerceptualError::DefinitionChanged);
        }
        self.active_mode = Some(channel_mode);
        self.pending_endpoints.push_back(presentation_end_samples);
        self.output_frames.clear();
        match channel_mode {
            SpectrumChannelMode::Lr => self.analyze_lr(interleaved, generation)?,
            SpectrumChannelMode::Mid | SpectrumChannelMode::Side => {
                self.analyze_mono(interleaved, channel_mode, generation)?;
            }
        }
        Ok(&self.output_frames)
    }

    fn analyze_lr(&mut self, input: &[f32], generation: u64) -> Result<(), PerceptualError> {
        self.lr_samples.clear();
        self.lr_samples
            .extend(input.iter().map(|sample| f64::from(*sample)));
        if let Some(resampler) = self.lr_resampler.as_mut() {
            resampler
                .process(&self.lr_samples, &mut self.resampled_lr)
                .map_err(|_| PerceptualError::ResamplerUnavailable)?;
            self.drain_lr(generation)
        } else {
            let sharpness = self
                .lr_stream
                .push_interleaved_slot(&self.lr_samples)
                .ok_or(PerceptualError::AnalysisUnavailable)?;
            self.publish_next(sharpness, generation)
        }
    }

    fn analyze_mono(
        &mut self,
        input: &[f32],
        channel_mode: SpectrumChannelMode,
        generation: u64,
    ) -> Result<(), PerceptualError> {
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
        if let Some(resampler) = self.mono_resampler.as_mut() {
            resampler
                .process(&self.mono_samples, &mut self.resampled_mono)
                .map_err(|_| PerceptualError::ResamplerUnavailable)?;
            self.drain_mono(generation)
        } else {
            let sharpness = self
                .mono_stream
                .push_interleaved_slot(&self.mono_samples)
                .ok_or(PerceptualError::AnalysisUnavailable)?;
            self.publish_next(sharpness, generation)
        }
    }

    fn drain_lr(&mut self, generation: u64) -> Result<(), PerceptualError> {
        let slot_samples = PHASE_D_APERTURE_FRAMES * self.input_channels;
        while self.resampled_lr.len() >= slot_samples {
            let sharpness = self
                .lr_stream
                .push_interleaved_slot(&self.resampled_lr[..slot_samples])
                .ok_or(PerceptualError::AnalysisUnavailable)?;
            self.resampled_lr.drain(..slot_samples);
            self.publish_next(sharpness, generation)?;
        }
        Ok(())
    }

    fn drain_mono(&mut self, generation: u64) -> Result<(), PerceptualError> {
        while self.resampled_mono.len() >= PHASE_D_APERTURE_FRAMES {
            let sharpness = self
                .mono_stream
                .push_interleaved_slot(&self.resampled_mono[..PHASE_D_APERTURE_FRAMES])
                .ok_or(PerceptualError::AnalysisUnavailable)?;
            self.resampled_mono.drain(..PHASE_D_APERTURE_FRAMES);
            self.publish_next(sharpness, generation)?;
        }
        Ok(())
    }

    fn publish_next(&mut self, sharpness: f64, generation: u64) -> Result<(), PerceptualError> {
        let presentation_end_samples = self
            .pending_endpoints
            .pop_front()
            .ok_or(PerceptualError::AnalysisUnavailable)?;
        self.output_frames.push(PerceptualFrame {
            schema_version: PERCEPTUAL_SCHEMA_VERSION,
            sample_rate: self.sample_rate,
            aperture_samples: self.aperture_samples() as u32,
            presentation_end_samples,
            state_epoch_samples: self
                .state_epoch_samples
                .ok_or(PerceptualError::InvalidStateEpoch)?,
            generation,
            channel_mode: self.active_mode.ok_or(PerceptualError::DefinitionChanged)?,
            channels: self.input_channels as u8,
            sharpness,
        });
        Ok(())
    }
}

#[cfg(test)]
#[path = "perceptual_tests.rs"]
mod tests;
