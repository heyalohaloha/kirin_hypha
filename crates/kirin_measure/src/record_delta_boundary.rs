//! Measurement-owned admission floor for a Keep's displayed exact delta.
use super::RecordStateMachine;
use crate::meter_session::{MeterSession, TimeRawPoint, TimeSourceSpan};

#[derive(Clone, Copy, Debug)]
pub(crate) struct RecordDeltaBoundary {
    pub(crate) span: TimeSourceSpan,
    pub(crate) observed: u64,
}
impl RecordDeltaBoundary {
    pub(crate) fn admits(self, point: &TimeRawPoint) -> bool {
        point.wire.span == self.span && point.wire.observed > self.observed
    }
}
impl RecordStateMachine {
    /// Explicit control-thread reset while the caller holds the Meter Session mutex.
    /// Only an already-admitted Keep may inherit the new span; an entering Keep still waits
    /// for the Measure Thread to drain Watch and establish its own admission floor.
    pub fn reset_meter_session(&self, session: &mut MeterSession) {
        session.reset();
        let generation = self.generation();
        let mut boundary =
            crate::sync_recovery::lock_recover(&self.record_delta_boundary, "Record delta reset");
        if self.is_recording()
            && self.generation() == generation
            && boundary
                .as_ref()
                .is_some_and(|(owner, _)| *owner == generation)
        {
            *boundary = Some((
                generation,
                RecordDeltaBoundary {
                    span: session.time_source_span(),
                    observed: session.snapshot().active_frames,
                },
            ));
        }
    }

    /// After draining Watch, before any Record audio enters the always-on meter.
    pub(crate) fn mark_record_delta_boundary(&self, generation: u64, session: &MeterSession) {
        let boundary = RecordDeltaBoundary {
            span: session.time_source_span(),
            observed: session.snapshot().active_frames,
        };
        *crate::sync_recovery::lock_recover(&self.record_delta_boundary, "Record delta boundary") =
            Some((generation, boundary));
    }

    pub(crate) fn record_delta_boundary(&self, generation: u64) -> Option<RecordDeltaBoundary> {
        self.record_delta_boundary
            .try_lock()
            .ok()?
            .as_ref()
            .filter(|(owner, _)| *owner == generation)
            .map(|(_, boundary)| *boundary)
    }
}
