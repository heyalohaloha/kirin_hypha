//! POST-only absolute observation timeline for the optional Analysis page.
//!
//! LUFS-M and recent True Peak use the same `MeasureEngine` implementation as Watch. Sharpness
//! uses the established continuous Phase D path. All three are joined inside one exact 100 ms
//! host-presentation aperture; this display state never replaces Watch, Record, or plugin_data.

use std::collections::VecDeque;
use std::fmt;

use crate::perceptual::{PerceptualError, SharpnessContinuousAnalyzer};
use crate::spectrum::SpectrumChannelMode;
use crate::{MeasureEngine, MeasureResult};

pub const ABSOLUTE_SCHEMA_VERSION: u16 = 1;
pub const ABSOLUTE_PRESENTATION_HZ: u32 = 10;
pub const ABSOLUTE_TIMELINE_CAPACITY: usize = 64;
pub const ABSOLUTE_HISTORY_SECONDS: i64 = 6;
const JOIN_CAPACITY: usize = 16;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct AbsoluteFrame {
    pub schema_version: u16,
    pub sample_rate: u32,
    pub aperture_samples: u32,
    pub presentation_end_samples: i64,
    pub state_epoch_samples: i64,
    pub generation: u64,
    pub channels: u8,
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
    pub sharpness: Option<f64>,
}

impl AbsoluteFrame {
    pub fn is_valid(&self) -> bool {
        self.schema_version == ABSOLUTE_SCHEMA_VERSION
            && self.sample_rate >= 8_000
            && self.aperture_samples > 0
            && self.presentation_end_samples > self.state_epoch_samples
            && self.generation != 0
            && (1..=2).contains(&self.channels)
            && self.sharpness.is_some()
            && [self.lufs_m, self.true_peak, self.sharpness]
                .into_iter()
                .flatten()
                .all(f64::is_finite)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct AbsoluteTimeline {
    frames: VecDeque<AbsoluteFrame>,
}

impl Default for AbsoluteTimeline {
    fn default() -> Self {
        Self {
            frames: VecDeque::with_capacity(ABSOLUTE_TIMELINE_CAPACITY),
        }
    }
}

impl AbsoluteTimeline {
    pub fn push(&mut self, frame: AbsoluteFrame) -> bool {
        if !frame.is_valid() {
            return false;
        }
        if let Some(newest) = self.frames.back() {
            if !same_format(newest, &frame) {
                self.frames.clear();
            } else if frame.presentation_end_samples <= newest.presentation_end_samples {
                // A new state epoch moving backwards is a confirmed transport boundary. A
                // duplicate or stale result inside the same epoch must never replace truth that
                // has already been published.
                if frame.presentation_end_samples < newest.presentation_end_samples
                    && (frame.state_epoch_samples != newest.state_epoch_samples
                        || frame.generation != newest.generation)
                {
                    self.frames.clear();
                } else {
                    return false;
                }
            } else {
                let history_samples = i64::from(frame.sample_rate) * ABSOLUTE_HISTORY_SECONDS;
                if frame.presentation_end_samples - newest.presentation_end_samples
                    >= history_samples
                {
                    self.frames.clear();
                }
            }
        }
        if self.frames.len() == ABSOLUTE_TIMELINE_CAPACITY {
            self.frames.pop_front();
        }
        self.frames.push_back(frame);
        let history_samples = i64::from(frame.sample_rate) * ABSOLUTE_HISTORY_SECONDS;
        while self.frames.front().is_some_and(|oldest| {
            frame.presentation_end_samples - oldest.presentation_end_samples > history_samples
        }) {
            self.frames.pop_front();
        }
        true
    }

    pub fn newest(&self) -> Option<&AbsoluteFrame> {
        self.frames.back()
    }

    pub fn frames(&self) -> impl DoubleEndedIterator<Item = &AbsoluteFrame> + ExactSizeIterator {
        self.frames.iter()
    }

    pub fn clear(&mut self) {
        self.frames.clear();
    }
}

fn same_format(left: &AbsoluteFrame, right: &AbsoluteFrame) -> bool {
    left.schema_version == right.schema_version
        && left.sample_rate == right.sample_rate
        && left.aperture_samples == right.aperture_samples
        && left.channels == right.channels
}

#[derive(Debug)]
pub enum AbsoluteError {
    InvalidFormat,
    WrongApertureLength,
    InvalidEpoch,
    CoreUnavailable,
    Perceptual(PerceptualError),
}

impl fmt::Display for AbsoluteError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::InvalidFormat => "invalid absolute observation format",
            Self::WrongApertureLength => "absolute observation requires one exact 100 ms aperture",
            Self::InvalidEpoch => "absolute observation endpoint is outside its state epoch",
            Self::CoreUnavailable => "absolute observation core produced no frame",
            Self::Perceptual(_) => "absolute Sharpness observation failed",
        })
    }
}

