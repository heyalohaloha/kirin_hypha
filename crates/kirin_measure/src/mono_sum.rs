//! MONO — how much of each band survives the mono sum.
//!
//! One exact 100 ms observation in, one value per third-octave band out. Mid and Side are built in
//! the time domain before the FFT, so a phase cancellation is already in `|M|` and the number is
//! what the band actually loses, not a width approximation derived from magnitudes.
//!
//! ```text
//! M[n] = (L[n] + R[n]) / 2
//! S[n] = (L[n] - R[n]) / 2
//! mono(b) = 10 * log10( Pm(b) / (Pm(b) + Ps(b)) )
//! ```
//!
//! Identical channels read 0 dB, one channel alone reads -3.01 dB, and an inverted pair falls to
//! the computed floor. This module owns no thread, file, or UI state, and never touches the audio
//! it is given.

use rustfft::num_complex::Complex32;
use rustfft::{Fft, FftPlanner};
use std::sync::Arc;

use crate::log_bands::log_band_edges;

/// Third-octave over the audible range: 11.10 octaves across 32 bands is 0.347 octave each.
pub const MONO_SUM_BAND_COUNT: usize = 32;
pub const MONO_SUM_MIN_HZ: f32 = 10.0;
pub const MONO_SUM_MAX_HZ: f32 = 22_000.0;
/// Below this the band has nothing to measure and the value is undefined, never 0 dB.
pub const MONO_SUM_LEVEL_FLOOR_DBFS: f32 = -120.0;
/// A band that keeps a millionth of its energy is gone. Distinguishing -60 from -90 has no use,
/// and the ratio is a float divide that would otherwise reach negative infinity.
pub const MONO_SUM_FLOOR_DB: f32 = -60.0;
/// Below this frequency the observation holds fewer than three cycles, so the readout is marked
/// approximate. Same rule and the same three cycles as the Spectrum's own boundary.
pub const MONO_SUM_APPROXIMATE_CYCLES: f32 = 3.0;

/// One band's value, or `None` when the band has no measurable content.
pub type MonoSumBands = [Option<f32>; MONO_SUM_BAND_COUNT];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum BandBins {
    /// The band covers at least one whole bin.
    Range { first: usize, last: usize },
    /// The band is narrower than a bin, so it takes the one bin nearest its centre. Both Mid and
    /// Side read the same bin, so the ratio stays exact even where the resolution does not.
    Nearest { bin: usize },
}

/// Keeps the FFT plan, the window and the band layout for one observation length.
pub struct MonoSumAnalyzer {
    sample_rate: u32,
    frames: usize,
    fft: Arc<dyn Fft<f32>>,
    fft_size: usize,
    window: Vec<f32>,
    amplitude_scale: f32,
    bands: [BandBins; MONO_SUM_BAND_COUNT],
    mid: Vec<Complex32>,
    side: Vec<Complex32>,
    scratch: Vec<Complex32>,
}

impl std::fmt::Debug for MonoSumAnalyzer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("MonoSumAnalyzer")
            .field("sample_rate", &self.sample_rate)
            .field("frames", &self.frames)
            .field("fft_size", &self.fft_size)
            .finish()
    }
}

