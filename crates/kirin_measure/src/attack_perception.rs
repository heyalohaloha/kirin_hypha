//! Worker-side perceptual descriptors for one ATTACK event.
//!
//! The descriptors are factual observations, not quality judgements. The caller supplies an exact
//! 100 ms pre-event context and 30 ms attack aperture, both interleaved and zero-padded at the
//! content boundary when necessary. Stereo is aggregated as mean linear power; no downmix is used.

use std::fmt;

pub const ATTACK_CONTEXT_MICROS: u32 = 100_000;
pub const ATTACK_DETAIL_MICROS: u32 = 30_000;
pub const ATTACK_LEVEL_FLOOR_DBFS: f32 = -120.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackPerceptualFeatures {
    pub sample_rate: u32,
    pub channels: u8,
    pub context_frames: u32,
    pub attack_frames: u32,
    /// Attack RMS minus the immediately preceding context RMS.
    pub contrast_db: f32,
    /// True when either side of `contrast_db` touched the fixed -120 dBFS measurement floor.
    pub contrast_floor_limited: bool,
    pub context_rms_dbfs: f32,
    pub attack_rms_dbfs: f32,
    pub sample_peak_dbfs: f32,
    pub crest_db: f32,
    /// Level-independent first-difference power relative to attack power.
    pub sample_edge_ratio_db: f32,
    /// Contiguous width around the highest attack-power frame above the -3 dB power point.
    pub peak_plateau_ms: f32,
    /// Energy centroid inside the 30 ms aperture after subtracting preceding-context power.
    pub temporal_centroid_ms: Option<f32>,
    /// Hypha DIN 45692 Sharpness over the first source-grid 100 ms aperture containing the
    /// complete 30 ms attack; `None` when that aperture is unavailable.
    pub sharpness_acum: Option<f32>,
}

impl AttackPerceptualFeatures {
    pub fn analyze(
        context_interleaved: &[f32],
        attack_interleaved: &[f32],
        sample_rate: u32,
        channels: usize,
        sharpness_acum: Option<f32>,
    ) -> Result<Self, AttackPerceptionError> {
        validate_identity(sample_rate, channels)?;
        if sharpness_acum.is_some_and(|value| !value.is_finite() || value < 0.0) {
            return Err(AttackPerceptionError::InvalidSharpness);
        }
        let context_frames = validate_window(
            context_interleaved,
            sample_rate,
            channels,
            ATTACK_CONTEXT_MICROS,
            AttackPerceptionError::ContextLengthMismatch,
        )?;
        let attack_frames = validate_window(
            attack_interleaved,
            sample_rate,
            channels,
            ATTACK_DETAIL_MICROS,
            AttackPerceptionError::AttackLengthMismatch,
        )?;
        if context_interleaved
            .iter()
            .chain(attack_interleaved)
            .any(|sample| !sample.is_finite())
        {
            return Err(AttackPerceptionError::NonFiniteSample);
        }

        let context_power = mean_power(context_interleaved);
        let attack_power = mean_power(attack_interleaved);
        let floor_power = 10.0_f64.powf(f64::from(ATTACK_LEVEL_FLOOR_DBFS) / 10.0);
        let context_rms_dbfs = power_dbfs(context_power, floor_power);
        let attack_rms_dbfs = power_dbfs(attack_power, floor_power);
        let sample_peak = attack_interleaved
            .iter()
            .fold(0.0_f64, |peak, sample| peak.max(f64::from(sample.abs())));
        let sample_peak_dbfs = amplitude_dbfs(sample_peak, floor_power.sqrt());

        Ok(Self {
            sample_rate,
            channels: channels as u8,
            context_frames: context_frames as u32,
            attack_frames: attack_frames as u32,
            contrast_db: attack_rms_dbfs - context_rms_dbfs,
            contrast_floor_limited: context_power <= floor_power || attack_power <= floor_power,
            context_rms_dbfs,
            attack_rms_dbfs,
            sample_peak_dbfs,
            crest_db: sample_peak_dbfs - attack_rms_dbfs,
            sample_edge_ratio_db: sample_edge_ratio_db(attack_interleaved, channels, floor_power),
            peak_plateau_ms: peak_plateau_ms(
                attack_interleaved,
                channels,
                sample_rate,
                floor_power,
            ),
            temporal_centroid_ms: temporal_centroid_ms(
                attack_interleaved,
                channels,
                sample_rate,
                context_power,
            ),
            sharpness_acum,
        })
    }

