use super::super::band::{observation, scalar_proof};
use super::*;
use crate::attack_snapshot_classify::ATT;
use crate::attack_snapshot_classify::{classify_band_lanes, BandSideEvidence};
use kirin_measure::attack_perception::band::*;
use kirin_measure::attack_runtime::snapshot::{AttackBandObservation, AttackFinish};
use kirin_measure::attack_runtime::snapshot::{AttackObservationSnapshot, AttackSourceEvidence};
use kirin_measure::AttackEvent;
use kirin_measure::AttackHistory;

fn fixture() -> (
    KirinHyphaEngine,
    AttackObservationSnapshot,
    Vec<AttackEvent>,
) {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    engine
        .spectrum
        .set_attack_pair_authority(engine.pair_binding.generation(), None);
    let source = AttackSourceKey {
        incarnation: [1; 16],
        generation: 1,
        sample_rate: 48_000,
        channels: 2,
        odf_hash: [3; 32],
    };
    let band = AttackBand::from_index(1).unwrap();
    let events: Vec<_> = (1..=10)
        .map(|id| AttackEvent {
            generation: source.generation,
            sample_rate: source.sample_rate,
            channels: source.channels,
            definition_hash: source.odf_hash,
            event_sample: 96_000 + id * 12_000,
            decision_sample: 96_256 + id * 12_000,
            value: 1.0,
        })
        .collect();
    let own = events
        .iter()
        .take(8)
        .map(|event| {
            let end = event.event_sample + 12_000;
            AttackBandObservation {
                event: *event,
                requested_end: end,
                actual_end: end,
                finish: AttackFinish::Full,
                revision: 1,
                measure: Some(AttackBandMeasure {
                    band,
                    sample_rate: 48_000,
                    channels: 2,
                    event_sample: event.event_sample,
                    span_end_sample: end,
                    span_end: BandSpanEnd::NextHit,
                    peak_frames: 480.0,
                    level_dbfs: -20.0,
                    sound: BandSound::Rises {
                        arrival: BandArrival::At {
                            arrival_frames: 48.0,
                            attack_frames: 96.0,
                        },
                        release: BandRelease::At(4800.0),
                    },
                    envelope: BandEnvelope::default(),
                }),
                head_valid: [1; 96],
                tail_valid: [0; 64],
            }
        })
        .collect();
    let local = AttackObservationSnapshot {
        source: Some(AttackSourceEvidence {
            source,
            odf_support_start: 0,
            odf_support_end: 240_000,
            pcm_start: 0,
            pcm_end: 240_000,
            cutoff: 240_000,
            band_semantic_hash: kirin_measure::attack_runtime::semantics::band_semantic_hash(),
            clock_policy: 1,
        }),
        band: Some(band),
        revision: 1,
        own,
        anchored: Vec::new(),
    };
    (engine, local, events)
}

#[test]
fn live_poll_backfills_completed_hits_and_waits_for_publication() {
    let (engine, mut local, events) = fixture();
    local.own.push(AttackBandObservation {
        event: events[9],
        requested_end: 230_400,
        actual_end: events[9].event_sample,
        finish: AttackFinish::Acquiring,
        revision: 2,
        measure: None,
        head_valid: [0; 96],
        tail_valid: [0; 64],
    });
    let runtime = engine.attack_runtime.as_ref().unwrap();
    runtime.fixture_publish_history(AttackHistory::fixture_from_events(&events, &[]));
    runtime.fixture_publish_observation(local);
    let summary = engine
        .attack_band_summary_v2(KirinAttackBandSummaryV2Request {
            version: 2,
            struct_size: 16,
            band: 1,
            target: KIRIN_TARGET_POST,
            reserved: [0; 6],
        })
        .unwrap();
    assert_eq!(
        summary.events[..summary.cohort_count as usize]
            .iter()
            .map(|e| e.event_sample)
            .collect::<Vec<_>>(),
        events[..8]
            .iter()
            .map(|e| e.event_sample)
            .collect::<Vec<_>>()
    );
    assert_eq!(summary.newer_unfinished_count, 2);
    assert_eq!(
        (
            summary.has_representative_pre,
            summary.has_representative_post
        ),
        (0, 0)
    );
    assert_eq!(summary.representative_pre, Default::default());
    assert_eq!(summary.representative_post, Default::default());
    assert_eq!(summary.lanes[ATT].whole_interval.lower.value, 2.0);
}

