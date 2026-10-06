use anyhow::{bail, Context, Result};
use std::path::Path;
use std::process::Command;

// Shared semantic key/plist/binary checks with the pkg, Windows and manifest producers.
// The input is public and defaults explicitly to empty; CMake cache is never trusted.
pub fn verify_bundle(bundle: &Path) -> Result<()> {
    checked(
        Command::new("node")
            .arg("scripts/updates/update_key_binding.mjs")
            .arg("mac-bundles")
            .arg(bundle),
    )?;
    Ok(())
}

pub fn verify_zip(zip: &Path, with_aax: bool) -> Result<serde_json::Value> {
    checked(
        Command::new("node")
            .arg("scripts/updates/update_key_binding.mjs")
            .arg("mac-zip")
            .arg(zip)
            .arg(if with_aax { "6" } else { "4" }),
    )
}

fn checked(command: &mut Command) -> Result<serde_json::Value> {
    command.env(
        "KIRIN_HYPHA_UPDATE_PUBLIC_KEY",
        std::env::var("KIRIN_HYPHA_UPDATE_PUBLIC_KEY").unwrap_or_default(),
    );
    let out = command
        .output()
        .context("run exact update-key package evidence check")?;
    if !out.status.success() {
        bail!(
            "update-key package evidence rejected: {}",
            String::from_utf8_lossy(&out.stderr)
        );
    }
    serde_json::from_slice(&out.stdout).context("parse update-key package evidence")
}
