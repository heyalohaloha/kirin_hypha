use serde::Serialize;

use super::annotation::{AnnotationArtifact, Completion};
use super::contract::TOOL_VERSION;
use super::manifest::SourceKind;
use super::matching::{match_events, TOLERANCE_MICROS};
use super::plan::{definition_sha256, PlanArtifact, PLAN_SCHEMA};

const RESULT_SCHEMA: &str = "kirin-hypha-attack-midi-proxy-audit-result-v1";
const INTER_ANNOTATOR_F1_MINIMUM: f64 = 0.90;
const MIDI_PROXY_PRECISION_MINIMUM: f64 = 0.95;
const MIDI_PROXY_RECALL_MINIMUM: f64 = 0.95;

#[derive(Debug, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct AuditResult {
    schema: String,
    tool_version: String,
    definition_sha256: String,
    input_plan_schema: String,
    plan_sha256: String,
    source_kind: SourceKind,
    source_id: String,
    source_sha256: String,
    formal_gate_eligible: bool,
    formal_gate_blocker: String,
    annotator_a_id: String,
    annotator_b_id: String,
    annotator_a_sha256: String,
    annotator_b_sha256: String,
    candidate_output_observed: bool,
    tolerance_micros_inclusive: i64,
    thresholds: Thresholds,
    pub(crate) status: String,
    not_ready_reasons: Vec<String>,
    metrics: Option<AuditMetrics>,
    gates: Option<Gates>,
}

#[derive(Debug, Serialize)]
#[serde(deny_unknown_fields)]
struct Thresholds {
    inter_annotator_f1_minimum: f64,
    midi_proxy_precision_minimum_each_annotator: f64,
    midi_proxy_recall_minimum_each_annotator: f64,
}

#[derive(Debug, Serialize)]
#[serde(deny_unknown_fields)]
struct AuditMetrics {
    annotator_a_vs_b: PairMetrics,
    midi_proxy_vs_annotator_a: PairMetrics,
    midi_proxy_vs_annotator_b: PairMetrics,
}

#[derive(Clone, Copy, Debug, Serialize)]
#[serde(deny_unknown_fields)]
struct PairMetrics {
    prediction_count: usize,
    reference_count: usize,
    match_count: usize,
    precision: f64,
    recall: f64,
    f1: f64,
}

#[derive(Debug, Serialize)]
#[serde(deny_unknown_fields)]
struct Gates {
    inter_annotator_f1: bool,
    midi_proxy_vs_annotator_a_precision: bool,
    midi_proxy_vs_annotator_a_recall: bool,
    midi_proxy_vs_annotator_b_precision: bool,
    midi_proxy_vs_annotator_b_recall: bool,
    all_passed: bool,
}

