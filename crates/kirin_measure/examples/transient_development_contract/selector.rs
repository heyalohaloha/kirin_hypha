use std::collections::{BTreeSet, HashSet};

use sha2::{Digest, Sha256};

use crate::contract::{
    LEGACY_BASELINE_PERFORMANCE_IDS, MAX_TARGET_PERFORMANCE_IDS, RENDER_KEY_VERSION,
    SELECTION_SEED, TARGET_PERFORMANCE_IDS,
};
use crate::drum_excerpt;
use crate::metadata::{MetadataRow, Performance};
use crate::policy::{
    assess_development, DevelopmentAssessment, MIN_BEAT_FILL_RATIO, MIN_KITS, MIN_STYLES,
};

#[derive(Clone, Debug)]
pub(crate) struct SelectionOutcome {
    pub(crate) selected: Vec<Performance>,
    pub(crate) reserve: Vec<Performance>,
    pub(crate) assessment: DevelopmentAssessment,
}

const RENDER_CHOICE_DOMAIN: &str = "attack-drum-development-render-choice-v1";

pub(crate) fn performance_rank_key(row: &MetadataRow) -> String {
    drum_excerpt::performance_rank_key(&row.split, &row.id)
        .expect("validated nonempty metadata identity")
}

pub(crate) fn render_choice_key(row: &MetadataRow) -> String {
    domain_hash(
        RENDER_CHOICE_DOMAIN,
        &[
            RENDER_KEY_VERSION,
            SELECTION_SEED,
            &row.split,
            &row.id,
            &row.kit_name,
        ],
    )
}

fn domain_hash(domain: &str, fields: &[&str]) -> String {
    hex::encode(domain_digest(domain, fields))
}

fn domain_digest(domain: &str, fields: &[&str]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    for field in std::iter::once(domain).chain(fields.iter().copied()) {
        hasher.update((field.len() as u64).to_be_bytes());
        hasher.update(field.as_bytes());
    }
    hasher.finalize().into()
}

pub(crate) fn select_development(
    candidates: &[Performance],
    required_drummers: &BTreeSet<String>,
) -> Result<SelectionOutcome, String> {
    if TARGET_PERFORMANCE_IDS > MAX_TARGET_PERFORMANCE_IDS
        || !TARGET_PERFORMANCE_IDS.is_multiple_of(usize::from(crate::folds::FOLD_COUNT))
    {
        return Err("invalid fixed development target policy".to_string());
    }
    validate_candidates(candidates)?;
    let mut order = (0..candidates.len()).collect::<Vec<_>>();
    order.sort_by(|&left, &right| {
        candidates[left]
            .selection_key
            .cmp(&candidates[right].selection_key)
            .then_with(|| candidates[left].row.id.cmp(&candidates[right].row.id))
    });
    let mut chosen = HashSet::new();
    let mut selected_indices = Vec::new();

    // The six already-opened validation IDs are development-required evidence.
    for &index in &order {
        if candidates[index].forced_opened_validation {
            add(index, &mut chosen, &mut selected_indices);
        }
    }
    // Quota-first coverage prevents a rare drummer from inflating a plain hash prefix.
    for drummer in required_drummers {
        if let Some(index) = order
            .iter()
            .copied()
            .find(|&index| !chosen.contains(&index) && candidates[index].row.drummer == *drummer)
        {
            add(index, &mut chosen, &mut selected_indices);
        }
    }
    add_new_styles(candidates, &order, &mut chosen, &mut selected_indices);
    add_new_kits(candidates, &order, &mut chosen, &mut selected_indices);

    // Preserve the complete v1 60-ID selection before appending the old rank
    // reserve. Changing the v2 target must never silently redraw that baseline.
    let minimum_each =
        (MIN_BEAT_FILL_RATIO * LEGACY_BASELINE_PERFORMANCE_IDS as f64).ceil() as usize;
    add_until_type_count(
        candidates,
        &order,
        "beat",
        minimum_each,
        &mut chosen,
        &mut selected_indices,
    );
    add_until_type_count(
        candidates,
        &order,
        "fill",
        minimum_each,
        &mut chosen,
        &mut selected_indices,
    );
    while selected_indices.len() < LEGACY_BASELINE_PERFORMANCE_IDS {
        if !add_next(&order, &mut chosen, &mut selected_indices, |_| true) {
            break;
        }
    }
    while selected_indices.len() < TARGET_PERFORMANCE_IDS {
        if !add_next(&order, &mut chosen, &mut selected_indices, |_| true) {
            break;
        }
    }
    let selected = selected_indices
        .iter()
        .map(|&index| candidates[index].clone())
        .collect::<Vec<_>>();
    let reserve = order
        .iter()
        .filter(|index| !chosen.contains(index))
        .map(|&index| candidates[index].clone())
        .collect();
    let outcome = SelectionOutcome {
        assessment: assess_development(&selected, required_drummers),
        selected,
        reserve,
    };
    validate_formal_manifest(&outcome, candidates, required_drummers)?;
    Ok(outcome)
}

