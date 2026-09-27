//! About 1 ms content-grid bins of level, peak and Sharpness frames: the only data every ATTACK
//! window reads. PRE and POST share the content sample grid, so one onset gives both sides the
//! same windows. Bins reach back 7 s, so POST can be measured at a PRE onset after the exchange.

use std::collections::vec_deque::Iter;
use std::collections::VecDeque;

use super::sharpness::{SharpnessFrame, SHARPNESS_MIN_LOUDNESS_SONE};
use super::state::{AttackEvent, AttackEventShape};
use crate::attack_perception::attack_bin_frames;
use crate::{
    AttackPerceptualFeatures, ATTACK_BODY_BINS, ATTACK_HEAD_BINS, ATTACK_LEVEL_FLOOR_DBFS,
    ATTACK_MIN_BODY_BINS, ATTACK_SHAPE_LEAD_BINS, ATTACK_SHAPE_POINT_CAPACITY,
    ATTACK_SHARPNESS_BINS,
};

const RETENTION_BINS: usize = 7_000;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub(super) struct Bin {
    /// Sum over the bin's frames of the mean-over-channels sample power.
    pub(super) power: f64,
    /// Largest absolute sample of any channel.
    pub(super) peak_sample: f32,
    /// Largest square root of a frame's mean-over-channels power (the waveform envelope).
    pub(super) peak_frame: f32,
    /// Per channel: sum of Sharpness x loudness over the Phase D frames in this bin.
    pub(super) sharpness: [f64; 2],
    /// Per channel: sum of filtered loudness over the same frames.
    pub(super) loudness: [f64; 2],
}

pub(super) struct AttackBins {
    sample_rate: u32,
    channels: usize,
    bin_frames: i64,
    generation: u64,
    first: i64,
    bins: VecDeque<Bin>,
    /// First bin whose windows read Sharpness in this run; `None` when the rate has none.
    sharpness_start: Option<i64>,
    /// Bins before this index have every Phase D frame.
    sharpness_end: i64,
    /// Phase D frames of bins that are not complete yet; applied once their bin arrives.
    waiting: Vec<SharpnessFrame>,
}

impl AttackBins {
    pub(super) fn new(sample_rate: u32, channels: usize) -> Self {
        Self {
            sample_rate,
            channels,
            bin_frames: attack_bin_frames(sample_rate),
            generation: 0,
            first: 0,
            bins: VecDeque::with_capacity(RETENTION_BINS),
            sharpness_start: None,
            sharpness_end: i64::MIN,
            waiting: Vec::new(),
        }
    }

    /// Start a continuous run. Bins are complete from the first whole bin at or after `start`;
    /// windows read Sharpness from the first whole bin at or after `sharpness_from`, where the
    /// Phase D frames exist and have settled. Without a Sharpness stream every window is complete
    /// as soon as its level bins are.
    pub(super) fn begin_run(&mut self, start: i64, generation: u64, sharpness_from: Option<i64>) {
        self.generation = generation;
        self.first = self.whole_bin_from(start);
        self.bins.clear();
        self.sharpness_start = sharpness_from.map(|from| self.whole_bin_from(from));
        self.sharpness_end = i64::MIN;
        self.waiting.clear();
    }

    pub(super) fn clear(&mut self) {
        self.generation = 0;
        self.bins.clear();
        self.sharpness_start = None;
        self.waiting.clear();
    }

    fn whole_bin_from(&self, sample: i64) -> i64 {
        sample.div_euclid(self.bin_frames) + i64::from(sample.rem_euclid(self.bin_frames) != 0)
    }

    /// The Sharpness stream failed: windows complete without it for the rest of the run.
    pub(super) fn disable_sharpness(&mut self) {
        self.sharpness_start = None;
    }

    fn end(&self) -> i64 {
        self.first + self.bins.len() as i64
    }

