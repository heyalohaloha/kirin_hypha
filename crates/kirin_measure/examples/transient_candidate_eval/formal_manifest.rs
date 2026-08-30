use std::collections::{BTreeSet, HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};

use super::{
    parse_csv_line, positive_finite, resolve_input, FormalSelectionMetadata, Selection,
    SelectionManifest,
};
use crate::contract::{sha256_bytes, FormalAuthorization};
use crate::drum_excerpt::{
    decimal_seconds_to_samples_half_up, excerpt_bounds_44100, performance_rank_key,
    EXCERPT_SAMPLE_RATE,
};

const DEVELOPMENT_HEADER: &str = "selection_rank,selection_key,fold,drummer,session,id,style,bpm,beat_type,time_signature,source_duration_seconds,split,midi_filename,audio_filename,kit_name,midi_sha256,excerpt_start_sample_44100,excerpt_end_sample_44100,excerpt_raw_notes,excerpt_compound_events,excerpt_kick_only_events,excerpt_hat_only_events,excerpt_density_events_per_second";
const FOLDS_HEADER: &str = "id,fold,drummer,session,lodo_holdout,loso_holdout,selection_rank";
const FORMAL_PERFORMANCE_IDS: usize = 290;
const MIN_EXCERPT_DURATION_SAMPLES_44100: u64 = 79_380_000;
const REQUIRED_DRUMMERS: [&str; 9] = [
    "drummer1",
    "drummer10",
    "drummer3",
    "drummer4",
    "drummer5",
    "drummer6",
    "drummer7",
    "drummer8",
    "drummer9",
];

#[derive(Debug)]
#[allow(dead_code)] // Retained for a future authorized result provenance record.
pub(crate) struct FormalSelectionManifest {
    pub(crate) selection: SelectionManifest,
    pub(crate) folds_path: PathBuf,
    pub(crate) folds_sha256: String,
}

pub(crate) fn read_formal_selection(
    authorization: &FormalAuthorization,
    root: &Path,
    manifest_path: &Path,
    folds_path: &Path,
) -> Result<FormalSelectionManifest, String> {
    let expected_manifest_sha256 = authorization.manifest_sha256();
    let expected_folds_sha256 = authorization.folds_sha256();
    let receipts = authorization.receipt_sha256();
    for (name, digest) in [
        ("formal manifest", expected_manifest_sha256),
        ("formal folds", expected_folds_sha256),
        ("formal authorization", authorization.authorization_sha256()),
        ("formal authorization chain", authorization.chain_sha256()),
        (
            "development selection receipt",
            &receipts.development_selection,
        ),
        (
            "MIDI archive member receipt",
            &receipts.midi_archive_members,
        ),
        ("audio ingest receipt", &receipts.audio_ingest),
        ("fold balance receipt", &receipts.fold_balance),
        ("blind proxy audit receipt", &receipts.blind_proxy_audit),
        ("candidate plan receipt", &receipts.candidate_plan),
    ] {
        require_sha256(digest, name)?;
    }
    let root = fs::canonicalize(root).map_err(|error| format!("dataset root: {error}"))?;
    if !root.is_dir() {
        return Err(format!(
            "dataset root is not a directory: {}",
            root.display()
        ));
    }
    let (manifest_path, manifest_bytes) = read_pinned(
        manifest_path,
        expected_manifest_sha256,
        "formal development manifest",
    )?;
    let entries = parse_development_rows(&root, &manifest_bytes)?;
    validate_development_minima(&entries)?;
    let (folds_path, folds_bytes) = read_pinned(folds_path, expected_folds_sha256, "formal folds")?;
    validate_fold_metadata(&entries, &folds_bytes)?;
    Ok(FormalSelectionManifest {
        selection: SelectionManifest {
            path: manifest_path,
            sha256: expected_manifest_sha256.to_string(),
            entries,
        },
        folds_path,
        folds_sha256: expected_folds_sha256.to_string(),
    })
}

