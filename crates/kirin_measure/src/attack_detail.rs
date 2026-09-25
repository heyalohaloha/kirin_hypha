//! Content-grid bins and queued events: attaches measured detail to confirmed ATTACK events.
//!
//! Each sample updates one about-1 ms bin and the 10 ms waveform. At the end of every block the
//! bins and the block's Phase D Sharpness frames move to the runtime's shared bin history, where
//! this worker measures its own hits and the POST coordinator measures POST at PRE onsets.

use std::collections::VecDeque;
use std::sync::Mutex;

use crate::attack_perception::attack_bin_frames;
use crate::{AttackWaveformPoint, ATTACK_BODY_BINS, ATTACK_HEAD_BINS, ATTACK_LEVEL_FLOOR_DBFS};

use super::bins::{AttackBins, Bin};
use super::sharpness::{AttackSharpnessStream, SharpnessFrame};
use super::state::{AttackDetailedEvent, AttackEvent};

const WAVEFORM_BIN_MICROS: u32 = 10_000;
const PENDING_EVENT_CAPACITY: usize = 32;

pub(super) struct AttackDetailTracker {
    channels: usize,
    bin_frames: i64,
    generation: u64,
    next_position: Option<i64>,
    /// A run started in this block; the shared bins restart at the next flush.
    restart: Option<i64>,
    partial: Bin,
    partial_open: bool,
    completed: Vec<(i64, Bin)>,
    block_start: i64,
    block: Vec<f32>,
    sharpness: Option<AttackSharpnessStream>,
    frames: Vec<SharpnessFrame>,
    waveform_bin_frames: i64,
    waveform_start: Option<i64>,
    waveform_count: i64,
    waveform_power_sum: f64,
    waveform_peak: f64,
    pending_events: VecDeque<AttackEvent>,
    decided_before: Option<i64>,
    sample_rate: u32,
}

impl AttackDetailTracker {
    pub(super) fn new(sample_rate: u32, channels: usize) -> Self {
        Self {
            channels,
            bin_frames: attack_bin_frames(sample_rate),
            generation: 0,
            next_position: None,
            restart: None,
            partial: Bin::default(),
            partial_open: false,
            completed: Vec::new(),
            block_start: 0,
            block: Vec::new(),
            sharpness: AttackSharpnessStream::new(sample_rate, channels),
            frames: Vec::new(),
            waveform_bin_frames: frames_for_micros(sample_rate, WAVEFORM_BIN_MICROS),
            waveform_start: None,
            waveform_count: 0,
            waveform_power_sum: 0.0,
            waveform_peak: 0.0,
            pending_events: VecDeque::with_capacity(PENDING_EVENT_CAPACITY),
            decided_before: None,
            sample_rate,
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
            self.restart = Some(start);
            if let Some(stream) = self.sharpness.as_mut() {
                stream.reset(start);
            }
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
        let right = right.unwrap_or(0.0);
        let power = if self.channels == 2 {
            (f64::from(left).powi(2) + f64::from(right).powi(2)) * 0.5
        } else {
            f64::from(left).powi(2)
        };
        self.push_bin(position, power, left.abs().max(right.abs()));
        if self.block.is_empty() {
            self.block_start = position;
        }
        self.block.push(left);
        if self.channels == 2 {
            self.block.push(right);
        }
        self.next_position = position.checked_add(1);
        if self.next_position.is_none() {
            self.reset();
            return Err(());
        }
        Ok(self.push_waveform_frame(position, power))
    }

    pub(super) fn queue_event(&mut self, event: AttackEvent) {
        if self.pending_events.len() == PENDING_EVENT_CAPACITY {
            self.pending_events.pop_front();
        }
        self.pending_events.push_back(event);
    }

    /// Every onset before `sample` has been decided by the peak picker.
    pub(super) fn note_decided_before(&mut self, sample: i64) {
        self.decided_before = Some(sample);
    }

    /// Move this block's bins and Sharpness frames to the shared history, then measure every
    /// queued hit whose windows and next onset are final. The body ends at the next onset.
    pub(super) fn flush(&mut self, shared: &Mutex<AttackBins>) -> Vec<AttackDetailedEvent> {
        self.frames.clear();
        let sharpness_failed = self
            .sharpness
            .as_mut()
            .is_some_and(|stream| !stream.push(self.block_start, &self.block, &mut self.frames));
        self.block.clear();
        let mut bins = match shared.lock() {
            Ok(bins) => bins,
            Err(poisoned) => poisoned.into_inner(),
        };
        if let Some(start) = self.restart.take() {
            let epoch = self.sharpness.as_ref().map(AttackSharpnessStream::epoch);
            bins.begin_run(start, self.generation, epoch);
        }
        for (index, bin) in self.completed.drain(..) {
            bins.push_level(index, bin);
        }
        if sharpness_failed {
            self.sharpness = None;
            bins.disable_sharpness();
        } else if let Some(stream) = self.sharpness.as_ref() {
            bins.push_sharpness(&self.frames, stream.next_frame_sample());
        }
        let mut details = Vec::new();
        while let Some(&event) = self.pending_events.front() {
            let limit = (event.event_sample.div_euclid(self.bin_frames)
                + ATTACK_HEAD_BINS
                + ATTACK_BODY_BINS)
                * self.bin_frames;
            if !bins.ready_for(event.event_sample)
                || self.decided_before.is_none_or(|decided| decided < limit)
            {
                break;
            }
            self.pending_events.pop_front();
            let next = self.pending_events.front().map(|next| next.event_sample);
            let body_end = bins.body_end_sample(event.event_sample, next);
            if let Some((features, shape)) = bins.measure(event, body_end) {
                let detail = AttackDetailedEvent {
                    event,
                    features,
                    shape,
                };
                if detail.has_valid_layout() {
                    details.push(detail);
                }
            }
        }
        details
    }

    pub(super) fn reset(&mut self) {
        self.generation = 0;
        self.next_position = None;
        self.restart = None;
        self.partial_open = false;
        self.completed.clear();
        self.block.clear();
        self.frames.clear();
        self.reset_waveform();
        self.pending_events.clear();
        self.decided_before = None;
    }

    /// Bins start at whole content-grid boundaries; a run that starts mid-bin skips that bin.
    fn push_bin(&mut self, position: i64, power: f64, peak: f32) {
        let offset = position.rem_euclid(self.bin_frames);
        if offset == 0 {
            self.partial = Bin::default();
            self.partial_open = true;
        }
        if !self.partial_open {
            return;
        }
        self.partial.power += power;
        self.partial.peak_sample = self.partial.peak_sample.max(peak);
        self.partial.peak_frame = self.partial.peak_frame.max(power.sqrt() as f32);
        if offset == self.bin_frames - 1 {
            self.completed
                .push((position.div_euclid(self.bin_frames), self.partial));
            self.partial_open = false;
        }
    }

    fn push_waveform_frame(&mut self, position: i64, power: f64) -> Option<AttackWaveformPoint> {
        if self.waveform_start.is_none() {
            if position.rem_euclid(self.waveform_bin_frames) != 0 {
                return None;
            }
            self.waveform_start = Some(position);
        }
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
}

fn frames_for_micros(sample_rate: u32, micros: u32) -> i64 {
    ((u64::from(sample_rate) * u64::from(micros) + 500_000) / 1_000_000) as i64
}

#[cfg(test)]
#[path = "attack_detail_tests.rs"]
mod tests;
