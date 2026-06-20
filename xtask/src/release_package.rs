use anyhow::{anyhow, bail, Context, Result};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::release_gate::{git_dirty_for_manifest, verify_package_mode, UNSIGNED_SUFFIX};

const TEAM_ID: &str = "7N8BSMA684";
const DIST_DIR: &str = "dist";
const PACKAGE_SUFFIX: &str = "macOS-Universal";
const FORBIDDEN_FRAMEWORKS: &[&str] = &["WebKit.framework", "DiscRecording.framework"];

struct ShipBundle {
    label: &'static str,
    format_dir: &'static str,
    src: PathBuf,
    file_name: &'static str,
    binary_name: &'static str,
    is_au: bool,
}

pub fn run(args: Vec<String>) -> Result<()> {
    let mut dist_dir = PathBuf::from(DIST_DIR);
    let mut dry_run = false;
    let mut allow_unsigned = false;

    let mut iter = args.into_iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--dist-dir" => {
                dist_dir = PathBuf::from(iter.next().context("--dist-dir requires a value")?);
            }
            "--dry-run" => dry_run = true,
            "--allow-unsigned" => allow_unsigned = true,
            "-h" | "--help" => {
                print_help();
                return Ok(());
            }
            other => bail!("unknown argument: {other}"),
        }
    }

    let version = read_version()?;
    let bundles = ship_bundles();
    verify_ship_set_shape(&bundles)?;
    if !dry_run {
        verify_package_mode(&dist_dir, allow_unsigned)?;
    }
    verify_sources(&bundles, &version, allow_unsigned || dry_run)?;
    let source_git_dirty = git_dirty_for_manifest();

    let package_leaf = package_leaf(&version, allow_unsigned);
    let package_root_name = format!("Kirin Hypha {version}");
    let package_root = dist_dir.join(&package_root_name);
    let zip_path = dist_dir.join(format!("{package_leaf}.zip"));
    let sha_path = dist_dir.join(format!("{package_leaf}.zip.sha256"));
    let manifest_path = dist_dir.join("release-manifest.json");

    if dry_run {
        eprintln!("[release-package] dry run only");
        eprintln!(
            "  signing check: {}",
            if allow_unsigned {
                "skipped (--allow-unsigned)"
            } else {
                "skipped by dry-run"
            }
        );
        eprintln!("  package root: {}", package_root.display());
        eprintln!("  zip:          {}", zip_path.display());
        eprintln!("  sha256:       {}", sha_path.display());
        eprintln!("  manifest:     {}", manifest_path.display());
        for b in &bundles {
            let dst = format!("{}/{}", b.format_dir, b.file_name);
            eprintln!("  include:      {} -> {dst}", b.src.display());
        }
        return Ok(());
    }

    fs::create_dir_all(&dist_dir).with_context(|| format!("create {}", dist_dir.display()))?;
    remove_path_if_exists(&package_root)?;
    remove_path_if_exists(&zip_path)?;
    remove_path_if_exists(&sha_path)?;
    remove_path_if_exists(&manifest_path)?;

    fs::create_dir_all(&package_root)
        .with_context(|| format!("create {}", package_root.display()))?;
    for b in &bundles {
        let format_dir = package_root.join(b.format_dir);
        fs::create_dir_all(&format_dir)
            .with_context(|| format!("create {}", format_dir.display()))?;
        let dst = format_dir.join(b.file_name);
        run_status(
            Command::new("ditto").arg(&b.src).arg(&dst),
            "ditto bundle into package",
        )?;
    }

    copy_required("README.md", &package_root.join("README.md"))?;
    copy_required("LICENSE", &package_root.join("LICENSE"))?;
    fs::write(package_root.join("INSTALL.txt"), install_text(&version))
        .with_context(|| format!("write {}", package_root.join("INSTALL.txt").display()))?;

    run_status(
        Command::new("ditto")
            .args(["-c", "-k", "--keepParent"])
            .arg(&package_root)
            .arg(&zip_path),
        "ditto zip release package",
    )?;
    let sha = sha256_file(&zip_path)?;
    let zip_name = zip_path.file_name().unwrap().to_string_lossy();
    fs::write(&sha_path, format!("{sha}  {zip_name}\n"))
        .with_context(|| format!("write {}", sha_path.display()))?;
    fs::write(
        &manifest_path,
        manifest_json(
            &version,
            &package_leaf,
            &sha,
            allow_unsigned,
            &source_git_dirty,
            &bundles,
        )?,
    )
    .with_context(|| format!("write {}", manifest_path.display()))?;

    eprintln!("[release-package] wrote {}", zip_path.display());
    eprintln!("[release-package] sha256 {sha}");
    Ok(())
}