impl std::error::Error for AbsoluteError {}

impl From<PerceptualError> for AbsoluteError {
    fn from(error: PerceptualError) -> Self {
        Self::Perceptual(error)
    }
}

pub struct AbsoluteContinuousAnalyzer {
    sample_rate: u32,
    channels: usize,
    aperture_samples: usize,
    core: MeasureEngine,
    sharpness: SharpnessContinuousAnalyzer,
    input_f64: Vec<f64>,
    pending_core: VecDeque<(i64, MeasureResult)>,
    output_frames: Vec<AbsoluteFrame>,
    state_epoch_samples: Option<i64>,
}

impl AbsoluteContinuousAnalyzer {
    pub fn new(sample_rate: u32, channels: usize) -> Result<Self, AbsoluteError> {
        if !(8_000..=384_000).contains(&sample_rate)
            || !sample_rate.is_multiple_of(ABSOLUTE_PRESENTATION_HZ)
            || !(1..=2).contains(&channels)
        {
            return Err(AbsoluteError::InvalidFormat);
        }
        let aperture_samples = (sample_rate / ABSOLUTE_PRESENTATION_HZ) as usize;
        Ok(Self {
            sample_rate,
            channels,
            aperture_samples,
            core: MeasureEngine::new(sample_rate, channels)
                .map_err(|_| AbsoluteError::CoreUnavailable)?,
            sharpness: SharpnessContinuousAnalyzer::new(sample_rate, channels)?,
            input_f64: Vec::with_capacity(aperture_samples * channels),
            pending_core: VecDeque::with_capacity(JOIN_CAPACITY),
            output_frames: Vec::with_capacity(2),
            state_epoch_samples: None,
        })
    }

    pub fn aperture_samples(&self) -> usize {
        self.aperture_samples
    }

    pub fn reset_at_epoch(&mut self, epoch: i64) -> Result<(), AbsoluteError> {
        if epoch.rem_euclid(self.aperture_samples as i64) != 0 {
            return Err(AbsoluteError::InvalidEpoch);
        }
        self.core.reset();
        self.sharpness.reset_at_epoch(epoch)?;
        self.input_f64.clear();
        self.pending_core.clear();
        self.output_frames.clear();
        self.state_epoch_samples = Some(epoch);
        Ok(())
    }