fn parse_development_rows(root: &Path, bytes: &[u8]) -> Result<Vec<Selection>, String> {
    let text = std::str::from_utf8(bytes)
        .map_err(|error| format!("formal development manifest UTF-8: {error}"))?;
    let mut lines = text.lines();
    if lines.next().map(|line| line.trim_end_matches('\r')) != Some(DEVELOPMENT_HEADER) {
        return Err("unexpected formal development manifest header".to_string());
    }
    let mut entries = Vec::new();
    let mut selection_keys = HashSet::new();
    let mut performance_ids = HashSet::new();
    let mut input_paths = HashSet::new();
    for (index, raw_line) in lines.enumerate() {
        let row = index + 2;
        let line = raw_line.trim_end_matches('\r');
        if line.is_empty() {
            return Err(format!("blank formal development manifest row {row}"));
        }
        let fields = parse_csv_line(line).map_err(|error| format!("row {row}: {error}"))?;
        if fields.len() != 23 || fields.iter().any(String::is_empty) {
            return Err(format!(
                "formal development row {row} must have 23 nonempty fields"
            ));
        }
        let selection_rank = positive_u32(&fields[0], "selection_rank", row)?;
        if selection_rank as usize != entries.len() + 1 {
            return Err(format!("selection_rank is not contiguous at row {row}"));
        }
        require_sha256(&fields[1], &format!("selection_key at row {row}"))?;
        let expected_selection_key = performance_rank_key(&fields[11], &fields[5])
            .map_err(|error| format!("selection_key identity at row {row}: {error}"))?;
        if fields[1] != expected_selection_key {
            return Err(format!(
                "selection_key does not match the pinned rank mapping at row {row}"
            ));
        }
        if !selection_keys.insert(fields[1].clone()) {
            return Err(format!("duplicate selection_key at row {row}"));
        }
        let fold = parse_fold(&fields[2], row)?;
        if !performance_ids.insert(fields[5].clone()) {
            return Err(format!(
                "formal development requires one render per performance ID; duplicate at row {row}"
            ));
        }
        let bpm = positive_finite(&fields[7], "bpm", row)?;
        if !matches!(fields[8].as_str(), "beat" | "fill") {
            return Err(format!("invalid beat_type at row {row}"));
        }
        let declared_duration = positive_finite(&fields[10], "duration", row)?;
        if !matches!(fields[11].as_str(), "train" | "validation") {
            return Err(format!("formal split leakage at row {row}: {}", fields[11]));
        }
        require_sha256(&fields[15], &format!("MIDI SHA-256 at row {row}"))?;
        let excerpt_start_sample_44100 =
            nonnegative_u64(&fields[16], "excerpt_start_sample_44100", row)?;
        let excerpt_end_sample_44100 =
            nonnegative_u64(&fields[17], "excerpt_end_sample_44100", row)?;
        let source_samples =
            decimal_seconds_to_samples_half_up(&fields[10], u64::from(EXCERPT_SAMPLE_RATE))
                .map_err(|error| format!("source duration at row {row}: {error}"))?;
        let expected_excerpt = excerpt_bounds_44100(source_samples, &fields[11], &fields[5])
            .map_err(|error| format!("excerpt identity at row {row}: {error}"))?;
        if excerpt_start_sample_44100 != expected_excerpt.start_sample
            || excerpt_end_sample_44100 != expected_excerpt.end_sample
        {
            return Err(format!(
                "formal excerpt does not match the pinned mapping at row {row}: source={source_samples} expected=[{},{}) actual=[{excerpt_start_sample_44100},{excerpt_end_sample_44100})",
                expected_excerpt.start_sample, expected_excerpt.end_sample,
            ));
        }
        let raw_notes = nonnegative_usize(&fields[18], "excerpt_raw_notes", row)?;
        let compound_events = nonnegative_usize(&fields[19], "excerpt_compound_events", row)?;
        let kick_only_events = nonnegative_usize(&fields[20], "excerpt_kick_only_events", row)?;
        let hat_only_events = nonnegative_usize(&fields[21], "excerpt_hat_only_events", row)?;
        let density = nonnegative_finite(&fields[22], "excerpt_density", row)?;
        let excerpt_duration = (excerpt_end_sample_44100 - excerpt_start_sample_44100) as f64
            / f64::from(EXCERPT_SAMPLE_RATE);
        let expected_density = compound_events as f64 / excerpt_duration;
        if (density - expected_density).abs() > 1.1e-9 {
            return Err(format!(
                "density does not match excerpt compounds at row {row}"
            ));
        }
        let midi = resolve_input(root, &fields[12], "MIDI")?;
        let audio = resolve_input(root, &fields[13], "audio")?;
        if !input_paths.insert(midi.clone()) || !input_paths.insert(audio.clone()) {
            return Err(format!("duplicate formal input path at row {row}"));
        }
        entries.push(Selection {
            drummer: fields[3].clone(),
            session: fields[4].clone(),
            id: fields[5].clone(),
            style: fields[6].clone(),
            bpm,
            beat_type: fields[8].clone(),
            time_signature: fields[9].clone(),
            declared_duration,
            split: fields[11].clone(),
            midi,
            audio,
            kit_name: fields[14].clone(),
            formal: Some(FormalSelectionMetadata {
                selection_rank,
                selection_key: fields[1].clone(),
                fold,
                expected_midi_sha256: fields[15].clone(),
                declared_excerpt_raw_notes: raw_notes,
                declared_excerpt_compound_events: compound_events,
                declared_excerpt_kick_only_events: kick_only_events,
                declared_excerpt_hat_only_events: hat_only_events,
                declared_excerpt_density_events_per_second: density,
                excerpt_start_sample_44100,
                excerpt_end_sample_44100,
            }),
        });
    }
    if entries.is_empty() {
        return Err("formal development manifest has no rows".to_string());
    }
    Ok(entries)
}