    /// Append the next complete bin of the current run; out-of-order bins are refused.
    pub(super) fn push_level(&mut self, index: i64, bin: Bin) {
        if index < self.end() {
            return;
        }
        if index != self.end() {
            self.bins.clear();
            self.first = index;
        }
        if self.bins.len() == RETENTION_BINS {
            self.bins.pop_front();
            self.first += 1;
        }
        self.bins.push_back(bin);
    }

    /// Add Phase D frames and record the bin index before which every frame has arrived. A frame
    /// whose bin is not complete yet waits for it; a frame before the retained bins is dropped.
    pub(super) fn push_sharpness(&mut self, frames: &[SharpnessFrame], next_frame_sample: i64) {
        let mut waiting = std::mem::take(&mut self.waiting);
        waiting.extend_from_slice(frames);
        let end = self.end();
        waiting.retain(|frame| {
            let index = frame.source_sample.div_euclid(self.bin_frames);
            if index >= end {
                return true;
            }
            if let Some(bin) = index
                .checked_sub(self.first)
                .and_then(|offset| usize::try_from(offset).ok())
                .and_then(|offset| self.bins.get_mut(offset))
            {
                for channel in 0..self.channels {
                    if frame.loudness[channel] >= SHARPNESS_MIN_LOUDNESS_SONE {
                        bin.sharpness[channel] +=
                            frame.sharpness[channel] * frame.loudness[channel];
                        bin.loudness[channel] += frame.loudness[channel];
                    }
                }
            }
            false
        });
        self.waiting = waiting;
        self.sharpness_end = next_frame_sample.div_euclid(self.bin_frames);
    }

    /// True once the head, body and Sharpness bins of an onset are all complete.
    pub(super) fn ready_for(&self, onset: i64) -> bool {
        let start = onset.div_euclid(self.bin_frames);
        self.end() >= start + ATTACK_HEAD_BINS + ATTACK_BODY_BINS
            && (self.sharpness_start.is_none()
                || self.sharpness_end >= start + ATTACK_SHARPNESS_BINS)
    }

    /// The exclusive body end for an onset: 100 ms after the head, or the bin of the next onset
    /// when that comes first, never inside the head. Both PRE and POST read this one sample.
    pub(super) fn body_end_sample(&self, onset: i64, next_onset: Option<i64>) -> i64 {
        let head_end = onset.div_euclid(self.bin_frames) + ATTACK_HEAD_BINS;
        let limit = head_end + ATTACK_BODY_BINS;
        let end = next_onset
            .map(|next| next.div_euclid(self.bin_frames))
            .filter(|next| *next < limit)
            .unwrap_or(limit);
        end.max(head_end) * self.bin_frames
    }

