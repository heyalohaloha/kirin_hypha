use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::fs;
use std::path::{Component, Path, PathBuf};

use serde::Serialize;

use crate::contract::{sha256_bytes, METADATA_SHA256};
use crate::csv::parse_csv_line;
use crate::drum_excerpt::{
    decimal_seconds_to_samples_half_up, excerpt_bounds_44100, EXCERPT_SAMPLE_RATE,
};
use crate::ledger::{OpenedLedger, OpenedUse};
use crate::midi::{inspect_midi, MidiSummary};
use crate::selector::{performance_rank_key, render_choice_key};

const METADATA_HEADER: &str = "drummer,session,id,style,bpm,beat_type,time_signature,duration,split,midi_filename,audio_filename,kit_name";

#[derive(Clone, Debug, PartialEq)]
pub(crate) struct MetadataRow {
    pub(crate) drummer: String,
    pub(crate) session: String,
    pub(crate) id: String,
    pub(crate) style: String,
    pub(crate) bpm: f64,
    pub(crate) beat_type: String,
    pub(crate) time_signature: String,
    pub(crate) duration_decimal: String,
    pub(crate) duration: f64,
    pub(crate) duration_samples_44100: u64,
    pub(crate) split: String,
    pub(crate) midi_filename: String,
    pub(crate) audio_filename: String,
    pub(crate) kit_name: String,
}

impl MetadataRow {
    pub(crate) fn primary_style(&self) -> &str {
        self.style.split('/').next().unwrap_or(&self.style)
    }
}

#[derive(Clone, Debug)]
pub(crate) struct Performance {
    pub(crate) row: MetadataRow,
    pub(crate) selection_key: String,
    pub(crate) midi: MidiSummary,
    pub(crate) forced_opened_validation: bool,
}

impl Performance {
    pub(crate) fn density(&self) -> f64 {
        self.midi.compound_events as f64 / self.excerpt_duration_secs()
    }

    pub(crate) fn excerpt_end_sample_44100(&self) -> u64 {
        excerpt_bounds_44100(
            self.row.duration_samples_44100,
            &self.row.split,
            &self.row.id,
        )
        .expect("validated excerpt bounds")
        .end_sample
    }

    pub(crate) fn excerpt_start_sample_44100(&self) -> u64 {
        excerpt_bounds_44100(
            self.row.duration_samples_44100,
            &self.row.split,
            &self.row.id,
        )
        .expect("validated excerpt bounds")
        .start_sample
    }

    pub(crate) fn excerpt_duration_secs(&self) -> f64 {
        (self.excerpt_end_sample_44100() - self.excerpt_start_sample_44100()) as f64
            / f64::from(EXCERPT_SAMPLE_RATE)
    }
}

