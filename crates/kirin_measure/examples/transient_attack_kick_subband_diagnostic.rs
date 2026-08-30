//! Focused subband diagnosis for the 45 completed B-558 kick-listening clips.
//!
//! This reads no holdout and tunes no threshold. It compares the frozen B-553
//! full-band trace with low/mid/high components around the fixed 200 ms marker.

use std::collections::BTreeMap;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::Path;
use std::process;

use kirin_measure::{SuperFluxAnalyzer, TransientOdfFrame};
use serde::{Deserialize, Serialize};

#[path = "transient_attack_kick_subband_diagnostic/audit_input.rs"]
mod audit_input;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/contract.rs"]
mod contract;
#[path = "transient_drum_excerpt/mod.rs"]
mod drum_excerpt;
#[path = "transient_drum_midi/mod.rs"]
mod drum_midi;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/evaluation.rs"]
mod evaluation;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/input.rs"]
mod input;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/matching.rs"]
mod matching;
#[allow(dead_code, unused_imports)]
#[path = "transient_candidate_eval/metrics.rs"]
mod metrics;

use audit_input::{read_pinned_inputs, read_responses, sha256, Cli, Response};
use contract::{CandidateConfig, FormalAnalyzer, PeakRule};
use evaluation::peak_candidates;
use input::read_mono_pcm_wav;

const EXPECTED_CANDIDATE_ID: &str = "b553-superflux-pilot-best";
const TARGET_SAMPLE: i64 = 8_820;
const WINDOW_RADIUS_SAMPLES: i64 = 2_205;

#[derive(Debug, Deserialize)]
struct KeyArtifact {
    schema: String,
    candidate_id: String,
    candidate_status_exposed_in_pack: bool,
    events: Vec<KeyEvent>,
}

#[derive(Debug, Deserialize)]
struct KeyEvent {
    clip_id: String,
    clip_sha256: String,
    diagnostic: DiagnosticEvent,
}

#[derive(Debug, Deserialize)]
struct DiagnosticEvent {
    label_time_micros: u64,
    matched: bool,
    miss_class: String,
    nearest_eligible_error_ms: Option<f64>,
}

#[derive(Clone, Copy)]
struct BandFrame {
    sample: i64,
    full: f32,
    low: f32,
    mid: f32,
    high: f32,
}

#[derive(Debug, Serialize)]
struct EventResult {
    clip_id: String,
    listening_group: &'static str,
    detector_miss_class: String,
    full_peak: f32,
    full_peak_error_ms: f64,
    low_peak: f32,
    low_peak_error_ms: f64,
    mid_peak: f32,
    high_peak: f32,
    low_eligible_error_ms: Option<f64>,
    prior_full_to_low_gap_ms: Option<f64>,
}

#[derive(Debug, Serialize)]
struct GroupSummary {
    listening_group: &'static str,
    count: usize,
    full_peak_median: f64,
    low_peak_median: f64,
    mid_peak_median: f64,
    high_peak_median: f64,
    low_eligible_within_25_ms: usize,
    low_eligible_within_50_ms: usize,
    low_followup_25_to_75_ms: usize,
}

#[derive(Serialize)]
struct Artifact {
    schema: &'static str,
    status: &'static str,
    profile: &'static str,
    candidate_id: &'static str,
    publication_eligible: bool,
    candidate_tuning_performed: bool,
    input_event_count: usize,
    band_contract: &'static str,
    group_summaries: Vec<GroupSummary>,
    events: Vec<EventResult>,
}

fn main() {
    run().unwrap_or_else(|error| {
        eprintln!("ATTACK kick subband diagnostic failed: {error}");
        process::exit(1);
    });
}

