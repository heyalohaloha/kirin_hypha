//! Continuous DIN 45692 Sharpness frames for per-hit ATTACK windows.
//!
//! One ISO 532-1 Phase D stream per channel runs continuously from the run start. Every 2 kHz frame
//! keeps its Sharpness and filtered loudness, so a hit window reads the loudness-weighted Sharpness
//! of the 100 ms after its onset instead of one instant at a fixed-grid aperture end.
//!
//! Phase D output depends slightly on where its input is split, so input is fed in 100 ms chunks
//! on the absolute content grid (as the SHARP timeline is). Host block sizes never change a value,
//! and PRE and POST split one content position identically.

use crate::phase_d::sharpness;
use crate::phase_d::stream::PhaseDStream;
use crate::phase_d::tables::FieldType;
use crate::resampler::ResamplerTo48k;

const PHASE_D_RATE: i64 = 48_000;
/// Phase D decimates 48 kHz to 2 kHz: one frame at every 24th 48 kHz sample from the reset.
const PHASE_D_FRAME_SAMPLES: i64 = 24;
/// The DIN 45692 implementation reports 0 acum below 0.1 sone; such frames carry no Sharpness.
pub(super) const SHARPNESS_MIN_LOUDNESS_SONE: f64 = 0.1;

#[derive(Clone, Copy, Debug, PartialEq)]
pub(super) struct SharpnessFrame {
    /// Source-rate content sample of the frame. Non-48 kHz input maps through the resampler,
    /// whose initial group delay is already trimmed.
    pub(super) source_sample: i64,
    pub(super) sharpness: [f64; 2],
    pub(super) loudness: [f64; 2],
}

pub(super) struct AttackSharpnessStream {
    sample_rate: i64,
    channels: usize,
    chunk: i64,
    streams: Vec<PhaseDStream>,
    resampler: Option<ResamplerTo48k>,
    pending: Vec<f64>,
    resampled: Vec<f64>,
    channel: Vec<f64>,
    epoch: i64,
    frames: i64,
}

impl AttackSharpnessStream {
    /// `None` for rates the Phase D model and its 48 kHz resampler do not cover.
    pub(super) fn new(sample_rate: u32, channels: usize) -> Option<Self> {
        if !(8_000..=384_000).contains(&sample_rate) || !matches!(channels, 1 | 2) {
            return None;
        }
        let resampler = if sample_rate == PHASE_D_RATE as u32 {
            None
        } else {
            Some(ResamplerTo48k::new(sample_rate, channels).ok()?)
        };
        Some(Self {
            sample_rate: i64::from(sample_rate),
            channels,
            chunk: i64::from(sample_rate / 10),
            streams: (0..channels)
                .map(|_| PhaseDStream::new(FieldType::Free))
                .collect(),
            resampler,
            pending: Vec::new(),
            resampled: Vec::new(),
            channel: Vec::new(),
            epoch: 0,
            frames: 0,
        })
    }

    /// Restart every stateful stage for a continuous run from `start`. Measurement begins at the
    /// first 100 ms content-grid boundary at or after it.
    pub(super) fn reset(&mut self, start: i64) {
        for stream in &mut self.streams {
            stream.reset();
        }
        if let Some(resampler) = self.resampler.as_mut() {
            resampler.reset();
        }
        self.pending.clear();
        self.resampled.clear();
        self.epoch = start + (self.chunk - start.rem_euclid(self.chunk)) % self.chunk;
        self.frames = 0;
    }

    /// First content sample with Phase D frames in this run.
    pub(super) fn epoch(&self) -> i64 {
        self.epoch
    }

    /// Source sample of the next frame: every bin before it has all of its frames.
    pub(super) fn next_frame_sample(&self) -> i64 {
        self.frame_sample(self.frames)
    }

    fn frame_sample(&self, frame: i64) -> i64 {
        let numerator = frame * PHASE_D_FRAME_SAMPLES * self.sample_rate + PHASE_D_RATE / 2;
        self.epoch + numerator.div_euclid(PHASE_D_RATE)
    }

    /// Feed the interleaved samples of a continuous block starting at `start`, and append the
    /// frames of every 100 ms chunk it completes.
    pub(super) fn push(
        &mut self,
        start: i64,
        interleaved: &[f32],
        out: &mut Vec<SharpnessFrame>,
    ) -> bool {
        let skip = usize::try_from(self.epoch - start).unwrap_or(0) * self.channels;
        self.pending.extend(
            interleaved
                .iter()
                .skip(skip)
                .map(|sample| f64::from(*sample)),
        );
        let chunk = self.chunk as usize * self.channels;
        while self.pending.len() >= chunk {
            if !self.push_chunk(chunk, out) {
                return false;
            }
            self.pending.drain(..chunk);
        }
        true
    }

    fn push_chunk(&mut self, chunk: usize, out: &mut Vec<SharpnessFrame>) -> bool {
        match self.resampler.as_mut() {
            Some(resampler) => {
                if resampler
                    .process(&self.pending[..chunk], &mut self.resampled)
                    .is_err()
                {
                    return false;
                }
            }
            None => self.resampled.extend_from_slice(&self.pending[..chunk]),
        }
        let frames = self.resampled.len() / self.channels;
        if frames == 0 {
            return true;
        }
        let first = out.len();
        for channel in 0..self.channels {
            self.channel.clear();
            self.channel.extend(
                self.resampled[..frames * self.channels]
                    .iter()
                    .skip(channel)
                    .step_by(self.channels),
            );
            let Some((slopes, loudness)) = self.streams[channel].push_iso_core(&self.channel)
            else {
                continue;
            };
            let values = sharpness::compute(&slopes.n_specific[..loudness.len()], &loudness);
            for (index, (sharp, loud)) in values.into_iter().zip(loudness).enumerate() {
                if channel == 0 {
                    let source_sample = self.frame_sample(self.frames + index as i64);
                    out.push(SharpnessFrame {
                        source_sample,
                        sharpness: [0.0; 2],
                        loudness: [0.0; 2],
                    });
                }
                if let Some(frame) = out.get_mut(first + index) {
                    frame.sharpness[channel] = sharp;
                    frame.loudness[channel] = loud;
                }
            }
        }
        self.frames += (out.len() - first) as i64;
        self.resampled.drain(..frames * self.channels);
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frames_follow_the_source_grid_at_48k_and_resampled_rates() {
        let mut stream = AttackSharpnessStream::new(48_000, 1).unwrap();
        stream.reset(1_000);
        let mut frames = Vec::new();
        assert!(stream.push(1_000, &[0.1; 8_599], &mut frames));
        assert!(
            frames.is_empty(),
            "the first chunk starts at 4800 and ends at 9600"
        );
        assert!(stream.push(9_599, &[0.1; 1], &mut frames));
        assert_eq!(frames.len(), 200);
        assert_eq!(frames[0].source_sample, 4_800);
        assert_eq!(frames[199].source_sample, 4_800 + 199 * 24);
        assert_eq!(stream.next_frame_sample(), 9_600);

        let mut resampled = AttackSharpnessStream::new(96_000, 2).unwrap();
        resampled.reset(0);
        let mut frames = Vec::new();
        assert!(resampled.push(0, &vec![0.1; 96_000 * 2 / 2], &mut frames));
        assert!(!frames.is_empty());
        assert!(frames
            .windows(2)
            .all(|pair| pair[1].source_sample - pair[0].source_sample == 48));
    }

    #[test]
    fn unsupported_rates_have_no_stream() {
        assert!(AttackSharpnessStream::new(1_000, 1).is_none());
        assert!(AttackSharpnessStream::new(48_000, 3).is_none());
    }
}
