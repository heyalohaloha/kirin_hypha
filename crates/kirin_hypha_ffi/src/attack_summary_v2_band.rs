//! Classify completion first; copy envelopes and validate their grids only for the selected cohort.
use super::poll::source_key;
use super::*;
use crate::attack_snapshot_classify::{classify_band_lanes, BandSideEvidence};
use kirin_measure::attack_perception::band::AttackBand;
use kirin_measure::attack_runtime::snapshot::{
    AttackObservationSnapshot, AttackSourceEvidence, AttackSourceKey,
};
use kirin_measure::spectrum_exchange::{AttackMappingProof, AttackObservationView};
use kirin_measure::AttackHistory;

pub(super) fn observation<'a>(
    snapshot: &'a AttackObservationSnapshot,
    onset: i64,
    requested_end: Option<i64>,
    band: AttackBand,
) -> BandSideEvidence<'a> {
    let Some(source) = snapshot.source.filter(|s| s.valid()) else {
        return BandSideEvidence::Missing(KIRIN_REASON_SOURCE_CHANGED);
    };
    if snapshot.band != Some(band)
        || source.band_semantic_hash
            != kirin_measure::attack_runtime::semantics::band_semantic_hash()
    {
        return BandSideEvidence::Missing(KIRIN_REASON_SEMANTICS);
    }
    let entry = match requested_end {
        Some(end) => snapshot.anchored_at(onset, end),
        None => snapshot.own_at(onset),
    };
    if let Some(entry) = entry {
        if !entry.valid()
            || !source.source.matches(&entry.event)
            || entry.measure.is_some_and(|m| m.band != band)
        {
            return BandSideEvidence::Missing(KIRIN_REASON_SOURCE_CHANGED);
        }
        return BandSideEvidence::Observation(entry);
    }
    // Only the producer's explicit pending observation proves an accepted valid request.
    // A newer separately read history may contain an event not yet in this facts packet.
    if onset < source.pcm_start {
        BandSideEvidence::Missing(KIRIN_REASON_NOT_KEPT)
    } else {
        BandSideEvidence::Missing(KIRIN_REASON_WAITING_PUBLICATION)
    }
}

fn envelope(side: BandSideEvidence<'_>) -> Option<SummaryEnvelope> {
    let BandSideEvidence::Observation(observation) = side else {
        return None;
    };
    let measure = observation.measure?;
    Some(SummaryEnvelope {
        head: measure.envelope.head.map(|v| f64::from(v) / 100.0),
        tail: measure.envelope.tail.map(|v| f64::from(v) / 100.0),
        head_valid: observation.head_valid,
        tail_valid: observation.tail_valid,
    })
}

fn measured_end(side: BandSideEvidence<'_>) -> Option<i64> {
    match side {
        BandSideEvidence::Observation(obs) if obs.measure.is_some() => Some(obs.actual_end),
        _ => None,
    }
}

pub(super) fn scalar_proof(
    proof: Option<&AttackMappingProof>,
    band: AttackBand,
    onset: i64,
    pre: BandSideEvidence<'_>,
    post: BandSideEvidence<'_>,
    local: AttackSourceKey,
) -> Result<u64, u8> {
    let proof = proof.ok_or(KIRIN_REASON_MAPPING)?;
    if !crate::attack_snapshot_classify::band_proof_compatible(proof) {
        return Err(KIRIN_REASON_SEMANTICS);
    }
    let end = measured_end(pre)
        .into_iter()
        .chain(measured_end(post))
        .max()
        .unwrap_or(onset.saturating_add(i64::from(local.sample_rate) * 13 / 100));
    if proof.validates_span(band, onset, end, proof.pre.source, local) {
        Ok(super::attack_proof_revision(proof))
    } else {
        Err(KIRIN_REASON_MAPPING)
    }
}

fn masks<const N: usize>(
    proof: Option<&AttackMappingProof>,
    band: AttackBand,
    onset: i64,
    local: AttackSourceKey,
    head: bool,
) -> [u8; N] {
    std::array::from_fn(|i| {
        let rate = i64::from(local.sample_rate);
        // The producer grid rounds microsecond spans to samples, including at 44.1 kHz.
        let frames = |micros: i64| ((rate * micros + 500_000) / 1_000_000).max(1);
        let (lead, span) = if head {
            (frames(20_000), frames(60_000))
        } else {
            (0, frames(300_000))
        };
        let at = onset
            .saturating_sub(lead)
            .saturating_add(span * (2 * i as i64 + 1) / (2 * N as i64));
        u8::from(proof.is_some_and(|proof| {
            proof.validates_span(
                band,
                onset,
                at.saturating_add(1).max(onset.saturating_add(1)),
                proof.pre.source,
                local,
            )
        }))
    })
}