pub(crate) fn validate_formal_manifest(
    outcome: &SelectionOutcome,
    candidates: &[Performance],
    required_drummers: &BTreeSet<String>,
) -> Result<(), String> {
    let assessment = assess_development(&outcome.selected, required_drummers);
    if assessment.status != outcome.assessment.status
        || assessment.unique_performance_ids != outcome.assessment.unique_performance_ids
        || assessment.unique_duration_samples_44100
            != outcome.assessment.unique_duration_samples_44100
        || assessment.kick_only_events != outcome.assessment.kick_only_events
        || assessment.hat_only_events != outcome.assessment.hat_only_events
    {
        return Err("formal development manifest assessment mismatch".to_string());
    }
    let candidate_keys = candidates
        .iter()
        .map(|item| {
            (
                item.row.id.as_str(),
                item.row.kit_name.as_str(),
                item.selection_key.as_str(),
            )
        })
        .collect::<HashSet<_>>();
    for selected in &outcome.selected {
        if !matches!(selected.row.split.as_str(), "train" | "validation")
            || !candidate_keys.contains(&(
                selected.row.id.as_str(),
                selected.row.kit_name.as_str(),
                selected.selection_key.as_str(),
            ))
        {
            return Err("formal development manifest contains a foreign or sealed row".to_string());
        }
    }
    let forced_pool = candidates
        .iter()
        .filter(|item| item.forced_opened_validation)
        .map(|item| item.row.id.as_str())
        .collect::<BTreeSet<_>>();
    let forced_selected = outcome
        .selected
        .iter()
        .filter(|item| item.forced_opened_validation)
        .map(|item| item.row.id.as_str())
        .collect::<BTreeSet<_>>();
    if forced_pool != forced_selected {
        return Err("formal development manifest omitted opened validation evidence".to_string());
    }
    Ok(())
}

fn add_new_styles(
    candidates: &[Performance],
    order: &[usize],
    chosen: &mut HashSet<usize>,
    selected: &mut Vec<usize>,
) {
    let mut styles = selected
        .iter()
        .map(|&index| candidates[index].row.primary_style().to_string())
        .collect::<HashSet<_>>();
    for &index in order {
        if styles.len() >= MIN_STYLES {
            break;
        }
        let style = candidates[index].row.primary_style();
        if !chosen.contains(&index) && !styles.contains(style) {
            styles.insert(style.to_string());
            add(index, chosen, selected);
        }
    }
}

fn add_new_kits(
    candidates: &[Performance],
    order: &[usize],
    chosen: &mut HashSet<usize>,
    selected: &mut Vec<usize>,
) {
    let mut kits = selected
        .iter()
        .map(|&index| candidates[index].row.kit_name.clone())
        .collect::<HashSet<_>>();
    for &index in order {
        if kits.len() >= MIN_KITS {
            break;
        }
        let kit = &candidates[index].row.kit_name;
        if !chosen.contains(&index) && !kits.contains(kit) {
            kits.insert(kit.clone());
            add(index, chosen, selected);
        }
    }
}

fn add_until_type_count(
    candidates: &[Performance],
    order: &[usize],
    beat_type: &str,
    minimum: usize,
    chosen: &mut HashSet<usize>,
    selected: &mut Vec<usize>,
) {
    while selected
        .iter()
        .filter(|&&index| candidates[index].row.beat_type == beat_type)
        .count()
        < minimum
    {
        if !add_next(order, chosen, selected, |index| {
            candidates[index].row.beat_type == beat_type
        }) {
            break;
        }
    }
}

fn add_next(
    order: &[usize],
    chosen: &mut HashSet<usize>,
    selected: &mut Vec<usize>,
    predicate: impl Fn(usize) -> bool,
) -> bool {
    if let Some(index) = order
        .iter()
        .copied()
        .find(|index| !chosen.contains(index) && predicate(*index))
    {
        add(index, chosen, selected);
        true
    } else {
        false
    }
}

fn add(index: usize, chosen: &mut HashSet<usize>, selected: &mut Vec<usize>) {
    if chosen.insert(index) {
        selected.push(index);
    }
}

