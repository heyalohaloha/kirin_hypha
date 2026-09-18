//! What the measurement subsystems do when they are handed more than two channels (P-0 §6).
//!
//! Companion to `memory_contract_probe`, which measures bytes. This one measures behaviour:
//! whether a subsystem accepts the channels it was built with, and whether a per-channel fact is
//! observed on the channel it was put in. Construction succeeding is not the same as running.
//!
//! `cargo run -p kirin_measure --example channel_contract_probe --release -- <mode> <rate> <channels>`

use ebur128::{EbuR128, Mode};
use kirin_measure::SpectrumRuntime;

const MODE: Mode = Mode::M
    .union(Mode::S)
    .union(Mode::I)
    .union(Mode::LRA)
    .union(Mode::TRUE_PEAK);

fn probe_accept(native_rate: u32, channels: usize) {
    println!("\n== SpectrumRuntime acceptance: native {native_rate} Hz, {channels} ch ==");
    let runtime = SpectrumRuntime::new(native_rate, channels);
    runtime.set_enabled(true);

    let chunk_frames = native_rate as usize / 10;
    let chunk: Vec<f32> = (0..chunk_frames * channels)
        .map(|i| ((i % 97) as f32 / 97.0) * 0.5 - 0.25)
        .collect();
    let mut accepted = 0usize;
    for i in 0..40 {
        if runtime.push_block_from_audio(&chunk, channels, Some((i * chunk_frames) as i64)) {
            accepted += 1;
        }
    }
    // Give any worker a chance to drain before reading its counters.
    std::thread::sleep(std::time::Duration::from_millis(500));
    let stats = runtime.stats();
    println!("  push_block_from_audio returned true   {accepted} / 40");
    println!("  enabled                               {}", stats.enabled);
    println!(
        "  worker_running                        {}",
        stats.worker_running
    );
    println!("  channels                              {}", stats.channels);
    println!(
        "  pushed_blocks / dropped_blocks        {} / {}",
        stats.pushed_blocks, stats.dropped_blocks
    );
    println!(
        "  analyzed_frames                       {}",
        stats.analyzed_frames
    );
    println!(
        "  analyzed_perceptual_frames            {}",
        stats.analyzed_perceptual_frames
    );
    println!(
        "  analyzed_absolute_frames              {}",
        stats.analyzed_absolute_frames
    );
    println!(
        "  analyzed_mid_side_frames              {}",
        stats.analyzed_mid_side_frames
    );
    println!(
        "  history observations                  {}",
        runtime.try_history().map_or(0, |h| h.frames().len())
    );
}

/// Production routing: measure_thread.rs:1022 feeds the 48 kHz Watch engine on every iteration,
/// while the two native-rate Record engines are fed only inside `if is_recording` (:1046). Feeding
/// all three, as the earlier probe did, measures a generation that is recording on every instance.
fn probe_true_peak(rate: u32, channels: usize) {
    println!("\n== True Peak per channel: {rate} Hz, {channels} ch ==");
    println!("  peak in channel c = -6.02 dBFS (0.5), every other channel silent");
    println!(
        "  {:<10} {:<14} {:<12} verdict",
        "signal ch", "observed on", "value dBTP"
    );

    let frames = rate as usize / 10;
    for c in 0..channels {
        let mut meter = EbuR128::new(channels as u32, rate, MODE).expect("EbuR128");
        let mut block = vec![0.0f64; frames * channels];
        for f in 0..frames {
            // A short tone so the 4x interpolation has something to reconstruct.
            let phase = (f as f64) * std::f64::consts::TAU * 997.0 / rate as f64;
            block[f * channels + c] = 0.5 * phase.sin();
        }
        for _ in 0..10 {
            meter.add_frames_f64(&block).expect("add_frames");
        }
        let mut observed: Vec<usize> = Vec::new();
        let mut value = f64::NEG_INFINITY;
        for k in 0..channels {
            if let Ok(tp) = meter.true_peak(k as u32) {
                if tp > 1e-6 {
                    observed.push(k);
                    value = value.max(20.0 * tp.log10());
                }
            }
        }
        let verdict = if observed == vec![c] {
            "ok"
        } else if observed.is_empty() {
            "NOT OBSERVED"
        } else {
            "wrong channel"
        };
        println!(
            "  {:<10} {:<14} {:<12} {}",
            c,
            format!("{observed:?}"),
            if value.is_finite() {
                format!("{value:.2}")
            } else {
                "-".to_owned()
            },
            verdict
        );
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    match args.get(1).map(String::as_str) {
        Some("accept") => probe_accept(
            args[2].parse().expect("rate"),
            args[3].parse().expect("channels"),
        ),
        Some("truepeak") => probe_true_peak(
            args[2].parse().expect("rate"),
            args[3].parse().expect("channels"),
        ),
        _ => println!("usage: channel_contract_probe <accept|truepeak> <rate> <channels>"),
    }
}