pub(crate) fn score(
    plan: &PlanArtifact,
    annotator_a: &AnnotationArtifact,
    annotator_b: &AnnotationArtifact,
) -> AuditResult {
    let thresholds = Thresholds {
        inter_annotator_f1_minimum: INTER_ANNOTATOR_F1_MINIMUM,
        midi_proxy_precision_minimum_each_annotator: MIDI_PROXY_PRECISION_MINIMUM,
        midi_proxy_recall_minimum_each_annotator: MIDI_PROXY_RECALL_MINIMUM,
    };
    let mut not_ready_reasons = Vec::new();
    if annotator_a.completion != Completion::Complete {
        not_ready_reasons.push("annotator_A_pending".to_string());
    }
    if annotator_b.completion != Completion::Complete {
        not_ready_reasons.push("annotator_B_pending".to_string());
    }
    let (status, metrics, gates) = if not_ready_reasons.is_empty() {
        let proxy = plan
            .plan
            .items
            .iter()
            .map(|item| {
                item.midi_proxy_onsets_micros
                    .iter()
                    .map(|&value| i64::try_from(value).expect("validated audit onset fits i64"))
                    .collect::<Vec<_>>()
            })
            .collect::<Vec<_>>();
        let inter = compare(&annotator_a.events_by_item, &annotator_b.events_by_item);
        let proxy_a = compare(&proxy, &annotator_a.events_by_item);
        let proxy_b = compare(&proxy, &annotator_b.events_by_item);
        let gates = Gates {
            inter_annotator_f1: inter.f1 >= INTER_ANNOTATOR_F1_MINIMUM,
            midi_proxy_vs_annotator_a_precision: proxy_a.precision >= MIDI_PROXY_PRECISION_MINIMUM,
            midi_proxy_vs_annotator_a_recall: proxy_a.recall >= MIDI_PROXY_RECALL_MINIMUM,
            midi_proxy_vs_annotator_b_precision: proxy_b.precision >= MIDI_PROXY_PRECISION_MINIMUM,
            midi_proxy_vs_annotator_b_recall: proxy_b.recall >= MIDI_PROXY_RECALL_MINIMUM,
            all_passed: false,
        };
        let gates = Gates {
            all_passed: gates.inter_annotator_f1
                && gates.midi_proxy_vs_annotator_a_precision
                && gates.midi_proxy_vs_annotator_a_recall
                && gates.midi_proxy_vs_annotator_b_precision
                && gates.midi_proxy_vs_annotator_b_recall,
            ..gates
        };
        let status = if gates.all_passed {
            "synthetic_fixture_pass"
        } else {
            "synthetic_fixture_fail"
        }
        .to_string();
        (
            status,
            Some(AuditMetrics {
                annotator_a_vs_b: inter,
                midi_proxy_vs_annotator_a: proxy_a,
                midi_proxy_vs_annotator_b: proxy_b,
            }),
            Some(gates),
        )
    } else {
        ("synthetic_fixture_not_ready".to_string(), None, None)
    };
    AuditResult {
        schema: RESULT_SCHEMA.to_string(),
        tool_version: TOOL_VERSION.to_string(),
        definition_sha256: definition_sha256(),
        input_plan_schema: PLAN_SCHEMA.to_string(),
        plan_sha256: plan.raw_sha256.clone(),
        source_kind: plan.plan.source_kind,
        source_id: plan.plan.source_id.clone(),
        source_sha256: plan.plan.source_sha256.clone(),
        formal_gate_eligible: false,
        formal_gate_blocker:
            "synthetic fixture proves tooling only; formal development audit is disabled"
                .to_string(),
        annotator_a_id: annotator_a.annotator_id.clone(),
        annotator_b_id: annotator_b.annotator_id.clone(),
        annotator_a_sha256: annotator_a.raw_sha256.clone(),
        annotator_b_sha256: annotator_b.raw_sha256.clone(),
        candidate_output_observed: false,
        tolerance_micros_inclusive: TOLERANCE_MICROS,
        thresholds,
        status,
        not_ready_reasons,
        metrics,
        gates,
    }
}

pub(crate) fn render_result(result: &AuditResult) -> Result<Vec<u8>, String> {
    let mut bytes = serde_json::to_vec_pretty(result)
        .map_err(|error| format!("cannot serialize audit result: {error}"))?;
    bytes.push(b'\n');
    Ok(bytes)
}

fn compare(predictions: &[Vec<i64>], references: &[Vec<i64>]) -> PairMetrics {
    debug_assert_eq!(predictions.len(), references.len());
    let prediction_count = predictions.iter().map(Vec::len).sum::<usize>();
    let reference_count = references.iter().map(Vec::len).sum::<usize>();
    let match_count = predictions
        .iter()
        .zip(references)
        .map(|(prediction, reference)| match_events(prediction, reference).len())
        .sum::<usize>();
    let precision = ratio(match_count, prediction_count, reference_count);
    let recall = ratio(match_count, reference_count, prediction_count);
    let f1 = if precision + recall == 0.0 {
        0.0
    } else {
        2.0 * precision * recall / (precision + recall)
    };
    PairMetrics {
        prediction_count,
        reference_count,
        match_count,
        precision,
        recall,
        f1,
    }
}

fn ratio(matches: usize, denominator: usize, other_count: usize) -> f64 {
    if denominator == 0 {
        if other_count == 0 {
            1.0
        } else {
            0.0
        }
    } else {
        matches as f64 / denominator as f64
    }
}

