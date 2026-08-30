use std::collections::HashSet;
use std::fs;
use std::path::{Component, Path};

use serde::Deserialize;

use super::contract::is_sha256;
use super::csv::{parse_line, parse_positive_duration_micros};
use super::io::sha256_bytes;

const DEVELOPMENT_HEADER: &str = "selection_rank,selection_key,fold,drummer,session,id,style,bpm,beat_type,time_signature,duration,split,midi_filename,audio_filename,kit_name,midi_sha256,raw_notes,compound_events,kick_only_events,hat_only_events,density_events_per_second";
const SYNTHETIC_SCHEMA: &str = "kirin-hypha-attack-midi-proxy-synthetic-fixture-v1";
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

#[derive(Clone, Copy, Debug, Deserialize, serde::Serialize, Eq, PartialEq)]
#[serde(rename_all = "snake_case")]
pub(crate) enum SourceKind {
    DevelopmentSelection,
    SyntheticFixture,
}

#[derive(Clone, Debug)]
pub(crate) struct SourceArtifact {
    pub(crate) kind: SourceKind,
    pub(crate) id: String,
    pub(crate) raw_sha256: String,
    pub(crate) tracks: Vec<SourceTrack>,
}

#[derive(Clone, Debug)]
pub(crate) struct SourceTrack {
    pub(crate) selection_rank: u32,
    pub(crate) fold: String,
    pub(crate) drummer: String,
    pub(crate) performance_id: String,
    pub(crate) style: String,
    pub(crate) beat_type: String,
    pub(crate) split: String,
    pub(crate) duration_micros: u64,
    pub(crate) midi_relative_path: String,
    pub(crate) audio_relative_path: String,
    pub(crate) kit_name: String,
    pub(crate) midi_sha256: String,
    pub(crate) kick_only_events: u64,
    pub(crate) hat_only_events: u64,
    pub(crate) proxy_onsets_micros: Option<Vec<u64>>,
}

