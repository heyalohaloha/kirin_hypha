//! Candidate-blind listening pack for the B-555 ATTACK kick diagnosis.
//!
//! The pack exposes only opaque clip IDs and a fixed MIDI reference position.
//! Detector status, performance identity, and diagnostic class stay in a
//! separate key file. This tool does not tune or authorize ATTACK.

use std::collections::{BTreeMap, HashMap};
use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process;

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/contract.rs"]
mod contract;
#[path = "transient_drum_excerpt/mod.rs"]
mod drum_excerpt;
#[path = "transient_drum_midi/mod.rs"]
mod drum_midi;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/input.rs"]
mod input;
#[path = "transient_attack_kick_listening_pack/review.rs"]
mod review;

use input::{read_development_pilot_selection, read_mono_pcm_wav, Selection};

const SAMPLE_RATE: u32 = 44_100;
const PRE_SAMPLES: usize = 8_820;
const POST_SAMPLES: usize = 13_230;
const EVENTS_PER_CELL: usize = 5;
const TARGET_DRUMMERS: [&str; 3] = ["drummer4", "drummer5", "drummer7"];
const TARGET_CLASSES: [&str; 3] = [
    "matched",
    "eligible_peak_25_to_50_ms",
    "no_eligible_peak_within_50_ms",
];

#[derive(Debug)]
struct Cli {
    root: PathBuf,
    manifest: PathBuf,
    folds: PathBuf,
    diagnostic: PathBuf,
    output_dir: PathBuf,
    key: PathBuf,
}

