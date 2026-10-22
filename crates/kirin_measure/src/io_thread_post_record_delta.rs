//! Last exact LEVEL delta observed during one Keep, independent of current/HOLD presentation.
use crate::delta::DeltaResult;
use crate::meter_delta_history::TimeComparisonReason;
use crate::record::delta_boundary::RecordDeltaBoundary;
use crate::{MeterDeltaHistoryExchange, MeterHistoryResolution};

#[derive(Default)]
pub(super) struct RecordDeltaCache {
    generation: Option<u64>,
    exact: Option<([Option<f64>; 4], Option<f64>)>,
}

impl RecordDeltaCache {
    pub(super) fn resolve(
        &mut self,
        generation: u64,
        legacy: DeltaResult,
        history: Option<&MeterDeltaHistoryExchange>,
        boundary: Option<RecordDeltaBoundary>,
    ) -> DeltaResult {
        let view = history.and_then(|history| {
            history
                .time_comparison(MeterHistoryResolution::Hz10, 0, u64::MAX, 0)?
                .ok()
                .flatten()
        });
        let point = view
            .as_ref()
            .filter(|view| view.reason == TimeComparisonReason::Active)
            .and_then(|view| view.point.as_ref());
        self.select(generation, legacy, point, boundary)
    }

    fn select(
        &mut self,
        generation: u64,
        mut legacy: DeltaResult,
        point: Option<&crate::meter_session::time_observation::TimeRawPoint>,
        boundary: Option<RecordDeltaBoundary>,
    ) -> DeltaResult {
        if self.generation != Some(generation) {
            self.generation = Some(generation);
            self.exact = None;
        }
        if let Some(point) =
            point.filter(|point| boundary.is_some_and(|boundary| boundary.admits(point)))
        {
            self.exact = Some((point.wire.values[..4].try_into().unwrap(), point.wire.crest));
        }
        if let Some(([m, s, tp, psr], crest)) = self.exact {
            legacy.lufs = m;
            legacy.lufs_s = s;
            legacy.tp = tp;
            legacy.psr = psr;
            legacy.crest = crest;
            // RecordDisplay materializes this snapshot when the legacy PRE file becomes stale.
            if let Some(snapshot) = legacy.last_active.as_mut() {
                snapshot.lufs = m;
                snapshot.lufs_s = s;
                snapshot.tp = tp;
                snapshot.psr = psr;
                snapshot.crest = crest;
            }
        }
        legacy
    }
}

#[cfg(test)]
#[path = "io_thread_post_record_delta_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "io_thread_post_record_boundary_tests.rs"]
mod boundary_tests;

#[cfg(test)]
#[path = "io_thread_post_record_reset_tests.rs"]
mod reset_tests;
