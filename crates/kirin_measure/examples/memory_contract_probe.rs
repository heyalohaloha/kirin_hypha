//! Minimal RSS probe for the capture-generation memory contract (P-0 investigation).
//!
//! Reads `/proc/self/statm` between construction steps so the ingest contract's formula model can
//! be compared against what the process actually keeps resident. Formula values are upper bounds on
//! logical reservation; this measures the other side. Linux only — it prints and exits elsewhere.
//!
//! `cargo run -p kirin_measure --example memory_contract_probe --release`

use ebur128::{EbuR128, Mode};
use kirin_measure::phase_d::channels::PhaseDChannelStream;
use kirin_measure::phase_d::tables::FieldType;
use kirin_measure::resampler::ResamplerTo48k;
use kirin_measure::{
    AttackRuntime, MeasureEngine, SharpnessContinuousAnalyzer, SpectrumRuntime, StereoMeter,
};

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
    // Every channel has to carry signal, or an untouched region would be the test's doing rather
    // than the code's -- which is exactly the distinction §12 turns on.
    for c in 0..channels {
        assert!(
            chunk.iter().skip(c).step_by(channels).any(|v| *v != 0.0),
            "channel {c} of the probe signal is silent"
        );
    }
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

/// Everything the four ingest regions and the MeasureEngine audit leave out. Each subsystem is
/// built `instances` times, measured, then fed the same 4 s of audio and measured again, so the
/// untouched and resident sides stay separate the way §10 showed they must.
fn probe_census(native_rate: u32, channels: usize, instances: usize) {
    println!(
        "\n== subsystem census: native {native_rate} Hz, {channels} ch, {instances} instance(s) =="
    );
    println!(
        "  {:<34} {:>12} {:>12}",
        "subsystem", "allocated", "after feed"
    );

    let chunk_frames = native_rate as usize / 10;
    let chunk_f64: Vec<f64> = (0..chunk_frames * channels)
        .map(|i| ((i % 97) as f64 / 97.0) * 0.5 - 0.25)
        .collect();
    let chunk_f32: Vec<f32> = chunk_f64.iter().map(|v| *v as f32).collect();
    let pushes = 40usize;

    macro_rules! census {
        ($label:expr, $build:expr, $feed:expr) => {{
            // A subsystem that refuses this channel count is a result, not a crash: the stereo
            // guards are exactly what surround has to deal with, so record and carry on.
            let probe: Option<_> = $build;
            if probe.is_none() {
                println!("  {:<34} {:>12} {:>12}", $label, "rejected", "-");
            } else {
                drop(probe);
                let base = rss_bytes().expect("statm");
                let mut held: Vec<_> = (0..instances).filter_map(|_| $build).collect();
                let allocated = rss_bytes().expect("statm");
                #[allow(clippy::redundant_closure_call)]
                for item in held.iter_mut() {
                    ($feed)(item);
                }
                let fed = rss_bytes().expect("statm");
                println!(
                    "  {:<34} {:>+11.2} {:>+12.2}",
                    $label,
                    mib(allocated as i64 - base as i64),
                    mib(fed as i64 - base as i64)
                );
                std::hint::black_box(held);
            }
        }};
    }

    census!(
        "StereoMeter",
        StereoMeter::new(native_rate, channels).ok(),
        |m: &mut StereoMeter| {
            for _ in 0..pushes {
                m.push_observation(&chunk_f64);
            }
        }
    );
    census!(
        "PhaseDChannelStream",
        Some(PhaseDChannelStream::new(FieldType::Free, channels)),
        |s: &mut PhaseDChannelStream| {
            for _ in 0..pushes {
                let _ = s.push_interleaved_slot(&chunk_f64);
            }
        }
    );
    census!(
        "SharpnessContinuousAnalyzer",
        SharpnessContinuousAnalyzer::new(native_rate, channels).ok(),
        |_: &mut SharpnessContinuousAnalyzer| {}
    );
    census!(
        "SpectrumRuntime",
        {
            let r = SpectrumRuntime::new(native_rate, channels);
            r.set_enabled(true);
            Some(r)
        },
        |r: &mut std::sync::Arc<SpectrumRuntime>| {
            for i in 0..pushes {
                r.push_block_from_audio(&chunk_f32, channels, Some((i * chunk_frames) as i64));
            }
        }
    );
    census!(
        "AttackRuntime",
        AttackRuntime::new(native_rate, channels).ok().inspect(|r| {
            r.set_enabled(true);
        }),
        |r: &mut std::sync::Arc<AttackRuntime>| {
            for i in 0..pushes {
                r.push_block_from_audio(&chunk_f32, channels, Some((i * chunk_frames) as i64));
            }
        }
    );
    if native_rate != 48_000 {
        census!(
            "ResamplerTo48k",
            ResamplerTo48k::new(native_rate, channels).ok(),
            |r: &mut ResamplerTo48k| {
                let mut out = Vec::new();
                for _ in 0..pushes {
                    let _ = r.process(&chunk_f64, &mut out);
                }
            }
        );
    }
}

