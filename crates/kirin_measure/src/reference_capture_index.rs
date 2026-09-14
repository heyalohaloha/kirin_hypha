//! One-second, worker-only observations. Never used to alter audio or canonical meters.
use sha2::{Digest, Sha256};

pub const BAND_UNAVAILABLE: i16 = i16::MIN;
pub const BAND_SILENT: i16 = i16::MIN + 1;
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CaptureUnit {
    pub digest: [u8; 32],
    pub bands: [i16; 8],
    pub rms: [f32; 2],
    pub peak: [f32; 2],
}
const _: () = assert!(std::mem::size_of::<CaptureUnit>() == 64);

pub struct CaptureIndex {
    hash: Sha256,
    rate: u32,
    channels: usize,
    target: u32,
    frames: u32,
    valid: bool,
    alpha: [f64; 3],
    filter: [[f64; 3]; 2],
    bands: [[f64; 4]; 2],
    energy: [f64; 2],
    peak: [f64; 2],
}
impl CaptureIndex {
    pub fn new(rate: u32, channels: usize, frames: u32) -> Option<Self> {
        if !(8_000..=768_000).contains(&rate)
            || !(1..=2).contains(&channels)
            || frames == 0
            || frames > rate
        {
            return None;
        }
        let mut hash = Sha256::new();
        hash.update(b"Hypha Capture Unit 1\0");
        hash.update(rate.to_le_bytes());
        hash.update((channels as u32).to_le_bytes());
        // Frame count is appended at finish so a short final unit needs no PCM buffer.
        Some(Self {
            hash,
            rate,
            channels,
            target: frames,
            frames: 0,
            valid: true,
            alpha: [120.0, 1000.0, 6000.0_f64.min(0.375 * rate as f64)]
                .map(|f| 1.0 - (-2.0 * std::f64::consts::PI * f / rate as f64).exp()),
            filter: [[0.0; 3]; 2],
            bands: [[0.0; 4]; 2],
            energy: [0.0; 2],
            peak: [0.0; 2],
        })
    }
    pub fn push(&mut self, samples: &[f32]) -> bool {
        if !self.valid
            || samples.len() % self.channels != 0
            || samples.len() / self.channels > (self.target - self.frames) as usize
        {
            self.valid = false;
            return false;
        }
        // Batch canonical bytes: hashing every float separately is unnecessarily expensive.
        let mut bytes = [0_u8; 1024];
        for part in samples.chunks(256) {
            for (i, &value) in part.iter().enumerate() {
                if !value.is_finite() {
                    self.valid = false;
                    return false;
                }
                let normalized = if value == 0.0 { 0.0_f32 } else { value };
                bytes[i * 4..i * 4 + 4].copy_from_slice(&normalized.to_bits().to_le_bytes());
            }
            self.hash.update(&bytes[..part.len() * 4]);
        }
        for frame in samples.chunks_exact(self.channels) {
            for (c, &sample) in frame.iter().enumerate() {
                let v = sample as f64;
                self.energy[c] += v * v;
                self.peak[c] = self.peak[c].max(v.abs());
                for k in 0..3 {
                    self.filter[c][k] += self.alpha[k] * (v - self.filter[c][k]);
                }
                let f = self.filter[c];
                let values = [f[0], f[1] - f[0], f[2] - f[1], v - f[2]];
                for (k, value) in values.into_iter().enumerate() {
                    self.bands[c][k] += value * value;
                }
            }
        }
        self.frames += (samples.len() / self.channels) as u32;
        true
    }
    pub fn frames(&self) -> u32 {
        self.frames
    }
    pub fn finish(&self) -> Option<CaptureUnit> {
        if !self.valid || self.frames == 0 {
            return None;
        }
        let mut hash = self.hash.clone();
        hash.update(self.frames.to_le_bytes());
        let mut result = CaptureUnit {
            digest: hash.finalize().into(),
            bands: [BAND_UNAVAILABLE; 8],
            rms: [0.0; 2],
            peak: [0.0; 2],
        };
        for c in 0..self.channels {
            result.rms[c] = (self.energy[c] / self.frames as f64).sqrt() as f32;
            result.peak[c] = self.peak[c] as f32;
            if !result.rms[c].is_finite() || !result.peak[c].is_finite() {
                return None;
            }
            for k in 0..4 {
                let energy = self.bands[c][k] / self.frames as f64;
                let db = 1000.0 * energy.log10();
                result.bands[c * 4 + k] = if energy == 0.0 {
                    BAND_SILENT
                } else if db.is_finite() && db >= -32766.0 && db <= 32767.0 {
                    db.round() as i16
                } else {
                    BAND_UNAVAILABLE
                };
            }
        }
        Some(result)
    }
    pub fn reset(&mut self) {
        *self = Self::new(self.rate, self.channels, self.target).unwrap();
    }
}
fn level(v: f32) -> f64 {
    if v > 0.0 {
        20.0 * (v as f64).log10()
    } else {
        f64::NEG_INFINITY
    }
}
fn changed(a: f64, b: f64, floor: f64, threshold: f64) -> bool {
    (a >= floor && b >= floor && (a - b).abs() + 1e-6 >= threshold)
        || (a < floor && b >= floor + 0.5)
        || (b < floor && a >= floor + 0.5)
}
/// 0: no material notification; 1: exact input digest; 2: material difference.
/// A 0 result is never evidence of exact identity.
pub fn compare(a: &CaptureUnit, b: &CaptureUnit, channels: usize) -> u32 {
    if !(1..=2).contains(&channels) {
        return 0;
    }
    if a.digest == b.digest {
        return 1;
    }
    for c in 0..channels {
        if changed(level(a.rms[c]), level(b.rms[c]), -70.0, 0.1) {
            return 2;
        }
        let (ap, bp) = (level(a.peak[c]), level(b.peak[c]));
        if ap >= -60.0 && bp >= -60.0 && (ap - bp).abs() + 1e-6 >= 0.2 {
            return 2;
        }
        for k in 0..4 {
            let (a, b) = (a.bands[c * 4 + k], b.bands[c * 4 + k]);
            if a == BAND_UNAVAILABLE || b == BAND_UNAVAILABLE {
                continue;
            }
            let db = |v| {
                if v == BAND_SILENT {
                    f64::NEG_INFINITY
                } else {
                    v as f64 / 100.0
                }
            };
            if changed(db(a), db(b), -70.0, 0.1) {
                return 2;
            }
        }
    }
    0
}
#[cfg(test)]
#[path = "reference_capture_index_tests.rs"]
mod tests;