fn print_help() {
    eprintln!(
        "Usage: cargo run -p xtask -- release-package [--dist-dir dist] [--dry-run] [--allow-unsigned]\n\n\
         Builds the Lemon Squeezy upload zip from construction-C only: JUCE AU + egui VST3.\n\
         Default checks: clean source worktree, Developer-ID team {TEAM_ID}, notarized, universal,\n\
         version-matched, no WebKit/DiscRecording. --allow-unsigned is forced to /tmp and marks\n\
         the zip as {UNSIGNED_SUFFIX}."
    );
}

fn ship_bundles() -> Vec<ShipBundle> {
    [
        ("PRE AU", "Audio Unit", "juce_shell/build-universal/KirinHyphaPRE_artefacts/Release/AU/Kirin Hypha PRE.component", "Kirin Hypha PRE.component", "Kirin Hypha PRE", true),
        ("POST AU", "Audio Unit", "juce_shell/build-universal/KirinHyphaPOST_artefacts/Release/AU/Kirin Hypha POST.component", "Kirin Hypha POST.component", "Kirin Hypha POST", true),
        ("PRE VST3", "VST3", "target/bundled/Kirin Hypha PRE.vst3", "Kirin Hypha PRE.vst3", "Kirin Hypha PRE", false),
        ("POST VST3", "VST3", "target/bundled/Kirin Hypha POST.vst3", "Kirin Hypha POST.vst3", "Kirin Hypha POST", false),
    ]
    .into_iter()
    .map(|(label, format_dir, src, file_name, binary_name, is_au)| ShipBundle {
        label,
        format_dir,
        src: PathBuf::from(src),
        file_name,
        binary_name,
        is_au,
    })
    .collect()
}

fn verify_ship_set_shape(bundles: &[ShipBundle]) -> Result<()> {
    if bundles.len() != 4 {
        bail!("construction-C ship set must contain exactly 4 bundles");
    }
    for b in bundles {
        let s = b.src.to_string_lossy();
        if s.contains("/Release/VST3/") || s.contains("KirinHyphaPRE_artefacts/Release/VST3") {
            bail!("JUCE VST3 forbidden in ship set: {}", b.src.display());
        }
        match (b.is_au, b.src.extension().and_then(|x| x.to_str())) {
            (true, Some("component")) | (false, Some("vst3")) => {}
            _ => bail!("unexpected bundle extension for {}", b.src.display()),
        }
    }
    Ok(())
}

fn verify_sources(bundles: &[ShipBundle], version: &str, allow_unsigned: bool) -> Result<()> {
    for b in bundles {
        if !b.src.is_dir() {
            bail!("{} missing: {}", b.label, b.src.display());
        }
        let bin = b.src.join("Contents/MacOS").join(b.binary_name);
        verify_universal(&bin).with_context(|| format!("{} is not universal", b.label))?;
        verify_forbidden_frameworks_absent(&bin)
            .with_context(|| format!("{} forbidden framework check failed", b.label))?;
        verify_bundle_version(&b.src, version)
            .with_context(|| format!("{} version mismatch", b.label))?;
        if b.is_au {
            verify_au_resource_usage(&b.src)
                .with_context(|| format!("{} AU resourceUsage check failed", b.label))?;
        }
        if !allow_unsigned {
            verify_signed_and_notarized(&b.src)
                .with_context(|| format!("{} signing/notarization check failed", b.label))?;
        }
        eprintln!(
            "[release-package] verified {} ({})",
            b.label,
            b.src.display()
        );
    }
    Ok(())
}

fn verify_universal(bin: &Path) -> Result<()> {
    let out = Command::new("lipo")
        .arg("-archs")
        .arg(bin)
        .output()
        .with_context(|| format!("spawn lipo for {}", bin.display()))?;
    if !out.status.success() {
        bail!(
            "lipo failed for {}: {}",
            bin.display(),
            String::from_utf8_lossy(&out.stderr)
        );
    }
    let archs = String::from_utf8_lossy(&out.stdout);
    let has_x86 = archs.split_whitespace().any(|a| a == "x86_64");
    let has_arm = archs.split_whitespace().any(|a| a == "arm64");
    if !(has_x86 && has_arm) {
        bail!("{} is not universal: {}", bin.display(), archs.trim());
    }
    Ok(())
}

