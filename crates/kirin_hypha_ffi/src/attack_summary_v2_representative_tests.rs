use super::*;
use crate::attack_summary_v2::{
    all_summary_representative, KirinAttackBandSummaryV2, KirinAttackBandSummaryV2Request,
};
use kirin_measure::spectrum_exchange::{
    AttackContentEvent, AttackMappingProof, AttackObservationView,
};
use kirin_measure::{AttackHistory, AttackPairEvent, AttackPairEventKind};
use std::sync::Arc;

#[test]
fn all_representative_is_the_newest_completed_cohort_hit_and_is_stable_without_a_new_stamp() {
    let (engine, mut local) = super::tests::selected_fixture();
    engine
        .spectrum
        .set_attack_pair_authority(engine.pair_binding.generation(), None);
    let runtime = engine.attack_runtime.as_ref().unwrap();
    let base = runtime.try_poll_single(31).unwrap().detail.unwrap();
    let source = local.source.as_mut().unwrap();
    source.cutoff = 96_000;
    source.pcm_end = 96_000;
    source.odf_support_end = 96_000;
    runtime.fixture_publish_observation(local);
    let details: Vec<_> = (0..10)
        .map(|i| {
            let mut d = base;
            let at = i * 9600;
            d.event.event_sample = at;
            d.event.decision_sample += at;
            d.shape.event_sample = at;
            d.shape.start_sample += at;
            d.shape.end_sample += at;
            d.features.transient_db = Some(i as f32);
            d.features.body_rms_dbfs = Some(-18.0 - i as f32);
            d.features.window_start_sample += at;
            d.features.body_end_sample += at;
            d.features.complete = true;
            d
        })
        .collect();
    let events: Vec<_> = details.iter().map(|d| d.event).collect();
    runtime.fixture_publish_history(AttackHistory::fixture_from_events(&events, &details[..9]));
    let request = KirinAttackBandSummaryV2Request {
        version: 2,
        struct_size: 16,
        ..Default::default()
    };
    let summary = engine.attack_band_summary_v2(request).unwrap();
    assert_eq!(summary.cohort_count, 8);
    assert_eq!(summary.events[0].event_sample, 9600);
    assert_eq!(summary.events[7].event_sample, 76_800);
    assert_eq!(summary.newer_unfinished_count, 1);
    assert_eq!(summary.has_representative_pre, 0);
    assert_eq!(summary.has_representative_post, 1);
    // Literal fixture facts are independent from the producer's detail converter.
    assert_eq!(summary.representative_post.event_sample, 76_800);
    assert_eq!(summary.representative_post.body_end_sample, 83_040);
    assert_eq!(summary.representative_post.transient_db, 8.0);
    assert_eq!(summary.representative_post.body_rms_dbfs, -26.0);
    assert_eq!(summary.representative_post.shape, [0.5; 96]);
    assert_eq!(summary, engine.attack_band_summary_v2(request).unwrap());
    runtime.fixture_publish_history(AttackHistory::default());
    let empty = engine.attack_band_summary_v2(request).unwrap();
    assert_eq!(
        (
            empty.cohort_count,
            empty.has_representative_pre,
            empty.has_representative_post
        ),
        (0, 0, 0)
    );
    assert_eq!(empty.representative_post, Default::default());
}

#[test]
fn representative_pre_requires_the_complete_raw_span_but_post_only_remains_visible() {
    let (engine, local) = super::tests::selected_fixture();
    let detail = engine
        .attack_runtime
        .as_ref()
        .unwrap()
        .try_poll_single(31)
        .unwrap()
        .detail
        .unwrap();
    let source = local.source.unwrap();
    let proof = AttackMappingProof {
        authority_revision: 1,
        request_id: [1; 16],
        target_hash: [2; 32],
        pre: source,
        post: source,
        mapping_epoch: 1,
        support_start: 0,
        support_end: 48_000,
        band_semantic_hash: source.band_semantic_hash,
    };
    let pair = AttackPairEvent {
        pair_generation: 1,
        pre_generation: 1,
        post_generation: 1,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: source.source.odf_hash,
        event_sample: 0,
        decision_sample: 2,
        kind: AttackPairEventKind::Matched,
        pre_event_sample: Some(0),
        post_event_sample: Some(0),
        pre_value: Some(1.0),
        post_value: Some(1.0),
        delta_value: Some(0.0),
    };
    let history = AttackHistory::fixture_from_events(&[detail.event], &[detail]);
    let mut post = detail;
    post.shape.points = [0.25; kirin_measure::ATTACK_SHAPE_POINT_CAPACITY];
    post.features.attack_rms_dbfs = -24.0;
    post.features.sample_peak_dbfs = -12.0;
    post.features.body_rms_dbfs = Some(-30.0);
    let mut view = AttackObservationView {
        proof: Some(proof),
        pre_history: Some(Arc::new(history.clone())),
        post_anchored: vec![post],
        events: vec![AttackContentEvent {
            source: source.source,
            event_sample: 0,
            token: 99,
            pair,
        }],
        ..Default::default()
    };
    let mut summary = KirinAttackBandSummaryV2 {
        cohort_count: 1,
        ..Default::default()
    };
    summary.events[0] = KirinSnapshotEventKey {
        source: KirinSnapshotSourceKey {
            incarnation: source.source.incarnation,
            generation: source.source.generation,
            sample_rate: 48_000,
            channels: 2,
            odf_hash: source.source.odf_hash,
            reserved: [0; 3],
        },
        event_sample: 0,
        token: 99,
    };
    let check = |view: &AttackObservationView, has_pre| {
        let mut s = summary;
        all_summary_representative(&mut s, &history, view, source, true);
        assert_eq!(
            (s.has_representative_pre, s.has_representative_post),
            (has_pre, 1)
        );
        assert_eq!(s.representative_post.event_sample, 0);
        assert_eq!(s.representative_post.body_end_sample, 6240);
        assert_eq!(s.representative_post.shape, [0.25; 96]);
        if has_pre == 1 {
            assert_eq!(s.representative_pre.event_sample, 0);
            assert_eq!(s.representative_pre.shape, [0.5; 96]);
        }
        if has_pre == 0 {
            assert_eq!(s.representative_pre, Default::default());
        }
    };
    check(&view, 1);
    view.proof.as_mut().unwrap().support_end = detail.features.body_end_sample - 1;
    check(&view, 0);
    view.proof = Some(proof);
    view.events[0].pair.pre_event_sample = None;
    view.events[0].pair.kind = AttackPairEventKind::PostOnly;
    check(&view, 0);
}
