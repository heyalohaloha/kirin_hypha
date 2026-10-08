use super::super::{
    decode_attack_snapshot, encode_attack_observation_snapshot, encode_attack_snapshot,
};
use super::*;
use crate::attack_perception::band::{AttackBandMeasure, BandEnvelope, BandSound, BandSpanEnd};
use crate::attack_runtime::semantics::band_semantic_hash;
use crate::attack_runtime::{AttackBandDetail, AttackOdfFrame};
use uuid::Uuid;

fn fixture() -> (AttackHistory, AttackObservationSnapshot, AttackBandResults) {
    let mut history = AttackHistory::with_capacity();
    for index in 0..128 {
        let at = 1024 + index * 256;
        history.push(AttackOdfFrame {
            generation: 7,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: [9; 32],
            window_samples: 2048,
            hop_samples: 256,
            support_start_samples: at - 1024,
            support_end_samples: at + 1024,
            event_sample: at,
            value: 0.0,
        });
    }
    let event = AttackEvent {
        generation: 7,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: [9; 32],
        event_sample: 10_000,
        decision_sample: 12_048,
        value: 0.5,
    };
    let pending = AttackEvent {
        event_sample: 30_000,
        decision_sample: 32_048,
        ..event
    };
    history.push_event(event);
    history.push_event(pending);
    let band = AttackBand::from_index(5).unwrap();
    let measure = AttackBandMeasure {
        band,
        sample_rate: 48_000,
        channels: 2,
        event_sample: event.event_sample,
        span_end_sample: 17_200,
        span_end: BandSpanEnd::AudioEnd,
        peak_frames: 0.0,
        level_dbfs: -120.0,
        sound: BandSound::Silent,
        envelope: BandEnvelope::default(),
    };
    let finished = AttackBandObservation::measured(event, 24_400, 8, Some(measure));
    let waiting = AttackBandObservation::pending(pending, 44_400, 9);
    let source = AttackSourceEvidence {
        source: AttackSourceKey {
            incarnation: [4; 16],
            generation: 7,
            sample_rate: 48_000,
            channels: 2,
            odf_hash: [9; 32],
        },
        odf_support_start: 0,
        odf_support_end: 34_560,
        pcm_start: 0,
        pcm_end: 34_560,
        cutoff: 34_560,
        band_semantic_hash: band_semantic_hash(),
        clock_policy: 1,
    };
    let mut results = AttackBandResults::new(Some(band), 7);
    assert!(results.put_own(AttackBandDetail {
        event,
        band,
        span_end_sample: 24_400,
        measure: Some(measure)
    }));
    (
        history,
        AttackObservationSnapshot {
            source: Some(source),
            band: Some(band),
            revision: 9,
            own: vec![finished, waiting],
            anchored: Vec::new(),
        },
        results,
    )
}

#[test]
fn v5_restores_every_public_key_pending_and_partial_floor_masks_without_legacy_reinterpretation() {
    let (history, observation, results) = fixture();
    let id = Uuid::from_bytes([3; 16]);
    let bytes = encode_attack_observation_snapshot(id, &history, &observation);
    assert_eq!(&bytes[8..10], &[5, 0]);
    let decoded = decode_attack_snapshot(&bytes).unwrap();
    assert_eq!(decoded.request_id, id);
    assert_eq!(
        decoded.history.events().copied().collect::<Vec<_>>(),
        history.events().copied().collect::<Vec<_>>()
    );
    let typed = decoded.observations.unwrap();
    assert_eq!(typed, observation);
    assert_eq!(typed.own[0].requested_end, 24_400);
    assert_eq!(typed.own[0].actual_end, 17_200);
    assert!(typed.own[0].head_valid.iter().all(|v| *v == 1));
    assert!(typed.own[0].tail_valid[..32].iter().all(|v| *v == 1));
    assert!(typed.own[0].tail_valid[32..].iter().all(|v| *v == 0));
    assert_eq!(typed.own[1].finish, AttackFinish::Acquiring);
    assert!(typed.own[1].measure.is_none());
    let old = encode_attack_snapshot(id, &history, observation.band, &results);
    assert_eq!(&old[8..10], &[4, 0]);
    assert!(decode_attack_snapshot(&old).unwrap().observations.is_none());
}

#[test]
fn malformed_v5_unknown_tags_counts_declared_source_and_truncation_are_rejected() {
    let (history, observation, results) = fixture();
    let id = Uuid::from_bytes([3; 16]);
    let bytes = encode_attack_observation_snapshot(id, &history, &observation);
    let extension = encode_attack_snapshot(id, &history, observation.band, &results).len();
    // Literal extension offsets are an independent wire-layout expectation.
    for (at, patch) in [
        (extension + 28, vec![6]),
        (extension + 29, vec![1]),
        (extension + 152, vec![9]),
        (extension + 153, vec![1]),
        (extension + 154, vec![241, 0]),
        (extension + 156, vec![241, 0]),
        (extension + 160 + 2 * 20 + 44, vec![99]),
    ] {
        let mut bad = bytes.clone();
        bad[at..at + patch.len()].copy_from_slice(&patch);
        assert!(
            decode_attack_snapshot(&bad).is_none(),
            "invalid byte offset {at}"
        );
    }
    for length in [0, 91, extension + 159, bytes.len() - 1] {
        assert!(decode_attack_snapshot(&bytes[..length]).is_none());
    }
    let mut foreign = observation.clone();
    foreign.source.as_mut().unwrap().source.generation += 1;
    assert!(encode_attack_observation_snapshot(id, &history, &foreign).is_empty());
}
