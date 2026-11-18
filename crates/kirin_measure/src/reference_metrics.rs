//! Non-RT Reference observations, using the unchanged live measurement engines.
//!
//! Feed one complete 100 ms aperture at a time, from one immutable decoded source.
//! No audio is retained beyond the engine windows. Decode, cancellation, low-priority
//! scheduling and cache ownership belong to the caller, never to the audio callback.
use crate::channel_layout::ChannelLayout;
use crate::engine::MeasureEngine;
use crate::perceptual::SharpnessContinuousAnalyzer;
use crate::spectrum::SpectrumChannelMode;
use std::collections::VecDeque;

#[derive(Clone, Debug, PartialEq)]
pub struct ReferenceMetricFrame {
    pub end_sample: u64,
    pub crest_db: Option<f64>,
    pub psr_db: Option<f64>,
    pub sharpness_acum: Option<f64>,
}

pub struct ReferenceMetrics {
    level: MeasureEngine,
    sharpness: SharpnessContinuousAnalyzer,
    pcm: Vec<f64>,
    aperture_frames: usize,
    channels: usize,
    end_sample: u64,
    pending: VecDeque<ReferenceMetricFrame>,
    failed: bool,
    finished: bool,
}

impl ReferenceMetrics {
    pub fn new(sample_rate: u32, channels: usize) -> Result<Self, String> {
        Self::new_at(sample_rate, channels, 0)
    }

    /// Independent continuous-state scan beginning at an absolute 100 ms boundary.
    /// Caller must warm up and discard earlier observations before comparing a Cue.
    pub fn new_at(sample_rate: u32, channels: usize, start_sample: u64) -> Result<Self, String> {
        if !(8000..=384000).contains(&sample_rate)
            || !sample_rate.is_multiple_of(10)
            || start_sample > i64::MAX as u64
            || !start_sample.is_multiple_of(u64::from(sample_rate / 10))
        {
            return Err("Reference start must be an absolute 100 ms boundary".into());
        }
        let layout = ChannelLayout::mono_or_stereo_by_count(channels)
            .ok_or("Reference metrics require mono or stereo")?;
        let mut sharpness = SharpnessContinuousAnalyzer::new(sample_rate, channels)
            .map_err(|error| error.to_string())?;
        sharpness
            .reset_at_epoch(start_sample as i64)
            .map_err(|error| error.to_string())?;
        let aperture_frames = sharpness.aperture_samples();
        Ok(Self {
            level: MeasureEngine::new(sample_rate, layout)?,
            sharpness,
            pcm: Vec::with_capacity(aperture_frames * channels),
            aperture_frames,
            channels,
            end_sample: start_sample,
            pending: VecDeque::with_capacity(4),
            failed: false,
            finished: false,
        })
    }

    pub fn aperture_frames(&self) -> usize {
        self.aperture_frames
    }

    /// At EOF, release complete level apertures whose SHARP resampler has not yet
    /// produced an observation. Do not pad audio or invent a SHARP tail. Finishing
    /// twice is empty; feeding after finish is refused until a fresh instance exists.
    pub fn finish(&mut self) -> Result<Vec<ReferenceMetricFrame>, String> {
        if self.failed {
            return Err("Reference analysis failed".into());
        }
        self.finished = true;
        Ok(self.pending.drain(..).collect())
    }

    /// Invalid input is rejected before either engine advances. An engine error poisons
    /// this analysis: start a new instance rather than joining two different histories.
    /// Returned endpoints can lag input because the live sharpness resampler has latency.
    /// A final partial aperture is not padded or reported as an observation.
    pub fn push_aperture(&mut self, input: &[f32]) -> Result<Vec<ReferenceMetricFrame>, String> {
        if self.failed || self.finished {
            return Err("Reference analysis must be restarted".into());
        }
        if input.len() != self.aperture_frames * self.channels {
            return Err("Reference metrics require one complete 100 ms aperture".into());
        }
        if input.iter().any(|value| !value.is_finite()) {
            return Err("Reference PCM contains a non-finite sample".into());
        }
        let end = self
            .end_sample
            .checked_add(self.aperture_frames as u64)
            .filter(|end| *end <= i64::MAX as u64)
            .ok_or("Reference timeline overflow")?;
        self.pcm.clear();
        self.pcm
            .extend(input.iter().map(|sample| f64::from(*sample)));
        let Some(level) = self.level.push(&self.pcm) else {
            self.failed = true;
            return Err("Reference level engine did not publish an aperture".into());
        };
        self.end_sample = end;
        self.pending.push_back(ReferenceMetricFrame {
            end_sample: end,
            crest_db: level.crest,
            psr_db: level.psr,
            sharpness_acum: None,
        });
        let sharp =
            match self
                .sharpness
                .analyze_aperture(input, SpectrumChannelMode::Lr, end as i64, 1)
            {
                Ok(frames) => frames,
                Err(error) => {
                    self.failed = true;
                    return Err(error.to_string());
                }
            };
        let mut output = Vec::with_capacity(sharp.len());
        for frame in sharp {
            let Some(mut level) = self.pending.pop_front() else {
                self.failed = true;
                return Err("Reference sharpness has no matching level aperture".into());
            };
            if level.end_sample != frame.presentation_end_samples as u64 {
                self.failed = true;
                return Err("Reference metric endpoints disagree".into());
            }
            level.sharpness_acum = frame.sharpness.is_finite().then_some(frame.sharpness);
            output.push(level);
        }
        if self.pending.len() > 8 {
            self.failed = true;
            return Err("Reference sharpness exceeded its bounded aperture delay".into());
        }
        Ok(output)
    }
}

#[cfg(test)]
#[path = "reference_metrics_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "reference_metrics_warmup_tests.rs"]
mod warmup_tests;
