use std::collections::{BTreeSet, HashSet};

use serde::Serialize;

use crate::metadata::Performance;

pub(crate) const MIN_PERFORMANCE_IDS: usize = 60;
pub(crate) const MIN_DURATION_SECS: f64 = 1_800.0;
pub(crate) const MIN_BEAT_FILL_RATIO: f64 = 0.30;
pub(crate) const MIN_STYLES: usize = 8;
pub(crate) const MIN_KITS: usize = 8;
pub(crate) const MIN_KICK_ONLY_EVENTS: usize = 200;
pub(crate) const MIN_HAT_ONLY_EVENTS: usize = 200;
pub(crate) const REQUIRED_OPENED_VALIDATION_IDS: usize = 6;

const STAGE1_WINDOWS: [u16; 2] = [1_024, 2_048];
const BANDS_PER_OCTAVE: [u8; 2] = [12, 24];
const MAX_FILTER_RADII: [u8; 2] = [0, 1];
const REFERENCE_DBFS: [i8; 4] = [-80, -70, -60, -50];
const SUPERFLUX_DELTAS_MICRO: [u32; 7] = [6_250, 8_000, 12_500, 25_000, 50_000, 100_000, 200_000];
const SUPERFLUX_FLOORS_MICRO: [u32; 6] = [0, 25_000, 50_000, 100_000, 200_000, 400_000];
const STAGE2_PRE_MAX: [u8; 2] = [3, 6];
const STAGE2_POST: [u8; 3] = [0, 1, 2];
const STAGE2_PRE_AVG: [u8; 3] = [12, 19, 24];
const MEL_DELTAS_MILLI: [u16; 6] = [250, 500, 750, 1_000, 1_500, 2_000];
const MEL_FLOORS_MILLI: [u16; 6] = [0, 500, 1_000, 1_500, 2_000, 3_000];

#[derive(Clone, Debug, Serialize)]
pub(crate) struct Deficit {
    pub(crate) metric: String,
    pub(crate) actual: String,
    pub(crate) required: String,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct DevelopmentAssessment {
    pub(crate) status: String,
    pub(crate) winner_allowed: bool,
    pub(crate) unique_performance_ids: usize,
    pub(crate) unique_duration_secs: f64,
    pub(crate) beat_ids: usize,
    pub(crate) fill_ids: usize,
    pub(crate) beat_ratio: f64,
    pub(crate) fill_ratio: f64,
    pub(crate) styles: usize,
    pub(crate) kits: usize,
    pub(crate) drummers: usize,
    pub(crate) required_drummers: usize,
    pub(crate) kick_only_events: usize,
    pub(crate) hat_only_events: usize,
    pub(crate) forced_opened_validation_ids: usize,
    pub(crate) deficits: Vec<Deficit>,
}

impl DevelopmentAssessment {
    pub(crate) fn status_code(&self) -> &str {
        &self.status
    }

