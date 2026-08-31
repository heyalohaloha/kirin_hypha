use super::*;

fn history() -> AttackHistory {
    let mut history = AttackHistory::with_capacity();
    let definition_hash = [7; 32];
    let frame = AttackOdfFrame {
        generation: 3,
        sample_rate: 48_000,
        channels: 2,
        definition_hash,
        window_samples: 2_048,
        hop_samples: 256,
        support_start_samples: 0,
        support_end_samples: 2_048,
        event_sample: 1_024,
        value: 0.25,
    };
    history.push(frame);
    history.push_waveform(AttackWaveformPoint {
        generation: 3,
        sample_rate: 48_000,
        channels: 2,
        start_sample: 0,
        end_sample: 480,
        peak_linear: 0.5,
        rms_dbfs: -12.0,
    });
    let event = AttackEvent {
        generation: 3,
        sample_rate: 48_000,
        channels: 2,
        definition_hash,
        event_sample: 1_024,
        decision_sample: 2_048,
        value: 0.25,
    };
    history.push_event(event);
    history.push_detail(AttackDetailedEvent {
        event,
        features: AttackPerceptualFeatures {
            sample_rate: 48_000,
            channels: 2,
            context_frames: 4_800,
            attack_frames: 1_440,
            contrast_db: 6.0,
            contrast_floor_limited: false,
            context_rms_dbfs: -24.0,
            attack_rms_dbfs: -18.0,
            sample_peak_dbfs: -6.0,
            crest_db: 12.0,
            sample_edge_ratio_db: -4.0,
            peak_plateau_ms: 2.5,
            temporal_centroid_ms: Some(8.0),
            sharpness_acum: Some(1.25),
        },
        shape: AttackEventShape {
            start_sample: -3_776,
            end_sample: 2_464,
            event_sample: 1_024,
            points: [0.5; ATTACK_SHAPE_POINT_CAPACITY],
        },
    });
    history
}

#[test]
fn attack_snapshot_round_trips_exact_history_and_request() {
    let request_id = Uuid::new_v4();
    let bytes = encode_attack_snapshot(request_id, &history());
    assert!(bytes.len() < ATTACK_SNAPSHOT_MAX_BYTES as usize);
    let decoded = decode_attack_snapshot(&bytes).unwrap();
    assert_eq!(decoded.request_id, request_id);
    assert_eq!(
        decoded.history.frames().copied().collect::<Vec<_>>(),
        history().frames().copied().collect::<Vec<_>>()
    );
    assert_eq!(
        decoded.history.waveform().copied().collect::<Vec<_>>(),
        history().waveform().copied().collect::<Vec<_>>()
    );
    assert_eq!(
        decoded.history.details().copied().collect::<Vec<_>>(),
        history().details().copied().collect::<Vec<_>>()
    );
}

#[test]
fn attack_snapshot_rejects_truncation_trailing_bytes_and_invalid_bool() {
    let bytes = encode_attack_snapshot(Uuid::new_v4(), &history());
    assert!(decode_attack_snapshot(&bytes[..bytes.len() - 1]).is_none());
    let mut trailing = bytes.clone();
    trailing.push(0);
    assert!(decode_attack_snapshot(&trailing).is_none());
    let detail_bool_offset = 92 + 12 + 24 + 20;
    let mut invalid_bool = bytes;
    invalid_bool[detail_bool_offset] = 2;
    assert!(decode_attack_snapshot(&invalid_bool).is_none());
}

#[test]
fn empty_history_has_no_publishable_payload() {
    assert!(encode_attack_snapshot(Uuid::new_v4(), &AttackHistory::default()).is_empty());
}
