//! Whole-span MATCH meter. Called only by the non-realtime comparison service.
use ebur128::{EbuR128, Mode};

pub struct KirinLiveMatchSpan {
    pre: EbuR128,
    post: EbuR128,
    min_frames: u64,
    frames: u64,
    pub channels: usize,
    fault: bool,
}

#[repr(C)]
pub struct KirinLiveMatchSpanFacts {
    pub pre_lufs: f64,
    pub post_lufs: f64,
    pub pre_peak: f64,
    pub post_peak: f64,
}

impl KirinLiveMatchSpan {
    pub fn new(rate: u32, channels: u32) -> Option<Self> {
        if !matches!(channels, 1 | 2) || !(8_000..=384_000).contains(&rate) {
            return None;
        }
        let mode = Mode::I | Mode::TRUE_PEAK | Mode::HISTOGRAM;
        Some(Self {
            pre: EbuR128::new(channels, rate, mode).ok()?,
            post: EbuR128::new(channels, rate, mode).ok()?,
            min_frames: u64::from(rate) * 3,
            frames: 0,
            channels: channels as usize,
            fault: false,
        })
    }

    pub fn feed(&mut self, pre: &[f32], post: &[f32]) -> bool {
        if self.fault || pre.len() != post.len() || !pre.len().is_multiple_of(self.channels) {
            return false;
        }
        if pre.iter().chain(post).any(|x| !x.is_finite())
            || self.pre.add_frames_f32(pre).is_err()
            || self.post.add_frames_f32(post).is_err()
        {
            self.fault = true;
            return false;
        }
        self.frames = self
            .frames
            .saturating_add((pre.len() / self.channels) as u64);
        true
    }

    /// Break filter/window continuity, retaining only completed gate windows and true peaks.
    pub fn cut(&mut self) -> bool {
        if self.fault {
            return false;
        }
        self.pre.cut_preserving_measurements();
        self.post.cut_preserving_measurements();
        true
    }

    pub fn measure(&self) -> Option<KirinLiveMatchSpanFacts> {
        if self.fault || self.frames < self.min_frames {
            return None;
        }
        let pre_lufs = self.pre.loudness_global().ok()?;
        let post_lufs = self.post.loudness_global().ok()?;
        if !pre_lufs.is_finite() || !post_lufs.is_finite() {
            return None;
        }
        let peak = |meter: &EbuR128| {
            meter
                .true_peak_at_cutoff()
                .ok()
                .filter(|p| *p > 0.0)
                .map(|p| 20.0 * p.log10())
        };
        Some(KirinLiveMatchSpanFacts {
            pre_lufs,
            post_lufs,
            pre_peak: peak(&self.pre)?,
            post_peak: peak(&self.post)?,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stereo_rejects_partial_frames_without_poisoning_the_meter() {
        let mut meter = KirinLiveMatchSpan::new(48_000, 2).unwrap();
        let malformed = [0.1, 0.2, 0.3];
        assert!(!meter.feed(&malformed, &malformed));
        assert_eq!(meter.frames, 0);
        assert!(meter.feed(&malformed[..2], &malformed[..2]));
        assert_eq!(meter.frames, 1);
    }

    #[test]
    fn whole_span_differs_from_last_four_and_matches_independent_exact_gate() {
        let rate = 48_000;
        for channels in [1, 2] {
            let tones = |span: usize, scale: f64| -> Vec<f32> {
                (0..span)
                    .flat_map(|n| {
                        let sample = (scale
                            * (std::f64::consts::TAU * 997.0 * n as f64 / rate as f64).sin())
                            as f32;
                        std::iter::repeat_n(sample, channels)
                    })
                    .collect()
            };
            let intro_pre = tones(rate as usize * 8, 0.06);
            let intro_post = tones(rate as usize * 8, 0.12);
            let chorus_pre = tones(rate as usize * 4, 0.24);
            let chorus_post = tones(rate as usize * 4, 0.24);
            let mut meter = KirinLiveMatchSpan::new(rate, channels as u32).unwrap();
            assert!(meter.feed(
                &intro_pre[..rate as usize * channels],
                &intro_post[..rate as usize * channels]
            ));
            assert!(meter.measure().is_none());
            assert!(meter.feed(
                &intro_pre[rate as usize * channels..],
                &intro_post[rate as usize * channels..]
            ));
            assert!(meter.feed(&chorus_pre, &chorus_post));
            let whole = meter.measure().unwrap();
            let mut last = KirinLiveMatchSpan::new(rate, channels as u32).unwrap();
            assert!(last.feed(&chorus_pre, &chorus_post));
            let last = last.measure().unwrap();
            assert!(
                (whole.post_lufs - whole.pre_lufs - (last.post_lufs - last.pre_lufs)).abs() > 0.1
            );
            for (intro, chorus, actual) in [
                (&intro_pre, &chorus_pre, whole.pre_lufs),
                (&intro_post, &chorus_post, whole.post_lufs),
            ] {
                let mut reference = EbuR128::new(channels as u32, rate, Mode::I).unwrap();
                reference
                    .add_frames_f32(&[intro.as_slice(), chorus.as_slice()].concat())
                    .unwrap();
                assert!((reference.loudness_global().unwrap() - actual).abs() <= 0.1);
            }
        }
    }
}
