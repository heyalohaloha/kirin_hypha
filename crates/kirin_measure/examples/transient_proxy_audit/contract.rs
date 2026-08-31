use std::collections::BTreeMap;
use std::ffi::OsString;
use std::path::PathBuf;

pub(crate) const PROFILE: &str = "DRUM";
pub(crate) const PURPOSE: &str = "midi-proxy-blind-acoustic-development";
pub(crate) const TOOL_VERSION: &str = "attack-drum-midi-proxy-audit-v1";
pub(crate) const SELECTION_SEED: &str = "ATTACK-MIDI-PROXY-AUDIT-20260830";

#[derive(Debug)]
pub(crate) struct Cli {
    pub(crate) command: Command,
}

#[derive(Debug)]
pub(crate) enum Command {
    Prepare(PrepareCli),
    Score(ScoreCli),
}

#[derive(Debug)]
pub(crate) struct PrepareCli {
    pub(crate) source: PrepareSource,
    pub(crate) source_sha256: String,
    pub(crate) plan_output: PathBuf,
    pub(crate) annotator_a_output: PathBuf,
    pub(crate) annotator_b_output: PathBuf,
}

#[derive(Debug)]
pub(crate) enum PrepareSource {
    Development {
        selection: PathBuf,
        midi_root: PathBuf,
    },
    Synthetic {
        fixture: PathBuf,
    },
}

#[derive(Debug)]
pub(crate) struct ScoreCli {
    pub(crate) plan: PathBuf,
    pub(crate) annotator_a: PathBuf,
    pub(crate) annotator_b: PathBuf,
    pub(crate) result_output: PathBuf,
}

impl Cli {
    pub(crate) fn parse_env() -> Result<Self, String> {
        Self::parse(std::env::args_os().skip(1))
    }

    fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut values = collect_flags(arguments)?;

        // Isolation checks intentionally precede every filesystem operation.
        if take_string(&mut values, "--purpose")? != PURPOSE {
            return Err(format!(
                "only --purpose {PURPOSE} is permitted; diagnostic/test/fresh holdout are isolated"
            ));
        }
        if take_string(&mut values, "--profile")? != PROFILE {
            return Err("only --profile DRUM is permitted; 2MIX is isolated".to_string());
        }

        let command = match take_string(&mut values, "--command")?.as_str() {
            "prepare" => Command::Prepare(parse_prepare(&mut values)?),
            "score" => Command::Score(parse_score(&mut values)?),
            value => return Err(format!("unsupported --command: {value}")),
        };
        if let Some(flag) = values.keys().next() {
            return Err(format!("unknown CLI flag: {flag}"));
        }
        Ok(Self { command })
    }
}

fn parse_prepare(values: &mut BTreeMap<String, OsString>) -> Result<PrepareCli, String> {
    let development = values.remove("--development-selection").map(PathBuf::from);
    let synthetic = values.remove("--synthetic-fixture").map(PathBuf::from);
    let source =
        match (development, synthetic) {
            (Some(selection), None) => PrepareSource::Development {
                selection,
                midi_root: take_path(values, "--midi-root")?,
            },
            (None, Some(fixture)) => {
                if values.contains_key("--midi-root") {
                    return Err("--midi-root is forbidden for a synthetic fixture".to_string());
                }
                PrepareSource::Synthetic { fixture }
            }
            _ => return Err(
                "prepare requires exactly one of --development-selection or --synthetic-fixture"
                    .to_string(),
            ),
        };
    let source_sha256 = take_string(values, "--source-sha256")?;
    if !is_sha256(&source_sha256) {
        return Err("--source-sha256 must be 64 lowercase hexadecimal digits".to_string());
    }
    Ok(PrepareCli {
        source,
        source_sha256,
        plan_output: take_path(values, "--plan-output")?,
        annotator_a_output: take_path(values, "--annotator-a-output")?,
        annotator_b_output: take_path(values, "--annotator-b-output")?,
    })
}

fn parse_score(values: &mut BTreeMap<String, OsString>) -> Result<ScoreCli, String> {
    Ok(ScoreCli {
        plan: take_path(values, "--plan")?,
        annotator_a: take_path(values, "--annotator-a")?,
        annotator_b: take_path(values, "--annotator-b")?,
        result_output: take_path(values, "--result-output")?,
    })
}

fn collect_flags(
    arguments: impl IntoIterator<Item = OsString>,
) -> Result<BTreeMap<String, OsString>, String> {
    let mut values = BTreeMap::new();
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
    Ok(values)
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

pub(crate) fn is_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn prepare_args() -> Vec<OsString> {
        [
            "--purpose",
            PURPOSE,
            "--profile",
            PROFILE,
            "--command",
            "prepare",
            "--development-selection",
            "/sealed/development.csv",
            "--midi-root",
            "/sealed/midi",
            "--source-sha256",
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "--plan-output",
            "/new/plan.json",
            "--annotator-a-output",
            "/new/a.csv",
            "--annotator-b-output",
            "/new/b.csv",
        ]
        .into_iter()
        .map(OsString::from)
        .collect()
    }

    #[test]
    fn rejects_fresh_and_2mix_before_paths_can_be_opened() {
        let mut fresh = prepare_args();
        replace(&mut fresh, "--purpose", "fresh-holdout");
        assert!(Cli::parse(fresh).unwrap_err().contains("isolated"));

        let mut mix = prepare_args();
        replace(&mut mix, "--profile", "2MIX");
        assert!(Cli::parse(mix).unwrap_err().contains("2MIX"));
    }

    #[test]
    fn source_kind_and_unknown_flags_fail_closed() {
        let mut both = prepare_args();
        both.extend([
            OsString::from("--synthetic-fixture"),
            OsString::from("/fixture.json"),
        ]);
        assert!(Cli::parse(both).unwrap_err().contains("exactly one"));

        let mut candidate = prepare_args();
        candidate.extend([
            OsString::from("--candidate-output"),
            OsString::from("/forbidden.json"),
        ]);
        assert!(Cli::parse(candidate).unwrap_err().contains("unknown"));
    }

    #[test]
    fn score_contract_is_explicit() {
        let args = [
            "--purpose",
            PURPOSE,
            "--profile",
            PROFILE,
            "--command",
            "score",
            "--plan",
            "/plan.json",
            "--annotator-a",
            "/a.csv",
            "--annotator-b",
            "/b.csv",
            "--result-output",
            "/new/result.json",
        ]
        .into_iter()
        .map(OsString::from)
        .collect::<Vec<_>>();
        assert!(matches!(
            Cli::parse(args).unwrap().command,
            Command::Score(_)
        ));
    }

    fn replace(arguments: &mut [OsString], flag: &str, value: &str) {
        let index = arguments
            .iter()
            .position(|argument| argument == flag)
            .unwrap();
        arguments[index + 1] = OsString::from(value);
    }
}