    pub fn has_valid_layout(&self) -> bool {
        let expected_context = frames_for_micros(self.sample_rate, ATTACK_CONTEXT_MICROS);
        let expected_attack = frames_for_micros(self.sample_rate, ATTACK_DETAIL_MICROS);
        self.sample_rate > 0
            && matches!(self.channels, 1 | 2)
            && u64::from(self.context_frames) == expected_context
            && u64::from(self.attack_frames) == expected_attack
            && [
                self.contrast_db,
                self.context_rms_dbfs,
                self.attack_rms_dbfs,
                self.sample_peak_dbfs,
                self.crest_db,
                self.sample_edge_ratio_db,
                self.peak_plateau_ms,
            ]
            .into_iter()
            .all(f32::is_finite)
            && (0.0..=30.0).contains(&self.peak_plateau_ms)
            && self
                .temporal_centroid_ms
                .is_none_or(|value| value.is_finite() && (0.0..=30.0).contains(&value))
            && self
                .sharpness_acum
                .is_none_or(|value| value.is_finite() && value >= 0.0)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackPerceptualDelta {
    pub pre: AttackPerceptualFeatures,
    pub post: AttackPerceptualFeatures,
    pub contrast_db: f32,
    pub sample_peak_db: f32,
    pub crest_db: f32,
    pub sample_edge_ratio_db: f32,
    pub peak_plateau_ms: f32,
    pub temporal_centroid_ms: Option<f32>,
    pub sharpness_acum: Option<f32>,
}

impl AttackPerceptualDelta {
    pub fn between(
        pre: AttackPerceptualFeatures,
        post: AttackPerceptualFeatures,
    ) -> Result<Self, AttackPerceptionError> {
        if !pre.has_valid_layout() || !post.has_valid_layout() {
            return Err(AttackPerceptionError::InvalidFeatures);
        }
        if pre.sample_rate != post.sample_rate
            || pre.channels != post.channels
            || pre.context_frames != post.context_frames
            || pre.attack_frames != post.attack_frames
        {
            return Err(AttackPerceptionError::IdentityMismatch);
        }
        Ok(Self {
            pre,
            post,
            contrast_db: post.contrast_db - pre.contrast_db,
            sample_peak_db: post.sample_peak_dbfs - pre.sample_peak_dbfs,
            crest_db: post.crest_db - pre.crest_db,
            sample_edge_ratio_db: post.sample_edge_ratio_db - pre.sample_edge_ratio_db,
            peak_plateau_ms: post.peak_plateau_ms - pre.peak_plateau_ms,
            temporal_centroid_ms: difference(pre.temporal_centroid_ms, post.temporal_centroid_ms),
            sharpness_acum: difference(pre.sharpness_acum, post.sharpness_acum),
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AttackPerceptionError {
    InvalidSampleRate,
    InvalidChannels,
    ContextLengthMismatch,
    AttackLengthMismatch,
    NonFiniteSample,
    InvalidSharpness,
    InvalidFeatures,
    IdentityMismatch,
}

impl fmt::Display for AttackPerceptionError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::InvalidSampleRate => "ATTACK perceptual sample rate must be positive",
            Self::InvalidChannels => "ATTACK perceptual input must be mono or stereo",
            Self::ContextLengthMismatch => "ATTACK context must be exactly 100 ms",
            Self::AttackLengthMismatch => "ATTACK detail must be exactly 30 ms",
            Self::NonFiniteSample => "ATTACK perceptual input contains a non-finite sample",
            Self::InvalidSharpness => "ATTACK Sharpness must be finite and non-negative",
            Self::InvalidFeatures => "ATTACK perceptual features are invalid",
            Self::IdentityMismatch => "PRE/POST ATTACK perceptual layouts do not match",
        })
    }
}

fn validate_identity(sample_rate: u32, channels: usize) -> Result<(), AttackPerceptionError> {
    if sample_rate == 0 {
        return Err(AttackPerceptionError::InvalidSampleRate);
    }
    if !matches!(channels, 1 | 2) {
        return Err(AttackPerceptionError::InvalidChannels);
    }
    Ok(())
}

fn validate_window(
    samples: &[f32],
    sample_rate: u32,
    channels: usize,
    micros: u32,
    error: AttackPerceptionError,
) -> Result<usize, AttackPerceptionError> {
    let expected = frames_for_micros(sample_rate, micros);
    let expected_samples = expected
        .checked_mul(channels as u64)
        .and_then(|value| usize::try_from(value).ok())
        .ok_or(error)?;
    if samples.len() != expected_samples {
        return Err(error);
    }
    usize::try_from(expected).map_err(|_| error)
}

fn frames_for_micros(sample_rate: u32, micros: u32) -> u64 {
    (u64::from(sample_rate) * u64::from(micros) + 500_000) / 1_000_000
}

fn mean_power(samples: &[f32]) -> f64 {
    samples
        .iter()
        .map(|sample| {
            let value = f64::from(*sample);
            value * value
        })
        .sum::<f64>()
        / samples.len() as f64
}

fn power_dbfs(power: f64, floor_power: f64) -> f32 {
    (10.0 * power.max(floor_power).log10()) as f32
}

fn amplitude_dbfs(amplitude: f64, floor_amplitude: f64) -> f32 {
    (20.0 * amplitude.max(floor_amplitude).log10()) as f32
}

fn temporal_centroid_ms(
    attack: &[f32],
    channels: usize,
    sample_rate: u32,
    context_power: f64,
) -> Option<f32> {
    let mut weighted_time = 0.0_f64;
    let mut excess_sum = 0.0_f64;
    for (frame, channel_samples) in attack.chunks_exact(channels).enumerate() {
        let frame_power = channel_samples
            .iter()
            .map(|sample| f64::from(*sample).powi(2))
            .sum::<f64>()
            / channels as f64;
        let excess = (frame_power - context_power).max(0.0);
        weighted_time += (frame as f64 + 0.5) * excess;
        excess_sum += excess;
    }
    (excess_sum > f64::EPSILON)
        .then(|| (weighted_time / excess_sum / f64::from(sample_rate) * 1_000.0) as f32)
}

fn sample_edge_ratio_db(attack: &[f32], channels: usize, floor_power: f64) -> f32 {
    let frames = attack.len() / channels;
    if frames < 2 {
        return ATTACK_LEVEL_FLOOR_DBFS;
    }
    let mut difference_power = 0.0_f64;
    for frame in 1..frames {
        let current = frame * channels;
        let previous = (frame - 1) * channels;
        for channel in 0..channels {
            let difference =
                f64::from(attack[current + channel]) - f64::from(attack[previous + channel]);
            difference_power += difference * difference;
        }
    }
    difference_power /= ((frames - 1) * channels) as f64;
    let signal_power = mean_power(attack).max(floor_power);
    let ratio_floor = 10.0_f64.powf(f64::from(ATTACK_LEVEL_FLOOR_DBFS) / 10.0);
    (10.0 * (difference_power / signal_power).max(ratio_floor).log10()) as f32
}

fn peak_plateau_ms(attack: &[f32], channels: usize, sample_rate: u32, floor_power: f64) -> f32 {
    let frames = attack.len() / channels;
    let Some((peak_index, peak_power)) = (0..frames)
        .map(|index| (index, frame_power_at(attack, channels, index)))
        .max_by(|left, right| left.1.total_cmp(&right.1))
    else {
        return 0.0;
    };
    if peak_power <= floor_power {
        return 0.0;
    }
    let threshold = peak_power * 0.5;
    let mut first = peak_index;
    while first > 0 && frame_power_at(attack, channels, first - 1) >= threshold {
        first -= 1;
    }
    let mut last = peak_index;
    while last + 1 < frames && frame_power_at(attack, channels, last + 1) >= threshold {
        last += 1;
    }
    ((last - first + 1) as f64 * 1_000.0 / f64::from(sample_rate)) as f32
}

fn frame_power_at(attack: &[f32], channels: usize, frame: usize) -> f64 {
    let start = frame * channels;
    attack[start..start + channels]
        .iter()
        .map(|sample| f64::from(*sample).powi(2))
        .sum::<f64>()
        / channels as f64
}

fn difference(pre: Option<f32>, post: Option<f32>) -> Option<f32> {
    pre.zip(post).map(|(pre, post)| post - pre)
}

#[cfg(test)]
#[path = "attack_perception_tests.rs"]
mod tests;
