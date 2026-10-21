//! ALL consumes existing details and navigation authority; it never requests a band worker.
use super::*;
use crate::attack_snapshot_classify::{classify_all_lanes, AllSideEvidence};
use kirin_measure::attack_runtime::snapshot::{
    AttackFinish, AttackSourceEvidence, AttackSourceKey,
};
use kirin_measure::spectrum_exchange::AttackObservationView;
use kirin_measure::{AttackDetailedEvent, AttackHistory};

pub(crate) fn representative(
    snapshot: &mut KirinAttackBandSummaryV2,
    history: &AttackHistory,
    view: &AttackObservationView,
    source: AttackSourceEvidence,
    paired: bool,
) {
    let Some(index) = snapshot.cohort_count.checked_sub(1) else {
        return;
    };
    let key = snapshot.events[index as usize];
    let (pre, post) = if paired {
        let Some(content) = view.events.iter().find(|content| {
            super::poll::source_event(content.source, content.event_sample, content.token) == key
        }) else {
            return;
        };
        let onset = content
            .pair
            .pre_event_sample
            .unwrap_or(content.event_sample);
        let pre = content.pair.pre_event_sample.and_then(|at| {
            let proof = view.proof.as_ref()?;
            view.pre_history.as_ref()?.details().find(|d| {
                d.event.event_sample == at
                    && proof.pre.source.matches(&d.event)
                    && d.features.complete
                    && proof.validates_raw_span(
                        d.shape.start_sample,
                        d.features.body_end_sample,
                        proof.pre.source,
                        source.source,
                    )
            })
        });
        (
            pre,
            view.post_anchored
                .iter()
                .find(|d| d.event.event_sample == onset && source.source.matches(&d.event)),
        )
    } else {
        (
            None,
            history.details().find(|d| {
                d.event.event_sample == key.event_sample && source.source.matches(&d.event)
            }),
        )
    };
    if let Some(detail) = pre {
        snapshot.representative_pre = super::super::convert::to_c_attack_detail(detail);
        snapshot.has_representative_pre = 1;
    }
    if let Some(detail) = post {
        snapshot.representative_post = super::super::convert::to_c_attack_detail(detail);
        snapshot.has_representative_post = 1;
    }
}

fn side(
    detail: Option<&AttackDetailedEvent>,
    onset: i64,
    source: AttackSourceEvidence,
) -> AllSideEvidence<'_> {
    match detail.filter(|d| source.source.matches(&d.event)) {
        Some(detail) => AllSideEvidence::Detail(
            detail,
            if detail.features.complete {
                AttackFinish::Full
            } else {
                AttackFinish::Acquiring
            },
        ),
        None if onset < source.pcm_start => AllSideEvidence::Missing(KIRIN_REASON_NOT_KEPT),
        None => AllSideEvidence::Pending(KIRIN_REASON_WAITING_PUBLICATION),
    }
}
fn final_missing(side: AllSideEvidence<'_>) -> bool {
    matches!(side, AllSideEvidence::Missing(_))
}

pub(crate) fn events(
    history: &AttackHistory,
    view: &AttackObservationView,
    source: AttackSourceEvidence,
    paired: bool,
    target: u8,
) -> Vec<(SummaryEvent, bool)> {
    let item = |key_source: AttackSourceKey, sample, token, kind, pre, post, proof| {
        let lanes = classify_all_lanes(target, pre, post, kind, proof);
        let unfinished = super::cohort::unfinished(
            &lanes,
            target == KIRIN_TARGET_DELTA
                && kind == 0
                && !final_missing(pre)
                && !final_missing(post)
                && Result::is_err(&proof),
        );
        (
            SummaryEvent {
                key: super::poll::source_event(key_source, sample, token),
                kind,
                lanes,
                pre: None,
                post: None,
                proof_head: [0; 96],
                proof_tail: [0; 64],
            },
            unfinished,
        )
    };
    if !paired {
        return history
            .events()
            .filter(|event| source.source.matches(event))
            .map(|event| {
                let post = side(
                    history.details().find(|d| d.event == *event),
                    event.event_sample,
                    source,
                );
                item(
                    source.source,
                    event.event_sample,
                    event.event_sample as u64,
                    4,
                    AllSideEvidence::Missing(KIRIN_REASON_NO_PAIR),
                    post,
                    Err(KIRIN_REASON_NO_PAIR),
                )
            })
            .collect();
    }
    let proof = view.proof.expect("paired navigation supplies a proof");
    view.events
        .iter()
        .map(|content| {
            let pair = content.pair;
            let onset = pair.pre_event_sample.unwrap_or(content.event_sample);
            let pre_detail = pair.pre_event_sample.and_then(|at| {
                view.pre_history
                    .as_ref()?
                    .details()
                    .find(|d| d.event.event_sample == at)
            });
            let pre = if pair.pre_event_sample.is_some() {
                side(pre_detail, onset, proof.pre)
            } else {
                AllSideEvidence::Missing(KIRIN_REASON_NO_PAIR)
            };
            let post = side(
                view.post_anchored
                    .iter()
                    .find(|d| d.event.event_sample == onset),
                onset,
                source,
            );
            let verified = pre_detail
                .filter(|detail| {
                    detail.features.complete
                        && proof.validates_raw_span(
                            detail.shape.start_sample,
                            detail.features.body_end_sample,
                            proof.pre.source,
                            source.source,
                        )
                })
                .map(|_| super::attack_proof_revision(&proof))
                .ok_or(KIRIN_REASON_MAPPING);
            item(
                content.source,
                content.event_sample,
                content.token,
                pair.kind as u8,
                pre,
                post,
                verified,
            )
        })
        .collect()
}
