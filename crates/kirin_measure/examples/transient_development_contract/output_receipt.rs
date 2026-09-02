use std::collections::BTreeSet;

use serde::Serialize;

use crate::contract::{
    sha256_bytes, InputIdentities, LEGACY_BASELINE_ID_LIST_SHA256, LEGACY_BASELINE_PERFORMANCE_IDS,
    LOWER_BOUND_IMPOSSIBLE_PERFORMANCE_IDS, MAX_TARGET_PERFORMANCE_IDS, RENDER_KEY_VERSION,
    SELECTION_SEED, SELECTION_VERSION, TARGET_PERFORMANCE_IDS, TARGET_SEARCH_START_PERFORMANCE_IDS,
    TARGET_SEARCH_STEP,
};
use crate::drum_excerpt::{
    EXCERPT_CAP_SAMPLES, EXCERPT_MAPPING_VERSION, EXCERPT_SAMPLE_RATE,
    EXCERPT_START_QUANTUM_SAMPLES, EXCERPT_WINDOW_DOMAIN, EXCERPT_WINDOW_SEED, RANK_KEY_DOMAIN,
    RANK_KEY_SEED, RANK_KEY_VERSION,
};
use crate::folds::{
    FoldPlan, BEST_SWAP_PASSES, FOLD_ASSIGNMENT_SEED, FOLD_ASSIGNMENT_VERSION, FOLD_COUNT,
    RANDOM_SWAP_ATTEMPTS, SEARCH_RESTARTS,
};
use crate::ledger::OpenedLedger;
use crate::metadata::{MetadataStats, Performance};
use crate::output_csv::{fold_balance, FoldBalance, FOLDS_NAME, MANIFEST_NAME};
use crate::output_stats::{
    duration_sample_binding, sampling_audit, source_diagnostic, SamplingAudit, SourceMidiDiagnostic,
};
use crate::policy::{
    mel32_grid_count, mel32_stage2_grid_count, stage1_grid_count, stage2_grid_count,
    DevelopmentAssessment, MIN_BEAT_FILL_RATIO, MIN_DURATION_SAMPLES_44100, MIN_DURATION_SECS,
    MIN_HAT_ONLY_EVENTS, MIN_KICK_ONLY_EVENTS, MIN_KITS, MIN_PERFORMANCE_IDS, MIN_STYLES,
};
use crate::selector::SelectionOutcome;

#[derive(Serialize)]
pub(crate) struct ArtifactPart {
    pub(crate) path: String,
    pub(crate) sha256: String,
    pub(crate) rows: usize,
}

#[derive(Serialize)]
struct Receipt<'a> {
    schema: &'static str,
    purpose: &'static str,
    profile: &'static str,
    legacy_v1_is_formal_input: bool,
    dataset: DatasetReceipt<'a>,
    selection: SelectionReceipt,
    excerpt: ExcerptReceipt,
    metadata: &'a MetadataStats,
    required_drummers: &'a BTreeSet<String>,
    assessment: &'a DevelopmentAssessment,
    policy: PolicyReceipt,
    source_midi_diagnostic: SourceMidiDiagnostic,
    folds: FoldReceipt<'a>,
    grid: GridReceipt,
    artifacts: ArtifactReceipt,
    this_selector_run: SelectorRunReceipt,
    blind_acoustic_audit: &'static str,
    candidate_evaluation_allowed: bool,
    winner_allowed: bool,
    winner_blockers: Vec<&'static str>,
}

#[derive(Serialize)]
struct DatasetReceipt<'a> {
    id: &'static str,
    version: &'static str,
    metadata_sha256: &'a str,
    midi_archive_sha256: &'a str,
    midi_archive_verified: bool,
    selected_midi_files_hashed: bool,
    midi_root_archive_provenance_verified: bool,
    expected_audio_archive_sha256: &'static str,
    audio_archive_verified: bool,
    audio_hash_duplicate_check: &'static str,
    opened_ledger_sha256: &'a str,
    test_isolation_incident_sha256: &'a str,
    original_fresh_holdout_isolation_breached: bool,
    opened_sources: &'a [crate::ledger::LedgerSource],
    opened_ledger_ids: usize,
    opened_validation_required_ids: usize,
    opened_test_diagnostic_only_ids: usize,
}

