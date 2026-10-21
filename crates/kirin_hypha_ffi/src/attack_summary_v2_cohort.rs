//! Completion is decided before selecting the most recent eight content events.
use super::aggregate::fixed_cohort;
use super::*;
use crate::attack_snapshot_classify::BandSideEvidence;
use kirin_measure::attack_runtime::snapshot::AttackFinish;

pub(super) fn unfinished(lanes: &[KirinSnapshotScalarEvidence; 4], mapping_wait: bool) -> bool {
    mapping_wait
        || lanes.iter().any(|value| {
            value.class == KIRIN_SCALAR_PENDING
                || matches!(
                    value.reason,
                    KIRIN_REASON_WAITING_AUDIO
                        | KIRIN_REASON_WAITING_SERVICE
                        | KIRIN_REASON_WAITING_PUBLICATION
                )
        })
}

pub(super) fn final_missing(side: BandSideEvidence<'_>) -> bool {
    match side {
        BandSideEvidence::Observation(obs) => {
            matches!(obs.finish, AttackFinish::NotKept | AttackFinish::Retired)
        }
        BandSideEvidence::Missing(reason) => matches!(
            reason,
            KIRIN_REASON_NOT_KEPT
                | KIRIN_REASON_NO_PAIR
                | KIRIN_REASON_SOURCE_CHANGED
                | KIRIN_REASON_SEMANTICS
        ),
        BandSideEvidence::Pending(_) => false,
    }
}

pub(super) fn select(
    candidates: &[(SummaryEvent, bool)],
    cutoff: i64,
    rate: u32,
) -> (Vec<SummaryEvent>, u8) {
    let start = cutoff.saturating_sub(i64::from(rate) * 6);
    let complete: Vec<_> = candidates
        .iter()
        .filter(|(_, unfinished)| !unfinished)
        .collect();
    let cohort = fixed_cohort(&complete, cutoff, rate, |e| e.0.key.event_sample);
    let newest = cohort.last().map(|e| e.0.key.event_sample);
    let newer_unfinished = candidates
        .iter()
        .filter(|(e, unfinished)| {
            *unfinished
                && (start..=cutoff).contains(&e.key.event_sample)
                && newest.is_none_or(|at| e.key.event_sample > at)
        })
        .count();
    // Both local history and the paired ledger are bounded to 240 events.
    (
        cohort.into_iter().map(|e| e.0.clone()).collect(),
        newer_unfinished.min(255) as u8,
    )
}
