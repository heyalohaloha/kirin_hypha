use std::collections::{BTreeSet, HashSet};

use serde::Serialize;

use crate::contract::TARGET_PERFORMANCE_IDS;
use crate::drum_excerpt::EXCERPT_SAMPLE_RATE;
use crate::folds::{FOLD_COUNT, MIN_HAT_EVENTS_PER_FOLD, MIN_KICK_EVENTS_PER_FOLD};
use crate::metadata::Performance;

pub(crate) const MIN_PERFORMANCE_IDS: usize = TARGET_PERFORMANCE_IDS;
pub(crate) const MIN_DURATION_SECS: f64 = 1_800.0;
pub(crate) const MIN_DURATION_SAMPLES_44100: u64 = 1_800 * EXCERPT_SAMPLE_RATE as u64;
pub(crate) const MIN_BEAT_FILL_RATIO: f64 = 0.30;
pub(crate) const MIN_STYLES: usize = 8;
pub(crate) const MIN_KITS: usize = 8;
pub(crate) const MIN_KICK_ONLY_EVENTS: usize =
    MIN_KICK_EVENTS_PER_FOLD as usize * FOLD_COUNT as usize;
pub(crate) const MIN_HAT_ONLY_EVENTS: usize =
    MIN_HAT_EVENTS_PER_FOLD as usize * FOLD_COUNT as usize;
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
    pub(crate) unique_duration_samples_44100: u64,
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
    let unique_duration_samples_44100 = selected
        .iter()
        .map(|item| item.excerpt_end_sample_44100() - item.excerpt_start_sample_44100())
        .sum::<u64>();
    let unique_duration_secs =
        unique_duration_samples_44100 as f64 / f64::from(EXCERPT_SAMPLE_RATE);
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
    if unique_duration_samples_44100 < MIN_DURATION_SAMPLES_44100 {
        deficits.push(Deficit {
            metric: "unique_duration_samples_44100".into(),
            actual: unique_duration_samples_44100.to_string(),
            required: format!(">={MIN_DURATION_SAMPLES_44100}"),
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
        unique_duration_samples_44100,
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
    Err("candidate evaluation forbidden; sealed CandidateSetReceipt and evaluator-owned summary are not implemented in B-550".into())
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
    fn midi_quota_readiness_cannot_construct_a_winner_summary() {
        let assessment = DevelopmentAssessment {
            status: "development_midi_quotas_ready".into(),
            winner_allowed: false,
            unique_performance_ids: 290,
            unique_duration_samples_44100: MIN_DURATION_SAMPLES_44100,
            unique_duration_secs: 1_800.0,
            beat_ids: 30,
            fill_ids: 30,
            beat_ratio: 0.5,
            fill_ratio: 0.5,
            styles: 8,
            kits: 8,
            drummers: 9,
            required_drummers: 9,
            kick_only_events: MIN_KICK_ONLY_EVENTS,
            hat_only_events: MIN_HAT_ONLY_EVENTS,
            forced_opened_validation_ids: 6,
            deficits: Vec::new(),
        };
        assert!(candidate_evaluation_gate(&assessment)
            .unwrap_err()
            .contains("sealed CandidateSetReceipt"));
    }
}
