//! Minimal RSS probe for the capture-generation memory contract (P-0 investigation).
//!
//! Reads `/proc/self/statm` between construction steps so the ingest contract's formula model can
//! be compared against what the process actually keeps resident. Formula values are upper bounds on
//! logical reservation; this measures the other side. Linux only — it prints and exits elsewhere.
//!
//! `cargo run -p kirin_measure --example memory_contract_probe --release`

use ebur128::{EbuR128, Mode};
use kirin_measure::MeasureEngine;

const MODE: Mode = Mode::M
    .union(Mode::S)
    .union(Mode::I)
    .union(Mode::LRA)
    .union(Mode::TRUE_PEAK);

static PUSH_CHUNKS: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(40);

fn rss_bytes() -> Option<u64> {
    let statm = std::fs::read_to_string("/proc/self/statm").ok()?;
    let resident_pages: u64 = statm.split_whitespace().nth(1)?.parse().ok()?;
    Some(resident_pages * 4096)
}

fn mib(bytes: i64) -> f64 {
    bytes as f64 / (1024.0 * 1024.0)
}

/// What one Hypha instance's Measure Thread builds: a 48 kHz Watch engine plus a native-rate
/// Record TRACE engine and Record summary engine (measure_thread.rs:245 / 252 / 262).
fn instance_engines(native_rate: u32, channels: usize) -> Vec<MeasureEngine> {
    vec![
        MeasureEngine::new(48_000, channels).expect("watch engine"),
        MeasureEngine::new(native_rate, channels).expect("record trace engine"),
        MeasureEngine::new(native_rate, channels).expect("record summary engine"),
    ]
}

fn step<T>(label: &str, before: u64, held: &T) -> u64 {
    let after = rss_bytes().expect("statm");
    println!(
        "  {label:<44} RSS {:8.1} MiB   delta {:+8.2} MiB",
        mib(after as i64),
        mib(after as i64 - before as i64)
    );
    std::hint::black_box(held);
    after
}

fn formula_audio_data_bytes(rate: u32, channels: usize) -> u64 {
    let samples_in_100ms = (rate as usize + 5) / 10;
    let mut frames = rate as usize * 3000 / 1000;
    if !frames.is_multiple_of(samples_in_100ms) {
        frames += samples_in_100ms - frames % samples_in_100ms;
    }
    (frames * channels * std::mem::size_of::<f64>()) as u64
}

fn probe_single(native_rate: u32, channels: usize) {
    println!("\n== single-instance build-up: native {native_rate} Hz, {channels} ch ==");
    let base = rss_bytes().expect("statm");
    println!("  {:<44} RSS {:8.1} MiB", "baseline", mib(base as i64));

    let one_ebu = EbuR128::new(channels as u32, native_rate, MODE).expect("EbuR128");
    let after_ebu = step("1x EbuR128 (native rate)", base, &one_ebu);

    let one_engine = MeasureEngine::new(native_rate, channels).expect("MeasureEngine");
    let after_engine = step("1x MeasureEngine (native rate)", after_ebu, &one_engine);

    let engines = instance_engines(native_rate, channels);
    step(
        "3x MeasureEngine (one instance's set)",
        after_engine,
        &engines,
    );

    let predicted = formula_audio_data_bytes(48_000, channels)
        + formula_audio_data_bytes(native_rate, channels) * 2;
    println!(
        "  {:<44} {:8.2} MiB  (audio_data only)",
        "formula prediction for the 3x set",
        mib(predicted as i64)
    );
    std::hint::black_box((one_ebu, one_engine, engines));
}

