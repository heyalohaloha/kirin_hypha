//! Streaming Reference tonal balance for the non-real-time display worker.
//!
//! The measurement is intentionally independent from Kirin OS code. It implements the shared
//! public numeric contract: 60 equal-log bands, three retained-origin Hann-window planes, and
//! band power relative to 20 Hz–20 kHz power from the same aperture.

use rustfft::{num_complex::Complex, Fft, FftPlanner};
use std::{array, sync::Arc};

pub const TONAL_BAND_COUNT: usize = 60;
pub const TONAL_PLANE_COUNT: usize = 3;
pub const TONAL_MIN_HZ: f64 = 20.0;
pub const TONAL_MAX_HZ: f64 = 20_000.0;
pub const TONAL_FLOOR_DB: f64 = -120.0;
const SILENCE_GATE_POWER: f64 = 1.0e-10;
const MIN_FFT_SIZE: usize = 256;
const MAX_FFT_SIZE: usize = 1 << 18;

#[derive(Clone, Copy)]
struct PlaneSpec {
    upper_hz: f64,
    window_seconds: f64,
    hop_seconds: f64,
}

const PLANE_SPECS: [PlaneSpec; TONAL_PLANE_COUNT] = [
    PlaneSpec {
        upper_hz: 160.0,
        window_seconds: 2.0,
        hop_seconds: 0.5,
    },
    PlaneSpec {
        upper_hz: 640.0,
        window_seconds: 0.5,
        hop_seconds: 0.2,
    },
    PlaneSpec {
        upper_hz: TONAL_MAX_HZ,
        window_seconds: 0.2,
        hop_seconds: 0.1,
    },
];

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct TonalSnapshot {
    pub sample_rate: u32,
    pub channels: u32,
    pub epoch: u64,
    pub frames_seen: u64,
    pub fft_size: [u32; TONAL_PLANE_COUNT],
    pub hop_samples: [u32; TONAL_PLANE_COUNT],
    pub window_end_samples: [u64; TONAL_PLANE_COUNT],
    pub values_db: [f32; TONAL_BAND_COUNT],
    pub valid_bits: u64,
}

struct Plane {
    index: usize,
    channels: usize,
    fft_size: usize,
    hop: usize,
    ring: Vec<f32>,
    write_frame: usize,
    frames_seen: u64,
    next_window_end: u64,
    window: Vec<f32>,
    fft_buffer: Vec<Complex<f64>>,
    power: Vec<f64>,
    fft: Arc<dyn Fft<f64>>,
    broadband: Option<(usize, usize)>,
    bands: Vec<(usize, Option<(usize, usize)>)>,
}

impl Plane {
    fn new(index: usize, sample_rate: u32, channels: usize) -> Self {
        let spec = PLANE_SPECS[index];
        let fft_size = fft_size_for_window(spec.window_seconds, sample_rate);
        let hop = (spec.hop_seconds * f64::from(sample_rate)).round().max(1.0) as usize;
        let bins = fft_size / 2 + 1;
        let bin_hz = f64::from(sample_rate) / fft_size as f64;
        let mut planner = FftPlanner::<f64>::new();
        let fft = planner.plan_fft_forward(fft_size);
        let bands = band_ranges(index, bin_hz, bins);
        Self {
            index,
            channels,
            fft_size,
            hop,
            ring: vec![0.0; fft_size * channels],
            write_frame: 0,
            frames_seen: 0,
            next_window_end: fft_size as u64,
            window: (0..fft_size)
                .map(|sample| {
                    (0.5 * (1.0 - (std::f64::consts::TAU * sample as f64 / fft_size as f64).cos()))
                        as f32
                })
                .collect(),
            fft_buffer: vec![Complex::new(0.0, 0.0); fft_size],
            power: vec![0.0; bins],
            fft,
            broadband: bin_range(TONAL_MIN_HZ, TONAL_MAX_HZ, bin_hz, bins),
            bands,
        }
    }

    fn reset(&mut self) {
        self.ring.fill(0.0);
        self.write_frame = 0;
        self.frames_seen = 0;
        self.next_window_end = self.fft_size as u64;
    }

