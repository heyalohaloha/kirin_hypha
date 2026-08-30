use std::collections::VecDeque;

pub const ATTACK_ODF_HISTORY_CAPACITY: usize = 1_200;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackOdfFrame {
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub definition_hash: [u8; 32],
    pub window_samples: u32,
    pub hop_samples: u32,
    pub support_start_samples: i64,
    pub support_end_samples: i64,
    pub event_sample: i64,
    pub value: f32,
}

impl AttackOdfFrame {
    pub fn has_valid_layout(&self) -> bool {
        let Some(support_length) = self
            .support_end_samples
            .checked_sub(self.support_start_samples)
        else {
            return false;
        };
        let Some(expected_event) = self
            .support_start_samples
            .checked_add(i64::from(self.window_samples / 2))
        else {
            return false;
        };
        self.generation > 0
            && self.sample_rate > 0
            && matches!(self.channels, 1 | 2)
            && self.window_samples > 0
            && self.hop_samples > 0
            && support_length == i64::from(self.window_samples)
            && self.event_sample == expected_event
            && self.value.is_finite()
            && self.value >= 0.0
    }
}

#[derive(Clone, Debug)]
pub struct AttackHistory {
    frames: VecDeque<AttackOdfFrame>,
}

impl Default for AttackHistory {
    fn default() -> Self {
        Self::with_capacity()
    }
}

impl AttackHistory {
    pub(crate) fn with_capacity() -> Self {
        Self {
            frames: VecDeque::with_capacity(ATTACK_ODF_HISTORY_CAPACITY),
        }
    }

    pub(crate) fn push(&mut self, frame: AttackOdfFrame) {
        if !frame.has_valid_layout() {
            return;
        }
        if self.frames.back().is_some_and(|newest| {
            newest.generation != frame.generation
                || newest.definition_hash != frame.definition_hash
                || newest.sample_rate != frame.sample_rate
                || newest.channels != frame.channels
        }) {
            self.frames.clear();
        }
        if self.frames.len() == ATTACK_ODF_HISTORY_CAPACITY {
            self.frames.pop_front();
        }
        self.frames.push_back(frame);
    }

    pub fn newest(&self) -> Option<&AttackOdfFrame> {
        self.frames.back()
    }

    pub fn frames(&self) -> impl DoubleEndedIterator<Item = &AttackOdfFrame> + ExactSizeIterator {
        self.frames.iter()
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct AttackRuntimeStats {
    pub enabled: bool,
    pub worker_running: bool,
    pub channels: u8,
    pub pushed_blocks: u64,
    pub dropped_blocks: u64,
    pub analyzed_frames: u64,
}
