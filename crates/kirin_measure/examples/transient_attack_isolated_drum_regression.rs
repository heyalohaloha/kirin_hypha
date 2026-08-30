//! Real-sample regression for isolated kick and snare use of ATTACK DRUM.
//!
//! The input directories stay outside the repository. This tool stores only file names, hashes,
//! formats, and detector results; it never copies or publishes licensed audio.

use std::collections::{BTreeMap, HashSet};
use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process;

use kirin_measure::analyze_drum_attacks_interleaved_offline;
use serde::Serialize;
use sha2::{Digest, Sha256};

#[path = "transient_attack_isolated_drum_regression/wav.rs"]
mod wav;

const MINIMUM_FILES_PER_CLASS: usize = 50;
const MINIMUM_GROUPS_PER_CLASS: usize = 2;
const MINIMUM_INITIAL_COVERAGE: f64 = 0.95;
const INITIAL_WINDOW_MILLIS: u64 = 250;

#[derive(Debug)]
struct Cli {
    kick_dirs: Vec<PathBuf>,
    snare_dirs: Vec<PathBuf>,
    result: PathBuf,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Serialize)]
#[serde(rename_all = "snake_case")]
enum DrumClass {
    Kick,
    Snare,
}

#[derive(Debug)]
struct InputFile {
    class: DrumClass,
    source_group: String,
    path: PathBuf,
}

#[derive(Clone, Debug, Serialize)]
struct FileResult {
    class: DrumClass,
    source_group: String,
    file_name: String,
    raw_sha256: String,
    channels: u16,
    bits_per_sample: u16,
    sample_count: u64,
    peak_abs: f32,
    events: Vec<EventPoint>,
    initial_window_detected: bool,
}

#[derive(Clone, Debug, Serialize)]
struct EventPoint {
    sample: i64,
    millis: f64,
    value: f32,
}

#[derive(Clone, Debug, Serialize)]
struct ClassSummary {
    class: DrumClass,
    files: usize,
    source_groups: usize,
    initial_window_detected: usize,
    initial_coverage: f64,
    total_events: usize,
    p95_events_per_file: usize,
    coverage_gate_passed: bool,
    event_count_gate_eligible: bool,
    all_gates_passed: bool,
}

#[derive(Serialize)]
struct Artifact<'a> {
    schema: &'static str,
    status: &'static str,
    profile: &'static str,
    input_scope: &'static str,
    publication_eligible: bool,
    detector_contract: &'static str,
    gates: GateContract,
    kick: &'a ClassSummary,
    snare: &'a ClassSummary,
    files: &'a [FileResult],
    deterministic_result_sha256: String,
}

#[derive(Serialize)]
struct GateContract {
    minimum_files_per_class: usize,
    minimum_source_groups_per_class: usize,
    initial_window_millis: u64,
    minimum_initial_coverage: f64,
    event_count_interpretation: &'static str,
}

#[derive(Serialize)]
struct DigestInput<'a> {
    schema: &'static str,
    detector_contract: &'static str,
    kick: &'a ClassSummary,
    snare: &'a ClassSummary,
    files: &'a [FileResult],
}

fn main() {
    run().unwrap_or_else(|error| {
        eprintln!("ATTACK isolated-drum regression failed: {error}");
        process::exit(1);
    });
}

