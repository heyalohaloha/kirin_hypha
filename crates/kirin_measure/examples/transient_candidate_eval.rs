//! Local-only ATTACK Evaluator v2 for already-opened E-GMD diagnostics.
//!
//! The dataset stays outside the repository. The evaluator requires an
//! explicit manifest, one explicit candidate config, and a new result path.
//! B-548 rejects fresh holdout and 2MIX purposes before resolving input paths.

use std::fs;
use std::process;

use chrono::Utc;

#[path = "transient_candidate_eval/contract.rs"]
mod contract;
#[path = "transient_candidate_eval/evaluation.rs"]
mod evaluation;
#[path = "transient_candidate_eval/input.rs"]
mod input;
#[path = "transient_candidate_eval/matching.rs"]
mod matching;
#[path = "transient_candidate_eval/metrics.rs"]
mod metrics;
#[path = "transient_candidate_eval/result.rs"]
mod result;

use contract::{CandidateConfig, Cli};
use evaluation::{evaluate_tracks, load_track};
use input::read_selection;
use result::write_result;

fn main() {
    let started_at_utc = Utc::now();
    let cli = Cli::parse_env().unwrap_or_else(|error| fail(2, error));
    if cli.result.exists() {
        fail(
            2,
            format!("result already exists: {}", cli.result.display()),
        );
    }
    let root = fs::canonicalize(&cli.root)
        .unwrap_or_else(|error| fail(2, format!("dataset root: {error}")));
    let candidate =
        CandidateConfig::read(&cli.candidate_config).unwrap_or_else(|error| fail(2, error));
    let manifest = read_selection(&root, &cli.manifest).unwrap_or_else(|error| fail(2, error));
    let tracks = manifest
        .entries
        .iter()
        .cloned()
        .map(|selection| load_track(&root, selection))
        .collect::<Result<Vec<_>, _>>()
        .unwrap_or_else(|error| fail(2, error));
    let kind = candidate
        .config
        .kind()
        .unwrap_or_else(|error| fail(2, error));
    let evaluation = evaluate_tracks(&tracks, kind, candidate.config.peak_picker)
        .unwrap_or_else(|error| fail(1, error));
    let (result_digest, definition_hash) = write_result(
        &cli,
        &manifest,
        &candidate,
        &tracks,
        &evaluation,
        started_at_utc,
    )
    .unwrap_or_else(|error| fail(1, error));
    println!(
        "ATTACK Evaluator v2 complete: rows={} labels={} predictions={} tp={} fp={} fn={} diagnostic_gates={} result={} digest={} definition={}",
        tracks.len(),
        evaluation.counts.label_count,
        evaluation.counts.prediction_count,
        evaluation.counts.tp,
        evaluation.counts.fp,
        evaluation.counts.fn_count,
        if evaluation.all_gates_passed { "pass" } else { "fail" },
        cli.result.display(),
        result_digest,
        definition_hash,
    );
}

fn fail(code: i32, message: String) -> ! {
    eprintln!("ATTACK Evaluator v2 error: {message}");
    process::exit(code);
}
