use crate::{SuperFluxAnalyzer, SuperFluxFrame};

use super::state::AttackOdfFrame;

pub(super) struct AttackAssembler {
    analyzer: SuperFluxAnalyzer,
    channels: usize,
    window_samples: usize,
    hop_samples: i64,
    definition_hash: [u8; 32],
    left: Vec<f32>,
    right: Vec<f32>,
    ordered_left: Vec<f32>,
    ordered_right: Vec<f32>,
    write_index: usize,
    filled: usize,
    next_position: Option<i64>,
    generation: u64,
}

impl AttackAssembler {
    pub(super) fn new(analyzer: SuperFluxAnalyzer, channels: usize) -> Self {
        let layout = analyzer.layout();
        let window_samples = layout.window_samples;
        let hop_samples = layout.hop_samples as i64;
        let definition_hash = layout.definition_hash;
        Self {
            analyzer,
            channels,
            window_samples,
            hop_samples,
            definition_hash,
            left: vec![0.0; window_samples],
            right: vec![0.0; window_samples],
            ordered_left: vec![0.0; window_samples],
            ordered_right: vec![0.0; window_samples],
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
            if start == 0 {
                self.prefill_source_origin();
            }
        }
        self.next_position = Some(start);
        true
    }

    pub(super) fn push_frame(&mut self, left: f32, right: Option<f32>) -> Option<AttackOdfFrame> {
        if !left.is_finite()
            || right.is_some_and(|value| !value.is_finite())
            || (self.channels == 2 && right.is_none())
        {
            self.reset();
            return None;
        }
        self.left[self.write_index] = left;
        self.right[self.write_index] = right.unwrap_or(0.0);
        self.write_index = (self.write_index + 1) % self.window_samples;
        self.filled = self.filled.saturating_add(1).min(self.window_samples);
        let end = self.next_position?.checked_add(1)?;
        self.next_position = Some(end);
        let event_sample =
            end.checked_sub((self.window_samples - self.window_samples / 2) as i64)?;
        if self.filled != self.window_samples || event_sample.rem_euclid(self.hop_samples) != 0 {
            return None;
        }
        copy_ordered(&self.left, self.write_index, &mut self.ordered_left);
        let right = if self.channels == 2 {
            copy_ordered(&self.right, self.write_index, &mut self.ordered_right);
            Some(self.ordered_right.as_slice())
        } else {
            None
        };
        let support_start = end.checked_sub(self.window_samples as i64)?;
        self.analyzer
            .analyze_window(&self.ordered_left, right, support_start)
            .ok()
            .flatten()
            .map(|frame| self.wrap(frame))
    }

    pub(super) fn reset(&mut self) {
        self.analyzer.reset();
        self.left.fill(0.0);
        self.right.fill(0.0);
        self.write_index = 0;
        self.filled = 0;
        self.next_position = None;
        self.generation = 0;
    }

    fn prefill_source_origin(&mut self) {
        let padding = self.window_samples / 2;
        self.write_index = padding % self.window_samples;
        self.filled = padding;
    }

    fn wrap(&self, frame: SuperFluxFrame) -> AttackOdfFrame {
        AttackOdfFrame {
            generation: self.generation,
            sample_rate: self.analyzer.layout().sample_rate,
            channels: self.channels as u8,
            definition_hash: self.definition_hash,
            window_samples: self.window_samples as u32,
            hop_samples: self.hop_samples as u32,
            support_start_samples: frame.support_start_samples,
            support_end_samples: frame.support_end_samples,
            event_sample: frame.event_sample,
            value: frame.value,
        }
    }
}

fn copy_ordered(source: &[f32], start: usize, destination: &mut [f32]) {
    let tail = source.len() - start;
    destination[..tail].copy_from_slice(&source[start..]);
    destination[tail..].copy_from_slice(&source[..start]);
}
