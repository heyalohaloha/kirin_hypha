//! Hypha True Peak self-check through the public measurement engine path.
//!
//! This is the reportable path for public self-verification:
//! WAV test signal -> `MeasureEngine::push()` in 100 ms chunks -> `finalize().max_true_peak`.
//! It is intentionally separate from `tp_offline_reference.rs`, which checks the
//! lower-level offline reference calculation.
//!
//! Run:
//!   cargo test -p kirin_measure --test tp_engine_self_check -- --nocapture

use kirin_measure::engine::MeasureEngine;
use std::fs::File;
use std::path::Path;
use symphonia::core::audio::{AudioBufferRef, Signal};
use symphonia::core::codecs::{DecoderOptions, CODEC_TYPE_NULL};
use symphonia::core::formats::FormatOptions;
use symphonia::core::io::MediaSourceStream;
use symphonia::core::meta::MetadataOptions;
use symphonia::core::probe::Hint;

const SIGNALS_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../test_signals");
const STRICT_SELF_CHECK_TOLERANCE_DBTP: f64 = 0.1;

struct TpSignal {
    filename: &'static str,
    label: &'static str,
    expected_dbtp: f64,
}

fn true_peak_signals() -> &'static [TpSignal] {
    &[
        TpSignal {
            filename: "06_truepeak_phase0.wav",
            label: "997 Hz 0 dBFS phase 0",
            expected_dbtp: 0.0,
        },
        TpSignal {
            filename: "07_truepeak_phase1.wav",
            label: "997 Hz 0 dBFS phase 1",
            expected_dbtp: 0.0,
        },
        TpSignal {
            filename: "08_truepeak_phase2.wav",
            label: "997 Hz 0 dBFS phase 2",
            expected_dbtp: 0.0,
        },
        TpSignal {
            filename: "09_truepeak_phase3.wav",
            label: "997 Hz 0 dBFS phase 3",
            expected_dbtp: 0.0,
        },
        TpSignal {
            filename: "10_truepeak_near_nyquist.wav",
            label: "11760 Hz -6 dBFS near-Nyquist",
            expected_dbtp: -6.0,
        },
    ]
}

fn decode_wav(path: &str) -> Result<(Vec<f64>, u32, usize), String> {
    let file = File::open(path).map_err(|e| format!("open {path}: {e}"))?;
    let mss = MediaSourceStream::new(Box::new(file), Default::default());
    let mut hint = Hint::new();
    if let Some(ext) = Path::new(path).extension().and_then(|e| e.to_str()) {
        hint.with_extension(ext);
    }
    let probed = symphonia::default::get_probe()
        .format(
            &hint,
            mss,
            &FormatOptions::default(),
            &MetadataOptions::default(),
        )
        .map_err(|e| format!("probe {path}: {e}"))?;
    let mut format = probed.format;
    let track = format
        .tracks()
        .iter()
        .find(|t| t.codec_params.codec != CODEC_TYPE_NULL)
        .ok_or_else(|| format!("no audio track: {path}"))?;
    let track_id = track.id;
    let sample_rate = track
        .codec_params
        .sample_rate
        .ok_or_else(|| format!("no sample_rate: {path}"))?;
    let channels = track
        .codec_params
        .channels
        .map(|c| c.count())
        .ok_or_else(|| format!("no channels: {path}"))?;

    let mut decoder = symphonia::default::get_codecs()
        .make(&track.codec_params, &DecoderOptions::default())
        .map_err(|e| format!("decoder {path}: {e}"))?;

    let mut interleaved = Vec::new();
    loop {
        use symphonia::core::errors::Error as SymphoniaError;
        let packet = match format.next_packet() {
            Ok(packet) => packet,
            Err(SymphoniaError::IoError(ref e))
                if e.kind() == std::io::ErrorKind::UnexpectedEof =>
            {
                break
            }
            Err(e) => return Err(format!("packet {path}: {e}")),
        };
        if packet.track_id() != track_id {
            continue;
        }

        let decoded = decoder
            .decode(&packet)
            .map_err(|e| format!("decode {path}: {e}"))?;
        match decoded {
            AudioBufferRef::S24(buffer) => {
                let nch = buffer.spec().channels.count().min(channels);
                for frame in 0..buffer.frames() {
                    for ch in 0..nch {
                        interleaved.push(buffer.chan(ch)[frame].inner() as f64 / 8_388_607.0);
                    }
                }
            }
            AudioBufferRef::F32(buffer) => {
                let nch = buffer.spec().channels.count().min(channels);
                for frame in 0..buffer.frames() {
                    for ch in 0..nch {
                        interleaved.push(buffer.chan(ch)[frame] as f64);
                    }
                }
            }
            AudioBufferRef::F64(buffer) => {
                let nch = buffer.spec().channels.count().min(channels);
                for frame in 0..buffer.frames() {
                    for ch in 0..nch {
                        interleaved.push(buffer.chan(ch)[frame]);
                    }
                }
            }
            _ => return Err(format!("unsupported sample format: {path}")),
        }
    }

    Ok((interleaved, sample_rate, channels))
}

fn hypha_engine_session_true_peak_dbtp(filename: &str) -> Result<f64, String> {
    let path = format!("{SIGNALS_DIR}/{filename}");
    let (interleaved, sample_rate, channels) = decode_wav(&path)?;
    let mut engine = MeasureEngine::new(sample_rate, channels)?;
    let chunk_elems = (sample_rate as usize / 10) * channels;

    for chunk in interleaved.chunks(chunk_elems) {
        let _ = engine.push(chunk);
    }

    engine
        .finalize()
        .max_true_peak
        .ok_or_else(|| format!("max_true_peak was None: {filename}"))
}

#[test]
fn hypha_engine_true_peak_self_check_report() {
    println!();
    println!("Hypha True Peak self-check via MeasureEngine");
    println!("path: WAV -> MeasureEngine::push(100 ms chunks) -> finalize().max_true_peak");
    println!("tolerance for this self-check: +/-{STRICT_SELF_CHECK_TOLERANCE_DBTP:.3} dBTP");
    println!(
        "{:<34} {:>10} {:>10} {:>10}  result",
        "signal", "expected", "measured", "error"
    );

    let mut all_pass = true;
    for signal in true_peak_signals() {
        let measured = hypha_engine_session_true_peak_dbtp(signal.filename)
            .unwrap_or_else(|e| panic!("{}: {e}", signal.filename));
        let error = measured - signal.expected_dbtp;
        let passed = error.abs() <= STRICT_SELF_CHECK_TOLERANCE_DBTP;
        all_pass &= passed;
        println!(
            "{:<34} {:>+9.3} {:>+10.3} {:>+10.3}  {}",
            signal.label,
            signal.expected_dbtp,
            measured,
            error,
            if passed { "PASS" } else { "FAIL" }
        );
    }

    assert!(
        all_pass,
        "one or more True Peak self-check signals exceeded tolerance"
    );
}
