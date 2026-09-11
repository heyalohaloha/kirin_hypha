use crate::absolute_timeline::{
    AbsoluteContinuousAnalyzer, AbsoluteFrame, ABSOLUTE_HISTORY_SECONDS,
};
use crate::perceptual::{PerceptualFrame, SharpnessContinuousAnalyzer};
use crate::spectrum::{
    SpectrumAnalyzer, SpectrumChannelMode, SpectrumFrame, SPECTRUM_PRESENTATION_HZ,
};
use crate::MidSideSpectrumFrame;

pub(super) struct SpectrumAssembler {
    analyzer: SpectrumAnalyzer,
    channels: usize,
    cadence_samples: i64,
    window_size: usize,
    left: Vec<f32>,
    right: Vec<f32>,
    ordered_left: Vec<f32>,
    ordered_right: Vec<f32>,
    write_index: usize,
    filled: usize,
    next_position: Option<i64>,
    generation: u64,
}

impl SpectrumAssembler {
    pub(super) fn new(analyzer: SpectrumAnalyzer, channels: usize) -> Self {
        let cadence_samples = i64::from(analyzer.sample_rate() / SPECTRUM_PRESENTATION_HZ);
        let window_size = analyzer.aperture_samples();
        Self {
            analyzer,
            channels,
            cadence_samples,
            window_size,
            left: vec![0.0; window_size],
            right: vec![0.0; window_size],
            ordered_left: vec![0.0; window_size],
            ordered_right: vec![0.0; window_size],
            write_index: 0,
            filled: 0,
            next_position: None,
            generation: 0,
        }
    }

    pub(super) fn begin_block(&mut self, start: i64, generation: u64) -> bool {
        if generation == 0 {
            self.reset();
            return false;
        }
        if self.generation != generation || self.next_position.is_some_and(|next| next != start) {
            self.reset();
            self.generation = generation;
        }
        self.next_position = Some(start);
        true
    }

    pub(super) fn push_frame(
        &mut self,
        left: f32,
        right: Option<f32>,
        channel_mode: SpectrumChannelMode,
    ) -> Option<SpectrumFrame> {
        let end = self.push_sample(left, right)?;
        let right = (self.channels == 2).then_some(self.ordered_right.as_slice());
        self.analyzer
            .analyze_mode(
                &self.ordered_left,
                right,
                channel_mode,
                end,
                self.generation,
            )
            .ok()
    }

    pub(super) fn push_mid_side_frame(
        &mut self,
        left: f32,
        right: Option<f32>,
    ) -> Option<MidSideSpectrumFrame> {
        let end = self.push_sample(left, right)?;
        if self.channels != 2 {
            return None;
        }
        let mid = self
            .analyzer
            .analyze_mode(
                &self.ordered_left,
                Some(&self.ordered_right),
                SpectrumChannelMode::Mid,
                end,
                self.generation,
            )
            .ok()?;
        let side = self
            .analyzer
            .analyze_mode(
                &self.ordered_left,
                Some(&self.ordered_right),
                SpectrumChannelMode::Side,
                end,
                self.generation,
            )
            .ok()?;
        MidSideSpectrumFrame::from_frames(mid, side)
    }

    fn push_sample(&mut self, left: f32, right: Option<f32>) -> Option<i64> {
        if !left.is_finite() || right.is_some_and(|value| !value.is_finite()) {
            self.reset();
            return None;
        }
        self.left[self.write_index] = left;
        self.right[self.write_index] = right.unwrap_or(0.0);
        self.write_index = (self.write_index + 1) % self.window_size;
        self.filled = self.filled.saturating_add(1).min(self.window_size);
        let end = self.next_position?.checked_add(1)?;
        self.next_position = Some(end);
        if self.filled != self.window_size || end.rem_euclid(self.cadence_samples) != 0 {
            return None;
        }
        copy_ordered(&self.left, self.write_index, &mut self.ordered_left);
        if self.channels == 2 {
            copy_ordered(&self.right, self.write_index, &mut self.ordered_right);
        }
        Some(end)
    }

    pub(super) fn reset(&mut self) {
        self.write_index = 0;
        self.filled = 0;
        self.next_position = None;
        self.generation = 0;
    }
}

fn copy_ordered(source: &[f32], start: usize, destination: &mut [f32]) {
    let tail = source.len() - start;
    destination[..tail].copy_from_slice(&source[start..]);
    destination[tail..].copy_from_slice(&source[..start]);
}

pub(super) struct PerceptualAssembler {
    analyzer: SharpnessContinuousAnalyzer,
    channels: usize,
    aperture_samples: i64,
    samples: Vec<f32>,
    collecting: bool,
    next_position: Option<i64>,
    generation: u64,
    state_epoch_samples: Option<i64>,
    sequence_started: bool,
    rearm_required: bool,
}