#[derive(Serialize)]
struct SelectionReceipt {
    algorithm_version: &'static str,
    seed: &'static str,
    rank_key_version: &'static str,
    rank_key_domain: &'static str,
    rank_key_seed: &'static str,
    rank_key_contract: &'static str,
    render_key_version: &'static str,
    render_key_contract: &'static str,
    legacy_baseline_performance_ids: usize,
    legacy_baseline_performance_id_list_sha256: &'static str,
    target_search_start_performance_ids: usize,
    target_search_step: usize,
    maximum_target_performance_ids: usize,
    fixed_target_performance_ids: usize,
    first_fixed_five_step_target_proven_possible: Option<usize>,
    duration_infeasible_through_performance_ids: usize,
    lower_bound_n170_excerpt_duration_samples_44100: u64,
    minimum_required_excerpt_duration_samples_44100: u64,
    lower_bound_dominant_hat_contributor_first_selection_rank: usize,
    lower_bound_hat_argument_applies_from_performance_ids: usize,
    lower_bound_impossible_performance_ids: usize,
    lower_bound_n285_hat_events: usize,
    lower_bound_n285_maximum_single_id_hat_events: usize,
    lower_bound_dominant_fold_minimum_hat_events: usize,
    lower_bound_other_fold_minimum_hat_events: usize,
    lower_bound_required_global_hat_events: usize,
    lower_bound_reason: &'static str,
    minimality_proof: String,
}

#[derive(Serialize)]
struct ExcerptReceipt {
    mapping_version: &'static str,
    hash_domain: &'static str,
    seed: &'static str,
    sample_rate: u32,
    maximum_window_samples: u64,
    start_quantum_samples: u64,
    interval: &'static str,
    mapping_formula: &'static str,
    note_inclusion_formula: &'static str,
    duration_conversion: &'static str,
    empty_excerpt_policy: &'static str,
    source_duration_sample_binding_contract: &'static str,
    source_duration_sample_binding_sha256: String,
    sampling_audit: SamplingAudit,
}

#[derive(Serialize)]
struct PolicyReceipt {
    minimum_performance_ids: usize,
    minimum_excerpt_duration_secs: f64,
    minimum_excerpt_duration_samples_44100: u64,
    minimum_beat_ratio: f64,
    minimum_fill_ratio: f64,
    minimum_primary_styles: usize,
    minimum_kits: usize,
    require_all_available_drummers: bool,
    minimum_global_kick_only_events_derived_from_folds: usize,
    minimum_global_hat_only_events_derived_from_folds: usize,
    one_render_per_performance_id: bool,
    primary_style_definition: &'static str,
    quota_counting_unit: &'static str,
}

#[derive(Serialize)]
struct FoldReceipt<'a> {
    count: u8,
    unit: &'static str,
    algorithm_version: &'static str,
    seed: &'static str,
    deterministic_restarts: u8,
    random_swap_attempts_per_restart: usize,
    best_swap_passes_per_restart: usize,
    categorical_features_are_objective_and_diagnostic_only: [&'static str; 8],
    lodo_groups: &'a BTreeSet<String>,
    loso_groups: &'a BTreeSet<String>,
    qualification: &'a crate::folds::FoldQualification,
    balance_audit: Vec<FoldBalance>,
}

#[derive(Serialize)]
struct GridReceipt {
    superflux_stage1: usize,
    superflux_stage2: usize,
    mel32_stage1: usize,
    mel32_stage2: usize,
}

#[derive(Serialize)]
struct ArtifactReceipt {
    manifest_path: &'static str,
    manifest_schema_columns: usize,
    manifest_sha256: String,
    manifest_rows: usize,
    folds_path: &'static str,
    folds_sha256: String,
    folds_rows: usize,
    exclusion_parts: Vec<ArtifactPart>,
    reserve_parts: Vec<ArtifactPart>,
}

#[derive(Serialize)]
struct SelectorRunReceipt {
    audio_opened: bool,
    test_midi_opened: bool,
    fresh_holdout_opened: bool,
    two_mix_opened: bool,
    candidate_scores_run: bool,
}

