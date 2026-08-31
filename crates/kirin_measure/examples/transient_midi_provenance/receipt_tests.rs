use std::fs;

use tempfile::tempdir;

use super::*;

#[test]
fn synthetic_receipt_is_byte_identical_and_contains_no_runtime_identity() {
    let parent = parent();
    let mut members = members();
    let first = render_receipt(input(&parent, &members)).unwrap();
    members.reverse();
    let second = render_receipt(input(&parent, &members)).unwrap();
    assert_eq!(first, second);
    assert!(first.ends_with(b"\n"));

    let text = std::str::from_utf8(&first).unwrap();
    assert!(text.contains(r#""component_verified": true"#));
    assert!(text.contains(r#""overall_formal_authorization": false"#));
    assert!(text.contains(r#""evidence_class": "operational_assertions_not_evidence""#));
    for forbidden in ["timestamp", "created_at", "hostname", "/Users/"] {
        assert!(!text.contains(forbidden), "receipt contains {forbidden}");
    }
}

#[test]
fn publish_is_create_new_and_never_overwrites() {
    let directory = tempdir().unwrap();
    let result = directory.path().join("receipt.json");
    let bytes = b"deterministic receipt\n";
    assert_eq!(
        publish_receipt_create_new(&result, bytes).unwrap(),
        sha256_bytes(bytes)
    );
    assert_eq!(fs::read(&result).unwrap(), bytes);
    let error = publish_receipt_create_new(&result, b"replacement\n").unwrap_err();
    assert!(error.contains("receipt already exists"), "{error}");
    assert_eq!(fs::read(&result).unwrap(), bytes);
}

#[test]
fn nonempty_canonical_duplicates_fail_but_empty_excerpt_collisions_are_reported() {
    let parent = parent();
    let mut duplicate = members();
    duplicate[1].excerpt.observed.notes_sha256 = duplicate[0].excerpt.observed.notes_sha256.clone();
    assert!(render_receipt(input(&parent, &duplicate)).is_ok());
    duplicate[1].excerpt.observed.events_sha256 =
        duplicate[0].excerpt.observed.events_sha256.clone();
    assert!(render_receipt(input(&parent, &duplicate))
        .unwrap_err()
        .contains("duplicate audit"));

    let mut empty = members();
    let empty_hash = sha256_bytes(b"canonical-empty");
    for member in &mut empty[..2] {
        member.excerpt.manifest_counts = EventCounts::default();
        member.excerpt.observed.counts = EventCounts::default();
        member.excerpt.observed.notes_sha256 = empty_hash.clone();
        member.excerpt.observed.events_sha256 = empty_hash.clone();
    }
    let bytes = render_receipt(input(&parent, &empty)).unwrap();
    let value: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
    assert_eq!(value["aggregate"]["empty_excerpts"], 2);
    assert_eq!(
        value["duplicate_audit"]["empty_excerpt_performance_ids"]
            .as_array()
            .unwrap()
            .len(),
        2
    );
}

fn input<'a>(parent: &'a ParentEvidence, members: &'a [MemberEvidence]) -> ReceiptInput<'a> {
    ReceiptInput {
        parent,
        archive: ArchiveReceiptInput {
            archive_sha256: PINNED_MIDI_ARCHIVE_SHA256.to_string(),
            archive_bytes: PINNED_MIDI_ARCHIVE_BYTES,
            central_directory_entries: PINNED_ARCHIVE_ENTRIES,
            selected_uncompressed_bytes: members
                .iter()
                .map(|member| member.uncompressed_bytes)
                .sum(),
            selected_compressed_bytes: members.iter().map(|member| member.compressed_bytes).sum(),
            member_name_prefix: "e-gmd-v1.0.0/".to_string(),
        },
        members,
    }
}

fn parent() -> ParentEvidence {
    ParentEvidence {
        receipt_sha256: PINNED_DEVELOPMENT_RECEIPT_SHA256.to_string(),
        manifest_sha256: PINNED_MANIFEST_SHA256.to_string(),
        folds_sha256: PINNED_FOLDS_SHA256.to_string(),
        archive_sha256: PINNED_MIDI_ARCHIVE_SHA256.to_string(),
        source_raw_notes: PINNED_MANIFEST_ROWS * 2,
        source_compound_events: PINNED_MANIFEST_ROWS,
        source_kick_only_events: 0,
        source_hat_only_events: 0,
    }
}

fn members() -> Vec<MemberEvidence> {
    (0..PINNED_MANIFEST_ROWS)
        .map(|index| {
            let rank = index + 1;
            let filename = format!("drummer/session/{rank}.mid");
            let member_sha256 = sha256_bytes(format!("member-{rank}").as_bytes());
            let source = CanonicalEvidence {
                counts: EventCounts {
                    raw_notes: 2,
                    compound_events: 1,
                    kick_only_events: 0,
                    hat_only_events: 0,
                },
                notes_sha256: sha256_bytes(format!("source-notes-{rank}").as_bytes()),
                events_sha256: sha256_bytes(format!("source-events-{rank}").as_bytes()),
            };
            let excerpt_counts = EventCounts {
                raw_notes: 2,
                compound_events: 1,
                kick_only_events: 0,
                hat_only_events: 0,
            };
            MemberEvidence {
                selection_rank: rank,
                selection_key: sha256_bytes(format!("selection-{rank}").as_bytes()),
                fold: (index % 5) as u8,
                performance_id: format!("performance-{rank}"),
                split: if index < PINNED_TRAIN_ROWS {
                    DevelopmentSplit::Train
                } else {
                    DevelopmentSplit::Validation
                },
                manifest_midi_filename: filename.clone(),
                archive_member_name: format!("e-gmd-v1.0.0/{filename}"),
                manifest_midi_sha256: member_sha256.clone(),
                member_sha256,
                uncompressed_bytes: 100 + rank as u64,
                compressed_bytes: 50 + rank as u64,
                crc32: rank as u32,
                compression: MemberCompression::Deflated,
                source,
                excerpt: ExcerptEvidence {
                    start_sample_44100: 0,
                    end_sample_44100: 44_100,
                    manifest_counts: excerpt_counts,
                    observed: CanonicalEvidence {
                        counts: excerpt_counts,
                        notes_sha256: sha256_bytes(format!("excerpt-notes-{rank}").as_bytes()),
                        events_sha256: sha256_bytes(format!("excerpt-events-{rank}").as_bytes()),
                    },
                },
            }
        })
        .collect()
}