    pub(crate) fn ready(&self) -> bool {
        self.deficits.is_empty()
    }
}

pub(crate) fn assess_development(
    selected: &[Performance],
    required_drummers: &BTreeSet<String>,
) -> DevelopmentAssessment {
    let unique_ids = selected
        .iter()
        .map(|item| item.row.id.as_str())
        .collect::<HashSet<_>>();
    let unique_performance_ids = unique_ids.len();
    let unique_duration_secs = selected.iter().map(|item| item.row.duration).sum::<f64>();
    let beat_ids = selected
        .iter()
        .filter(|item| item.row.beat_type == "beat")
        .count();
    let fill_ids = selected
        .iter()
        .filter(|item| item.row.beat_type == "fill")
        .count();
    let denominator = unique_performance_ids.max(1) as f64;
    let beat_ratio = beat_ids as f64 / denominator;
    let fill_ratio = fill_ids as f64 / denominator;
    let styles = selected
        .iter()
        .map(|item| item.row.primary_style())
        .collect::<HashSet<_>>()
        .len();
    let kits = selected
        .iter()
        .map(|item| item.row.kit_name.as_str())
        .collect::<HashSet<_>>()
        .len();
    let selected_drummers = selected
        .iter()
        .map(|item| item.row.drummer.as_str())
        .collect::<BTreeSet<_>>();
    let missing_drummers = required_drummers
        .iter()
        .filter(|drummer| !selected_drummers.contains(drummer.as_str()))
        .cloned()
        .collect::<Vec<_>>();
    let kick_only_events = selected.iter().map(|item| item.midi.kick_only_events).sum();
    let hat_only_events = selected.iter().map(|item| item.midi.hat_only_events).sum();
    let forced_opened_validation_ids = selected
        .iter()
        .filter(|item| item.forced_opened_validation)
        .count();
    let mut deficits = Vec::new();
    deficit_usize(
        &mut deficits,
        "unique_performance_ids",
        unique_performance_ids,
        MIN_PERFORMANCE_IDS,
    );
    if unique_duration_secs + 1e-9 < MIN_DURATION_SECS {
        deficits.push(Deficit {
            metric: "unique_duration_secs".into(),
            actual: format!("{unique_duration_secs:.9}"),
            required: format!(">={MIN_DURATION_SECS:.1}"),
        });
    }
    deficit_ratio(&mut deficits, "beat_ratio", beat_ratio);
    deficit_ratio(&mut deficits, "fill_ratio", fill_ratio);
    deficit_usize(&mut deficits, "styles", styles, MIN_STYLES);
    deficit_usize(&mut deficits, "kits", kits, MIN_KITS);
    if !missing_drummers.is_empty() {
        deficits.push(Deficit {
            metric: "all_available_drummers".into(),
            actual: format!("missing={}", missing_drummers.join("|")),
            required: format!("{} fixed pool drummers", required_drummers.len()),
        });
    }
    deficit_usize(
        &mut deficits,
        "kick_only_events",
        kick_only_events,
        MIN_KICK_ONLY_EVENTS,
    );
    deficit_usize(
        &mut deficits,
        "hat_only_events",
        hat_only_events,
        MIN_HAT_ONLY_EVENTS,
    );
    deficit_usize(
        &mut deficits,
        "forced_opened_validation_ids",
        forced_opened_validation_ids,
        REQUIRED_OPENED_VALIDATION_IDS,
    );
    if unique_performance_ids != selected.len() {
        deficits.push(Deficit {
            metric: "performance_id_grouping".into(),
            actual: format!("{} rows / {unique_performance_ids} IDs", selected.len()),
            required: "one selected render per unique ID".into(),
        });
    }
    if selected
        .iter()
        .any(|item| !matches!(item.row.split.as_str(), "train" | "validation"))
    {
        deficits.push(Deficit {
            metric: "split_isolation".into(),
            actual: "forbidden split present".into(),
            required: "train|validation only".into(),
        });
    }
    let status = if deficits.is_empty() {
        "development_midi_quotas_ready"
    } else {
        "insufficient_development_data"
    };
    DevelopmentAssessment {
        status: status.into(),
        winner_allowed: false,
        unique_performance_ids,
        unique_duration_secs,
        beat_ids,
        fill_ids,
        beat_ratio,
        fill_ratio,
        styles,
        kits,
        drummers: selected_drummers.len(),
        required_drummers: required_drummers.len(),
        kick_only_events,
        hat_only_events,
        forced_opened_validation_ids,
        deficits,
    }
}

pub(crate) fn candidate_evaluation_gate(assessment: &DevelopmentAssessment) -> Result<(), String> {
    if !assessment.ready() {
        return Err("insufficient_development_data: winner selection forbidden".into());
    }
    Err("candidate evaluation forbidden; verified typed prerequisite receipt chain is not implemented in B-549".into())
}

pub(crate) fn stage1_grid_count() -> usize {
    STAGE1_WINDOWS.len()
        * BANDS_PER_OCTAVE.len()
        * MAX_FILTER_RADII.len()
        * REFERENCE_DBFS.len()
        * SUPERFLUX_DELTAS_MICRO.len()
        * SUPERFLUX_FLOORS_MICRO.len()
}

pub(crate) fn stage2_grid_count() -> usize {
    STAGE2_PRE_MAX.len()
        * STAGE2_POST.len()
        * STAGE2_PRE_AVG.len()
        * SUPERFLUX_DELTAS_MICRO.len()
        * SUPERFLUX_FLOORS_MICRO.len()
}

pub(crate) fn mel32_grid_count() -> usize {
    MEL_DELTAS_MILLI.len() * MEL_FLOORS_MILLI.len()
}

pub(crate) fn mel32_stage2_grid_count() -> usize {
    STAGE2_PRE_MAX.len()
        * STAGE2_POST.len()
        * STAGE2_PRE_AVG.len()
        * MEL_DELTAS_MILLI.len()
        * MEL_FLOORS_MILLI.len()
}

#[derive(Clone, Debug)]
pub(crate) struct CandidateGateSummary {
    pub(crate) candidate_id: String,
    pub(crate) evaluable: bool,
    pub(crate) normalized_gate_margins: Vec<f64>,
    pub(crate) false_positives_per_second: f64,
    pub(crate) timing_p95_ms: f64,
    pub(crate) worker_micros_per_frame: Option<f64>,
}

pub(crate) fn select_gate_margin_winner(
    candidates: &[CandidateGateSummary],
) -> Result<String, String> {
    let mut eligible = candidates
        .iter()
        .filter_map(|candidate| {
            let worst = candidate
                .normalized_gate_margins
                .iter()
                .copied()
                .reduce(f64::min)?;
            let finite = worst.is_finite()
                && candidate.false_positives_per_second.is_finite()
                && candidate.timing_p95_ms.is_finite();
            (candidate.evaluable && finite && worst >= 0.0).then_some((candidate, worst))
        })
        .collect::<Vec<_>>();
    if eligible.is_empty() {
        return Err("no_eligible_candidate: winner selection forbidden".to_string());
    }
    eligible.sort_by(|(left, left_worst), (right, right_worst)| {
        right_worst
            .total_cmp(left_worst)
            .then_with(|| {
                left.false_positives_per_second
                    .total_cmp(&right.false_positives_per_second)
            })
            .then_with(|| left.timing_p95_ms.total_cmp(&right.timing_p95_ms))
    });
    let (first, first_worst) = eligible[0];
    let tied = eligible
        .iter()
        .take_while(|(candidate, worst)| {
            worst.to_bits() == first_worst.to_bits()
                && candidate.false_positives_per_second.to_bits()
                    == first.false_positives_per_second.to_bits()
                && candidate.timing_p95_ms.to_bits() == first.timing_p95_ms.to_bits()
        })
        .map(|(candidate, _)| *candidate)
        .collect::<Vec<_>>();
    if tied.len() > 1 {
        if tied
            .iter()
            .any(|candidate| candidate.worker_micros_per_frame.is_none())
        {
            return Err("runtime_tie_break_missing: winner selection forbidden".to_string());
        }
        if tied.iter().any(|candidate| {
            !candidate
                .worker_micros_per_frame
                .is_some_and(f64::is_finite)
        }) {
            return Err("runtime_tie_break_invalid: winner selection forbidden".to_string());
        }
        return Ok(tied
            .into_iter()
            .min_by(|left, right| {
                left.worker_micros_per_frame
                    .unwrap()
                    .total_cmp(&right.worker_micros_per_frame.unwrap())
                    .then_with(|| left.candidate_id.cmp(&right.candidate_id))
            })
            .unwrap()
            .candidate_id
            .clone());
    }
    Ok(first.candidate_id.clone())
}

fn deficit_usize(deficits: &mut Vec<Deficit>, metric: &str, actual: usize, required: usize) {
    if actual < required {
        deficits.push(Deficit {
            metric: metric.into(),
            actual: actual.to_string(),
            required: format!(">={required}"),
        });
    }
}

fn deficit_ratio(deficits: &mut Vec<Deficit>, metric: &str, actual: f64) {
    if actual + 1e-12 < MIN_BEAT_FILL_RATIO {
        deficits.push(Deficit {
            metric: metric.into(),
            actual: format!("{actual:.9}"),
            required: format!(">={MIN_BEAT_FILL_RATIO:.2}"),
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preregistered_grid_counts_are_exact() {
        assert_eq!(stage1_grid_count(), 1_344);
        assert_eq!(stage2_grid_count(), 756);
        assert_eq!(mel32_grid_count(), 36);
        assert_eq!(mel32_stage2_grid_count(), 648);
    }

    #[test]
    fn insufficient_data_forbids_candidate_evaluation_and_winner() {
        let assessment = assess_development(&[], &BTreeSet::from(["drummer1".into()]));
        assert_eq!(assessment.status, "insufficient_development_data");
        assert!(!assessment.winner_allowed);
        assert!(candidate_evaluation_gate(&assessment)
            .unwrap_err()
            .contains("winner selection forbidden"));
    }

    #[test]
    fn midi_quota_readiness_cannot_bypass_the_typed_receipt_chain() {
        let assessment = DevelopmentAssessment {
            status: "development_midi_quotas_ready".into(),
            winner_allowed: false,
            unique_performance_ids: 60,
            unique_duration_secs: 1_800.0,
            beat_ids: 30,
            fill_ids: 30,
            beat_ratio: 0.5,
            fill_ratio: 0.5,
            styles: 8,
            kits: 8,
            drummers: 9,
            required_drummers: 9,
            kick_only_events: 200,
            hat_only_events: 200,
            forced_opened_validation_ids: 6,
            deficits: Vec::new(),
        };
        assert!(candidate_evaluation_gate(&assessment)
            .unwrap_err()
            .contains("typed prerequisite receipt chain"));
    }

    #[test]
    fn zero_margin_is_eligible_and_nonevaluable_candidate_is_ignored() {
        let candidates = [
            candidate("target", true, 0.0, 1.0, 15.0, Some(2.0)),
            candidate("not-evaluable", false, 10.0, 0.0, 0.0, Some(0.0)),
            candidate("below", true, -0.001, 0.0, 0.0, Some(0.0)),
        ];
        assert_eq!(select_gate_margin_winner(&candidates).unwrap(), "target");
    }

    #[test]
    fn missing_runtime_for_exact_tie_forbids_winner() {
        let candidates = [
            candidate("a", true, 0.1, 0.5, 5.0, None),
            candidate("b", true, 0.1, 0.5, 5.0, Some(1.0)),
        ];
        assert!(select_gate_margin_winner(&candidates)
            .unwrap_err()
            .contains("runtime_tie_break_missing"));
    }

    fn candidate(
        id: &str,
        evaluable: bool,
        margin: f64,
        fp: f64,
        timing: f64,
        runtime: Option<f64>,
    ) -> CandidateGateSummary {
        CandidateGateSummary {
            candidate_id: id.into(),
            evaluable,
            normalized_gate_margins: vec![margin, margin + 1.0],
            false_positives_per_second: fp,
            timing_p95_ms: timing,
            worker_micros_per_frame: runtime,
        }
    }
}
