//! ATTACK DRUM development scorer.
//!
//! This tool measures frozen B-550 development data and never authorizes a
//! winner, a fresh holdout run, or a public ATTACK build.

use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process;

use kirin_measure::{SuperFluxAnalyzer, TransientCandidateAnalyzer, TransientOdfKind};
use serde::Serialize;

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
#[path = "transient_candidate_eval/pilot_input.rs"]
mod pilot_input;

use contract::{sha256_bytes, CandidateArtifact, CandidateConfig, FormalAnalyzer};
use evaluation::formal_evaluation::{evaluate_development_pilot, DevelopmentPilotEvaluation};
use input::read_development_pilot_selection;
use pilot_input::load_development_pilot_track;

#[derive(Debug)]
struct Cli {
    root: PathBuf,
    manifest: PathBuf,
    folds: PathBuf,
    candidate: PathBuf,
    result: PathBuf,
}

#[derive(Serialize)]
struct PilotArtifact<'a> {
    schema: &'static str,
    status: &'static str,
    profile: &'static str,
    publication_eligible: bool,
    winner_eligible: bool,
    fresh_holdout_eligible: bool,
    manifest_sha256: &'a str,
    folds_sha256: &'a str,
    candidate: CandidateEvidence<'a>,
    evaluation: &'a DevelopmentPilotEvaluation,
    deterministic_result_sha256: String,
}

#[derive(Serialize)]
struct CandidateEvidence<'a> {
    id: &'a str,
    raw_config_sha256: &'a str,
    semantic_config_sha256: &'a str,
    measurement_definition_sha256: &'a str,
    config: &'a CandidateConfig,
}

#[derive(Serialize)]
struct DeterministicCore<'a> {
    schema: &'static str,
    manifest_sha256: &'a str,
    folds_sha256: &'a str,
    candidate_semantic_sha256: &'a str,
    measurement_definition_sha256: &'a str,
    evaluation: &'a DevelopmentPilotEvaluation,
}

fn main() {
    run().unwrap_or_else(|error| {
        eprintln!("ATTACK DRUM pilot failed: {error}");
        process::exit(1);
    });
}

fn run() -> Result<(), String> {
    let cli = Cli::parse(std::env::args_os().skip(1))?;
    if cli.result.exists() {
        return Err(format!("result already exists: {}", cli.result.display()));
    }
    let root = fs::canonicalize(&cli.root).map_err(|error| format!("dataset root: {error}"))?;
    let manifest = read_development_pilot_selection(&root, &cli.manifest, &cli.folds)?;
    let candidate = CandidateConfig::read(&cli.candidate)?;
    let (_, analyzer, rule) = candidate.config.formal_parts()?;
    let measurement_definition_sha256 = measurement_definition(analyzer, &candidate)?;
    let mut tracks = Vec::with_capacity(manifest.selection.entries.len());
    for selection in manifest.selection.entries.iter().cloned() {
        tracks.push(load_development_pilot_track(&root, selection)?);
    }
    let evaluation = evaluate_development_pilot(&tracks, analyzer, rule)?;
    let deterministic = DeterministicCore {
        schema: "kirin-hypha-attack-drum-development-pilot-v1",
        manifest_sha256: &manifest.selection.sha256,
        folds_sha256: &manifest.folds_sha256,
        candidate_semantic_sha256: &candidate.semantic_sha256,
        measurement_definition_sha256: &measurement_definition_sha256,
        evaluation: &evaluation,
    };
    let deterministic_result_sha256 = sha256_bytes(
        &serde_json::to_vec(&deterministic)
            .map_err(|error| format!("cannot serialize pilot digest input: {error}"))?,
    );
    let artifact = PilotArtifact {
        schema: "kirin-hypha-attack-drum-development-pilot-v1",
        status: "development_measurement_complete_not_winner_eligible",
        profile: "DRUM",
        publication_eligible: false,
        winner_eligible: false,
        fresh_holdout_eligible: false,
        manifest_sha256: &manifest.selection.sha256,
        folds_sha256: &manifest.folds_sha256,
        candidate: CandidateEvidence {
            id: candidate.config.candidate_id(),
            raw_config_sha256: &candidate.raw_sha256,
            semantic_config_sha256: &candidate.semantic_sha256,
            measurement_definition_sha256: &measurement_definition_sha256,
            config: &candidate.config,
        },
        evaluation: &evaluation,
        deterministic_result_sha256,
    };
    publish_create_new(
        &cli.result,
        &serde_json::to_vec_pretty(&artifact)
            .map_err(|error| format!("cannot serialize development pilot result: {error}"))?,
    )?;
    println!(
        "ATTACK DRUM pilot complete: candidate={} precision={:?} recall={:?} f1={:?} fp_per_s={:?} pooled={} every_fold={} result={}",
        candidate.config.candidate_id(),
        evaluation.pooled.micro.precision,
        evaluation.pooled.micro.recall,
        evaluation.pooled.micro.f1,
        evaluation.pooled.micro.false_positives_per_second,
        evaluation.all_pooled_micro_gates_passed,
        evaluation.all_fold_micro_gates_passed,
        cli.result.display(),
    );
    Ok(())
}

