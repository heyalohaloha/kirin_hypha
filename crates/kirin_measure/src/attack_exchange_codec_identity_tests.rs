//! A separately published band's records cannot inherit an unrelated history header identity.

use super::*;

fn encoded_results(history: &AttackHistory, results: &AttackBandResults) -> AttackBandResults {
    let bytes = encode_attack_snapshot(Uuid::new_v4(), history, Some(band3()), results);
    assert_eq!(
        u16::from_le_bytes(bytes[8..10].try_into().unwrap()),
        4,
        "wire version unchanged"
    );
    let decoded = decode_attack_snapshot(&bytes).expect("valid history still decodes");
    assert_eq!(
        decoded.history.frames().copied().collect::<Vec<_>>(),
        history.frames().copied().collect::<Vec<_>>()
    );
    assert_eq!(
        decoded.history.details().copied().collect::<Vec<_>>(),
        history.details().copied().collect::<Vec<_>>()
    );
    let band = decoded
        .band_results
        .expect("the chosen band is still declared");
    assert_eq!(band.band, Some(band3()));
    assert_eq!(band.generation, history.newest().unwrap().generation);
    band
}

#[test]
fn another_run_rate_layout_or_definition_never_gets_the_header_identity() {
    let history = history();
    let event = *history.events().next().unwrap();
    for mismatch in 0..5 {
        let mut foreign = event;
        match mismatch {
            0 => foreign.generation -= 1,
            1 => foreign.generation += 1,
            2 => foreign.sample_rate = 96_000,
            3 => foreign.channels = 1,
            _ => foreign.definition_hash = [8; 32],
        }
        for measured in [false, true] {
            let sound = measured.then_some(BandSound::Rises {
                arrival: BandArrival::At {
                    arrival_frames: 20.5,
                    attack_frames: 240.0,
                },
                release: BandRelease::At(4_000.0),
            });
            let mut detail = band_detail(foreign, sound, false);
            if let Some(measure) = detail.measure.as_mut() {
                measure.sample_rate = foreign.sample_rate;
                measure.channels = foreign.channels;
                measure.span_end_sample =
                    foreign.event_sample + i64::from(foreign.sample_rate) * 3 / 10;
                detail.span_end_sample = measure.span_end_sample;
            }
            // Each record is valid in its own run/format, even at this history's same onset.
            let mut results = AttackBandResults::new(Some(band3()), foreign.generation);
            assert!(results.put_own(detail));
            let decoded = encoded_results(&history, &results);
            assert!(
                decoded.own().is_empty(),
                "mismatch {mismatch}, measured={measured} must not be re-stamped"
            );
        }
    }
}

#[test]
fn even_a_current_container_cannot_re_stamp_a_foreign_record() {
    let history = history();
    let mut old = *history.events().next().unwrap();
    old.generation -= 1;
    let mut results = AttackBandResults::new(Some(band3()), old.generation);
    assert!(results.put_own(band_detail(old, None, false)));
    // The public container identity alone is insufficient: each record must still qualify.
    results.generation = history.newest().unwrap().generation;
    assert!(encoded_results(&history, &results).own().is_empty());
}

#[test]
fn qualifying_records_survive_exactly_and_foreign_records_are_left_out() {
    let history = history();
    let event = *history.events().next().unwrap();
    let sound = BandSound::Rises {
        arrival: BandArrival::At {
            arrival_frames: 20.5,
            attack_frames: 240.0,
        },
        release: BandRelease::At(4_000.0),
    };
    let current = band_detail(event, Some(sound), false);
    let mut results = AttackBandResults::new(Some(band3()), event.generation);
    assert!(results.put_own(current));
    assert_eq!(encoded_results(&history, &results).own(), &[current]);
    let foreign = AttackEvent {
        event_sample: event.event_sample + 9_600,
        decision_sample: event.decision_sample + 9_600,
        definition_hash: [8; 32],
        ..event
    };
    assert!(results.put_own(band_detail(foreign, Some(sound), false)));
    let decoded = encoded_results(&history, &results);
    assert_eq!(
        decoded.own(),
        &[current],
        "no qualified value/key/envelope is changed"
    );
    assert_eq!(
        decoded.own()[0].measure.unwrap().envelope,
        current.measure.unwrap().envelope
    );
}
