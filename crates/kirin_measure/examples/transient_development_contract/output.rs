use std::collections::BTreeSet;

use serde::Serialize;

use crate::contract::{sha256_bytes, InputIdentities, SELECTION_SEED, SELECTION_VERSION};
use crate::csv::encode_csv_field;
use crate::folds::{FoldPlan, FOLD_COUNT};
use crate::ledger::OpenedLedger;
use crate::metadata::{MetadataStats, Performance, PreflightExclusion};
use crate::policy::{
    mel32_grid_count, mel32_stage2_grid_count, stage1_grid_count, stage2_grid_count,
    DevelopmentAssessment, MIN_BEAT_FILL_RATIO, MIN_DURATION_SECS, MIN_HAT_ONLY_EVENTS,
    MIN_KICK_ONLY_EVENTS, MIN_KITS, MIN_PERFORMANCE_IDS, MIN_STYLES,
};
use crate::selector::SelectionOutcome;

const MANIFEST_NAME: &str = "attack_drum_development_manifest_v1.csv";
const FOLDS_NAME: &str = "attack_drum_development_folds_v1.csv";
const RECEIPT_NAME: &str = "attack_drum_development_receipt_v1.json";
const RESERVE_ROWS_PER_SHARD: usize = 400;
const MANIFEST_HEADER: &str = "selection_rank,selection_key,fold,drummer,session,id,style,bpm,beat_type,time_signature,duration,split,midi_filename,audio_filename,kit_name,midi_sha256,raw_notes,compound_events,kick_only_events,hat_only_events,density_events_per_second";

pub(crate) struct ArtifactFile {
    pub(crate) name: String,
    pub(crate) bytes: Vec<u8>,
}

pub(crate) struct Artifacts {
    pub(crate) files: Vec<ArtifactFile>,
}

#[derive(Serialize)]
struct Receipt<'a> {
    schema: &'static str,
    purpose: &'static str,
    profile: &'static str,
    dataset_id: &'static str,
    dataset_version: &'static str,
    metadata_sha256: &'a str,
    midi_archive_sha256: &'a str,
    midi_archive_verified: bool,
    selected_midi_files_hashed: bool,
    midi_root_archive_provenance_verified: bool,
    audio_hash_duplicate_check: &'static str,
    expected_audio_archive_sha256: &'static str,
    audio_archive_verified: bool,
    opened_ledger_sha256: &'a str,
    test_isolation_incident_sha256: &'a str,
    original_fresh_holdout_isolation_breached: bool,
    opened_sources: &'a [crate::ledger::LedgerSource],
    opened_ledger_ids: usize,
    opened_validation_required_ids: usize,
    opened_test_diagnostic_only_ids: usize,
    selection_algorithm_version: &'static str,
    seed: &'static str,
    rank_hash_contract: &'static str,
    render_hash_contract: &'static str,
    metadata: &'a MetadataStats,
    required_drummers: &'a BTreeSet<String>,
    assessment: &'a DevelopmentAssessment,
    policy: PolicyReceipt,
    folds: FoldReceipt<'a>,
    grid: GridReceipt,
    artifacts: ArtifactReceipt,
    this_selector_run_audio_opened: bool,
    this_selector_run_test_midi_opened: bool,
    this_selector_run_fresh_holdout_opened: bool,
    this_selector_run_two_mix_opened: bool,
    this_selector_run_candidate_scores_run: bool,
    blind_acoustic_audit: &'static str,
    candidate_evaluation_allowed: bool,
    winner_allowed: bool,
    winner_blockers: Vec<&'static str>,
}

#[derive(Serialize)]
struct PolicyReceipt {
    minimum_performance_ids: usize,
    minimum_unique_duration_secs: f64,
    minimum_beat_ratio: f64,
    minimum_fill_ratio: f64,
    minimum_primary_styles: usize,
    minimum_kits: usize,
    require_all_available_drummers: bool,
    minimum_kick_only_events: usize,
    minimum_hat_only_events: usize,
    one_render_per_performance_id: bool,
    primary_style_definition: &'static str,
    quota_counting_unit: &'static str,
}