impl Cli {
    fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut values = BTreeMap::<String, OsString>::new();
        let mut arguments = arguments.into_iter();
        while let Some(flag) = arguments.next() {
            let flag = flag
                .to_str()
                .ok_or("CLI flag is not valid UTF-8")?
                .to_string();
            if !flag.starts_with("--") {
                return Err(format!("unexpected positional argument: {flag}"));
            }
            let value = arguments
                .next()
                .ok_or_else(|| format!("missing value for {flag}"))?;
            if values.insert(flag.clone(), value).is_some() {
                return Err(format!("duplicate CLI flag: {flag}"));
            }
        }
        let profile = take_string(&mut values, "--profile")?;
        if profile != "DRUM" {
            return Err("development pilot permits only --profile DRUM".to_string());
        }
        let cli = Self {
            root: take_path(&mut values, "--root")?,
            manifest: take_path(&mut values, "--manifest")?,
            folds: take_path(&mut values, "--folds")?,
            candidate: take_path(&mut values, "--candidate-config")?,
            result: take_path(&mut values, "--result")?,
        };
        if let Some(flag) = values.keys().next() {
            return Err(format!("unknown CLI flag: {flag}"));
        }
        Ok(cli)
    }
}

fn measurement_definition(
    analyzer: FormalAnalyzer,
    candidate: &CandidateArtifact,
) -> Result<String, String> {
    let layout = match analyzer {
        FormalAnalyzer::Mel32V2 => TransientCandidateAnalyzer::new(44_100, TransientOdfKind::Mel32)
            .map_err(str::to_string)?
            .layout()
            .definition_hex(),
        FormalAnalyzer::FixedSuperflux(config) => SuperFluxAnalyzer::new(44_100, config)
            .map_err(str::to_string)?
            .layout()
            .definition_hex(),
    };
    Ok(sha256_bytes(
        format!(
            "attack-drum-development-pilot-v1\0{}\0{layout}",
            candidate.semantic_sha256
        )
        .as_bytes(),
    ))
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

fn take_path(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<PathBuf, String> {
    values
        .remove(flag)
        .map(PathBuf::from)
        .ok_or_else(|| format!("missing required flag: {flag}"))
}

fn take_string(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<String, String> {
    values
        .remove(flag)
        .ok_or_else(|| format!("missing required flag: {flag}"))?
        .into_string()
        .map_err(|_| format!("{flag} value is not valid UTF-8"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cli_is_drum_only_and_requires_every_path() {
        let args = |profile: &str| {
            [
                "--profile",
                profile,
                "--root",
                "/data",
                "--manifest",
                "/manifest.csv",
                "--folds",
                "/folds.csv",
                "--candidate-config",
                "/candidate.json",
                "--result",
                "/result.json",
            ]
            .into_iter()
            .map(OsString::from)
            .collect::<Vec<_>>()
        };
        assert!(Cli::parse(args("DRUM")).is_ok());
        assert!(Cli::parse(args("2MIX")).unwrap_err().contains("DRUM"));
    }
}
