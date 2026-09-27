use super::*;

fn history() -> AttackHistory {
    history_with(true)
}

/// One hit, complete or with only its head measured (the body not final yet).
fn history_with(complete: bool) -> AttackHistory {
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
            bin_frames: 48,
            window_start_sample: 1_008,
            attack_rms_dbfs: -18.0,
            sample_peak_dbfs: -6.0,
            crest_db: 12.0,
            complete,
            body_end_sample: 1_008 + if complete { 130 } else { 30 } * 48,
            body_rms_dbfs: complete.then_some(-24.0),
            transient_db: complete.then_some(6.0),
            sharpness_acum: complete.then_some(1.25),
        },
        shape: AttackEventShape {
            start_sample: 1_008 - 20 * 48,
            end_sample: 1_008 + if complete { 130 } else { 42 } * 48,
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
fn a_head_only_detail_round_trips_and_its_flag_is_checked() {
    let head = history_with(false);
    assert_eq!(
        head.details().len(),
        1,
        "a head-only detail is a valid detail"
    );
    let bytes = encode_attack_snapshot(Uuid::new_v4(), &head);
    let decoded = decode_attack_snapshot(&bytes).unwrap();
    assert_eq!(
        decoded.history.details().copied().collect::<Vec<_>>(),
        head.details().copied().collect::<Vec<_>>()
    );
    let complete_flag_offset = 92 + 12 + 24 + 22;
    let mut invalid = bytes.clone();
    invalid[complete_flag_offset] = 2;
    assert!(decode_attack_snapshot(&invalid).is_none());
    let mut version_two = bytes;
    version_two[8..10].copy_from_slice(&2_u16.to_le_bytes());
    assert!(
        decode_attack_snapshot(&version_two).is_none(),
        "version 2 has no head-only details"
    );
}

#[test]
fn a_complete_detail_replaces_its_head_and_advances_the_revision() {
    let mut history = history_with(false);
    let complete = *history_with(true).details().next().unwrap();
    let before = history.revision();
    history.push_detail(complete);
    assert_eq!(history.details().len(), 1);
    assert_eq!(*history.details().next().unwrap(), complete);
    assert!(
        history.revision() > before,
        "PRE republishes a completed detail"
    );
    let head = *history_with(false).details().next().unwrap();
    let completed = history.revision();
    history.push_detail(head);
    assert_eq!(
        *history.details().next().unwrap(),
        complete,
        "never back to the head"
    );
    assert_eq!(history.revision(), completed);
}

#[test]
fn empty_history_has_no_publishable_payload() {
    assert!(encode_attack_snapshot(Uuid::new_v4(), &AttackHistory::default()).is_empty());
}
