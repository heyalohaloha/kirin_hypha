use std::collections::{BTreeMap, BTreeSet};

use serde::Serialize;

use crate::metadata::Performance;

#[path = "fold_audit.rs"]
mod fold_audit;
#[path = "fold_search.rs"]
mod fold_search;

use fold_audit::{
    category_audit, deficit_minimum, deficit_ratio, deficit_spread, normalized_square, spread,
};

pub(crate) const FOLD_COUNT: u8 = 5;
pub(crate) const FOLD_ASSIGNMENT_VERSION: &str = "attack-drum-balanced-excerpt-folds-v2";
pub(crate) const FOLD_ASSIGNMENT_SEED: &str = "ATTACK-DRUM-FOLDS-20260830-v2";
pub(crate) const SEARCH_RESTARTS: u8 = 16;
pub(crate) const RANDOM_SWAP_ATTEMPTS: usize = 100_000;
pub(crate) const BEST_SWAP_PASSES: usize = 16;

const METRIC_NAMES: [&str; 4] = [
    "excerpt_duration_samples_44100",
    "excerpt_compound_events",
    "excerpt_kick_only_events",
    "excerpt_hat_only_events",
];
const CATEGORY_NAMES: [&str; 8] = [
    "drummer",
    "session",
    "kit",
    "primary_style",
    "tempo_20_bpm_bin",
    "density_0.5_event_per_second_bin",
    "split",
    "forced_opened_validation",
];
const RATIO_LIMITS: [RatioLimit; 4] = [
    RatioLimit::new(5, 4),
    RatioLimit::new(5, 4),
    RatioLimit::new(3, 2),
    RatioLimit::new(3, 2),
];
pub(crate) const MIN_KICK_EVENTS_PER_FOLD: u64 = 150;
pub(crate) const MIN_HAT_EVENTS_PER_FOLD: u64 = 300;
pub(crate) const MIN_POSITIVE_IDS_PER_FOLD: u64 = 8;

#[derive(Clone, Debug)]
pub(crate) struct FoldPlan {
    pub(crate) by_performance_id: BTreeMap<String, u8>,
    pub(crate) lodo_groups: BTreeSet<String>,
    pub(crate) loso_groups: BTreeSet<String>,
    pub(crate) qualification: FoldQualification,
}

