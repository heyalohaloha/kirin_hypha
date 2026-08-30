use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};

const EXPECTED_KEY_SHA256: &str =
    "582d4003d02603063a75a349030e2d78f5cc900ee76681b93c9d74d0f6716598";
const EXPECTED_RESPONSES_SHA256: &str =
    "392df1d01a63a1832f7836c4401a866933153f4a5b43fb6672ccef27ce9458f9";

#[derive(Debug)]
pub(crate) struct Cli {
    pub(crate) clips: PathBuf,
    pub(crate) key: PathBuf,
    pub(crate) responses: PathBuf,
    pub(crate) candidate: PathBuf,
    pub(crate) result: PathBuf,
}

#[derive(Debug)]
pub(crate) struct Response {
    pub(crate) audible_kick: String,
    pub(crate) nearest_kick_ms: String,
}

pub(crate) fn read_pinned_inputs(
    key: &Path,
    responses: &Path,
) -> Result<(Vec<u8>, Vec<u8>), String> {
    Ok((
        read_pinned(key, EXPECTED_KEY_SHA256, "listening key")?,
        read_pinned(responses, EXPECTED_RESPONSES_SHA256, "listening responses")?,
    ))
}

pub(crate) fn read_responses(bytes: &[u8]) -> Result<BTreeMap<String, Response>, String> {
    let text = std::str::from_utf8(bytes).map_err(|error| format!("responses UTF-8: {error}"))?;
    let mut lines = text.trim_start_matches('\u{feff}').lines();
    let header = lines.next().ok_or("empty listening responses")?;
    let expected = "review_id\tclip_id\taudible_kick\tconfidence\tnearest_kick_ms\tnote\tinterface\tmonitor_or_headphone\tsample_rate\tplayback_level\troom_or_location";
    if header.trim_end_matches('\r') != expected {
        return Err("unexpected listening response header".to_string());
    }
    let mut result = BTreeMap::new();
    for line in lines {
        let fields = line.trim_end_matches('\r').split('\t').collect::<Vec<_>>();
        if fields.len() != 11 || !matches!(fields[2], "yes" | "no" | "uncertain") {
            return Err("invalid listening response row".to_string());
        }
        if result
            .insert(
                fields[1].to_string(),
                Response {
                    audible_kick: fields[2].to_string(),
                    nearest_kick_ms: fields[4].to_string(),
                },
            )
            .is_some()
        {
            return Err("duplicate listening response clip".to_string());
        }
    }
    if result.len() != 45 {
        return Err("listening response count is not 45".to_string());
    }
    Ok(result)
}

pub(crate) fn sha256(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

fn read_pinned(path: &Path, expected: &str, label: &str) -> Result<Vec<u8>, String> {
    let bytes = fs::read(path).map_err(|error| format!("cannot read {label}: {error}"))?;
    if sha256(&bytes) != expected {
        return Err(format!("{label} SHA-256 mismatch"));
    }
    Ok(bytes)
}

impl Cli {
    pub(crate) fn parse_env() -> Result<Self, String> {
        Self::parse(std::env::args_os().skip(1))
    }

    fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut values = BTreeMap::<String, OsString>::new();
        let mut arguments = arguments.into_iter();
        while let Some(flag) = arguments.next() {
            let flag = flag.to_str().ok_or("CLI flag is not UTF-8")?.to_string();
            let value = arguments
                .next()
                .ok_or_else(|| format!("missing value for {flag}"))?;
            if values.insert(flag.clone(), value).is_some() {
                return Err(format!("duplicate CLI flag: {flag}"));
            }
        }
        if take_string(&mut values, "--profile")? != "DRUM" {
            return Err("subband diagnosis permits only DRUM".to_string());
        }
        let cli = Self {
            clips: take_path(&mut values, "--clips")?,
            key: take_path(&mut values, "--listening-key")?,
            responses: take_path(&mut values, "--responses")?,
            candidate: take_path(&mut values, "--candidate-config")?,
            result: take_path(&mut values, "--result")?,
        };
        if let Some(flag) = values.keys().next() {
            return Err(format!("unknown CLI flag: {flag}"));
        }
        Ok(cli)
    }
}

fn take_path(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<PathBuf, String> {
    values
        .remove(flag)
        .map(PathBuf::from)
        .ok_or_else(|| format!("missing {flag}"))
}

fn take_string(values: &mut BTreeMap<String, OsString>, flag: &str) -> Result<String, String> {
    values
        .remove(flag)
        .ok_or_else(|| format!("missing {flag}"))?
        .into_string()
        .map_err(|_| format!("{flag} is not UTF-8"))
}