#[cfg(test)]
mod tests {
    use super::super::annotation::AnnotationArtifact;
    use super::super::manifest::SourceKind;
    use super::super::plan::{AuditItem, AuditPlan};
    use super::*;

    fn plan(proxy: Vec<i64>) -> PlanArtifact {
        let proxy = proxy.into_iter().map(|value| value as u64).collect();
        PlanArtifact {
            raw_sha256: "1".repeat(64),
            plan: AuditPlan {
                schema: PLAN_SCHEMA.to_string(),
                tool_version: TOOL_VERSION.to_string(),
                definition_sha256: definition_sha256(),
                profile: "DRUM".to_string(),
                purpose: super::super::contract::PURPOSE.to_string(),
                selection_seed: super::super::contract::SELECTION_SEED.to_string(),
                selection_algorithm:
                    "sha256-length-prefixed-unique-performance-one-render-exact-600s-v1".to_string(),
                source_kind: SourceKind::SyntheticFixture,
                source_id: "test".to_string(),
                source_sha256: "2".repeat(64),
                target_duration_micros: 600_000_000,
                selected_duration_micros: 600_000_000,
                candidate_output_observed: false,
                audio_opened_by_tool: false,
                test_or_fresh_holdout_permitted: false,
                two_mix_permitted: false,
                coordinator_only_contains_midi_proxy: true,
                items: vec![AuditItem {
                    item_id: "audit-001".to_string(),
                    selection_rank: 1,
                    fold: "0".to_string(),
                    drummer: "synthetic".to_string(),
                    performance_id: "synthetic".to_string(),
                    source_split: "synthetic".to_string(),
                    kit_name: "kit".to_string(),
                    audio_relative_path: "synthetic.wav".to_string(),
                    midi_sha256: "3".repeat(64),
                    source_duration_micros: 600_000_000,
                    segment_start_micros: 0,
                    segment_duration_micros: 600_000_000,
                    midi_proxy_onsets_micros: proxy,
                }],
            },
        }
    }

    fn annotation(id: &str, completion: Completion, events: Vec<i64>) -> AnnotationArtifact {
        AnnotationArtifact {
            annotator_id: id.to_string(),
            raw_sha256: if id == "A" { "a" } else { "b" }.repeat(64),
            completion,
            events_by_item: vec![events],
        }
    }

    #[test]
    fn pending_annotations_are_not_ready_without_metrics() {
        let proxy = plan(vec![1_000_000]);
        let a = annotation("A", Completion::Pending, vec![]);
        let b = annotation("B", Completion::Complete, vec![1_000_000]);
        let result = score(&proxy, &a, &b);
        assert_eq!(result.status, "synthetic_fixture_not_ready");
        assert!(!result.formal_gate_eligible);
        assert_eq!(result.source_kind, SourceKind::SyntheticFixture);
        assert!(result.metrics.is_none() && result.gates.is_none());
    }

    #[test]
    fn exact_threshold_is_inclusive_and_below_threshold_fails() {
        let proxy_events = (0..20).map(|index| 100_000 * index).collect::<Vec<_>>();
        let proxy = plan(proxy_events.clone());
        let nineteen = proxy_events[..19].to_vec();
        let a = annotation("A", Completion::Complete, nineteen.clone());
        let b = annotation("B", Completion::Complete, nineteen);
        assert_eq!(score(&proxy, &a, &b).status, "synthetic_fixture_pass");

        let eighteen = proxy_events[..18].to_vec();
        let a = annotation("A", Completion::Complete, eighteen.clone());
        let b = annotation("B", Completion::Complete, eighteen);
        assert_eq!(score(&proxy, &a, &b).status, "synthetic_fixture_fail");
    }

    #[test]
    fn all_three_comparisons_use_inclusive_twenty_five_ms_matching() {
        let proxy = plan(vec![1_000_000, 2_000_000]);
        let a = annotation("A", Completion::Complete, vec![1_025_000, 2_000_000]);
        let b = annotation("B", Completion::Complete, vec![1_000_000, 2_025_000]);
        assert_eq!(score(&proxy, &a, &b).status, "synthetic_fixture_pass");
    }
}
