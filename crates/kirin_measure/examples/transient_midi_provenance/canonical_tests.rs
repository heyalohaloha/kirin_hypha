use super::*;

fn parsed(time_micros: u64) -> ParsedDrumMidi {
    ParsedDrumMidi {
        raw_notes: 1,
        notes: vec![DrumNote {
            time_micros,
            pitch: 36,
            velocity: 100,
        }],
        events: vec![CompoundEvent {
            time_micros,
            kick_only: true,
            hat_only: false,
            note_start: 0,
            note_end: 1,
        }],
    }
}

#[test]
fn excerpt_digest_is_relative_and_source_digest_is_absolute() {
    let first = digest_contract(&parsed(1_000_000), &parsed(1_000_000), 44_100, 88_200).unwrap();
    let second = digest_contract(&parsed(2_000_000), &parsed(2_000_000), 88_200, 132_300).unwrap();
    assert_ne!(first.source_notes_sha256, second.source_notes_sha256);
    assert_ne!(first.source_events_sha256, second.source_events_sha256);
    assert_eq!(first.excerpt_notes_sha256, second.excerpt_notes_sha256);
    assert_eq!(first.excerpt_events_sha256, second.excerpt_events_sha256);
}

#[test]
fn duration_pitch_velocity_and_classification_are_bound() {
    let source = parsed(1_000_000);
    let baseline = digest_contract(&source, &source, 44_100, 88_200).unwrap();
    let longer = digest_contract(&source, &source, 44_100, 88_201).unwrap();
    assert_ne!(baseline.excerpt_notes_sha256, longer.excerpt_notes_sha256);
    assert_ne!(baseline.excerpt_events_sha256, longer.excerpt_events_sha256);

    let mut changed = source.clone();
    changed.notes[0].velocity = 99;
    changed.events[0].hat_only = true;
    let changed = digest_contract(&changed, &changed, 44_100, 88_200).unwrap();
    assert_ne!(baseline.source_notes_sha256, changed.source_notes_sha256);
    assert_ne!(baseline.source_events_sha256, changed.source_events_sha256);
}

#[test]
fn event_before_excerpt_origin_is_rejected() {
    assert!(digest_contract(&parsed(0), &parsed(0), 1, 2).is_err());
    assert!(digest_contract(&parsed(0), &parsed(0), 2, 2).is_err());
}