pub(super) struct AbsoluteAssembler {
    analyzer: AbsoluteContinuousAnalyzer,
    channels: usize,
    aperture_samples: i64,
    samples: Vec<f32>,
    collecting: bool,
    next_position: Option<i64>,
    aligned_start: Option<i64>,
    state_epoch: Option<i64>,
    generation: u64,
    history_reset_required: bool,
}

impl AbsoluteAssembler {
    pub(super) fn new(sample_rate: u32, channels: usize) -> Result<Self, crate::AbsoluteError> {
        let analyzer = AbsoluteContinuousAnalyzer::new(sample_rate, channels)?;
        let aperture_samples = analyzer.aperture_samples() as i64;
        Ok(Self {
            analyzer,
            channels,
            aperture_samples,
            samples: Vec::with_capacity(aperture_samples as usize * channels),
            collecting: false,
            next_position: None,
            aligned_start: None,
            state_epoch: None,
            generation: 0,
            history_reset_required: false,
        })
    }

    pub(super) fn begin_block(&mut self, start: i64, generation: u64) -> bool {
        if generation == 0 {
            self.reset();
            return false;
        }
        if self.generation != generation || self.next_position.is_some_and(|next| next != start) {
            let history_samples = self.aperture_samples
                * i64::from(crate::absolute_timeline::ABSOLUTE_PRESENTATION_HZ)
                * ABSOLUTE_HISTORY_SECONDS;
            let history_reset_required = self.next_position.is_some_and(|expected| {
                start < expected || start.saturating_sub(expected) >= history_samples
            });
            self.reset();
            self.generation = generation;
            // A short forward miss is an observation gap, not permission to erase already
            // verified history. Backwards transport and a gap spanning the complete six-second
            // field start a new run immediately.
            self.history_reset_required = history_reset_required;
            self.aligned_start = start
                .checked_add(self.aperture_samples - 1)
                .map(|value| value - value.rem_euclid(self.aperture_samples));
        }
        self.next_position = Some(start);
        self.aligned_start.is_some()
    }

    pub(super) fn take_history_reset_required(&mut self) -> bool {
        std::mem::take(&mut self.history_reset_required)
    }

    pub(super) fn push_frame(&mut self, left: f32, right: Option<f32>) -> Option<&[AbsoluteFrame]> {
        let start = self.next_position?;
        let end = start.checked_add(1)?;
        self.next_position = Some(end);
        let aligned_start = self.aligned_start?;
        if start < aligned_start {
            return None;
        }
        if !self.collecting {
            if start != aligned_start && start.rem_euclid(self.aperture_samples) != 0 {
                self.reset();
                return None;
            }
            if self.state_epoch.is_none() {
                if self.analyzer.reset_at_epoch(start).is_err() {
                    self.reset();
                    return None;
                }
                self.state_epoch = Some(start);
            }
            self.samples.clear();
            self.collecting = true;
        }
        if !left.is_finite() || right.is_some_and(|sample| !sample.is_finite()) {
            self.reset();
            return None;
        }
        self.samples.push(left);
        if self.channels == 2 {
            let Some(right) = right else {
                self.reset();
                return None;
            };
            self.samples.push(right);
        }
        if end.rem_euclid(self.aperture_samples) != 0
            || self.samples.len() != self.aperture_samples as usize * self.channels
        {
            return None;
        }
        self.collecting = false;
        self.aligned_start = Some(end);
        self.analyzer
            .analyze_aperture(&self.samples, end, self.generation)
            .ok()
    }

    pub(super) fn reset(&mut self) {
        self.samples.clear();
        self.collecting = false;
        self.next_position = None;
        self.aligned_start = None;
        self.state_epoch = None;
        self.generation = 0;
        self.history_reset_required = false;
    }
}

impl PerceptualAssembler {
    pub(super) fn new(sample_rate: u32, channels: usize) -> Result<Self, crate::PerceptualError> {
        let analyzer = SharpnessContinuousAnalyzer::new(sample_rate, channels)?;
        let aperture_samples = analyzer.aperture_samples() as i64;
        Ok(Self {
            analyzer,
            channels,
            aperture_samples,
            samples: Vec::with_capacity(aperture_samples as usize * channels),
            collecting: false,
            next_position: None,
            generation: 0,
            state_epoch_samples: None,
            sequence_started: false,
            rearm_required: false,
        })
    }