fn run() -> Result<(), String> {
    let cli = Cli::parse(std::env::args_os().skip(1))?;
    if cli.result.exists() {
        return Err(format!("result already exists: {}", cli.result.display()));
    }
    let mut inputs = collect_inputs(DrumClass::Kick, &cli.kick_dirs)?;
    inputs.extend(collect_inputs(DrumClass::Snare, &cli.snare_dirs)?);
    let mut raw_hashes = HashSet::new();
    let mut files = Vec::with_capacity(inputs.len());
    for input in inputs {
        let result = analyze_file(input)?;
        if !raw_hashes.insert(result.raw_sha256.clone()) {
            return Err(format!("duplicate audio bytes: {}", result.file_name));
        }
        files.push(result);
    }
    files.sort_by(|left, right| {
        (left.class, &left.source_group, &left.file_name).cmp(&(
            right.class,
            &right.source_group,
            &right.file_name,
        ))
    });
    let kick = summarize(DrumClass::Kick, &files);
    let snare = summarize(DrumClass::Snare, &files);
    let detector_contract =
        "B-553 fixed SuperFlux 2048/12bpo/r0/-50dBFS + causal local mean delta .00625 + 30ms refractory";
    let digest = DigestInput {
        schema: "kirin-hypha-attack-isolated-drum-regression-v1",
        detector_contract,
        kick: &kick,
        snare: &snare,
        files: &files,
    };
    let deterministic_result_sha256 = sha256(
        &serde_json::to_vec(&digest)
            .map_err(|error| format!("cannot serialize regression digest: {error}"))?,
    );
    let passed = kick.all_gates_passed && snare.all_gates_passed;
    let artifact = Artifact {
        schema: "kirin-hypha-attack-isolated-drum-regression-v1",
        status: if passed {
            "isolated_kick_and_snare_regression_pass"
        } else {
            "isolated_kick_or_snare_regression_fail"
        },
        profile: "DRUM",
        input_scope: "percussion-dominant buses plus isolated kick and snare tracks",
        publication_eligible: false,
        detector_contract,
        gates: GateContract {
            minimum_files_per_class: MINIMUM_FILES_PER_CLASS,
            minimum_source_groups_per_class: MINIMUM_GROUPS_PER_CLASS,
            initial_window_millis: INITIAL_WINDOW_MILLIS,
            minimum_initial_coverage: MINIMUM_INITIAL_COVERAGE,
            event_count_interpretation:
                "diagnostic_only_without_candidate_blind_audible_attack_count_reference",
        },
        kick: &kick,
        snare: &snare,
        files: &files,
        deterministic_result_sha256,
    };
    publish_create_new(
        &cli.result,
        &serde_json::to_vec_pretty(&artifact)
            .map_err(|error| format!("cannot serialize regression result: {error}"))?,
    )?;
    println!(
        "ATTACK isolated-drum regression: kick={}/{} ({:.3}) snare={}/{} ({:.3}) p95_events={}/{} status={} result={}",
        kick.initial_window_detected,
        kick.files,
        kick.initial_coverage,
        snare.initial_window_detected,
        snare.files,
        snare.initial_coverage,
        kick.p95_events_per_file,
        snare.p95_events_per_file,
        if passed { "pass" } else { "fail" },
        cli.result.display(),
    );
    Ok(())
}

impl Cli {
    fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut kick_dirs = Vec::new();
        let mut snare_dirs = Vec::new();
        let mut scalar = BTreeMap::<String, OsString>::new();
        let mut arguments = arguments.into_iter();
        while let Some(flag) = arguments.next() {
            let flag = flag
                .to_str()
                .ok_or("CLI flag is not valid UTF-8")?
                .to_string();
            let value = arguments
                .next()
                .ok_or_else(|| format!("missing value for {flag}"))?;
            match flag.as_str() {
                "--kick-dir" => kick_dirs.push(PathBuf::from(value)),
                "--snare-dir" => snare_dirs.push(PathBuf::from(value)),
                "--profile" | "--result" => {
                    if scalar.insert(flag.clone(), value).is_some() {
                        return Err(format!("duplicate CLI flag: {flag}"));
                    }
                }
                _ => return Err(format!("unknown CLI flag: {flag}")),
            }
        }
        let profile = take_string(&mut scalar, "--profile")?;
        if profile != "DRUM" {
            return Err("isolated-drum regression permits only --profile DRUM".to_string());
        }
        let result = PathBuf::from(scalar.remove("--result").ok_or("missing --result")?);
        if kick_dirs.len() < MINIMUM_GROUPS_PER_CLASS || snare_dirs.len() < MINIMUM_GROUPS_PER_CLASS
        {
            return Err(format!(
                "at least {MINIMUM_GROUPS_PER_CLASS} kick and snare source directories are required"
            ));
        }
        Ok(Self {
            kick_dirs,
            snare_dirs,
            result,
        })
    }
}