#[derive(Debug, Deserialize)]
struct DiagnosticInput {
    schema: String,
    profile: String,
    candidate_id: String,
    events: Vec<DiagnosticEvent>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
struct DiagnosticEvent {
    performance_id: String,
    drummer: String,
    label_time_micros: u64,
    matched: bool,
    miss_class: String,
    nearest_selected_error_ms: Option<f64>,
    nearest_eligible_error_ms: Option<f64>,
    midi_velocity_max: u8,
    attack_peak_dbfs: f64,
    attack_rise_db: f64,
}

#[derive(Serialize)]
struct KeyArtifact {
    schema: &'static str,
    status: &'static str,
    candidate_id: String,
    pack_sha256: String,
    review_sha256: String,
    candidate_status_exposed_in_pack: bool,
    events: Vec<KeyEvent>,
}

#[derive(Serialize)]
struct KeyEvent {
    clip_id: String,
    clip_sha256: String,
    diagnostic: DiagnosticEvent,
}

#[derive(Clone)]
struct ChosenEvent {
    clip_id: String,
    event: DiagnosticEvent,
}

fn main() {
    run().unwrap_or_else(|error| {
        eprintln!("ATTACK kick listening pack failed: {error}");
        process::exit(1);
    });
}

fn run() -> Result<(), String> {
    let cli = Cli::parse(std::env::args_os().skip(1))?;
    if cli.output_dir.exists() || cli.key.exists() {
        return Err("output directory and key must not already exist".to_string());
    }
    let root = fs::canonicalize(&cli.root).map_err(|error| format!("dataset root: {error}"))?;
    let selection = read_development_pilot_selection(&root, &cli.manifest, &cli.folds)?;
    let selections = selection
        .selection
        .entries
        .into_iter()
        .map(|entry| (entry.id.clone(), entry))
        .collect::<HashMap<_, _>>();
    let diagnostic = read_diagnostic(&cli.diagnostic)?;
    let chosen = choose_events(&diagnostic.events, &selections)?;

    fs::create_dir(&cli.output_dir)
        .map_err(|error| format!("cannot create pack directory: {error}"))?;
    let clips_dir = cli.output_dir.join("clips");
    fs::create_dir(&clips_dir)
        .map_err(|error| format!("cannot create clips directory: {error}"))?;

    let mut key_events = Vec::with_capacity(chosen.len());
    let mut manifest = String::from("clip_id,filename,sha256,target_reference_ms,duration_ms\n");
    for (performance_id, events) in group_by_performance(&chosen) {
        let source = selections.get(&performance_id).ok_or_else(|| {
            format!("diagnostic performance is absent from manifest: {performance_id}")
        })?;
        let wav = read_mono_pcm_wav(&source.audio)?;
        if wav.metadata.sample_rate != SAMPLE_RATE || wav.metadata.channels != 1 {
            return Err(format!(
                "listening source is not mono 44.1 kHz: {performance_id}"
            ));
        }
        for chosen_event in events {
            let center = micros_to_sample(chosen_event.event.label_time_micros)?;
            let start = center
                .checked_sub(PRE_SAMPLES)
                .ok_or_else(|| format!("insufficient pre-context: {}", chosen_event.clip_id))?;
            let end = center
                .checked_add(POST_SAMPLES)
                .ok_or("clip end overflow")?;
            let clip = wav
                .samples
                .get(start..end)
                .ok_or_else(|| format!("insufficient post-context: {}", chosen_event.clip_id))?;
            let bytes = encode_pcm16_wav(clip)?;
            let clip_sha256 = sha256_bytes(&bytes);
            let filename = format!("{}.wav", chosen_event.clip_id);
            publish_create_new(&clips_dir.join(&filename), &bytes)?;
            manifest.push_str(&format!(
                "{},{},{},200,500\n",
                chosen_event.clip_id, filename, clip_sha256
            ));
            key_events.push(KeyEvent {
                clip_id: chosen_event.clip_id.clone(),
                clip_sha256,
                diagnostic: chosen_event.event.clone(),
            });
        }
    }
    key_events.sort_by(|left, right| left.clip_id.cmp(&right.clip_id));
    publish_create_new(&cli.output_dir.join("manifest.csv"), manifest.as_bytes())?;
    publish_create_new(
        &cli.output_dir.join("annotation.csv"),
        annotation_template(&chosen).as_bytes(),
    )?;
    publish_create_new(
        &cli.output_dir.join("README.txt"),
        instructions().as_bytes(),
    )?;
    let clip_ids = chosen
        .iter()
        .map(|event| event.clip_id.clone())
        .collect::<Vec<_>>();
    let review_bytes = review::render_review_html(&clip_ids)?;
    let review_sha256 = sha256_bytes(&review_bytes);
    publish_create_new(&cli.output_dir.join("review.html"), &review_bytes)?;
    let pack_sha256 = pack_digest(&manifest, &key_events);
    let key = KeyArtifact {
        schema: "kirin-hypha-attack-kick-listening-key-v1",
        status: "development_acoustic_audit_pending_not_candidate_tuning",
        candidate_id: diagnostic.candidate_id,
        pack_sha256,
        review_sha256,
        candidate_status_exposed_in_pack: false,
        events: key_events,
    };
    let mut key_bytes = serde_json::to_vec_pretty(&key)
        .map_err(|error| format!("cannot serialize listening key: {error}"))?;
    key_bytes.push(b'\n');
    publish_create_new(&cli.key, &key_bytes)?;
    println!(
        "ATTACK candidate-blind listening pack ready: clips={} pack={} key={}",
        chosen.len(),
        cli.output_dir.display(),
        cli.key.display()
    );
    Ok(())
}

fn read_diagnostic(path: &Path) -> Result<DiagnosticInput, String> {
    let bytes = fs::read(path)
        .map_err(|error| format!("cannot read diagnostic {}: {error}", path.display()))?;
    let input: DiagnosticInput = serde_json::from_slice(&bytes)
        .map_err(|error| format!("invalid diagnostic JSON: {error}"))?;
    if input.schema != "kirin-hypha-attack-kick-diagnostic-v1"
        || input.profile != "DRUM"
        || input.candidate_id != "b553-superflux-pilot-best"
    {
        return Err("listening pack requires the exact B-555 DRUM diagnostic".to_string());
    }
    Ok(input)
}

fn choose_events(
    events: &[DiagnosticEvent],
    selections: &HashMap<String, Selection>,
) -> Result<Vec<ChosenEvent>, String> {
    let mut selected = Vec::new();
    for drummer in TARGET_DRUMMERS {
        for class in TARGET_CLASSES {
            let mut pool = events
                .iter()
                .filter(|event| event.drummer == drummer && event.miss_class == class)
                .filter(|event| has_context(event, selections))
                .cloned()
                .collect::<Vec<_>>();
            pool.sort_by_key(selection_key);
            let mut cell = Vec::<DiagnosticEvent>::new();
            for event in pool {
                let separated = cell.iter().all(|chosen| {
                    chosen.performance_id != event.performance_id
                        || chosen.label_time_micros.abs_diff(event.label_time_micros) >= 500_000
                });
                if separated {
                    cell.push(event);
                }
                if cell.len() == EVENTS_PER_CELL {
                    break;
                }
            }
            if cell.len() != EVENTS_PER_CELL {
                return Err(format!(
                    "listening cell {drummer}/{class} has only {} separated events",
                    cell.len()
                ));
            }
            selected.extend(cell);
        }
    }
    selected.sort_by_key(blind_key);
    Ok(selected
        .into_iter()
        .enumerate()
        .map(|(index, event)| ChosenEvent {
            clip_id: format!("K{:03}", index + 1),
            event,
        })
        .collect())
}

fn has_context(event: &DiagnosticEvent, selections: &HashMap<String, Selection>) -> bool {
    let Some(selection) = selections.get(&event.performance_id) else {
        return false;
    };
    event.label_time_micros >= 200_000
        && event.label_time_micros.saturating_add(300_000)
            <= (selection.declared_duration * 1_000_000.0).round() as u64
}

fn selection_key(event: &DiagnosticEvent) -> String {
    digest_fields("attack-kick-listening-cell-v1", event)
}

fn blind_key(event: &DiagnosticEvent) -> String {
    digest_fields("attack-kick-listening-order-v1", event)
}

fn digest_fields(domain: &str, event: &DiagnosticEvent) -> String {
    sha256_bytes(
        format!(
            "{domain}\0{}\0{}\0{}",
            event.performance_id, event.label_time_micros, event.miss_class
        )
        .as_bytes(),
    )
}

fn group_by_performance(chosen: &[ChosenEvent]) -> BTreeMap<String, Vec<&ChosenEvent>> {
    let mut grouped = BTreeMap::new();
    for event in chosen {
        grouped
            .entry(event.event.performance_id.clone())
            .or_insert_with(Vec::new)
            .push(event);
    }
    grouped
}

fn micros_to_sample(micros: u64) -> Result<usize, String> {
    usize::try_from((u128::from(micros) * u128::from(SAMPLE_RATE) + 500_000) / 1_000_000)
        .map_err(|_| "sample index does not fit usize".to_string())
}

fn encode_pcm16_wav(samples: &[f32]) -> Result<Vec<u8>, String> {
    let data_bytes = samples
        .len()
        .checked_mul(2)
        .ok_or("WAV data size overflow")?;
    let riff_size = u32::try_from(data_bytes.checked_add(36).ok_or("WAV RIFF overflow")?)
        .map_err(|_| "WAV clip is too large".to_string())?;
    let data_size = u32::try_from(data_bytes).map_err(|_| "WAV data is too large".to_string())?;
    let mut bytes = Vec::with_capacity(data_bytes + 44);
    bytes.extend_from_slice(b"RIFF");
    bytes.extend_from_slice(&riff_size.to_le_bytes());
    bytes.extend_from_slice(b"WAVEfmt ");
    bytes.extend_from_slice(&16_u32.to_le_bytes());
    bytes.extend_from_slice(&1_u16.to_le_bytes());
    bytes.extend_from_slice(&1_u16.to_le_bytes());
    bytes.extend_from_slice(&SAMPLE_RATE.to_le_bytes());
    bytes.extend_from_slice(&(SAMPLE_RATE * 2).to_le_bytes());
    bytes.extend_from_slice(&2_u16.to_le_bytes());
    bytes.extend_from_slice(&16_u16.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&data_size.to_le_bytes());
    for sample in samples {
        let value = (sample * 32_768.0).round().clamp(-32_768.0, 32_767.0) as i16;
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    Ok(bytes)
}

fn annotation_template(chosen: &[ChosenEvent]) -> String {
    let mut csv = String::from(
        "clip_id,target_reference_ms,audible_attack_yes_no_uncertain,nearest_attack_ms,confidence_1_to_3,notes\n",
    );
    for event in chosen {
        csv.push_str(&format!("{},200,,,,\n", event.clip_id));
    }
    csv
}

fn instructions() -> &'static str {
    "ATTACK kick 聴取確認\n\nreview.htmlをブラウザで開いてください。入力は自動保存され、途中または完了TSVを画面から保存できます。annotation.csvは予備の手入力用です。\n再生音量は全clipで固定してください。各clipは500 msで、MIDI上のkick位置は200 msです。\n全行を終えるまで、別置きのkeyファイルを開かないでください。\n各clipの150–250 ms内に、明瞭に聴こえるattackがあるかを判定します。\nyes / no / uncertainを記入し、yesの場合はclip先頭から最寄りattackまでのmsも記入してください。\n確信度は1=低〜5=高です。繰り返し再生は可、clipごとの音量正規化は不可です。\n"
}

fn pack_digest(manifest: &str, events: &[KeyEvent]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(b"attack-kick-listening-pack-v1\0");
    hasher.update(manifest.as_bytes());
    for event in events {
        hasher.update(event.clip_id.as_bytes());
        hasher.update([0]);
        hasher.update(event.clip_sha256.as_bytes());
        hasher.update([0]);
    }
    hex::encode(hasher.finalize())
}

fn sha256_bytes(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

fn publish_create_new(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|error| format!("cannot create {}: {error}", path.display()))?;
    file.write_all(bytes)
        .and_then(|()| file.sync_all())
        .map_err(|error| format!("cannot publish {}: {error}", path.display()))
}

impl Cli {
    fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut values = BTreeMap::<String, OsString>::new();
        let mut arguments = arguments.into_iter();
        while let Some(flag) = arguments.next() {
            let flag = flag.to_str().ok_or("CLI flag is not UTF-8")?.to_string();
            let value = arguments
                .next()
                .ok_or_else(|| format!("missing value for {flag}"))?;
            if values.insert(flag.clone(), value).is_some() {
                return Err(format!("duplicate CLI flag: {flag}"));
            }
        }
        if take_string(&mut values, "--profile")? != "DRUM" {
            return Err("listening pack permits only DRUM".to_string());
        }
        let cli = Self {
            root: take_path(&mut values, "--root")?,
            manifest: take_path(&mut values, "--manifest")?,
            folds: take_path(&mut values, "--folds")?,
            diagnostic: take_path(&mut values, "--diagnostic")?,
            output_dir: take_path(&mut values, "--output-dir")?,
            key: take_path(&mut values, "--key")?,
        };
        if let Some(flag) = values.keys().next() {
            return Err(format!("unknown CLI flag: {flag}"));
        }
        Ok(cli)
    }
}

fn take_path(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<PathBuf, String> {
    values
        .remove(flag)
        .map(PathBuf::from)
        .ok_or_else(|| format!("missing {flag}"))
}

fn take_string(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<String, String> {
    values
        .remove(flag)
        .ok_or_else(|| format!("missing {flag}"))?
        .into_string()
        .map_err(|_| format!("{flag} is not UTF-8"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pcm16_clip_has_exact_header_and_extrema() {
        let bytes = encode_pcm16_wav(&[-1.0, 0.0, 0.999_969_5]).unwrap();
        assert_eq!(bytes.len(), 50);
        assert_eq!(&bytes[..4], b"RIFF");
        assert_eq!(u32::from_le_bytes(bytes[40..44].try_into().unwrap()), 6);
        assert_eq!(
            i16::from_le_bytes(bytes[44..46].try_into().unwrap()),
            -32768
        );
        assert_eq!(i16::from_le_bytes(bytes[48..50].try_into().unwrap()), 32767);
    }

    #[test]
    fn fixed_reference_maps_to_exact_sample() {
        assert_eq!(micros_to_sample(200_000).unwrap(), PRE_SAMPLES);
        assert_eq!(PRE_SAMPLES + POST_SAMPLES, 22_050);
    }
}
