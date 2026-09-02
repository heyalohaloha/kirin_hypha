use super::*;
use crate::metrics::{
    Counts, GateResult, MacroValues, MetricMean, MetricValues, TimingValues, TrackEvaluation,
};
use std::collections::BTreeSet;

#[test]
fn five_fold_report_exposes_exact_worst_normalized_margin() {
    let reports = (0..5)
        .map(|fold| {
            let margin = if fold == 3 { -0.10 } else { 0.05 };
            report(margin, margin >= 0.0)
        })
        .collect();
    let formal = build_reports(reports).unwrap();
    assert_eq!(formal.folds.map(|fold| fold.fold), [0, 1, 2, 3, 4]);
    let minimum = formal.worst_fold.minimum_normalized_margin.unwrap();
    assert!((minimum + 0.10).abs() < 1e-12, "{minimum}");
    assert_eq!(formal.worst_fold.limiting_fold, Some(3));
    assert_eq!(formal.worst_fold.limiting_gate, Some("precision"));
    assert!(!formal.worst_fold.all_fold_gates_passed);
}

#[test]
fn missing_performance_macro_contributor_is_not_evaluable_and_cannot_pass() {
    let mut incomplete = report(0.0, true);
    incomplete.tracks[0].counts.duration_seconds = 0.0;
    let reports = (0..5)
        .map(|fold| {
            if fold == 2 {
                incomplete.clone()
            } else {
                report(0.0, true)
            }
        })
        .collect();
    let formal = build_reports(reports).unwrap();
    let gate = formal.folds[2]
        .formal_gates
        .iter()
        .find(|gate| gate.name == "performance_macro_false_positives_per_second")
        .unwrap();
    assert_eq!(gate.actual, None);
    assert!(!gate.passed);
    assert!(!formal.worst_fold.all_folds_evaluable);
    assert!(!formal.worst_fold.all_fold_gates_passed);
}

#[test]
fn formal_macro_policy_separates_labeled_and_negative_performances() {
    let mut mixed = report(0.0, true);
    let mut negative = track();
    negative.performance_id = "synthetic-negative".to_string();
    negative.counts = Counts {
        duration_seconds: 1.0,
        ..Counts::default()
    };
    mixed.tracks.push(negative);
    let formal = build_reports((0..5).map(|_| mixed.clone()).collect()).unwrap();
    let gates = &formal.folds[0].formal_gates;
    let precision = gates
        .iter()
        .find(|gate| gate.name == "performance_macro_precision")
        .unwrap();
    assert_eq!(precision.contributors, 1);
    assert_eq!(precision.expected_contributors, 1);
    assert_eq!(precision.actual, Some(1.0));
    let fp_rate = gates
        .iter()
        .find(|gate| gate.name == "performance_macro_false_positives_per_second")
        .unwrap();
    assert_eq!(fp_rate.contributors, 2);
    assert_eq!(fp_rate.expected_contributors, 2);
    assert_eq!(fp_rate.actual, Some(0.0));
}

#[test]
fn labeled_zero_prediction_performance_contributes_zeros() {
    let mut zero_prediction = report(0.0, true);
    zero_prediction.tracks[0].counts = Counts {
        duration_seconds: 1.0,
        label_count: 1,
        fn_count: 1,
        ..Counts::default()
    };
    let formal = build_reports((0..5).map(|_| zero_prediction.clone()).collect()).unwrap();
    for name in [
        "performance_macro_precision",
        "performance_macro_recall",
        "performance_macro_f1",
    ] {
        let gate = formal.folds[0]
            .formal_gates
            .iter()
            .find(|gate| gate.name == name)
            .unwrap();
        assert_eq!(gate.actual, Some(0.0));
        assert_eq!(gate.contributors, 1);
        assert_eq!(gate.expected_contributors, 1);
        assert!(!gate.passed);
    }
}

#[test]
fn missing_fold_or_nonevaluable_gate_cannot_form_a_passing_worst_fold() {
    let error = build_reports((0..4).map(|_| report(0.0, true)).collect()).unwrap_err();
    assert!(error.contains("each fold 0..4"));

    let mut fold_four = report(0.0, false);
    fold_four.gates[0].actual = None;
    let reports = (0..4)
        .map(|_| report(0.0, true))
        .chain(std::iter::once(fold_four))
        .collect();
    let formal = build_reports(reports).unwrap();
    assert!(!formal.worst_fold.all_folds_evaluable);
    assert_eq!(formal.worst_fold.minimum_normalized_margin, None);
    assert_eq!(formal.worst_fold.limiting_fold, Some(4));
}