#[test]
fn waiting_audio_service_publication_and_short_mapping_are_unfinished() {
    let (_, mut local, events) = fixture();
    let band = local.band.unwrap();
    for reason in [
        KIRIN_REASON_WAITING_AUDIO,
        KIRIN_REASON_WAITING_SERVICE,
        KIRIN_REASON_WAITING_PUBLICATION,
    ] {
        assert!(super::super::cohort::unfinished(
            &[KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_UNKNOWN, reason); 4],
            false
        ));
    }
    for reason in [KIRIN_REASON_NOT_KEPT, KIRIN_REASON_NO_PAIR] {
        let side = BandSideEvidence::Missing(reason);
        assert!(!super::super::cohort::unfinished(
            &[KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_UNKNOWN, reason); 4],
            false
        ));
        assert!(super::super::cohort::final_missing(side));
    }
    let evidence = local.source.unwrap();
    let mut proof = AttackMappingProof {
        authority_revision: 1,
        request_id: [4; 16],
        target_hash: [5; 32],
        pre: evidence,
        post: evidence,
        mapping_epoch: 1,
        support_start: 0,
        support_end: events[0].event_sample + 1000,
        band_semantic_hash: evidence.band_semantic_hash,
    };
    let side = BandSideEvidence::Observation(&local.own[0]);
    assert!(scalar_proof(
        Some(&proof),
        band,
        events[0].event_sample,
        side,
        side,
        evidence.source
    )
    .is_err());
    proof.support_end = evidence.cutoff;
    assert!(scalar_proof(
        Some(&proof),
        band,
        events[0].event_sample,
        side,
        side,
        evidence.source
    )
    .is_ok());
    local.own[0].finish = AttackFinish::NotKept;
    local.own[0].measure = None;
    assert!(super::super::cohort::final_missing(
        BandSideEvidence::Observation(&local.own[0])
    ));
}

#[test]
fn final_no_pair_is_completed_even_when_the_output_hit_is_still_acquiring() {
    let (engine, mut local, events) = fixture();
    local
        .own
        .push(AttackBandObservation::pending(events[9], 230_400, 2));
    let runtime = engine.attack_runtime.as_ref().unwrap();
    runtime.fixture_publish_history(AttackHistory::fixture_from_events(&events, &[]));
    runtime.fixture_publish_observation(local);
    let packet = engine
        .attack_band_summary_v2(KirinAttackBandSummaryV2Request {
            version: 2,
            struct_size: 16,
            band: 1,
            target: KIRIN_TARGET_DELTA,
            reserved: [0; 6],
        })
        .unwrap();
    assert_eq!(packet.newer_unfinished_count, 0);
    assert_eq!(
        packet.events[packet.cohort_count as usize - 1].event_sample,
        events[9].event_sample
    );
    assert!(packet
        .lanes
        .iter()
        .all(|lane| lane.whole_numeric_informative == 0));
}

#[test]
fn incompatible_band_meaning_is_final_update_pre_instead_of_waiting_for_mapping() {
    let (_, local, events) = fixture();
    let band = local.band.unwrap();
    let mut pre = local.clone();
    pre.source.as_mut().unwrap().band_semantic_hash = [9; 32];
    let a = observation(&pre, events[0].event_sample, None, band);
    let b = observation(&local, events[0].event_sample, None, band);
    assert!(matches!(
        a,
        BandSideEvidence::Missing(KIRIN_REASON_SEMANTICS)
    ));
    let evidence = local.source.unwrap();
    let proof = AttackMappingProof {
        authority_revision: 1,
        request_id: [4; 16],
        target_hash: [5; 32],
        pre: pre.source.unwrap(),
        post: evidence,
        mapping_epoch: 1,
        support_start: 0,
        support_end: evidence.cutoff,
        band_semantic_hash: evidence.band_semantic_hash,
    };
    let qualified = scalar_proof(
        Some(&proof),
        band,
        events[0].event_sample,
        a,
        b,
        evidence.source,
    );
    assert_eq!(qualified, Err(KIRIN_REASON_SEMANTICS));
    let lanes = classify_band_lanes(KIRIN_TARGET_DELTA, band, a, b, qualified);
    assert!(lanes
        .iter()
        .all(|lane| lane.reason == KIRIN_REASON_SEMANTICS));
    assert!(!super::super::cohort::unfinished(&lanes, false));
    assert!(super::super::cohort::final_missing(a));
}