impl MonoSumAnalyzer {
    /// `frames` is the observation length in frames. Returns `None` for a layout that cannot carry
    /// the band split rather than reporting values the resolution does not support.
    pub fn new(sample_rate: u32, frames: usize) -> Option<Self> {
        if !(8_000..=384_000).contains(&sample_rate) || frames < 64 {
            return None;
        }
        let fft_size = frames.checked_next_power_of_two()?;
        let max_bin = fft_size / 2 - 1;
        let bin_hz = sample_rate as f32 / fft_size as f32;
        let highest_hz = MONO_SUM_MAX_HZ.min(sample_rate as f32 * 0.5 - bin_hz);
        let lowest_hz = MONO_SUM_MIN_HZ.max(bin_hz);
        if !highest_hz.is_finite() || !lowest_hz.is_finite() || highest_hz <= lowest_hz {
            return None;
        }

        let mut planner = FftPlanner::<f32>::new();
        let fft = planner.plan_fft_forward(fft_size);
        // Hann, the same window the Spectrum uses.
        //
        // A four-term Blackman-Harris was tried first, on the reasoning that a ratio between two
        // spectra wants the lowest possible sidelobes. Measured, it was worse: what limits this
        // measurement is the band next door, and there the main lobe decides, not the sidelobes.
        // Blackman-Harris doubles the main lobe width. With a loud cancellation 40 dB above its
        // neighbour, Hann moves that neighbour 4.2 dB and Blackman-Harris moves it 9.4 dB.
        let window = (0..frames)
            .map(|index| {
                let phase = std::f32::consts::TAU * index as f32 / frames.saturating_sub(1) as f32;
                0.5 - 0.5 * phase.cos()
            })
            .collect::<Vec<_>>();
        let amplitude_scale = 2.0 / window.iter().sum::<f32>();
        let bands = std::array::from_fn(|index| band_bins(index, bin_hz, max_bin));
        let scratch_len = fft.get_inplace_scratch_len();
        Some(Self {
            sample_rate,
            frames,
            fft,
            fft_size,
            window,
            amplitude_scale,
            bands,
            mid: vec![Complex32::default(); fft_size],
            side: vec![Complex32::default(); fft_size],
            scratch: vec![Complex32::default(); scratch_len],
        })
    }

    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    pub fn frames(&self) -> usize {
        self.frames
    }

    /// Frequencies below this hold fewer than three cycles in one observation.
    pub fn approximate_below_hz(&self) -> f32 {
        MONO_SUM_APPROXIMATE_CYCLES * self.sample_rate as f32 / self.frames as f32
    }

    /// `interleaved` is one exact observation of stereo frames. Returns `None` when the length
    /// does not match the layout, so a short or malformed observation is skipped rather than
    /// zero-padded into a value that was never measured.
    pub fn analyze(&mut self, interleaved: &[f64]) -> Option<MonoSumBands> {
        if interleaved.len() != self.frames * 2 {
            return None;
        }

        for (index, frame) in interleaved.as_chunks::<2>().0.iter().enumerate() {
            let weight = self.window[index];
            let left = frame[0] as f32;
            let right = frame[1] as f32;
            self.mid[index] = Complex32::new((left + right) * 0.5 * weight, 0.0);
            self.side[index] = Complex32::new((left - right) * 0.5 * weight, 0.0);
        }
        for index in self.frames..self.fft_size {
            self.mid[index] = Complex32::default();
            self.side[index] = Complex32::default();
        }
        self.fft
            .process_with_scratch(&mut self.mid, &mut self.scratch);
        self.fft
            .process_with_scratch(&mut self.side, &mut self.scratch);

        let scale = self.amplitude_scale * self.amplitude_scale;
        let level_floor_power = 10.0_f32.powf(MONO_SUM_LEVEL_FLOOR_DBFS / 10.0);
        let ratio_floor = 10.0_f32.powf(MONO_SUM_FLOOR_DB / 10.0);
        let power = |bin: &Complex32| bin.norm_sqr();

        Some(std::array::from_fn(|index| {
            let (mid_power, side_power) = match self.bands[index] {
                BandBins::Range { first, last } => (
                    self.mid[first..=last].iter().map(power).sum::<f32>(),
                    self.side[first..=last].iter().map(power).sum::<f32>(),
                ),
                BandBins::Nearest { bin } => (power(&self.mid[bin]), power(&self.side[bin])),
            };
            let total = (mid_power + side_power) * scale;
            if !total.is_finite() || total <= level_floor_power {
                return None;
            }
            let surviving = mid_power / (mid_power + side_power);
            if !surviving.is_finite() {
                return None;
            }
            Some(10.0 * surviving.max(ratio_floor).log10())
        }))
    }
}

fn band_bins(index: usize, bin_hz: f32, max_bin: usize) -> BandBins {
    let (low, high) = log_band_edges(index, MONO_SUM_BAND_COUNT, MONO_SUM_MIN_HZ, MONO_SUM_MAX_HZ);
    let first = (low / bin_hz).ceil().max(1.0) as usize;
    let last = (high / bin_hz).floor().min(max_bin as f32) as usize;
    if last >= first {
        return BandBins::Range { first, last };
    }
    let centre = (low * high).sqrt() / bin_hz;
    BandBins::Nearest {
        bin: (centre.round() as usize).clamp(1, max_bin),
    }
}

#[cfg(test)]
#[path = "mono_sum_tests.rs"]
mod tests;
