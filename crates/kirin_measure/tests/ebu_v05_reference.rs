//! Local-only complete EBU Loudness Test Set v05 gate.
//!
//! The licensed EBU WAVs stay outside this repository. The test verifies the immutable archive,
//! decodes all 70 WAVs, exercises the Hypha `MeasureEngine`, checks every Tech 3341/3342 minimum
//! requirement represented by the archive, and records the four alignment/reference files.
//!
//! Run with:
//! `KIRIN_EBU_TEST_SET_ZIP=/absolute/path/to/ebu-loudness-test-set-v05.zip \
//!  cargo test -p kirin_measure --test ebu_v05_reference -- --ignored --nocapture`

#[path = "support/ebu_v05.rs"]
mod support;

use std::path::Path;

use support::{fmt, Measurement};

const LUFS_TOLERANCE: f64 = 0.1;
const LRA_TOLERANCE: f64 = 1.0;
const WRAPPER_TOLERANCE: f64 = 0.000_001;

fn close(actual: Option<f64>, expected: f64, tolerance: f64) -> bool {
    actual.is_some_and(|actual| (actual - expected).abs() <= tolerance + f64::EPSILON)
}

fn record_check(
    failures: &mut Vec<String>,
    name: &str,
    metric: &str,
    actual: Option<f64>,
    expected: f64,
    tolerance: f64,
) {
    if !close(actual, expected, tolerance) {
        failures.push(format!(
            "{name}: {metric}={} expected {expected:.3} ±{tolerance:.3}",
            fmt(actual)
        ));
    }
}

fn record_range(
    failures: &mut Vec<String>,
    name: &str,
    metric: &str,
    actual: Option<f64>,
    minimum: f64,
    maximum: f64,
) {
    if !actual.is_some_and(|actual| actual >= minimum && actual <= maximum) {
        failures.push(format!(
            "{name}: {metric}={} expected [{minimum:.3}, {maximum:.3}]",
            fmt(actual)
        ));
    }
}

fn check_wrapper_parity(failures: &mut Vec<String>, measurement: &Measurement) {
    for (metric, hypha, reference) in [
        (
            "I parity",
            measurement.hypha.lufs_i,
            measurement.reference_i,
        ),
        (
            "LRA parity",
            measurement.hypha.lra,
            measurement.reference_lra,
        ),
        (
            "Max TP parity",
            measurement.hypha.max_true_peak,
            measurement.reference_max_true_peak,
        ),
    ] {
        match (hypha, reference) {
            (Some(hypha), Some(reference)) if (hypha - reference).abs() <= WRAPPER_TOLERANCE => {}
            (None, None) => {}
            _ => failures.push(format!(
                "{}: {metric} Hypha={} reference={}",
                measurement.name,
                fmt(hypha),
                fmt(reference)
            )),
        }
    }
}

fn print_row(measurement: &Measurement) {
    println!(
        "EBU_V05\t{}\t{}ch\t{}Hz\t{:.3}s\tM={}\tS={}\tI={}\tLRA={}\tMaxTP={}\tpending_frames={}",
        measurement.name,
        measurement.channels,
        measurement.sample_rate,
        measurement.source_frames as f64 / measurement.sample_rate as f64,
        fmt(measurement.last_m()),
        fmt(measurement.last_s()),
        fmt(measurement.hypha.lufs_i),
        fmt(measurement.hypha.lra),
        fmt(measurement.hypha.max_true_peak),
        measurement.source_frames - measurement.observed_frames,
    );
}

fn measure_checked(archive: &Path, name: &str, failures: &mut Vec<String>) -> Measurement {
    let measurement = support::measure(archive, name);
    check_wrapper_parity(failures, &measurement);
    print_row(&measurement);
    measurement
}