fn validate_development_minima(entries: &[Selection]) -> Result<(), String> {
    if entries.len() != FORMAL_PERFORMANCE_IDS {
        return Err(format!(
            "formal development v2 requires exactly {FORMAL_PERFORMANCE_IDS} unique performance IDs"
        ));
    }
    let duration_samples = entries.iter().try_fold(0_u64, |total, entry| {
        let length = {
            let formal = entry.formal.as_ref().unwrap();
            formal
                .excerpt_end_sample_44100
                .checked_sub(formal.excerpt_start_sample_44100)
                .ok_or("formal excerpt sample range is reversed")?
        };
        total
            .checked_add(length)
            .ok_or("formal excerpt duration sample sum overflow")
    })?;
    if duration_samples < MIN_EXCERPT_DURATION_SAMPLES_44100 {
        return Err(format!(
            "formal development requires at least {MIN_EXCERPT_DURATION_SAMPLES_44100} excerpt samples at 44.1 kHz"
        ));
    }
    let beat = entries
        .iter()
        .filter(|entry| entry.beat_type == "beat")
        .count();
    let fill = entries
        .iter()
        .filter(|entry| entry.beat_type == "fill")
        .count();
    if beat * 10 < entries.len() * 3 || fill * 10 < entries.len() * 3 {
        return Err("formal development requires beat and fill at 30% or more each".to_string());
    }
    let styles = entries
        .iter()
        .map(|entry| entry.style.split('/').next().unwrap_or(&entry.style))
        .collect::<HashSet<_>>();
    let kits = entries
        .iter()
        .map(|entry| entry.kit_name.as_str())
        .collect::<HashSet<_>>();
    let drummers = entries
        .iter()
        .map(|entry| entry.drummer.as_str())
        .collect::<BTreeSet<_>>();
    let required_drummers = REQUIRED_DRUMMERS.into_iter().collect::<BTreeSet<_>>();
    if styles.len() < 8 || styles.contains("") || kits.len() < 8 || drummers != required_drummers {
        return Err("formal development style/kit/drummer quotas are not satisfied".to_string());
    }
    let folds = entries
        .iter()
        .map(|entry| entry.formal.as_ref().unwrap().fold)
        .collect::<BTreeSet<_>>();
    let fold_sizes = (0_u8..5)
        .map(|fold| {
            entries
                .iter()
                .filter(|entry| entry.formal.as_ref().unwrap().fold == fold)
                .count()
        })
        .collect::<BTreeSet<_>>();
    if folds != BTreeSet::from([0, 1, 2, 3, 4]) || fold_sizes.len() != 1 {
        return Err(
            "formal development five-fold coverage or equal fold size is not satisfied".to_string(),
        );
    }
    Ok(())
}

