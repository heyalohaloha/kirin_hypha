use crate::spectrum::{SpectrumChannelMode, SpectrumFrame};
use std::sync::atomic::{AtomicU64, Ordering};

// Worker-only identity: a new assembler or any aperture reset breaks display continuity.
pub(crate) fn next_mid_side_continuity() -> u64 {
    static NEXT: AtomicU64 = AtomicU64::new(1);
    let mut value = NEXT.load(Ordering::Relaxed);
    loop {
        let Some(next) = value.checked_add(1) else {
            return 0;
        };
        match NEXT.compare_exchange_weak(value, next, Ordering::Relaxed, Ordering::Relaxed) {
            Ok(_) => return value,
            Err(current) => value = current,
        }
    }
}

/// One coherent POST-local Mid/Side observation from the same stereo aperture.
#[derive(Clone, Debug, PartialEq)]
pub struct MidSideSpectrumFrame {
    pub mid: SpectrumFrame,
    pub side: SpectrumFrame,
    pub continuity: u64,
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
        coherent.then_some(Self {
            mid,
            side,
            continuity: 0,
        })
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
    #[test]
    fn spectrum_history_identity_outlives_poll_recovery_and_changes_at_real_edges() {
        let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
        let samples = vec![0.25; SPECTRUM_WINDOW_SIZE];
        let mut frame = analyzer
            .analyze_mode(&samples, None, SpectrumChannelMode::Lr, 4800, 7)
            .unwrap();
        let mut history = crate::SpectrumHistory::with_capacity();
        history.push(frame.clone());
        let first = history.continuity();
        assert_ne!(first, 0);
        assert_eq!(history.continuity_span(), Some((4800, 4800, first)));
        for _ in 0..24 {
            frame.presentation_end_samples += 1600;
            history.push(frame.clone());
        }
        assert_eq!(history.continuity(), first);
        assert_eq!(history.frames().len(), 8);
        assert_eq!(
            history.continuity_span(),
            Some((4800, frame.presentation_end_samples, first))
        );
        frame.presentation_end_samples += 3200;
        history.push(frame.clone());
        let gap = history.continuity();
        assert_ne!(gap, first);
        for _ in 0..24 {
            frame.presentation_end_samples += 1600;
            history.push(frame.clone());
        }
        assert_eq!(history.continuity(), gap);
        frame.presentation_end_samples = 4800;
        history.push(frame.clone());
        let seek = history.continuity();
        assert_ne!(seek, gap);
        frame.generation += 1;
        frame.presentation_end_samples += 1600;
        history.push(frame.clone());
        assert_ne!(history.continuity(), seek);
        let mut same_measurement = crate::SpectrumHistory::with_capacity();
        same_measurement.push(frame);
        assert_eq!(same_measurement, history);
        assert_ne!(same_measurement.continuity(), history.continuity());
    }
}