#[derive(Serialize)]
struct FoldReceipt<'a> {
    count: u8,
    unit: &'static str,
    balance_features: [&'static str; 7],
    lodo_groups: &'a BTreeSet<String>,
    loso_groups: &'a BTreeSet<String>,
    balance_audit: Vec<FoldBalance>,
}

#[derive(Serialize)]
struct FoldBalance {
    fold: u8,
    performance_ids: usize,
    duration_secs: f64,
    beat_ids: usize,
    fill_ids: usize,
    drummers: usize,
    sessions: usize,
    kits: usize,
    primary_styles: usize,
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
    manifest_sha256: String,
    manifest_rows: usize,
    folds_path: &'static str,
    folds_sha256: String,
    folds_rows: usize,
    exclusion_parts: Vec<ArtifactPart>,
    reserve_parts: Vec<ArtifactPart>,
}

#[derive(Serialize)]
struct ArtifactPart {
    path: String,
    sha256: String,
    rows: usize,
}

pub(crate) fn render_artifacts(
    selection: &SelectionOutcome,
    folds: &FoldPlan,
    metadata: &MetadataStats,
    required_drummers: &BTreeSet<String>,
    ledger: &OpenedLedger,
    identities: &InputIdentities,
    exclusions: &[PreflightExclusion],
) -> Result<Artifacts, String> {
    let manifest = render_manifest(&selection.selected, folds)?;
    let folds_csv = render_folds(&selection.selected, folds)?;
    let reserve = render_reserve_shards(&selection.reserve);
    let exclusion_files = render_exclusion_shards(exclusions);
    let exclusion_parts = exclusion_files
        .iter()
        .map(|(name, bytes, rows)| ArtifactPart {
            path: name.clone(),
            sha256: sha256_bytes(bytes),
            rows: *rows,
        })
        .collect();
    let reserve_parts = reserve
        .iter()
        .map(|(name, bytes, rows)| ArtifactPart {
            path: name.clone(),
            sha256: sha256_bytes(bytes),
            rows: *rows,
        })
        .collect();
    let mut winner_blockers = vec![
        "midi_archive_member_provenance_unverified",
        "audio_archive_not_verified",
        "audio_hash_duplicate_check_pending",
        "evaluation_excerpt_contract_not_frozen",
        "fold_balance_not_qualified",
        "blind_acoustic_audit_required",
        "candidate_scores_not_run",
        "test_isolation_incident_requires_guardian_or_new_holdout",
    ];
    if !selection.assessment.ready() {
        winner_blockers.insert(0, "insufficient_development_data");
    }
    let receipt = Receipt {
        schema: "kirin-hypha-attack-drum-development-receipt-v1",
        purpose: "development-selection",
        profile: "DRUM",
        dataset_id: "E-GMD",
        dataset_version: "1.0.0",
        metadata_sha256: &identities.metadata_sha256,
        midi_archive_sha256: &identities.midi_archive_sha256,
        midi_archive_verified: true,
        selected_midi_files_hashed: true,
        midi_root_archive_provenance_verified: false,
        audio_hash_duplicate_check: "pending_until_audio_ingest",
        expected_audio_archive_sha256:
            "7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053",
        audio_archive_verified: false,
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
        selection_algorithm_version: SELECTION_VERSION,
        seed: SELECTION_SEED,
        rank_hash_contract:
            "SHA-256(u64be_len||domain + u64be_len||field for version,seed,split,performance_id)",
        render_hash_contract: "independent domain; same encoding plus kit_name; render choice only",
        metadata,
        required_drummers,
        assessment: &selection.assessment,
        policy: PolicyReceipt {
            minimum_performance_ids: MIN_PERFORMANCE_IDS,
            minimum_unique_duration_secs: MIN_DURATION_SECS,
            minimum_beat_ratio: MIN_BEAT_FILL_RATIO,
            minimum_fill_ratio: MIN_BEAT_FILL_RATIO,
            minimum_primary_styles: MIN_STYLES,
            minimum_kits: MIN_KITS,
            require_all_available_drummers: true,
            minimum_kick_only_events: MIN_KICK_ONLY_EVENTS,
            minimum_hat_only_events: MIN_HAT_ONLY_EVENTS,
            one_render_per_performance_id: true,
            primary_style_definition: "substring before first slash",
            quota_counting_unit:
                "unique performance ID; kit render never duplicates duration/events",
        },
        folds: FoldReceipt {
            count: FOLD_COUNT,
            unit: "performance_id",
            balance_features: [
                "drummer",
                "session",
                "kit",
                "beat_type",
                "tempo_20_bpm_bin",
                "density_0.5_event_per_second_bin",
                "unique_duration",
            ],
            lodo_groups: &folds.lodo_groups,
            loso_groups: &folds.loso_groups,
            balance_audit: fold_balance(&selection.selected, folds)?,
        },
        grid: GridReceipt {
            superflux_stage1: stage1_grid_count(),
            superflux_stage2: stage2_grid_count(),
            mel32_stage1: mel32_grid_count(),
            mel32_stage2: mel32_stage2_grid_count(),
        },
        artifacts: ArtifactReceipt {
            manifest_path: MANIFEST_NAME,
            manifest_sha256: sha256_bytes(&manifest),
            manifest_rows: selection.selected.len(),
            folds_path: FOLDS_NAME,
            folds_sha256: sha256_bytes(&folds_csv),
            folds_rows: folds.by_performance_id.len(),
            exclusion_parts,
            reserve_parts,
        },
        this_selector_run_audio_opened: false,
        this_selector_run_test_midi_opened: false,
        this_selector_run_fresh_holdout_opened: false,
        this_selector_run_two_mix_opened: false,
        this_selector_run_candidate_scores_run: false,
        blind_acoustic_audit: "not_attached",
        candidate_evaluation_allowed: false,
        winner_allowed: false,
        winner_blockers,
    };
    let mut receipt = serde_json::to_vec_pretty(&receipt)
        .map_err(|error| format!("cannot serialize development receipt: {error}"))?;
    receipt.push(b'\n');
    let mut files = vec![
        ArtifactFile {
            name: MANIFEST_NAME.into(),
            bytes: manifest,
        },
        ArtifactFile {
            name: FOLDS_NAME.into(),
            bytes: folds_csv,
        },
    ];
    files.extend(
        exclusion_files
            .into_iter()
            .map(|(name, bytes, _)| ArtifactFile { name, bytes }),
    );
    files.extend(
        reserve
            .into_iter()
            .map(|(name, bytes, _)| ArtifactFile { name, bytes }),
    );
    files.push(ArtifactFile {
        name: RECEIPT_NAME.into(),
        bytes: receipt,
    });
    Ok(Artifacts { files })
}

