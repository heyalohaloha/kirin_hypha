use crate::perceptual::{PerceptualFrame, SharpnessApertureAnalyzer};
use crate::spectrum::{
    SpectrumAnalyzer, SpectrumChannelMode, SpectrumFrame, SPECTRUM_PRESENTATION_HZ,
    SPECTRUM_WINDOW_SIZE,
};

pub(super) struct SpectrumAssembler {
    analyzer: SpectrumAnalyzer,
    channels: usize,
    cadence_samples: i64,
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
        Self {
            analyzer,
            channels,
            cadence_samples,
            left: vec![0.0; SPECTRUM_WINDOW_SIZE],
            right: vec![0.0; SPECTRUM_WINDOW_SIZE],
            ordered_left: vec![0.0; SPECTRUM_WINDOW_SIZE],
            ordered_right: vec![0.0; SPECTRUM_WINDOW_SIZE],
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
        if !left.is_finite() || right.is_some_and(|value| !value.is_finite()) {
            self.reset();
            return None;
        }
        self.left[self.write_index] = left;
        self.right[self.write_index] = right.unwrap_or(0.0);
        self.write_index = (self.write_index + 1) % SPECTRUM_WINDOW_SIZE;
        self.filled = self.filled.saturating_add(1).min(SPECTRUM_WINDOW_SIZE);
        let end = self.next_position?.checked_add(1)?;
        self.next_position = Some(end);
        if self.filled != SPECTRUM_WINDOW_SIZE || end.rem_euclid(self.cadence_samples) != 0 {
            return None;
        }
        copy_ordered(&self.left, self.write_index, &mut self.ordered_left);
        let right = if self.channels == 2 {
            copy_ordered(&self.right, self.write_index, &mut self.ordered_right);
            Some(self.ordered_right.as_slice())
        } else {
            None
        };
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
    analyzer: SharpnessApertureAnalyzer,
    channels: usize,
    aperture_samples: i64,
    samples: Vec<f32>,
    collecting: bool,
    next_position: Option<i64>,
    generation: u64,
}

impl PerceptualAssembler {
    pub(super) fn new(sample_rate: u32, channels: usize) -> Result<Self, crate::PerceptualError> {
        let analyzer = SharpnessApertureAnalyzer::new(sample_rate, channels)?;
        let aperture_samples = analyzer.aperture_samples() as i64;
        Ok(Self {
            analyzer,
            channels,
            aperture_samples,
            samples: Vec::with_capacity(aperture_samples as usize * channels),
            collecting: false,
            next_position: None,
            generation: 0,
        })
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
    ) -> Option<PerceptualFrame> {
        let start = self.next_position?;
        let end = start.checked_add(1)?;
        self.next_position = Some(end);
        if !self.collecting {
            if start.rem_euclid(self.aperture_samples) != 0 {
                return None;
            }
            self.samples.clear();
            self.collecting = true;
        }
        self.samples.push(left);
        if self.channels == 2 {
            self.samples.push(right?);
        }
        if end.rem_euclid(self.aperture_samples) != 0
            || self.samples.len() != self.aperture_samples as usize * self.channels
        {
            return None;
        }
        self.collecting = false;
        self.analyzer
            .analyze(&self.samples, channel_mode, end, self.generation)
            .ok()
    }

    pub(super) fn reset(&mut self) {
        self.samples.clear();
        self.collecting = false;
        self.next_position = None;
        self.generation = 0;
    }
}