fn validate_candidates(candidates: &[Performance]) -> Result<(), String> {
    if candidates.is_empty() {
        return Err("insufficient_development_data: empty candidate pool".to_string());
    }
    let mut ids = HashSet::new();
    let mut keys = HashSet::new();
    for candidate in candidates {
        if !matches!(candidate.row.split.as_str(), "train" | "validation")
            || !ids.insert(candidate.row.id.as_str())
            || !keys.insert(candidate.selection_key.as_str())
            || candidate.selection_key != performance_rank_key(&candidate.row)
        {
            return Err("invalid or duplicate deterministic candidate".to_string());
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::midi::MidiSummary;

    #[test]
    fn selection_is_deterministic_quota_first_and_forces_opened_validation() {
        let candidates = synthetic_pool(400);
        let drummers = (0..9).map(|index| format!("drummer{index}")).collect();
        let first = select_development(&candidates, &drummers).unwrap();
        let second = select_development(&candidates, &drummers).unwrap();
        let ids = |outcome: &SelectionOutcome| {
            outcome
                .selected
                .iter()
                .map(|item| item.row.id.clone())
                .collect::<Vec<_>>()
        };
        assert_eq!(ids(&first), ids(&second));
        assert!(first.assessment.ready());
        assert_eq!(first.assessment.forced_opened_validation_ids, 6);
        assert!(first
            .selected
            .iter()
            .take(6)
            .all(|item| item.forced_opened_validation));
        assert_eq!(first.selected.len(), TARGET_PERFORMANCE_IDS);
        let legacy_pool = synthetic_pool(250);
        let legacy = select_development(&legacy_pool, &drummers).unwrap();
        let prefix = ids(&legacy)[..LEGACY_BASELINE_PERFORMANCE_IDS].join("\n");
        assert_eq!(
            crate::contract::sha256_bytes(prefix.as_bytes()),
            "84d3e8f3832b0a181b98949cb0943fd693ad73a11a05d538d2ddf3fd0547b484"
        );
    }

    #[test]
    fn exhausted_small_pool_returns_insufficient_and_forbids_winner() {
        let candidates = synthetic_pool(12);
        let drummers = (0..9).map(|index| format!("drummer{index}")).collect();
        let outcome = select_development(&candidates, &drummers).unwrap();
        assert_eq!(outcome.selected.len(), 12);
        assert_eq!(outcome.assessment.status, "insufficient_development_data");
        assert!(!outcome.assessment.winner_allowed);
    }

    #[test]
    fn domain_separated_length_prefix_resists_ambiguous_fields() {
        let candidates = synthetic_pool(60);
        let mut left = candidates[0].row.clone();
        left.split = "ab".into();
        left.id = "c".into();
        let mut right = left.clone();
        right.split = "a".into();
        right.id = "bc".into();
        assert_ne!(performance_rank_key(&left), performance_rank_key(&right));
        assert_ne!(performance_rank_key(&left), render_choice_key(&left));
        let drummers = (0..9).map(|index| format!("drummer{index}")).collect();
        let mut outcome = select_development(&candidates, &drummers).unwrap();
        outcome.selected[0].selection_key = "0".repeat(64);
        assert!(validate_formal_manifest(&outcome, &candidates, &drummers).is_err());
    }

    #[test]
    fn input_order_does_not_change_manifest_order() {
        let candidates = synthetic_pool(400);
        let mut reversed = candidates.clone();
        reversed.reverse();
        let drummers = (0..9).map(|index| format!("drummer{index}")).collect();
        let ids = |pool: &[Performance]| {
            select_development(pool, &drummers)
                .unwrap()
                .selected
                .into_iter()
                .map(|item| item.row.id)
                .collect::<Vec<_>>()
        };
        assert_eq!(ids(&candidates), ids(&reversed));
    }

    fn synthetic_pool(count: usize) -> Vec<Performance> {
        (0..count)
            .map(|index| {
                let split = if index % 2 == 0 {
                    "train"
                } else {
                    "validation"
                };
                let row = MetadataRow {
                    drummer: format!("drummer{}", index % 9),
                    session: format!("drummer{}/session{}", index % 9, index % 15),
                    id: format!("drummer{}/session{}/{}", index % 9, index % 15, index),
                    style: format!("style{}", index % 10),
                    bpm: 60.0 + index as f64,
                    beat_type: if index % 2 == 0 { "beat" } else { "fill" }.into(),
                    time_signature: "4-4".into(),
                    duration_decimal: "31".into(),
                    duration: 31.0,
                    duration_samples_44100: 31 * 44_100,
                    split: split.into(),
                    midi_filename: format!("midi/{index}.midi"),
                    audio_filename: format!("audio/{index}.wav"),
                    kit_name: format!("Kit {}", index % 10),
                };
                Performance {
                    selection_key: performance_rank_key(&row),
                    row,
                    midi: MidiSummary {
                        sha256: format!("{index:064x}"),
                        raw_notes: 30,
                        compound_events: 20,
                        kick_only_events: 10,
                        hat_only_events: 10,
                        first_event_time_secs: 0.0,
                        last_event_time_secs: 30.0,
                        source_raw_notes: 30,
                        source_compound_events: 20,
                        source_kick_only_events: 10,
                        source_hat_only_events: 10,
                        source_first_raw_note_time_secs: 0.0,
                        source_last_raw_note_time_secs: 30.0,
                    },
                    forced_opened_validation: index < 6,
                }
            })
            .collect()
    }
}
