use std::collections::BTreeSet;

use serde::Serialize;

use crate::csv::encode_csv_field;
use crate::folds::{FoldPlan, FOLD_COUNT};
use crate::metadata::{Performance, PreflightExclusion};

pub(crate) const MANIFEST_NAME: &str = "attack_drum_development_manifest_v2.csv";
pub(crate) const FOLDS_NAME: &str = "attack_drum_development_folds_v2.csv";
pub(crate) const RECEIPT_NAME: &str = "attack_drum_development_receipt_v2.json";
const RESERVE_ROWS_PER_SHARD: usize = 400;
const MANIFEST_HEADER: &str = "selection_rank,selection_key,fold,drummer,session,id,style,bpm,beat_type,time_signature,source_duration_seconds,split,midi_filename,audio_filename,kit_name,midi_sha256,excerpt_start_sample_44100,excerpt_end_sample_44100,excerpt_raw_notes,excerpt_compound_events,excerpt_kick_only_events,excerpt_hat_only_events,excerpt_density_events_per_second";
const FOLDS_HEADER: &str = "id,fold,drummer,session,lodo_holdout,loso_holdout,selection_rank";

#[derive(Serialize)]
pub(crate) struct FoldBalance {
    fold: u8,
    performance_ids: usize,
    excerpt_duration_samples_44100: u64,
    excerpt_duration_secs: f64,
    beat_ids: usize,
    fill_ids: usize,
    validation_ids: usize,
    forced_opened_validation_ids: usize,
    excerpt_compound_events: usize,
    excerpt_kick_only_events: usize,
    excerpt_hat_only_events: usize,
    kick_positive_ids: usize,
    hat_positive_ids: usize,
    drummers: usize,
    sessions: usize,
    kits: usize,
    primary_styles: usize,
}

pub(crate) fn render_manifest(
    selected: &[Performance],
    folds: &FoldPlan,
) -> Result<Vec<u8>, String> {
    let mut output = format!("{MANIFEST_HEADER}\n");
    for (index, item) in selected.iter().enumerate() {
        let fields = vec![
            (index + 1).to_string(),
            item.selection_key.clone(),
            folds.fold_for(&item.row.id)?.to_string(),
            item.row.drummer.clone(),
            item.row.session.clone(),
            item.row.id.clone(),
            item.row.style.clone(),
            item.row.bpm.to_string(),
            item.row.beat_type.clone(),
            item.row.time_signature.clone(),
            item.row.duration_decimal.clone(),
            item.row.split.clone(),
            item.row.midi_filename.clone(),
            item.row.audio_filename.clone(),
            item.row.kit_name.clone(),
            item.midi.sha256.clone(),
            item.excerpt_start_sample_44100().to_string(),
            item.excerpt_end_sample_44100().to_string(),
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

pub(crate) fn render_folds(selected: &[Performance], folds: &FoldPlan) -> Result<Vec<u8>, String> {
    let mut output = format!("{FOLDS_HEADER}\n");
    for (index, item) in selected.iter().enumerate() {
        push_csv_row(
            &mut output,
            &[
                item.row.id.clone(),
                folds.fold_for(&item.row.id)?.to_string(),
                item.row.drummer.clone(),
                item.row.session.clone(),
                item.row.drummer.clone(),
                item.row.session.clone(),
                (index + 1).to_string(),
            ],
        );
    }
    Ok(output.into_bytes())
}

pub(crate) fn render_reserve_shards(reserve: &[Performance]) -> Vec<(String, Vec<u8>, usize)> {
    reserve
        .chunks(RESERVE_ROWS_PER_SHARD)
        .enumerate()
        .map(|(part, rows)| {
            let mut output = "reserve_rank,selection_key,drummer,session,id,style,bpm,beat_type,time_signature,source_duration_seconds,split,midi_filename,audio_filename,kit_name,midi_sha256,excerpt_start_sample_44100,excerpt_end_sample_44100,excerpt_raw_notes,excerpt_compound_events,excerpt_kick_only_events,excerpt_hat_only_events,excerpt_density_events_per_second\n".to_string();
            for (offset, item) in rows.iter().enumerate() {
                let index = part * RESERVE_ROWS_PER_SHARD + offset;
                push_csv_row(&mut output, &reserve_fields(index + 1, item));
            }
            (
                format!("attack_drum_development_reserve_v2_part_{:03}.csv", part + 1),
                output.into_bytes(),
                rows.len(),
            )
        })
        .collect()
}

fn reserve_fields(rank: usize, item: &Performance) -> Vec<String> {
    vec![
        rank.to_string(),
        item.selection_key.clone(),
        item.row.drummer.clone(),
        item.row.session.clone(),
        item.row.id.clone(),
        item.row.style.clone(),
        item.row.bpm.to_string(),
        item.row.beat_type.clone(),
        item.row.time_signature.clone(),
        item.row.duration_decimal.clone(),
        item.row.split.clone(),
        item.row.midi_filename.clone(),
        item.row.audio_filename.clone(),
        item.row.kit_name.clone(),
        item.midi.sha256.clone(),
        item.excerpt_start_sample_44100().to_string(),
        item.excerpt_end_sample_44100().to_string(),
        item.midi.raw_notes.to_string(),
        item.midi.compound_events.to_string(),
        item.midi.kick_only_events.to_string(),
        item.midi.hat_only_events.to_string(),
        format!("{:.9}", item.density()),
    ]
}

pub(crate) fn render_exclusion_shards(
    exclusions: &[PreflightExclusion],
) -> Vec<(String, Vec<u8>, usize)> {
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
                    "attack_drum_development_exclusions_v2_part_{:03}.csv",
                    part + 1
                ),
                output.into_bytes(),
                end - start,
            )
        })
        .collect()
}