fn probe_scaling(native_rate: u32, channels: usize) {
    println!("\n== instance scaling: native {native_rate} Hz, {channels} ch ==");
    let base = rss_bytes().expect("statm");
    let mut held: Vec<Vec<MeasureEngine>> = Vec::new();
    let mut previous = base;
    for target in [1usize, 2, 4, 8, 12, 24] {
        while held.len() < target {
            held.push(instance_engines(native_rate, channels));
        }
        let after = rss_bytes().expect("statm");
        println!(
            "  {:>2} instance(s)  RSS {:8.1} MiB   total {:+8.2} MiB   step {:+7.2} MiB   per-instance {:6.2} MiB",
            target,
            mib(after as i64),
            mib(after as i64 - base as i64),
            mib(after as i64 - previous as i64),
            mib(after as i64 - base as i64) / target as f64
        );
        previous = after;
    }
    std::hint::black_box(held);
}

/// Push enough audio for every engine to write across its whole 3000 ms `audio_data` ring, so the
/// zero pages `alloc_zeroed` handed out actually fault in. Untouched reservations stay off RSS,
/// which is the whole point of measuring this separately from the formula.
fn probe_touched(native_rate: u32, channels: usize, instances: usize) {
    println!("\n== touched vs untouched: native {native_rate} Hz, {channels} ch, {instances} instance(s) ==");
    let base = rss_bytes().expect("statm");
    let mut held: Vec<Vec<MeasureEngine>> = (0..instances)
        .map(|_| instance_engines(native_rate, channels))
        .collect();
    let after_alloc = rss_bytes().expect("statm");
    println!(
        "  {:<44} {:+8.2} MiB",
        "allocated, never pushed",
        mib(after_alloc as i64 - base as i64)
    );

    // 4 s of native-rate audio, in 100 ms chunks, at a level that is not silence.
    let chunk_frames = native_rate as usize / 10;
    let chunk: Vec<f64> = (0..chunk_frames * channels)
        .map(|i| ((i % 97) as f64 / 97.0) * 0.5 - 0.25)
        .collect();
    for engines in held.iter_mut() {
        for engine in engines.iter_mut() {
            for _ in 0..PUSH_CHUNKS.load(std::sync::atomic::Ordering::Relaxed) {
                let _ = engine.push(&chunk);
            }
        }
    }
    let after_push = rss_bytes().expect("statm");
    println!(
        "  {:<44} {:+8.2} MiB   (step {:+7.2} MiB)",
        "after pushing 4 s through every engine",
        mib(after_push as i64 - base as i64),
        mib(after_push as i64 - after_alloc as i64)
    );

    let predicted = (formula_audio_data_bytes(48_000, channels)
        + formula_audio_data_bytes(native_rate, channels) * 2)
        * instances as u64;
    println!(
        "  {:<44} {:8.2} MiB  (audio_data only)",
        "formula prediction",
        mib(predicted as i64)
    );
    std::hint::black_box(held);
}

fn main() {
    if rss_bytes().is_none() {
        println!("/proc/self/statm unavailable — this probe is Linux only.");
        return;
    }
    // One case per process. A case run after another reuses pages the allocator already took from
    // the OS, so its delta under-reports; isolation is what makes the numbers comparable.
    let args: Vec<String> = std::env::args().collect();
    match args.get(1).map(String::as_str) {
        Some("single") => {
            let rate: u32 = args[2].parse().expect("rate");
            let ch: usize = args[3].parse().expect("channels");
            probe_single(rate, ch);
        }
        Some("scaling") => {
            let rate: u32 = args[2].parse().expect("rate");
            let ch: usize = args[3].parse().expect("channels");
            probe_scaling(rate, ch);
        }
        Some("touched") => {
            let rate: u32 = args[2].parse().expect("rate");
            let ch: usize = args[3].parse().expect("channels");
            let instances: usize = args[4].parse().expect("instances");
            if let Some(secs) = args.get(5).and_then(|s| s.parse::<usize>().ok()) {
                PUSH_CHUNKS.store(secs * 10, std::sync::atomic::Ordering::Relaxed);
            }
            probe_touched(rate, ch, instances);
        }
        _ => {
            println!("usage: memory_contract_probe <single|scaling|touched> <rate> <channels> [instances]");
            println!("Formula side: docs/hypha_surround_ingest_capacity_20260918.md");
        }
    }
}