/// Calibrate the measurement itself: allocate a known zeroed f64 buffer, write one value per page,
/// and see whether RSS follows. Without this, a gap between formula and measurement cannot be
/// attributed to the code under test rather than to how residency is being read.
fn probe_calibration(mib_target: usize) {
    println!("\n== RSS calibration: {mib_target} MiB of zeroed f64 ==");
    let elements = mib_target * 1024 * 1024 / std::mem::size_of::<f64>();
    let base = rss_bytes().expect("statm");
    let mut buffer = vec![0.0f64; elements];
    let allocated = rss_bytes().expect("statm");
    println!(
        "  {:<34} {:>+11.2} MiB",
        "allocated (alloc_zeroed)",
        mib(allocated as i64 - base as i64)
    );
    // One write per 4 KiB page is all residency needs.
    let stride = 4096 / std::mem::size_of::<f64>();
    for i in (0..elements).step_by(stride) {
        buffer[i] = 1.0;
    }
    let touched = rss_bytes().expect("statm");
    println!(
        "  {:<34} {:>+11.2} MiB   ratio {:.3}",
        "after one write per page",
        mib(touched as i64 - base as i64),
        mib(touched as i64 - base as i64) / mib_target as f64
    );
    std::hint::black_box(buffer);
}

/// Production routing: measure_thread.rs:1022 feeds the 48 kHz Watch engine on every iteration,
/// while the two native-rate Record engines are fed only inside `if is_recording` (:1046). Feeding
/// all three measures a generation that is recording on every instance.
fn probe_routing(native_rate: u32, channels: usize, instances: usize) {
    println!("\n== routing: native {native_rate} Hz, {channels} ch, {instances} instance(s) ==");
    for (label, feed_record) in [
        ("Watch only (not recording)", false),
        ("Watch + Record x2", true),
    ] {
        let base = rss_bytes().expect("statm");
        let mut held: Vec<Vec<MeasureEngine>> = (0..instances)
            .map(|_| instance_engines(native_rate, channels))
            .collect();

        // The Watch engine always receives audio resampled to 48 kHz.
        let watch_frames = 4_800usize;
        let watch_chunk: Vec<f64> = (0..watch_frames * channels)
            .map(|i| ((i % 97) as f64 / 97.0) * 0.5 - 0.25)
            .collect();
        let native_frames = native_rate as usize / 10;
        let native_chunk: Vec<f64> = (0..native_frames * channels)
            .map(|i| ((i % 97) as f64 / 97.0) * 0.5 - 0.25)
            .collect();

        for engines in held.iter_mut() {
            for _ in 0..40 {
                let _ = engines[0].push(&watch_chunk);
            }
            if feed_record {
                for engine in engines.iter_mut().skip(1) {
                    for _ in 0..40 {
                        let _ = engine.push(&native_chunk);
                    }
                }
            }
        }
        let after = rss_bytes().expect("statm");
        println!(
            "  {label:<30} {:>+10.2} MiB",
            mib(after as i64 - base as i64)
        );
        std::hint::black_box(held);
        if !feed_record {
            // Drop before the next case so the two are not measured on top of each other.
            continue;
        }
    }
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
        Some("routing") => {
            probe_routing(
                args[2].parse().expect("rate"),
                args[3].parse().expect("channels"),
                args[4].parse().expect("instances"),
            );
        }
        Some("calibrate") => {
            probe_calibration(args[2].parse().expect("MiB"));
        }
        Some("census") => {
            let rate: u32 = args[2].parse().expect("rate");
            let ch: usize = args[3].parse().expect("channels");
            let instances: usize = args[4].parse().expect("instances");
            probe_census(rate, ch, instances);
        }
        _ => {
            println!("usage: memory_contract_probe <single|scaling|touched|census|routing|calibrate> <rate> <channels> [instances]");
            println!("Formula side: docs/hypha_surround_ingest_capacity_20260918.md");
        }
    }
}
