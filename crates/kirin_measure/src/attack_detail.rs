//! Bounded raw-audio history used to attach perceptual detail to confirmed ATTACK events.

use std::collections::VecDeque;

use crate::{AttackPerceptualFeatures, ATTACK_CONTEXT_MICROS, ATTACK_DETAIL_MICROS};

use super::state::{AttackDetailedEvent, AttackEvent};

const RETENTION_MICROS: u32 = 200_000;

#[derive(Clone, Copy, Debug)]
struct StoredFrame {
    position: i64,
    left: f32,
    right: f32,
}

pub(super) struct AttackDetailTracker {
    sample_rate: u32,
    channels: usize,
    capacity: usize,
    frames: VecDeque<StoredFrame>,
    generation: u64,
    next_position: Option<i64>,
    source_origin_available: bool,
    context: Vec<f32>,
    attack: Vec<f32>,
}

impl AttackDetailTracker {
    pub(super) fn new(sample_rate: u32, channels: usize) -> Self {
        let capacity = frames_for_micros(sample_rate, RETENTION_MICROS) as usize;
        let context_samples =
            frames_for_micros(sample_rate, ATTACK_CONTEXT_MICROS) as usize * channels;
        let attack_samples =
            frames_for_micros(sample_rate, ATTACK_DETAIL_MICROS) as usize * channels;
        Self {
            sample_rate,
            channels,
            capacity,
            frames: VecDeque::with_capacity(capacity),
            generation: 0,
            next_position: None,
            source_origin_available: false,
            context: vec![0.0; context_samples],
            attack: vec![0.0; attack_samples],
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
            self.source_origin_available = start == 0;
        }
        self.next_position = Some(start);
        true
    }

    pub(super) fn push_frame(&mut self, left: f32, right: Option<f32>) -> bool {
        if !left.is_finite()
            || right.is_some_and(|value| !value.is_finite())
            || (self.channels == 2 && right.is_none())
        {
            self.reset();
            return false;
        }
        let Some(position) = self.next_position else {
            return false;
        };
        if self.frames.len() == self.capacity {
            self.frames.pop_front();
        }
        self.frames.push_back(StoredFrame {
            position,
            left,
            right: right.unwrap_or(0.0),
        });
        self.next_position = position.checked_add(1);
        self.next_position.is_some()
    }

    pub(super) fn capture(&mut self, event: AttackEvent) -> Option<AttackDetailedEvent> {
        if !event.has_valid_layout()
            || event.generation != self.generation
            || event.sample_rate != self.sample_rate
            || event.channels as usize != self.channels
        {
            return None;
        }
        let context_frames = frames_for_micros(self.sample_rate, ATTACK_CONTEXT_MICROS) as i64;
        let attack_frames = frames_for_micros(self.sample_rate, ATTACK_DETAIL_MICROS) as i64;
        let context_start = event.event_sample.checked_sub(context_frames)?;
        let attack_end = event.event_sample.checked_add(attack_frames)?;
        if self.next_position? < attack_end || (context_start < 0 && !self.source_origin_available)
        {
            return None;
        }
        self.fill_window(context_start, event.event_sample, true)?;
        self.fill_window(event.event_sample, attack_end, false)?;
        let features = AttackPerceptualFeatures::analyze(
            &self.context,
            &self.attack,
            self.sample_rate,
            self.channels,
            None,
        )
        .ok()?;
        Some(AttackDetailedEvent { event, features })
    }

    pub(super) fn reset(&mut self) {
        self.frames.clear();
        self.generation = 0;
        self.next_position = None;
        self.source_origin_available = false;
        self.context.fill(0.0);
        self.attack.fill(0.0);
    }

    fn fill_window(&mut self, start: i64, end: i64, context: bool) -> Option<()> {
        let channels = self.channels;
        let destination = if context {
            &mut self.context
        } else {
            &mut self.attack
        };
        let expected_frames = usize::try_from(end.checked_sub(start)?).ok()?;
        if destination.len() != expected_frames.checked_mul(channels)? {
            return None;
        }
        for (offset, position) in (start..end).enumerate() {
            let destination_offset = offset * channels;
            if position < 0 {
                destination[destination_offset..destination_offset + channels].fill(0.0);
                continue;
            }
            let frame = frame_at(&self.frames, position)?;
            destination[destination_offset] = frame.left;
            if channels == 2 {
                destination[destination_offset + 1] = frame.right;
            }
        }
        Some(())
    }
}

fn frame_at(frames: &VecDeque<StoredFrame>, position: i64) -> Option<StoredFrame> {
    let first = frames.front()?.position;
    let index = usize::try_from(position.checked_sub(first)?).ok()?;
    let frame = *frames.get(index)?;
    (frame.position == position).then_some(frame)
}

fn frames_for_micros(sample_rate: u32, micros: u32) -> u64 {
    (u64::from(sample_rate) * u64::from(micros) + 500_000) / 1_000_000
}

#[cfg(test)]
#[path = "attack_detail_tests.rs"]
mod tests;
