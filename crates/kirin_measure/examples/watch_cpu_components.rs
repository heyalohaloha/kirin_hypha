//! Read-only, offline component timings. No product algorithm or acceptance threshold changes.
//! Run the same WAV and build profile on the validation machine; times are inclusive unless
//! explicitly compared as a differential experiment. This is not a DAW deadline benchmark.
use ebur128::{EbuR128, Mode};
use kirin_measure::{MeasureEngine, MeterSession, StereoMeter};
use std::hint::black_box;
use std::time::Instant;

struct Input {
    samples: Vec<f64>,
    rate: u32,
    channels: usize,
}

type Operation = Box<dyn FnMut(&[f64], usize)>;

fn measure(label: &str, input: &Input, frames: usize, factory: impl Fn() -> Operation) {
    let audio_seconds = input.samples.len() as f64 / input.channels as f64 / input.rate as f64;
    let mut trials = [0.0; 3];
    for elapsed in &mut trials {
        let mut operation = factory();
        let start = Instant::now();
        for (index, chunk) in input.samples.chunks(frames * input.channels).enumerate() {
            operation(black_box(chunk), index);
        }
        *elapsed = start.elapsed().as_secs_f64() * 1_000.0 / audio_seconds;
    }
    trials.sort_by(f64::total_cmp);
    println!(
        "COMPONENT {label} ms_per_audio_second={:.6} min={:.6} max={:.6} trials=3 chunk_frames={frames}",
        trials[1], trials[0], trials[2]
    );
}

fn ebu_case(input: &Input, label: &str, mode: Mode, query: u8, cadence: usize) {
    measure(label, input, input.rate as usize / 100, || {
        let mut ebu = EbuR128::new(input.channels as u32, input.rate, mode).unwrap();
        Box::new(move |chunk, index| {
            ebu.add_frames_f64(chunk).unwrap();
            if cadence != 0 && (index + 1).is_multiple_of(cadence) {
                if query & 1 != 0 {
                    black_box(ebu.loudness_momentary().unwrap());
                }
                if query & 2 != 0 {
                    black_box(ebu.loudness_shortterm().unwrap());
                }
            }
        })
    });
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = std::env::args()
        .nth(1)
        .ok_or("usage: watch_cpu_components <WAV>")?;
    let mut reader = hound::WavReader::open(&path)?;
    let spec = reader.spec();
    if spec.sample_rate != 48_000 || !(1..=2).contains(&spec.channels) {
        return Err("diagnostic requires 48 kHz mono/stereo WAV".into());
    }
    let samples: Vec<f64> = match spec.sample_format {
        hound::SampleFormat::Float => reader
            .samples::<f32>()
            .map(|value| value.map(f64::from))
            .collect::<Result<_, _>>()?,
        hound::SampleFormat::Int => {
            let scale = 2.0_f64.powi(i32::from(spec.bits_per_sample) - 1);
            reader
                .samples::<i32>()
                .map(|value| value.map(|sample| f64::from(sample) / scale))
                .collect::<Result<_, _>>()?
        }
    };
    let input = Input {
        samples,
        rate: spec.sample_rate,
        channels: usize::from(spec.channels),
    };
    if input.samples.is_empty()
        || !input.samples.len().is_multiple_of(input.channels)
        || input.samples.iter().any(|value| !value.is_finite())
    {
        return Err("empty, misaligned, or non-finite audio".into());
    }
    println!(
        "INPUT rate={} channels={} frames={} peak={:.9} debug_assertions={}",
        input.rate,
        input.channels,
        input.samples.len() / input.channels,
        input
            .samples
            .iter()
            .map(|value| value.abs())
            .fold(0.0, f64::max),
        cfg!(debug_assertions)
    );
    let loudness = Mode::M | Mode::S | Mode::I | Mode::LRA;
    let full = loudness | Mode::TRUE_PEAK;
    ebu_case(&input, "ebu_feed_no_true_peak", loudness, 0, 0);
    ebu_case(&input, "ebu_feed_true_peak_only", Mode::TRUE_PEAK, 0, 0);
    ebu_case(&input, "ebu_feed_full_no_queries", full, 0, 0);
    ebu_case(&input, "ebu_full_M_every_10ms", full, 1, 1);
    ebu_case(&input, "ebu_full_S_every_10ms", full, 2, 1);
    ebu_case(&input, "ebu_full_MS_every_10ms", full, 3, 1);
    ebu_case(&input, "ebu_full_MS_every_100ms", full, 3, 10);

    let frames = input.rate as usize / 10;
    measure("watch_measure_engine", &input, frames, || {
        let mut engine = MeasureEngine::new(input.rate, input.channels).unwrap();
        Box::new(move |chunk, _| {
            black_box(engine.push(chunk));
        })
    });
    measure("stereo_meter_including_true_peak", &input, frames, || {
        let mut stereo = StereoMeter::new(input.rate, input.channels).unwrap();
        Box::new(move |chunk, _| {
            assert!(stereo.push_observation(chunk));
            black_box(stereo.snapshot());
        })
    });
    measure(
        "meter_session_including_stereo_and_history",
        &input,
        frames,
        || {
            let mut session = MeterSession::new(input.rate, input.channels).unwrap();
            Box::new(move |chunk, _| {
                assert!(session.push_active(chunk));
                black_box(session.snapshot());
            })
        },
    );
    measure("watch_engine_plus_meter_session", &input, frames, || {
        let mut engine = MeasureEngine::new(input.rate, input.channels).unwrap();
        let mut session = MeterSession::new(input.rate, input.channels).unwrap();
        Box::new(move |chunk, _| {
            assert!(session.push_active(chunk));
            black_box(session.snapshot());
            black_box(engine.push(chunk));
        })
    });
    Ok(())
}