#[allow(clippy::too_many_arguments)]
pub(crate) fn render_receipt(
    selection: &SelectionOutcome,
    folds: &FoldPlan,
    metadata: &MetadataStats,
    required_drummers: &BTreeSet<String>,
    ledger: &OpenedLedger,
    identities: &InputIdentities,
    manifest: &[u8],
    folds_csv: &[u8],
    exclusion_parts: Vec<ArtifactPart>,
    reserve_parts: Vec<ArtifactPart>,
) -> Result<Vec<u8>, String> {
    let selected = &selection.selected;
    let mut winner_blockers = vec![
        "midi_archive_member_provenance_unverified",
        "audio_archive_not_verified",
        "audio_hash_duplicate_check_pending",
        "canonical_excerpt_label_duplicate_check_pending",
        "blind_acoustic_audit_required",
        "candidate_scores_not_run",
        "test_isolation_incident_requires_guardian_or_new_holdout",
        "formal_authorization_not_pinned_in_source_commit",
        "formal_receipt_semantic_verifiers_not_implemented",
        "not_ready_context_guard_unimplemented",
        "candidate_set_completion_receipt_not_implemented",
        "lodo_loso_diagnostic_results_not_ready",
    ];
    if !selection.assessment.ready() {
        winner_blockers.insert(0, "insufficient_development_data");
    }
    if !folds.qualification.qualified {
        winner_blockers.insert(0, "fold_balance_not_qualified");
    }
    let receipt = Receipt {
        schema: "kirin-hypha-attack-drum-development-receipt-v2",
        purpose: "development-selection",
        profile: "DRUM",
        legacy_v1_is_formal_input: false,
        dataset: dataset_receipt(ledger, identities),
        selection: selection_receipt(selected, folds.qualification.qualified)?,
        excerpt: excerpt_receipt(selected),
        metadata,
        required_drummers,
        assessment: &selection.assessment,
        policy: policy_receipt(),
        source_midi_diagnostic: source_diagnostic(selected),
        folds: FoldReceipt {
            count: FOLD_COUNT,
            unit: "performance_id",
            algorithm_version: FOLD_ASSIGNMENT_VERSION,
            seed: FOLD_ASSIGNMENT_SEED,
            deterministic_restarts: SEARCH_RESTARTS,
            random_swap_attempts_per_restart: RANDOM_SWAP_ATTEMPTS,
            best_swap_passes_per_restart: BEST_SWAP_PASSES,
            categorical_features_are_objective_and_diagnostic_only: [
                "drummer",
                "session",
                "kit",
                "primary_style",
                "tempo_20_bpm_bin",
                "density_0.5_event_per_second_bin",
                "split",
                "forced_opened_validation",
            ],
            lodo_groups: &folds.lodo_groups,
            loso_groups: &folds.loso_groups,
            qualification: &folds.qualification,
            balance_audit: fold_balance(selected, folds)?,
        },
        grid: GridReceipt {
            superflux_stage1: stage1_grid_count(),
            superflux_stage2: stage2_grid_count(),
            mel32_stage1: mel32_grid_count(),
            mel32_stage2: mel32_stage2_grid_count(),
        },
        artifacts: ArtifactReceipt {
            manifest_path: MANIFEST_NAME,
            manifest_schema_columns: 23,
            manifest_sha256: sha256_bytes(manifest),
            manifest_rows: selected.len(),
            folds_path: FOLDS_NAME,
            folds_sha256: sha256_bytes(folds_csv),
            folds_rows: folds.by_performance_id.len(),
            exclusion_parts,
            reserve_parts,
        },
        this_selector_run: SelectorRunReceipt {
            audio_opened: false,
            test_midi_opened: false,
            fresh_holdout_opened: false,
            two_mix_opened: false,
            candidate_scores_run: false,
        },
        blind_acoustic_audit: "not_attached",
        candidate_evaluation_allowed: false,
        winner_allowed: false,
        winner_blockers,
    };
    let mut bytes = serde_json::to_vec_pretty(&receipt)
        .map_err(|error| format!("cannot serialize development receipt: {error}"))?;
    bytes.push(b'\n');
    Ok(bytes)
}

fn dataset_receipt<'a>(
    ledger: &'a OpenedLedger,
    identities: &'a InputIdentities,
) -> DatasetReceipt<'a> {
    DatasetReceipt {
        id: "E-GMD",
        version: "1.0.0",
        metadata_sha256: &identities.metadata_sha256,
        midi_archive_sha256: &identities.midi_archive_sha256,
        midi_archive_verified: true,
        selected_midi_files_hashed: true,
        midi_root_archive_provenance_verified: false,
        expected_audio_archive_sha256:
            "7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053",
        audio_archive_verified: false,
        audio_hash_duplicate_check: "pending_until_audio_ingest",
        opened_ledger_sha256: &ledger.sha256,
        test_isolation_incident_sha256: &ledger.isolation_incident_sha256,
        original_fresh_holdout_isolation_breached: ledger.fresh_holdout_isolation_breached,
        opened_sources: &ledger.sources,
        opened_ledger_ids: ledger.len(),
        opened_validation_required_ids: ledger
            .entries
            .iter()
            .filter(|entry| entry.source_split == crate::ledger::SourceSplit::Validation)
            .count(),
        opened_test_diagnostic_only_ids: ledger
            .entries
            .iter()
            .filter(|entry| entry.source_split == crate::ledger::SourceSplit::Test)
            .count(),
    }
}

