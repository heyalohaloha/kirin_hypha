//! Bounded, worker-only Reference display measurements. Never a Record authority.
pub use crate::analysis_lease::capture::{BlindCaptureExclusion, CaptureAdmission};
pub use crate::analysis_lease::reference_owner::{ReferenceAnalysisGrant, ReferenceAnalysisOwner};
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
    /// The bin's raw sums and end-of-bin values, so Hypha can recompute Kirin OS's per-hop facts
    /// (native/src/reference_analysis.rs) for a hop made of whole bins (2026-10-04, range strips).
    /// Linear true peak: the largest `prev_true_peak` over the bin's pushes.
    pub true_peak: f64,
    /// LUFS-M at the end of the bin (NaN until 0.4 s have been pushed).
    pub momentary_lufs: f64,
    /// Stereo only (0 for mono): sum of l*r, ((l + r) / 2)^2 and ((l - r) / 2)^2.
    pub cross: f64,
    pub mid: f64,
    pub side: f64,
}
// The C header (kirin_hypha_reference_visual_ffi.h) mirrors this layout field by field.
const _: () = assert!(std::mem::size_of::<VisualBin>() == 96);

pub struct VisualMeter {
    meter: EbuR128,
    rate: u32,
    channels: usize,
    total: u64,
    frames: u64,
    peak: [f64; 2],
    energy: [f64; 2],
    tp: f64,
    cross: f64,
    mid: f64,
    side: f64,
    sealed: Option<(f64, f64)>,
    final_short: Option<f64>,
    final_momentary: Option<f64>,
}
impl VisualMeter {
    pub fn new(rate: u32, channels: usize) -> Option<Self> {
        Self::with_mode(rate, channels, Mode::M | Mode::S | Mode::TRUE_PEAK)
    }
    pub fn capture(rate: u32, channels: usize) -> Option<Self> {
        Self::with_mode(
            rate,
            channels,
            Mode::M | Mode::S | Mode::I | Mode::TRUE_PEAK,
        )
    }
    fn with_mode(rate: u32, channels: usize, mode: Mode) -> Option<Self> {
        if !(8_000..=768_000).contains(&rate) || !(1..=2).contains(&channels) {
            return None;
        }
        Some(Self {
            meter: EbuR128::new(channels as u32, rate, mode).ok()?,
            rate,
            channels,
            total: 0,
            frames: 0,
            peak: [0.0; 2],
            energy: [0.0; 2],
            tp: 0.0,
            cross: 0.0,
            mid: 0.0,
            side: 0.0,
            sealed: None,
            final_short: None,
            final_momentary: None,
        })
    }
    pub fn push(&mut self, samples: &[f32]) -> bool {
        if self.sealed.is_some()
            || samples.is_empty()
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
            if self.channels == 2 {
                let (l, r) = (f64::from(frame[0]), f64::from(frame[1]));
                self.cross += l * r;
                self.mid += ((l + r) * 0.5).powi(2);
                self.side += ((l - r) * 0.5).powi(2);
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
            short_lufs: if let Some(value) = self.final_short {
                value
            } else if self.total >= u64::from(self.rate) * 3 {
                self.meter.loudness_shortterm().unwrap_or(f64::NAN)
            } else {
                f64::NAN
            },
            crest_db: if combined > 1e-15 && self.tp > 0.0 {
                20.0 * (self.tp / combined).log10()
            } else {
                f64::NAN
            },
            true_peak: self.tp,
            momentary_lufs: if let Some(value) = self.final_momentary {
                value
            } else {
                self.momentary()
            },
            cross: self.cross,
            mid: self.mid,
            side: self.side,
        };
        self.frames = 0;
        self.peak = [0.0; 2];
        self.energy = [0.0; 2];
        self.tp = 0.0;
        self.cross = 0.0;
        self.mid = 0.0;
        self.side = 0.0;
        Some(bin)
    }
    /// Close zero-extended TP FIR support without adding silence to accepted frames, RMS,
    /// short-term or integrated loudness. The meter cannot accept another pass afterwards.
    pub fn seal_capture(&mut self) -> (f64, f64) {
        if let Some(totals) = self.sealed {
            return totals;
        }
        let integrated = self.integrated();
        self.final_momentary = Some(self.momentary());
        self.final_short = Some(if self.total >= u64::from(self.rate) * 3 {
            self.meter.loudness_shortterm().unwrap_or(f64::NAN)
        } else {
            f64::NAN
        });
        // ebur128 0.1.10 uses at most 24 input frames of TP FIR history (2x/4x).
        let zeros = [0.0_f32; 64];
        let _ = self.meter.add_frames_f32(&zeros[..32 * self.channels]);
        for c in 0..self.channels {
            self.tp = self
                .tp
                .max(self.meter.prev_true_peak(c as u32).unwrap_or(0.0));
        }
        let totals = (integrated, self.maximum_true_peak());
        self.sealed = Some(totals);
        totals
    }
    pub fn pending_true_peak(&self) -> f64 {
        self.tp
    }
    // Kirin OS emits LUFS-M only once 0.4 s have been measured (`end >= sr * 4 / 10`).
    fn momentary(&self) -> f64 {
        if self.total >= u64::from(self.rate) * 4 / 10 {
            self.meter.loudness_momentary().unwrap_or(f64::NAN)
        } else {
            f64::NAN
        }
    }
    pub fn integrated(&self) -> f64 {
        self.meter.loudness_global().unwrap_or(f64::NAN)
    }
    pub fn maximum_true_peak(&self) -> f64 {
        (0..self.channels)
            .map(|c| self.meter.true_peak(c as u32).unwrap_or(0.0))
            .fold(0.0, f64::max)
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

#[cfg(test)]
mod hop_fact_tests {
    use super::*;
    #[test]
    fn bins_carry_the_kirin_os_hop_sums_and_momentary() {
        // L and R 60 degrees apart, rising level: the bin sums equal a direct sum of the same samples,
        // LUFS-M equals an independent meter at the end of every bin, and a mono meter keeps them at 0.
        let rate = 48_000u32;
        let mut meter = VisualMeter::new(rate, 2).unwrap();
        let mut oracle = EbuR128::new(2, rate, Mode::M).unwrap();
        let mut index = 0usize;
        for bin in 0..12 {
            let (mut cross, mut mid, mut side) = (0.0_f64, 0.0_f64, 0.0_f64);
            for _ in 0..8 {
                let pcm: Vec<f32> = (0..600)
                    .flat_map(|_| {
                        let t = index as f64 / f64::from(rate);
                        index += 1;
                        let gain = 0.1 + 0.02 * bin as f64;
                        let l = (gain * (std::f64::consts::TAU * 440.0 * t).sin()) as f32;
                        let r = (gain
                            * (std::f64::consts::TAU * 440.0 * t + std::f64::consts::FRAC_PI_3)
                                .sin()) as f32;
                        [l, r]
                    })
                    .collect();
                for frame in pcm.as_chunks::<2>().0 {
                    let (l, r) = (f64::from(frame[0]), f64::from(frame[1]));
                    cross += l * r;
                    mid += ((l + r) * 0.5).powi(2);
                    side += ((l - r) * 0.5).powi(2);
                }
                assert!(meter.push(&pcm));
                oracle.add_frames_f32(&pcm).unwrap();
            }
            let value = meter.finish_bin().unwrap();
            assert!(
                (value.cross - cross).abs() < 1e-9
                    && (value.mid - mid).abs() < 1e-9
                    && (value.side - side).abs() < 1e-9
            );
            assert!(value.true_peak > 0.0 && value.true_peak >= value.peak[0].max(value.peak[1]));
            if bin < 3 {
                assert!(
                    value.momentary_lufs.is_nan(),
                    "bin {bin}: LUFS-M waits for 0.4 s"
                );
            } else {
                assert!((value.momentary_lufs - oracle.loudness_momentary().unwrap()).abs() < 1e-9);
            }
        }
        let mut mono = VisualMeter::new(rate, 1).unwrap();
        assert!(mono.push(&[0.5; 4800]));
        let value = mono.finish_bin().unwrap();
        assert!(value.cross == 0.0 && value.mid == 0.0 && value.side == 0.0);
    }
}

#[cfg(test)]
mod capture_tests {
    use super::*;
    #[test]
    fn capture_totals_use_every_frame_and_flush_only_true_peak_support() {
        for rate in [44100, 48000, 96000, 192000] {
            for channels in [1, 2] {
                for block in [64, 128, 512, 1024] {
                    let mut meter = VisualMeter::capture(rate, channels).unwrap();
                    let mut oracle =
                        EbuR128::new(channels as u32, rate, Mode::I | Mode::S | Mode::TRUE_PEAK)
                            .unwrap();
                    let total = rate as usize * 4 + 1;
                    let mut sum = 0.0;
                    let mut peak = 0.0_f64;
                    for offset in (0..total).step_by(block) {
                        let count = block.min(total - offset);
                        let pcm: Vec<f32> = (offset..offset + count)
                            .flat_map(|i| {
                                let v = ((std::f64::consts::TAU * 1000.0 * i as f64 / rate as f64)
                                    .sin()
                                    * if i < rate as usize * 2 { 0.1 } else { 0.3 })
                                    as f32;
                                peak = peak.max(f64::from(v).abs());
                                sum += f64::from(v).powi(2);
                                (0..channels).map(move |c| if c == 0 { v } else { -v })
                            })
                            .collect();
                        assert!(meter.push(&pcm));
                        oracle.add_frames_f32(&pcm).unwrap();
                    }
                    let expected_i = oracle.loudness_global().unwrap();
                    let expected_s = oracle.loudness_shortterm().unwrap();
                    oracle.add_frames_f32(&vec![0.0; 32 * channels]).unwrap();
                    let expected_tp = (0..channels)
                        .map(|c| oracle.true_peak(c as u32).unwrap())
                        .fold(0.0, f64::max);
                    let (i, tp) = meter.seal_capture();
                    let bin = meter.finish_bin().unwrap();
                    assert_eq!(bin.frames, total as u64);
                    assert!(
                        (i - expected_i).abs() < 0.0001
                            && (bin.short_lufs - expected_s).abs() < 0.0001
                    );
                    assert!((tp - expected_tp).abs() < 1e-9);
                    assert!(
                        (bin.peak[0] - peak).abs() < 1e-9
                            && (bin.rms[0] - (sum / total as f64).sqrt()).abs() < 1e-9
                    );
                    assert!(!meter.push(&[0.0, 0.0]));
                }
            }
        }
        let mut tail = VisualMeter::capture(48000, 1).unwrap();
        assert!(tail.push(&[1.0]));
        let (_, tp) = tail.seal_capture();
        let bin = tail.finish_bin().unwrap();
        assert_eq!(bin.frames, 1);
        assert_eq!(bin.rms[0], 1.0);
        assert!(tp >= 1.0);
        assert!(bin.short_lufs.is_nan());
    }
}
