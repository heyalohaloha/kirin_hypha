use super::*;
use crate::attack_perception::band::{
    centi_db, AttackBand, AttackBandMeasure, BandArrival, BandEnvelope, BandRelease, BandSound,
    BandSpanEnd,
};
use crate::attack_runtime::{AttackBandDetail, AttackBandResults};

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
    let bytes = encode_attack_snapshot(request_id, &history(), None, &AttackBandResults::default());
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
    let bytes = encode_attack_snapshot(
        Uuid::new_v4(),
        &history(),
        None,
        &AttackBandResults::default(),
    );
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
    let bytes = encode_attack_snapshot(Uuid::new_v4(), &head, None, &AttackBandResults::default());
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
    assert!(encode_attack_snapshot(
        Uuid::new_v4(),
        &AttackHistory::default(),
        None,
        &AttackBandResults::default()
    )
    .is_empty());
}

fn band3() -> AttackBand {
    AttackBand::from_index(3).unwrap()
}

/// One band result at `event`: measured with `sound` over a 300 ms or next-hit tail, or not kept.
fn band_detail(event: AttackEvent, sound: Option<BandSound>, next_hit: bool) -> AttackBandDetail {
    let span_end_sample = event.event_sample + if next_hit { 4_800 } else { 14_400 };
    let mut envelope = BandEnvelope::default();
    envelope.head[40] = centi_db(-12.0);
    envelope.tail[0] = centi_db(-12.25);
    AttackBandDetail {
        event,
        band: band3(),
        span_end_sample,
        measure: sound.map(|sound| AttackBandMeasure {
            band: band3(),
            sample_rate: 48_000,
            channels: 2,
            event_sample: event.event_sample,
            span_end_sample,
            span_end: if next_hit {
                BandSpanEnd::NextHit
            } else {
                BandSpanEnd::Window
            },
            peak_frames: 300.0,
            level_dbfs: if sound == BandSound::Silent {
                -80.0
            } else {
                -12.0
            },
            sound,
            envelope,
        }),
    }
}

/// Six hits, one of every outcome a band result states.
fn every_outcome() -> (AttackHistory, AttackBandResults) {
    let mut history = history();
    let identity = *history.newest().unwrap();
    let first = *history.events().next_back().unwrap();
    let mut results = AttackBandResults::new(Some(band3()), first.generation);
    let rises = |arrival, release| Some(BandSound::Rises { arrival, release });
    let timed = BandArrival::At {
        arrival_frames: 20.5,
        attack_frames: 240.0,
    };
    let outcomes = [
        (rises(timed, BandRelease::At(4_000.0)), false),
        (rises(BandArrival::Ringing, BandRelease::CutByNextHit), true),
        (rises(timed, BandRelease::AtLeast(14_100.0)), false),
        (Some(BandSound::RingsOn), false),
        (Some(BandSound::Silent), false),
        (None, false),
    ];
    for (index, (sound, next_hit)) in outcomes.into_iter().enumerate() {
        let event = AttackEvent {
            event_sample: first.event_sample + index as i64 * 9_600,
            decision_sample: first.decision_sample + index as i64 * 9_600,
            ..first
        };
        if index > 0 {
            history.push(AttackOdfFrame {
                event_sample: event.event_sample,
                support_start_samples: event.event_sample - 1_024,
                support_end_samples: event.event_sample + 1_024,
                ..identity
            });
            history.push_event(event);
        }
        assert!(results.put_own(band_detail(event, sound, next_hit)));
    }
    (history, results)
}

#[test]
fn a_band_snapshot_round_trips_every_outcome_and_a_plain_one_has_none() {
    let request_id = Uuid::new_v4();
    let (history, results) = every_outcome();
    let bytes = encode_attack_snapshot(request_id, &history, Some(band3()), &results);
    let decoded = decode_attack_snapshot(&bytes).unwrap();
    assert_eq!(decoded.band_results.as_ref(), Some(&results));
    // Without a requested band the snapshot is version 3: no band, no hits.
    let plain = decode_attack_snapshot(&encode_attack_snapshot(
        request_id, &history, None, &results,
    ))
    .unwrap();
    assert_eq!(plain.band_results, None);
    // PRE declares the requested band at once, with no hits while its results are another band's.
    let declared = decode_attack_snapshot(&encode_attack_snapshot(
        request_id,
        &history,
        AttackBand::from_index(5),
        &results,
    ))
    .unwrap()
    .band_results
    .unwrap();
    assert_eq!(declared.band, AttackBand::from_index(5));
    assert!(declared.own().is_empty());
}

