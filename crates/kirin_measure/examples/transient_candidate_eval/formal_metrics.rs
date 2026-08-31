use std::collections::BTreeMap;

use serde::Serialize;

use super::{Counts, EvaluationReport};

#[path = "formal_report_set.rs"]
mod formal_report_set;

use formal_report_set::{
    validate_report_set, FormalMembershipContract, FormalReportIdentity, FormalScoredReport,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum FormalGateAggregation {
    Micro,
    PerformanceMacro,
    KickOnlyContributorMacro,
    HatOnlyContributorMacro,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct FormalGateResult {
    pub(crate) name: &'static str,
    pub(crate) aggregation: FormalGateAggregation,
    pub(crate) target: f64,
    pub(crate) comparison: &'static str,
    pub(crate) actual: Option<f64>,
    pub(crate) contributors: usize,
    pub(crate) expected_contributors: usize,
    pub(crate) passed: bool,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct NormalizedGateMargin {
    pub(crate) gate: &'static str,
    pub(crate) aggregation: FormalGateAggregation,
    pub(crate) comparison: &'static str,
    pub(crate) target: f64,
    pub(crate) actual: Option<f64>,
    pub(crate) normalized_margin: Option<f64>,
    pub(crate) passed: bool,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct FormalFoldEvaluation {
    pub(crate) fold: u8,
    pub(crate) formal_gates: Vec<FormalGateResult>,
    pub(crate) normalized_gate_margins: Vec<NormalizedGateMargin>,
    pub(crate) evaluation: EvaluationReport,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct WorstFoldResult {
    pub(crate) all_folds_evaluable: bool,
    pub(crate) all_fold_gates_passed: bool,
    pub(crate) minimum_normalized_margin: Option<f64>,
    pub(crate) limiting_fold: Option<u8>,
    pub(crate) limiting_gate: Option<&'static str>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct FormalEvaluationReport {
    pub(crate) aggregation_contract: &'static str,
    pub(crate) manifest_sha256: String,
    pub(crate) performance_id_count: usize,
    pub(crate) identity: FormalReportIdentity,
    pub(crate) pooled: EvaluationReport,
    pub(crate) pooled_formal_gates: Vec<FormalGateResult>,
    pub(crate) folds: [FormalFoldEvaluation; 5],
    pub(crate) worst_fold: WorstFoldResult,
}

pub(crate) fn build_formal_evaluation(
    membership: FormalMembershipContract,
    pooled: FormalScoredReport,
    mut fold_reports: Vec<(u8, FormalScoredReport)>,
) -> Result<FormalEvaluationReport, String> {
    fold_reports.sort_by_key(|(fold, _)| *fold);
    if fold_reports.len() != 5
        || fold_reports
            .iter()
            .enumerate()
            .any(|(expected, (actual, _))| usize::from(*actual) != expected)
    {
        return Err("formal evaluation requires exactly one report for each fold 0..4".to_string());
    }
    validate_report_set(&membership, &pooled, &fold_reports)?;
    let identity = pooled.identity;
    let pooled = pooled.evaluation;
    let folds = fold_reports
        .into_iter()
        .map(|(fold, report)| {
            let evaluation = report.evaluation;
            let formal_gates = formal_gates(&evaluation);
            FormalFoldEvaluation {
                fold,
                normalized_gate_margins: formal_gates
                    .iter()
                    .map(|gate| NormalizedGateMargin {
                        gate: gate.name,
                        aggregation: gate.aggregation,
                        comparison: gate.comparison,
                        target: gate.target,
                        actual: gate.actual,
                        normalized_margin: normalized_margin(
                            gate.comparison,
                            gate.target,
                            gate.actual,
                        ),
                        passed: gate.passed,
                    })
                    .collect(),
                formal_gates,
                evaluation,
            }
        })
        .collect::<Vec<_>>()
        .try_into()
        .map_err(|_| "formal fold report count invariant failed".to_string())?;
    let worst_fold = summarize_worst_fold(&folds);
    let pooled_formal_gates = formal_gates(&pooled);
    Ok(FormalEvaluationReport {
        aggregation_contract:
            "manifest_exact_disjoint_fold_union_and_shared_candidate_config_definition_identity;pooled_micro_and_performance_macro_plus_every_grouped_fold;no_fold_compensation",
        manifest_sha256: membership.manifest_sha256,
        performance_id_count: membership.performance_id_count,
        identity,
        pooled,
        pooled_formal_gates,
        folds,
        worst_fold,
    })
}

fn normalized_margin(comparison: &str, target: f64, actual: Option<f64>) -> Option<f64> {
    let actual = actual?;
    if !actual.is_finite() || !target.is_finite() || target <= 0.0 {
        return None;
    }
    match comparison {
        "greater_than_or_equal" => Some((actual - target) / target),
        "less_than_or_equal" => Some((target - actual) / target),
        _ => None,
    }
}

fn summarize_worst_fold(folds: &[FormalFoldEvaluation; 5]) -> WorstFoldResult {
    let all_folds_evaluable = folds.iter().all(|fold| {
        fold.normalized_gate_margins
            .iter()
            .all(|gate| gate.normalized_margin.is_some())
    });
    let all_fold_gates_passed = all_folds_evaluable
        && folds
            .iter()
            .all(|fold| fold.formal_gates.iter().all(|gate| gate.passed));
    if !all_folds_evaluable {
        let missing = folds.iter().find_map(|fold| {
            fold.normalized_gate_margins
                .iter()
                .find(|gate| gate.normalized_margin.is_none())
                .map(|gate| (fold.fold, gate.gate))
        });
        return WorstFoldResult {
            all_folds_evaluable,
            all_fold_gates_passed,
            minimum_normalized_margin: None,
            limiting_fold: missing.map(|(fold, _)| fold),
            limiting_gate: missing.map(|(_, gate)| gate),
        };
    }
    let limiting = folds
        .iter()
        .flat_map(|fold| {
            fold.normalized_gate_margins
                .iter()
                .map(move |gate| (fold.fold, gate))
        })
        .min_by(|(left_fold, left), (right_fold, right)| {
            left.normalized_margin
                .unwrap()
                .total_cmp(&right.normalized_margin.unwrap())
                .then_with(|| left_fold.cmp(right_fold))
                .then_with(|| left.gate.cmp(right.gate))
        });
    WorstFoldResult {
        all_folds_evaluable,
        all_fold_gates_passed,
        minimum_normalized_margin: limiting.map(|(_, gate)| gate.normalized_margin.unwrap()),
        limiting_fold: limiting.map(|(fold, _)| fold),
        limiting_gate: limiting.map(|(_, gate)| gate.gate),
    }
}

fn formal_gates(evaluation: &EvaluationReport) -> Vec<FormalGateResult> {
    let performance_counts = performance_counts(evaluation);
    let expected = performance_counts.len();
    let mut gates = evaluation
        .gates
        .iter()
        .map(|gate| {
            let actual = (expected > 0).then_some(gate.actual).flatten();
            FormalGateResult {
                name: gate.name,
                aggregation: FormalGateAggregation::Micro,
                target: gate.target,
                comparison: gate.comparison,
                actual,
                contributors: expected,
                expected_contributors: expected,
                passed: expected > 0 && gate.passed,
            }
        })
        .collect::<Vec<_>>();
    let performance_macro = formal_performance_macro(&performance_counts);
    gates.extend([
        macro_gate(
            "performance_macro_precision",
            0.85,
            performance_macro.precision,
            false,
        ),
        macro_gate(
            "performance_macro_recall",
            0.75,
            performance_macro.recall,
            false,
        ),
        macro_gate("performance_macro_f1", 0.80, performance_macro.f1, false),
        macro_gate(
            "performance_macro_false_positives_per_second",
            1.0,
            performance_macro.false_positives_per_second,
            true,
        ),
    ]);
    let kick = contributor_mean(&performance_counts, |counts| {
        (counts.kick_only_tp, counts.kick_only_total)
    });
    let hat = contributor_mean(&performance_counts, |counts| {
        (counts.hat_only_tp, counts.hat_only_total)
    });
    gates.push(contributor_gate(
        "kick_only_contributor_macro_recall",
        0.75,
        kick,
        FormalGateAggregation::KickOnlyContributorMacro,
    ));
    gates.push(contributor_gate(
        "hat_only_contributor_macro_recall",
        0.50,
        hat,
        FormalGateAggregation::HatOnlyContributorMacro,
    ));
    gates
}

fn macro_gate(
    name: &'static str,
    target: f64,
    values: ContributorMean,
    upper: bool,
) -> FormalGateResult {
    let actual = (values.contributors == values.expected_contributors
        && values.expected_contributors > 0)
        .then_some(values.value)
        .flatten();
    let passed = actual.is_some_and(|actual| {
        if upper {
            actual <= target
        } else {
            actual >= target
        }
    });
    FormalGateResult {
        name,
        aggregation: FormalGateAggregation::PerformanceMacro,
        target,
        comparison: if upper {
            "less_than_or_equal"
        } else {
            "greater_than_or_equal"
        },
        actual,
        contributors: values.contributors,
        expected_contributors: values.expected_contributors,
        passed,
    }
}

fn contributor_gate(
    name: &'static str,
    target: f64,
    values: ContributorMean,
    aggregation: FormalGateAggregation,
) -> FormalGateResult {
    let actual = (values.contributors == values.expected_contributors
        && values.expected_contributors > 0)
        .then_some(values.value)
        .flatten();
    let passed = actual.is_some_and(|actual| actual >= target);
    FormalGateResult {
        name,
        aggregation,
        target,
        comparison: "greater_than_or_equal",
        actual,
        contributors: values.contributors,
        expected_contributors: values.expected_contributors,
        passed,
    }
}

fn performance_counts(evaluation: &EvaluationReport) -> BTreeMap<&str, Counts> {
    let mut performance = BTreeMap::<&str, Counts>::new();
    for track in &evaluation.tracks {
        performance
            .entry(&track.performance_id)
            .or_default()
            .add(&track.counts);
    }
    performance
}

struct ContributorMean {
    value: Option<f64>,
    contributors: usize,
    expected_contributors: usize,
}

struct FormalPerformanceMacro {
    precision: ContributorMean,
    recall: ContributorMean,
    f1: ContributorMean,
    false_positives_per_second: ContributorMean,
}

fn formal_performance_macro(counts: &BTreeMap<&str, Counts>) -> FormalPerformanceMacro {
    let expected_labeled = counts
        .values()
        .filter(|counts| counts.label_count > 0)
        .count();
    let labeled = counts
        .values()
        .filter(|counts| counts.label_count > 0)
        .filter_map(|counts| {
            if counts.tp > counts.label_count || counts.tp > counts.prediction_count {
                return None;
            }
            let precision = if counts.prediction_count == 0 {
                0.0
            } else {
                counts.tp as f64 / counts.prediction_count as f64
            };
            let recall = counts.tp as f64 / counts.label_count as f64;
            let f1 = if precision + recall == 0.0 {
                0.0
            } else {
                2.0 * precision * recall / (precision + recall)
            };
            Some((precision, recall, f1))
        })
        .collect::<Vec<_>>();
    let fp_per_second = counts
        .values()
        .filter_map(|counts| {
            (counts.duration_seconds.is_finite() && counts.duration_seconds > 0.0)
                .then_some(counts.fp as f64 / counts.duration_seconds)
        })
        .collect::<Vec<_>>();
    FormalPerformanceMacro {
        precision: contributor_values(labeled.iter().map(|values| values.0), expected_labeled),
        recall: contributor_values(labeled.iter().map(|values| values.1), expected_labeled),
        f1: contributor_values(labeled.iter().map(|values| values.2), expected_labeled),
        false_positives_per_second: contributor_values(fp_per_second, counts.len()),
    }
}

fn contributor_values(
    values: impl IntoIterator<Item = f64>,
    expected_contributors: usize,
) -> ContributorMean {
    let values = values.into_iter().collect::<Vec<_>>();
    ContributorMean {
        value: (!values.is_empty()).then(|| values.iter().sum::<f64>() / values.len() as f64),
        contributors: values.len(),
        expected_contributors,
    }
}

fn contributor_mean(
    counts: &BTreeMap<&str, Counts>,
    field: impl Fn(&Counts) -> (usize, usize),
) -> ContributorMean {
    let expected = counts.values().filter(|counts| field(counts).1 > 0).count();
    let values = counts
        .values()
        .filter_map(|counts| {
            let (tp, total) = field(counts);
            (total > 0 && tp <= total).then_some(tp as f64 / total as f64)
        })
        .collect::<Vec<_>>();
    contributor_values(values, expected)
}

#[cfg(test)]
#[path = "formal_metrics_tests.rs"]
mod tests;
