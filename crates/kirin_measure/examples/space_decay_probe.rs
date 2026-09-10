//! G1 fixed-window research only. Not linked into the plugin or a frozen product definition.
//! Usage: space_decay_probe FLOAT32_WAV ONSET_FRAME FIT_START_MS FIT_END_MS FLOOR_DB
//! Fit selection and floor are explicit inputs; there is no automatic interval search.

use kirin_measure::space_decay::SPACE_DECAY_CALCULATOR_DEFINITION_ID;
use sha2::{Digest, Sha256};
use std::io::Read;

#[path = "space_decay_probe/analysis.rs"]
mod analysis;
use analysis::{analyze, boundary};
#[path = "space_decay_probe/local.rs"]
mod local;
use local::local_decay_profile;

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.len() != 5 {
        return Err(
            "usage: space_decay_probe FLOAT32_WAV ONSET_FRAME FIT_START_MS FIT_END_MS FLOOR_DB"
                .into(),
        );
    }
    let onset: usize = args[1].parse()?;
    let start_ms: u32 = args[2].parse()?;
    let end_ms: u32 = args[3].parse()?;
    let floor_db: f64 = args[4].parse()?;
    const MAX_BYTES: u64 = 256 * 1_024 * 1_024;
    let mut bytes = Vec::new();
    std::fs::File::open(&args[0])?
        .take(MAX_BYTES + 1)
        .read_to_end(&mut bytes)?;
    if bytes.len() as u64 > MAX_BYTES {
        return Err("research input exceeds 256 MiB bound".into());
    }
    let mut wav = hound::WavReader::new(std::io::Cursor::new(&bytes))?;
    let spec = wav.spec();
    // Intentionally narrow research input: no hidden resampling, conversion, downmix or padding.
    if spec.sample_format != hound::SampleFormat::Float
        || spec.bits_per_sample != 32
        || !matches!(spec.channels, 1 | 2)
        || !(8_000..=768_000).contains(&spec.sample_rate)
        || end_ms > 6_000
    {
        return Err("unsupported research input or range".into());
    }
    let channels = usize::from(spec.channels);
    let skip = onset.checked_mul(channels).ok_or("onset overflow")?;
    let frames = boundary(spec.sample_rate, end_ms.max(250)).ok_or("boundary overflow")?;
    let count = frames.checked_mul(channels).ok_or("range overflow")?;
    let pcm = wav
        .samples::<f32>()
        .skip(skip)
        .take(count)
        .collect::<Result<Vec<_>, _>>()?;
    let facts = analyze(&pcm, spec.sample_rate, channels, start_ms, end_ms, floor_db)?;
    let local_profile = local_decay_profile(
        &pcm,
        spec.sample_rate,
        channels,
        start_ms,
        end_ms,
        floor_db,
        &[0.5, 1.0, 2.0, 3.0, 6.0],
    )?;
    println!(
        "{}",
        serde_json::to_string_pretty(&serde_json::json!({
            "schema": "hypha.space.fixed-window-research.v1",
            "product_qualified": false,
            "calculator_definition_id": SPACE_DECAY_CALCULATOR_DEFINITION_ID,
            "input_sha256": hex::encode(Sha256::digest(&bytes)),
            "sample_rate": spec.sample_rate, "channels": channels,
            "onset_frame": onset, "captured_frames": pcm.len() / channels,
            "fit_start_ms": start_ms, "fit_end_ms": end_ms, "supplied_floor_db": floor_db,
            "facts": facts, "local_decay_profile": local_profile,
            "limitations": "Manual interval; D20 and multi-threshold local peak-to-trough episodes are separate development diagnostics. No recovery threshold, acceptance gate or product display is frozen. D20 is a regression equivalent, not RT60."
        }))?
    );
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
#[path = "space_decay_probe/tests.rs"]
mod tests;
