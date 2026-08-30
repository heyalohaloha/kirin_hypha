use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

use kirin_measure::TransientOdfKind;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

pub(crate) const CANDIDATE_SCHEMA: &str = "kirin-hypha-attack-candidate-config-v1";
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
}

#[derive(Clone, Copy, Debug, PartialEq, Deserialize, Serialize)]
#[serde(tag = "mode", rename_all = "snake_case", deny_unknown_fields)]
pub(crate) enum PeakRule {
    LegacyAbsolute {
        threshold: f32,
        radius_hops: usize,
        refractory_ms: f64,
    },
    LocalMean {
        delta: f32,
        absolute_floor: f32,
        pre_max_hops: usize,
        post_max_hops: usize,
        pre_avg_hops: usize,
        post_avg_hops: usize,
        refractory_ms: f64,
    },
}

impl PeakRule {
    pub(crate) fn refractory_samples(self, sample_rate: u32) -> i64 {
        match self {
            Self::LegacyAbsolute { refractory_ms, .. } | Self::LocalMean { refractory_ms, .. } => {
                (refractory_ms * f64::from(sample_rate) / 1_000.0).round() as i64
            }
        }
    }

    fn validate(self) -> Result<(), String> {
        match self {
            Self::LegacyAbsolute {
                threshold,
                radius_hops,
                refractory_ms,
            } => {
                if !threshold.is_finite()
                    || threshold < 0.0
                    || radius_hops == 0
                    || !valid_positive(refractory_ms)
                {
                    return Err("invalid legacy peak rule".to_string());
                }
            }
            Self::LocalMean {
                delta,
                absolute_floor,
                pre_max_hops,
                post_max_hops: _,
                pre_avg_hops,
                post_avg_hops: _,
                refractory_ms,
            } => {
                if !delta.is_finite()
                    || !absolute_floor.is_finite()
                    || delta < 0.0
                    || absolute_floor < 0.0
                    || pre_max_hops == 0
                    || pre_avg_hops == 0
                    || (refractory_ms - 30.0).abs() > f64::EPSILON
                {
                    return Err("invalid v2 local-mean peak rule".to_string());
                }
            }
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct AnalyzerConfig {
    pub(crate) odf_kind: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct CandidateConfig {
    pub(crate) schema: String,
    pub(crate) candidate_id: String,
    pub(crate) profile: String,
    pub(crate) eligibility: String,
    pub(crate) analyzer: AnalyzerConfig,
    pub(crate) peak_picker: PeakRule,
}

impl CandidateConfig {
    pub(crate) fn read(path: &Path) -> Result<CandidateArtifact, String> {
        let raw = fs::read(path)
            .map_err(|error| format!("cannot read candidate config {}: {error}", path.display()))?;
        let config: Self = serde_json::from_slice(&raw)
            .map_err(|error| format!("invalid candidate config JSON: {error}"))?;
        config.validate()?;
        let semantic = serde_json::to_vec(&config)
            .map_err(|error| format!("cannot canonicalize candidate config: {error}"))?;
        Ok(CandidateArtifact {
            config,
            raw_sha256: sha256_bytes(&raw),
            semantic_sha256: sha256_bytes(&semantic),
        })
    }

    pub(crate) fn kind(&self) -> Result<TransientOdfKind, String> {
        match self.analyzer.odf_kind.as_str() {
            "mel32" => Ok(TransientOdfKind::Mel32),
            "mel40" => Ok(TransientOdfKind::Mel40),
            "complex" => Ok(TransientOdfKind::Complex),
            "hybrid" => Ok(TransientOdfKind::Hybrid),
            _ => Err("unsupported candidate ODF kind".to_string()),
        }
    }

    fn validate(&self) -> Result<(), String> {
        if self.schema != CANDIDATE_SCHEMA {
            return Err("unexpected candidate config schema".to_string());
        }
        if self.candidate_id.is_empty() || self.profile != "DRUM" {
            return Err("candidate ID/profile is invalid".to_string());
        }
        if self.eligibility != "diagnostic_only" {
            return Err("B-548 accepts diagnostic_only candidates".to_string());
        }
        self.kind()?;
        self.peak_picker.validate()
    }
}

#[derive(Debug)]
pub(crate) struct CandidateArtifact {
    pub(crate) config: CandidateConfig,
    pub(crate) raw_sha256: String,
    pub(crate) semantic_sha256: String,
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

        let purpose = take_string(&mut values, "--purpose")?;
        if purpose != "opened-diagnostic" {
            return Err("B-548 permits only --purpose opened-diagnostic".to_string());
        }
        if take_string(&mut values, "--profile")? != "DRUM" {
            return Err("B-548 permits only --profile DRUM".to_string());
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
        let cli = Self {
            root: take_path(&mut values, "--root")?,
            manifest: take_path(&mut values, "--manifest")?,
            candidate_config: take_path(&mut values, "--candidate-config")?,
            result: take_path(&mut values, "--result")?,
            purpose: Purpose::OpenedDiagnostic,
            dataset_id,
            dataset_version,
            dataset_archive_sha256,
            git_commit,
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

fn valid_positive(value: f64) -> bool {
    value.is_finite() && value > 0.0
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
    }

    fn replace(arguments: &mut [OsString], flag: &str, replacement: &str) {
        let index = arguments.iter().position(|value| value == flag).unwrap();
        arguments[index + 1] = OsString::from(replacement);
    }
}