fn selection_receipt(
    selected: &[Performance],
    chosen_assignment_qualified: bool,
) -> Result<SelectionReceipt, String> {
    let mut baseline = selected
        .iter()
        .take(LEGACY_BASELINE_PERFORMANCE_IDS)
        .map(|item| item.row.id.as_str())
        .collect::<Vec<_>>()
        .join("\n");
    baseline.push('\n');
    if sha256_bytes(baseline.as_bytes()) != LEGACY_BASELINE_ID_LIST_SHA256 {
        return Err("legacy v1 60-ID baseline changed".to_string());
    }
    let lower_prefix = selected
        .iter()
        .take(LOWER_BOUND_IMPOSSIBLE_PERFORMANCE_IDS)
        .collect::<Vec<_>>();
    if lower_prefix.len() != LOWER_BOUND_IMPOSSIBLE_PERFORMANCE_IDS {
        return Err("fixed selection cannot establish the N285 lower bound".to_string());
    }
    let n285_hat_events = lower_prefix
        .iter()
        .map(|item| item.midi.hat_only_events)
        .sum::<usize>();
    let maximum_single_id = lower_prefix
        .iter()
        .map(|item| item.midi.hat_only_events)
        .max()
        .unwrap_or(0);
    let dominant_fold_minimum = maximum_single_id
        .checked_mul(4)
        .ok_or("N285 hat lower-bound overflow")?;
    let other_fold_minimum = dominant_fold_minimum
        .checked_mul(2)
        .and_then(|value| value.checked_add(2))
        .ok_or("N285 hat ratio lower-bound overflow")?
        / 3;
    let required_global = dominant_fold_minimum
        .checked_add(
            other_fold_minimum
                .checked_mul(4)
                .ok_or("N285 global hat lower-bound overflow")?,
        )
        .ok_or("N285 global hat lower-bound overflow")?;
    if n285_hat_events >= required_global {
        return Err("N285 derived hat lower bound no longer proves impossibility".to_string());
    }
    let dominant_first_rank = selected
        .iter()
        .position(|item| item.midi.hat_only_events == maximum_single_id)
        .map(|index| index + 1)
        .ok_or("dominant N285 hat contributor is missing")?;
    if dominant_first_rank > TARGET_SEARCH_START_PERFORMANCE_IDS {
        return Err("dominant N285 hat contributor enters after the search start".to_string());
    }
    let duration_n170 = selected
        .iter()
        .take(TARGET_SEARCH_START_PERFORMANCE_IDS - TARGET_SEARCH_STEP)
        .map(|item| item.excerpt_end_sample_44100() - item.excerpt_start_sample_44100())
        .sum::<u64>();
    let minimum_duration_samples = MIN_DURATION_SAMPLES_44100;
    if duration_n170 >= minimum_duration_samples {
        return Err("N170 no longer proves the excerpt-duration lower bound".to_string());
    }
    Ok(SelectionReceipt {
        algorithm_version: SELECTION_VERSION,
        seed: SELECTION_SEED,
        rank_key_version: RANK_KEY_VERSION,
        rank_key_domain: RANK_KEY_DOMAIN,
        rank_key_seed: RANK_KEY_SEED,
        rank_key_contract:
            "SHA-256(u64be_len||domain + u64be_len||field for version,seed,split,performance_id)",
        render_key_version: RENDER_KEY_VERSION,
        render_key_contract:
            "independent domain; same length-prefixed encoding plus kit_name; render choice only",
        legacy_baseline_performance_ids: LEGACY_BASELINE_PERFORMANCE_IDS,
        legacy_baseline_performance_id_list_sha256: LEGACY_BASELINE_ID_LIST_SHA256,
        target_search_start_performance_ids: TARGET_SEARCH_START_PERFORMANCE_IDS,
        target_search_step: TARGET_SEARCH_STEP,
        maximum_target_performance_ids: MAX_TARGET_PERFORMANCE_IDS,
        fixed_target_performance_ids: TARGET_PERFORMANCE_IDS,
        first_fixed_five_step_target_proven_possible: chosen_assignment_qualified
            .then_some(TARGET_PERFORMANCE_IDS),
        duration_infeasible_through_performance_ids: TARGET_SEARCH_START_PERFORMANCE_IDS
            - TARGET_SEARCH_STEP,
        lower_bound_n170_excerpt_duration_samples_44100: duration_n170,
        minimum_required_excerpt_duration_samples_44100: minimum_duration_samples,
        lower_bound_dominant_hat_contributor_first_selection_rank: dominant_first_rank,
        lower_bound_hat_argument_applies_from_performance_ids:
            TARGET_SEARCH_START_PERFORMANCE_IDS,
        lower_bound_impossible_performance_ids: LOWER_BOUND_IMPOSSIBLE_PERFORMANCE_IDS,
        lower_bound_n285_hat_events: n285_hat_events,
        lower_bound_n285_maximum_single_id_hat_events: maximum_single_id,
        lower_bound_dominant_fold_minimum_hat_events: dominant_fold_minimum,
        lower_bound_other_fold_minimum_hat_events: other_fold_minimum,
        lower_bound_required_global_hat_events: required_global,
        lower_bound_reason:
            "<=25% per-ID share makes the dominant fold at least 4*max_ID; max/min<=3/2 makes every other fold at least ceil(2*dominant/3)",
        minimality_proof: if chosen_assignment_qualified {
            "prefixes through N170 fail the monotone 1800-second excerpt gate; the dominant hat contributor enters before N175, so every five-step prefix N175..N285 has the same maximum and no more hats than impossible N285; the in-repo N290 assignment passes every frozen hard gate"
        } else {
            "N170 and N175..N285 lower bounds hold, but the in-repo N290 assignment is not qualified; no first possible target is established"
        }
        .to_string(),
    })
}

