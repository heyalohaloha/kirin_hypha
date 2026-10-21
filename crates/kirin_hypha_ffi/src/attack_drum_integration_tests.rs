//! A known onset ledger drives real measurements, then the public Summary poll. No worker waits.
use super::*;
use kirin_measure::attack_runtime::single::test_support::drum;
use kirin_measure::attack_runtime::snapshot::AttackBandObservation;
use kirin_measure::attack_runtime::snapshot::{AttackObservationSnapshot, AttackSourceEvidence};
use kirin_measure::spectrum_exchange::AttackObservationView;
use kirin_measure::spectrum_exchange::{AttackContentEvent, AttackPairAuthority};
use kirin_measure::AttackHistory;
use kirin_measure::{
    AttackEvent, AttackPairEvent, AttackPairEventKind, LatchedPre, LatchedPreReadiness,
};
use std::sync::Arc;

const RATE: u32 = 48_000;

fn rhythm() -> (Vec<f32>, Vec<AttackEvent>) {
    let events: Vec<_> = (0..14)
        .map(|i| AttackEvent {
            generation: 1,
            sample_rate: RATE,
            channels: 2,
            definition_hash: [3; 32],
            event_sample: 24_000 + i * 12_000,
            decision_sample: 24_000 + i * 12_000,
            value: 1.0,
        })
        .collect();
    let mut audio = Vec::with_capacity(48_000 * 9);
    for frame in 0..216_000 {
        let mut value = 0.0;
        for (i, event) in events.iter().enumerate() {
            let t = (frame - event.event_sample) as f64 / f64::from(RATE);
            if !(0.0..0.35).contains(&t) {
                continue;
            }
            let sine = |hz: f64| (std::f64::consts::TAU * hz * t).sin();
            let rise = (t / 0.002).min(1.0);
            // Eighth-note hats, alternating kick and snare on the quarter-note grid.
            value += rise * 0.16 * (-t / 0.018).exp() * (sine(4000.0) + 0.4 * sine(6500.0));
            if i % 4 == 0 {
                value += rise * 0.55 * (-t / 0.04).exp() * sine(63.0);
            }
            if i % 4 == 2 {
                value += rise * 0.3 * (-t / 0.045).exp() * (sine(1000.0) + 0.4 * sine(1600.0));
            }
        }
        audio.extend([value as f32; 2]);
    }
    (audio, events)
}

