use super::*;

#[test]
fn source_duration_uses_exact_cross_product_and_two_ms_tolerance() {
    let row = ManifestRow {
        selection_rank: 1,
        selection_key: "a".repeat(64),
        fold: 0,
        performance_id: "drummer/session/1".to_string(),
        split: "train".to_string(),
        midi_relative_name: "drummer/session/file.midi".to_string(),
        audio_relative_name: "drummer/session/file.wav".to_string(),
        midi_sha256: "b".repeat(64),
        source_duration_decimal: "1".to_string(),
        source_duration_samples_44100: 44_100,
        excerpt_start_sample_44100: 0,
        excerpt_end_sample_44100: 44_100,
        excerpt_raw_notes: 1,
        excerpt_compound_events: 1,
        excerpt_kick_only_events: 1,
        excerpt_hat_only_events: 0,
    };
    let parsed = |micros| ParsedDrumMidi {
        raw_notes: 1,
        notes: vec![crate::drum_midi::DrumNote {
            time_micros: micros,
            pitch: 36,
            velocity: 100,
        }],
        events: Vec::new(),
    };
    assert!(verify_source_time_range(&row, &parsed(1_002_000)).is_ok());
    assert!(verify_source_time_range(&row, &parsed(1_002_001)).is_err());
}

#[test]
fn count_addition_is_checked_and_class_specific() {
    let left = EventCounts {
        raw_notes: 1,
        compound_events: 2,
        kick_only_events: 3,
        hat_only_events: 4,
    };
    let right = EventCounts {
        raw_notes: 4,
        compound_events: 3,
        kick_only_events: 2,
        hat_only_events: 1,
    };
    assert_eq!(
        checked_add_counts(left, right).unwrap(),
        EventCounts {
            raw_notes: 5,
            compound_events: 5,
            kick_only_events: 5,
            hat_only_events: 5,
        }
    );
    assert!(checked_add_counts(
        EventCounts {
            raw_notes: usize::MAX,
            ..EventCounts::default()
        },
        EventCounts {
            raw_notes: 1,
            ..EventCounts::default()
        }
    )
    .is_err());
}

#[test]
fn member_sha_and_half_open_excerpt_counts_are_recomputed() {
    let bytes = midi_file(1, &[0, 0x99, 36, 100, 2, 0x99, 42, 100, 0, 0xff, 0x2f, 0]);
    let digest = sha256_bytes(&bytes);
    let mut row = row_fixture(digest.clone());
    let member = VerifiedMember {
        relative_name: row.midi_relative_name.clone(),
        member_name: format!("{ARCHIVE_MEMBER_PREFIX}{}", row.midi_relative_name),
        uncompressed_size: bytes.len() as u64,
        compressed_size: bytes.len() as u64,
        crc32: crc32fast::hash(&bytes),
        compression: "stored",
        bytes,
    };
    let verified = verify_member(&row, &member).unwrap();
    assert_eq!(verified.source.counts.raw_notes, 2);
    assert_eq!(verified.excerpt.observed.counts.raw_notes, 1);
    assert_eq!(verified.excerpt.observed.counts.compound_events, 1);

    row.excerpt_raw_notes = 2;
    assert!(verify_member(&row, &member).is_err());
    row.excerpt_raw_notes = 1;
    row.midi_sha256 = "0".repeat(64);
    assert!(verify_member(&row, &member).is_err());
}

fn row_fixture(midi_sha256: String) -> ManifestRow {
    ManifestRow {
        selection_rank: 1,
        selection_key: "a".repeat(64),
        fold: 0,
        performance_id: "drummer/session/1".to_string(),
        split: "train".to_string(),
        midi_relative_name: "drummer/session/file.midi".to_string(),
        audio_relative_name: "drummer/session/file.wav".to_string(),
        midi_sha256,
        source_duration_decimal: "1".to_string(),
        source_duration_samples_44100: 44_100,
        excerpt_start_sample_44100: 0,
        excerpt_end_sample_44100: 44_100,
        excerpt_raw_notes: 1,
        excerpt_compound_events: 1,
        excerpt_kick_only_events: 1,
        excerpt_hat_only_events: 0,
    }
}

fn midi_file(division: u16, track: &[u8]) -> Vec<u8> {
    let mut bytes = b"MThd\0\0\0\x06\0\0\0\x01".to_vec();
    bytes.extend_from_slice(&division.to_be_bytes());
    bytes.extend_from_slice(b"MTrk");
    bytes.extend_from_slice(&(track.len() as u32).to_be_bytes());
    bytes.extend_from_slice(track);
    bytes
}