fn excerpt_receipt(selected: &[Performance]) -> ExcerptReceipt {
    ExcerptReceipt {
        mapping_version: EXCERPT_MAPPING_VERSION,
        hash_domain: EXCERPT_WINDOW_DOMAIN,
        seed: EXCERPT_WINDOW_SEED,
        sample_rate: EXCERPT_SAMPLE_RATE,
        maximum_window_samples: EXCERPT_CAP_SAMPLES,
        start_quantum_samples: EXCERPT_START_QUANTUM_SAMPLES,
        interval: "half-open [start_sample,end_sample)",
        mapping_formula:
            "position_count=floor((source_samples-1323000)/441)+1; q=floor(hash_u64*position_count/2^64) by u128 multiply-high; start=q*441; end=start+1323000",
        note_inclusion_formula:
            "start_sample*1000000 <= absolute_note_micros*44100 < end_sample*1000000 using u128",
        duration_conversion:
            "exact metadata decimal seconds * 44100 rounded half-up; f64 forbidden",
        empty_excerpt_policy:
            "valid negative interval retained; raw/event counts may be zero; evidence gates remain fail-closed",
        source_duration_sample_binding_contract:
            "per selected row in rank order: u64be_len+selection_rank_decimal, u64be_len+id, u64be_len+source_duration_decimal, u64be_source_samples",
        source_duration_sample_binding_sha256: duration_sample_binding(selected),
        sampling_audit: sampling_audit(selected),
    }
}

fn policy_receipt() -> PolicyReceipt {
    PolicyReceipt {
        minimum_performance_ids: MIN_PERFORMANCE_IDS,
        minimum_excerpt_duration_secs: MIN_DURATION_SECS,
        minimum_excerpt_duration_samples_44100: MIN_DURATION_SAMPLES_44100,
        minimum_beat_ratio: MIN_BEAT_FILL_RATIO,
        minimum_fill_ratio: MIN_BEAT_FILL_RATIO,
        minimum_primary_styles: MIN_STYLES,
        minimum_kits: MIN_KITS,
        require_all_available_drummers: true,
        minimum_global_kick_only_events_derived_from_folds: MIN_KICK_ONLY_EVENTS,
        minimum_global_hat_only_events_derived_from_folds: MIN_HAT_ONLY_EVENTS,
        one_render_per_performance_id: true,
        primary_style_definition: "substring before first slash",
        quota_counting_unit: "unique performance ID; selected kit render contributes once",
    }
}
