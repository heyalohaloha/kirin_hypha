//! Versioned, qualified content-window payload independent of current TIME absolute entries.

use super::finite;
use crate::engine::ContentWindowObservation;
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq)]
pub(super) struct ContentWirePoint {
    pub(super) measurement_epoch: u64,
    pub(super) incarnation: u64,
    pub(super) generation: u64,
    pub(super) run_id: u64,
    pub(super) observed_frames: u64,
    pub(super) endpoint_samples: i64,
    pub(super) timeline_endpoint_samples: Option<i64>,
    pub(super) timeline_source: u8,
    pub(super) auxiliary_source: u8,
    pub(super) presentation_source: u8,
    pub(super) output_presentation_samples: Option<u32>,
    pub(super) lufs_m: Option<f64>,
    pub(super) true_peak: Option<f64>,
}

impl ContentWirePoint {
    pub(super) fn from_observation(
        point: ContentWindowObservation,
        measurement_epoch: u64,
        incarnation: u64,
        generation: u64,
    ) -> Self {
        Self {
            measurement_epoch,
            incarnation,
            generation,
            run_id: point.run_id,
            observed_frames: point.observed_frames,
            endpoint_samples: point.endpoint_samples,
            timeline_endpoint_samples: point.timeline_endpoint_samples,
            timeline_source: point.timeline_source as u8,
            auxiliary_source: point.auxiliary_source as u8,
            presentation_source: point.presentation_latency.source as u8,
            output_presentation_samples: point.presentation_latency.output,
            lufs_m: finite(point.lufs_m),
            true_peak: finite(point.true_peak),
        }
    }

    pub(super) fn valid(&self, sample_rate: u32) -> bool {
        self.measurement_epoch > 0
            && self.incarnation > 0
            && self.run_id > 0
            && sample_rate > 0
            && sample_rate.is_multiple_of(10)
            && self
                .endpoint_samples
                .rem_euclid(i64::from(sample_rate / 10))
                == 0
            && [self.lufs_m, self.true_peak]
                .into_iter()
                .flatten()
                .all(f64::is_finite)
    }
}
