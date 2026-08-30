use std::collections::BTreeMap;

use serde::Serialize;

#[derive(Clone, Debug, Default, Serialize)]
pub(crate) struct Counts {
    pub(crate) duration_seconds: f64,
    pub(crate) label_count: usize,
    pub(crate) prediction_count: usize,
    pub(crate) tp: usize,
    pub(crate) fp: usize,
    pub(crate) fn_count: usize,
    pub(crate) duplicate_fp: usize,
    pub(crate) merged_fn: usize,
    pub(crate) kick_only_tp: usize,
    pub(crate) kick_only_total: usize,
    pub(crate) hat_only_tp: usize,
    pub(crate) hat_only_total: usize,
    pub(crate) kick_containing_tp: usize,
    pub(crate) kick_containing_total: usize,
    pub(crate) hat_containing_tp: usize,
    pub(crate) hat_containing_total: usize,
}

impl Counts {
    pub(crate) fn add(&mut self, other: &Self) {
        self.duration_seconds += other.duration_seconds;
        self.label_count += other.label_count;
        self.prediction_count += other.prediction_count;
        self.tp += other.tp;
        self.fp += other.fp;
        self.fn_count += other.fn_count;
        self.duplicate_fp += other.duplicate_fp;
        self.merged_fn += other.merged_fn;
        self.kick_only_tp += other.kick_only_tp;
        self.kick_only_total += other.kick_only_total;
        self.hat_only_tp += other.hat_only_tp;
        self.hat_only_total += other.hat_only_total;
        self.kick_containing_tp += other.kick_containing_tp;
        self.kick_containing_total += other.kick_containing_total;
        self.hat_containing_tp += other.hat_containing_tp;
        self.hat_containing_total += other.hat_containing_total;
    }
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MetricValues {
    pub(crate) precision: Option<f64>,
    pub(crate) recall: Option<f64>,
    pub(crate) f1: Option<f64>,
    pub(crate) false_positives_per_second: Option<f64>,
    pub(crate) kick_only_recall: Option<f64>,
    pub(crate) hat_only_recall: Option<f64>,
    pub(crate) kick_containing_recall: Option<f64>,
    pub(crate) hat_containing_recall: Option<f64>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct TimingValues {
    pub(crate) absolute_p50_ms: Option<f64>,
    pub(crate) absolute_p95_ms: Option<f64>,
    pub(crate) absolute_max_ms: Option<f64>,
    pub(crate) signed_mean_ms: Option<f64>,
    pub(crate) signed_median_ms: Option<f64>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct TrackEvaluation {
    pub(crate) performance_id: String,
    pub(crate) kit_name: String,
    pub(crate) source_split: String,
    pub(crate) counts: Counts,
    pub(crate) metrics: MetricValues,
    pub(crate) timing: TimingValues,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct GateResult {
    pub(crate) name: &'static str,
    pub(crate) target: f64,
    pub(crate) comparison: &'static str,
    pub(crate) actual: Option<f64>,
    pub(crate) status: &'static str,
    pub(crate) passed: bool,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MetricMean {
    pub(crate) value: Option<f64>,
    pub(crate) contributors: usize,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MacroValues {
    pub(crate) aggregation_unit: &'static str,
    pub(crate) precision: MetricMean,
    pub(crate) recall: MetricMean,
    pub(crate) f1: MetricMean,
    pub(crate) false_positives_per_second: MetricMean,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct EvaluationReport {
    pub(crate) counts: Counts,
    pub(crate) micro: MetricValues,
    pub(crate) macro_values: MacroValues,
    pub(crate) timing: TimingValues,
    pub(crate) gates: Vec<GateResult>,
    pub(crate) all_gates_passed: bool,
    pub(crate) tracks: Vec<TrackEvaluation>,
}

pub(crate) fn metrics(counts: &Counts) -> MetricValues {
    let precision = ratio(counts.tp, counts.tp + counts.fp);
    let recall = ratio(counts.tp, counts.tp + counts.fn_count);
    MetricValues {
        precision,
        recall,
        f1: match (precision, recall) {
            (Some(precision), Some(recall)) if precision + recall > 0.0 => {
                Some(2.0 * precision * recall / (precision + recall))
            }
            (Some(_), Some(_)) => Some(0.0),
            _ => None,
        },
        false_positives_per_second: (counts.duration_seconds > 0.0)
            .then_some(counts.fp as f64 / counts.duration_seconds),
        kick_only_recall: ratio(counts.kick_only_tp, counts.kick_only_total),
        hat_only_recall: ratio(counts.hat_only_tp, counts.hat_only_total),
        kick_containing_recall: ratio(counts.kick_containing_tp, counts.kick_containing_total),
        hat_containing_recall: ratio(counts.hat_containing_tp, counts.hat_containing_total),
    }
}

pub(crate) fn timing(signed_seconds: &[f64]) -> TimingValues {
    if signed_seconds.is_empty() {
        return TimingValues {
            absolute_p50_ms: None,
            absolute_p95_ms: None,
            absolute_max_ms: None,
            signed_mean_ms: None,
            signed_median_ms: None,
        };
    }
    let mut absolute = signed_seconds
        .iter()
        .map(|value| value.abs())
        .collect::<Vec<_>>();
    absolute.sort_by(f64::total_cmp);
    let mut signed = signed_seconds.to_vec();
    signed.sort_by(f64::total_cmp);
    TimingValues {
        absolute_p50_ms: Some(nearest_rank(&absolute, 0.50) * 1_000.0),
        absolute_p95_ms: Some(nearest_rank(&absolute, 0.95) * 1_000.0),
        absolute_max_ms: absolute.last().map(|value| value * 1_000.0),
        signed_mean_ms: Some(signed.iter().sum::<f64>() / signed.len() as f64 * 1_000.0),
        signed_median_ms: Some(median(&signed) * 1_000.0),
    }
}

pub(crate) fn gates(metrics: &MetricValues, timing: &TimingValues) -> Vec<GateResult> {
    vec![
        lower_gate("precision", 0.85, metrics.precision),
        lower_gate("recall", 0.75, metrics.recall),
        lower_gate("f1", 0.80, metrics.f1),
        upper_gate("timing_absolute_p95_ms", 15.0, timing.absolute_p95_ms),
        upper_gate(
            "false_positives_per_second",
            1.0,
            metrics.false_positives_per_second,
        ),
        upper_gate(
            "signed_timing_median_absolute_ms",
            5.333,
            timing.signed_median_ms.map(f64::abs),
        ),
        lower_gate("kick_only_recall", 0.75, metrics.kick_only_recall),
        lower_gate("hat_only_recall", 0.50, metrics.hat_only_recall),
    ]
}

pub(crate) fn macro_metrics(tracks: &[TrackEvaluation]) -> MacroValues {
    let mut performance_counts = BTreeMap::<&str, Counts>::new();
    for track in tracks {
        performance_counts
            .entry(&track.performance_id)
            .or_default()
            .add(&track.counts);
    }
    let values = performance_counts.values().map(metrics).collect::<Vec<_>>();
    MacroValues {
        aggregation_unit: "performance_id_counts_then_metrics",
        precision: average(values.iter().filter_map(|value| value.precision)),
        recall: average(values.iter().filter_map(|value| value.recall)),
        f1: average(values.iter().filter_map(|value| value.f1)),
        false_positives_per_second: average(
            values
                .iter()
                .filter_map(|value| value.false_positives_per_second),
        ),
    }
}

fn lower_gate(name: &'static str, target: f64, actual: Option<f64>) -> GateResult {
    let passed = actual.is_some_and(|value| value >= target);
    GateResult {
        name,
        target,
        comparison: "greater_than_or_equal",
        actual,
        status: gate_status(actual, passed),
        passed,
    }
}

fn upper_gate(name: &'static str, target: f64, actual: Option<f64>) -> GateResult {
    let passed = actual.is_some_and(|value| value <= target);
    GateResult {
        name,
        target,
        comparison: "less_than_or_equal",
        actual,
        status: gate_status(actual, passed),
        passed,
    }
}

fn ratio(numerator: usize, denominator: usize) -> Option<f64> {
    (denominator > 0).then_some(numerator as f64 / denominator as f64)
}

fn nearest_rank(sorted: &[f64], percentile: f64) -> f64 {
    sorted[((percentile * sorted.len() as f64).ceil() as usize).saturating_sub(1)]
}

fn median(sorted: &[f64]) -> f64 {
    let middle = sorted.len() / 2;
    if sorted.len().is_multiple_of(2) {
        (sorted[middle - 1] + sorted[middle]) / 2.0
    } else {
        sorted[middle]
    }
}

fn average(values: impl Iterator<Item = f64>) -> MetricMean {
    let values = values.collect::<Vec<_>>();
    MetricMean {
        value: (!values.is_empty()).then(|| values.iter().sum::<f64>() / values.len() as f64),
        contributors: values.len(),
    }
}

fn gate_status(actual: Option<f64>, passed: bool) -> &'static str {
    if actual.is_none() {
        "not_evaluable"
    } else if passed {
        "pass"
    } else {
        "fail"
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_denominators_and_timing_are_not_fabricated_as_zero() {
        let values = metrics(&Counts::default());
        let timing = timing(&[]);
        assert_eq!(values.precision, None);
        assert_eq!(values.kick_only_recall, None);
        assert_eq!(timing.absolute_p95_ms, None);
        assert!(gates(&values, &timing).iter().all(|gate| !gate.passed));
    }

    #[test]
    fn nearest_rank_and_even_signed_median_are_fixed() {
        let values = timing(&[-0.004, -0.002, 0.001, 0.010]);
        assert_eq!(values.absolute_p50_ms, Some(2.0));
        assert_eq!(values.absolute_p95_ms, Some(10.0));
        assert_eq!(values.signed_median_ms, Some(-0.5));
    }

    #[test]
    fn macro_aggregates_kit_renders_once_per_performance() {
        let track = |performance_id: &str, tp, fp, fn_count| {
            let counts = Counts {
                duration_seconds: 1.0,
                label_count: tp + fn_count,
                prediction_count: tp + fp,
                tp,
                fp,
                fn_count,
                ..Counts::default()
            };
            TrackEvaluation {
                performance_id: performance_id.to_string(),
                kit_name: "kit".to_string(),
                source_split: "test".to_string(),
                metrics: metrics(&counts),
                timing: timing(&[]),
                counts,
            }
        };
        let values = macro_metrics(&[
            track("same", 1, 0, 0),
            track("same", 0, 1, 1),
            track("other", 1, 0, 0),
        ]);

        assert_eq!(
            values.aggregation_unit,
            "performance_id_counts_then_metrics"
        );
        assert_eq!(values.precision.contributors, 2);
        assert_eq!(values.precision.value, Some(0.75));
    }
}
