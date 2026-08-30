use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

use serde::Serialize;
use sha2::{Digest, Sha256};

#[allow(dead_code)] // Formal realizers compile now but cannot run before prerequisite readiness.
#[path = "candidate.rs"]
mod candidate;
#[allow(dead_code)] // Envelope parsing is tested but source pinning blocks it in production.
#[path = "formal_contract.rs"]
mod formal_contract;

#[cfg(test)]
pub(crate) use candidate::FormalAnalyzerConfig;
pub(crate) use candidate::{
    CandidateArtifact, CandidateConfig, DiagnosticAnalyzerConfig, FormalAnalyzer, PeakRule,
};
#[cfg(test)]
pub(crate) use formal_contract::synthetic_formal_authorization;
pub(crate) use formal_contract::{
    verify_formal_prerequisites, FormalArguments, FormalAuthorization,
};

const DATASET_ID: &str = "E-GMD";
const DATASET_VERSION: &str = "1.0.0";
const DATASET_ARCHIVE_SHA256: &str =
    "7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053";
const OPENED_MANIFEST_SHA256: [&str; 2] = [
    "adf8753f31ab655089a31434e66c6dbf8084a6589b5d0d32ca8358f92b5b66a3",
    "151e876109722459b7d836525e5cf6e0d2e7fe1c41bc87b23c4ca6faadd6c8c3",
];

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum Purpose {
    OpenedDiagnostic,
    FormalDevelopment,
}

#[derive(Debug)]
pub(crate) struct Cli {
    pub(crate) root: PathBuf,
    pub(crate) manifest: PathBuf,
    pub(crate) candidate_config: PathBuf,
    pub(crate) result: PathBuf,
    pub(crate) purpose: Purpose,
    pub(crate) dataset_id: String,
    pub(crate) dataset_version: String,
    pub(crate) dataset_archive_sha256: String,
    pub(crate) git_commit: String,
    #[allow(dead_code)] // Deliberately unread until a source-pinned formal trust root exists.
    pub(crate) formal: Option<FormalArguments>,
}

impl Cli {
    pub(crate) fn parse_env() -> Result<Self, String> {
        Self::parse(std::env::args_os().skip(1))
    }

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

        let purpose = match take_string(&mut values, "--purpose")?.as_str() {
            "opened-diagnostic" => Purpose::OpenedDiagnostic,
            "formal-development" => Purpose::FormalDevelopment,
            _ => {
                return Err(
                    "only --purpose opened-diagnostic|formal-development is permitted".to_string(),
                )
            }
        };
        if take_string(&mut values, "--profile")? != "DRUM" {
            return Err(
                "ATTACK evaluator permits only --profile DRUM; 2MIX is isolated".to_string(),
            );
        }
        let git_commit = take_string(&mut values, "--git-commit")?;
        if !is_git_commit(&git_commit) {
            return Err("--git-commit must be 40 lowercase hex digits".to_string());
        }
        let dataset_id = take_nonempty(&mut values, "--dataset-id")?;
        let dataset_version = take_nonempty(&mut values, "--dataset-version")?;
        let dataset_archive_sha256 = take_nonempty(&mut values, "--dataset-archive-sha256")?;
        if dataset_id != DATASET_ID
            || dataset_version != DATASET_VERSION
            || dataset_archive_sha256 != DATASET_ARCHIVE_SHA256
        {
            return Err("opened diagnostic requires pinned E-GMD v1.0.0 identity".to_string());
        }
        let formal = match purpose {
            Purpose::OpenedDiagnostic => None,
            Purpose::FormalDevelopment => Some(FormalArguments {
                folds: take_path(&mut values, "--folds")?,
                authorization: take_path(&mut values, "--formal-authorization")?,
                authorization_sha256: take_nonempty(&mut values, "--formal-authorization-sha256")?,
            }),
        };
        let cli = Self {
            root: take_path(&mut values, "--root")?,
            manifest: take_path(&mut values, "--manifest")?,
            candidate_config: take_path(&mut values, "--candidate-config")?,
            result: take_path(&mut values, "--result")?,
            purpose,
            dataset_id,
            dataset_version,
            dataset_archive_sha256,
            git_commit,
            formal,
        };
        if let Some(flag) = values.keys().next() {
            return Err(format!("unknown CLI flag: {flag}"));
        }
        Ok(cli)
    }
}

pub(crate) fn verify_opened_diagnostic_manifest(path: &Path) -> Result<String, String> {
    let bytes = fs::read(path).map_err(|error| {
        format!(
            "cannot read opened diagnostic manifest {}: {error}",
            path.display()
        )
    })?;
    let digest = sha256_bytes(&bytes);
    if !OPENED_MANIFEST_SHA256.contains(&digest.as_str()) {
        return Err(format!(
            "manifest is not in the exact opened-diagnostic allowlist: {digest}"
        ));
    }
    Ok(digest)
}

pub(crate) fn sha256_bytes(bytes: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    hex::encode(hasher.finalize())
}

