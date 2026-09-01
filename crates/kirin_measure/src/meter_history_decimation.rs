use crate::{MeterHistoryEntry, MeterHistoryRange, MeterHistoryResolution};

#[derive(Default)]
struct RangeAggregate {
    min: Option<f64>,
    max: Option<f64>,
    weighted_sum: f64,
    weight: u64,
}

impl RangeAggregate {
    fn push(&mut self, range: MeterHistoryRange, weight: u16) {
        if let Some(value) = range.min.filter(|value| value.is_finite()) {
            self.min = Some(self.min.map_or(value, |current| current.min(value)));
        }
        if let Some(value) = range.max.filter(|value| value.is_finite()) {
            self.max = Some(self.max.map_or(value, |current| current.max(value)));
        }
        if let Some(value) = range.mean.filter(|value| value.is_finite()) {
            let weight = u64::from(weight.max(1));
            self.weighted_sum += value * weight as f64;
            self.weight = self.weight.saturating_add(weight);
        }
    }

    fn finish(self) -> MeterHistoryRange {
        MeterHistoryRange {
            min: self.min,
            max: self.max,
            mean: (self.weight > 0).then(|| self.weighted_sum / self.weight as f64),
        }
    }
}

struct EntryAggregate {
    first: MeterHistoryEntry,
    observations: u32,
    clip_event_count: [u32; 2],
    lufs_m: RangeAggregate,
    lufs_s: RangeAggregate,
    true_peak: RangeAggregate,
    correlation: RangeAggregate,
    plr: RangeAggregate,
}

impl EntryAggregate {
    fn new(point: MeterHistoryEntry) -> Self {
        let mut result = Self {
            first: point,
            observations: 0,
            clip_event_count: [0; 2],
            lufs_m: RangeAggregate::default(),
            lufs_s: RangeAggregate::default(),
            true_peak: RangeAggregate::default(),
            correlation: RangeAggregate::default(),
            plr: RangeAggregate::default(),
        };
        result.push(point);
        result
    }

    fn push(&mut self, point: MeterHistoryEntry) {
        self.observations = self
            .observations
            .saturating_add(u32::from(point.observation_count.max(1)));
        self.first.last_observed_frames = point.last_observed_frames;
        self.first.last_timeline_endpoint_samples = point.last_timeline_endpoint_samples;
        for (total, count) in self.clip_event_count.iter_mut().zip(point.clip_event_count) {
            *total = total.saturating_add(count);
        }
        self.lufs_m.push(point.lufs_m, point.observation_count);
        self.lufs_s.push(point.lufs_s, point.observation_count);
        self.true_peak
            .push(point.true_peak, point.observation_count);
        self.correlation
            .push(point.correlation, point.observation_count);
        self.plr.push(point.plr, point.observation_count);
    }

    fn finish(mut self, resolution: MeterHistoryResolution) -> MeterHistoryEntry {
        self.first.resolution = resolution;
        self.first.observation_count = self.observations.min(u32::from(u16::MAX)) as u16;
        self.first.clip_event_count = self.clip_event_count;
        self.first.lufs_m = self.lufs_m.finish();
        self.first.lufs_s = self.lufs_s.finish();
        self.first.true_peak = self.true_peak.finish();
        self.first.correlation = self.correlation.finish();
        self.first.plr = self.plr.finish();
        self.first
    }
}

pub(crate) fn decimate_history(
    points: impl Iterator<Item = MeterHistoryEntry>,
    available: usize,
    max_output: usize,
    resolution: MeterHistoryResolution,
) -> Vec<MeterHistoryEntry> {
    if max_output == 0 || available == 0 {
        return Vec::new();
    }
    let points: Vec<_> = points.take(available).collect();
    if points.is_empty() {
        return Vec::new();
    }

    fn run_ranges(points: &[MeterHistoryEntry]) -> Vec<(usize, usize)> {
        let mut ranges = Vec::new();
        let mut start = 0usize;
        for index in 1..points.len() {
            if points[index].generation != points[start].generation
                || points[index].run_id != points[start].run_id
            {
                ranges.push((start, index));
                start = index;
            }
        }
        ranges.push((start, points.len()));
        ranges
    }

    let all_runs = run_ranges(&points);
    // One output cannot truthfully describe two discontinuous transport runs. If the UI is
    // narrower than the number of runs, retain the newest runs rather than joining or relabelling
    // them; a shorter truthful window is preferable to a fabricated continuous one.
    let selected_start = all_runs
        .len()
        .checked_sub(max_output)
        .map_or(0, |first_run| all_runs[first_run].0);
    let selected = &points[selected_start..];
    let runs = run_ranges(selected);

    let mut allocations = vec![1usize; runs.len()];
    let mut remaining = max_output.saturating_sub(runs.len());
    while remaining > 0 {
        let mut best: Option<usize> = None;
        for (index, &(start, end)) in runs.iter().enumerate() {
            let length = end - start;
            if allocations[index] >= length {
                continue;
            }
            best = match best {
                None => Some(index),
                Some(current) => {
                    let current_length = runs[current].1 - runs[current].0;
                    if length * (allocations[current] + 1)
                        > current_length * (allocations[index] + 1)
                    {
                        Some(index)
                    } else {
                        Some(current)
                    }
                }
            };
        }
        let Some(index) = best else {
            break;
        };
        allocations[index] += 1;
        remaining -= 1;
    }

    let mut output = Vec::with_capacity(allocations.iter().sum());
    for ((run_start, run_end), bucket_count) in runs.into_iter().zip(allocations) {
        let length = run_end - run_start;
        for bucket in 0..bucket_count {
            let start = run_start + bucket * length / bucket_count;
            let end = run_start + (bucket + 1) * length / bucket_count;
            let mut aggregate = EntryAggregate::new(selected[start]);
            for point in selected[start + 1..end].iter().copied() {
                aggregate.push(point);
            }
            output.push(aggregate.finish(resolution));
        }
    }
    output
}