fn verify_forbidden_frameworks_absent(bin: &Path) -> Result<()> {
    let out = Command::new("otool")
        .arg("-L")
        .arg(bin)
        .output()
        .with_context(|| format!("spawn otool for {}", bin.display()))?;
    if !out.status.success() {
        bail!(
            "otool failed for {}: {}",
            bin.display(),
            String::from_utf8_lossy(&out.stderr)
        );
    }
    let libs = String::from_utf8_lossy(&out.stdout);
    for fw in FORBIDDEN_FRAMEWORKS {
        if libs.contains(fw) {
            bail!("{} links forbidden {}", bin.display(), fw);
        }
    }
    Ok(())
}

fn verify_bundle_version(bundle: &Path, expected: &str) -> Result<()> {
    let plist = bundle.join("Contents/Info.plist");
    for key in ["CFBundleShortVersionString", "CFBundleVersion"] {
        let value = plist_value(&plist, key)?;
        if value.trim() != expected {
            bail!(
                "{} {} = {}, expected {}",
                bundle.display(),
                key,
                value.trim(),
                expected
            );
        }
    }
    Ok(())
}

fn verify_au_resource_usage(bundle: &Path) -> Result<()> {
    let plist = bundle.join("Contents/Info.plist");
    let out = Command::new("plutil")
        .arg("-p")
        .arg(&plist)
        .output()
        .with_context(|| format!("spawn plutil for {}", plist.display()))?;
    if !out.status.success() {
        bail!(
            "plutil failed for {}: {}",
            plist.display(),
            String::from_utf8_lossy(&out.stderr)
        );
    }
    let text = String::from_utf8_lossy(&out.stdout);
    if !text.contains("temporary-exception.files.all.read-write") {
        bail!("AU resourceUsage missing files.all: {}", bundle.display());
    }
    if text.contains("network.client") {
        bail!("AU resourceUsage has network.client: {}", bundle.display());
    }
    Ok(())
}

fn verify_signed_and_notarized(bundle: &Path) -> Result<()> {
    run_status(
        Command::new("codesign")
            .args(["--verify", "--deep", "--strict", "--verbose=2"])
            .arg(bundle),
        "codesign verify",
    )?;
    // B-139: plugin bundles (`.component` / `.vst3`, CFBundlePackageType=BNDL) are not a
    // supported stapler target on macOS 15. `codesign --check-notarization` is the per-bundle
    // release gate for the notarization ticket; any outer `.dmg` / `.pkg` container may still be
    // stapled separately if we introduce one later.
    run_status(
        Command::new("codesign")
            .args([
                "--verify",
                "--deep",
                "--strict",
                "--check-notarization",
                "--verbose=2",
            ])
            .arg(bundle),
        "codesign --check-notarization",
    )?;
    let out = Command::new("codesign")
        .arg("-dvv")
        .arg(bundle)
        .output()
        .with_context(|| format!("spawn codesign -dvv for {}", bundle.display()))?;
    let info = String::from_utf8_lossy(&out.stderr);
    if !info.contains(&format!("TeamIdentifier={TEAM_ID}")) {
        bail!(
            "{} is not signed by team {TEAM_ID}:\n{}",
            bundle.display(),
            info.trim()
        );
    }
    Ok(())
}

fn plist_value(plist: &Path, key: &str) -> Result<String> {
    let out = Command::new("/usr/libexec/PlistBuddy")
        .args(["-c", &format!("Print :{key}")])
        .arg(plist)
        .output()
        .with_context(|| format!("spawn PlistBuddy for {}", plist.display()))?;
    if !out.status.success() {
        bail!(
            "PlistBuddy failed for {}: {}",
            plist.display(),
            String::from_utf8_lossy(&out.stderr)
        );
    }
    Ok(String::from_utf8_lossy(&out.stdout).trim().to_string())
}

fn read_version() -> Result<String> {
    let toml = fs::read_to_string("crates/hypha_pre/Cargo.toml")
        .context("read crates/hypha_pre/Cargo.toml")?;
    for line in toml.lines() {
        let t = line.trim();
        if let Some(rest) = t.strip_prefix("version") {
            if let Some(v) = rest.split('"').nth(1) {
                return Ok(v.to_string());
            }
        }
    }
    bail!("version not found in crates/hypha_pre/Cargo.toml")
}

