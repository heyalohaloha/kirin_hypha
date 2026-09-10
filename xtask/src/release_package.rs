use anyhow::{anyhow, bail, Context, Result};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::macos_codesign;
use crate::release_gate::{git_dirty_for_manifest, verify_package_mode, UNSIGNED_SUFFIX};
use crate::release_package_metadata::{self, BundleEntry};
use crate::ship_bundle::{self, BundleKind, MacBundleSpec};

const TEAM_ID: &str = "7N8BSMA684";
const DIST_DIR: &str = "dist";
const PACKAGE_SUFFIX: &str = "macOS-Universal";
const FORBIDDEN_FRAMEWORKS: &[&str] = &["WebKit.framework", "DiscRecording.framework"];

struct ShipBundle {
    spec: MacBundleSpec,
    source: PathBuf,
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
    let bundles = ship_bundles()?;
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
            eprintln!(
                "  include:      {} -> {}",
                b.source.display(),
                b.spec.archive_relative.display()
            );
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
        let dst = b.spec.archive_path(&package_root);
        let parent = dst
            .parent()
            .with_context(|| format!("archive destination has no parent: {}", dst.display()))?;
        fs::create_dir_all(parent).with_context(|| format!("create {}", parent.display()))?;
        run_status(
            Command::new("ditto").arg(&b.source).arg(&dst),
            "ditto bundle into package",
        )?;
        ship_bundle::verify_binary_copy(&b.source, &dst, &b.spec)
            .with_context(|| format!("{} archive copy mismatch", b.spec.label()))?;
        ship_bundle::verify_bundle_contract(&dst, &b.spec)
            .with_context(|| format!("{} archive metadata mismatch", b.spec.label()))?;
    }

    copy_required("README.md", &package_root.join("README.md"))?;
    copy_required("LICENSE", &package_root.join("LICENSE"))?;
    fs::write(
        package_root.join("INSTALL.txt"),
        release_package_metadata::install_text(&version),
    )
    .with_context(|| format!("write {}", package_root.join("INSTALL.txt").display()))?;

    run_status(
        Command::new("ditto")
            .args(["-c", "-k", "--norsrc", "--noqtn", "--keepParent"])
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
        release_package_metadata::manifest_json(
            &version,
            &package_leaf,
            &sha,
            allow_unsigned,
            &source_git_dirty,
            &bundle_entries(&bundles)?,
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
         Builds the Lemon Squeezy upload zip from the JUCE common-shell AU + VST3 set.\n\
         Default checks: clean source worktree, Developer-ID team {TEAM_ID}, notarized, universal,\n\
         version-matched, no WebKit/DiscRecording. --allow-unsigned is forced to /tmp and marks\n\
         the zip as {UNSIGNED_SUFFIX}."
    );
}

fn ship_bundles() -> Result<Vec<ShipBundle>> {
    let build_root = ship_bundle::default_build_root()?;
    Ok(ship_bundle::macos_bundles()?
        .into_iter()
        .map(|spec| ShipBundle {
            source: spec.source_path(&build_root),
            spec,
        })
        .collect())
}

fn verify_ship_set_shape(bundles: &[ShipBundle]) -> Result<()> {
    if bundles.len() != 4 {
        bail!("common-shell ship set must contain exactly 4 bundles");
    }
    let build_root = ship_bundle::default_build_root()?;
    for b in bundles {
        if !b.source.starts_with(&build_root) {
            bail!(
                "non-JUCE source forbidden in ship set: {}",
                b.source.display()
            );
        }
        if b.source.extension().and_then(|value| value.to_str()) != Some(b.spec.kind.extension()) {
            bail!("unexpected bundle extension for {}", b.source.display());
        }
    }
    Ok(())
}

fn verify_sources(bundles: &[ShipBundle], version: &str, allow_unsigned: bool) -> Result<()> {
    for b in bundles {
        let label = b.spec.label();
        if !b.source.is_dir() {
            bail!("{} missing: {}", label, b.source.display());
        }
        let bin = ship_bundle::verify_bundle_contract(&b.source, &b.spec)
            .with_context(|| format!("{} display metadata mismatch", label))?;
        verify_universal(&bin).with_context(|| format!("{} is not universal", label))?;
        verify_forbidden_frameworks_absent(&bin)
            .with_context(|| format!("{} forbidden framework check failed", label))?;
        verify_bundle_version(&b.source, version)
            .with_context(|| format!("{} version mismatch", label))?;
        if b.spec.kind == BundleKind::Au {
            verify_au_resource_usage(&b.source)
                .with_context(|| format!("{} AU resourceUsage check failed", label))?;
        }
        if !allow_unsigned {
            verify_signed_and_notarized(&b.source)
                .with_context(|| format!("{} signing/notarization check failed", label))?;
        }
        eprintln!(
            "[release-package] verified {} ({})",
            label,
            b.source.display()
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
        let value = ship_bundle::plist_value(&plist, key)?;
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
    macos_codesign::run_status(
        macos_codesign::command()
            .args(["--verify", "--deep", "--strict", "--verbose=2"])
            .arg(bundle),
        "codesign verify",
    )?;
    // B-139: plugin bundles (`.component` / `.vst3`, CFBundlePackageType=BNDL) are not a
    // supported stapler target on macOS 15. `codesign --check-notarization` is the per-bundle
    // release gate for the notarization ticket; any outer `.dmg` / `.pkg` container may still be
    // stapled separately if we introduce one later.
    macos_codesign::run_status(
        macos_codesign::command()
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
    let out = macos_codesign::command()
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

fn bundle_entries(bundles: &[ShipBundle]) -> Result<Vec<BundleEntry>> {
    bundles
        .iter()
        .map(|bundle| {
            Ok(BundleEntry {
                label: bundle.spec.label(),
                format: bundle
                    .spec
                    .archive_relative
                    .parent()
                    .and_then(Path::to_str)
                    .context("archive format path must be UTF-8")?
                    .to_string(),
                file: bundle
                    .spec
                    .archive_relative
                    .file_name()
                    .and_then(|value| value.to_str())
                    .context("archive bundle file name must be UTF-8")?
                    .to_string(),
            })
        })
        .collect()
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

#[cfg(test)]
#[path = "release_package/tests.rs"]
mod tests;
