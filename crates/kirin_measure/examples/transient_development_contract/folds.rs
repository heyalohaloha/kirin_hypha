use std::collections::{BTreeMap, BTreeSet};

use crate::metadata::Performance;

pub(crate) const FOLD_COUNT: u8 = 5;

#[derive(Clone, Debug)]
pub(crate) struct FoldPlan {
    pub(crate) by_performance_id: BTreeMap<String, u8>,
    pub(crate) lodo_groups: BTreeSet<String>,
    pub(crate) loso_groups: BTreeSet<String>,
}

impl FoldPlan {
    pub(crate) fn fold_for(&self, performance_id: &str) -> Result<u8, String> {
        self.by_performance_id
            .get(performance_id)
            .copied()
            .ok_or_else(|| format!("missing grouped fold for {performance_id}"))
    }
}

#[derive(Clone, Debug, Default)]
struct FoldState {
    ids: usize,
    duration_millis: u64,
    drummers: BTreeMap<String, usize>,
    sessions: BTreeMap<String, usize>,
    kits: BTreeMap<String, usize>,
    beat_types: BTreeMap<String, usize>,
    tempo_bins: BTreeMap<u16, usize>,
    density_bins: BTreeMap<u16, usize>,
}

pub(crate) fn assign_grouped_folds(selected: &[Performance]) -> Result<FoldPlan, String> {
    let mut ids = BTreeSet::new();
    if selected.iter().any(|item| !ids.insert(item.row.id.clone())) {
        return Err("fold input has duplicate performance ID".to_string());
    }
    let mut order = (0..selected.len()).collect::<Vec<_>>();
    order.sort_by(|&left, &right| {
        selected[right]
            .row
            .duration
            .total_cmp(&selected[left].row.duration)
            .then_with(|| {
                selected[left]
                    .selection_key
                    .cmp(&selected[right].selection_key)
            })
            .then_with(|| selected[left].row.id.cmp(&selected[right].row.id))
    });
    let mut states = vec![FoldState::default(); usize::from(FOLD_COUNT)];
    let mut by_performance_id = BTreeMap::new();
    for index in order {
        let item = &selected[index];
        let fold = (0..FOLD_COUNT)
            .min_by_key(|&fold| fold_cost(&states[usize::from(fold)], item, fold))
            .ok_or("fold count is zero")?;
        update_fold(&mut states[usize::from(fold)], item);
        by_performance_id.insert(item.row.id.clone(), fold);
    }
    let lodo_groups = selected
        .iter()
        .map(|item| item.row.drummer.clone())
        .collect();
    let loso_groups = selected
        .iter()
        .map(|item| item.row.session.clone())
        .collect();
    let plan = FoldPlan {
        by_performance_id,
        lodo_groups,
        loso_groups,
    };
    validate_plan(selected, &plan)?;
    Ok(plan)
}

fn fold_cost(
    state: &FoldState,
    item: &Performance,
    fold: u8,
) -> (usize, usize, usize, usize, usize, usize, usize, u64, u8) {
    (
        count(&state.drummers, &item.row.drummer),
        count(&state.sessions, &item.row.session),
        count(&state.kits, &item.row.kit_name),
        count(&state.beat_types, &item.row.beat_type),
        count(&state.tempo_bins, &tempo_bin(item.row.bpm)),
        count(&state.density_bins, &density_bin(item.density())),
        state.ids,
        state.duration_millis,
        fold,
    )
}

fn update_fold(state: &mut FoldState, item: &Performance) {
    state.ids += 1;
    state.duration_millis += (item.row.duration * 1_000.0).round() as u64;
    increment(&mut state.drummers, item.row.drummer.clone());
    increment(&mut state.sessions, item.row.session.clone());
    increment(&mut state.kits, item.row.kit_name.clone());
    increment(&mut state.beat_types, item.row.beat_type.clone());
    increment(&mut state.tempo_bins, tempo_bin(item.row.bpm));
    increment(&mut state.density_bins, density_bin(item.density()));
}

fn validate_plan(selected: &[Performance], plan: &FoldPlan) -> Result<(), String> {
    if plan.by_performance_id.len() != selected.len()
        || plan
            .by_performance_id
            .values()
            .any(|fold| *fold >= FOLD_COUNT)
    {
        return Err("invalid five-fold grouped assignment".to_string());
    }
    for drummer in &plan.lodo_groups {
        let held_out = selected
            .iter()
            .filter(|item| item.row.drummer == *drummer)
            .count();
        if held_out == 0 {
            return Err("empty leave-one-drummer-out partition".to_string());
        }
    }
    for session in &plan.loso_groups {
        let held_out = selected
            .iter()
            .filter(|item| item.row.session == *session)
            .count();
        if held_out == 0 {
            return Err("empty leave-one-session-out partition".to_string());
        }
    }
    Ok(())
}

fn tempo_bin(bpm: f64) -> u16 {
    (bpm / 20.0).floor().clamp(0.0, f64::from(u16::MAX)) as u16
}

fn density_bin(events_per_second: f64) -> u16 {
    (events_per_second * 2.0)
        .floor()
        .clamp(0.0, f64::from(u16::MAX)) as u16
}

fn count<K: Ord>(values: &BTreeMap<K, usize>, key: &K) -> usize {
    values.get(key).copied().unwrap_or(0)
}

fn increment<K: Ord>(values: &mut BTreeMap<K, usize>, key: K) {
    *values.entry(key).or_default() += 1;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::metadata::MetadataRow;
    use crate::midi::MidiSummary;
    use crate::selector::performance_rank_key;

    #[test]
    fn folds_are_deterministic_grouped_and_cover_lodo_loso_once() {
        let selected = pool(75);
        let first = assign_grouped_folds(&selected).unwrap();
        let second = assign_grouped_folds(&selected).unwrap();
        assert_eq!(first.by_performance_id, second.by_performance_id);
        assert_eq!(first.by_performance_id.len(), 75);
        assert_eq!(first.lodo_groups.len(), 9);
        assert_eq!(first.loso_groups.len(), 15);
        let used = first
            .by_performance_id
            .values()
            .copied()
            .collect::<BTreeSet<_>>();
        assert_eq!(used, BTreeSet::from([0, 1, 2, 3, 4]));
        for item in &selected {
            assert_eq!(first.fold_for(&item.row.id), second.fold_for(&item.row.id));
        }
    }

    #[test]
    fn duplicate_performance_id_fails_closed() {
        let mut selected = pool(2);
        selected[1].row.id = selected[0].row.id.clone();
        assert!(assign_grouped_folds(&selected)
            .unwrap_err()
            .contains("duplicate"));
    }

    fn pool(count: usize) -> Vec<Performance> {
        (0..count)
            .map(|index| {
                let row = MetadataRow {
                    drummer: format!("drummer{}", index % 9),
                    session: format!("session{}", index % 15),
                    id: format!("performance{index}"),
                    style: format!("style{}", index % 10),
                    bpm: 60.0 + (index % 120) as f64,
                    beat_type: if index % 2 == 0 { "beat" } else { "fill" }.into(),
                    time_signature: "4-4".into(),
                    duration: 20.0 + (index % 20) as f64,
                    split: "train".into(),
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
                        compound_events: 50 + index,
                        kick_only_events: 10,
                        hat_only_events: 10,
                        first_event_time_secs: 0.0,
                        last_event_time_secs: 10.0,
                    },
                    forced_opened_validation: false,
                }
            })
            .collect()
    }
}
