use super::*;
use crate::attack_snapshot_classify::{classify_all_lanes, AllSideEvidence};
use crate::attack_summary_v2::{
    all_summary_events, assemble_summary, KirinAttackBandSummaryV2, KirinAttackBandSummaryV2Request,
};
use kirin_measure::attack_runtime::snapshot::AttackFinish;
use kirin_measure::spectrum_exchange::{
    AttackContentEvent, AttackMappingProof, AttackObservationView,
};
use kirin_measure::{AttackHistory, AttackPairEvent, AttackPairEventKind};
use std::sync::Arc;

#[test]
fn live_all_one_hit_and_locked_single_share_values_classes_reasons_and_units() {
    let (engine, _) = super::tests::selected_fixture();
    engine
        .spectrum
        .set_attack_pair_authority(engine.pair_binding.generation(), None);
    let runtime = engine.attack_runtime.as_ref().unwrap();
    let mut state = runtime.try_poll_single(31).unwrap();
    let detail = state.detail.unwrap();
    state.request.key_token = detail.event.event_sample as u64;
    runtime.fixture_select_single(state.clone());
    runtime.fixture_publish_history(AttackHistory::fixture_from_events(
        &[detail.event],
        &[detail],
    ));
    let summary = engine
        .attack_band_summary_v2(KirinAttackBandSummaryV2Request {
            version: 2,
            struct_size: 16,
            band: 0,
            ..Default::default()
        })
        .unwrap();
    let single = engine.attack_single_v2(31).unwrap();
    assert_eq!(summary.events[0], single.event);
    assert_eq!(summary.cohort_count, 1);
    for (i, (expected, unit)) in [
        (6.0, KIRIN_INTERVAL_DECIBELS),
        (-18.0, KIRIN_INTERVAL_DBFS),
        (12.0, KIRIN_INTERVAL_DECIBELS),
        (1.25, KIRIN_INTERVAL_ACUM),
    ]
    .into_iter()
    .enumerate()
    {
        assert_eq!(
            summary.lanes[i].whole_interval,
            KirinSnapshotInterval::point(expected, unit)
        );
        assert_eq!(summary.evidence[0][i].interval, single.lanes[i].interval);
        assert_eq!(summary.evidence[0][i].class, single.lanes[i].class);
        assert_eq!(summary.evidence[0][i].reason, single.lanes[i].reason);
        assert_eq!(single.lanes[i].class, KIRIN_SCALAR_EXACT);
    }
}

#[test]
fn live_all_delta_is_the_median_of_per_hit_differences_and_matches_single() {
    let (engine, local) = super::tests::selected_fixture();
    let runtime = engine.attack_runtime.as_ref().unwrap();
    let mut state = runtime.try_poll_single(31).unwrap();
    let evidence = local.source.unwrap();
    let pre = state.detail.unwrap();
    let mut post = pre;
    post.features.transient_db = Some(3.0);
    post.features.attack_rms_dbfs = -12.0;
    post.features.crest_db = 8.0;
    post.features.sharpness_acum = Some(1.5);
    let proof = AttackMappingProof {
        authority_revision: 1,
        request_id: [1; 16],
        target_hash: [2; 32],
        pre: evidence,
        post: evidence,
        mapping_epoch: 1,
        support_start: 0,
        support_end: 48_000,
        band_semantic_hash: evidence.band_semantic_hash,
    };
    state.request.target = KIRIN_TARGET_DELTA;
    state.request.pair_kind = 0;
    state.request.pre_detail = Some(pre);
    state.request.pre_source = Some(evidence.source);
    state.request.proof_token = proof.binding_token();
    state.detail = Some(post);
    let pair = AttackPairEvent {
        pair_generation: 1,
        pre_generation: 1,
        post_generation: 1,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: evidence.source.odf_hash,
        event_sample: 0,
        decision_sample: 2,
        kind: AttackPairEventKind::Matched,
        pre_event_sample: Some(0),
        post_event_sample: Some(0),
        pre_value: Some(1.0),
        post_value: Some(1.0),
        delta_value: Some(0.0),
    };
    let history = AttackHistory::fixture_from_events(&[pre.event], &[pre]);
    let mut view = AttackObservationView {
        proof: Some(proof),
        pre: Some(Arc::new(local.clone())),
        post: Some(Arc::new(local.clone())),
        pre_history: Some(Arc::new(history.clone())),
        post_anchored: vec![post],
        events: vec![AttackContentEvent {
            source: evidence.source,
            event_sample: 0,
            token: 1,
            pair,
        }],
        ..Default::default()
    };
    let candidates = all_summary_events(&history, &view, evidence, true, KIRIN_TARGET_DELTA);
    assert!(!candidates[0].1);
    let single = assemble::assemble(&state, local.source, &view, 1);
    let mut header = single.header;
    header.kind = KIRIN_SNAPSHOT_BAND_SUMMARY;
    header.struct_size = std::mem::size_of::<KirinAttackBandSummaryV2>() as u32;
    let summary = assemble_summary(header, &[candidates[0].0.clone()]).unwrap();
    for (i, expected) in [-3.0, 6.0, -4.0, 0.25].into_iter().enumerate() {
        assert_eq!(summary.lanes[i].whole_interval.lower.value, expected);
        assert_eq!(summary.evidence[0][i].interval, single.lanes[i].interval);
        assert_eq!(
            (summary.evidence[0][i].class, summary.evidence[0][i].reason),
            (single.lanes[i].class, single.lanes[i].reason)
        );
    }
    view.proof.as_mut().unwrap().support_end = 3000;
    assert!(all_summary_events(&history, &view, evidence, true, KIRIN_TARGET_DELTA)[0].1);
    // These three paired distributions have a median difference of +1 dB;
    // subtracting the two marginal medians would incorrectly give 0 dB.
    let mut pre_details = Vec::new();
    let mut post_details = Vec::new();
    let mut content = Vec::new();
    for (i, (a, b)) in [(0.0, 100.0), (100.0, 101.0), (101.0, 0.0)]
        .into_iter()
        .enumerate()
    {
        let at = i as i64 * 12_000;
        let mut left = pre;
        left.event.event_sample = at;
        left.event.decision_sample = at + 2;
        left.features.window_start_sample = at;
        left.features.body_end_sample += at;
        left.features.transient_db = Some(a);
        left.shape.start_sample += at;
        left.shape.end_sample += at;
        left.shape.event_sample = at;
        let mut right = left;
        right.features.transient_db = Some(b);
        pre_details.push(left);
        post_details.push(right);
        content.push(AttackContentEvent {
            source: evidence.source,
            event_sample: at,
            token: i as u64,
            pair: AttackPairEvent {
                event_sample: at,
                decision_sample: at + 2,
                pre_event_sample: Some(at),
                post_event_sample: Some(at),
                ..pair
            },
        });
    }
    let events: Vec<_> = pre_details.iter().map(|d| d.event).collect();
    let history = AttackHistory::fixture_from_events(&events, &pre_details);
    view.proof = Some(proof);
    view.pre_history = Some(Arc::new(history.clone()));
    view.post_anchored = post_details;
    view.events = content;
    let rows: Vec<_> = all_summary_events(&history, &view, evidence, true, KIRIN_TARGET_DELTA)
        .into_iter()
        .map(|(event, unfinished)| {
            assert!(!unfinished);
            event
        })
        .collect();
    assert_eq!(
        assemble_summary(header, &rows).unwrap().lanes[0]
            .whole_interval
            .lower
            .value,
        1.0
    );
}

