//! Minimal BS.1770 level path for the optional POST absolute Analysis timeline.
//!
//! Record and Meter Session need the full `MeasureEngine` surface. This observer needs only
//! momentary loudness and the 400 ms recent True Peak fact, so running S/I/LRA/Crest/PSR here would
//! duplicate unrelated work for every visible POST Analysis slot.

use std::collections::VecDeque;

use ebur128::{EbuR128, Mode};

const RECENT_APERTURES: usize = 4;
const LUFS_VALID_FLOOR: f64 = -100.0;
const TRUE_PEAK_VALID_FLOOR: f64 = -100.0;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub(crate) struct AbsoluteLevelFrame {
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
}

pub(crate) struct AbsoluteLevelAnalyzer {
    ebu: EbuR128,
    channels: usize,
    aperture_samples: usize,
    input_f64: Vec<f64>,
    recent_true_peaks: VecDeque<f64>,
}

impl AbsoluteLevelAnalyzer {
    pub(crate) fn new(sample_rate: u32, channels: usize) -> Result<Self, ()> {
        let aperture_samples = ((sample_rate as usize) + 5) / 10;
        let ebu = EbuR128::new(channels as u32, sample_rate, Mode::M | Mode::TRUE_PEAK)
            .map_err(|_| ())?;
        Ok(Self {
            ebu,
            channels,
            aperture_samples,
            input_f64: Vec::with_capacity(aperture_samples * channels),
            recent_true_peaks: VecDeque::with_capacity(RECENT_APERTURES),
        })
    }

    pub(crate) fn reset(&mut self) {
        self.ebu.reset();
        self.input_f64.clear();
        self.recent_true_peaks.clear();
    }

    pub(crate) fn analyze(&mut self, interleaved: &[f32]) -> Result<AbsoluteLevelFrame, ()> {
        if interleaved.len() != self.aperture_samples * self.channels
            || interleaved.iter().any(|sample| !sample.is_finite())
        {
            return Err(());
        }

        self.input_f64.clear();
        self.input_f64
            .extend(interleaved.iter().map(|sample| f64::from(*sample)));
        self.ebu.add_frames_f64(&self.input_f64).map_err(|_| ())?;

        let aperture_peak = (0..self.channels as u32)
            .filter_map(|channel| self.ebu.prev_true_peak(channel).ok())
            .filter(|peak| peak.is_finite())
            .fold(0.0_f64, f64::max);
        if self.recent_true_peaks.len() == RECENT_APERTURES {
            self.recent_true_peaks.pop_front();
        }
        self.recent_true_peaks.push_back(aperture_peak);

        let lufs_m = self
            .ebu
            .loudness_momentary()
            .ok()
            .filter(|value| value.is_finite() && *value > LUFS_VALID_FLOOR);
        let true_peak = self
            .recent_true_peaks
            .iter()
            .copied()
            .fold(0.0_f64, f64::max);
        let true_peak = (true_peak > 0.0)
            .then(|| 20.0 * true_peak.log10())
            .filter(|value| value.is_finite() && *value > TRUE_PEAK_VALID_FLOOR);

        Ok(AbsoluteLevelFrame { lufs_m, true_peak })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::MeasureEngine;

    fn close(left: Option<f64>, right: Option<f64>) -> bool {
        match (left, right) {
            (Some(left), Some(right)) => (left - right).abs() <= 1.0e-12,
            (None, None) => true,
            _ => false,
        }
    }

    #[test]
    fn minimal_level_path_matches_full_engine_at_every_public_boundary() {
        for &(sample_rate, channels) in &[(44_100_u32, 1_usize), (48_000, 2), (96_000, 2)] {
            let aperture = ((sample_rate as usize) + 5) / 10;
            let mut minimal = AbsoluteLevelAnalyzer::new(sample_rate, channels).unwrap();
            let mut full = MeasureEngine::new(sample_rate, channels).unwrap();
            for slot in 0..12 {
                let input = (0..aperture)
                    .flat_map(|frame| {
                        let phase =
                            std::f32::consts::TAU * (slot * aperture + frame) as f32 * 997.0
                                / sample_rate as f32;
                        let left = phase.sin() * (0.08 + slot as f32 * 0.01);
                        [left, -0.37 * left].into_iter().take(channels)
                    })
                    .collect::<Vec<_>>();
                let full_input = input
                    .iter()
                    .map(|sample| f64::from(*sample))
                    .collect::<Vec<_>>();
                let expected = full.push(&full_input).unwrap();
                let actual = minimal.analyze(&input).unwrap();
                assert!(close(actual.lufs_m, expected.lufs_m));
                assert!(close(actual.true_peak, expected.true_peak));
            }
        }
    }

    #[test]
    fn true_peak_parity_survives_impulses_across_aperture_boundaries() {
        for &(sample_rate, channels) in &[(44_100_u32, 1_usize), (48_000, 2), (96_000, 2)] {
            let aperture = ((sample_rate as usize) + 5) / 10;
            let mut minimal = AbsoluteLevelAnalyzer::new(sample_rate, channels).unwrap();
            let mut full = MeasureEngine::new(sample_rate, channels).unwrap();
            for slot in 0..8 {
                let input = (0..aperture)
                    .flat_map(|frame| {
                        let absolute_frame = slot * aperture + frame;
                        let left = match absolute_frame {
                            value if value == aperture - 2 => 0.72,
                            value if value == aperture - 1 => -0.91,
                            value if value == aperture => 0.83,
                            value if value == aperture + 1 => -0.67,
                            value if value == 5 * aperture - 1 => -0.78,
                            value if value == 5 * aperture => 0.94,
                            _ => 0.0,
                        };
                        [left, -0.43 * left].into_iter().take(channels)
                    })
                    .collect::<Vec<_>>();
                let full_input = input
                    .iter()
                    .map(|sample| f64::from(*sample))
                    .collect::<Vec<_>>();
                let expected = full.push(&full_input).unwrap();
                let actual = minimal.analyze(&input).unwrap();
                assert!(
                    close(actual.true_peak, expected.true_peak),
                    "True Peak mismatch at {sample_rate} Hz, {channels} ch, slot {slot}: actual={:?}, expected={:?}",
                    actual.true_peak,
                    expected.true_peak
                );
            }
        }
    }

    #[test]
    fn reset_and_invalid_input_fail_closed_without_stale_peak() {
        let mut analyzer = AbsoluteLevelAnalyzer::new(48_000, 2).unwrap();
        let tone = vec![0.25_f32; 9_600];
        assert!(analyzer.analyze(&tone).unwrap().true_peak.is_some());
        analyzer.reset();
        assert_eq!(analyzer.analyze(&vec![0.0; 9_600]).unwrap().true_peak, None);
        assert!(analyzer.analyze(&tone[..9_599]).is_err());
        let mut invalid = tone;
        invalid[200] = f32::NAN;
        assert!(analyzer.analyze(&invalid).is_err());
    }
}
