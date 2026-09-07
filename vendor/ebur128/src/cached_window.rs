// Hypha local extension to MIT-licensed ebur128 0.1.10. See ../HYPHA_FORK.md.
// Cache raw sums of squares in bounded tiles, never running-total subtraction.
// This preserves quiet tails/zero windows after loud material without cancellation drift.
use super::{Channel, EbuR128, Error};
use crate::utils::energy_to_loudness;

const TILE_FRAMES: usize = 128;

pub(super) struct EnergyCache {
    stride: usize,
    tiles_per_channel: usize,
    sums: Box<[f64]>,
}

impl EnergyCache {
    pub(super) fn new(audio: &[f64], channels: u32) -> Self {
        let stride = audio.len() / channels as usize;
        let tiles_per_channel = (stride + TILE_FRAMES - 1) / TILE_FRAMES;
        let mut cache = Self {
            stride,
            tiles_per_channel,
            sums: vec![0.0; tiles_per_channel * channels as usize].into_boxed_slice(),
        };
        cache.refresh(audio, 0, stride);
        cache
    }

    pub(super) fn clear(&mut self) {
        self.sums.fill(0.0);
    }

    pub(super) fn refresh(&mut self, audio: &[f64], start: usize, frames: usize) {
        if frames == 0 {
            return;
        }
        assert!(start + frames <= self.stride);
        let first = start / TILE_FRAMES;
        let last = (start + frames - 1) / TILE_FRAMES;
        for (channel, audio) in audio.chunks_exact(self.stride).enumerate() {
            for tile in first..=last {
                let begin = tile * TILE_FRAMES;
                let end = (begin + TILE_FRAMES).min(self.stride);
                self.sums[channel * self.tiles_per_channel + tile] = squares(&audio[begin..end]);
            }
        }
    }

    fn range(&self, channel: usize, audio: &[f64], start: usize, end: usize) -> f64 {
        let first_full = ((start + TILE_FRAMES - 1) / TILE_FRAMES) * TILE_FRAMES;
        if first_full >= end {
            return squares(&audio[start..end]);
        }
        let last_full = end / TILE_FRAMES * TILE_FRAMES;
        let mut sum = squares(&audio[start..first_full]);
        for tile in first_full / TILE_FRAMES..last_full / TILE_FRAMES {
            sum += self.sums[channel * self.tiles_per_channel + tile];
        }
        sum + squares(&audio[last_full..end])
    }

    fn energy(&self, frames: usize, audio: &[f64], index: usize, map: &[Channel]) -> f64 {
        let mut total = 0.0;
        for (channel, data) in audio.chunks_exact(self.stride).enumerate() {
            let weight = match map[channel] {
                Channel::Unused => continue,
                Channel::LeftSurround
                | Channel::RightSurround
                | Channel::Mp060
                | Channel::Mm060
                | Channel::Mp090
                | Channel::Mm090 => 1.41,
                Channel::DualMono => 2.0,
                _ => 1.0,
            };
            let sum = if index < frames {
                self.range(channel, data, 0, index)
                    + self.range(channel, data, self.stride - frames + index, self.stride)
            } else {
                self.range(channel, data, index - frames, index)
            };
            total += sum * weight;
        }
        total / frames as f64
    }
}

fn squares(samples: &[f64]) -> f64 {
    samples
        .iter()
        .fold(0.0, |sum, sample| sum + sample * sample)
}

impl EbuR128 {
    /// Enable bounded tiled M/S queries. Call on the measurement/control thread, not audio RT.
    /// Canonical scalar APIs, filters, I/LRA history and true peak remain unchanged.
    pub fn enable_cached_window_queries(&mut self) {
        if self.energy_cache.is_none() {
            self.energy_cache = Some(EnergyCache::new(&self.audio_data, self.channels));
        }
    }

    pub(super) fn rebuild_energy_cache(&mut self, channels: u32) {
        if self.energy_cache.is_some() {
            self.energy_cache = Some(EnergyCache::new(&self.audio_data, channels));
        }
    }

    fn cached_loudness(&self, frames: usize) -> Result<f64, Error> {
        if frames > self.audio_data.len() / self.channels as usize {
            return Err(Error::InvalidMode);
        }
        let energy = self.energy_cache.as_ref().map_or_else(
            || self.energy_in_interval(frames),
            |cache| {
                Ok(cache.energy(
                    frames,
                    &self.audio_data,
                    self.audio_data_index,
                    &self.channel_map,
                ))
            },
        )?;
        Ok(if energy <= 0.0 {
            -f64::INFINITY
        } else {
            energy_to_loudness(energy)
        })
    }

    /// Last 400 ms using the optional cache, with the same window/cadence as the scalar API.
    pub fn loudness_momentary_cached(&self) -> Result<f64, Error> {
        self.cached_loudness(self.samples_in_100ms * 4)
    }

    /// Last 3 s using the optional cache, with the same window/cadence as the scalar API.
    pub fn loudness_shortterm_cached(&self) -> Result<f64, Error> {
        self.cached_loudness(self.samples_in_100ms * 30)
    }
}

#[cfg(test)]
#[path = "cached_window_tests.rs"]
mod tests;