    fn push_frame(&mut self, frame: &[f32], output: &mut TonalSnapshot) {
        let base = self.write_frame * self.channels;
        self.ring[base..base + self.channels].copy_from_slice(frame);
        self.write_frame = (self.write_frame + 1) % self.fft_size;
        self.frames_seen = self.frames_seen.saturating_add(1);
        if self.frames_seen == self.next_window_end {
            self.analyze(output);
            self.next_window_end = self.next_window_end.saturating_add(self.hop as u64);
        }
    }

    fn analyze(&mut self, output: &mut TonalSnapshot) {
        for (band, _) in &self.bands {
            output.valid_bits &= !(1_u64 << band);
            output.values_db[*band] = 0.0;
        }
        self.power.fill(0.0);
        let mut time_power = 0.0;
        let mut finite = true;
        for channel in 0..self.channels {
            for offset in 0..self.fft_size {
                let frame = (self.write_frame + offset) % self.fft_size;
                let sample = self.ring[frame * self.channels + channel];
                if !sample.is_finite() {
                    finite = false;
                    break;
                }
                let value = f64::from(sample);
                time_power += value * value;
                self.fft_buffer[offset] = Complex::new(value * f64::from(self.window[offset]), 0.0);
            }
            if !finite {
                break;
            }
            self.fft.process(&mut self.fft_buffer);
            for (power, value) in self.power.iter_mut().zip(&self.fft_buffer) {
                *power += value.norm_sqr();
            }
        }
        output.window_end_samples[self.index] = self.frames_seen;
        let divisor = (self.channels * self.fft_size) as f64;
        if !finite || time_power / divisor <= SILENCE_GATE_POWER {
            return;
        }
        for value in &mut self.power {
            *value /= self.channels as f64;
        }
        let Some((start, end)) = self.broadband else {
            return;
        };
        let broadband: f64 = self.power[start..end].iter().sum();
        if !broadband.is_finite() || broadband <= 0.0 {
            return;
        }
        for (band, range) in &self.bands {
            let Some((start, end)) = range else { continue };
            let power: f64 = self.power[*start..*end].iter().sum();
            if !power.is_finite() || power <= 0.0 {
                continue;
            }
            let value = 10.0 * (power / broadband).log10();
            if value.is_finite() && (TONAL_FLOOR_DB..=0.001).contains(&value) {
                output.values_db[*band] = value as f32;
                output.valid_bits |= 1_u64 << band;
            }
        }
    }
}

pub struct TonalMeter {
    sample_rate: u32,
    channels: usize,
    epoch: u64,
    planes: [Plane; TONAL_PLANE_COUNT],
    snapshot: TonalSnapshot,
}

impl TonalMeter {
    pub fn new(sample_rate: u32, channels: usize) -> Option<Self> {
        if !(40_000..=768_000).contains(&sample_rate) || !(1..=2).contains(&channels) {
            return None;
        }
        let planes = array::from_fn(|index| Plane::new(index, sample_rate, channels));
        let snapshot = TonalSnapshot {
            sample_rate,
            channels: channels as u32,
            epoch: 1,
            frames_seen: 0,
            fft_size: array::from_fn(|index| planes[index].fft_size as u32),
            hop_samples: array::from_fn(|index| planes[index].hop as u32),
            window_end_samples: [0; TONAL_PLANE_COUNT],
            values_db: [0.0; TONAL_BAND_COUNT],
            valid_bits: 0,
        };
        Some(Self {
            sample_rate,
            channels,
            epoch: 1,
            planes,
            snapshot,
        })
    }

    pub fn push(&mut self, samples: &[f32]) -> bool {
        if samples.is_empty()
            || !samples.len().is_multiple_of(self.channels)
            || samples.len() > 16_384
        {
            return false;
        }
        for frame in samples.chunks_exact(self.channels) {
            for plane in &mut self.planes {
                plane.push_frame(frame, &mut self.snapshot);
            }
            self.snapshot.frames_seen = self.snapshot.frames_seen.saturating_add(1);
        }
        true
    }

    pub fn reset(&mut self) {
        self.epoch = self.epoch.saturating_add(1).max(1);
        for plane in &mut self.planes {
            plane.reset();
        }
        self.snapshot.epoch = self.epoch;
        self.snapshot.frames_seen = 0;
        self.snapshot.window_end_samples = [0; TONAL_PLANE_COUNT];
        self.snapshot.values_db = [0.0; TONAL_BAND_COUNT];
        self.snapshot.valid_bits = 0;
    }

    pub fn snapshot(&self) -> TonalSnapshot {
        self.snapshot
    }