fn run() -> Result<(), String> {
    let cli = Cli::parse_env()?;
    if cli.result.exists() {
        return Err(format!("result already exists: {}", cli.result.display()));
    }
    let (key_bytes, response_bytes) = read_pinned_inputs(&cli.key, &cli.responses)?;
    let key: KeyArtifact = serde_json::from_slice(&key_bytes)
        .map_err(|error| format!("invalid listening key: {error}"))?;
    if key.schema != "kirin-hypha-attack-kick-listening-key-v1"
        || key.candidate_id != EXPECTED_CANDIDATE_ID
        || key.candidate_status_exposed_in_pack
        || key.events.len() != 45
    {
        return Err("unexpected listening key contract".to_string());
    }
    let responses = read_responses(&response_bytes)?;
    let candidate = CandidateConfig::read(&cli.candidate)?;
    if candidate.config.candidate_id() != EXPECTED_CANDIDATE_ID {
        return Err("unexpected ATTACK candidate".to_string());
    }
    let (_, analyzer, rule) = candidate.config.formal_parts()?;
    let FormalAnalyzer::FixedSuperflux(config) = analyzer else {
        return Err("subband diagnosis requires fixed SuperFlux".to_string());
    };

    let mut events = Vec::with_capacity(key.events.len());
    for event in key.events {
        let response = responses
            .get(&event.clip_id)
            .ok_or_else(|| format!("missing response for {}", event.clip_id))?;
        let path = cli.clips.join(format!("{}.wav", event.clip_id));
        let raw =
            fs::read(&path).map_err(|error| format!("cannot read {}: {error}", path.display()))?;
        if sha256(&raw) != event.clip_sha256 {
            return Err(format!("clip hash mismatch: {}", event.clip_id));
        }
        let wav = read_mono_pcm_wav(&path)?;
        if wav.metadata.sample_rate != 44_100 || wav.samples.len() != 22_050 {
            return Err(format!("unexpected clip format: {}", event.clip_id));
        }
        let source_center = micros_to_sample(event.diagnostic.label_time_micros)?;
        let source_clip_start = source_center
            .checked_sub(TARGET_SAMPLE)
            .ok_or("listening clip source start underflow")?;
        let frames = analyze_bands(&wav.samples, config, source_clip_start)?;
        events.push(summarize_event(event, response, &frames, rule)?);
    }
    if responses.len() != events.len() {
        return Err("response/key cardinality mismatch".to_string());
    }
    events.sort_by(|left, right| left.clip_id.cmp(&right.clip_id));
    let artifact = Artifact {
        schema: "kirin-hypha-attack-kick-subband-diagnostic-v1",
        status: "completed_listening_sample_diagnostic_not_candidate_tuning",
        profile: "DRUM",
        candidate_id: EXPECTED_CANDIDATE_ID,
        publication_eligible: false,
        candidate_tuning_performed: false,
        input_event_count: events.len(),
        band_contract: "same-B553-SuperFlux-band-flux;30-200Hz;200-2000Hz;2000-17000Hz;fixed-marker-plus-minus-50ms",
        group_summaries: summarize_groups(&events),
        events,
    };
    publish_create_new(
        &cli.result,
        &serde_json::to_vec_pretty(&artifact)
            .map_err(|error| format!("cannot serialize result: {error}"))?,
    )?;
    println!(
        "ATTACK kick subband diagnostic complete: {}",
        cli.result.display()
    );
    Ok(())
}

