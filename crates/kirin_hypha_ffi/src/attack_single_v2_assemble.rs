//! One producer reply supplies all values, shapes, masks and completion tags.
use super::*;
use crate::attack_snapshot_classify::{classify_band_lanes, BandSideEvidence};
use kirin_measure::attack_runtime::single::{AttackSingleReason, AttackSingleSnapshot};
use kirin_measure::attack_runtime::snapshot::{
    AttackBandObservation, AttackFinish, AttackSourceEvidence,
};
use kirin_measure::spectrum_exchange::AttackObservationView;

fn reason(reason: AttackSingleReason) -> u8 {
    match reason {
        AttackSingleReason::None => KIRIN_REASON_NONE,
        AttackSingleReason::RequestDeadline => KIRIN_REASON_REQUEST_DEADLINE,
        AttackSingleReason::WorkerUnavailable => KIRIN_REASON_WORKER_UNAVAILABLE,
        AttackSingleReason::SourceChanged => KIRIN_REASON_SOURCE_CHANGED,
        AttackSingleReason::AudioNotKept => KIRIN_REASON_NOT_KEPT,
    }
}
fn side(observation: Option<&AttackBandObservation>, missing: u8) -> BandSideEvidence<'_> {
    observation.map_or(
        BandSideEvidence::Missing(missing),
        BandSideEvidence::Observation,
    )
}
fn envelope(
    observation: Option<&AttackBandObservation>,
    values: &mut [f64; 160],
    mask: &mut [u8; 160],
) {
    let Some(observation) = observation else {
        return;
    };
    let Some(measure) = observation.measure else {
        return;
    };
    for (at, value) in measure
        .envelope
        .head
        .iter()
        .chain(measure.envelope.tail.iter())
        .enumerate()
    {
        values[at] = f64::from(*value) / 100.0;
    }
    mask[..96].copy_from_slice(&observation.head_valid);
    mask[96..].copy_from_slice(&observation.tail_valid);
}

pub(super) fn assemble(
    state: &AttackSingleSnapshot,
    source: Option<AttackSourceEvidence>,
    view: &AttackObservationView,
    signal: u8,
) -> KirinAttackSingleSnapshotV2 {
    let request = &state.request;
    let source_key = super::request::source_key(request.local_source);
    let mut snapshot = KirinAttackSingleSnapshotV2 {
        header: KirinSnapshotHeader {
            version: 2,
            struct_size: std::mem::size_of::<KirinAttackSingleSnapshotV2>() as u32,
            kind: KIRIN_SNAPSHOT_SINGLE,
            target: request.target,
            band: request.band.map_or(0, |band| band.index()),
            signal_state: signal,
            flags: 0,
            snapshot_revision: state.revision,
            authority_revision: request.pair_authority_revision,
            cutoff_sample: source.map_or(0, |source| source.cutoff),
            source: source_key,
            band_semantic_hash: source.map_or([0; 32], |source| source.band_semantic_hash),
        },
        event: KirinSnapshotEventKey {
            source: super::request::source_key(request.key_source),
            event_sample: request.key_event_sample,
            token: request.key_token,
        },
        request_token: state.token,
        measurement_revision: state.revision,
        finish: state.finish as u8,
        reason: reason(state.reason),
        pair_kind: request.pair_kind,
        ..Default::default()
    };
    if request.target != KIRIN_TARGET_POST
        && request.proof_token == [0; 32]
        && snapshot.reason == KIRIN_REASON_NONE
    {
        snapshot.reason = KIRIN_REASON_NO_PAIR;
    }
    if state.finish == AttackFinish::Retired {
        snapshot.lanes =
            [KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_UNKNOWN, snapshot.reason); 4];
        for lane in &mut snapshot.lanes {
            lane.finish = KIRIN_FINISH_RETIRED;
            lane.measurement_revision = state.revision;
        }
        return snapshot;
    }
    let proof = view.proof.filter(|proof| {
        proof.binding_token() == request.proof_token && proof.post.source == request.local_source
    });
    if let Some(band) = request.band {
        let missing = if request.proof_token == [0; 32] || request.pair_kind == 2 {
            KIRIN_REASON_NO_PAIR
        } else {
            KIRIN_REASON_MAPPING
        };
        let pre = side(request.pre.as_ref(), missing);
        let post = state.band_observation.as_ref().map_or(
            BandSideEvidence::Pending(KIRIN_REASON_WAITING_SERVICE),
            BandSideEvidence::Observation,
        );
        let actual_end = request
            .pre
            .iter()
            .chain(state.band_observation.iter())
            .filter(|entry| entry.measure.is_some())
            .map(|entry| entry.actual_end)
            .max()
            .unwrap_or(request.measurement_end);
        let proof_revision = proof
            .filter(|proof| {
                proof.validates_span(
                    band,
                    request.event.event_sample,
                    actual_end,
                    proof.pre.source,
                    request.local_source,
                )
            })
            .map(|proof| crate::attack_summary_v2::attack_proof_revision(&proof))
            .ok_or(missing);
        snapshot.lanes = classify_band_lanes(request.target, band, pre, post, proof_revision);
        envelope(
            request.pre.as_ref(),
            &mut snapshot.pre,
            &mut snapshot.pre_valid,
        );
        envelope(
            state.band_observation.as_ref(),
            &mut snapshot.post,
            &mut snapshot.post_valid,
        );
        for at in 0..160 {
            let rate = i64::from(request.local_source.sample_rate);
            let sample = if at < 96 {
                request.event.event_sample - rate * 20 / 1000
                    + rate * 60 / 1000 * (2 * at as i64 + 1) / 192
            } else {
                request.event.event_sample + rate * 300 / 1000 * (2 * (at - 96) as i64 + 1) / 128
            };
            let supports = proof.is_some_and(|proof| {
                proof.validates_span(
                    band,
                    request.event.event_sample,
                    sample
                        .saturating_add(1)
                        .max(request.event.event_sample.saturating_add(1)),
                    proof.pre.source,
                    request.local_source,
                )
            });
            snapshot.pre_valid[at] &= u8::from(supports);
            if request.target == KIRIN_TARGET_DELTA {
                snapshot.post_valid[at] &= u8::from(supports);
            }
        }
    } else {
        snapshot.lanes = [KirinSnapshotScalarEvidence::unavailable(
            KIRIN_SCALAR_NOT_APPLICABLE,
            KIRIN_REASON_NONE,
        ); 4];
        if let Some(detail) = state.detail {
            snapshot.all_post = super::super::convert::to_c_attack_detail(&detail);
            snapshot.has_all_post = 1;
        }
        if let Some(detail) = request.pre_detail.filter(|detail| {
            proof.is_some_and(|proof| {
                proof.validates_raw_span(
                    detail.shape.start_sample,
                    detail.features.body_end_sample,
                    proof.pre.source,
                    request.local_source,
                )
            })
        }) {
            snapshot.all_pre = super::super::convert::to_c_attack_detail(&detail);
            snapshot.has_all_pre = 1;
        }
        if request.target != KIRIN_TARGET_POST && snapshot.has_all_pre == 0 {
            snapshot.has_all_post = 0;
            snapshot.all_post = Default::default();
        }
    }
    snapshot
}
