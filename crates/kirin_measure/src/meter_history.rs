//! Fixed-capacity, multi-resolution TIME history for the always-on Meter Session.

use std::collections::VecDeque;

use crate::{CaptureClockSource, MeasureResult};

pub const HISTORY_10_HZ_CAPACITY: usize = 10 * 60 * 10;
pub const HISTORY_1_HZ_CAPACITY: usize = 2 * 60 * 60;
pub const HISTORY_0_1_HZ_CAPACITY: usize = 24 * 60 * 6;

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
    pub generation: u64,
    pub run_id: u64,
    pub observation_count: u16,
    pub first_observed_frames: u64,
    pub last_observed_frames: u64,
    pub first_timeline_endpoint_samples: Option<i64>,
    pub last_timeline_endpoint_samples: Option<i64>,
    pub timeline_source: CaptureClockSource,
    pub lufs_m: MeterHistoryRange,
    pub lufs_s: MeterHistoryRange,
    pub true_peak: MeterHistoryRange,
    pub correlation: MeterHistoryRange,
}

impl MeterHistoryEntry {
    fn exact(
        generation: u64,
        run_id: u64,
        observed_frames: u64,
        timeline: (Option<i64>, CaptureClockSource),
        current: &MeasureResult,
        correlation: Option<f64>,
    ) -> Self {
        Self {
            resolution: MeterHistoryResolution::Hz10,
            generation,
            run_id,
            observation_count: 1,
            first_observed_frames: observed_frames,
            last_observed_frames: observed_frames,
            first_timeline_endpoint_samples: timeline.0,
            last_timeline_endpoint_samples: timeline.0,
            timeline_source: timeline.1,
            lufs_m: MeterHistoryRange::exact(current.lufs_m),
            lufs_s: MeterHistoryRange::exact(current.lufs_s),
            true_peak: MeterHistoryRange::exact(current.true_peak),
            correlation: MeterHistoryRange::exact(correlation),
        }
    }
}

impl MeterHistoryRange {
    fn exact(value: Option<f64>) -> Self {
        Self {
            min: value,
            max: value,
            mean: value,
        }
    }
}

#[derive(Debug, Clone, Copy, Default)]
struct RangeAccumulator {
    min: Option<f64>,
    max: Option<f64>,
    sum: f64,
    count: u16,
}

impl RangeAccumulator {
    fn push(&mut self, value: Option<f64>) {
        let Some(value) = value.filter(|value| value.is_finite()) else {
            return;
        };
        self.min = Some(self.min.map_or(value, |current| current.min(value)));
        self.max = Some(self.max.map_or(value, |current| current.max(value)));
        self.sum += value;
        self.count = self.count.saturating_add(1);
    }

    fn finish(self) -> MeterHistoryRange {
        MeterHistoryRange {
            min: self.min,
            max: self.max,
            mean: (self.count > 0).then(|| self.sum / f64::from(self.count)),
        }
    }
}

#[derive(Debug, Clone, Copy)]
struct BucketAccumulator {
    generation: u64,
    run_id: u64,
    observation_count: u16,
    first_observed_frames: u64,
    last_observed_frames: u64,
    first_timeline_endpoint_samples: Option<i64>,
    last_timeline_endpoint_samples: Option<i64>,
    timeline_complete: bool,
    timeline_source: CaptureClockSource,
    lufs_m: RangeAccumulator,
    lufs_s: RangeAccumulator,
    true_peak: RangeAccumulator,
    correlation: RangeAccumulator,
}

impl BucketAccumulator {
    fn new(point: MeterHistoryEntry) -> Self {
        let mut bucket = Self {
            generation: point.generation,
            run_id: point.run_id,
            observation_count: 0,
            first_observed_frames: point.first_observed_frames,
            last_observed_frames: point.last_observed_frames,
            first_timeline_endpoint_samples: point.first_timeline_endpoint_samples,
            last_timeline_endpoint_samples: point.last_timeline_endpoint_samples,
            timeline_complete: point.first_timeline_endpoint_samples.is_some(),
            timeline_source: point.timeline_source,
            lufs_m: RangeAccumulator::default(),
            lufs_s: RangeAccumulator::default(),
            true_peak: RangeAccumulator::default(),
            correlation: RangeAccumulator::default(),
        };
        bucket.push(point);
        bucket
    }

    fn push(&mut self, point: MeterHistoryEntry) {
        self.observation_count = self.observation_count.saturating_add(1);
        self.last_observed_frames = point.last_observed_frames;
        self.timeline_complete &= point.last_timeline_endpoint_samples.is_some();
        if self.timeline_source != point.timeline_source {
            self.timeline_source = CaptureClockSource::Unknown;
        }
        self.last_timeline_endpoint_samples = point.last_timeline_endpoint_samples;
        self.lufs_m.push(point.lufs_m.mean);
        self.lufs_s.push(point.lufs_s.mean);
        self.true_peak.push(point.true_peak.mean);
        self.correlation.push(point.correlation.mean);
    }