    pub fn allocated_bytes(&self) -> usize {
        self.planes
            .iter()
            .map(|plane| {
                plane.ring.capacity() * std::mem::size_of::<f32>()
                    + plane.window.capacity() * std::mem::size_of::<f32>()
                    + plane.fft_buffer.capacity() * std::mem::size_of::<Complex<f64>>()
                    + plane.power.capacity() * std::mem::size_of::<f64>()
                    + plane.bands.capacity()
                        * std::mem::size_of::<(usize, Option<(usize, usize)>)>()
            })
            .sum()
    }

    pub fn configuration(&self) -> (u32, usize) {
        (self.sample_rate, self.channels)
    }
}

fn fft_size_for_window(seconds: f64, sample_rate: u32) -> usize {
    let needed = (seconds * f64::from(sample_rate)).round().max(1.0) as usize;
    let mut size = MIN_FFT_SIZE;
    while size * 2 <= needed && size * 2 <= MAX_FFT_SIZE {
        size *= 2;
    }
    size
}

fn band_ranges(
    plane_index: usize,
    bin_hz: f64,
    bins: usize,
) -> Vec<(usize, Option<(usize, usize)>)> {
    let ratio = (TONAL_MAX_HZ / TONAL_MIN_HZ).powf(1.0 / TONAL_BAND_COUNT as f64);
    (0..TONAL_BAND_COUNT)
        .filter_map(|index| {
            let lower = TONAL_MIN_HZ * ratio.powf(index as f64);
            let upper = if index + 1 == TONAL_BAND_COUNT {
                TONAL_MAX_HZ
            } else {
                TONAL_MIN_HZ * ratio.powf((index + 1) as f64)
            };
            let center = (lower * upper).sqrt();
            let owner = PLANE_SPECS
                .iter()
                .position(|spec| center < spec.upper_hz || spec.upper_hz == TONAL_MAX_HZ)
                .unwrap_or(TONAL_PLANE_COUNT - 1);
            (owner == plane_index).then(|| (index, bin_range(lower, upper, bin_hz, bins)))
        })
        .collect()
}

fn bin_range(lower_hz: f64, upper_hz: f64, bin_hz: f64, bins: usize) -> Option<(usize, usize)> {
    let start = (lower_hz / bin_hz).ceil().max(1.0) as usize;
    let end = (upper_hz / bin_hz).ceil() as usize;
    let start = start.min(bins);
    let end = end.min(bins);
    (start < end).then_some((start, end))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn retained_grid_and_tone_are_measured_without_channel_cancellation() {
        let mut meter = TonalMeter::new(48_000, 2).unwrap();
        assert_eq!(meter.snapshot().fft_size, [65_536, 16_384, 8_192]);
        assert_eq!(meter.snapshot().hop_samples, [24_000, 9_600, 4_800]);
        for start in (0..96_000).step_by(256) {
            let frames = (96_000 - start).min(256);
            let mut pcm = Vec::with_capacity(frames * 2);
            for index in start..start + frames {
                let value =
                    (std::f64::consts::TAU * 1_000.0 * index as f64 / 48_000.0).sin() as f32 * 0.2;
                pcm.extend([value, -value]);
            }
            assert!(meter.push(&pcm));
        }
        let snapshot = meter.snapshot();
        assert_eq!(snapshot.window_end_samples, [89_536, 93_184, 94_592]);
        assert!(snapshot.valid_bits.count_ones() >= 2);
        assert!(snapshot.values_db.iter().any(|value| *value > -0.1));
        assert!(meter.allocated_bytes() < 32 * 1024 * 1024);
    }

    #[test]
    fn silence_nonfinite_and_reset_do_not_publish_invented_bands() {
        let mut meter = TonalMeter::new(48_000, 1).unwrap();
        for _ in 0..256 {
            assert!(meter.push(&[0.0; 256]));
        }
        assert_eq!(meter.snapshot().valid_bits, 0);
        meter.reset();
        assert_eq!(meter.snapshot().epoch, 2);
        assert_eq!(meter.snapshot().frames_seen, 0);
        assert!(!meter.push(&[]));
        assert!(!meter.push(&[0.0, f32::NAN].repeat(9_000)));
        assert!(TonalMeter::new(8_000, 2).is_none());
        assert!(TonalMeter::new(48_000, 3).is_none());
    }
}
