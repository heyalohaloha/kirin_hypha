use anyhow::{bail, Context, Result};
use std::process::Command;

pub struct BundleEntry {
    pub label: String,
    pub format: String,
    pub file: String,
}

pub fn install_text(version: &str, with_aax: bool) -> String {
    let aax = if with_aax {
        "\nAAX:\n\
         - Copy AAX/Kirin Hypha PRE.aaxplugin and AAX/Kirin Hypha POST.aaxplugin to /Library/Application Support/Avid/Audio/Plug-Ins/ (administrator access is required).\n"
    } else {
        ""
    };
    format!(
        "Kirin Hypha {version}\n\n\
         Install either or both formats. Remove old Kirin Hypha PRE/POST copies from user-level and system-level plug-in folders first if your DAW still loads stale binaries.\n\n\
         VST3:\n\
         - Copy VST3/PRE Kirin Hypha.vst3 and VST3/POST Kirin Hypha.vst3 to ~/Library/Audio/Plug-Ins/VST3/\n\n\
         Audio Unit:\n\
         - Copy Audio Unit/Kirin Hypha PRE.component and Audio Unit/Kirin Hypha POST.component to ~/Library/Audio/Plug-Ins/Components/\n\n\
         {aax}\n\
         Restart or rescan your DAW after installation. If your DAW caches plug-ins, force a full plug-in rescan.\n"
    )
}

pub fn manifest_json(
    version: &str,
    package_leaf: &str,
    sha256: &str,
    allow_unsigned: bool,
    git_dirty: &str,
    with_aax: bool,
    bundles: &[BundleEntry],
) -> Result<String> {
    let commit = command_stdout(Command::new("git").args(["rev-parse", "HEAD"]))
        .unwrap_or_else(|_| "unknown".to_string());
    let mut s = String::new();
    s.push_str("{\n");
    s.push_str(&format!(
        "  \"product\": \"Kirin Hypha\",\n  \"version\": \"{version}\",\n"
    ));
    s.push_str(&format!(
        "  \"commit\": \"{}\",\n",
        json_escape(commit.trim())
    ));
    s.push_str(&format!(
        "  \"package\": \"{package_leaf}.zip\",\n  \"sha256\": \"{sha256}\",\n"
    ));
    s.push_str(&format!(
        "  \"unsigned_smoke_test\": {allow_unsigned},\n  \"git_dirty\": \"{}\",\n",
        json_escape(git_dirty)
    ));
    let ship_set = if with_aax {
        "juce-common-shell+aax"
    } else {
        "juce-common-shell"
    };
    s.push_str(&format!(
        "  \"ship_set\": \"{ship_set}\",\n  \"aax_included\": {with_aax},\n  \"bundles\": [\n"
    ));
    for (index, bundle) in bundles.iter().enumerate() {
        let comma = if index + 1 == bundles.len() { "" } else { "," };
        s.push_str(&format!(
            "    {{ \"label\": \"{}\", \"format\": \"{}\", \"file\": \"{}\" }}{comma}\n",
            json_escape(&bundle.label),
            json_escape(&bundle.format),
            json_escape(&bundle.file)
        ));
    }
    s.push_str("  ]\n}\n");
    Ok(s)
}

fn command_stdout(cmd: &mut Command) -> Result<String> {
    let out = cmd.output().context("spawn command")?;
    if !out.status.success() {
        bail!("command failed: {}", String::from_utf8_lossy(&out.stderr));
    }
    Ok(String::from_utf8_lossy(&out.stdout).to_string())
}

fn json_escape(value: &str) -> String {
    value
        .replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n")
        .replace('\r', "\\r")
}