pub(crate) fn fold_balance(
    selected: &[Performance],
    folds: &FoldPlan,
) -> Result<Vec<FoldBalance>, String> {
    (0..FOLD_COUNT)
        .map(|fold| fold_summary(fold, selected, folds))
        .collect()
}

fn fold_summary(
    fold: u8,
    selected: &[Performance],
    folds: &FoldPlan,
) -> Result<FoldBalance, String> {
    let rows = selected
        .iter()
        .filter(|item| folds.fold_for(&item.row.id) == Ok(fold))
        .collect::<Vec<_>>();
    let duration_samples = rows
        .iter()
        .map(|item| item.excerpt_end_sample_44100() - item.excerpt_start_sample_44100())
        .sum::<u64>();
    Ok(FoldBalance {
        fold,
        performance_ids: rows.len(),
        excerpt_duration_samples_44100: duration_samples,
        excerpt_duration_secs: duration_samples as f64 / 44_100.0,
        beat_ids: count(&rows, |item| item.row.beat_type == "beat"),
        fill_ids: count(&rows, |item| item.row.beat_type == "fill"),
        validation_ids: count(&rows, |item| item.row.split == "validation"),
        forced_opened_validation_ids: count(&rows, |item| item.forced_opened_validation),
        excerpt_compound_events: rows.iter().map(|item| item.midi.compound_events).sum(),
        excerpt_kick_only_events: rows.iter().map(|item| item.midi.kick_only_events).sum(),
        excerpt_hat_only_events: rows.iter().map(|item| item.midi.hat_only_events).sum(),
        kick_positive_ids: count(&rows, |item| item.midi.kick_only_events > 0),
        hat_positive_ids: count(&rows, |item| item.midi.hat_only_events > 0),
        drummers: unique(&rows, |item| item.row.drummer.as_str()),
        sessions: unique(&rows, |item| item.row.session.as_str()),
        kits: unique(&rows, |item| item.row.kit_name.as_str()),
        primary_styles: unique(&rows, |item| item.row.primary_style()),
    })
}

fn count(rows: &[&Performance], predicate: impl Fn(&Performance) -> bool) -> usize {
    rows.iter().filter(|item| predicate(item)).count()
}

fn unique<'a>(rows: &'a [&Performance], value: impl Fn(&'a Performance) -> &'a str) -> usize {
    rows.iter()
        .map(|item| value(item))
        .collect::<BTreeSet<_>>()
        .len()
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