    /// Measure the windows of `event` against this run's bins. With `body_end_sample` the hit is
    /// complete: its body ends there and every window must be final. Without it only the head is
    /// measured, while the body is not final or after its audio stopped. `None` when a required
    /// bin is not retained.
    pub(super) fn measure(
        &self,
        event: AttackEvent,
        body_end_sample: Option<i64>,
    ) -> Option<(AttackPerceptualFeatures, AttackEventShape)> {
        if event.generation != self.generation
            || event.sample_rate != self.sample_rate
            || usize::from(event.channels) != self.channels
        {
            return None;
        }
        let start = event.event_sample.div_euclid(self.bin_frames);
        let head_end = start + ATTACK_HEAD_BINS;
        let body_end = match body_end_sample {
            Some(sample) => {
                if !self.ready_for(event.event_sample) || sample.rem_euclid(self.bin_frames) != 0 {
                    return None;
                }
                let end = sample.div_euclid(self.bin_frames);
                if end < head_end || end > head_end + ATTACK_BODY_BINS {
                    return None;
                }
                end
            }
            None => head_end,
        };
        let complete = body_end_sample.is_some();
        let head = self.range(start, head_end)?;
        let floor_power = 10.0_f64.powf(f64::from(ATTACK_LEVEL_FLOOR_DBFS) / 10.0);
        let rms_dbfs = |bins: Iter<'_, Bin>| {
            let frames = bins.len() as f64 * self.bin_frames as f64;
            let power = bins.map(|bin| bin.power).sum::<f64>() / frames;
            (10.0 * power.max(floor_power).log10()) as f32
        };
        let attack_rms_dbfs = rms_dbfs(head.clone());
        let peak = head.fold(0.0_f32, |peak, bin| peak.max(bin.peak_sample));
        let sample_peak_dbfs = (20.0 * f64::from(peak).max(floor_power.sqrt()).log10()) as f32;
        let body_rms_dbfs = (body_end - head_end >= ATTACK_MIN_BODY_BINS)
            .then(|| self.range(head_end, body_end).map(rms_dbfs))
            .flatten();
        let features = AttackPerceptualFeatures {
            sample_rate: self.sample_rate,
            channels: self.channels as u8,
            bin_frames: self.bin_frames as u32,
            window_start_sample: start * self.bin_frames,
            attack_rms_dbfs,
            sample_peak_dbfs,
            crest_db: sample_peak_dbfs - attack_rms_dbfs,
            complete,
            body_end_sample: body_end * self.bin_frames,
            body_rms_dbfs,
            transient_db: body_rms_dbfs.map(|body| attack_rms_dbfs - body),
            sharpness_acum: complete.then(|| self.sharpness(start)).flatten(),
        };
        let shape = self.shape(event.event_sample, start)?;
        Some((features, shape))
    }

    fn range(&self, from: i64, to: i64) -> Option<Iter<'_, Bin>> {
        let offset = usize::try_from(from.checked_sub(self.first)?).ok()?;
        let count = usize::try_from(to.checked_sub(from)?).ok()?;
        (offset + count <= self.bins.len()).then(|| self.bins.range(offset..offset + count))
    }

    /// Loudness-weighted Sharpness of the 100 ms from the window start, averaged over the
    /// channels that are loud enough to have one. A window that starts before the run's first
    /// Phase D frame has none.
    fn sharpness(&self, start: i64) -> Option<f32> {
        if self.sharpness_start.is_none_or(|first| start < first) {
            return None;
        }
        let bins = self.range(start, start + ATTACK_SHARPNESS_BINS)?;
        let mut sum = 0.0;
        let mut measured = 0;
        for channel in 0..self.channels {
            let loudness = bins.clone().map(|bin| bin.loudness[channel]).sum::<f64>();
            if loudness > 0.0 {
                sum += bins.clone().map(|bin| bin.sharpness[channel]).sum::<f64>() / loudness;
                measured += 1;
            }
        }
        (measured > 0).then(|| (sum / f64::from(measured)) as f32)
    }

    /// Frame-envelope peaks over the measured part of [window start - 20 ms, window start +
    /// 130 ms). Bins before the run or already dropped, and bins not measured yet, are left out
    /// instead of drawn as silence; the 96 points spread over the measured span.
    fn shape(&self, event_sample: i64, start: i64) -> Option<AttackEventShape> {
        let from = (start - ATTACK_SHAPE_LEAD_BINS).max(self.first);
        let to = (start + ATTACK_HEAD_BINS + ATTACK_BODY_BINS).min(self.end());
        let total = usize::try_from(to - from).ok().filter(|total| *total > 0)?;
        let mut points = [0.0_f32; ATTACK_SHAPE_POINT_CAPACITY];
        for (index, point) in points.iter_mut().enumerate() {
            let first = index * total / ATTACK_SHAPE_POINT_CAPACITY;
            let last = ((index + 1) * total / ATTACK_SHAPE_POINT_CAPACITY).max(first + 1);
            *point = self
                .range(from + first as i64, from + last as i64)?
                .fold(0.0_f32, |peak, bin| peak.max(bin.peak_frame));
        }
        let shape = AttackEventShape {
            start_sample: from * self.bin_frames,
            end_sample: to * self.bin_frames,
            event_sample,
            points,
        };
        shape.has_valid_layout().then_some(shape)
    }
}

#[cfg(test)]
#[path = "attack_bins_tests.rs"]
mod tests;