fn package_leaf(version: &str, allow_unsigned: bool) -> String {
    let leaf = format!("Kirin-Hypha-{version}-{PACKAGE_SUFFIX}");
    if allow_unsigned {
        format!("{leaf}-{UNSIGNED_SUFFIX}")
    } else {
        leaf
    }
}

fn install_text(version: &str) -> String {
    format!(
        "Kirin Hypha {version}\n\n\
         Install either or both formats. Remove old Kirin Hypha PRE/POST copies from user-level and system-level plug-in folders first if your DAW still loads stale binaries.\n\n\
         VST3:\n\
         - Copy VST3/Kirin Hypha PRE.vst3 and VST3/Kirin Hypha POST.vst3 to ~/Library/Audio/Plug-Ins/VST3/\n\n\
         Audio Unit:\n\
         - Copy Audio Unit/Kirin Hypha PRE.component and Audio Unit/Kirin Hypha POST.component to ~/Library/Audio/Plug-Ins/Components/\n\n\
         Restart or rescan your DAW after installation. If your DAW caches plug-ins, force a full plug-in rescan.\n"
    )
}

fn manifest_json(
    version: &str,
    package_leaf: &str,
    sha256: &str,
    allow_unsigned: bool,
    git_dirty: &str,
    bundles: &[ShipBundle],
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
    s.push_str("  \"ship_set\": \"construction-C\",\n  \"bundles\": [\n");
    for (i, b) in bundles.iter().enumerate() {
        let comma = if i + 1 == bundles.len() { "" } else { "," };
        s.push_str(&format!(
            "    {{ \"label\": \"{}\", \"format\": \"{}\", \"file\": \"{}\" }}{comma}\n",
            json_escape(b.label),
            json_escape(b.format_dir),
            json_escape(b.file_name)
        ));
    }
    s.push_str("  ]\n}\n");
    Ok(s)
}

fn sha256_file(path: &Path) -> Result<String> {
    let out = Command::new("shasum")
        .args(["-a", "256"])
        .arg(path)
        .output()
        .with_context(|| format!("spawn shasum for {}", path.display()))?;
    if !out.status.success() {
        bail!(
            "shasum failed for {}: {}",
            path.display(),
            String::from_utf8_lossy(&out.stderr)
        );
    }
    String::from_utf8_lossy(&out.stdout)
        .split_whitespace()
        .next()
        .map(str::to_string)
        .ok_or_else(|| anyhow!("shasum produced no digest for {}", path.display()))
}

fn copy_required(src: &str, dst: &Path) -> Result<()> {
    fs::copy(src, dst).with_context(|| format!("copy {src} -> {}", dst.display()))?;
    Ok(())
}

fn remove_path_if_exists(path: &Path) -> Result<()> {
    if !path.exists() {
        return Ok(());
    }
    if path.is_dir() {
        fs::remove_dir_all(path).with_context(|| format!("remove {}", path.display()))
    } else {
        fs::remove_file(path).with_context(|| format!("remove {}", path.display()))
    }
}

fn run_status(cmd: &mut Command, label: &str) -> Result<()> {
    let status = cmd.status().with_context(|| format!("spawn {label}"))?;
    if !status.success() {
        bail!("{label} failed with status {status}");
    }
    Ok(())
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ship_set_is_construction_c_only() {
        let bundles = ship_bundles();
        verify_ship_set_shape(&bundles).unwrap();
        assert!(bundles
            .iter()
            .any(|b| b.src.starts_with("target/bundled") && !b.is_au));
        assert!(bundles
            .iter()
            .any(|b| b.src.starts_with("juce_shell/build-universal") && b.is_au));
        assert!(!bundles
            .iter()
            .any(|b| b.src.to_string_lossy().contains("/Release/VST3/")));
    }

    #[test]
    fn manifest_mentions_all_four_files() {
        let bundles = ship_bundles();
        let leaf = package_leaf("1.1.1", false);
        let json = manifest_json("1.1.1", &leaf, "abc", false, "false", &bundles).unwrap();
        for file in [
            "Kirin Hypha PRE.component",
            "Kirin Hypha POST.component",
            "Kirin Hypha PRE.vst3",
            "Kirin Hypha POST.vst3",
        ] {
            assert!(json.contains(file));
        }
        assert!(!json.contains("Release/VST3"));
        assert!(json.contains("\"unsigned_smoke_test\": false"));
    }
}