fn analyze_bands(
    samples: &[f32],
    config: kirin_measure::SuperFluxConfig,
    source_start: i64,
) -> Result<Vec<BandFrame>, String> {
    let mut analyzer = SuperFluxAnalyzer::new(44_100, config).map_err(str::to_string)?;
    let window = analyzer.layout().window_samples;
    let hop = analyzer.layout().hop_samples;
    let fft = analyzer.layout().fft_size as f64;
    let groups = analyzer
        .layout()
        .band_triplets()
        .map(|bins| match bins[1] as f64 * 44_100.0 / fft {
            hz if hz < 200.0 => 0,
            hz if hz < 2_000.0 => 1,
            _ => 2,
        })
        .collect::<Vec<_>>();
    if !(groups.contains(&0) && groups.contains(&1) && groups.contains(&2)) {
        return Err("SuperFlux diagnostic band partition is empty".to_string());
    }
    let phase = source_start.rem_euclid(hop as i64);
    let first_center = (-phase).rem_euclid(hop as i64);
    let mut buffer = vec![0.0_f32; window];
    let mut flux = vec![0.0_f32; groups.len()];
    for warmup in (1..=analyzer.layout().spectral_lag_frames).rev() {
        analyzer
            .analyze_window_with_band_flux(
                &buffer,
                None,
                first_center - (warmup * hop) as i64 - (window / 2) as i64,
                &mut flux,
            )
            .map_err(str::to_string)?;
    }
    let flush_end = samples
        .len()
        .checked_add(window / 2)
        .ok_or("subband flush overflow")? as i64;
    let mut frames = Vec::new();
    for center in (first_center..flush_end).step_by(hop) {
        buffer.fill(0.0);
        let support_start = center - (window / 2) as i64;
        for (offset, value) in buffer.iter_mut().enumerate() {
            if let Ok(index) = usize::try_from(support_start + offset as i64) {
                if let Some(sample) = samples.get(index) {
                    *value = *sample;
                }
            }
        }
        if let Some(frame) = analyzer
            .analyze_window_with_band_flux(&buffer, None, support_start, &mut flux)
            .map_err(str::to_string)?
        {
            frames.push(BandFrame {
                sample: frame.event_sample,
                full: frame.value,
                low: band_mean(&flux, &groups, 0),
                mid: band_mean(&flux, &groups, 1),
                high: band_mean(&flux, &groups, 2),
            });
        }
    }
    Ok(frames)
}

fn band_mean(flux: &[f32], groups: &[u8], wanted: u8) -> f32 {
    let mut count = 0;
    let sum = flux
        .iter()
        .zip(groups)
        .filter_map(|(value, group)| (*group == wanted).then_some(*value))
        .inspect(|_| count += 1)
        .sum::<f32>();
    sum / count as f32
}

fn summarize_event(
    event: KeyEvent,
    response: &Response,
    frames: &[BandFrame],
    rule: PeakRule,
) -> Result<EventResult, String> {
    let group = listening_group(response, &event.diagnostic)?;
    let full = peak_in_window(frames, |frame| frame.full)?;
    let low = peak_in_window(frames, |frame| frame.low)?;
    let mid = peak_in_window(frames, |frame| frame.mid)?;
    let high = peak_in_window(frames, |frame| frame.high)?;
    let low_frames = frames
        .iter()
        .map(|frame| TransientOdfFrame {
            support_start_samples: frame.sample,
            support_end_samples: frame.sample,
            event_sample: frame.sample,
            mel_flux: 0.0,
            complex_odf: 0.0,
            value: frame.low,
        })
        .collect::<Vec<_>>();
    let low_eligible_error_ms = peak_candidates(&low_frames, rule)
        .into_iter()
        .map(|candidate| samples_to_ms(candidate.0 - TARGET_SAMPLE))
        .min_by(|left, right| left.abs().total_cmp(&right.abs()));
    let prior_full_to_low_gap_ms = low_eligible_error_ms
        .zip(event.diagnostic.nearest_eligible_error_ms)
        .map(|(low, full)| low - full);
    Ok(EventResult {
        clip_id: event.clip_id,
        listening_group: group,
        detector_miss_class: event.diagnostic.miss_class,
        full_peak: full.1,
        full_peak_error_ms: samples_to_ms(full.0 - TARGET_SAMPLE),
        low_peak: low.1,
        low_peak_error_ms: samples_to_ms(low.0 - TARGET_SAMPLE),
        mid_peak: mid.1,
        high_peak: high.1,
        low_eligible_error_ms,
        prior_full_to_low_gap_ms,
    })
}