#[test]
fn drum_rhythm_completed_second_hit_has_values_and_identical_pair_never_returns_to_missing() {
    let (audio, events) = rhythm();
    let details = drum::details(&audio, &events);
    let post_events: Vec<_> = events
        .iter()
        .map(|e| AttackEvent {
            generation: 2,
            ..*e
        })
        .collect();
    let post_details = drum::details(&audio, &post_events);
    assert_eq!(details.len(), events.len());
    assert!(details
        .iter()
        .all(|d| d.features.complete && d.features.sharpness_acum.is_some()));
    for index in [0, 2, 5, 7] {
        let engine =
            KirinHyphaEngine::new(RATE, kirin_measure::channel_layout::ChannelLayout::stereo());
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        // Paths are opaque locator inputs only: this fixture never starts IO or writes files.
        engine.pair_binding.replace_exact(
            "fixture".into(),
            LatchedPre {
                name: "fixture".into(),
                instance_id: "fixture-pre".into(),
                project_dir: "fixture-project".into(),
                pre_json: "fixture-project/fixture-pre.json".into(),
                daw_session_id: None,
                host_process_id: None,
                readiness: LatchedPreReadiness::Confirmed,
            },
        );
        let authority = AttackSnapshotAuthority::read(&engine).unwrap();
        let origin = AttackPairAuthority {
            generation: authority.pair.generation,
            project_hash: "fixture-project".into(),
            pre_instance_id: "fixture-pre".into(),
            post_instance_id: authority.post_id.clone(),
            owner_id: authority.owner.clone(),
            claimed_at_bits: authority.claim,
        };
        let band = AttackBand::from_index(index);
        let measured = band
            .map(|band| drum::band(&audio, &events, band))
            .unwrap_or_default();
        let post_measured = band
            .map(|band| drum::band(&audio, &post_events, band))
            .unwrap_or_default();
        let ready = |i: usize| {
            if let Some(band) = band {
                measured[i].actual_end + band.period_frames(RATE) / 2 + 1
            } else {
                details[i].features.body_end_sample
            }
        };
        let mut seen = [false; 4];
        let mut first_value = None;
        let mut stamps = 0;
        for cutoff in (24_000..=192_000).step_by(480) {
            let post_source = AttackSourceKey {
                incarnation: [2; 16],
                generation: 2,
                sample_rate: RATE,
                channels: 2,
                odf_hash: [3; 32],
            };
            let pre_source = AttackSourceKey {
                incarnation: [1; 16],
                generation: 1,
                ..post_source
            };
            let evidence = |source| AttackSourceEvidence {
                source,
                odf_support_start: 0,
                odf_support_end: cutoff,
                pcm_start: 0,
                pcm_end: cutoff,
                cutoff,
                clock_policy: 1,
                band_semantic_hash: kirin_measure::attack_runtime::semantics::band_semantic_hash(),
            };
            let visible: Vec<_> = events
                .iter()
                .copied()
                .filter(|e| e.event_sample <= cutoff)
                .collect();
            let complete: Vec<_> = details
                .iter()
                .copied()
                .enumerate()
                .filter(|(i, _)| ready(*i) <= cutoff)
                .map(|(_, d)| d)
                .collect();
            let pre_history = AttackHistory::fixture_from_events(&visible, &complete);
            let post_visible: Vec<_> = post_events
                .iter()
                .copied()
                .filter(|e| e.event_sample <= cutoff)
                .collect();
            let post_complete: Vec<_> = post_details
                .iter()
                .copied()
                .enumerate()
                .filter(|(i, _)| ready(*i) <= cutoff)
                .map(|(_, d)| d)
                .collect();
            let history = AttackHistory::fixture_from_events(&post_visible, &post_complete);
            let observations = |visible: &[AttackEvent], measured: &[AttackBandObservation]| {
                visible
                    .iter()
                    .enumerate()
                    .filter_map(|(i, event)| {
                        if ready(i) <= cutoff {
                            measured.get(i).copied()
                        } else {
                            band.map(|_| {
                                AttackBandObservation::pending(
                                    *event,
                                    measured[i].requested_end,
                                    cutoff as u64,
                                )
                            })
                        }
                    })
                    .collect::<Vec<_>>()
            };
            let own = observations(&post_visible, &post_measured);
            let local = AttackObservationSnapshot {
                source: Some(evidence(post_source)),
                band,
                revision: cutoff as u64,
                own: own.clone(),
                anchored: own,
            };
            let pre = AttackObservationSnapshot {
                source: Some(evidence(pre_source)),
                own: observations(&visible, &measured),
                ..local.clone()
            };
            let proof = AttackMappingProof {
                authority_revision: authority.pair.generation,
                request_id: [4; 16],
                target_hash: [5; 32],
                pre: evidence(pre_source),
                post: evidence(post_source),
                mapping_epoch: 1,
                support_start: 0,
                support_end: cutoff,
                band_semantic_hash: evidence(post_source).band_semantic_hash,
            };
            let pairs = visible
                .iter()
                .enumerate()
                .map(|(i, e)| AttackContentEvent {
                    source: pre_source,
                    event_sample: e.event_sample,
                    token: i as u64 + 1,
                    pair: AttackPairEvent {
                        pair_generation: authority.pair.generation,
                        pre_generation: 1,
                        post_generation: 2,
                        sample_rate: RATE,
                        channels: 2,
                        definition_hash: e.definition_hash,
                        event_sample: e.event_sample,
                        decision_sample: e.decision_sample,
                        kind: AttackPairEventKind::Matched,
                        pre_event_sample: Some(e.event_sample),
                        post_event_sample: Some(e.event_sample),
                        pre_value: Some(1.0),
                        post_value: Some(1.0),
                        delta_value: Some(0.0),
                    },
                })
                .collect();
            engine
                .spectrum
                .fixture_publish_attack_view(AttackObservationView {
                    status: SpectrumViewStatus::Active,
                    authority_revision: authority.pair.generation,
                    target_hash: proof.target_hash,
                    proof: Some(proof),
                    origin: Some(origin.clone()),
                    pre: Some(Arc::new(pre)),
                    post: Some(Arc::new(local.clone())),
                    pre_history: Some(Arc::new(pre_history)),
                    post_history: Some(Arc::new(history.clone())),
                    post_anchored: post_complete,
                    events: pairs,
                    mapping_request_id: proof.request_id,
                    mapping_source_pair: Some((pre_source, post_source)),
                    ..Default::default()
                });
            let runtime = engine.attack_runtime.as_ref().unwrap();
            runtime.fixture_publish_history(history);
            runtime.fixture_publish_observation(local);
            let packet = engine
                .attack_band_summary_v2(KirinAttackBandSummaryV2Request {
                    version: 2,
                    struct_size: 16,
                    band: index,
                    target: KIRIN_TARGET_DELTA,
                    reserved: [0; 6],
                })
                .unwrap();
            assert_eq!(packet.comparison_binding, proof.binding_token());
            stamps += 1;
            for (i, lane) in packet.lanes.iter().enumerate() {
                let has_value = lane.whole_numeric_informative == 1;
                if cutoff >= ready(1) {
                    assert!(has_value, "band {index} lane {i} at {cutoff}");
                }
                if seen[i] {
                    assert!(
                        has_value,
                        "returned to missing: band {index} lane {i} at {cutoff}"
                    );
                }
                seen[i] |= has_value;
                if has_value {
                    assert_eq!(
                        (
                            lane.whole_interval.lower.value,
                            lane.whole_interval.upper.value
                        ),
                        (0.0, 0.0)
                    );
                }
            }
            if seen.iter().all(|v| *v) && first_value.is_none() {
                first_value = Some(cutoff);
            }
        }
        assert!(seen.iter().all(|v| *v));
        println!("rhythm band {index}: first all-lane value at {} samples ({:.1} ms from first onset), second complete at {}, {stamps} content stamps, missing returns 0",
            first_value.unwrap(), (first_value.unwrap() - events[0].event_sample) as f64 / 48.0, ready(1));
    }
}
