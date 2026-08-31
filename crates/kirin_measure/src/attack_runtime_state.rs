use std::collections::VecDeque;

use crate::AttackPerceptualFeatures;

pub const ATTACK_ODF_HISTORY_CAPACITY: usize = 1_200;
pub const ATTACK_EVENT_HISTORY_CAPACITY: usize = 240;

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

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackEvent {
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub definition_hash: [u8; 32],
    pub event_sample: i64,
    pub decision_sample: i64,
    pub value: f32,
}

impl AttackEvent {
    pub(crate) fn from_odf(frame: AttackOdfFrame) -> Self {
        Self {
            generation: frame.generation,
            sample_rate: frame.sample_rate,
            channels: frame.channels,
            definition_hash: frame.definition_hash,
            event_sample: frame.event_sample,
            decision_sample: i64::MIN,
            value: frame.value,
        }
    }

    pub(crate) fn decided_at(mut self, decision_sample: i64) -> Self {
        self.decision_sample = decision_sample;
        self
    }

    pub fn has_valid_layout(&self) -> bool {
        self.generation > 0
            && self.sample_rate > 0
            && matches!(self.channels, 1 | 2)
            && self.decision_sample >= self.event_sample
            && self.value.is_finite()
            && self.value >= 0.0
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackDetailedEvent {
    pub event: AttackEvent,
    pub features: AttackPerceptualFeatures,
}

impl AttackDetailedEvent {
    pub fn has_valid_layout(&self) -> bool {
        self.event.has_valid_layout()
            && self.features.has_valid_layout()
            && self.event.sample_rate == self.features.sample_rate
            && self.event.channels == self.features.channels
    }
}

#[derive(Clone, Debug)]
pub struct AttackHistory {
    frames: VecDeque<AttackOdfFrame>,
    events: VecDeque<AttackEvent>,
    details: VecDeque<AttackDetailedEvent>,
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
            events: VecDeque::with_capacity(ATTACK_EVENT_HISTORY_CAPACITY),
            details: VecDeque::with_capacity(ATTACK_EVENT_HISTORY_CAPACITY),
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
            self.events.clear();
            self.details.clear();
        }
        if self.frames.len() == ATTACK_ODF_HISTORY_CAPACITY {
            self.frames.pop_front();
        }
        self.frames.push_back(frame);
    }

    pub(crate) fn push_event(&mut self, event: AttackEvent) {
        if !event.has_valid_layout() {
            return;
        }
        if self.frames.back().is_none_or(|newest| {
            newest.generation != event.generation
                || newest.definition_hash != event.definition_hash
                || newest.sample_rate != event.sample_rate
                || newest.channels != event.channels
        }) {
            return;
        }
        if self.events.len() == ATTACK_EVENT_HISTORY_CAPACITY {
            self.events.pop_front();
        }
        self.events.push_back(event);
    }

    pub(crate) fn push_detail(&mut self, detail: AttackDetailedEvent) {
        if !detail.has_valid_layout()
            || self
                .events
                .back()
                .is_none_or(|event| *event != detail.event)
        {
            return;
        }
        if self.details.len() == ATTACK_EVENT_HISTORY_CAPACITY {
            self.details.pop_front();
        }
        self.details.push_back(detail);
    }

    pub fn newest(&self) -> Option<&AttackOdfFrame> {
        self.frames.back()
    }

    pub fn frames(&self) -> impl DoubleEndedIterator<Item = &AttackOdfFrame> + ExactSizeIterator {
        self.frames.iter()
    }

    pub fn events(&self) -> impl DoubleEndedIterator<Item = &AttackEvent> + ExactSizeIterator {
        self.events.iter()
    }

    pub fn details(
        &self,
    ) -> impl DoubleEndedIterator<Item = &AttackDetailedEvent> + ExactSizeIterator {
        self.details.iter()
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