#[test]
fn every_history_at_its_bounds_fits_the_snapshot() {
    let mut history = AttackHistory::with_capacity();
    let identity = *self::history().newest().unwrap();
    for index in 0..ATTACK_ODF_HISTORY_CAPACITY as i64 {
        history.push(AttackOdfFrame {
            event_sample: 1_024 + index * 256,
            support_start_samples: index * 256,
            support_end_samples: 2_048 + index * 256,
            ..identity
        });
    }
    for index in 0..ATTACK_WAVEFORM_HISTORY_CAPACITY as i64 {
        history.push_waveform(AttackWaveformPoint {
            generation: 3,
            sample_rate: 48_000,
            channels: 2,
            start_sample: index * 480,
            end_sample: (index + 1) * 480,
            peak_linear: 0.5,
            rms_dbfs: -12.0,
        });
    }
    let template = *self::history().details().next().unwrap();
    let mut results = AttackBandResults::new(Some(band3()), 3);
    for index in 0..ATTACK_EVENT_HISTORY_CAPACITY as i64 {
        let event = AttackEvent {
            event_sample: 1_024 + index * 1_200,
            decision_sample: 2_048 + index * 1_200,
            ..template.event
        };
        history.push_event(event);
        history.push_detail(AttackDetailedEvent {
            event,
            features: AttackPerceptualFeatures {
                window_start_sample: event.event_sample / 48 * 48,
                body_end_sample: event.event_sample / 48 * 48 + 130 * 48,
                ..template.features
            },
            shape: AttackEventShape {
                start_sample: event.event_sample / 48 * 48 - 20 * 48,
                end_sample: event.event_sample / 48 * 48 + 130 * 48,
                event_sample: event.event_sample,
                ..template.shape
            },
        });
        let sound = BandSound::Rises {
            arrival: BandArrival::Ringing,
            release: BandRelease::AtLeast(100.0),
        };
        assert!(results.put_own(band_detail(event, Some(sound), false)));
    }
    assert_eq!(history.frames().len(), ATTACK_ODF_HISTORY_CAPACITY);
    assert_eq!(history.waveform().len(), ATTACK_WAVEFORM_HISTORY_CAPACITY);
    assert_eq!(history.details().len(), ATTACK_EVENT_HISTORY_CAPACITY);
    assert_eq!(results.own().len(), ATTACK_BAND_HISTORY_CAPACITY);
    let bytes = encode_attack_snapshot(Uuid::new_v4(), &history, Some(band3()), &results);
    assert_eq!(bytes.len(), ATTACK_SNAPSHOT_WORST_CASE_BYTES);
    assert!(bytes.len() as u64 <= ATTACK_SNAPSHOT_MAX_BYTES);
    let decoded = decode_attack_snapshot(&bytes).unwrap();
    assert_eq!(
        decoded.band_results.unwrap().own().len(),
        ATTACK_BAND_HISTORY_CAPACITY
    );
}

#[test]
fn a_band_section_that_does_not_hold_together_is_refused_whole() {
    let (history, results) = every_outcome();
    let bytes = encode_attack_snapshot(Uuid::new_v4(), &history, Some(band3()), &results);
    let section = bytes.len() - 4 - 6 * 384;
    let record = |index: usize| section + 4 + index * 384;
    // An unknown sound code.
    let mut unknown = bytes.clone();
    unknown[record(0) + 30] = 7;
    assert!(decode_attack_snapshot(&unknown).is_none());
    // A 300 ms tail that does not end 300 ms after its onset.
    let mut stretched = bytes.clone();
    let end = record(0) + 36;
    let span = i64::from_le_bytes(stretched[end..end + 8].try_into().unwrap());
    stretched[end..end + 8].copy_from_slice(&(span + 1).to_le_bytes());
    assert!(decode_attack_snapshot(&stretched).is_none());
    // A release cut by the next hit over a 300 ms tail.
    let mut cut = bytes.clone();
    cut[record(0) + 32] = 1;
    assert!(decode_attack_snapshot(&cut).is_none());
    // More hits than the history can show.
    let mut many = bytes.clone();
    many[section + 2..section + 4].copy_from_slice(&241_u16.to_le_bytes());
    assert!(decode_attack_snapshot(&many).is_none());
    // An unknown band.
    let mut band = bytes;
    band[section] = 9;
    assert!(decode_attack_snapshot(&band).is_none());
}