pub(super) fn paired_events(
    view: &AttackObservationView,
    local: &AttackObservationSnapshot,
    band: AttackBand,
    target: u8,
    source: AttackSourceEvidence,
) -> Vec<(SummaryEvent, bool)> {
    view.events
        .iter()
        .map(|content| {
            let pair = content.pair;
            let pre_onset = pair.pre_event_sample;
            let pre = pre_onset
                .and_then(|at| {
                    view.pre
                        .as_ref()
                        .map(|snapshot| observation(snapshot, at, None, band))
                })
                .unwrap_or(BandSideEvidence::Missing(KIRIN_REASON_MAPPING));
            let onset = pre_onset.unwrap_or(content.event_sample);
            let requested_end = match pre {
                BandSideEvidence::Observation(obs) => Some(obs.requested_end),
                _ => None,
            };
            let post = if pre_onset.is_some() {
                requested_end
                    .map(|end| observation(local, onset, Some(end), band))
                    .unwrap_or(BandSideEvidence::Missing(KIRIN_REASON_MAPPING))
            } else {
                pair.post_event_sample
                    .map(|at| observation(local, at, None, band))
                    .unwrap_or(BandSideEvidence::Missing(KIRIN_REASON_MAPPING))
            };
            let proof = scalar_proof(view.proof.as_ref(), band, onset, pre, post, source.source);
            let lanes = classify_band_lanes(target, band, pre, post, proof);
            let unfinished = super::cohort::unfinished(
                &lanes,
                target == KIRIN_TARGET_DELTA
                    && !super::cohort::final_missing(pre)
                    && !super::cohort::final_missing(post)
                    && proof.is_err(),
            );
            (
                SummaryEvent {
                    key: KirinSnapshotEventKey {
                        source: source_key(content.source),
                        event_sample: content.event_sample,
                        token: content.token,
                    },
                    kind: pair.kind as u8,
                    lanes,
                    pre: None,
                    post: None,
                    proof_head: [0; 96],
                    proof_tail: [0; 64],
                },
                unfinished,
            )
        })
        .collect()
}

pub(super) fn local_events(
    history: &AttackHistory,
    local: &AttackObservationSnapshot,
    band: AttackBand,
    target: u8,
    source: AttackSourceEvidence,
) -> Vec<(SummaryEvent, bool)> {
    let published: Vec<_> = history
        .events()
        .filter(|event| source.source.matches(event))
        .collect();
    published
        .into_iter()
        .map(|event| {
            let post = observation(local, event.event_sample, None, band);
            let pre = BandSideEvidence::Missing(KIRIN_REASON_NO_PAIR);
            let lanes = classify_band_lanes(target, band, pre, post, Err(KIRIN_REASON_NO_PAIR));
            let unfinished = super::cohort::unfinished(&lanes, false);
            (
                SummaryEvent {
                    key: KirinSnapshotEventKey {
                        source: source_key(source.source),
                        event_sample: event.event_sample,
                        token: event.event_sample as u64,
                    },
                    kind: 4,
                    lanes,
                    pre: None,
                    post: None,
                    proof_head: [0; 96],
                    proof_tail: [0; 64],
                },
                unfinished,
            )
        })
        .collect()
}

pub(super) fn enrich(
    events: &mut [SummaryEvent],
    view: &AttackObservationView,
    local: &AttackObservationSnapshot,
    band: AttackBand,
    source: AttackSourceEvidence,
    paired: bool,
) {
    for event in events {
        if !paired {
            event.post = envelope(observation(local, event.key.event_sample, None, band));
            continue;
        }
        let Some(content) = view.events.iter().find(|content| {
            super::poll::source_event(content.source, content.event_sample, content.token)
                == event.key
        }) else {
            continue;
        };
        let pair = content.pair;
        let onset = pair.pre_event_sample.unwrap_or(content.event_sample);
        let pre = pair
            .pre_event_sample
            .and_then(|at| {
                view.pre
                    .as_ref()
                    .map(|snapshot| observation(snapshot, at, None, band))
            })
            .unwrap_or(BandSideEvidence::Missing(KIRIN_REASON_MAPPING));
        let post = if pair.pre_event_sample.is_some() {
            match pre {
                BandSideEvidence::Observation(obs) => {
                    observation(local, onset, Some(obs.requested_end), band)
                }
                _ => BandSideEvidence::Missing(KIRIN_REASON_MAPPING),
            }
        } else {
            pair.post_event_sample
                .map(|at| observation(local, at, None, band))
                .unwrap_or(BandSideEvidence::Missing(KIRIN_REASON_MAPPING))
        };
        event.pre = envelope(pre);
        event.post = envelope(post);
        event.proof_head = masks(view.proof.as_ref(), band, onset, source.source, true);
        event.proof_tail = masks(view.proof.as_ref(), band, onset, source.source, false);
    }
}
