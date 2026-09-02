//! Exact acoustic-onset review pack for the 11 confirmed audible B-560 kick misses.
//!
//! The HTML exposes opaque clip IDs, audio, waveform, and the fixed MIDI marker.
//! It does not expose detector status, performance identity, or candidate values.

use std::collections::{BTreeMap, HashMap};
use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process;

use serde::Deserialize;
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
#[path = "transient_attack_kick_timing_pack/review.rs"]
mod review;

use input::read_mono_pcm_wav;

const EXPECTED_KEY_SHA256: &str =
    "582d4003d02603063a75a349030e2d78f5cc900ee76681b93c9d74d0f6716598";
const EXPECTED_DIAGNOSTIC_SHA256: &str =
    "b50c6fb62edd97bb7c9efbefef94f6b2c538d0c078033d3141063dd5c528a07a";

#[derive(Debug)]
struct Cli {
    clips: PathBuf,
    focus: PathBuf,
    key: PathBuf,
    diagnostic: PathBuf,
    output_dir: PathBuf,
}

#[derive(Debug, Deserialize)]
struct KeyArtifact {
    schema: String,
    candidate_status_exposed_in_pack: bool,
    events: Vec<KeyEvent>,
}

#[derive(Debug, Deserialize)]
struct KeyEvent {
    clip_id: String,
    clip_sha256: String,
    focus_sha256: String,
}

#[derive(Debug, Deserialize)]
struct DiagnosticArtifact {
    schema: String,
    candidate_tuning_performed: bool,
    events: Vec<DiagnosticEvent>,
}

#[derive(Debug, Deserialize)]
struct DiagnosticEvent {
    clip_id: String,
    listening_group: String,
}

fn main() {
    run().unwrap_or_else(|error| {
        eprintln!("ATTACK kick timing pack failed: {error}");
        process::exit(1);
    });
}

fn run() -> Result<(), String> {
    let cli = Cli::parse(std::env::args_os().skip(1))?;
    if cli.output_dir.exists() {
        return Err(format!(
            "output already exists: {}",
            cli.output_dir.display()
        ));
    }
    let key: KeyArtifact = read_pinned_json(&cli.key, EXPECTED_KEY_SHA256, "listening key")?;
    let diagnostic: DiagnosticArtifact = read_pinned_json(
        &cli.diagnostic,
        EXPECTED_DIAGNOSTIC_SHA256,
        "subband diagnostic",
    )?;
    if key.schema != "kirin-hypha-attack-kick-listening-key-v1"
        || key.candidate_status_exposed_in_pack
        || key.events.len() != 45
    {
        return Err("unexpected listening key contract".to_string());
    }
    if diagnostic.schema != "kirin-hypha-attack-kick-subband-diagnostic-v1"
        || diagnostic.candidate_tuning_performed
        || diagnostic.events.len() != 45
    {
        return Err("unexpected subband diagnostic contract".to_string());
    }
    let key_events = key
        .events
        .into_iter()
        .map(|event| (event.clip_id.clone(), event))
        .collect::<HashMap<_, _>>();
    let mut chosen = diagnostic
        .events
        .into_iter()
        .filter(|event| event.listening_group == "audible_target_missed")
        .map(|event| event.clip_id)
        .collect::<Vec<_>>();
    chosen.sort();
    if chosen.len() != 11 {
        return Err(format!(
            "timing review requires 11 confirmed misses, got {}",
            chosen.len()
        ));
    }

    fs::create_dir(&cli.output_dir)
        .map_err(|error| format!("cannot create timing pack: {error}"))?;
    let output_clips = cli.output_dir.join("clips");
    let output_focus = cli.output_dir.join("focus");
    fs::create_dir(&output_clips).map_err(|error| format!("cannot create clips: {error}"))?;
    fs::create_dir(&output_focus).map_err(|error| format!("cannot create focus: {error}"))?;
    let mut review_clips = Vec::with_capacity(chosen.len());
    let mut manifest = String::from("review_id,clip_id,clip_sha256,focus_sha256\n");
    for (index, clip_id) in chosen.iter().enumerate() {
        let key = key_events
            .get(clip_id)
            .ok_or_else(|| format!("missing key event: {clip_id}"))?;
        let clip_source = cli.clips.join(format!("{clip_id}.wav"));
        let focus_source = cli.focus.join(format!("{clip_id}_focus.wav"));
        let clip = read_verified(&clip_source, &key.clip_sha256)?;
        let focus = read_verified(&focus_source, &key.focus_sha256)?;
        publish_create_new(&output_clips.join(format!("{clip_id}.wav")), &clip)?;
        publish_create_new(&output_focus.join(format!("{clip_id}_focus.wav")), &focus)?;
        let wav = read_mono_pcm_wav(&clip_source)?;
        if wav.metadata.sample_rate != 44_100 || wav.samples.len() != 22_050 {
            return Err(format!("unexpected listening clip format: {clip_id}"));
        }
        review_clips.push(review::ReviewClip {
            clip_id: clip_id.clone(),
            waveform: review::waveform_envelope(&wav.samples),
        });
        manifest.push_str(&format!(
            "{},{},{},{}\n",
            index + 1,
            clip_id,
            key.clip_sha256,
            key.focus_sha256
        ));
    }
    publish_create_new(&cli.output_dir.join("manifest.csv"), manifest.as_bytes())?;
    publish_create_new(
        &cli.output_dir.join("README.txt"),
        "review.htmlを開き、低いキックが始まる瞬間を波形上でクリックしてください。先行するスネアは選びません。\n".as_bytes(),
    )?;
    let html = review::render_review_html(&review_clips)?;
    publish_create_new(&cli.output_dir.join("review.html"), &html)?;
    println!(
        "ATTACK kick timing pack ready: clips=11 html={} sha256={}",
        cli.output_dir.join("review.html").display(),
        sha256(&html)
    );
    Ok(())
}

fn read_pinned_json<T: for<'de> Deserialize<'de>>(
    path: &Path,
    expected: &str,
    label: &str,
) -> Result<T, String> {
    let bytes = fs::read(path).map_err(|error| format!("cannot read {label}: {error}"))?;
    if sha256(&bytes) != expected {
        return Err(format!("{label} SHA-256 mismatch"));
    }
    serde_json::from_slice(&bytes).map_err(|error| format!("invalid {label}: {error}"))
}

fn read_verified(path: &Path, expected: &str) -> Result<Vec<u8>, String> {
    let bytes =
        fs::read(path).map_err(|error| format!("cannot read {}: {error}", path.display()))?;
    if sha256(&bytes) != expected {
        return Err(format!("source hash mismatch: {}", path.display()));
    }
    Ok(bytes)
}

fn sha256(bytes: &[u8]) -> String {
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
            return Err("timing pack permits only DRUM".to_string());
        }
        let cli = Self {
            clips: take_path(&mut values, "--clips")?,
            focus: take_path(&mut values, "--focus")?,
            key: take_path(&mut values, "--listening-key")?,
            diagnostic: take_path(&mut values, "--subband-diagnostic")?,
            output_dir: take_path(&mut values, "--output-dir")?,
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