fn render_manifest(selected: &[Performance], folds: &FoldPlan) -> Result<Vec<u8>, String> {
    let mut output = format!("{MANIFEST_HEADER}\n");
    for (index, item) in selected.iter().enumerate() {
        let fold = folds.fold_for(&item.row.id)?;
        let fields = vec![
            (index + 1).to_string(),
            item.selection_key.clone(),
            fold.to_string(),
            item.row.drummer.clone(),
            item.row.session.clone(),
            item.row.id.clone(),
            item.row.style.clone(),
            item.row.bpm.to_string(),
            item.row.beat_type.clone(),
            item.row.time_signature.clone(),
            item.row.duration.to_string(),
            item.row.split.clone(),
            item.row.midi_filename.clone(),
            item.row.audio_filename.clone(),
            item.row.kit_name.clone(),
            item.midi.sha256.clone(),
            item.midi.raw_notes.to_string(),
            item.midi.compound_events.to_string(),
            item.midi.kick_only_events.to_string(),
            item.midi.hat_only_events.to_string(),
            format!("{:.9}", item.density()),
        ];
        push_csv_row(&mut output, &fields);
    }
    Ok(output.into_bytes())
}

fn render_folds(selected: &[Performance], folds: &FoldPlan) -> Result<Vec<u8>, String> {
    let mut output =
        "id,fold,drummer,session,lodo_holdout,loso_holdout,selection_rank\n".to_string();
    for (index, item) in selected.iter().enumerate() {
        let fields = [
            item.row.id.clone(),
            folds.fold_for(&item.row.id)?.to_string(),
            item.row.drummer.clone(),
            item.row.session.clone(),
            item.row.drummer.clone(),
            item.row.session.clone(),
            (index + 1).to_string(),
        ];
        push_csv_row(&mut output, &fields);
    }
    Ok(output.into_bytes())
}