#[derive(Clone, Debug)]
pub(crate) struct PerformanceRows {
    pub(crate) rows: Vec<MetadataRow>,
    pub(crate) forced_opened_validation: bool,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct PreflightExclusion {
    pub(crate) performance_id: String,
    pub(crate) kit_name: String,
    pub(crate) midi_filename: String,
    pub(crate) render_choice_key: String,
    pub(crate) reason: String,
}

#[derive(Debug)]
pub(crate) struct EnrichedPool {
    pub(crate) performances: Vec<Performance>,
    pub(crate) exclusions: Vec<PreflightExclusion>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MetadataStats {
    pub(crate) total_rows: usize,
    pub(crate) eligible_rows_before_opened_exclusion: usize,
    pub(crate) excluded_test_rows: usize,
    pub(crate) excluded_opened_rows: usize,
    pub(crate) eligible_performance_ids: usize,
}

#[derive(Debug)]
pub(crate) struct MetadataPool {
    pub(crate) performances: Vec<PerformanceRows>,
    pub(crate) available_drummers: BTreeSet<String>,
    pub(crate) stats: MetadataStats,
}

pub(crate) fn read_official_metadata(
    path: &Path,
    ledger: &OpenedLedger,
) -> Result<MetadataPool, String> {
    let bytes = fs::read(path)
        .map_err(|error| format!("cannot read official metadata {}: {error}", path.display()))?;
    let digest = sha256_bytes(&bytes);
    if digest != METADATA_SHA256 {
        return Err(format!("official metadata SHA-256 mismatch: {digest}"));
    }
    parse_metadata_bytes(&bytes, ledger)
}

fn parse_metadata_bytes(bytes: &[u8], ledger: &OpenedLedger) -> Result<MetadataPool, String> {
    let text = std::str::from_utf8(bytes)
        .map_err(|error| format!("official metadata is not UTF-8: {error}"))?;
    let mut lines = text.lines();
    if lines.next().map(|line| line.trim_end_matches('\r')) != Some(METADATA_HEADER) {
        return Err("unexpected official metadata header".to_string());
    }
    let mut groups = BTreeMap::<String, Vec<MetadataRow>>::new();
    let mut row_keys = HashSet::new();
    let mut input_paths = HashSet::new();
    let (mut total_rows, mut eligible_rows, mut excluded_test, mut excluded_opened) = (0, 0, 0, 0);
    for (index, raw_line) in lines.enumerate() {
        let line = raw_line.trim_end_matches('\r');
        if line.is_empty() {
            continue;
        }
        total_rows += 1;
        let fields = parse_csv_line(line).map_err(|error| format!("row {}: {error}", index + 2))?;
        if fields.len() != 12 || fields.iter().any(String::is_empty) {
            return Err(format!("invalid metadata row {}", index + 2));
        }
        let split = fields[8].as_str();
        if split == "test" {
            // Test rows are rejected before a path object is constructed.
            excluded_test += 1;
            continue;
        }
        if !matches!(split, "train" | "validation") {
            return Err(format!(
                "row {} has forbidden split before path resolution: {split}",
                index + 2
            ));
        }
        eligible_rows += 1;
        let disposition = ledger.opened_use(&fields[2]);
        if disposition == Some(OpenedUse::DiagnosticOnly) {
            excluded_opened += 1;
            continue;
        }
        validate_relative(&fields[9], "MIDI", index + 2)?;
        validate_relative(&fields[10], "audio", index + 2)?;
        let bpm = positive_finite(&fields[4], "bpm", index + 2)?;
        let duration = positive_finite(&fields[7], "duration", index + 2)?;
        let duration_samples_44100 =
            decimal_seconds_to_samples_half_up(&fields[7], u64::from(EXCERPT_SAMPLE_RATE))
                .map_err(|error| format!("row {}: {error}", index + 2))?;
        if !matches!(fields[5].as_str(), "beat" | "fill") {
            return Err(format!("invalid beat_type at row {}", index + 2));
        }
        let row_key = (fields[2].clone(), fields[11].clone());
        if !row_keys.insert(row_key) {
            return Err(format!("duplicate id + kit at row {}", index + 2));
        }
        if !input_paths.insert(fields[9].clone()) || !input_paths.insert(fields[10].clone()) {
            return Err(format!("duplicate input path at row {}", index + 2));
        }
        groups
            .entry(fields[2].clone())
            .or_default()
            .push(MetadataRow {
                drummer: fields[0].clone(),
                session: fields[1].clone(),
                id: fields[2].clone(),
                style: fields[3].clone(),
                bpm,
                beat_type: fields[5].clone(),
                time_signature: fields[6].clone(),
                duration_decimal: fields[7].clone(),
                duration,
                duration_samples_44100,
                split: fields[8].clone(),
                midi_filename: fields[9].clone(),
                audio_filename: fields[10].clone(),
                kit_name: fields[11].clone(),
            });
    }
    if groups.is_empty() {
        return Err("official metadata has no eligible unopened train/validation rows".to_string());
    }
    let mut available_drummers = BTreeSet::new();
    let mut performances = Vec::with_capacity(groups.len());
    for rows in groups.into_values() {
        validate_group(&rows)?;
        available_drummers.insert(rows[0].drummer.clone());
        let forced_opened_validation =
            ledger.opened_use(&rows[0].id) == Some(OpenedUse::DevelopmentRequired);
        performances.push(PerformanceRows {
            rows,
            forced_opened_validation,
        });
    }
    performances.sort_by(|left, right| left.rows[0].id.cmp(&right.rows[0].id));
    Ok(MetadataPool {
        stats: MetadataStats {
            total_rows,
            eligible_rows_before_opened_exclusion: eligible_rows,
            excluded_test_rows: excluded_test,
            excluded_opened_rows: excluded_opened,
            eligible_performance_ids: performances.len(),
        },
        performances,
        available_drummers,
    })
}

pub(crate) fn enrich_performances(
    groups: Vec<PerformanceRows>,
    midi_root: &Path,
) -> Result<EnrichedPool, String> {
    let root = fs::canonicalize(midi_root)
        .map_err(|error| format!("cannot resolve MIDI root {}: {error}", midi_root.display()))?;
    let mut performances = Vec::with_capacity(groups.len());
    let mut exclusions = Vec::new();
    for group in groups {
        let forced_opened_validation = group.forced_opened_validation;
        let mut choices = group
            .rows
            .into_iter()
            .map(|row| {
                let key = render_choice_key(&row);
                (row, key)
            })
            .collect::<Vec<_>>();
        choices.sort_by(|left, right| {
            left.1
                .cmp(&right.1)
                .then_with(|| left.0.kit_name.cmp(&right.0.kit_name))
        });
        let mut chosen = None;
        for (row, render_key) in choices {
            if !matches!(row.split.as_str(), "train" | "validation") {
                return Err("internal split guard rejected MIDI before path resolution".to_string());
            }
            match inspect_render(&root, &row) {
                Ok(midi) => {
                    chosen = Some((row, midi));
                    break;
                }
                Err(reason) => exclusions.push(PreflightExclusion {
                    performance_id: row.id.clone(),
                    kit_name: row.kit_name.clone(),
                    midi_filename: row.midi_filename.clone(),
                    render_choice_key: render_key,
                    reason,
                }),
            }
        }
        if let Some((row, midi)) = chosen {
            let rank_key = performance_rank_key(&row);
            performances.push(Performance {
                row,
                selection_key: rank_key,
                midi,
                forced_opened_validation,
            });
        }
    }
    reject_cross_id_duplicate_midi(&performances)?;
    performances.sort_by(|left, right| {
        left.selection_key
            .cmp(&right.selection_key)
            .then_with(|| left.row.id.cmp(&right.row.id))
    });
    Ok(EnrichedPool {
        performances,
        exclusions,
    })
}

fn inspect_render(root: &Path, row: &MetadataRow) -> Result<MidiSummary, String> {
    let path = resolve_midi(root, &row.midi_filename)?;
    let excerpt = excerpt_bounds_44100(row.duration_samples_44100, &row.split, &row.id)?;
    let midi = inspect_midi(
        &path,
        excerpt.start_sample,
        excerpt.end_sample,
        EXCERPT_SAMPLE_RATE,
    )?;
    if midi.source_first_raw_note_time_secs < -0.002
        || midi.source_last_raw_note_time_secs > row.duration + 0.002
    {
        return Err(format!(
            "annotation_outside_duration:first={:.9};last={:.9};duration={:.9}",
            midi.source_first_raw_note_time_secs, midi.source_last_raw_note_time_secs, row.duration
        ));
    }
    Ok(midi)
}

fn reject_cross_id_duplicate_midi(performances: &[Performance]) -> Result<(), String> {
    let mut midi_hashes = HashSet::new();
    for performance in performances {
        if !midi_hashes.insert(performance.midi.sha256.as_str()) {
            return Err(format!(
                "duplicate MIDI SHA-256 across performance IDs: {}",
                performance.midi.sha256
            ));
        }
    }
    Ok(())
}

fn validate_group(rows: &[MetadataRow]) -> Result<(), String> {
    let first = rows.first().ok_or("empty performance group")?;
    for row in &rows[1..] {
        let same = row.drummer == first.drummer
            && row.session == first.session
            && row.id == first.id
            && row.style == first.style
            && row.bpm.to_bits() == first.bpm.to_bits()
            && row.beat_type == first.beat_type
            && row.time_signature == first.time_signature
            && row.split == first.split;
        if !same {
            return Err(format!(
                "inconsistent metadata across kits for {}",
                first.id
            ));
        }
    }
    Ok(())
}

fn resolve_midi(root: &Path, relative: &str) -> Result<PathBuf, String> {
    let path = fs::canonicalize(root.join(relative))
        .map_err(|error| format!("missing_midi:{relative}:{error}"))?;
    if !path.starts_with(root) || !path.is_file() {
        return Err(format!("MIDI is outside root or not a file: {relative}"));
    }
    Ok(path)
}

fn validate_relative(value: &str, kind: &str, row: usize) -> Result<(), String> {
    if Path::new(value)
        .components()
        .any(|component| !matches!(component, Component::Normal(_)))
    {
        return Err(format!(
            "invalid {kind} relative path at row {row}: {value}"
        ));
    }
    Ok(())
}

fn positive_finite(value: &str, name: &str, row: usize) -> Result<f64, String> {
    let value = value
        .parse::<f64>()
        .map_err(|_| format!("invalid {name} at row {row}"))?;
    if !value.is_finite() || value <= 0.0 {
        return Err(format!("invalid {name} at row {row}"));
    }
    Ok(value)
}

#[cfg(test)]
#[path = "metadata_tests.rs"]
mod tests;