#[test]
fn all_classifier_preserves_head_completion_body_cut_quiet_body_and_pair_failures() {
    let (engine, _) = super::tests::selected_fixture();
    let state = engine
        .attack_runtime
        .as_ref()
        .unwrap()
        .try_poll_single(31)
        .unwrap();
    let full = state.detail.unwrap();
    let mut head = full;
    head.features.complete = false;
    head.features.body_end_sample = 1440;
    head.features.body_rms_dbfs = None;
    head.features.transient_db = None;
    head.features.sharpness_acum = None;
    let missing = AllSideEvidence::Missing(KIRIN_REASON_NO_PAIR);
    let lanes = |detail: &kirin_measure::AttackDetailedEvent, finish| {
        classify_all_lanes(
            KIRIN_TARGET_POST,
            missing,
            AllSideEvidence::Detail(detail, finish),
            4,
            Err(KIRIN_REASON_NO_PAIR),
        )
    };
    let pending = lanes(&head, AttackFinish::Acquiring);
    assert_eq!(
        [
            pending[0].class,
            pending[1].class,
            pending[2].class,
            pending[3].class
        ],
        [
            KIRIN_SCALAR_PENDING,
            KIRIN_SCALAR_EXACT,
            KIRIN_SCALAR_EXACT,
            KIRIN_SCALAR_PENDING
        ]
    );
    let ended = lanes(&head, AttackFinish::AudioEnd);
    assert_eq!(ended[0].reason, KIRIN_REASON_AUDIO_END);
    head.features.complete = true;
    assert_eq!(
        lanes(&head, AttackFinish::Full)[0].reason,
        KIRIN_REASON_NEXT_HIT
    );
    let mut quiet = full;
    quiet.features.body_rms_dbfs = Some(-73.0);
    assert_eq!(
        lanes(&quiet, AttackFinish::Full)[0].class,
        KIRIN_SCALAR_NOT_APPLICABLE
    );
    quiet.features.body_rms_dbfs = Some(-72.0);
    assert_eq!(
        lanes(&quiet, AttackFinish::Full)[0].class,
        KIRIN_SCALAR_EXACT
    );
    let side = AllSideEvidence::Detail(&full, AttackFinish::Full);
    assert!(
        classify_all_lanes(KIRIN_TARGET_DELTA, side, side, 0, Err(KIRIN_REASON_MAPPING))
            .iter()
            .all(|v| v.reason == KIRIN_REASON_MAPPING)
    );
    assert!(classify_all_lanes(KIRIN_TARGET_DELTA, side, side, 2, Ok(1))
        .iter()
        .all(|v| v.reason == KIRIN_REASON_NO_PAIR));
}

#[test]
fn all_summary_uses_navigation_source_through_a_band_transition() {
    let (engine, mut local) = super::tests::selected_fixture();
    engine
        .spectrum
        .set_attack_pair_authority(engine.pair_binding.generation(), None);
    let runtime = engine.attack_runtime.as_ref().unwrap();
    let detail = runtime.try_poll_single(31).unwrap().detail.unwrap();
    local.band = kirin_measure::attack_perception::band::AttackBand::from_index(1);
    runtime.fixture_publish_observation(local);
    runtime.fixture_publish_history(AttackHistory::fixture_from_events(
        &[detail.event],
        &[detail],
    ));
    let request = KirinAttackBandSummaryV2Request {
        version: 2,
        struct_size: 16,
        ..Default::default()
    };
    assert_eq!(
        engine.attack_band_summary_v2(request).unwrap().lanes[0]
            .whole_interval
            .lower
            .value,
        6.0
    );
    assert_eq!(
        engine.attack_band_summary_v2(KirinAttackBandSummaryV2Request { band: 2, ..request }),
        Err(KIRIN_SNAPSHOT_INVALID_REQUEST)
    );
}
