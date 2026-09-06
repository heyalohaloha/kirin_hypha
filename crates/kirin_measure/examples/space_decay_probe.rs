//! G1 fixed-window research only. Not linked into the plugin or a frozen product definition.
//! Usage: space_decay_probe FLOAT32_WAV ONSET_FRAME FIT_START_MS FIT_END_MS FLOOR_DB
//! Fit selection and floor are explicit inputs; there is no automatic interval search.

use serde::Serialize;
use sha2::{Digest, Sha256};
use std::io::Read;

#[derive(Debug, Serialize)]
struct Fit {
    points: usize,
    slope_db_per_second: f64,
    r_squared: f64,
    observed_fall_db: f64,
    largest_rise_db: f64,
    d20_seconds: Option<f64>,
}

#[derive(Debug, Serialize)]
struct Facts {
    early_db: Option<f64>,
    early_reason: Option<&'static str>,
    fit: Option<Fit>,
    fit_reason: Option<&'static str>,
}

fn boundary(rate: u32, milliseconds: u32) -> Option<usize> {
    usize::try_from((u64::from(rate) * u64::from(milliseconds) + 500) / 1_000).ok()
}

fn power(samples: &[f32], channels: usize) -> f64 {
    samples
        .iter()
        .map(|&value| f64::from(value).powi(2))
        .sum::<f64>()
        / channels as f64
}

fn early(pcm: &[f32], rate: u32, channels: usize, floor: f64) -> Result<f64, &'static str> {
    let split = boundary(rate, 80).ok_or("boundary_overflow")?;
    let end = boundary(rate, 250).ok_or("boundary_overflow")?;
    if pcm.len() / channels < end {
        return Err("incomplete_window");
    }
    let first = power(&pcm[..split * channels], channels);
    let last = power(&pcm[split * channels..end * channels], channels);
    if first / split as f64 <= floor || last / (end - split) as f64 <= floor {
        return Err("window_at_or_below_supplied_floor");
    }
    Ok(10.0 * (first / last).log10())
}

fn fit(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    start_ms: u32,
    end_ms: u32,
    floor: f64,
) -> Result<Fit, &'static str> {
    if start_ms >= end_ms
        || end_ms > 6_000
        || !start_ms.is_multiple_of(10)
        || !end_ms.is_multiple_of(10)
    {
        return Err("fit_range_invalid");
    }
    if pcm.len() / channels < boundary(rate, end_ms).ok_or("boundary_overflow")? {
        return Err("incomplete_window");
    }
    let mut points = Vec::new();
    for ms in (start_ms..end_ms).step_by(10) {
        // Independently round each boundary from the fixed origin, never accumulate rounded hops.
        let first = boundary(rate, ms).ok_or("boundary_overflow")?;
        let end = boundary(rate, ms + 10).ok_or("boundary_overflow")?;
        let average =
            power(&pcm[first * channels..end * channels], channels) / (end - first) as f64;
        if average <= floor {
            return Err("fit_at_or_below_supplied_floor");
        }
        let seconds = (first as f64 + end as f64) * 0.5 / f64::from(rate);
        points.push((seconds, 10.0 * average.log10()));
    }
    if points.len() < 10 {
        return Err("fewer_than_ten_points");
    }
    let count = points.len() as f64;
    let mx = points.iter().map(|p| p.0).sum::<f64>() / count;
    let my = points.iter().map(|p| p.1).sum::<f64>() / count;
    let xx = points.iter().map(|p| (p.0 - mx).powi(2)).sum::<f64>();
    let xy = points.iter().map(|p| (p.0 - mx) * (p.1 - my)).sum::<f64>();
    let yy = points.iter().map(|p| (p.1 - my).powi(2)).sum::<f64>();
    let slope = xy / xx;
    let fall = points[0].1 - points[points.len() - 1].1;
    let rise = points
        .windows(2)
        .map(|p| p[1].1 - p[0].1)
        .fold(0.0, f64::max);
    Ok(Fit {
        points: points.len(),
        slope_db_per_second: slope,
        r_squared: if yy > 0.0 {
            (xy * xy / (xx * yy)).clamp(0.0, 1.0)
        } else {
            0.0
        },
        observed_fall_db: fall,
        largest_rise_db: rise,
        d20_seconds: (slope < 0.0 && fall >= 20.0).then(|| 20.0 / -slope),
    })
}

fn analyze(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    start_ms: u32,
    end_ms: u32,
    floor_db: f64,
) -> Result<Facts, &'static str> {
    if !(8_000..=768_000).contains(&rate)
        || !matches!(channels, 1 | 2)
        || !pcm.len().is_multiple_of(channels)
        || pcm.iter().any(|sample| !sample.is_finite())
        || !floor_db.is_finite()
        || !(-300.0..=0.0).contains(&floor_db)
    {
        return Err("invalid_input");
    }
    let floor = 10.0_f64.powf(floor_db / 10.0);
    let early = early(pcm, rate, channels, floor);
    let fit = fit(pcm, rate, channels, start_ms, end_ms, floor);
    Ok(Facts {
        early_db: early.as_ref().ok().copied(),
        early_reason: early.err(),
        fit_reason: fit.as_ref().err().copied(),
        fit: fit.ok(),
    })
}

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
    println!(
        "{}",
        serde_json::to_string_pretty(&serde_json::json!({
            "schema": "hypha.space.fixed-window-research.v1",
            "product_qualified": false,
            "input_sha256": hex::encode(Sha256::digest(&bytes)),
            "sample_rate": spec.sample_rate, "channels": channels,
            "onset_frame": onset, "captured_frames": pcm.len() / channels,
            "fit_start_ms": start_ms, "fit_end_ms": end_ms, "supplied_floor_db": floor_db,
            "facts": facts,
            "limitations": "Manual interval; R2, rise, floor margin and interval selection gates are not frozen. D20 is a regression equivalent, not RT60."
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