#[test]
fn formal_report_set_rejects_identity_membership_and_pooled_union_drift() {
    let reports = (0..5).map(|_| report(0.0, true)).collect();
    let (membership, pooled, mut folds) = report_set(reports);
    folds[0].1.identity.candidate_id = "different-candidate".to_string();
    let error = build_formal_evaluation(membership, pooled, folds).unwrap_err();
    assert!(error.contains("one candidate/config/definition"), "{error}");

    let reports = (0..5).map(|_| report(0.0, true)).collect();
    let (membership, pooled, mut folds) = report_set(reports);
    folds[0].1.evaluation.tracks[0].performance_id = "wrong-fold-id".to_string();
    let error = build_formal_evaluation(membership, pooled, folds).unwrap_err();
    assert!(error.contains("manifest membership"), "{error}");

    let reports = (0..5).map(|_| report(0.0, true)).collect();
    let (membership, mut pooled, folds) = report_set(reports);
    pooled.evaluation.tracks[0].kit_name = "mutated-pooled-track".to_string();
    let error = build_formal_evaluation(membership, pooled, folds).unwrap_err();
    assert!(error.contains("exact union"), "{error}");
}

fn build_reports(reports: Vec<EvaluationReport>) -> Result<FormalEvaluationReport, String> {
    let (membership, pooled, folds) = report_set(reports);
    build_formal_evaluation(membership, pooled, folds)
}

fn report_set(
    mut reports: Vec<EvaluationReport>,
) -> (
    FormalMembershipContract,
    FormalScoredReport,
    Vec<(u8, FormalScoredReport)>,
) {
    assert!(!reports.is_empty());
    let identity = FormalReportIdentity::synthetic();
    let mut fold_ids: [BTreeSet<String>; 5] = std::array::from_fn(|_| BTreeSet::new());
    let mut pooled = reports[0].clone();
    pooled.counts = Counts::default();
    pooled.tracks.clear();
    let mut folds = Vec::with_capacity(reports.len());
    for (fold, mut evaluation) in reports.drain(..).enumerate() {
        for (track_index, track) in evaluation.tracks.iter_mut().enumerate() {
            track.performance_id = format!("formal-fold-{fold}-track-{track_index}");
            if fold < fold_ids.len() {
                fold_ids[fold].insert(track.performance_id.clone());
            }
            pooled.counts.add(&track.counts);
        }
        pooled.tracks.extend(evaluation.tracks.iter().cloned());
        folds.push((
            u8::try_from(fold).unwrap(),
            FormalScoredReport::synthetic(identity.clone(), evaluation),
        ));
    }
    (
        FormalMembershipContract::synthetic(fold_ids),
        FormalScoredReport::synthetic(identity, pooled),
        folds,
    )
}

fn report(precision_margin: f64, passed: bool) -> EvaluationReport {
    let precision_target = 0.85;
    let precision_actual = precision_target * (1.0 + precision_margin);
    EvaluationReport {
        counts: counts(),
        micro: MetricValues {
            precision: Some(precision_actual),
            recall: Some(1.0),
            f1: Some(1.0),
            false_positives_per_second: Some(0.0),
            kick_only_recall: Some(1.0),
            hat_only_recall: Some(1.0),
            kick_containing_recall: Some(1.0),
            hat_containing_recall: Some(1.0),
        },
        macro_values: MacroValues {
            aggregation_unit: "fixture",
            precision: mean(1.0),
            recall: mean(1.0),
            f1: mean(1.0),
            false_positives_per_second: mean(0.0),
        },
        timing: TimingValues {
            absolute_p50_ms: Some(0.0),
            absolute_p95_ms: Some(0.0),
            absolute_max_ms: Some(0.0),
            signed_mean_ms: Some(0.0),
            signed_median_ms: Some(0.0),
        },
        gates: vec![GateResult {
            name: "precision",
            target: precision_target,
            comparison: "greater_than_or_equal",
            actual: Some(precision_actual),
            status: if passed { "pass" } else { "fail" },
            passed,
        }],
        all_gates_passed: passed,
        tracks: vec![track()],
    }
}

fn counts() -> Counts {
    Counts {
        duration_seconds: 1.0,
        label_count: 2,
        prediction_count: 2,
        tp: 2,
        kick_only_tp: 1,
        kick_only_total: 1,
        hat_only_tp: 1,
        hat_only_total: 1,
        ..Counts::default()
    }
}

fn track() -> TrackEvaluation {
    let counts = counts();
    TrackEvaluation {
        performance_id: "synthetic-performance".to_string(),
        kit_name: "synthetic-kit".to_string(),
        source_split: "train".to_string(),
        metrics: MetricValues {
            precision: Some(1.0),
            recall: Some(1.0),
            f1: Some(1.0),
            false_positives_per_second: Some(0.0),
            kick_only_recall: Some(1.0),
            hat_only_recall: Some(1.0),
            kick_containing_recall: Some(1.0),
            hat_containing_recall: Some(1.0),
        },
        timing: TimingValues {
            absolute_p50_ms: Some(0.0),
            absolute_p95_ms: Some(0.0),
            absolute_max_ms: Some(0.0),
            signed_mean_ms: Some(0.0),
            signed_median_ms: Some(0.0),
        },
        counts,
    }
}

fn mean(value: f64) -> MetricMean {
    MetricMean {
        value: Some(value),
        contributors: 1,
    }
}
