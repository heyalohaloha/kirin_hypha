//! Fixed-capacity, multi-resolution TIME history for the always-on Meter Session.

use std::collections::VecDeque;
#[path = "meter_history_bucket.rs"]
mod bucket;
use bucket::BucketAccumulator;
#[path = "time_history_access.rs"]
mod time_access;
pub use time_access::reduce_time_history;

pub use crate::meter_history_decimation::TimeHistoryCountOverflow;
use crate::meter_history_decimation::{checked_decimate_history, decimate_history};
use crate::{CaptureClockSource, MeasureResult};

pub const HISTORY_10_HZ_CAPACITY: usize = 10 * 60 * 10;
pub const HISTORY_1_HZ_CAPACITY: usize = 2 * 60 * 60;
pub const HISTORY_0_1_HZ_CAPACITY: usize = 24 * 60 * 6;
/// Persisted TIME history is currently a mono/stereo/exact-5.1 product surface.
///
/// Keep this separate from the 16-slot public ABI: reserving future ABI capacity in every
/// preallocated history entry would cost roughly 40 MiB across 24 PRE/POST instances without
/// measuring another channel. Wider layouts must first define their product and memory contract.
pub const METER_HISTORY_CHANNELS: usize = 6;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MeterHistoryResolution {
    Hz10 = 0,
    Hz1 = 1,
    Hz0_1 = 2,
}

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct MeterHistoryRange {
    pub min: Option<f64>,
    pub max: Option<f64>,
    pub mean: Option<f64>,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct MeterHistoryEntry {
    pub resolution: MeterHistoryResolution,
    /// どの測定区間の点か。`generation` も `run_id` も session ごとに 1 から数え直すので、
    /// 別 layout / rate で作り直した engine の最初の行は、前の区間の最初の行と同じ番号になる。
    /// **区間をまたいだ継続かどうかを言えるのはこの値だけである**（D-12 / 棚卸し §16.5）。
    pub measurement_epoch: u64,
    pub generation: u64,
    pub run_id: u64,
    pub observation_count: u16,
    /// Exact TIME segment; zero marks a legacy bucket containing multiple segments.
    /// A mixed bucket must be refined from retained exact facts before TIME export.
    pub segment_id: u64,
    pub connects_previous: bool,
    /// M, S, TP, CORR, PSR.
    pub valid_count: [u16; 5],
    pub first_observed_frames: u64,
    pub last_observed_frames: u64,
    pub first_timeline_endpoint_samples: Option<i64>,
    pub last_timeline_endpoint_samples: Option<i64>,
    pub timeline_source: CaptureClockSource,
    /// New contiguous sample-clip runs first observed inside this history point, per input role.
    pub clip_event_count: [u32; METER_HISTORY_CHANNELS],
    pub lufs_m: MeterHistoryRange,
    pub lufs_s: MeterHistoryRange,
    pub true_peak: MeterHistoryRange,
    pub correlation: MeterHistoryRange,
    /// The engine's PSR at this point: 400 ms sample peak against the 3 s Short-term loudness.
    /// It follows the music. PLR is a whole-session fact read from the session snapshot, so
    /// it is not repeated in every point.
    pub psr: MeterHistoryRange,
}

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct MeterHistoryAux {
    pub correlation: Option<f64>,
    pub clip_event_count: [u32; METER_HISTORY_CHANNELS],
}

impl MeterHistoryEntry {
    fn exact(
        measurement_epoch: u64,
        generation: u64,
        run_id: u64,
        observed_frames: u64,
        timeline: (Option<i64>, CaptureClockSource),
        current: &MeasureResult,
        aux: MeterHistoryAux,
    ) -> Self {
        Self {
            resolution: MeterHistoryResolution::Hz10,
            measurement_epoch,
            generation,
            run_id,
            observation_count: 1,
            segment_id: 0,
            connects_previous: false,
            valid_count: [
                current.lufs_m,
                current.lufs_s,
                current.true_peak,
                aux.correlation,
                current.psr,
            ]
            .map(|v| u16::from(v.is_some_and(f64::is_finite))),
            first_observed_frames: observed_frames,
            last_observed_frames: observed_frames,
            first_timeline_endpoint_samples: timeline.0,
            last_timeline_endpoint_samples: timeline.0,
            timeline_source: timeline.1,
            clip_event_count: aux.clip_event_count,
            lufs_m: MeterHistoryRange::exact(current.lufs_m),
            lufs_s: MeterHistoryRange::exact(current.lufs_s),
            true_peak: MeterHistoryRange::exact(current.true_peak),
            correlation: MeterHistoryRange::exact(aux.correlation),
            psr: MeterHistoryRange::exact(current.psr),
        }
    }
}

impl MeterHistoryRange {
    fn exact(value: Option<f64>) -> Self {
        let value = value.filter(|v| v.is_finite());
        Self {
            min: value,
            max: value,
            mean: value,
        }
    }
}

struct HistoryTier {
    resolution: MeterHistoryResolution,
    target_observations: u16,
    capacity: usize,
    entries: VecDeque<MeterHistoryEntry>,
    pending: Option<BucketAccumulator>,
}

impl HistoryTier {
    fn aggregate(
        resolution: MeterHistoryResolution,
        target_observations: u16,
        capacity: usize,
    ) -> Self {
        Self {
            resolution,
            target_observations,
            capacity,
            entries: VecDeque::with_capacity(capacity.saturating_add(1)),
            pending: None,
        }
    }

    fn push(&mut self, point: MeterHistoryEntry) {
        // Legacy retention is grouped by 10/100 observations, not by each metric's
        // finite/None transitions. TIME export refines a mixed bucket separately.
        if self.pending.is_some_and(|pending| {
            pending.measurement_epoch != point.measurement_epoch
                || pending.generation != point.generation
                || pending.run_id != point.run_id
        }) {
            self.flush_pending();
        }
        if let Some(pending) = self.pending.as_mut() {
            pending.push(point);
        } else {
            self.pending = Some(BucketAccumulator::new(point));
        }
        if self
            .pending
            .is_some_and(|pending| pending.observation_count >= self.target_observations)
        {
            self.flush_pending();
        }
    }

    fn flush_pending(&mut self) {
        if let Some(pending) = self.pending.take() {
            push_bounded(
                &mut self.entries,
                pending.finish(self.resolution),
                self.capacity,
            );
        }
    }

    fn recent(&self, max_entries: usize) -> Vec<MeterHistoryEntry> {
        let pending = self.pending.map(|pending| pending.finish(self.resolution));
        let available = self.entries.len() + usize::from(pending.is_some());
        let skip = available.saturating_sub(max_entries);
        self.entries
            .iter()
            .copied()
            .chain(pending)
            .skip(skip)
            .collect()
    }

    fn recent_decimated(&self, max_entries: usize, max_output: usize) -> Vec<MeterHistoryEntry> {
        let pending = self.pending.map(|pending| pending.finish(self.resolution));
        let available = self.entries.len() + usize::from(pending.is_some());
        let selected = available.min(max_entries);
        let points = self
            .entries
            .iter()
            .copied()
            .chain(pending)
            .skip(available.saturating_sub(selected));
        decimate_history(points, selected, max_output, self.resolution)
    }

    fn clear(&mut self) {
        self.entries.clear();
        self.pending = None;
    }
}

pub struct MeterHistory {
    exact_capacity: usize,
    exact: VecDeque<MeterHistoryEntry>,
    one_second: HistoryTier,
    ten_seconds: HistoryTier,
    last: Option<MeterHistoryEntry>,
    segment: u64,
    step_frames: u64,
}

impl MeterHistory {
    pub fn new() -> Self {
        Self::with_config(
            HISTORY_10_HZ_CAPACITY,
            HISTORY_1_HZ_CAPACITY,
            HISTORY_0_1_HZ_CAPACITY,
            10,
            100,
        )
    }

    fn with_config(
        exact_capacity: usize,
        one_second_capacity: usize,
        ten_seconds_capacity: usize,
        one_second_observations: u16,
        ten_second_observations: u16,
    ) -> Self {
        Self {
            exact_capacity,
            last: None,
            segment: 0,
            step_frames: 0,
            exact: VecDeque::with_capacity(exact_capacity.saturating_add(1)),
            one_second: HistoryTier::aggregate(
                MeterHistoryResolution::Hz1,
                one_second_observations,
                one_second_capacity,
            ),
            ten_seconds: HistoryTier::aggregate(
                MeterHistoryResolution::Hz0_1,
                ten_second_observations,
                ten_seconds_capacity,
            ),
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub fn push(
        &mut self,
        measurement_epoch: u64,
        generation: u64,
        run_id: u64,
        observed_frames: u64,
        timeline: (Option<i64>, CaptureClockSource),
        current: &MeasureResult,
        aux: MeterHistoryAux,
    ) {
        let mut point = MeterHistoryEntry::exact(
            measurement_epoch,
            generation,
            run_id,
            observed_frames,
            timeline,
            current,
            aux,
        );
        point.connects_previous = self.last.is_some_and(|last| {
            last.measurement_epoch == point.measurement_epoch
                && last.generation == point.generation
                && last.run_id == point.run_id
                && last.timeline_source == point.timeline_source
                && last.valid_count == point.valid_count
                && (self.step_frames == 0
                    || point
                        .first_observed_frames
                        .checked_sub(last.last_observed_frames)
                        == Some(self.step_frames))
                && match (
                    last.last_timeline_endpoint_samples,
                    point.first_timeline_endpoint_samples,
                ) {
                    (Some(a), Some(b)) => {
                        self.step_frames == 0 || b.checked_sub(a) == Some(self.step_frames as i64)
                    }
                    (None, None) => true,
                    _ => false,
                }
        });
        if !point.connects_previous {
            self.segment = self.segment.wrapping_add(1).max(1);
        }
        point.segment_id = self.segment;
        self.last = Some(point);
        push_bounded(&mut self.exact, point, self.exact_capacity);
        self.one_second.push(point);
        self.ten_seconds.push(point);
    }

    pub fn recent(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
    ) -> Vec<MeterHistoryEntry> {
        match resolution {
            MeterHistoryResolution::Hz10 => recent_bounded(&self.exact, max_entries),
            MeterHistoryResolution::Hz1 => self.one_second.recent(max_entries),
            MeterHistoryResolution::Hz0_1 => self.ten_seconds.recent(max_entries),
        }
    }

    pub fn recent_decimated(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
        max_output: usize,
    ) -> Vec<MeterHistoryEntry> {
        match resolution {
            MeterHistoryResolution::Hz10 => {
                let selected = self.exact.len().min(max_entries);
                let points = self
                    .exact
                    .iter()
                    .copied()
                    .skip(self.exact.len().saturating_sub(selected));
                decimate_history(points, selected, max_output, resolution)
            }
            MeterHistoryResolution::Hz1 => {
                self.one_second.recent_decimated(max_entries, max_output)
            }
            MeterHistoryResolution::Hz0_1 => {
                self.ten_seconds.recent_decimated(max_entries, max_output)
            }
        }
    }

    pub fn set_step_frames(&mut self, frames: u64) {
        self.step_frames = frames;
    }

    pub fn reset(&mut self) {
        self.last = None;
        self.segment = 0;
        self.exact.clear();
        self.one_second.clear();
        self.ten_seconds.clear();
    }
}

impl Default for MeterHistory {
    fn default() -> Self {
        Self::new()
    }
}

fn push_bounded<T>(queue: &mut VecDeque<T>, value: T, capacity: usize) {
    if capacity == 0 {
        return;
    }
    while queue.len() >= capacity {
        queue.pop_front();
    }
    queue.push_back(value);
}

fn recent_bounded<T: Copy>(queue: &VecDeque<T>, max_entries: usize) -> Vec<T> {
    queue
        .iter()
        .skip(queue.len().saturating_sub(max_entries))
        .copied()
        .collect()
}

#[cfg(test)]
#[path = "meter_history_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "time_history_tests.rs"]
mod time_tests;
