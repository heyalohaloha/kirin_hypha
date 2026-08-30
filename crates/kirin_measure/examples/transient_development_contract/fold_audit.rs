use super::*;

pub(super) fn category_audit(state: &SearchState<'_>) -> Vec<CategoryAudit> {
    CATEGORY_NAMES
        .into_iter()
        .enumerate()
        .map(|(feature_index, feature)| {
            let buckets = state
                .items
                .iter()
                .map(|item| item.categories[feature_index])
                .collect::<BTreeSet<_>>();
            let worst = buckets
                .iter()
                .map(|key| {
                    spread(
                        state
                            .folds
                            .iter()
                            .map(|fold| u64::from(fold.categories[*key])),
                    )
                })
                .max()
                .unwrap_or(0);
            CategoryAudit {
                feature,
                buckets: buckets.len(),
                worst_bucket_count_spread: worst,
            }
        })
        .collect()
}

pub(super) fn deficit_spread(
    deficits: &mut Vec<FoldDeficit>,
    metric: &str,
    values: impl Iterator<Item = u64>,
    limit: u64,
) {
    let actual = spread(values);
    if actual > limit {
        deficits.push(FoldDeficit {
            metric: format!("{metric}_spread"),
            actual: actual.to_string(),
            required: format!("<={limit}"),
        });
    }
}

pub(super) fn deficit_ratio(
    deficits: &mut Vec<FoldDeficit>,
    metric: &str,
    minimum: u64,
    maximum: u64,
    limit: RatioLimit,
) {
    if !limit.accepts(minimum, maximum) {
        deficits.push(FoldDeficit {
            metric: metric.into(),
            actual: if minimum == 0 {
                "undefined_zero_minimum".into()
            } else {
                format!("{:.9}", maximum as f64 / minimum as f64)
            },
            required: format!("<={}/{}", limit.numerator, limit.denominator),
        });
    }
}

pub(super) fn deficit_minimum(
    deficits: &mut Vec<FoldDeficit>,
    metric: &str,
    actual: u64,
    required: u64,
) {
    if actual < required {
        deficits.push(FoldDeficit {
            metric: metric.into(),
            actual: actual.to_string(),
            required: format!(">={required}"),
        });
    }
}

pub(super) fn normalized_square(value: u64, total: u64) -> u128 {
    u128::from(value.abs_diff(total)).pow(2) * 1_000_000 / u128::from(total.max(1)).pow(2)
}

pub(super) fn spread(values: impl Iterator<Item = u64>) -> u64 {
    let values = values.collect::<Vec<_>>();
    values.iter().max().unwrap_or(&0) - values.iter().min().unwrap_or(&0)
}
