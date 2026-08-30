//! Local-only ATTACK Evaluator v2 for already-opened E-GMD diagnostics.
//!
//! The dataset stays outside the repository. The evaluator requires an
//! explicit manifest, one explicit candidate config, and a new result path.
//! B-550 rejects fresh holdout and 2MIX purposes before resolving input paths.

use std::fs;
use std::process;

use chrono::Utc;

#[path = "transient_candidate_eval/contract.rs"]
mod contract;
#[path = "transient_drum_excerpt/mod.rs"]
mod drum_excerpt;
#[allow(dead_code)] // Formal excerpt helper remains dormant behind the context-guard blocker.
#[path = "transient_drum_midi/mod.rs"]
mod drum_midi;
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

use contract::{
    verify_formal_prerequisites, verify_opened_diagnostic_manifest, CandidateConfig, Cli, Purpose,
};
use evaluation::{evaluate_tracks, load_track};
use input::read_selection;
use result::write_result;

fn main() {
    let started_at_utc = Utc::now();
    let cli = Cli::parse_env().unwrap_or_else(|error| fail(2, error));
    guard_formal_mode_before_filesystem_access(&cli).unwrap_or_else(|error| fail(2, error));
    if cli.result.exists() {
        fail(
            2,
            format!("result already exists: {}", cli.result.display()),
        );
    }
    let manifest_sha256 =
        verify_opened_diagnostic_manifest(&cli.manifest).unwrap_or_else(|error| fail(2, error));
    let root = fs::canonicalize(&cli.root)
        .unwrap_or_else(|error| fail(2, format!("dataset root: {error}")));
    let candidate =
        CandidateConfig::read(&cli.candidate_config).unwrap_or_else(|error| fail(2, error));
    let manifest = read_selection(&root, &cli.manifest, &manifest_sha256)
        .unwrap_or_else(|error| fail(2, error));
    let tracks = manifest
        .entries
        .iter()
        .cloned()
        .map(|selection| load_track(&root, selection))
        .collect::<Result<Vec<_>, _>>()
        .unwrap_or_else(|error| fail(2, error));
    let kind = candidate
        .config
        .diagnostic_kind()
        .unwrap_or_else(|error| fail(2, error));
    let (_, peak_picker) = candidate
        .config
        .diagnostic_parts()
        .unwrap_or_else(|error| fail(2, error));
    let evaluation =
        evaluate_tracks(&tracks, kind, peak_picker).unwrap_or_else(|error| fail(1, error));
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

fn guard_formal_mode_before_filesystem_access(cli: &Cli) -> Result<(), String> {
    if cli.purpose != Purpose::FormalDevelopment {
        return Ok(());
    }
    verify_formal_prerequisites(cli)?;
    Err(
        "formal authorization unexpectedly passed while semantic receipt verifiers are disabled"
            .to_string(),
    )
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::*;
    use crate::contract::FormalArguments;

    #[test]
    fn formal_prerequisite_failure_precedes_all_filesystem_paths() {
        let missing = PathBuf::from("/synthetic/must-not-exist");
        let cli = Cli {
            root: missing.join("root"),
            manifest: missing.join("manifest.csv"),
            candidate_config: missing.join("candidate.json"),
            result: missing.join("result.json"),
            purpose: Purpose::FormalDevelopment,
            dataset_id: "E-GMD".to_string(),
            dataset_version: "1.0.0".to_string(),
            dataset_archive_sha256:
                "7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053".to_string(),
            git_commit: "aa".repeat(20),
            formal: Some(FormalArguments {
                folds: missing.join("folds.csv"),
                authorization: missing.join("authorization.json"),
                authorization_sha256: "11".repeat(32),
            }),
        };
        let error = guard_formal_mode_before_filesystem_access(&cli).unwrap_err();
        assert!(
            error.contains("formal_authorization_not_pinned_in_source_commit"),
            "{error}"
        );
        assert!(!cli.root.exists());
        assert!(!cli.manifest.exists());
        assert!(!cli.candidate_config.exists());
    }
}

fn fail(code: i32, message: String) -> ! {
    eprintln!("ATTACK Evaluator v2 error: {message}");
    process::exit(code);
}