    fn finish(self, resolution: MeterHistoryResolution) -> MeterHistoryEntry {
        MeterHistoryEntry {
            resolution,
            generation: self.generation,
            run_id: self.run_id,
            observation_count: self.observation_count,
            first_observed_frames: self.first_observed_frames,
            last_observed_frames: self.last_observed_frames,
            first_timeline_endpoint_samples: self
                .timeline_complete
                .then_some(self.first_timeline_endpoint_samples)
                .flatten(),
            last_timeline_endpoint_samples: self
                .timeline_complete
                .then_some(self.last_timeline_endpoint_samples)
                .flatten(),
            timeline_source: self.timeline_source,
            lufs_m: self.lufs_m.finish(),
            lufs_s: self.lufs_s.finish(),
            true_peak: self.true_peak.finish(),
            correlation: self.correlation.finish(),
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
        if self.pending.is_some_and(|pending| {
            pending.generation != point.generation || pending.run_id != point.run_id
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

    pub fn push(
        &mut self,
        generation: u64,
        run_id: u64,
        observed_frames: u64,
        timeline: (Option<i64>, CaptureClockSource),
        current: &MeasureResult,
        correlation: Option<f64>,
    ) {
        let point = MeterHistoryEntry::exact(
            generation,
            run_id,
            observed_frames,
            timeline,
            current,
            correlation,
        );
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

    pub fn reset(&mut self) {
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
mod tests {
    use super::*;

    fn point_value(value: f64) -> MeasureResult {
        MeasureResult {
            lufs_m: Some(value),
            lufs_s: Some(value - 1.0),
            true_peak: Some(value + 10.0),
            ..MeasureResult::default()
        }
    }

    #[test]
    fn product_capacities_match_ten_minutes_two_hours_and_twenty_four_hours() {
        assert_eq!(HISTORY_10_HZ_CAPACITY, 6_000);
        assert_eq!(HISTORY_1_HZ_CAPACITY, 7_200);
        assert_eq!(HISTORY_0_1_HZ_CAPACITY, 8_640);
    }

    #[test]
    fn one_second_bucket_keeps_min_max_mean_and_exact_endpoints() {
        let mut history = MeterHistory::with_config(20, 20, 20, 10, 100);
        for index in 0..10_u64 {
            history.push(
                1,
                4,
                (index + 1) * 4_800,
                (
                    Some(10_000 + ((index + 1) * 4_800) as i64),
                    CaptureClockSource::ProjectTimeline,
                ),
                &point_value(index as f64),
                Some(index as f64 / 10.0),
            );
        }
        let entries = history.recent(MeterHistoryResolution::Hz1, 10);
        assert_eq!(entries.len(), 1);
        let entry = entries[0];
        assert_eq!(entry.observation_count, 10);
        assert_eq!(entry.first_observed_frames, 4_800);
        assert_eq!(entry.last_observed_frames, 48_000);
        assert_eq!(entry.first_timeline_endpoint_samples, Some(14_800));
        assert_eq!(entry.last_timeline_endpoint_samples, Some(58_000));
        assert_eq!(entry.lufs_m.min, Some(0.0));
        assert_eq!(entry.lufs_m.max, Some(9.0));
        assert_eq!(entry.lufs_m.mean, Some(4.5));
    }

    #[test]
    fn run_change_flushes_partial_bucket_instead_of_joining_a_seek() {
        let mut history = MeterHistory::with_config(20, 20, 20, 10, 100);
        for index in 0..4_u64 {
            history.push(
                1,
                1,
                index + 1,
                (Some(index as i64), CaptureClockSource::ProjectTimeline),
                &point_value(1.0),
                None,
            );
        }
        history.push(
            1,
            2,
            5,
            (Some(50_000), CaptureClockSource::ProjectTimeline),
            &point_value(2.0),
            None,
        );
        let entries = history.recent(MeterHistoryResolution::Hz1, 10);
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0].run_id, 1);
        assert_eq!(entries[0].observation_count, 4);
        assert_eq!(entries[1].run_id, 2);
        assert_eq!(entries[1].observation_count, 1);
    }

    #[test]
    fn fixed_capacity_drops_only_the_oldest_entry_and_reset_clears_every_tier() {
        let mut history = MeterHistory::with_config(2, 2, 2, 1, 1);
        for index in 0..3_u64 {
            history.push(
                1,
                1,
                index + 1,
                (None, CaptureClockSource::Unknown),
                &point_value(index as f64),
                None,
            );
        }
        let exact = history.recent(MeterHistoryResolution::Hz10, 10);
        assert_eq!(exact.len(), 2);
        assert_eq!(exact[0].last_observed_frames, 2);
        assert_eq!(exact[1].last_observed_frames, 3);
        assert_eq!(history.recent(MeterHistoryResolution::Hz1, 10).len(), 2);
        assert_eq!(history.recent(MeterHistoryResolution::Hz0_1, 10).len(), 2);
        history.reset();
        assert!(history.recent(MeterHistoryResolution::Hz10, 10).is_empty());
        assert!(history.recent(MeterHistoryResolution::Hz1, 10).is_empty());
        assert!(history.recent(MeterHistoryResolution::Hz0_1, 10).is_empty());
    }
}