#[test]
#[ignore = "requires separately supplied EBU Loudness Test Set v05 archive"]
fn all_70_wavs_pass_tech3341_tech3342_and_hypha_wrapper() {
    let archive = support::archive_path();
    assert_eq!(support::archive_sha256(&archive), support::ARCHIVE_SHA256);
    let actual_names = support::archive_wav_names(&archive);
    let expected_names = support::expected_wav_names();
    assert_eq!(
        actual_names.len(),
        70,
        "the v05 archive must contain 70 WAVs"
    );
    assert_eq!(actual_names, expected_names, "the 70-WAV manifest changed");

    let mut failures = Vec::new();

    for (name, expected) in [
        ("1kHz Sine -20 LUFS-16bit.wav", -20.0),
        ("1kHz Sine -26 LUFS-16bit.wav", -26.0),
        ("1kHz Sine -40 LUFS-16bit.wav", -40.0),
        (
            "EBU-reference_listening_signal_pinknoise_500Hz_2kHz_R128.wav",
            -23.0,
        ),
    ] {
        let measured = measure_checked(&archive, name, &mut failures);
        record_check(
            &mut failures,
            name,
            "alignment I",
            measured.hypha.lufs_i,
            expected,
            LUFS_TOLERANCE,
        );
    }

    for (case, name, expected) in [
        (1, "seq-3341-1-16bit.wav", -23.0),
        (2, "seq-3341-2-16bit.wav", -33.0),
    ] {
        let measured = measure_checked(&archive, name, &mut failures);
        record_check(
            &mut failures,
            name,
            "I",
            measured.hypha.lufs_i,
            expected,
            LUFS_TOLERANCE,
        );
        for point in measured.points.iter().skip(10) {
            record_check(
                &mut failures,
                name,
                &format!("M at {:.1}s (case {case})", point.frames as f64 / 48_000.0),
                point.lufs_m,
                expected,
                LUFS_TOLERANCE,
            );
        }
        for point in measured.points.iter().skip(29) {
            record_check(
                &mut failures,
                name,
                &format!("S at {:.1}s (case {case})", point.frames as f64 / 48_000.0),
                point.lufs_s,
                expected,
                LUFS_TOLERANCE,
            );
        }
    }

    for (name, expected) in [
        ("seq-3341-3-16bit-v02.wav", -23.0),
        ("seq-3341-4-16bit-v02.wav", -23.0),
        ("seq-3341-5-16bit-v02.wav", -23.0),
        ("seq-3341-6-5channels-16bit.wav", -23.0),
        ("seq-3341-6-6channels-WAVEEX-16bit.wav", -23.0),
        ("seq-3341-7_seq-3342-5-24bit.wav", -23.0),
        ("seq-3341-2011-8_seq-3342-6-24bit-v02.wav", -23.0),
    ] {
        let measured = measure_checked(&archive, name, &mut failures);
        record_check(
            &mut failures,
            name,
            "I",
            measured.hypha.lufs_i,
            expected,
            LUFS_TOLERANCE,
        );
    }

    let case_9 = measure_checked(&archive, "seq-3341-9-24bit.wav", &mut failures);
    for point in case_9.points.iter().skip(29) {
        record_check(
            &mut failures,
            &case_9.name,
            &format!("S at {:.1}s", point.frames as f64 / 48_000.0),
            point.lufs_s,
            -23.0,
            LUFS_TOLERANCE,
        );
    }

    for index in 1..=20 {
        let name = format!("seq-3341-10-{index}-24bit.wav");
        let measured = measure_checked(&archive, &name, &mut failures);
        record_check(
            &mut failures,
            &name,
            "Max S",
            measured.max_s(),
            -23.0,
            LUFS_TOLERANCE,
        );
    }

    let case_11 = measure_checked(&archive, "seq-3341-11-24bit.wav", &mut failures);
    for segment in 0..20 {
        record_check(
            &mut failures,
            &case_11.name,
            &format!("segment {} Max S", segment + 1),
            case_11.segment_max_s(segment),
            -38.0 + segment as f64,
            LUFS_TOLERANCE,
        );
    }

    let case_12 = measure_checked(&archive, "seq-3341-12-24bit.wav", &mut failures);
    for point in case_12.points.iter().skip(10) {
        record_check(
            &mut failures,
            &case_12.name,
            &format!("M at {:.1}s", point.frames as f64 / 48_000.0),
            point.lufs_m,
            -23.0,
            LUFS_TOLERANCE,
        );
    }

    for index in 1..=20 {
        let name = format!(
            "seq-3341-13-{index}-24bit.wav{}",
            if index > 2 { ".wav" } else { "" }
        );
        let measured = measure_checked(&archive, &name, &mut failures);
        record_check(
            &mut failures,
            &name,
            "Max M",
            measured.max_m(),
            -23.0,
            LUFS_TOLERANCE,
        );
    }

    let case_14 = measure_checked(&archive, "seq-3341-14-24bit.wav.wav", &mut failures);
    for segment in 0..20 {
        record_check(
            &mut failures,
            &case_14.name,
            &format!("segment {} Max M", segment + 1),
            case_14.segment_max_m(segment),
            -38.0 + segment as f64,
            LUFS_TOLERANCE,
        );
    }

    for case in 15..=23 {
        let name = format!("seq-3341-{case}-24bit.wav.wav");
        let expected = match case {
            15..=18 => -6.0,
            19 => 3.0,
            20..=23 => 0.0,
            _ => unreachable!(),
        };
        let measured = measure_checked(&archive, &name, &mut failures);
        record_range(
            &mut failures,
            &name,
            "Max TP",
            measured.hypha.max_true_peak,
            expected - 0.4,
            expected + 0.2,
        );
    }

    for (name, expected) in [
        ("seq-3342-1-16bit.wav", 10.0),
        ("seq-3342-2-16bit.wav", 5.0),
        ("seq-3342-3-16bit.wav", 20.0),
        ("seq-3342-4-16bit.wav", 15.0),
        ("seq-3341-7_seq-3342-5-24bit.wav", 5.0),
        ("seq-3341-2011-8_seq-3342-6-24bit-v02.wav", 15.0),
    ] {
        let measured = measure_checked(&archive, name, &mut failures);
        record_check(
            &mut failures,
            name,
            "LRA",
            measured.hypha.lra,
            expected,
            LRA_TOLERANCE,
        );
    }

    assert!(
        failures.is_empty(),
        "EBU v05 failures ({}):\n{}",
        failures.len(),
        failures.join("\n")
    );
}