fn take_path(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<PathBuf, String> {
    values
        .remove(flag)
        .map(PathBuf::from)
        .ok_or_else(|| format!("missing required flag: {flag}"))
}

fn take_string(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<String, String> {
    let value = values
        .remove(flag)
        .ok_or_else(|| format!("missing required flag: {flag}"))?;
    value
        .into_string()
        .map_err(|_| format!("{flag} value is not valid UTF-8"))
}

fn take_nonempty(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<String, String> {
    let value = take_string(values, flag)?;
    if value.is_empty() {
        Err(format!("{flag} must not be empty"))
    } else {
        Ok(value)
    }
}

fn is_git_commit(value: &str) -> bool {
    value.len() == 40
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    fn base() -> Vec<OsString> {
        [
            "--purpose",
            "opened-diagnostic",
            "--profile",
            "DRUM",
            "--git-commit",
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "--root",
            "/data",
            "--manifest",
            "/manifests/fixed.csv",
            "--candidate-config",
            "/configs/mel32.json",
            "--result",
            "/tmp/result.json",
            "--dataset-id",
            "E-GMD",
            "--dataset-version",
            "1.0.0",
            "--dataset-archive-sha256",
            "7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053",
        ]
        .into_iter()
        .map(OsString::from)
        .collect()
    }

    #[test]
    fn opened_diagnostic_contract_is_explicit() {
        let cli = Cli::parse(base()).unwrap();
        assert_eq!(cli.purpose, Purpose::OpenedDiagnostic);
        assert!(cli.formal.is_none());
    }

    #[test]
    fn formal_cli_has_separate_required_arguments_but_no_2mix_route() {
        let mut formal = base();
        replace(&mut formal, "--purpose", "formal-development");
        formal.extend([
            OsString::from("--folds"),
            OsString::from("/contracts/folds.csv"),
            OsString::from("--formal-authorization"),
            OsString::from("/contracts/authorization.json"),
            OsString::from("--formal-authorization-sha256"),
            OsString::from("11".repeat(32)),
        ]);
        let cli = Cli::parse(formal).unwrap();
        assert_eq!(cli.purpose, Purpose::FormalDevelopment);
        assert_eq!(
            cli.formal.unwrap().folds,
            PathBuf::from("/contracts/folds.csv")
        );

        let mut missing = base();
        replace(&mut missing, "--purpose", "formal-development");
        assert!(Cli::parse(missing).unwrap_err().contains("--folds"));
    }

    #[test]
    fn fresh_holdout_and_2mix_fail_before_input_resolution() {
        let mut holdout = base();
        replace(&mut holdout, "--purpose", "fresh-holdout");
        assert!(Cli::parse(holdout)
            .unwrap_err()
            .contains("opened-diagnostic"));
        let mut mix = base();
        replace(&mut mix, "--profile", "2MIX");
        assert!(Cli::parse(mix).unwrap_err().contains("DRUM"));

        let mut dataset = base();
        replace(&mut dataset, "--dataset-version", "fresh");
        assert!(Cli::parse(dataset).unwrap_err().contains("pinned E-GMD"));

        let mut archive = base();
        replace(
            &mut archive,
            "--dataset-archive-sha256",
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        );
        assert!(Cli::parse(archive).unwrap_err().contains("pinned E-GMD"));
    }

    #[test]
    fn only_exact_opened_manifests_are_allowlisted() {
        let fixed = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("examples/transient_candidate_eval/fixtures/transient_egmd_selection_v1.csv");
        assert_eq!(
            verify_opened_diagnostic_manifest(&fixed).unwrap(),
            OPENED_MANIFEST_SHA256[0]
        );
        let directory = tempdir().unwrap();
        let fabricated = directory.path().join("fresh.csv");
        fs::write(&fabricated, b"fresh").unwrap();
        assert!(verify_opened_diagnostic_manifest(&fabricated)
            .unwrap_err()
            .contains("allowlist"));
    }

    #[test]
    fn duplicate_and_unknown_flags_fail_closed() {
        let mut duplicate = base();
        duplicate.extend([OsString::from("--profile"), OsString::from("DRUM")]);
        assert!(Cli::parse(duplicate).unwrap_err().contains("duplicate"));
        let mut unknown = base();
        unknown.extend([OsString::from("--tolerance-ms"), OsString::from("30")]);
        assert!(Cli::parse(unknown).unwrap_err().contains("unknown"));
    }

    #[test]
    fn candidate_config_rejects_unknown_fields_and_public_eligibility() {
        let directory = tempdir().unwrap();
        let path = directory.path().join("candidate.json");
        let valid = r#"{
          "schema":"kirin-hypha-attack-candidate-config-v1",
          "candidate_id":"b546-mel32-opened-diagnostic",
          "profile":"DRUM",
          "eligibility":"diagnostic_only",
          "analyzer":{"odf_kind":"mel32"},
          "peak_picker":{"mode":"legacy_absolute","threshold":1.44459105,"radius_hops":2,"refractory_ms":20.0}
        }"#;
        fs::write(&path, valid).unwrap();
        assert!(CandidateConfig::read(&path).is_ok());
        fs::write(
            &path,
            valid.replace("diagnostic_only", "publication_candidate"),
        )
        .unwrap();
        assert!(CandidateConfig::read(&path).is_err());
        fs::write(
            &path,
            valid.replace("\"analyzer\":{", "\"extra\":1,\"analyzer\":{"),
        )
        .unwrap();
        assert!(CandidateConfig::read(&path).is_err());
    }

    #[test]
    fn semantic_hash_ignores_json_whitespace() {
        let directory = tempdir().unwrap();
        let first = directory.path().join("first.json");
        let second = directory.path().join("second.json");
        let compact = r#"{"schema":"kirin-hypha-attack-candidate-config-v1","candidate_id":"x","profile":"DRUM","eligibility":"diagnostic_only","analyzer":{"odf_kind":"mel32"},"peak_picker":{"mode":"legacy_absolute","threshold":1.0,"radius_hops":2,"refractory_ms":20.0}}"#;
        fs::write(&first, compact).unwrap();
        fs::write(&second, compact.replace(",", ",\n")).unwrap();
        let first = CandidateConfig::read(&first).unwrap();
        let second = CandidateConfig::read(&second).unwrap();
        assert_ne!(first.raw_sha256, second.raw_sha256);
        assert_eq!(first.semantic_sha256, second.semantic_sha256);
        assert_eq!(serde_json::to_string(&first.config).unwrap(), compact);
    }

    #[test]
    fn formal_candidate_schemas_accept_only_integer_preregistered_grids() {
        let directory = tempdir().unwrap();
        let path = directory.path().join("formal.json");
        let mel32 = r#"{
          "schema":"kirin-hypha-attack-drum-formal-candidate-config-v1",
          "candidate_id":"mel32-v2-grid-001",
          "profile":"DRUM",
          "eligibility":"formal_development_candidate",
          "analyzer":{"front_end":"mel32_v2"},
          "peak_picker":{"mode":"local_mean","delta_micro_units":250000,"absolute_floor_micro_units":0,"pre_max_hops":3,"post_max_hops":0,"pre_avg_hops":12,"post_avg_hops":0,"refractory_micros":30000}
        }"#;
        fs::write(&path, mel32).unwrap();
        let artifact = CandidateConfig::read(&path).unwrap();
        assert!(matches!(
            artifact.config.formal_parts().unwrap().0,
            FormalAnalyzerConfig::Mel32V2
        ));
        fs::write(&path, mel32.replace("250000", "249999")).unwrap();
        assert!(CandidateConfig::read(&path)
            .unwrap_err()
            .contains("integer grid"));
        fs::write(&path, mel32.replace("250000", "0.25")).unwrap();
        assert!(CandidateConfig::read(&path).is_err());
        fs::write(
            &path,
            mel32.replace("formal_development_candidate", "diagnostic_only"),
        )
        .unwrap();
        assert!(CandidateConfig::read(&path).is_err());
    }

    #[test]
    fn formal_superflux_window_fixes_lag_and_mono_lr_topology() {
        let directory = tempdir().unwrap();
        let path = directory.path().join("superflux.json");
        let config = |window| {
            format!(
                r#"{{
          "schema":"kirin-hypha-attack-drum-formal-candidate-config-v1",
          "candidate_id":"superflux-{window}",
          "profile":"DRUM",
          "eligibility":"formal_development_candidate",
          "analyzer":{{"front_end":"fixed_superflux","reference_window_samples":{window},"bands_per_octave":24,"maximum_filter_radius":1,"reference_dbfs":-70}},
          "peak_picker":{{"mode":"local_mean","delta_micro_units":6250,"absolute_floor_micro_units":0,"pre_max_hops":6,"post_max_hops":0,"pre_avg_hops":19,"post_avg_hops":0,"refractory_micros":30000}}
        }}"#
            )
        };
        for (window, expected_lag) in [(1_024, 1), (2_048, 2)] {
            fs::write(&path, config(window)).unwrap();
            let artifact = CandidateConfig::read(&path).unwrap();
            let (_, analyzer, _) = artifact.config.formal_parts().unwrap();
            let FormalAnalyzer::FixedSuperflux(config) = analyzer else {
                panic!("expected fixed SuperFlux");
            };
            assert_eq!(config.channel_count, 1);
            assert_eq!(config.channel_mode, kirin_measure::SuperFluxChannelMode::Lr);
            let layout = kirin_measure::SuperFluxLayout::for_rate(44_100, config).unwrap();
            assert_eq!(layout.spectral_lag_frames, expected_lag);
        }
    }

    fn replace(arguments: &mut [OsString], flag: &str, replacement: &str) {
        let index = arguments.iter().position(|value| value == flag).unwrap();
        arguments[index + 1] = OsString::from(replacement);
    }
}