    pub(super) fn begin_block(
        &mut self,
        start: i64,
        generation: u64,
        state_epoch_samples: Option<i64>,
    ) -> bool {
        let Some(state_epoch_samples) = state_epoch_samples else {
            self.reset();
            return false;
        };
        if generation == 0 {
            self.reset();
            return false;
        }
        if self.generation != generation || self.state_epoch_samples != Some(state_epoch_samples) {
            self.reset();
            if self.analyzer.reset_at_epoch(state_epoch_samples).is_err() {
                return false;
            }
            self.generation = generation;
            self.state_epoch_samples = Some(state_epoch_samples);
        } else if self.next_position.is_some_and(|next| next != start) {
            self.reset();
            self.rearm_required = true;
            return false;
        }
        self.next_position = Some(start);
        true
    }

    pub(super) fn push_frame(
        &mut self,
        left: f32,
        right: Option<f32>,
        channel_mode: SpectrumChannelMode,
    ) -> Option<&[PerceptualFrame]> {
        let start = self.next_position?;
        let end = start.checked_add(1)?;
        self.next_position = Some(end);
        if !self.collecting {
            let epoch = self.state_epoch_samples?;
            if !self.sequence_started {
                if start < epoch {
                    return None;
                }
                if start != epoch {
                    self.reset();
                    self.rearm_required = true;
                    return None;
                }
                self.sequence_started = true;
            } else if start.rem_euclid(self.aperture_samples) != 0 {
                self.reset();
                self.rearm_required = true;
                return None;
            }
            self.samples.clear();
            self.collecting = true;
        }
        if !left.is_finite() || right.is_some_and(|sample| !sample.is_finite()) {
            self.reset();
            self.rearm_required = true;
            return None;
        }
        self.samples.push(left);
        if self.channels == 2 {
            let Some(right) = right else {
                self.reset();
                self.rearm_required = true;
                return None;
            };
            self.samples.push(right);
        }
        if end.rem_euclid(self.aperture_samples) != 0
            || self.samples.len() != self.aperture_samples as usize * self.channels
        {
            return None;
        }
        self.collecting = false;
        if self
            .analyzer
            .analyze_aperture(&self.samples, channel_mode, end, self.generation)
            .is_err()
        {
            self.reset();
            self.rearm_required = true;
            return None;
        }
        Some(self.analyzer.output_frames())
    }

    pub(super) fn take_rearm_required(&mut self) -> bool {
        std::mem::take(&mut self.rearm_required)
    }

    pub(super) fn reset(&mut self) {
        self.samples.clear();
        self.collecting = false;
        self.next_position = None;
        self.generation = 0;
        self.state_epoch_samples = None;
        self.sequence_started = false;
        self.rearm_required = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn analyzer_error_clears_state_and_requires_a_new_epoch() {
        let mut assembler = PerceptualAssembler::new(48_000, 2).unwrap();
        assert!(assembler.begin_block(0, 1, Some(0)));
        for _ in 0..4_800 {
            let _ = assembler.push_frame(0.1, Some(0.1), SpectrumChannelMode::Lr);
        }
        assert!(assembler.begin_block(4_800, 1, Some(0)));
        for _ in 0..4_800 {
            assert!(assembler
                .push_frame(0.1, Some(0.1), SpectrumChannelMode::Mid)
                .is_none());
        }
        assert!(assembler.take_rearm_required());
    }

    #[test]
    fn mid_side_uses_one_window_and_matches_the_single_channel_definitions() {
        let mut assembler = SpectrumAssembler::new(SpectrumAnalyzer::new(48_000).unwrap(), 2);
        assert!(assembler.begin_block(0, 11));
        let mut output = None;
        let mut left = Vec::with_capacity(4_800);
        let mut right = Vec::with_capacity(4_800);
        for index in 0..4_800 {
            let phase = index as f32 * 0.021;
            let l = phase.sin() * 0.5;
            let r = (phase * 1.7).cos() * 0.25;
            left.push(l);
            right.push(r);
            output = assembler.push_mid_side_frame(l, Some(r)).or(output);
        }
        let output = output.expect("one cadence-aligned Mid/Side frame");
        let left = &left[704..];
        let right = &right[704..];
        let mut expected = SpectrumAnalyzer::new(48_000).unwrap();
        let mid = expected
            .analyze_mode(left, Some(right), SpectrumChannelMode::Mid, 4_800, 11)
            .unwrap();
        let side = expected
            .analyze_mode(left, Some(right), SpectrumChannelMode::Side, 4_800, 11)
            .unwrap();
        assert_eq!(
            output.mid.dbfs.map(f32::to_bits),
            mid.dbfs.map(f32::to_bits)
        );
        assert_eq!(
            output.side.dbfs.map(f32::to_bits),
            side.dbfs.map(f32::to_bits)
        );
        assert_eq!(output.presentation_end_samples(), 4_800);
    }
}
