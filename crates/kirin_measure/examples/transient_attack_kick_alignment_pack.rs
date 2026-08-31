//! Auditory guide-click alignment for the 11 confirmed audible kick misses.
//!
//! A short guide tick is rendered into the left channel at fixed 10 ms steps.
//! The reviewer aligns that tick to the perceived low-kick onset by listening.

use std::collections::{BTreeMap, HashMap};
use std::f32::consts::PI;
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
#[path = "transient_attack_kick_alignment_pack/review.rs"]
mod review;

use input::read_mono_pcm_wav;

const EXPECTED_KEY_SHA256: &str =
    "582d4003d02603063a75a349030e2d78f5cc900ee76681b93c9d74d0f6716598";
const EXPECTED_DIAGNOSTIC_SHA256: &str =
    "b50c6fb62edd97bb7c9efbefef94f6b2c538d0c078033d3141063dd5c528a07a";
const SAMPLE_RATE: u32 = 44_100;
const GUIDE_MIN_MS: u32 = 100;
const GUIDE_MAX_MS: u32 = 300;
const GUIDE_STEP_MS: u32 = 10;

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
        eprintln!("ATTACK kick alignment pack failed: {error}");
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
        || diagnostic.schema != "kirin-hypha-attack-kick-subband-diagnostic-v1"
        || diagnostic.candidate_tuning_performed
        || diagnostic.events.len() != 45
    {
        return Err("unexpected ATTACK alignment input contract".to_string());
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
            "alignment review requires 11 clips, got {}",
            chosen.len()
        ));
    }

    fs::create_dir(&cli.output_dir).map_err(|error| format!("create pack: {error}"))?;
    let output_clips = cli.output_dir.join("clips");
    let output_focus = cli.output_dir.join("focus");
    let output_guide = cli.output_dir.join("guide");
    for directory in [&output_clips, &output_focus, &output_guide] {
        fs::create_dir(directory).map_err(|error| format!("create assets: {error}"))?;
    }
    let mut review_clips = Vec::with_capacity(chosen.len());
    let mut manifest = String::from("review_id,clip_id,clip_sha256,focus_sha256\n");
    let mut guide_manifest = String::from("clip_id,guide_ms,sha256\n");
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
        if wav.metadata.sample_rate != SAMPLE_RATE || wav.samples.len() != 22_050 {
            return Err(format!("unexpected clip format: {clip_id}"));
        }
        for guide_ms in (GUIDE_MIN_MS..=GUIDE_MAX_MS).step_by(GUIDE_STEP_MS as usize) {
            let guided = encode_guided_stereo(&wav.samples, guide_ms)?;
            let filename = format!("{clip_id}_{guide_ms}.wav");
            publish_create_new(&output_guide.join(&filename), &guided)?;
            guide_manifest.push_str(&format!("{clip_id},{guide_ms},{}\n", sha256(&guided)));
        }
        review_clips.push(review::ReviewClip {
            clip_id: clip_id.clone(),
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
        &cli.output_dir.join("guide_manifest.csv"),
        guide_manifest.as_bytes(),
    )?;
    publish_create_new(
        &cli.output_dir.join("README.txt"),
        "review.htmlを開き、左側の短いガイド音を低いkickの始まりへ耳で合わせてください。波形判定は行いません。\n".as_bytes(),
    )?;
    let html = review::render_review_html(&review_clips)?;
    publish_create_new(&cli.output_dir.join("review.html"), &html)?;
    println!(
        "ATTACK kick alignment pack ready: variants={} html={} sha256={}",
        chosen.len() * 21,
        cli.output_dir.join("review.html").display(),
        sha256(&html)
    );
    Ok(())
}

fn encode_guided_stereo(samples: &[f32], guide_ms: u32) -> Result<Vec<u8>, String> {
    if !(GUIDE_MIN_MS..=GUIDE_MAX_MS).contains(&guide_ms) || !guide_ms.is_multiple_of(GUIDE_STEP_MS)
    {
        return Err("guide position is outside the fixed grid".to_string());
    }
    let data_bytes = samples.len().checked_mul(4).ok_or("guided WAV overflow")?;
    let riff_size = u32::try_from(data_bytes.checked_add(36).ok_or("guided RIFF overflow")?)
        .map_err(|_| "guided WAV is too large".to_string())?;
    let data_size =
        u32::try_from(data_bytes).map_err(|_| "guided data is too large".to_string())?;
    let guide_start = guide_ms as usize * SAMPLE_RATE as usize / 1_000;
    let guide_len = SAMPLE_RATE as usize * 4 / 1_000;
    let mut bytes = Vec::with_capacity(data_bytes + 44);
    bytes.extend_from_slice(b"RIFF");
    bytes.extend_from_slice(&riff_size.to_le_bytes());
    bytes.extend_from_slice(b"WAVEfmt ");
    bytes.extend_from_slice(&16_u32.to_le_bytes());
    bytes.extend_from_slice(&1_u16.to_le_bytes());
    bytes.extend_from_slice(&2_u16.to_le_bytes());
    bytes.extend_from_slice(&SAMPLE_RATE.to_le_bytes());
    bytes.extend_from_slice(&(SAMPLE_RATE * 4).to_le_bytes());
    bytes.extend_from_slice(&4_u16.to_le_bytes());
    bytes.extend_from_slice(&16_u16.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&data_size.to_le_bytes());
    for (index, sample) in samples.iter().copied().enumerate() {
        let tick = index
            .checked_sub(guide_start)
            .filter(|offset| *offset < guide_len)
            .map_or(0.0, |offset| {
                let phase = offset as f32 / (guide_len - 1) as f32;
                0.12 * (PI * phase).sin().powi(2)
                    * (2.0 * PI * 6_000.0 * offset as f32 / SAMPLE_RATE as f32).sin()
            });
        let left = sample + tick;
        if left.abs() >= 1.0 {
            return Err("guide tick would clip".to_string());
        }
        for channel in [left, sample] {
            let value = (channel * 32_768.0).round().clamp(-32_768.0, 32_767.0) as i16;
            bytes.extend_from_slice(&value.to_le_bytes());
        }
    }
    Ok(bytes)
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
            return Err("alignment pack permits only DRUM".to_string());
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn guide_tick_is_left_only_and_grid_is_fixed() {
        let samples = vec![0.0; 22_050];
        let wav = encode_guided_stereo(&samples, 200).unwrap();
        assert_eq!(wav.len(), 44 + samples.len() * 4);
        let before = 44 + (8_000 * 4);
        assert_eq!(&wav[before..before + 4], &[0, 0, 0, 0]);
        let tick = 44 + (8_850 * 4);
        assert_ne!(&wav[tick..tick + 2], &[0, 0]);
        assert_eq!(&wav[tick + 2..tick + 4], &[0, 0]);
        assert!(encode_guided_stereo(&samples, 205).is_err());
    }
}
