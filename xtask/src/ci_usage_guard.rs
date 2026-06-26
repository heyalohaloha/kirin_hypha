use anyhow::{bail, Result};

const CI_WORKFLOW: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../.github/workflows/ci.yml"
));
const FULL_CI_JOB_IF: &str = "if: ${{ github.event_name == 'workflow_dispatch' || github.event_name == 'pull_request' || contains(github.event.head_commit.message, '[ci full]') }}";

pub fn run(args: Vec<String>) -> Result<()> {
    match args.as_slice() {
        [] => {}
        [arg] if arg == "-h" || arg == "--help" => {
            print_usage();
            return Ok(());
        }
        [arg] => bail!("unknown argument: {arg}"),
        _ => bail!("unknown arguments: {}", args.join(" ")),
    }

    verify_ci_usage_gate(CI_WORKFLOW)?;
    eprintln!("[ci-usage-guard] OK: full CI is gated behind workflow_dispatch, PR, or [ci full].");
    Ok(())
}

fn print_usage() {
    eprintln!(
        "Usage: cargo run -p xtask -- ci-usage-guard\n\n\
         Static guard for GitHub Actions usage. It verifies that runner jobs in\n\
         .github/workflows/ci.yml do not run on every push unless the commit\n\
         message explicitly contains [ci full]."
    );
}

fn verify_ci_usage_gate(source: &str) -> Result<()> {
    let source = normalize_newlines(source);
    require(
        &source,
        "on:\n  workflow_dispatch:\n  push:\n    branches: [main]\n  pull_request:\n    branches: [main]\n",
        "workflow triggers must stay manual/PR/main-push scoped",
    )?;

    let jobs = job_blocks(&source)?;
    if jobs.is_empty() {
        bail!("CI workflow has no jobs");
    }

    for (name, body) in jobs {
        if !body.contains("runs-on:") {
            continue;
        }
        require_exact_job_usage_gate(&name, body)?;
    }

    Ok(())
}

fn require_exact_job_usage_gate(name: &str, body: &str) -> Result<()> {
    let expected = format!("    {FULL_CI_JOB_IF}");
    let if_lines: Vec<_> = body
        .lines()
        .map(str::trim_end)
        .filter(|line| line.starts_with("    if:"))
        .collect();

    match if_lines.as_slice() {
        [line] if *line == expected => Ok(()),
        [] => bail!("job `{name}` must use exact top-level usage gate `{FULL_CI_JOB_IF}`"),
        [line] => bail!(
            "job `{name}` must use exact top-level usage gate `{FULL_CI_JOB_IF}`; found `{}`",
            line.trim()
        ),
        _ => bail!("job `{name}` must have exactly one top-level usage gate"),
    }
}

fn job_blocks(source: &str) -> Result<Vec<(String, &str)>> {
    let jobs_pos = source.find("\njobs:\n").or_else(|| source.find("jobs:\n"));
    let Some(jobs_pos) = jobs_pos else {
        bail!("CI workflow has no jobs section");
    };
    let jobs = &source[jobs_pos..];
    let mut blocks = Vec::new();
    let mut current: Option<(String, usize)> = None;
    let mut byte = 0usize;

    for line in jobs.split_inclusive('\n') {
        let line_without_newline = line.trim_end_matches('\n');
        if line_without_newline.starts_with("  ")
            && !line_without_newline.starts_with("    ")
            && line_without_newline.ends_with(':')
        {
            if let Some((name, start)) = current.take() {
                blocks.push((name, &jobs[start..byte]));
            }
            let name = line_without_newline
                .trim()
                .trim_end_matches(':')
                .to_string();
            current = Some((name, byte + line.len()));
        }
        byte += line.len();
    }

    if let Some((name, start)) = current {
        blocks.push((name, &jobs[start..]));
    }

    Ok(blocks)
}

fn normalize_newlines(source: &str) -> String {
    source.replace("\r\n", "\n").replace('\r', "\n")
}

fn require(source: &str, needle: &str, message: &str) -> Result<()> {
    if source.contains(needle) {
        Ok(())
    } else {
        bail!("{message}: missing `{needle}`")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ci_workflow_gates_all_runner_jobs() {
        verify_ci_usage_gate(CI_WORKFLOW).unwrap();
    }

    #[test]
    fn guard_accepts_crlf_checkout() {
        let crlf = CI_WORKFLOW.replace('\n', "\r\n");
        verify_ci_usage_gate(&crlf).unwrap();
    }

    #[test]
    fn guard_rejects_missing_manual_trigger() {
        let bad = CI_WORKFLOW.replace("  workflow_dispatch:\n", "");
        assert!(verify_ci_usage_gate(&bad)
            .unwrap_err()
            .to_string()
            .contains("workflow triggers"));
    }

    #[test]
    fn guard_rejects_push_gate_without_ci_full_token() {
        let bad = CI_WORKFLOW.replace("[ci full]", "[ci]");
        assert!(verify_ci_usage_gate(&bad)
            .unwrap_err()
            .to_string()
            .contains("[ci full]"));
    }

    #[test]
    fn guard_rejects_runner_job_without_top_level_if() {
        let bad = CI_WORKFLOW.replacen(
            "    if: ${{ github.event_name == 'workflow_dispatch' || github.event_name == 'pull_request' || contains(github.event.head_commit.message, '[ci full]') }}\n",
            "",
            1,
        );
        assert!(verify_ci_usage_gate(&bad)
            .unwrap_err()
            .to_string()
            .contains("exact top-level usage gate"));
    }

    #[test]
    fn guard_rejects_step_level_if_without_job_gate() {
        let bad = CI_WORKFLOW
            .replacen(&format!("    {FULL_CI_JOB_IF}\n"), "", 1)
            .replacen(
                "      - uses: actions/checkout@v4\n",
                &format!("      - uses: actions/checkout@v4\n        {FULL_CI_JOB_IF}\n"),
                1,
            );
        assert!(verify_ci_usage_gate(&bad)
            .unwrap_err()
            .to_string()
            .contains("exact top-level usage gate"));
    }

    #[test]
    fn guard_rejects_plain_push_escape_in_job_if() {
        let bad = CI_WORKFLOW.replacen(
            FULL_CI_JOB_IF,
            "if: ${{ github.event_name == 'workflow_dispatch' || github.event_name == 'pull_request' || github.event_name == 'push' || contains(github.event.head_commit.message, '[ci full]') }}",
            1,
        );
        assert!(verify_ci_usage_gate(&bad)
            .unwrap_err()
            .to_string()
            .contains("exact top-level usage gate"));
    }

    #[test]
    fn guard_rejects_ci_full_token_only_in_comment() {
        let bad = CI_WORKFLOW.replacen(
            FULL_CI_JOB_IF,
            "if: ${{ github.event_name == 'workflow_dispatch' || github.event_name == 'pull_request' || contains(github.event.head_commit.message, '[ci]') }} # [ci full]",
            1,
        );
        assert!(verify_ci_usage_gate(&bad)
            .unwrap_err()
            .to_string()
            .contains("exact top-level usage gate"));
    }
}
