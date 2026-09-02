//! Bounded raw-audio history used to attach perceptual detail to confirmed ATTACK events.

use std::collections::VecDeque;

use crate::{
    AttackEventShape, AttackPerceptualFeatures, AttackWaveformPoint, ATTACK_CONTEXT_MICROS,
    ATTACK_DETAIL_MICROS, ATTACK_LEVEL_FLOOR_DBFS, ATTACK_SHAPE_POINT_CAPACITY,
    PERCEPTUAL_PRESENTATION_HZ,
};
use crate::{SharpnessContinuousAnalyzer, SpectrumChannelMode};

use super::state::{AttackDetailedEvent, AttackEvent};

const RETENTION_MICROS: u32 = 250_000;
const WAVEFORM_BIN_MICROS: u32 = 10_000;
const PENDING_EVENT_CAPACITY: usize = 16;
const SHARPNESS_HISTORY_CAPACITY: usize = 8;

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
    waveform_bin_frames: i64,
    waveform_start: Option<i64>,
    waveform_count: i64,
    waveform_power_sum: f64,
    waveform_peak: f64,
    pending_events: VecDeque<AttackEvent>,
    sharpness: Option<SharpnessContinuousAnalyzer>,
    sharpness_aperture_frames: i64,
    sharpness_epoch: Option<i64>,
    sharpness_start: Option<i64>,
    sharpness_samples: Vec<f32>,
    sharpness_history: VecDeque<(i64, f32)>,
}