fn peak_in_window(
    frames: &[BandFrame],
    value: impl Fn(&BandFrame) -> f32,
) -> Result<(i64, f32), String> {
    frames
        .iter()
        .filter(|frame| (frame.sample - TARGET_SAMPLE).abs() <= WINDOW_RADIUS_SAMPLES)
        .map(|frame| (frame.sample, value(frame)))
        .max_by(|left, right| {
            left.1
                .total_cmp(&right.1)
                .then_with(|| right.0.cmp(&left.0))
        })
        .ok_or_else(|| "no diagnostic frame inside target window".to_string())
}

fn listening_group(
    response: &Response,
    diagnostic: &DiagnosticEvent,
) -> Result<&'static str, String> {
    match response.audible_kick.as_str() {
        "no" => Ok("not_audible"),
        "uncertain" => Ok("uncertain"),
        "yes"
            if response.nearest_kick_ms.contains("あと")
                || response.nearest_kick_ms == "0–150 ms" =>
        {
            Ok("audible_outside_target")
        }
        "yes"
            if response.nearest_kick_ms.is_empty() || response.nearest_kick_ms == "150–300 ms" =>
        {
            Ok(if diagnostic.matched {
                "audible_target_matched"
            } else {
                "audible_target_missed"
            })
        }
        _ => Err("unsupported listening response".to_string()),
    }
}

fn summarize_groups(events: &[EventResult]) -> Vec<GroupSummary> {
    let mut groups = BTreeMap::<&'static str, Vec<&EventResult>>::new();
    for event in events {
        groups.entry(event.listening_group).or_default().push(event);
    }
    groups
        .into_iter()
        .map(|(listening_group, events)| GroupSummary {
            listening_group,
            count: events.len(),
            full_peak_median: median(events.iter().map(|event| f64::from(event.full_peak))),
            low_peak_median: median(events.iter().map(|event| f64::from(event.low_peak))),
            mid_peak_median: median(events.iter().map(|event| f64::from(event.mid_peak))),
            high_peak_median: median(events.iter().map(|event| f64::from(event.high_peak))),
            low_eligible_within_25_ms: events
                .iter()
                .filter(|event| {
                    event
                        .low_eligible_error_ms
                        .is_some_and(|value| value.abs() <= 25.0)
                })
                .count(),
            low_eligible_within_50_ms: events
                .iter()
                .filter(|event| {
                    event
                        .low_eligible_error_ms
                        .is_some_and(|value| value.abs() <= 50.0)
                })
                .count(),
            low_followup_25_to_75_ms: events
                .iter()
                .filter(|event| {
                    event
                        .low_eligible_error_ms
                        .is_some_and(|value| value.abs() <= 25.0)
                        && event
                            .prior_full_to_low_gap_ms
                            .is_some_and(|value| (25.0..=75.0).contains(&value))
                })
                .count(),
        })
        .collect()
}

fn median(values: impl Iterator<Item = f64>) -> f64 {
    let mut values = values.collect::<Vec<_>>();
    values.sort_by(f64::total_cmp);
    let middle = values.len() / 2;
    if values.len() % 2 == 0 {
        0.5 * (values[middle - 1] + values[middle])
    } else {
        values[middle]
    }
}

fn micros_to_sample(micros: u64) -> Result<i64, String> {
    i64::try_from((u128::from(micros) * 44_100 + 500_000) / 1_000_000)
        .map_err(|_| "sample position overflow".to_string())
}

fn samples_to_ms(samples: i64) -> f64 {
    samples as f64 * 1_000.0 / 44_100.0
}

fn publish_create_new(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|error| format!("cannot create {}: {error}", path.display()))?;
    file.write_all(bytes)
        .and_then(|()| file.write_all(b"\n"))
        .and_then(|()| file.sync_all())
        .map_err(|error| format!("cannot publish {}: {error}", path.display()))
}
