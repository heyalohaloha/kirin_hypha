//! Legacy retention buckets with explicit mixed-TIME metadata and valid denominators.
use super::*;
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
pub(super) struct BucketAccumulator {
    pub(super) measurement_epoch: u64,
    pub(super) generation: u64,
    pub(super) run_id: u64,
    pub(super) observation_count: u16,
    pub(super) segment_id: u64,
    connects_previous: bool,
    first_observed_frames: u64,
    last_observed_frames: u64,
    first_timeline_endpoint_samples: Option<i64>,
    last_timeline_endpoint_samples: Option<i64>,
    timeline_complete: bool,
    timeline_source: CaptureClockSource,
    clip_event_count: [u32; METER_HISTORY_CHANNELS],
    lufs_m: RangeAccumulator,
    lufs_s: RangeAccumulator,
    true_peak: RangeAccumulator,
    correlation: RangeAccumulator,
    psr: RangeAccumulator,
}

impl BucketAccumulator {
    pub(super) fn new(point: MeterHistoryEntry) -> Self {
        let mut bucket = Self {
            measurement_epoch: point.measurement_epoch,
            generation: point.generation,
            run_id: point.run_id,
            observation_count: 0,
            segment_id: point.segment_id,
            connects_previous: point.connects_previous,
            first_observed_frames: point.first_observed_frames,
            last_observed_frames: point.last_observed_frames,
            first_timeline_endpoint_samples: point.first_timeline_endpoint_samples,
            last_timeline_endpoint_samples: point.last_timeline_endpoint_samples,
            timeline_complete: point.first_timeline_endpoint_samples.is_some(),
            timeline_source: point.timeline_source,
            clip_event_count: [0; METER_HISTORY_CHANNELS],
            lufs_m: RangeAccumulator::default(),
            lufs_s: RangeAccumulator::default(),
            true_peak: RangeAccumulator::default(),
            correlation: RangeAccumulator::default(),
            psr: RangeAccumulator::default(),
        };
        bucket.push(point);
        bucket
    }

    pub(super) fn push(&mut self, point: MeterHistoryEntry) {
        if self.segment_id != point.segment_id {
            self.segment_id = 0;
            self.connects_previous = false;
        }
        self.observation_count = self.observation_count.saturating_add(1);
        self.last_observed_frames = point.last_observed_frames;
        self.timeline_complete &= point.last_timeline_endpoint_samples.is_some();
        if self.timeline_source != point.timeline_source {
            self.timeline_source = CaptureClockSource::Unknown;
        }
        self.last_timeline_endpoint_samples = point.last_timeline_endpoint_samples;
        for (total, count) in self.clip_event_count.iter_mut().zip(point.clip_event_count) {
            *total = total.saturating_add(count);
        }
        self.lufs_m.push(point.lufs_m.mean);
        self.lufs_s.push(point.lufs_s.mean);
        self.true_peak.push(point.true_peak.mean);
        self.correlation.push(point.correlation.mean);
        self.psr.push(point.psr.mean);
    }

    pub(super) fn finish(self, resolution: MeterHistoryResolution) -> MeterHistoryEntry {
        MeterHistoryEntry {
            resolution,
            measurement_epoch: self.measurement_epoch,
            generation: self.generation,
            run_id: self.run_id,
            observation_count: self.observation_count,
            segment_id: self.segment_id,
            connects_previous: self.connects_previous && self.segment_id != 0,
            valid_count: [
                self.lufs_m.count,
                self.lufs_s.count,
                self.true_peak.count,
                self.correlation.count,
                self.psr.count,
            ],
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
            clip_event_count: self.clip_event_count,
            lufs_m: self.lufs_m.finish(),
            lufs_s: self.lufs_s.finish(),
            true_peak: self.true_peak.finish(),
            correlation: self.correlation.finish(),
            psr: self.psr.finish(),
        }
    }
}