impl FoldPlan {
    pub(crate) fn fold_for(&self, performance_id: &str) -> Result<u8, String> {
        self.by_performance_id
            .get(performance_id)
            .copied()
            .ok_or_else(|| format!("missing grouped fold for {performance_id}"))
    }
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct FoldQualification {
    pub(crate) status: String,
    pub(crate) qualified: bool,
    pub(crate) policy: FoldPolicy,
    pub(crate) audit: FoldAudit,
    pub(crate) deficits: Vec<FoldDeficit>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct FoldPolicy {
    require_equal_performance_ids: bool,
    maximum_beat_id_spread: u64,
    maximum_fill_id_spread: u64,
    ratio_limits: BTreeMap<&'static str, RatioLimit>,
    minimum_kick_only_events_per_fold: u64,
    minimum_hat_only_events_per_fold: u64,
    minimum_kick_positive_ids_per_fold: u64,
    minimum_hat_positive_ids_per_fold: u64,
    maximum_single_id_share: RatioLimit,
    categorical_features_are_diagnostic_only: bool,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct FoldAudit {
    pub(crate) performance_ids: Vec<u64>,
    pub(crate) beat_ids: Vec<u64>,
    pub(crate) fill_ids: Vec<u64>,
    pub(crate) validation_ids: Vec<u64>,
    pub(crate) forced_opened_validation_ids: Vec<u64>,
    pub(crate) metrics: Vec<MetricAudit>,
    pub(crate) kick_positive_ids: Vec<u64>,
    pub(crate) hat_positive_ids: Vec<u64>,
    pub(crate) categorical_balance: Vec<CategoryAudit>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MetricAudit {
    pub(crate) metric: &'static str,
    pub(crate) per_fold: Vec<u64>,
    pub(crate) minimum: u64,
    pub(crate) maximum: u64,
    pub(crate) max_to_min_ratio: Option<f64>,
    pub(crate) maximum_single_id_share_per_fold: Vec<Option<f64>>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct CategoryAudit {
    feature: &'static str,
    buckets: usize,
    worst_bucket_count_spread: u64,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct FoldDeficit {
    metric: String,
    actual: String,
    required: String,
}

#[derive(Clone, Copy, Debug, Serialize)]
pub(crate) struct RatioLimit {
    numerator: u64,
    denominator: u64,
}

impl RatioLimit {
    const fn new(numerator: u64, denominator: u64) -> Self {
        Self {
            numerator,
            denominator,
        }
    }

    fn accepts(self, minimum: u64, maximum: u64) -> bool {
        minimum > 0
            && u128::from(maximum) * u128::from(self.denominator)
                <= u128::from(minimum) * u128::from(self.numerator)
    }
}

#[derive(Clone, Debug)]
struct Item {
    id: String,
    beat: bool,
    metrics: [u64; 4],
    categories: [usize; 8],
    validation: bool,
    forced_opened_validation: bool,
}

#[derive(Clone, Debug)]
struct FoldState {
    ids: u64,
    beat_ids: u64,
    validation_ids: u64,
    forced_opened_validation_ids: u64,
    metrics: [u64; 4],
    positive_ids: [u64; 2],
    metric_values: [BTreeMap<u64, u16>; 4],
    categories: Vec<u16>,
}

impl FoldState {
    fn new(category_count: usize) -> Self {
        Self {
            ids: 0,
            beat_ids: 0,
            validation_ids: 0,
            forced_opened_validation_ids: 0,
            metrics: [0; 4],
            positive_ids: [0; 2],
            metric_values: std::array::from_fn(|_| BTreeMap::new()),
            categories: vec![0; category_count],
        }
    }

    fn maximum(&self, metric: usize) -> u64 {
        self.metric_values[metric]
            .last_key_value()
            .map_or(0, |(value, _)| *value)
    }
}

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
struct Score {
    hard_excess: u128,
    failed_gates: u32,
    diagnostic_isolation_cost: u128,
    balance_cost: u128,
}

type SearchResult = (Score, Vec<u8>, Vec<(String, u8)>);

struct SearchState<'a> {
    items: &'a [Item],
    assignment: Vec<u8>,
    folds: Vec<FoldState>,
    category_totals: &'a [u16],
    category_cost: u128,
    totals: [u64; 4],
}

pub(crate) fn assign_grouped_folds(selected: &[Performance]) -> Result<FoldPlan, String> {
    let mut ids = BTreeSet::new();
    if selected.iter().any(|item| !ids.insert(item.row.id.clone())) {
        return Err("fold input has duplicate performance ID".to_string());
    }
    if selected.len() < usize::from(FOLD_COUNT)
        || !selected.len().is_multiple_of(usize::from(FOLD_COUNT))
    {
        return Err("fold input count must be a positive multiple of five".to_string());
    }
    let (items, category_totals) = build_items(selected)?;
    let mut best: Option<SearchResult> = None;
    for restart in 0..SEARCH_RESTARTS {
        let (score, assignment, signature) =
            fold_search::search_restart(&items, &category_totals, restart);
        if best
            .as_ref()
            .is_none_or(|current| (score, &signature) < (current.0, &current.2))
        {
            best = Some((score, assignment, signature));
        }
    }
    let (_, assignment, _) = best.ok_or("fold search produced no assignment")?;
    let state = fold_search::state_for(&items, assignment.clone(), &category_totals);
    let qualification = qualify(&state);
    let by_performance_id = items
        .iter()
        .zip(assignment)
        .map(|(item, fold)| (item.id.clone(), fold))
        .collect::<BTreeMap<_, _>>();
    if by_performance_id.len() != selected.len()
        || by_performance_id.values().any(|fold| *fold >= FOLD_COUNT)
    {
        return Err("invalid deterministic five-fold assignment".to_string());
    }
    Ok(FoldPlan {
        by_performance_id,
        lodo_groups: selected
            .iter()
            .map(|item| item.row.drummer.clone())
            .collect(),
        loso_groups: selected
            .iter()
            .map(|item| item.row.session.clone())
            .collect(),
        qualification,
    })
}

fn build_items(selected: &[Performance]) -> Result<(Vec<Item>, Vec<u16>), String> {
    let mut ordered = selected.iter().collect::<Vec<_>>();
    ordered.sort_by(|left, right| {
        left.selection_key
            .cmp(&right.selection_key)
            .then_with(|| left.row.id.cmp(&right.row.id))
    });
    for performance in &ordered {
        crate::drum_excerpt::excerpt_bounds_44100(
            performance.row.duration_samples_44100,
            &performance.row.split,
            &performance.row.id,
        )?;
    }
    let category_values = ordered
        .iter()
        .map(|item| {
            [
                item.row.drummer.clone(),
                item.row.session.clone(),
                item.row.kit_name.clone(),
                item.row.primary_style().to_string(),
                ((item.row.bpm / 20.0).floor() as u16).to_string(),
                ((item.density() * 2.0).floor() as u16).to_string(),
                item.row.split.clone(),
                if item.forced_opened_validation {
                    "forced".to_string()
                } else {
                    "not_forced".to_string()
                },
            ]
        })
        .collect::<Vec<_>>();
    let keys = category_values
        .iter()
        .flat_map(|values| {
            values
                .iter()
                .enumerate()
                .map(|(feature, value)| (feature, value.clone()))
        })
        .collect::<BTreeSet<_>>();
    let index = keys
        .into_iter()
        .enumerate()
        .map(|(index, key)| (key, index))
        .collect::<BTreeMap<_, _>>();
    let mut totals = vec![0_u16; index.len()];
    let mut items = Vec::with_capacity(ordered.len());
    for (performance, values) in ordered.into_iter().zip(category_values) {
        let excerpt = crate::drum_excerpt::excerpt_bounds_44100(
            performance.row.duration_samples_44100,
            &performance.row.split,
            &performance.row.id,
        )?;
        let categories = std::array::from_fn(|feature| index[&(feature, values[feature].clone())]);
        for category in categories {
            totals[category] += 1;
        }
        items.push(Item {
            id: performance.row.id.clone(),
            beat: performance.row.beat_type == "beat",
            metrics: [
                excerpt
                    .end_sample
                    .checked_sub(excerpt.start_sample)
                    .ok_or("invalid excerpt sample interval")?,
                performance.midi.compound_events as u64,
                performance.midi.kick_only_events as u64,
                performance.midi.hat_only_events as u64,
            ],
            categories,
            validation: performance.row.split == "validation",
            forced_opened_validation: performance.forced_opened_validation,
        });
    }
    Ok((items, totals))
}

fn qualify(state: &SearchState<'_>) -> FoldQualification {
    let mut deficits = Vec::new();
    deficit_spread(
        &mut deficits,
        "performance_ids",
        state.folds.iter().map(|fold| fold.ids),
        0,
    );
    deficit_spread(
        &mut deficits,
        "beat_ids",
        state.folds.iter().map(|fold| fold.beat_ids),
        1,
    );
    deficit_spread(
        &mut deficits,
        "fill_ids",
        state.folds.iter().map(|fold| fold.ids - fold.beat_ids),
        1,
    );
    let mut metrics = Vec::new();
    for (metric, name) in METRIC_NAMES.into_iter().enumerate() {
        let per_fold = state
            .folds
            .iter()
            .map(|fold| fold.metrics[metric])
            .collect::<Vec<_>>();
        let minimum = per_fold.iter().copied().min().unwrap_or(0);
        let maximum = per_fold.iter().copied().max().unwrap_or(0);
        deficit_ratio(&mut deficits, name, minimum, maximum, RATIO_LIMITS[metric]);
        for (fold, fold_state) in state.folds.iter().enumerate() {
            deficit_ratio(
                &mut deficits,
                &format!("{name}_fold_{fold}_single_id_share"),
                fold_state.metrics[metric],
                fold_state.maximum(metric),
                RatioLimit::new(1, 4),
            );
        }
        metrics.push(MetricAudit {
            metric: name,
            per_fold,
            minimum,
            maximum,
            max_to_min_ratio: (minimum > 0).then_some(maximum as f64 / minimum as f64),
            maximum_single_id_share_per_fold: state
                .folds
                .iter()
                .map(|fold| {
                    (fold.metrics[metric] > 0)
                        .then_some(fold.maximum(metric) as f64 / fold.metrics[metric] as f64)
                })
                .collect(),
        });
    }
    for (fold, state) in state.folds.iter().enumerate() {
        deficit_minimum(
            &mut deficits,
            &format!("kick_events_fold_{fold}"),
            state.metrics[2],
            MIN_KICK_EVENTS_PER_FOLD,
        );
        deficit_minimum(
            &mut deficits,
            &format!("hat_events_fold_{fold}"),
            state.metrics[3],
            MIN_HAT_EVENTS_PER_FOLD,
        );
        deficit_minimum(
            &mut deficits,
            &format!("kick_positive_ids_fold_{fold}"),
            state.positive_ids[0],
            MIN_POSITIVE_IDS_PER_FOLD,
        );
        deficit_minimum(
            &mut deficits,
            &format!("hat_positive_ids_fold_{fold}"),
            state.positive_ids[1],
            MIN_POSITIVE_IDS_PER_FOLD,
        );
    }
    let qualified = deficits.is_empty();
    FoldQualification {
        status: if qualified {
            "fold_balance_qualified"
        } else {
            "fold_balance_not_qualified"
        }
        .into(),
        qualified,
        policy: fold_policy(),
        audit: FoldAudit {
            performance_ids: state.folds.iter().map(|fold| fold.ids).collect(),
            beat_ids: state.folds.iter().map(|fold| fold.beat_ids).collect(),
            fill_ids: state
                .folds
                .iter()
                .map(|fold| fold.ids - fold.beat_ids)
                .collect(),
            validation_ids: state.folds.iter().map(|fold| fold.validation_ids).collect(),
            forced_opened_validation_ids: state
                .folds
                .iter()
                .map(|fold| fold.forced_opened_validation_ids)
                .collect(),
            metrics,
            kick_positive_ids: state
                .folds
                .iter()
                .map(|fold| fold.positive_ids[0])
                .collect(),
            hat_positive_ids: state
                .folds
                .iter()
                .map(|fold| fold.positive_ids[1])
                .collect(),
            categorical_balance: category_audit(state),
        },
        deficits,
    }
}

fn fold_policy() -> FoldPolicy {
    FoldPolicy {
        require_equal_performance_ids: true,
        maximum_beat_id_spread: 1,
        maximum_fill_id_spread: 1,
        ratio_limits: METRIC_NAMES.into_iter().zip(RATIO_LIMITS).collect(),
        minimum_kick_only_events_per_fold: MIN_KICK_EVENTS_PER_FOLD,
        minimum_hat_only_events_per_fold: MIN_HAT_EVENTS_PER_FOLD,
        minimum_kick_positive_ids_per_fold: MIN_POSITIVE_IDS_PER_FOLD,
        minimum_hat_positive_ids_per_fold: MIN_POSITIVE_IDS_PER_FOLD,
        maximum_single_id_share: RatioLimit::new(1, 4),
        categorical_features_are_diagnostic_only: true,
    }
}

#[cfg(test)]
#[path = "folds_tests.rs"]
mod tests;
