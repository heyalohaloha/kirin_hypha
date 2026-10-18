//! Keep/Record display snapshot to its C ABI form (UI thread only).
use super::*;

pub(crate) fn to_c_record_display(
    snapshot: RecordDisplaySnapshot,
    current_pair_pre_instance_id: Option<&str>,
) -> KirinRecordDisplay {
    let has_measure = snapshot.measure.is_some() as u8;
    let has_session = snapshot.summary.is_some() as u8;
    let has_delta = snapshot.delta.is_some() as u8;
    let pair_matches_current = matches!(
        (
            snapshot.pair_pre_instance_id.as_deref(),
            current_pair_pre_instance_id
        ),
        (Some(recorded), Some(current)) if recorded == current
    ) as u8;
    KirinRecordDisplay {
        phase: record_display_phase_to_abi(snapshot.status),
        has_measure,
        has_session,
        has_delta,
        generation: snapshot.generation,
        measure: snapshot
            .measure
            .as_ref()
            .map_or_else(|| to_c_result(&MeasureResult::default()), to_c_result),
        session: snapshot
            .summary
            .as_ref()
            .map_or_else(|| to_c_session(&SessionSummary::default()), to_c_session),
        delta: snapshot
            .delta
            .as_ref()
            .map_or_else(|| to_c_delta(&DeltaResult::default()), to_c_delta),
        pair_matches_current,
    }
}
