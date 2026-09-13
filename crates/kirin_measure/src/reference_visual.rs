//! Bounded, worker-only Reference display measurements. Never a Record authority.
use crate::analysis_lease::AnalysisLease;
use ebur128::{EbuR128, Mode};

pub struct VisualAdmission(AnalysisLease);
impl Default for VisualAdmission {
    fn default() -> Self {
        #[cfg(not(test))]
        let lease = AnalysisLease::for_current_process();
        #[cfg(test)]
        let lease = AnalysisLease::at_path(
            std::env::temp_dir().join(format!("hypha-visual-{}.lease", uuid::Uuid::new_v4())),
        );
        Self(lease)
    }
}
impl VisualAdmission {
    pub fn acquire(&mut self) -> bool {
        self.0.try_acquire_for("Reference view").unwrap_or(false)
    }
    pub fn release(&mut self) {
        self.0.release();
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct VisualBin {
    pub frames: u64,
    pub peak: [f64; 2],
    pub rms: [f64; 2],
    pub short_lufs: f64,
    pub crest_db: f64,
}

pub struct VisualMeter {
    meter: EbuR128,
    rate: u32,
    channels: usize,
    total: u64,
    frames: u64,
    peak: [f64; 2],
    energy: [f64; 2],
    tp: f64,
}
impl VisualMeter {
    pub fn new(rate: u32, channels: usize) -> Option<Self> {
        if !(8_000..=768_000).contains(&rate) || !(1..=2).contains(&channels) {
            return None;
        }
        Some(Self {
            meter: EbuR128::new(channels as u32, rate, Mode::S | Mode::TRUE_PEAK).ok()?,
            rate,
            channels,
            total: 0,
            frames: 0,
            peak: [0.0; 2],
            energy: [0.0; 2],
            tp: 0.0,
        })
    }
    pub fn push(&mut self, samples: &[f32]) -> bool {
        if samples.is_empty()
            || !samples.len().is_multiple_of(self.channels)
            || samples.len() > 16384
            || samples.iter().any(|v| !v.is_finite())
        {
            return false;
        }
        if self.meter.add_frames_f32(samples).is_err() {
            return false;
        }
        for frame in samples.chunks_exact(self.channels) {
            for (c, value) in frame.iter().enumerate() {
                let v = f64::from(*value);
                self.peak[c] = self.peak[c].max(v.abs());
                self.energy[c] += v * v;
            }
        }
        for c in 0..self.channels {
            self.tp = self
                .tp
                .max(self.meter.prev_true_peak(c as u32).unwrap_or(0.0));
        }
        let frames = (samples.len() / self.channels) as u64;
        self.total += frames;
        self.frames += frames;
        true
    }
    pub fn finish_bin(&mut self) -> Option<VisualBin> {
        if self.frames == 0 {
            return None;
        }
        let rms = std::array::from_fn(|c| (self.energy[c] / self.frames as f64).sqrt());
        let combined =
            (self.energy.iter().sum::<f64>() / (self.frames as f64 * self.channels as f64)).sqrt();
        let bin = VisualBin {
            frames: self.frames,
            peak: self.peak,
            rms,
            short_lufs: if self.total >= u64::from(self.rate) * 3 {
                self.meter.loudness_shortterm().unwrap_or(f64::NAN)
            } else {
                f64::NAN
            },
            crest_db: if combined > 1e-15 && self.tp > 0.0 {
                20.0 * (self.tp / combined).log10()
            } else {
                f64::NAN
            },
        };
        self.frames = 0;
        self.peak = [0.0; 2];
        self.energy = [0.0; 2];
        self.tp = 0.0;
        Some(bin)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn window_gain_silence_and_invalid_input() {
        let mut a = VisualMeter::new(48000, 2).unwrap();
        let mut b = VisualMeter::new(48000, 2).unwrap();
        for bin in 0..31 {
            let pcm: Vec<f32> = (0..4800)
                .flat_map(|i| {
                    let v = (2.0 * std::f64::consts::PI * 1000.0 * i as f64 / 48000.0).sin() as f32
                        * 0.1;
                    [v, -v]
                })
                .collect();
            assert!(a.push(&pcm));
            assert!(b.push(&pcm.iter().map(|v| v * 0.5).collect::<Vec<_>>()));
            let av = a.finish_bin().unwrap();
            let bv = b.finish_bin().unwrap();
            assert_eq!(av.frames, 4800);
            assert!((av.rms[0] - 0.1 / 2.0_f64.sqrt()).abs() < 1e-7);
            assert!((av.peak[0] - 2.0 * bv.peak[0]).abs() < 1e-7);
            assert!((av.crest_db - bv.crest_db).abs() < 0.001);
            if bin < 29 {
                assert!(av.short_lufs.is_nan());
            } else {
                assert!((av.short_lufs - bv.short_lufs - 6.0205999).abs() < 0.01);
            }
        }
        assert!(!a.push(&[f32::NAN, 0.0]));
        assert!(!a.push(&[0.0]));
        let mut silent = VisualMeter::new(48000, 1).unwrap();
        assert!(silent.push(&[0.0; 128]));
        let bin = silent.finish_bin().unwrap();
        assert_eq!(bin.peak, [0.0; 2]);
        assert!(bin.crest_db.is_nan());
        assert!(VisualMeter::new(0, 2).is_none());
    }
}