impl AttackDetailTracker {
    pub(super) fn new(sample_rate: u32, channels: usize) -> Self {
        let capacity = frames_for_micros(sample_rate, RETENTION_MICROS) as usize;
        let context_samples =
            frames_for_micros(sample_rate, ATTACK_CONTEXT_MICROS) as usize * channels;
        let attack_samples =
            frames_for_micros(sample_rate, ATTACK_DETAIL_MICROS) as usize * channels;
        let sharpness_aperture_frames = i64::from(sample_rate / PERCEPTUAL_PRESENTATION_HZ);
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
            waveform_bin_frames: frames_for_micros(sample_rate, WAVEFORM_BIN_MICROS) as i64,
            waveform_start: None,
            waveform_count: 0,
            waveform_power_sum: 0.0,
            waveform_peak: 0.0,
            pending_events: VecDeque::with_capacity(PENDING_EVENT_CAPACITY),
            sharpness: SharpnessContinuousAnalyzer::new(sample_rate, channels).ok(),
            sharpness_aperture_frames,
            sharpness_epoch: None,
            sharpness_start: None,
            sharpness_samples: Vec::with_capacity(
                sharpness_aperture_frames.max(0) as usize * channels,
            ),
            sharpness_history: VecDeque::with_capacity(SHARPNESS_HISTORY_CAPACITY),
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
            self.start_sharpness(start);
        }
        self.next_position = Some(start);
        true
    }

    pub(super) fn push_frame(
        &mut self,
        left: f32,
        right: Option<f32>,
    ) -> Result<Option<AttackWaveformPoint>, ()> {
        if !left.is_finite()
            || right.is_some_and(|value| !value.is_finite())
            || (self.channels == 2 && right.is_none())
        {
            self.reset();
            return Err(());
        }
        let Some(position) = self.next_position else {
            return Err(());
        };
        if self.frames.len() == self.capacity {
            self.frames.pop_front();
        }
        self.frames.push_back(StoredFrame {
            position,
            left,
            right: right.unwrap_or(0.0),
        });
        self.push_sharpness(position, left, right.unwrap_or(0.0));
        self.next_position = position.checked_add(1);
        if self.next_position.is_none() {
            self.reset();
            return Err(());
        }
        Ok(self.push_waveform_frame(position, left, right.unwrap_or(0.0)))
    }

    #[cfg(test)]
    pub(super) fn capture(&mut self, event: AttackEvent) -> Option<AttackDetailedEvent> {
        self.capture_internal(event)
    }

    pub(super) fn queue_event(&mut self, event: AttackEvent) {
        if self.pending_events.len() == PENDING_EVENT_CAPACITY {
            self.pending_events.pop_front();
        }
        self.pending_events.push_back(event);
    }

    pub(super) fn capture_next_ready(&mut self) -> Option<AttackDetailedEvent> {
        let event = *self.pending_events.front()?;
        let attack_end = event
            .event_sample
            .checked_add(frames_for_micros(self.sample_rate, ATTACK_DETAIL_MICROS) as i64)?;
        let ready_at = self
            .sharpness_target(event.event_sample)
            .unwrap_or(attack_end)
            .max(attack_end);
        if self.next_position? < ready_at {
            return None;
        }
        self.pending_events.pop_front();
        self.capture_internal(event)
    }

    fn capture_internal(&mut self, event: AttackEvent) -> Option<AttackDetailedEvent> {
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
        let sharpness = self
            .sharpness_target(event.event_sample)
            .and_then(|target| {
                self.sharpness_history
                    .iter()
                    .rev()
                    .find(|(endpoint, _)| *endpoint == target)
                    .map(|(_, value)| *value)
            });
        let features = AttackPerceptualFeatures::analyze(
            &self.context,
            &self.attack,
            self.sample_rate,
            self.channels,
            sharpness,
        )
        .ok()?;
        let shape = self.event_shape(context_start, attack_end, event.event_sample)?;
        Some(AttackDetailedEvent {
            event,
            features,
            shape,
        })
    }

    pub(super) fn reset(&mut self) {
        self.frames.clear();
        self.generation = 0;
        self.next_position = None;
        self.source_origin_available = false;
        self.context.fill(0.0);
        self.attack.fill(0.0);
        self.reset_waveform();
        self.pending_events.clear();
        self.sharpness_epoch = None;
        self.sharpness_start = None;
        self.sharpness_samples.clear();
        self.sharpness_history.clear();
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

    fn push_waveform_frame(
        &mut self,
        position: i64,
        left: f32,
        right: f32,
    ) -> Option<AttackWaveformPoint> {
        if self.waveform_start.is_none() {
            if position.rem_euclid(self.waveform_bin_frames) != 0 {
                return None;
            }
            self.waveform_start = Some(position);
        }
        let power = if self.channels == 2 {
            (f64::from(left).powi(2) + f64::from(right).powi(2)) * 0.5
        } else {
            f64::from(left).powi(2)
        };
        self.waveform_power_sum += power;
        self.waveform_peak = self.waveform_peak.max(power.sqrt());
        self.waveform_count += 1;
        if self.waveform_count != self.waveform_bin_frames {
            return None;
        }
        let start_sample = self.waveform_start?;
        let end_sample = start_sample.checked_add(self.waveform_bin_frames)?;
        let floor_power = 10.0_f64.powf(f64::from(ATTACK_LEVEL_FLOOR_DBFS) / 10.0);
        let point = AttackWaveformPoint {
            generation: self.generation,
            sample_rate: self.sample_rate,
            channels: self.channels as u8,
            start_sample,
            end_sample,
            peak_linear: self.waveform_peak as f32,
            rms_dbfs: (10.0
                * (self.waveform_power_sum / self.waveform_count as f64)
                    .max(floor_power)
                    .log10()) as f32,
        };
        self.reset_waveform();
        point.has_valid_layout().then_some(point)
    }

    fn reset_waveform(&mut self) {
        self.waveform_start = None;
        self.waveform_count = 0;
        self.waveform_power_sum = 0.0;
        self.waveform_peak = 0.0;
    }

    fn start_sharpness(&mut self, start: i64) {
        let Some(analyzer) = self.sharpness.as_mut() else {
            return;
        };
        if self.sharpness_aperture_frames <= 0 {
            return;
        }
        let remainder = start.rem_euclid(self.sharpness_aperture_frames);
        let aligned = if remainder == 0 {
            start
        } else {
            start + self.sharpness_aperture_frames - remainder
        };
        if analyzer.reset_at_epoch(aligned).is_ok() {
            self.sharpness_epoch = Some(aligned);
            self.sharpness_start = Some(aligned);
        }
    }

    fn push_sharpness(&mut self, position: i64, left: f32, right: f32) {
        let Some(start) = self.sharpness_start else {
            return;
        };
        if position < start {
            return;
        }
        let frame_offset = self.sharpness_samples.len() / self.channels;
        if position != start + frame_offset as i64 {
            return;
        }
        self.sharpness_samples.push(left);
        if self.channels == 2 {
            self.sharpness_samples.push(right);
        }
        if frame_offset as i64 + 1 != self.sharpness_aperture_frames {
            return;
        }
        let end = start + self.sharpness_aperture_frames;
        let sharpness = self.sharpness.as_mut().and_then(|analyzer| {
            analyzer
                .analyze_aperture(
                    &self.sharpness_samples,
                    SpectrumChannelMode::Lr,
                    end,
                    self.generation,
                )
                .ok()?
                .last()
                .map(|frame| frame.sharpness as f32)
        });
        if let Some(value) = sharpness.filter(|value| value.is_finite() && *value >= 0.0) {
            if self.sharpness_history.len() == SHARPNESS_HISTORY_CAPACITY {
                self.sharpness_history.pop_front();
            }
            self.sharpness_history.push_back((end, value));
        }
        self.sharpness_samples.clear();
        self.sharpness_start = Some(end);
    }

    fn sharpness_target(&self, event_sample: i64) -> Option<i64> {
        let epoch = self.sharpness_epoch?;
        let ready = event_sample
            .checked_add(frames_for_micros(self.sample_rate, ATTACK_DETAIL_MICROS) as i64)?;
        if ready <= epoch || self.sharpness_aperture_frames <= 0 {
            return None;
        }
        let delta = ready.checked_sub(epoch)?;
        let slots =
            delta.checked_add(self.sharpness_aperture_frames - 1)? / self.sharpness_aperture_frames;
        epoch.checked_add(slots.checked_mul(self.sharpness_aperture_frames)?)
    }

    fn event_shape(
        &self,
        start_sample: i64,
        end_sample: i64,
        event_sample: i64,
    ) -> Option<AttackEventShape> {
        let total_frames = self.context.len() / self.channels + self.attack.len() / self.channels;
        let mut points = [0.0_f32; ATTACK_SHAPE_POINT_CAPACITY];
        for (point_index, point) in points.iter_mut().enumerate() {
            let start = point_index * total_frames / ATTACK_SHAPE_POINT_CAPACITY;
            let end =
                ((point_index + 1) * total_frames / ATTACK_SHAPE_POINT_CAPACITY).max(start + 1);
            let mut peak_power = 0.0_f64;
            for frame in start..end.min(total_frames) {
                let samples = if frame < self.context.len() / self.channels {
                    let offset = frame * self.channels;
                    &self.context[offset..offset + self.channels]
                } else {
                    let offset = (frame - self.context.len() / self.channels) * self.channels;
                    &self.attack[offset..offset + self.channels]
                };
                let power = samples
                    .iter()
                    .map(|sample| f64::from(*sample).powi(2))
                    .sum::<f64>()
                    / self.channels as f64;
                peak_power = peak_power.max(power);
            }
            *point = peak_power.sqrt() as f32;
        }
        let shape = AttackEventShape {
            start_sample,
            end_sample,
            event_sample,
            points,
        };
        shape.has_valid_layout().then_some(shape)
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
