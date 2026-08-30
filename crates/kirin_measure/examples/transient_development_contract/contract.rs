use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};

pub(crate) const SELECTION_SEED: &str = "ATTACK-V2-20260830";
pub(crate) const RENDER_KEY_VERSION: &str = "attack-drum-development-render-choice-v1";
pub(crate) const SELECTION_VERSION: &str = "attack-drum-development-balanced-excerpt-v2";
pub(crate) const LEGACY_BASELINE_PERFORMANCE_IDS: usize = 60;
pub(crate) const LEGACY_BASELINE_ID_LIST_SHA256: &str =
    "3047af1bb19b61cd4a38eb90f62be2612d67aa3974bc16cd148447c6521693ee";
pub(crate) const TARGET_SEARCH_START_PERFORMANCE_IDS: usize = 175;
pub(crate) const TARGET_SEARCH_STEP: usize = 5;
pub(crate) const TARGET_PERFORMANCE_IDS: usize = 290;
pub(crate) const LOWER_BOUND_IMPOSSIBLE_PERFORMANCE_IDS: usize = 285;
pub(crate) const MAX_TARGET_PERFORMANCE_IDS: usize = 400;
pub(crate) const METADATA_SHA256: &str =
    "80677e8fb00e973f33cb91ddaaf7f0cffe55359f9a76c1833ce56c84d1d92c64";
pub(crate) const MIDI_ARCHIVE_SHA256: &str =
    "5e70a6f4d760385a5e5ec986a2f02179d96f61181a920e592876b577a75844d3";

#[derive(Debug)]
pub(crate) struct Cli {
    pub(crate) metadata: PathBuf,
    pub(crate) midi_archive: PathBuf,
    pub(crate) midi_root: PathBuf,
    pub(crate) output_dir: PathBuf,
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

        // These checks deliberately precede all filesystem access.
        if take_string(&mut values, "--purpose")? != "development-selection" {
            return Err(
                "only --purpose development-selection is permitted; fresh/test are sealed".into(),
            );
        }
        if take_string(&mut values, "--profile")? != "DRUM" {
            return Err("only --profile DRUM is permitted; 2MIX is isolated".into());
        }
        let cli = Self {
            metadata: take_path(&mut values, "--metadata")?,
            midi_archive: take_path(&mut values, "--midi-archive")?,
            midi_root: take_path(&mut values, "--midi-root")?,
            output_dir: take_path(&mut values, "--output-dir")?,
        };
        if let Some(flag) = values.keys().next() {
            return Err(format!("unknown CLI flag: {flag}"));
        }
        Ok(cli)
    }
}

#[derive(Clone, Debug)]
pub(crate) struct InputIdentities {
    pub(crate) metadata_sha256: String,
    pub(crate) midi_archive_sha256: String,
}

pub(crate) fn verify_input_identity(cli: &Cli) -> Result<InputIdentities, String> {
    let metadata_sha256 = sha256_file(&cli.metadata)?;
    if metadata_sha256 != METADATA_SHA256 {
        return Err(format!(
            "official metadata SHA-256 mismatch: expected {METADATA_SHA256}, got {metadata_sha256}"
        ));
    }
    let midi_archive_sha256 = sha256_file(&cli.midi_archive)?;
    if midi_archive_sha256 != MIDI_ARCHIVE_SHA256 {
        return Err(format!(
            "official MIDI archive SHA-256 mismatch: expected {MIDI_ARCHIVE_SHA256}, got {midi_archive_sha256}"
        ));
    }
    if !cli.midi_root.is_dir() {
        return Err(format!(
            "MIDI root is not a directory: {}",
            cli.midi_root.display()
        ));
    }
    if cli.output_dir.exists() {
        return Err(format!(
            "output directory must not already exist: {}",
            cli.output_dir.display()
        ));
    }
    if !cli
        .output_dir
        .parent()
        .is_some_and(|parent| parent.is_dir())
    {
        return Err("output parent directory does not exist".to_string());
    }
    Ok(InputIdentities {
        metadata_sha256,
        midi_archive_sha256,
    })
}

pub(crate) fn sha256_bytes(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

fn sha256_file(path: &Path) -> Result<String, String> {
    let bytes =
        fs::read(path).map_err(|error| format!("cannot read {}: {error}", path.display()))?;
    Ok(sha256_bytes(&bytes))
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

    fn base() -> Vec<OsString> {
        [
            "--purpose",
            "development-selection",
            "--profile",
            "DRUM",
            "--metadata",
            "/sealed/metadata.csv",
            "--midi-archive",
            "/sealed/midi.zip",
            "--midi-root",
            "/sealed/midi",
            "--output-dir",
            "/new/output",
        ]
        .into_iter()
        .map(OsString::from)
        .collect()
    }

    #[test]
    fn rejects_fresh_test_and_2mix_without_touching_paths() {
        for forbidden in ["fresh-holdout", "test"] {
            let mut args = base();
            replace(&mut args, "--purpose", forbidden);
            assert!(Cli::parse(args).unwrap_err().contains("sealed"));
        }
        let mut args = base();
        replace(&mut args, "--profile", "2MIX");
        assert!(Cli::parse(args).unwrap_err().contains("isolated"));
    }

    #[test]
    fn development_cli_is_explicit_and_closed() {
        assert!(Cli::parse(base()).is_ok());
        let mut duplicate = base();
        duplicate.extend([OsString::from("--profile"), OsString::from("DRUM")]);
        assert!(Cli::parse(duplicate).unwrap_err().contains("duplicate"));
        let mut unknown = base();
        unknown.extend([OsString::from("--audio-root"), OsString::from("/audio")]);
        assert!(Cli::parse(unknown).unwrap_err().contains("unknown"));
    }

    fn replace(arguments: &mut [OsString], flag: &str, replacement: &str) {
        let index = arguments.iter().position(|value| value == flag).unwrap();
        arguments[index + 1] = OsString::from(replacement);
    }
}
