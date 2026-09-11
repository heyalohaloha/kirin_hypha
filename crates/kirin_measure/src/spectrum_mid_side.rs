use crate::spectrum::{SpectrumChannelMode, SpectrumFrame};

/// One coherent POST-local Mid/Side observation from the same stereo aperture.
#[derive(Clone, Debug, PartialEq)]
pub struct MidSideSpectrumFrame {
    pub mid: SpectrumFrame,
    pub side: SpectrumFrame,
}

impl MidSideSpectrumFrame {
    pub fn from_frames(mid: SpectrumFrame, side: SpectrumFrame) -> Option<Self> {
        let coherent = mid.channel_mode == SpectrumChannelMode::Mid
            && side.channel_mode == SpectrumChannelMode::Side
            && mid.channels == 2
            && side.channels == 2
            && mid.has_valid_layout()
            && side.has_valid_layout()
            && mid.schema_version == side.schema_version
            && mid.sample_rate == side.sample_rate
            && mid.aperture_samples == side.aperture_samples
            && mid.fft_size == side.fft_size
            && mid.band_count == side.band_count
            && mid.presentation_end_samples == side.presentation_end_samples
            && mid.generation == side.generation
            && mid.min_hz.to_bits() == side.min_hz.to_bits()
            && mid.max_hz.to_bits() == side.max_hz.to_bits();
        coherent.then_some(Self { mid, side })
    }

    pub fn presentation_end_samples(&self) -> i64 {
        self.mid.presentation_end_samples
    }

    pub fn generation(&self) -> u64 {
        self.mid.generation
    }

    pub fn has_valid_layout(&self) -> bool {
        Self::from_frames(self.mid.clone(), self.side.clone()).is_some()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{SpectrumAnalyzer, SPECTRUM_WINDOW_SIZE};

    #[test]
    fn only_accepts_mid_and_side_from_one_stereo_aperture() {
        let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
        let left = vec![0.25; SPECTRUM_WINDOW_SIZE];
        let right = vec![-0.25; SPECTRUM_WINDOW_SIZE];
        let mid = analyzer
            .analyze_mode(&left, Some(&right), SpectrumChannelMode::Mid, 9_600, 7)
            .unwrap();
        let side = analyzer
            .analyze_mode(&left, Some(&right), SpectrumChannelMode::Side, 9_600, 7)
            .unwrap();
        assert!(MidSideSpectrumFrame::from_frames(mid.clone(), side.clone()).is_some());

        let mut stale = side;
        stale.presentation_end_samples += 1;
        assert!(MidSideSpectrumFrame::from_frames(mid, stale).is_none());
    }
}
