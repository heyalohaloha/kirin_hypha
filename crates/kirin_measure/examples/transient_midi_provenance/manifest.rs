use std::collections::{BTreeSet, HashSet};

use crate::contract::{PINNED_MANIFEST_ROWS, PINNED_TRAIN_ROWS, PINNED_VALIDATION_ROWS};
use crate::drum_excerpt::{
    decimal_seconds_to_samples_half_up, excerpt_bounds_44100, performance_rank_key,
    EXCERPT_SAMPLE_RATE,
};

const HEADER: &str = "selection_rank,selection_key,fold,drummer,session,id,style,bpm,beat_type,time_signature,source_duration_seconds,split,midi_filename,audio_filename,kit_name,midi_sha256,excerpt_start_sample_44100,excerpt_end_sample_44100,excerpt_raw_notes,excerpt_compound_events,excerpt_kick_only_events,excerpt_hat_only_events,excerpt_density_events_per_second";
const EXPECTED_FOLDS: usize = 5;
const EXPECTED_ROWS_PER_FOLD: usize = 58;
const EXPECTED_DURATION_SAMPLES: u64 = 136_801_333;
const EXPECTED_RAW_NOTES: usize = 25_168;
const EXPECTED_COMPOUND_EVENTS: usize = 18_211;
const EXPECTED_KICK_ONLY_EVENTS: usize = 1_342;
const EXPECTED_HAT_ONLY_EVENTS: usize = 3_471;

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct ManifestRow {
    pub(crate) selection_rank: usize,
    pub(crate) selection_key: String,
    pub(crate) fold: u8,
    pub(crate) performance_id: String,
    pub(crate) split: String,
    pub(crate) midi_relative_name: String,
    pub(crate) audio_relative_name: String,
    pub(crate) midi_sha256: String,
    pub(crate) source_duration_decimal: String,
    pub(crate) source_duration_samples_44100: u64,
    pub(crate) excerpt_start_sample_44100: u64,
    pub(crate) excerpt_end_sample_44100: u64,
    pub(crate) excerpt_raw_notes: usize,
    pub(crate) excerpt_compound_events: usize,
    pub(crate) excerpt_kick_only_events: usize,
    pub(crate) excerpt_hat_only_events: usize,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct ManifestTotals {
    pub(crate) rows: usize,
    pub(crate) train_rows: usize,
    pub(crate) validation_rows: usize,
    pub(crate) excerpt_duration_samples_44100: u64,
    pub(crate) excerpt_raw_notes: usize,
    pub(crate) excerpt_compound_events: usize,
    pub(crate) excerpt_kick_only_events: usize,
    pub(crate) excerpt_hat_only_events: usize,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct VerifiedManifest {
    pub(crate) rows: Vec<ManifestRow>,
    pub(crate) totals: ManifestTotals,
}

pub(crate) fn parse_pinned_manifest(bytes: &[u8]) -> Result<VerifiedManifest, String> {
    let text = std::str::from_utf8(bytes)
        .map_err(|error| format!("development manifest is not UTF-8: {error}"))?;
    if !text.ends_with('\n') || text.contains('\r') {
        return Err("development manifest must use final-LF canonical lines".to_string());
    }
    let mut lines = text.lines();
    if lines.next() != Some(HEADER) {
        return Err("unexpected development manifest header".to_string());
    }
    let mut rows = Vec::new();
    let mut selection_keys = HashSet::new();
    let mut performance_ids = HashSet::new();
    let mut midi_paths = HashSet::new();
    let mut audio_paths = HashSet::new();
    for (index, line) in lines.enumerate() {
        let row_number = index + 2;
        if line.is_empty() {
            return Err(format!("blank development manifest row {row_number}"));
        }
        let fields = crate::csv::parse_csv_line(line)
            .map_err(|error| format!("manifest row {row_number}: {error}"))?;
        if fields.len() != 23 || fields.iter().any(String::is_empty) {
            return Err(format!(
                "development manifest row {row_number} must have 23 nonempty fields"
            ));
        }
        let selection_rank = canonical_usize(&fields[0], "selection_rank", row_number)?;
        if selection_rank != rows.len() + 1 {
            return Err(format!("non-contiguous selection_rank at row {row_number}"));
        }
        require_sha256(&fields[1], "selection_key", row_number)?;
        let expected_key = performance_rank_key(&fields[11], &fields[5])
            .map_err(|error| format!("rank identity at row {row_number}: {error}"))?;
        if fields[1] != expected_key || !selection_keys.insert(fields[1].clone()) {
            return Err(format!(
                "invalid or duplicate selection_key at row {row_number}"
            ));
        }
        let fold = canonical_u8(&fields[2], "fold", row_number)?;
        if usize::from(fold) >= EXPECTED_FOLDS {
            return Err(format!("fold is outside 0..4 at row {row_number}"));
        }
        validate_identity(&fields, row_number)?;
        if !performance_ids.insert(fields[5].clone()) {
            return Err(format!("duplicate performance ID at row {row_number}"));
        }
        if !matches!(fields[8].as_str(), "beat" | "fill") {
            return Err(format!("invalid beat_type at row {row_number}"));
        }
        positive_finite(&fields[7], "bpm", row_number)?;
        if !matches!(fields[11].as_str(), "train" | "validation") {
            return Err(format!("split leakage at row {row_number}"));
        }
        validate_relative_name(&fields[12], ".midi", "MIDI", row_number)?;
        validate_relative_name(&fields[13], ".wav", "audio", row_number)?;
        if !midi_paths.insert(fields[12].clone()) || !audio_paths.insert(fields[13].clone()) {
            return Err(format!("duplicate selected input path at row {row_number}"));
        }
        require_sha256(&fields[15], "MIDI SHA-256", row_number)?;
        let source_duration_samples_44100 =
            decimal_seconds_to_samples_half_up(&fields[10], u64::from(EXCERPT_SAMPLE_RATE))
                .map_err(|error| format!("source duration at row {row_number}: {error}"))?;
        let start = canonical_u64(&fields[16], "excerpt_start", row_number)?;
        let end = canonical_u64(&fields[17], "excerpt_end", row_number)?;
        let expected = excerpt_bounds_44100(source_duration_samples_44100, &fields[11], &fields[5])
            .map_err(|error| format!("excerpt identity at row {row_number}: {error}"))?;
        if start != expected.start_sample || end != expected.end_sample || start >= end {
            return Err(format!("excerpt mapping mismatch at row {row_number}"));
        }
        let raw = canonical_usize(&fields[18], "excerpt_raw_notes", row_number)?;
        let compounds = canonical_usize(&fields[19], "excerpt_compounds", row_number)?;
        let kick = canonical_usize(&fields[20], "excerpt_kick", row_number)?;
        let hat = canonical_usize(&fields[21], "excerpt_hat", row_number)?;
        let density = nonnegative_finite(&fields[22], "excerpt_density", row_number)?;
        let expected_density =
            compounds as f64 / ((end - start) as f64 / f64::from(EXCERPT_SAMPLE_RATE));
        if (density - expected_density).abs() > 1.1e-9 {
            return Err(format!("excerpt density mismatch at row {row_number}"));
        }
        rows.push(ManifestRow {
            selection_rank,
            selection_key: fields[1].clone(),
            fold,
            performance_id: fields[5].clone(),
            split: fields[11].clone(),
            midi_relative_name: fields[12].clone(),
            audio_relative_name: fields[13].clone(),
            midi_sha256: fields[15].clone(),
            source_duration_decimal: fields[10].clone(),
            source_duration_samples_44100,
            excerpt_start_sample_44100: start,
            excerpt_end_sample_44100: end,
            excerpt_raw_notes: raw,
            excerpt_compound_events: compounds,
            excerpt_kick_only_events: kick,
            excerpt_hat_only_events: hat,
        });
    }
    validate_complete(rows)
}

fn validate_complete(rows: Vec<ManifestRow>) -> Result<VerifiedManifest, String> {
    let train_rows = rows.iter().filter(|row| row.split == "train").count();
    let validation_rows = rows.iter().filter(|row| row.split == "validation").count();
    let fold_sizes = (0..EXPECTED_FOLDS)
        .map(|fold| {
            rows.iter()
                .filter(|row| usize::from(row.fold) == fold)
                .count()
        })
        .collect::<BTreeSet<_>>();
    let totals = ManifestTotals {
        rows: rows.len(),
        train_rows,
        validation_rows,
        excerpt_duration_samples_44100: checked_sum_u64(
            rows.iter()
                .map(|row| row.excerpt_end_sample_44100 - row.excerpt_start_sample_44100),
            "excerpt duration",
        )?,
        excerpt_raw_notes: checked_sum_usize(
            rows.iter().map(|row| row.excerpt_raw_notes),
            "excerpt raw notes",
        )?,
        excerpt_compound_events: checked_sum_usize(
            rows.iter().map(|row| row.excerpt_compound_events),
            "excerpt compounds",
        )?,
        excerpt_kick_only_events: checked_sum_usize(
            rows.iter().map(|row| row.excerpt_kick_only_events),
            "excerpt kick events",
        )?,
        excerpt_hat_only_events: checked_sum_usize(
            rows.iter().map(|row| row.excerpt_hat_only_events),
            "excerpt hat events",
        )?,
    };
    if totals.rows != PINNED_MANIFEST_ROWS
        || totals.train_rows != PINNED_TRAIN_ROWS
        || totals.validation_rows != PINNED_VALIDATION_ROWS
        || fold_sizes != BTreeSet::from([EXPECTED_ROWS_PER_FOLD])
        || totals.excerpt_duration_samples_44100 != EXPECTED_DURATION_SAMPLES
        || totals.excerpt_raw_notes != EXPECTED_RAW_NOTES
        || totals.excerpt_compound_events != EXPECTED_COMPOUND_EVENTS
        || totals.excerpt_kick_only_events != EXPECTED_KICK_ONLY_EVENTS
        || totals.excerpt_hat_only_events != EXPECTED_HAT_ONLY_EVENTS
    {
        return Err("development manifest fixed aggregate contract mismatch".to_string());
    }
    Ok(VerifiedManifest { rows, totals })
}

fn validate_identity(fields: &[String], row: usize) -> Result<(), String> {
    if !fields[4].starts_with(&format!("{}/", fields[3]))
        || fields[5]
            != format!(
                "{}/{}",
                fields[4],
                fields[5].rsplit('/').next().unwrap_or("")
            )
        || !fields[12].starts_with(&format!("{}/", fields[4]))
        || !fields[13].starts_with(&format!("{}/", fields[4]))
    {
        return Err(format!(
            "performance identity hierarchy mismatch at row {row}"
        ));
    }
    Ok(())
}

fn validate_relative_name(
    value: &str,
    suffix: &str,
    label: &str,
    row: usize,
) -> Result<(), String> {
    let components = value.split('/').collect::<Vec<_>>();
    if value.starts_with('/')
        || value.contains(['\\', '\0'])
        || value.contains(':')
        || !value.ends_with(suffix)
        || components.len() < 3
        || components
            .iter()
            .any(|part| part.is_empty() || matches!(*part, "." | ".."))
    {
        return Err(format!("unsafe {label} relative name at row {row}"));
    }
    Ok(())
}

fn require_sha256(value: &str, label: &str, row: usize) -> Result<(), String> {
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || matches!(byte, b'a'..=b'f'))
    {
        return Err(format!("invalid {label} at row {row}"));
    }
    Ok(())
}

fn canonical_usize(value: &str, label: &str, row: usize) -> Result<usize, String> {
    value
        .parse::<usize>()
        .ok()
        .filter(|parsed| parsed.to_string() == value)
        .ok_or_else(|| format!("invalid canonical {label} at row {row}"))
}

fn canonical_u64(value: &str, label: &str, row: usize) -> Result<u64, String> {
    value
        .parse::<u64>()
        .ok()
        .filter(|parsed| parsed.to_string() == value)
        .ok_or_else(|| format!("invalid canonical {label} at row {row}"))
}

fn canonical_u8(value: &str, label: &str, row: usize) -> Result<u8, String> {
    value
        .parse::<u8>()
        .ok()
        .filter(|parsed| parsed.to_string() == value)
        .ok_or_else(|| format!("invalid canonical {label} at row {row}"))
}

fn positive_finite(value: &str, label: &str, row: usize) -> Result<f64, String> {
    value
        .parse::<f64>()
        .ok()
        .filter(|parsed| parsed.is_finite() && *parsed > 0.0)
        .ok_or_else(|| format!("invalid positive {label} at row {row}"))
}

fn nonnegative_finite(value: &str, label: &str, row: usize) -> Result<f64, String> {
    value
        .parse::<f64>()
        .ok()
        .filter(|parsed| parsed.is_finite() && *parsed >= 0.0)
        .ok_or_else(|| format!("invalid nonnegative {label} at row {row}"))
}

fn checked_sum_u64(mut values: impl Iterator<Item = u64>, label: &str) -> Result<u64, String> {
    values.try_fold(0_u64, |sum, value| {
        sum.checked_add(value)
            .ok_or_else(|| format!("{label} aggregate overflow"))
    })
}

fn checked_sum_usize(
    mut values: impl Iterator<Item = usize>,
    label: &str,
) -> Result<usize, String> {
    values.try_fold(0_usize, |sum, value| {
        sum.checked_add(value)
            .ok_or_else(|| format!("{label} aggregate overflow"))
    })
}

#[cfg(test)]
#[path = "manifest_tests.rs"]
mod tests;
