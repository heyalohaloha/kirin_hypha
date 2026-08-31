use super::*;
use crate::midi::MidiSummary;
use tempfile::tempdir;

#[test]
fn primary_style_is_fixed_before_slash() {
    let mut row = sample_row("train");
    row.style = "funk/groove1".into();
    assert_eq!(row.primary_style(), "funk");
}

#[test]
fn group_rejects_metadata_drift_across_kits() {
    let first = sample_row("train");
    let mut second = first.clone();
    second.kit_name = "other".into();
    second.style = "different".into();
    assert!(validate_group(&[first, second]).is_err());
}

#[test]
fn duplicate_midi_hash_across_performance_ids_fails_closed() {
    let make = |id: &str| {
        let mut row = sample_row("train");
        row.id = id.into();
        Performance {
            row,
            selection_key: id.into(),
            midi: MidiSummary {
                sha256: "a".repeat(64),
                raw_notes: 1,
                compound_events: 1,
                kick_only_events: 1,
                hat_only_events: 0,
                first_event_time_secs: 0.0,
                last_event_time_secs: 0.0,
                source_raw_notes: 1,
                source_compound_events: 1,
                source_kick_only_events: 1,
                source_hat_only_events: 0,
                source_first_raw_note_time_secs: 0.0,
                source_last_raw_note_time_secs: 0.0,
            },
            forced_opened_validation: false,
        }
    };
    assert!(reject_cross_id_duplicate_midi(&[make("one"), make("two")])
        .unwrap_err()
        .contains("duplicate MIDI"));
}

#[test]
fn test_row_is_rejected_before_its_invalid_path_is_parsed() {
    let ledger = OpenedLedger::embedded().unwrap();
    let bytes = format!(
        "{METADATA_HEADER}\n{}\n{}\n",
        csv_row("drummer2/session1/1", "train", "ok.midi", "ok.wav"),
        csv_row("sealed/test/1", "test", "../sealed.midi", "../sealed.wav")
    );
    let pool = parse_metadata_bytes(bytes.as_bytes(), &ledger).unwrap();
    assert_eq!(pool.stats.excluded_test_rows, 1);
    assert_eq!(pool.performances.len(), 1);
}

#[test]
fn opened_validation_is_retained_as_required_development() {
    let ledger = OpenedLedger::embedded().unwrap();
    let bytes = format!(
        "{METADATA_HEADER}\n{}\n",
        csv_row(
            "drummer1/session1/16",
            "validation",
            "valid.midi",
            "valid.wav"
        )
    );
    let pool = parse_metadata_bytes(bytes.as_bytes(), &ledger).unwrap();
    assert_eq!(pool.performances.len(), 1);
    assert!(pool.performances[0].forced_opened_validation);
}

#[test]
fn invalid_first_render_falls_forward_by_fixed_render_key() {
    let root = tempdir().unwrap();
    let mut first = sample_row("train");
    first.midi_filename = "first.midi".into();
    first.kit_name = "First Kit".into();
    let mut second = first.clone();
    second.midi_filename = "second.midi".into();
    second.kit_name = "Second Kit".into();
    let mut rows = vec![first, second];
    rows.sort_by_key(render_choice_key);
    fs::write(root.path().join(&rows[0].midi_filename), b"invalid").unwrap();
    fs::write(root.path().join(&rows[1].midi_filename), one_note_midi()).unwrap();
    let pool = enrich_performances(
        vec![PerformanceRows {
            rows: rows.clone(),
            forced_opened_validation: false,
        }],
        root.path(),
    )
    .unwrap();
    assert_eq!(pool.performances.len(), 1);
    assert_eq!(
        pool.performances[0].row.midi_filename,
        rows[1].midi_filename
    );
    assert_eq!(pool.exclusions.len(), 1);
}

fn csv_row(id: &str, split: &str, midi: &str, audio: &str) -> String {
    format!("drummer1,drummer1/session1,{id},rock,120,beat,4-4,30,{split},{midi},{audio},Kit")
}

fn one_note_midi() -> Vec<u8> {
    let track = [0, 0x99, 36, 100, 0, 0xff, 0x2f, 0];
    let mut bytes = b"MThd\0\0\0\x06\0\0\0\x01\x01\xe0MTrk".to_vec();
    bytes.extend_from_slice(&(track.len() as u32).to_be_bytes());
    bytes.extend_from_slice(&track);
    bytes
}

pub(crate) fn sample_row(split: &str) -> MetadataRow {
    MetadataRow {
        drummer: "drummer1".into(),
        session: "drummer1/session1".into(),
        id: "drummer1/session1/1".into(),
        style: "rock".into(),
        bpm: 120.0,
        beat_type: "beat".into(),
        time_signature: "4-4".into(),
        duration_decimal: "30".into(),
        duration: 30.0,
        duration_samples_44100: 1_323_000,
        split: split.into(),
        midi_filename: "drummer1/session1/x.midi".into(),
        audio_filename: "drummer1/session1/x.wav".into(),
        kit_name: "Kit".into(),
    }
}
