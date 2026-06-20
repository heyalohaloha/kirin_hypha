use anyhow::{bail, Context, Result};
use std::process::{Command, Output};

const CODESIGN_BIN: &str = "/usr/bin/codesign";

pub fn command() -> Command {
    Command::new(CODESIGN_BIN)
}

pub fn run_status(cmd: &mut Command, label: &str) -> Result<()> {
    let out = run_output(cmd, label)?;
    if !out.status.success() {
        bail!("{}", failure_message(label, &out));
    }
    Ok(())
}

pub fn run_output(cmd: &mut Command, label: &str) -> Result<Output> {
    cmd.output().with_context(|| format!("spawn {label}"))
}

pub fn failure_message(label: &str, out: &Output) -> String {
    let stderr = String::from_utf8_lossy(&out.stderr);
    let stdout = String::from_utf8_lossy(&out.stdout);
    let mut msg = format!("{label} failed with status {}", out.status);

    let stderr_trimmed = stderr.trim();
    if !stderr_trimmed.is_empty() {
        msg.push_str("\nstderr:\n");
        msg.push_str(stderr_trimmed);
    }

    let stdout_trimmed = stdout.trim();
    if !stdout_trimmed.is_empty() {
        msg.push_str("\nstdout:\n");
        msg.push_str(stdout_trimmed);
    }

    if looks_like_sandbox_false_negative(&stderr) {
        msg.push_str("\n\nhint: codesign reported an invalid seal, but this exact message can be a sandboxed child-process false negative for notarized Audio Unit/VST3 bundles. Re-run the same xtask command from an unsandboxed Terminal/Codex approval context before re-signing. If direct /usr/bin/codesign verification also fails outside the sandbox, treat the bundle as genuinely damaged.");
    }

    msg
}

fn looks_like_sandbox_false_negative(stderr: &str) -> bool {
    stderr.contains("invalid signature (code or signature have been modified)")
        || stderr.contains("Authority=(unavailable)")
}