fn render_reserve_shards(reserve: &[Performance]) -> Vec<(String, Vec<u8>, usize)> {
    reserve
        .chunks(RESERVE_ROWS_PER_SHARD)
        .enumerate()
        .map(|(part, rows)| {
            let mut output = "reserve_rank,selection_key,drummer,session,id,style,bpm,beat_type,time_signature,duration,split,midi_filename,audio_filename,kit_name,midi_sha256,compound_events,kick_only_events,hat_only_events\n".to_string();
            for (offset, item) in rows.iter().enumerate() {
                let index = part * RESERVE_ROWS_PER_SHARD + offset;
        let fields = vec![
            (index + 1).to_string(),
            item.selection_key.clone(),
            item.row.drummer.clone(),
            item.row.session.clone(),
            item.row.id.clone(),
            item.row.style.clone(),
            item.row.bpm.to_string(),
            item.row.beat_type.clone(),
            item.row.time_signature.clone(),
            item.row.duration.to_string(),
            item.row.split.clone(),
            item.row.midi_filename.clone(),
            item.row.audio_filename.clone(),
            item.row.kit_name.clone(),
            item.midi.sha256.clone(),
            item.midi.compound_events.to_string(),
            item.midi.kick_only_events.to_string(),
            item.midi.hat_only_events.to_string(),
        ];
        push_csv_row(&mut output, &fields);
            }
            (
                format!("attack_drum_development_reserve_v1_part_{:03}.csv", part + 1),
                output.into_bytes(),
                rows.len(),
            )
        })
        .collect()
}

fn render_exclusion_shards(exclusions: &[PreflightExclusion]) -> Vec<(String, Vec<u8>, usize)> {
    let shard_count = exclusions.len().max(1).div_ceil(RESERVE_ROWS_PER_SHARD);
    (0..shard_count)
        .map(|part| {
            let start = part * RESERVE_ROWS_PER_SHARD;
            let end = (start + RESERVE_ROWS_PER_SHARD).min(exclusions.len());
            let mut output =
                "performance_id,kit_name,midi_filename,render_choice_key,reason\n".to_string();
            for item in &exclusions[start..end] {
                push_csv_row(
                    &mut output,
                    &[
                        item.performance_id.clone(),
                        item.kit_name.clone(),
                        item.midi_filename.clone(),
                        item.render_choice_key.clone(),
                        item.reason.clone(),
                    ],
                );
            }
            (
                format!(
                    "attack_drum_development_exclusions_v1_part_{:03}.csv",
                    part + 1
                ),
                output.into_bytes(),
                end - start,
            )
        })
        .collect()
}

fn fold_balance(selected: &[Performance], folds: &FoldPlan) -> Result<Vec<FoldBalance>, String> {
    (0..FOLD_COUNT)
        .map(|fold| {
            let rows = selected
                .iter()
                .filter(|item| folds.fold_for(&item.row.id) == Ok(fold))
                .collect::<Vec<_>>();
            Ok(FoldBalance {
                fold,
                performance_ids: rows.len(),
                duration_secs: rows.iter().map(|item| item.row.duration).sum(),
                beat_ids: rows
                    .iter()
                    .filter(|item| item.row.beat_type == "beat")
                    .count(),
                fill_ids: rows
                    .iter()
                    .filter(|item| item.row.beat_type == "fill")
                    .count(),
                drummers: rows
                    .iter()
                    .map(|item| &item.row.drummer)
                    .collect::<BTreeSet<_>>()
                    .len(),
                sessions: rows
                    .iter()
                    .map(|item| &item.row.session)
                    .collect::<BTreeSet<_>>()
                    .len(),
                kits: rows
                    .iter()
                    .map(|item| &item.row.kit_name)
                    .collect::<BTreeSet<_>>()
                    .len(),
                primary_styles: rows
                    .iter()
                    .map(|item| item.row.primary_style())
                    .collect::<BTreeSet<_>>()
                    .len(),
            })
        })
        .collect()
}

fn push_csv_row(output: &mut String, fields: &[String]) {
    output.push_str(
        &fields
            .iter()
            .map(|field| encode_csv_field(field))
            .collect::<Vec<_>>()
            .join(","),
    );
    output.push('\n');
}
