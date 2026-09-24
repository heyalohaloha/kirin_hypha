//! Provenance of a complete M/TP 400 ms aperture, derived from existing 100 ms observations.
//! No additional audio pass. Unknown or discontinuous history never becomes a valid window.
use super::*;

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub(super) struct WindowProvenance {
    pub measurement_epoch: u64,
    pub incarnation: u64,
    pub complete_400ms: bool,
    /// A qualified common content clock/occurrence, not merely equal local run counters.
    #[serde(default)]
    pub content_clock_qualified: bool,
}

fn compatible_sources(auxiliary: u8, presentation: u8) -> bool {
    // Only Studio Pro's VST3 continuous clock + output PDC relationship has been
    // observed in a real host. AU and AAX clocks are carried through the capture
    // boundary, but must remain fail-closed until their own host evidence exists.
    matches!((auxiliary, presentation), (1, 1))
}

pub(super) fn complete_window(points: &[MeterHistoryEntry], end: usize, rate: u32) -> bool {
    if end < 3 || rate == 0 || !rate.is_multiple_of(10) {
        return false;
    }
    let window = &points[end - 3..=end];
    let step = u64::from(rate / 10);
    let first = &window[0];
    if first.timeline_source == CaptureClockSource::Unknown || first.run_id == 0 {
        return false;
    }
    window.iter().enumerate().all(|(index, point)| {
        point.resolution == MeterHistoryResolution::Hz10
            && point.observation_count == 1
            && point.measurement_epoch == first.measurement_epoch
            && point.generation == first.generation
            && point.run_id == first.run_id
            && point.timeline_source == first.timeline_source
            && point
                .last_observed_frames
                .checked_sub(first.last_observed_frames)
                == Some(step * index as u64)
            && point
                .last_timeline_endpoint_samples
                .zip(first.last_timeline_endpoint_samples)
                .and_then(|(last, first)| last.checked_sub(first))
                == Some((step * index as u64) as i64)
    })
}

pub(super) fn complete_content_window(
    points: &[MeterHistoryEntry],
    witnesses: &[crate::meter_clock::MeterClockWitness],
    end: usize,
    rate: u32,
) -> bool {
    if !complete_window(points, end, rate) {
        return false;
    }
    let window = &points[end - 3..=end];
    let Some(first) = witness_for(&window[0], witnesses) else {
        return false;
    };
    let step = i64::from(rate / 10);
    let auxiliary = first.auxiliary_endpoint_samples;
    compatible_sources(
        first.auxiliary_source as u8,
        first.presentation_latency.source as u8,
    ) && first.presentation_latency.output.is_some()
        && auxiliary.is_some()
        && window.iter().enumerate().all(|(index, point)| {
            witness_for(point, witnesses).is_some_and(|clock| {
                clock.auxiliary_source == first.auxiliary_source
                    && clock.presentation_latency == first.presentation_latency
                    && clock
                        .auxiliary_endpoint_samples
                        .zip(auxiliary)
                        .and_then(|(last, first)| last.checked_sub(first))
                        == Some(step * index as i64)
            })
        })
}

pub(super) fn witness_for<'a>(
    point: &MeterHistoryEntry,
    witnesses: &'a [crate::meter_clock::MeterClockWitness],
) -> Option<&'a crate::meter_clock::MeterClockWitness> {
    witnesses.iter().find(|clock| {
        clock.measurement_epoch == point.measurement_epoch
            && clock.generation == point.generation
            && clock.run_id == point.run_id
            && clock.observed_frames == point.last_observed_frames
            && clock.timeline_endpoint_samples == point.last_timeline_endpoint_samples
            && clock.timeline_source == point.timeline_source
    })
}

pub(super) fn wire_tail(session: &MeterSession, rate: u32) -> Vec<WirePoint> {
    let points = session.recent_history(
        MeterHistoryResolution::Hz10,
        METER_HISTORY_EXCHANGE_POINTS + 3,
    );
    let witnesses = session.recent_clock_witnesses(METER_HISTORY_EXCHANGE_POINTS + 3);
    let incarnation = session.history_publication_revision().0;
    points
        .iter()
        .enumerate()
        .skip(points.len().saturating_sub(METER_HISTORY_EXCHANGE_POINTS))
        .filter_map(|(index, point)| {
            let witness = witness_for(point, &witnesses).copied();
            let mut wire = WirePoint::from_history(*point, witness)?;
            wire.window = Some(WindowProvenance {
                measurement_epoch: point.measurement_epoch,
                incarnation,
                complete_400ms: complete_window(&points, index, rate),
                content_clock_qualified: complete_content_window(&points, &witnesses, index, rate),
            });
            Some(wire)
        })
        .collect()
}

#[cfg(test)]
#[path = "meter_chain_window_tests.rs"]
mod tests;