fn take_string(values: &mut BTreeMap<String, OsString>, key: &str) -> Result<String, String> {
    values
        .remove(key)
        .ok_or_else(|| format!("missing {key}"))?
        .into_string()
        .map_err(|_| format!("{key} is not valid UTF-8"))
}

fn collect_inputs(class: DrumClass, directories: &[PathBuf]) -> Result<Vec<InputFile>, String> {
    let mut inputs = Vec::new();
    let mut groups = HashSet::new();
    for directory in directories {
        let directory = fs::canonicalize(directory)
            .map_err(|error| format!("source directory {}: {error}", directory.display()))?;
        if !directory.is_dir() {
            return Err(format!(
                "source is not a directory: {}",
                directory.display()
            ));
        }
        let source_group = source_group_name(&directory)?;
        if !groups.insert(source_group.clone()) {
            return Err(format!("duplicate source group name: {source_group}"));
        }
        let mut group_files = fs::read_dir(&directory)
            .map_err(|error| format!("source directory {}: {error}", directory.display()))?
            .map(|entry| entry.map_err(|error| error.to_string()))
            .collect::<Result<Vec<_>, String>>()?;
        group_files.sort_by_key(|entry| entry.file_name());
        let mut wav_count = 0;
        for entry in group_files {
            let path = entry.path();
            if entry
                .file_type()
                .map_err(|error| error.to_string())?
                .is_file()
                && path
                    .extension()
                    .and_then(|extension| extension.to_str())
                    .is_some_and(|extension| extension.eq_ignore_ascii_case("wav"))
            {
                wav_count += 1;
                inputs.push(InputFile {
                    class,
                    source_group: source_group.clone(),
                    path,
                });
            }
        }
        if wav_count == 0 {
            return Err(format!("source group has no WAV files: {source_group}"));
        }
    }
    Ok(inputs)
}

fn source_group_name(directory: &Path) -> Result<String, String> {
    let name = directory
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or("source directory name is not valid UTF-8")?;
    let parent = directory
        .parent()
        .and_then(Path::file_name)
        .and_then(|name| name.to_str())
        .ok_or("source directory parent is not valid UTF-8")?;
    Ok(format!("{parent}/{name}"))
}

fn analyze_file(input: InputFile) -> Result<FileResult, String> {
    let bytes = fs::read(&input.path)
        .map_err(|error| format!("audio {}: {error}", input.path.display()))?;
    let raw_sha256 = sha256(&bytes);
    let decoded = wav::decode_integer_pcm(&bytes)
        .map_err(|error| format!("audio {}: {error}", input.path.display()))?;
    let peak_abs_pcm24 = decoded
        .pcm24
        .iter()
        .map(|sample| i64::from(*sample).unsigned_abs())
        .max()
        .ok_or("decoded WAV has no samples")?;
    if peak_abs_pcm24 == 0 {
        return Err(format!("audio is exact silence: {}", input.path.display()));
    }
    let samples = decoded
        .pcm24
        .iter()
        .map(|sample| *sample as f32 / 8_388_608.0)
        .collect::<Vec<_>>();
    let events = analyze_drum_attacks_interleaved_offline(
        &samples,
        decoded.metadata.sample_rate,
        usize::from(decoded.metadata.channels),
    )
    .map_err(str::to_string)?;
    let initial_end =
        i64::from(decoded.metadata.sample_rate) * INITIAL_WINDOW_MILLIS as i64 / 1_000;
    let initial_window_detected = events.iter().any(|event| event.event_sample < initial_end);
    let events = events
        .into_iter()
        .map(|event| EventPoint {
            sample: event.event_sample,
            millis: event.event_sample as f64 * 1_000.0 / f64::from(decoded.metadata.sample_rate),
            value: event.value,
        })
        .collect();
    let file_name = input
        .path
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or("audio file name is not valid UTF-8")?
        .to_string();
    Ok(FileResult {
        class: input.class,
        source_group: input.source_group,
        file_name,
        raw_sha256,
        channels: decoded.metadata.channels,
        bits_per_sample: decoded.metadata.bits_per_sample,
        sample_count: decoded.metadata.sample_frames,
        peak_abs: peak_abs_pcm24 as f32 / 8_388_608.0,
        events,
        initial_window_detected,
    })
}