    pub fn analyze_aperture(
        &mut self,
        interleaved: &[f32],
        presentation_end_samples: i64,
        generation: u64,
    ) -> Result<&[AbsoluteFrame], AbsoluteError> {
        if interleaved.len() != self.aperture_samples * self.channels {
            return Err(AbsoluteError::WrongApertureLength);
        }
        let epoch = self
            .state_epoch_samples
            .ok_or(AbsoluteError::InvalidEpoch)?;
        if generation == 0
            || presentation_end_samples <= epoch
            || presentation_end_samples.rem_euclid(self.aperture_samples as i64) != 0
        {
            return Err(AbsoluteError::InvalidEpoch);
        }
        self.input_f64.clear();
        self.input_f64
            .extend(interleaved.iter().map(|sample| f64::from(*sample)));
        let core = self
            .core
            .push(&self.input_f64)
            .ok_or(AbsoluteError::CoreUnavailable)?;
        if self.pending_core.len() == JOIN_CAPACITY {
            self.pending_core.pop_front();
        }
        self.pending_core
            .push_back((presentation_end_samples, core));
        let sharp_frames = self.sharpness.analyze_aperture(
            interleaved,
            SpectrumChannelMode::Lr,
            presentation_end_samples,
            generation,
        )?;
        self.output_frames.clear();
        for sharp in sharp_frames {
            let Some(index) = self
                .pending_core
                .iter()
                .position(|(endpoint, _)| *endpoint == sharp.presentation_end_samples)
            else {
                continue;
            };
            let (_, core) = self.pending_core.remove(index).expect("matched core frame");
            self.output_frames.push(AbsoluteFrame {
                schema_version: ABSOLUTE_SCHEMA_VERSION,
                sample_rate: self.sample_rate,
                aperture_samples: self.aperture_samples as u32,
                presentation_end_samples: sharp.presentation_end_samples,
                state_epoch_samples: epoch,
                generation,
                channels: self.channels as u8,
                lufs_m: core.lufs_m,
                true_peak: core.true_peak,
                sharpness: Some(sharp.sharpness),
            });
        }
        while self.pending_core.front().is_some_and(|(endpoint, _)| {
            *endpoint < presentation_end_samples - 8 * self.aperture_samples as i64
        }) {
            self.pending_core.pop_front();
        }
        Ok(&self.output_frames)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn stereo_tone() -> Vec<f32> {
        (0..48_000)
            .flat_map(|index| {
                let sample =
                    (std::f32::consts::TAU * 1_000.0 * index as f32 / 48_000.0).sin() * 0.25;
                [sample, sample]
            })
            .collect()
    }

    #[test]
    fn exact_apertures_publish_one_post_absolute_timeline_without_delta() {
        let samples = stereo_tone();
        let mut analyzer = AbsoluteContinuousAnalyzer::new(48_000, 2).unwrap();
        analyzer.reset_at_epoch(0).unwrap();
        let mut timeline = AbsoluteTimeline::default();
        for (index, aperture) in samples.chunks(9_600).enumerate() {
            assert_eq!(aperture.len(), 9_600);
            for frame in analyzer
                .analyze_aperture(aperture, (index as i64 + 1) * 4_800, 1)
                .unwrap()
            {
                assert!(timeline.push(*frame));
            }
        }
        assert_eq!(timeline.frames().len(), 10);
        let newest = timeline.newest().unwrap();
        assert!(newest.lufs_m.is_some());
        assert!(newest.true_peak.is_some());
        assert!(newest.sharpness.is_some());
    }

    #[test]
    fn timeline_retains_exact_points_across_a_short_forward_gap() {
        let mut timeline = AbsoluteTimeline::default();
        let frame = |endpoint, epoch, generation| AbsoluteFrame {
            schema_version: ABSOLUTE_SCHEMA_VERSION,
            sample_rate: 48_000,
            aperture_samples: 4_800,
            presentation_end_samples: endpoint,
            state_epoch_samples: epoch,
            generation,
            channels: 2,
            lufs_m: Some(-20.0),
            true_peak: Some(-3.0),
            sharpness: Some(1.0),
        };
        assert!(timeline.push(frame(4_800, 0, 1)));
        assert!(!timeline.push(frame(4_800, 0, 1)));
        assert!(timeline.push(frame(14_400, 9_600, 2)));
        let endpoints = timeline
            .frames()
            .map(|frame| frame.presentation_end_samples)
            .collect::<Vec<_>>();
        assert_eq!(endpoints, [4_800, 14_400]);
        assert!(
            !endpoints.contains(&9_600),
            "a missing fact must not be invented"
        );
    }

    #[test]
    fn timeline_starts_a_new_run_on_backwards_transport_or_six_second_gap() {
        let mut timeline = AbsoluteTimeline::default();
        let frame = |endpoint, epoch, generation| AbsoluteFrame {
            schema_version: ABSOLUTE_SCHEMA_VERSION,
            sample_rate: 48_000,
            aperture_samples: 4_800,
            presentation_end_samples: endpoint,
            state_epoch_samples: epoch,
            generation,
            channels: 2,
            lufs_m: Some(-20.0),
            true_peak: Some(-3.0),
            sharpness: Some(1.0),
        };
        assert!(timeline.push(frame(48_000, 0, 1)));
        assert!(timeline.push(frame(52_800, 0, 1)));
        assert!(timeline.push(frame(9_600, 4_800, 2)));
        assert_eq!(timeline.frames().len(), 1);
        assert_eq!(timeline.newest().unwrap().presentation_end_samples, 9_600);

        assert!(timeline.push(frame(297_600, 9_600, 3)));
        assert_eq!(timeline.frames().len(), 1);
        assert_eq!(timeline.newest().unwrap().presentation_end_samples, 297_600);
    }

    #[test]
    fn sparse_source_keeps_exact_time_when_loudness_and_peak_are_below_floor() {
        let tone = stereo_tone();
        let mut samples = tone[..48_000].to_vec(); // 500 ms stereo tone.
        samples.extend(std::iter::repeat_n(0.0, 57_600)); // 600 ms stereo silence.
        let mut analyzer = AbsoluteContinuousAnalyzer::new(48_000, 2).unwrap();
        analyzer.reset_at_epoch(0).unwrap();
        let mut timeline = AbsoluteTimeline::default();
        let interleaved_aperture_samples = 9_600;
        for (index, aperture) in samples
            .chunks_exact(interleaved_aperture_samples)
            .enumerate()
        {
            let frames = analyzer
                .analyze_aperture(aperture, (index as i64 + 1) * 4_800, 1)
                .unwrap();
            assert_eq!(frames.len(), 1);
            assert!(timeline.push(frames[0]));
        }
        assert_eq!(timeline.frames().len(), 11);
        for (index, frame) in timeline.frames().enumerate() {
            assert_eq!(
                frame.presentation_end_samples,
                (index as i64 + 1) * i64::from(frame.aperture_samples)
            );
            assert!(frame.sharpness.is_some_and(f64::is_finite));
        }
        let newest = timeline.newest().unwrap();
        assert_eq!(newest.lufs_m, None);
        assert_eq!(newest.true_peak, None);
    }

    #[test]
    #[ignore = "release-mode two-slot absolute observation performance probe"]
    fn two_post_absolute_workers_fit_the_optional_analysis_budget() {
        use std::hint::black_box;
        use std::time::Instant;

        let samples = stereo_tone();
        let aperture = &samples[..9_600];
        let mut first = AbsoluteContinuousAnalyzer::new(48_000, 2).unwrap();
        let mut second = AbsoluteContinuousAnalyzer::new(48_000, 2).unwrap();
        first.reset_at_epoch(0).unwrap();
        second.reset_at_epoch(0).unwrap();
        let iterations = 40;
        let started = Instant::now();
        for index in 0..iterations {
            let endpoint = (index + 1) as i64 * 4_800;
            black_box(
                first
                    .analyze_aperture(black_box(aperture), endpoint, 1)
                    .unwrap(),
            );
            black_box(
                second
                    .analyze_aperture(black_box(aperture), endpoint, 1)
                    .unwrap(),
            );
        }
        let combined_ms_per_aperture =
            started.elapsed().as_secs_f64() * 1_000.0 / iterations as f64;
        let projected_two_worker_cpu_percent = combined_ms_per_aperture;
        eprintln!(
            "48k two POST absolute workers: {combined_ms_per_aperture:.3} ms/100ms pair, \
             projected CPU {projected_two_worker_cpu_percent:.3}%"
        );
        assert!(projected_two_worker_cpu_percent < 18.0);
    }
}
