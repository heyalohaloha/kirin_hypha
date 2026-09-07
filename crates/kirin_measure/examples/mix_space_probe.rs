//! Offline development only; never registered as a plugin profile or a product SPACE route.
//! Usage: mix_space_probe FLOAT32_WAV attack|space PARAMETERS_JSON
use sha2::{Digest, Sha256};
use std::io::Read;
#[path = "mix_space_probe/analysis.rs"]
mod analysis;

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.len() != 3 || !matches!(args[1].as_str(), "attack" | "space") {
        return Err("usage: mix_space_probe FLOAT32_WAV attack|space PARAMETERS_JSON".into());
    }
    let config = bounded_read(&args[2], 16 * 1024)?;
    let parameters: analysis::Parameters = serde_json::from_slice(&config)?;
    parameters.validate()?; // A holdout purpose is rejected before opening any audio.
    let bytes = bounded_read(&args[0], 256 * 1024 * 1024)?;
    let mut wav = hound::WavReader::new(std::io::Cursor::new(&bytes))?;
    let spec = wav.spec();
    if spec.sample_format != hound::SampleFormat::Float
        || spec.bits_per_sample != 32
        || !matches!(spec.channels, 1 | 2)
    {
        return Err("float32 mono/stereo WAV required".into());
    }
    let pcm = wav.samples::<f32>().collect::<Result<Vec<_>, _>>()?;
    let channels = usize::from(spec.channels);
    if pcm.len() / channels > spec.sample_rate as usize * 60 {
        return Err("60 second research limit".into());
    }
    let started = std::time::Instant::now();
    let (odf_definition, events) = analysis::detect(&pcm, spec.sample_rate, channels, &parameters)?;
    let mut observations = Vec::new();
    const SPACE_CAPACITY: usize = 240;
    if args[1] == "space" {
        for (i, event) in events.iter().take(SPACE_CAPACITY).enumerate() {
            observations.push(analysis::observe(
                &pcm,
                spec.sample_rate,
                channels,
                event.sample,
                events.get(i + 1).map(|event| event.sample),
                &parameters,
            )?);
        }
    }
    let elapsed_ms = started.elapsed().as_secs_f64() * 1000.0;
    println!(
        "{}",
        serde_json::to_string_pretty(&serde_json::json!({
            "schema": "hypha.mix-space.development-probe.v1", "mode": args[1],
            "product_qualified": false, "human_annotation_evaluated": false,
            "input_sha256": hex::encode(Sha256::digest(&bytes)),
            "parameters_sha256": hex::encode(Sha256::digest(&config)),
            "odf_definition_sha256": odf_definition, "parameters": parameters,
            "sample_rate": spec.sample_rate, "channels": channels, "frames": pcm.len() / channels,
            "pcm_bytes": pcm.len() * 4, "compute_elapsed_ms": elapsed_ms,
            "event_count": events.len(), "events": events, "space_observations": observations,
            "space_capacity": SPACE_CAPACITY,
            "space_skipped_capacity": if args[1] == "space" { events.len().saturating_sub(SPACE_CAPACITY) } else { 0 },
            "limitations": "Unfrozen candidate; no precision, recall or perceptual validity claim. Not the DRUM profile. SPACE intervals are separate from onset correctness."
        }))?
    );
    Ok(())
}

fn bounded_read(path: &str, limit: u64) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let mut bytes = Vec::new();
    std::fs::File::open(path)?
        .take(limit + 1)
        .read_to_end(&mut bytes)?;
    if bytes.len() as u64 > limit {
        return Err("research file size limit".into());
    }
    Ok(bytes)
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
#[path = "mix_space_probe/tests.rs"]
mod tests;