pub(crate) fn read_development_selection(
    path: &Path,
    expected_sha256: &str,
) -> Result<SourceArtifact, String> {
    let bytes = read_pinned(path, expected_sha256)?;
    let text = std::str::from_utf8(&bytes).map_err(|error| format!("selection UTF-8: {error}"))?;
    let mut lines = text.lines();
    if lines.next().map(|line| line.trim_end_matches('\r')) != Some(DEVELOPMENT_HEADER) {
        return Err("unexpected development selection header".to_string());
    }
    let mut tracks = Vec::new();
    let mut ranks = HashSet::new();
    let mut render_keys = HashSet::new();
    let mut performance_ids = HashSet::new();
    let mut selection_keys = HashSet::new();
    let mut midi_paths = HashSet::new();
    let mut audio_paths = HashSet::new();
    for (index, raw_line) in lines.enumerate() {
        let row = index + 2;
        let line = raw_line.trim_end_matches('\r');
        if line.is_empty() {
            return Err(format!("blank development selection row {row}"));
        }
        let fields = parse_line(line).map_err(|error| format!("row {row}: {error}"))?;
        if fields.len() != 21 || fields.iter().any(String::is_empty) {
            return Err(format!(
                "development selection row {row} must have 21 nonempty fields"
            ));
        }
        let selection_rank = positive_u32(&fields[0], "selection_rank", row)?;
        if selection_rank as usize != tracks.len() + 1 {
            return Err(format!("selection_rank is not contiguous at row {row}"));
        }
        if !ranks.insert(selection_rank) {
            return Err(format!("duplicate selection_rank at row {row}"));
        }
        if !is_sha256(&fields[1]) {
            return Err(format!("invalid selection_key at row {row}"));
        }
        if !selection_keys.insert(fields[1].clone()) {
            return Err(format!("duplicate selection_key at row {row}"));
        }
        if !valid_fold(&fields[2]) {
            return Err(format!("invalid fold at row {row}: {}", fields[2]));
        }
        if !matches!(fields[11].as_str(), "train" | "validation") {
            return Err(format!(
                "row {row} is outside DRUM development train/validation: {}",
                fields[11]
            ));
        }
        let duration = parse_positive_duration_micros(&fields[10])
            .map_err(|error| format!("invalid duration at row {row}: {error}"))?;
        positive_f64(&fields[7], "bpm", row)?;
        if nonnegative_u64(&fields[16], "raw_notes", row)? == 0
            || nonnegative_u64(&fields[17], "compound_events", row)? == 0
        {
            return Err(format!("empty MIDI counts at row {row}"));
        }
        let kick_only_events = nonnegative_u64(&fields[18], "kick_only_events", row)?;
        let hat_only_events = nonnegative_u64(&fields[19], "hat_only_events", row)?;
        nonnegative_f64(&fields[20], "density_events_per_second", row)?;
        validate_relative(&fields[12], "MIDI", row)?;
        validate_relative(&fields[13], "audio", row)?;
        if !midi_paths.insert(fields[12].clone()) || !audio_paths.insert(fields[13].clone()) {
            return Err(format!("duplicate MIDI or audio path at row {row}"));
        }
        if !is_sha256(&fields[15]) {
            return Err(format!("invalid MIDI SHA-256 at row {row}"));
        }
        if !render_keys.insert((fields[5].clone(), fields[14].clone())) {
            return Err(format!("duplicate performance + kit at row {row}"));
        }
        if !performance_ids.insert(fields[5].clone()) {
            return Err(format!(
                "formal development selection must contain one render per performance ID; duplicate at row {row}"
            ));
        }
        tracks.push(SourceTrack {
            selection_rank,
            fold: fields[2].clone(),
            drummer: fields[3].clone(),
            performance_id: fields[5].clone(),
            style: fields[6].clone(),
            beat_type: fields[8].clone(),
            split: fields[11].clone(),
            duration_micros: duration,
            midi_relative_path: fields[12].clone(),
            audio_relative_path: fields[13].clone(),
            kit_name: fields[14].clone(),
            midi_sha256: fields[15].clone(),
            kick_only_events,
            hat_only_events,
            proxy_onsets_micros: None,
        });
    }
    if tracks.is_empty() {
        return Err("development selection has no rows".to_string());
    }
    validate_formal_development(&tracks)?;
    Ok(SourceArtifact {
        kind: SourceKind::DevelopmentSelection,
        id: path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("development-selection")
            .to_string(),
        raw_sha256: expected_sha256.to_string(),
        tracks,
    })
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct SyntheticFixture {
    schema: String,
    fixture_id: String,
    tracks: Vec<SyntheticTrack>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct SyntheticTrack {
    selection_rank: u32,
    fold: String,
    drummer: String,
    performance_id: String,
    duration_micros: u64,
    audio_relative_path: String,
    kit_name: String,
    midi_sha256: String,
    proxy_onsets_micros: Vec<u64>,
}

pub(crate) fn read_synthetic_fixture(
    path: &Path,
    expected_sha256: &str,
) -> Result<SourceArtifact, String> {
    let bytes = read_pinned(path, expected_sha256)?;
    let fixture: SyntheticFixture = serde_json::from_slice(&bytes)
        .map_err(|error| format!("invalid synthetic fixture JSON: {error}"))?;
    if fixture.schema != SYNTHETIC_SCHEMA || fixture.fixture_id.is_empty() {
        return Err("unexpected synthetic fixture schema or empty fixture_id".to_string());
    }
    let mut ranks = HashSet::new();
    let mut render_keys = HashSet::new();
    let mut tracks = Vec::new();
    for (index, track) in fixture.tracks.into_iter().enumerate() {
        if track.selection_rank == 0 || !ranks.insert(track.selection_rank) {
            return Err(format!("invalid synthetic selection_rank at item {index}"));
        }
        if !valid_fold(&track.fold)
            || track.drummer.is_empty()
            || track.performance_id.is_empty()
            || track.kit_name.is_empty()
            || track.duration_micros == 0
            || !is_sha256(&track.midi_sha256)
        {
            return Err(format!("invalid synthetic metadata at item {index}"));
        }
        validate_relative(&track.audio_relative_path, "audio", index + 1)?;
        if !render_keys.insert((track.performance_id.clone(), track.kit_name.clone())) {
            return Err(format!(
                "duplicate synthetic performance + kit at item {index}"
            ));
        }
        validate_onsets(&track.proxy_onsets_micros, track.duration_micros, index)?;
        tracks.push(SourceTrack {
            selection_rank: track.selection_rank,
            fold: track.fold,
            drummer: track.drummer,
            performance_id: track.performance_id,
            style: "synthetic".to_string(),
            beat_type: "synthetic".to_string(),
            split: "synthetic".to_string(),
            duration_micros: track.duration_micros,
            midi_relative_path: "synthetic/no-file.mid".to_string(),
            audio_relative_path: track.audio_relative_path,
            kit_name: track.kit_name,
            midi_sha256: track.midi_sha256,
            kick_only_events: 0,
            hat_only_events: 0,
            proxy_onsets_micros: Some(track.proxy_onsets_micros),
        });
    }
    if tracks.is_empty() {
        return Err("synthetic fixture has no tracks".to_string());
    }
    Ok(SourceArtifact {
        kind: SourceKind::SyntheticFixture,
        id: fixture.fixture_id,
        raw_sha256: expected_sha256.to_string(),
        tracks,
    })
}

fn read_pinned(path: &Path, expected_sha256: &str) -> Result<Vec<u8>, String> {
    if !is_sha256(expected_sha256) {
        return Err("expected source SHA-256 is invalid".to_string());
    }
    let bytes = fs::read(path)
        .map_err(|error| format!("cannot read pinned source {}: {error}", path.display()))?;
    let actual = sha256_bytes(&bytes);
    if actual != expected_sha256 {
        return Err(format!(
            "source SHA-256 mismatch: expected {expected_sha256}, got {actual}"
        ));
    }
    Ok(bytes)
}

fn validate_formal_development(tracks: &[SourceTrack]) -> Result<(), String> {
    const MINIMUM_IDS: usize = 60;
    const MINIMUM_DURATION_MICROS: u64 = 1_800_000_000;
    const MINIMUM_STYLES: usize = 8;
    const MINIMUM_KITS: usize = 8;

    if tracks.len() < MINIMUM_IDS {
        return Err(format!(
            "formal development requires at least {MINIMUM_IDS} unique performance IDs"
        ));
    }
    let total_duration = tracks
        .iter()
        .try_fold(0_u64, |total, track| {
            total.checked_add(track.duration_micros)
        })
        .ok_or("formal development duration overflow")?;
    if total_duration < MINIMUM_DURATION_MICROS {
        return Err(
            "formal development requires at least 1800 seconds unique duration".to_string(),
        );
    }
    let beat_count = tracks
        .iter()
        .filter(|track| track.beat_type == "beat")
        .count();
    let fill_count = tracks
        .iter()
        .filter(|track| track.beat_type == "fill")
        .count();
    if tracks
        .iter()
        .any(|track| !matches!(track.beat_type.as_str(), "beat" | "fill"))
        || beat_count * 10 < tracks.len() * 3
        || fill_count * 10 < tracks.len() * 3
    {
        return Err("formal development requires beat and fill at 30% or more each".to_string());
    }
    let styles = tracks
        .iter()
        .map(|track| track.style.split('/').next().unwrap_or(&track.style))
        .collect::<HashSet<_>>();
    if styles.len() < MINIMUM_STYLES || styles.contains("") {
        return Err(format!(
            "formal development requires at least {MINIMUM_STYLES} primary styles"
        ));
    }
    let kits = tracks
        .iter()
        .map(|track| track.kit_name.as_str())
        .collect::<HashSet<_>>();
    if kits.len() < MINIMUM_KITS {
        return Err(format!(
            "formal development requires at least {MINIMUM_KITS} kits"
        ));
    }
    let drummers = tracks
        .iter()
        .map(|track| track.drummer.as_str())
        .collect::<HashSet<_>>();
    if drummers != REQUIRED_DRUMMERS.into_iter().collect::<HashSet<_>>() {
        return Err(
            "formal development must contain every E-GMD v1.0.0 train/validation drummer"
                .to_string(),
        );
    }
    let kick_only_events = tracks
        .iter()
        .try_fold(0_u64, |total, track| {
            total.checked_add(track.kick_only_events)
        })
        .ok_or("formal development kick-only count overflow")?;
    let hat_only_events = tracks
        .iter()
        .try_fold(0_u64, |total, track| {
            total.checked_add(track.hat_only_events)
        })
        .ok_or("formal development hat-only count overflow")?;
    if kick_only_events < 200 || hat_only_events < 200 {
        return Err(
            "formal development requires at least 200 kick-only and 200 hat-only compound events"
                .to_string(),
        );
    }
    let folds = tracks
        .iter()
        .map(|track| track.fold.as_str())
        .collect::<HashSet<_>>();
    if folds != HashSet::from(["0", "1", "2", "3", "4"]) {
        return Err("formal development requires all five folds 0..4".to_string());
    }
    Ok(())
}

fn validate_onsets(onsets: &[u64], duration: u64, index: usize) -> Result<(), String> {
    if onsets.is_empty()
        || onsets.iter().any(|&onset| onset >= duration)
        || onsets.windows(2).any(|pair| pair[0] >= pair[1])
    {
        return Err(format!("invalid synthetic proxy onsets at item {index}"));
    }
    Ok(())
}

fn validate_relative(value: &str, kind: &str, row: usize) -> Result<(), String> {
    let path = Path::new(value);
    if path.is_absolute()
        || path
            .components()
            .any(|component| !matches!(component, Component::Normal(_)))
    {
        return Err(format!(
            "invalid {kind} relative path at row {row}: {value}"
        ));
    }
    Ok(())
}

fn valid_fold(value: &str) -> bool {
    matches!(value, "0" | "1" | "2" | "3" | "4")
}

fn positive_u32(value: &str, name: &str, row: usize) -> Result<u32, String> {
    let parsed = value
        .parse::<u32>()
        .map_err(|_| format!("invalid {name} at row {row}"))?;
    if parsed == 0 {
        Err(format!("invalid {name} at row {row}"))
    } else {
        Ok(parsed)
    }
}

fn nonnegative_u64(value: &str, name: &str, row: usize) -> Result<u64, String> {
    value
        .parse::<u64>()
        .map_err(|_| format!("invalid {name} at row {row}"))
}

fn positive_f64(value: &str, name: &str, row: usize) -> Result<f64, String> {
    let parsed = value
        .parse::<f64>()
        .map_err(|_| format!("invalid {name} at row {row}"))?;
    if parsed.is_finite() && parsed > 0.0 {
        Ok(parsed)
    } else {
        Err(format!("invalid {name} at row {row}"))
    }
}

fn nonnegative_f64(value: &str, name: &str, row: usize) -> Result<f64, String> {
    let parsed = value
        .parse::<f64>()
        .map_err(|_| format!("invalid {name} at row {row}"))?;
    if parsed.is_finite() && parsed >= 0.0 {
        Ok(parsed)
    } else {
        Err(format!("invalid {name} at row {row}"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn committed_synthetic_fixture_is_pinned_and_valid() {
        let path = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("examples/transient_proxy_audit/fixtures/synthetic_proxy_audit_v1.json");
        let bytes = fs::read(&path).unwrap();
        let source = read_synthetic_fixture(&path, &sha256_bytes(&bytes)).unwrap();
        assert_eq!(source.kind, SourceKind::SyntheticFixture);
        assert_eq!(source.tracks.len(), 13);
        assert!(read_synthetic_fixture(&path, &"0".repeat(64)).is_err());
    }

    #[test]
    fn development_selection_must_reproduce_formal_selector_quotas() {
        let directory = tempdir().unwrap();
        let path = directory.path().join("development.csv");
        let mut csv = format!("{DEVELOPMENT_HEADER}\n");
        for index in 0..60 {
            let rank = index + 1;
            let beat_type = if index < 30 { "beat" } else { "fill" };
            let split = if index < 6 { "validation" } else { "train" };
            csv.push_str(&format!(
                "{rank},{rank:064x},{fold},{drummer},session,id{rank},style{style},120,{beat_type},4-4,30.000000000000001,{split},midi/{rank}.mid,audio/{rank}.wav,kit{kit},{midi_hash:064x},10,10,4,4,0.333333333\n",
                fold = index % 5,
                drummer = REQUIRED_DRUMMERS[index % REQUIRED_DRUMMERS.len()],
                style = index % 8,
                kit = index % 8,
                midi_hash = rank + 1_000,
            ));
        }
        fs::write(&path, &csv).unwrap();
        let source = read_development_selection(&path, &sha256_bytes(csv.as_bytes())).unwrap();
        assert_eq!(source.tracks.len(), 60);

        let short = csv.lines().take(60).collect::<Vec<_>>().join("\n") + "\n";
        fs::write(&path, &short).unwrap();
        assert!(
            read_development_selection(&path, &sha256_bytes(short.as_bytes()))
                .unwrap_err()
                .contains("60 unique")
        );
    }
}
