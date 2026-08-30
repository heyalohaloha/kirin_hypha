use super::*;
use crate::metadata::MetadataRow;
use crate::midi::MidiSummary;
use crate::selector::performance_rank_key;

#[test]
fn qualified_assignment_is_deterministic_and_input_order_independent() {
    let selected = pool(100);
    let first = assign_grouped_folds(&selected).unwrap();
    let second = assign_grouped_folds(&selected).unwrap();
    let mut reversed = selected.clone();
    reversed.reverse();
    let reversed = assign_grouped_folds(&reversed).unwrap();
    assert_eq!(first.by_performance_id, second.by_performance_id);
    assert_eq!(first.by_performance_id, reversed.by_performance_id);
    assert!(
        first.qualification.qualified,
        "{:?}",
        first.qualification.deficits
    );
    assert_eq!(first.qualification.audit.performance_ids, vec![20; 5]);
    assert!(spread(first.qualification.audit.validation_ids.into_iter()) <= 1);
    let mut forced = first.qualification.audit.forced_opened_validation_ids;
    forced.sort_unstable();
    assert_eq!(forced, vec![1, 1, 1, 1, 2]);
    assert_eq!(first.qualification.audit.categorical_balance.len(), 8);
    assert!(first
        .qualification
        .audit
        .categorical_balance
        .iter()
        .all(|audit| audit.buckets > 0));
}

#[test]
fn invalid_count_duplicate_and_duration_fail_closed() {
    assert!(assign_grouped_folds(&pool(4))
        .unwrap_err()
        .contains("multiple of five"));
    let mut duplicate = pool(10);
    duplicate[1].row.id = duplicate[0].row.id.clone();
    assert!(assign_grouped_folds(&duplicate)
        .unwrap_err()
        .contains("duplicate"));
    let mut invalid = pool(10);
    invalid[0].row.duration_samples_44100 = 0;
    assert!(assign_grouped_folds(&invalid).is_err());
    let underpowered = assign_grouped_folds(&pool(10)).unwrap();
    assert!(!underpowered.qualification.qualified);
    assert!(!underpowered.qualification.deficits.is_empty());
}

#[test]
fn ratio_boundary_is_integer_exact_and_zero_minimum_fails() {
    assert!(RatioLimit::new(5, 4).accepts(400, 500));
    assert!(!RatioLimit::new(5, 4).accepts(400, 501));
    assert!(!RatioLimit::new(3, 2).accepts(0, 0));
}

fn pool(count: usize) -> Vec<Performance> {
    (0..count)
        .map(|index| {
            let forced_opened_validation = index < 6;
            let split = if forced_opened_validation || index % 4 == 0 {
                "validation"
            } else {
                "train"
            };
            let row = MetadataRow {
                drummer: format!("drummer{}", index % 10),
                session: format!("drummer{}/session{}", index % 10, index % 20),
                id: format!("performance{index:03}"),
                style: format!("style{}/sub", index % 10),
                bpm: 80.0 + (index % 100) as f64,
                beat_type: if index % 2 == 0 { "beat" } else { "fill" }.into(),
                time_signature: "4-4".into(),
                duration_decimal: "20".into(),
                duration: 20.0,
                duration_samples_44100: 882_000,
                split: split.into(),
                midi_filename: format!("{index}.midi"),
                audio_filename: format!("{index}.wav"),
                kit_name: format!("kit{}", index % 12),
            };
            Performance {
                selection_key: performance_rank_key(&row),
                row,
                midi: MidiSummary {
                    sha256: format!("{index:064x}"),
                    raw_notes: 100,
                    compound_events: 60 + index % 3,
                    kick_only_events: 10 + index % 3,
                    hat_only_events: 20 + index % 3,
                    first_event_time_secs: 0.0,
                    last_event_time_secs: 10.0,
                    source_raw_notes: 100,
                    source_compound_events: 60 + index % 3,
                    source_kick_only_events: 10 + index % 3,
                    source_hat_only_events: 20 + index % 3,
                    source_first_raw_note_time_secs: 0.0,
                    source_last_raw_note_time_secs: 10.0,
                },
                forced_opened_validation,
            }
        })
        .collect()
}
