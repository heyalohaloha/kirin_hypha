use super::*;
use crate::attack_perception::band::{
    AttackBand, AttackBandMeasure, ATTACK_BAND_HEAD_POINTS, ATTACK_BAND_HISTORY_CAPACITY,
    ATTACK_BAND_TAIL_POINTS,
};
use crate::attack_runtime::AttackBandDetail;

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
    let bytes = encode_attack_snapshot(request_id, &history(), None);
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
    let bytes = encode_attack_snapshot(Uuid::new_v4(), &history(), None);
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
    let bytes = encode_attack_snapshot(Uuid::new_v4(), &head, None);
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
    assert!(encode_attack_snapshot(Uuid::new_v4(), &AttackHistory::default(), None).is_empty());
}

fn band_history() -> AttackHistory {
    let mut history = history();
    let event = *history.events().next_back().unwrap();
    let mut head_dbfs = [-60.0_f32; ATTACK_BAND_HEAD_POINTS];
    head_dbfs[40] = -12.0;
    let mut tail_dbfs = [-40.0_f32; ATTACK_BAND_TAIL_POINTS];
    tail_dbfs[0] = -12.0;
    history.push_band_detail(AttackBandDetail {
        event,
        measure: AttackBandMeasure {
            band: AttackBand::from_index(3).unwrap(),
            sample_rate: 48_000,
            channels: 2,
            event_sample: event.event_sample,
            span_end_sample: event.event_sample + 14_400,
            peak_frames: 300.0,
            level_dbfs: -12.0,
            arrival_frames: Some(20.5),
            attack_frames: Some(240.0),
            release_frames: None,
            head_dbfs,
            tail_dbfs,
        },
    });
    assert_eq!(history.band_details().len(), 1);
    history
}

#[test]
fn a_band_snapshot_round_trips_its_measures_and_a_plain_one_has_none() {
    let request_id = Uuid::new_v4();
    let band = AttackBand::from_index(3);
    let bytes = encode_attack_snapshot(request_id, &band_history(), band);
    assert!(bytes.len() < ATTACK_SNAPSHOT_MAX_BYTES as usize);
    let decoded = decode_attack_snapshot(&bytes).unwrap();
    assert_eq!(decoded.band, band);
    assert_eq!(
        decoded.history.band_details().copied().collect::<Vec<_>>(),
        band_history().band_details().copied().collect::<Vec<_>>()
    );
    // The same history published without a band is version 3: no band, no measures.
    let plain =
        decode_attack_snapshot(&encode_attack_snapshot(request_id, &band_history(), None)).unwrap();
    assert_eq!(plain.band, None);
    assert_eq!(plain.history.band_details().len(), 0);
    // Measures of another band than the declared one are left out.
    let other = decode_attack_snapshot(&encode_attack_snapshot(
        request_id,
        &band_history(),
        AttackBand::from_index(5),
    ))
    .unwrap();
    assert_eq!(other.band, AttackBand::from_index(5));
    assert_eq!(other.history.band_details().len(), 0);
}

#[test]
fn a_band_snapshot_is_bounded_and_rejects_a_measure_of_an_unknown_hit() {
    let mut full = history();
    let identity = *full.newest().unwrap();
    for index in 0..ATTACK_BAND_HISTORY_CAPACITY as i64 + 8 {
        let event = AttackEvent {
            event_sample: 1_024 + (index + 1) * 4_800,
            decision_sample: 2_048 + (index + 1) * 4_800,
            ..*full.events().next_back().unwrap()
        };
        full.push(AttackOdfFrame {
            event_sample: event.event_sample,
            support_start_samples: event.event_sample - 1_024,
            support_end_samples: event.event_sample + 1_024,
            ..identity
        });
        full.push_event(event);
        full.push_band_detail(AttackBandDetail {
            event,
            measure: AttackBandMeasure {
                event_sample: event.event_sample,
                span_end_sample: event.event_sample + 4_800,
                ..band_history().band_details().next().unwrap().measure
            },
        });
    }
    assert_eq!(full.band_details().len(), ATTACK_BAND_HISTORY_CAPACITY);
    let bytes = encode_attack_snapshot(Uuid::new_v4(), &full, AttackBand::from_index(3));
    assert!(bytes.len() < ATTACK_SNAPSHOT_MAX_BYTES as usize);
    assert_eq!(
        decode_attack_snapshot(&bytes)
            .unwrap()
            .history
            .band_details()
            .len(),
        ATTACK_BAND_HISTORY_CAPACITY
    );
    // A band measure whose span ends before its onset is refused whole.
    let mut bytes =
        encode_attack_snapshot(Uuid::new_v4(), &band_history(), AttackBand::from_index(3));
    let band_section = bytes.len() - 4 - 692;
    bytes[band_section + 24..band_section + 32].copy_from_slice(&0_i64.to_le_bytes());
    assert!(decode_attack_snapshot(&bytes).is_none());
}