fn summarize(class: DrumClass, files: &[FileResult]) -> ClassSummary {
    let members = files
        .iter()
        .filter(|file| file.class == class)
        .collect::<Vec<_>>();
    let source_groups = members
        .iter()
        .map(|file| file.source_group.as_str())
        .collect::<HashSet<_>>()
        .len();
    let initial_window_detected = members
        .iter()
        .filter(|file| file.initial_window_detected)
        .count();
    let mut event_counts = members
        .iter()
        .map(|file| file.events.len())
        .collect::<Vec<_>>();
    event_counts.sort_unstable();
    let p95_index = event_counts
        .len()
        .saturating_mul(95)
        .div_ceil(100)
        .saturating_sub(1);
    let p95_events_per_file = event_counts.get(p95_index).copied().unwrap_or(usize::MAX);
    let initial_coverage = if members.is_empty() {
        0.0
    } else {
        initial_window_detected as f64 / members.len() as f64
    };
    let coverage_gate_passed = members.len() >= MINIMUM_FILES_PER_CLASS
        && source_groups >= MINIMUM_GROUPS_PER_CLASS
        && initial_coverage >= MINIMUM_INITIAL_COVERAGE;
    ClassSummary {
        class,
        files: members.len(),
        source_groups,
        initial_window_detected,
        initial_coverage,
        total_events: event_counts.iter().sum(),
        p95_events_per_file,
        coverage_gate_passed,
        event_count_gate_eligible: false,
        all_gates_passed: coverage_gate_passed,
    }
}

fn sha256(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

fn publish_create_new(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|error| format!("result {}: {error}", path.display()))?;
    file.write_all(bytes)
        .and_then(|_| file.write_all(b"\n"))
        .and_then(|_| file.sync_all())
        .map_err(|error| format!("result {}: {error}", path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(profile: &str) -> Vec<OsString> {
        [
            "--profile",
            profile,
            "--kick-dir",
            "kick-a",
            "--kick-dir",
            "kick-b",
            "--snare-dir",
            "snare-a",
            "--snare-dir",
            "snare-b",
            "--result",
            "result.json",
        ]
        .into_iter()
        .map(OsString::from)
        .collect()
    }

    #[test]
    fn cli_keeps_drum_explicit_and_requires_two_independent_groups() {
        assert!(Cli::parse(args("DRUM")).is_ok());
        assert!(Cli::parse(args("2MIX")).unwrap_err().contains("DRUM"));
        let mut insufficient = args("DRUM");
        insufficient.drain(4..6);
        assert!(Cli::parse(insufficient).unwrap_err().contains("at least 2"));
    }

    #[test]
    fn summary_gates_presence_and_keeps_event_count_diagnostic() {
        let files = (0..50)
            .map(|index| FileResult {
                class: DrumClass::Kick,
                source_group: if index % 2 == 0 { "a" } else { "b" }.to_string(),
                file_name: format!("{index}.wav"),
                raw_sha256: format!("{index:064x}"),
                channels: 1,
                bits_per_sample: 24,
                sample_count: 44_100,
                peak_abs: 0.5,
                events: (0..if index == 0 { 3 } else { 1 })
                    .map(|_| EventPoint {
                        sample: 0,
                        millis: 0.0,
                        value: 1.0,
                    })
                    .collect(),
                initial_window_detected: index < 48,
            })
            .collect::<Vec<_>>();
        let summary = summarize(DrumClass::Kick, &files);
        assert_eq!(summary.initial_coverage, 0.96);
        assert_eq!(summary.p95_events_per_file, 1);
        assert!(!summary.event_count_gate_eligible);
        assert!(summary.all_gates_passed);
    }
}