fn validate_fold_metadata(entries: &[Selection], bytes: &[u8]) -> Result<(), String> {
    let text =
        std::str::from_utf8(bytes).map_err(|error| format!("fold metadata UTF-8: {error}"))?;
    let mut lines = text.lines();
    if lines.next().map(|line| line.trim_end_matches('\r')) != Some(FOLDS_HEADER) {
        return Err("unexpected formal fold metadata header".to_string());
    }
    let by_id = entries
        .iter()
        .map(|entry| (entry.id.as_str(), entry))
        .collect::<HashMap<_, _>>();
    let mut seen = HashSet::new();
    for (index, raw_line) in lines.enumerate() {
        let row = index + 2;
        let line = raw_line.trim_end_matches('\r');
        if line.is_empty() {
            return Err(format!("blank fold metadata row {row}"));
        }
        let fields = parse_csv_line(line).map_err(|error| format!("fold row {row}: {error}"))?;
        if fields.len() != 7 || fields.iter().any(String::is_empty) {
            return Err(format!(
                "fold metadata row {row} must have 7 nonempty fields"
            ));
        }
        let entry = by_id
            .get(fields[0].as_str())
            .ok_or_else(|| format!("fold metadata has unknown performance ID at row {row}"))?;
        if !seen.insert(fields[0].clone()) {
            return Err(format!("duplicate fold metadata ID at row {row}"));
        }
        let formal = entry.formal.as_ref().unwrap();
        let rank = positive_u32(&fields[6], "fold selection_rank", row)?;
        if parse_fold(&fields[1], row)? != formal.fold
            || fields[2] != entry.drummer
            || fields[3] != entry.session
            || fields[4] != entry.drummer
            || fields[5] != entry.session
            || rank != formal.selection_rank
        {
            return Err(format!(
                "fold metadata disagrees with manifest at row {row}"
            ));
        }
    }
    if seen.len() != entries.len() {
        return Err("fold metadata does not cover every manifest ID exactly once".to_string());
    }
    Ok(())
}

fn read_pinned(path: &Path, expected: &str, name: &str) -> Result<(PathBuf, Vec<u8>), String> {
    let bytes = fs::read(path)
        .map_err(|error| format!("cannot read {name} {}: {error}", path.display()))?;
    let actual = sha256_bytes(&bytes);
    if actual != expected {
        return Err(format!(
            "{name} SHA-256 mismatch: expected {expected}, got {actual}"
        ));
    }
    let canonical =
        fs::canonicalize(path).map_err(|error| format!("cannot resolve {name}: {error}"))?;
    Ok((canonical, bytes))
}

fn parse_fold(value: &str, row: usize) -> Result<u8, String> {
    value
        .parse::<u8>()
        .ok()
        .filter(|fold| *fold < 5)
        .ok_or_else(|| format!("invalid fold at row {row}: {value}"))
}

fn positive_u32(value: &str, name: &str, row: usize) -> Result<u32, String> {
    value
        .parse::<u32>()
        .ok()
        .filter(|parsed| *parsed > 0)
        .ok_or_else(|| format!("invalid {name} at row {row}: {value}"))
}

fn nonnegative_usize(value: &str, name: &str, row: usize) -> Result<usize, String> {
    value
        .parse::<usize>()
        .map_err(|_| format!("invalid {name} at row {row}: {value}"))
}

fn nonnegative_u64(value: &str, name: &str, row: usize) -> Result<u64, String> {
    value
        .parse::<u64>()
        .map_err(|_| format!("invalid {name} at row {row}: {value}"))
}

fn nonnegative_finite(value: &str, name: &str, row: usize) -> Result<f64, String> {
    let parsed = value
        .parse::<f64>()
        .map_err(|_| format!("invalid {name} at row {row}: {value}"))?;
    if parsed.is_finite() && parsed >= 0.0 {
        Ok(parsed)
    } else {
        Err(format!("invalid {name} at row {row}: {value}"))
    }
}

fn require_sha256(value: &str, name: &str) -> Result<(), String> {
    if value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        Ok(())
    } else {
        Err(format!("{name} must be 64 lowercase hex digits"))
    }
}

#[cfg(test)]
#[path = "formal_manifest_tests.rs"]
mod tests;
