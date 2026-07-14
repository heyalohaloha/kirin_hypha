// B-042 / B-083: macOS codesign + notarization pipeline.
//
// The four macOS ship bundles are the AU/VST3 outputs of one JUCE universal build. VST3 retains
// the original nih-plug component IDs and state migration, so session continuity no longer
// requires keeping a second GUI/runtime in the ship set.
//
// Pipeline per bundle (B-139 verified against `man codesign` / `man notarytool` / `man stapler`):
//   1. codesign --sign <identity> --options runtime --timestamp --deep --force <bundle>
//   2. ditto -c -k --keepParent <bundle> <bundle>.zip
//   3. xcrun notarytool submit <zip> --wait  (--keychain-profile, or --apple-id/--team-id/--password)
//   4. codesign --verify --check-notarization <bundle>
//
// Notes:
//   - `spctl -t plugin` is NOT a valid type (`man spctl` allows only execute|install|open). For
//     notarized plugin bundles the release gate uses codesign's online ticket check.
//   - `stapler(1)` supports UDIF disk images, signed flat packages, and certain executable
//     bundles such as `.app`. `.component` / `.vst3` plugin bundles are `BNDL` and are not
//     valid stapler targets on macOS 15 (EX_NOINPUT / kLSDataUnavailableErr).
//   - `--password` and `--keychain-profile` are mutually exclusive; the latter is preferred when
//     credentials are pre-stored via `xcrun notarytool store-credentials`.
//   - The actual codesign/notarytool steps need an Apple Developer identity + credentials
//     (Daisuke). `--dry-run` resolves the four bundle paths and prints the exact commands that
//     would run, WITHOUT signing or contacting Apple (no credentials required).

use anyhow::{bail, Context, Result};
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::macos_codesign;

const DEFAULT_JUCE_DIR: &str = "juce_shell/build-universal";
const DEFAULT_IDENTITY: &str = "Developer ID Application: daisuke nishio (7N8BSMA684)";
const DEFAULT_TEAM_ID: &str = "7N8BSMA684";
const DEFAULT_KEYCHAIN_PROFILE: &str = "kirin-notarize";

const SHIP_BUNDLES: [(&str, &str, &str, bool, Option<&str>); 4] = [
    (
        "PRE  AU  ",
        "KirinHyphaPRE_artefacts/Release/AU/Kirin Hypha PRE.component",
        "PRE Kirin Hypha",
        true,
        None,
    ),
    (
        "POST AU  ",
        "KirinHyphaPOST_artefacts/Release/AU/Kirin Hypha POST.component",
        "POST Kirin Hypha",
        true,
        None,
    ),
    (
        "PRE  VST3",
        "KirinHyphaPRE_artefacts/Release/VST3/Kirin Hypha PRE.vst3",
        "PRE Kirin Hypha",
        false,
        Some("4B6972696E4879706861505245763031"),
    ),
    (
        "POST VST3",
        "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3",
        "POST Kirin Hypha",
        false,
        Some("4B6972696E4879706861504F53547631"),
    ),
];

struct Bundle {
    label: String,
    path: PathBuf,
    display_name: &'static str,
    is_au: bool,
    component_cid: Option<&'static str>,
}

fn resolve_bundles(juce_dir: &Path) -> Vec<Bundle> {
    SHIP_BUNDLES
        .iter()
        .map(|(label, rel, display_name, is_au, component_cid)| Bundle {
            label: (*label).to_string(),
            path: juce_dir.join(rel),
            display_name,
            is_au: *is_au,
            component_cid: *component_cid,
        })
        .collect()
}

pub fn run(args: Vec<String>) -> Result<()> {
    let mut build_dir: Option<String> = None;
    let mut identity: Option<String> = None;
    let mut team_id: Option<String> = None;
    let mut apple_id: Option<String> = None;
    let mut password: Option<String> = None;
    let mut keychain_profile: Option<String> = None;
    let mut dry_run = false;

    let mut iter = args.into_iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--build-dir" => {
                build_dir = Some(iter.next().context("--build-dir requires a value")?);
            }
            "--identity" => {
                identity = Some(iter.next().context("--identity requires a value")?);
            }
            "--team-id" => {
                team_id = Some(iter.next().context("--team-id requires a value")?);
            }
            "--apple-id" => {
                apple_id = Some(iter.next().context("--apple-id requires a value")?);
            }
            "--password" => {
                password = Some(iter.next().context("--password requires a value")?);
            }
            "--keychain-profile" => {
                keychain_profile =
                    Some(iter.next().context("--keychain-profile requires a value")?);
            }
            "--dry-run" => dry_run = true,
            "-h" | "--help" => {
                print_help();
                return Ok(());
            }
            other => bail!("unknown argument: {other}"),
        }
    }

    let juce_dir = PathBuf::from(build_dir.unwrap_or_else(|| DEFAULT_JUCE_DIR.to_string()));
    let bundles = resolve_bundles(&juce_dir);

    let identity = identity.unwrap_or_else(|| DEFAULT_IDENTITY.to_string());
    let team_id = team_id.unwrap_or_else(|| DEFAULT_TEAM_ID.to_string());
    if keychain_profile.is_none() && password.is_none() {
        keychain_profile = Some(DEFAULT_KEYCHAIN_PROFILE.to_string());
    }

    if dry_run {
        return dry_run_report(&bundles, &identity, keychain_profile.as_deref());
    }

    // ── 実署名モード: credential 必須（Daisuke 専管） ──────────────────────
    match (&password, &keychain_profile) {
        (Some(_), Some(_)) => bail!("--password and --keychain-profile are mutually exclusive"),
        (None, None) => bail!("either --password or --keychain-profile is required"),
        (Some(_), None) if apple_id.is_none() => {
            bail!("--apple-id is required when --password is used")
        }
        _ => {}
    }

    for b in &bundles {
        if !b.path.exists() {
            bail!(
                "bundle not found: {}\n\
                 Hint: run scripts/build_juce_universal.sh first (B-082), \
                 or pass --build-dir <DIR>.",
                b.path.display()
            );
        }
        verify_display_metadata(b).with_context(|| {
            format!(
                "{} display metadata mismatch before notarize",
                b.path.display()
            )
        })?;
        eprintln!(
            "==================== {} ====================",
            b.label.trim()
        );
        notarize_one(
            &b.path,
            &identity,
            &team_id,
            apple_id.as_deref(),
            password.as_deref(),
            keychain_profile.as_deref(),
        )?;
    }

    eprintln!();
    eprintln!(
        "notarize complete: {} JUCE common-shell bundle(s) under {}",
        bundles.len(),
        juce_dir.display()
    );
    Ok(())
}

/// 1 bundle に対する codesign → zip → notarytool submit → codesign ticket check。
fn notarize_one(
    bundle: &Path,
    identity: &str,
    team_id: &str,
    apple_id: Option<&str>,
    password: Option<&str>,
    keychain_profile: Option<&str>,
) -> Result<()> {
    // Step 1: codesign (hardened runtime, deep, timestamp).
    eprintln!("==> codesign --options runtime --timestamp --deep --force");
    macos_codesign::run_status(
        macos_codesign::command()
            .args(["--sign", identity])
            .args(["--timestamp", "--options", "runtime", "--deep", "--force"])
            .arg(bundle),
        "codesign",
    )?;

    // Step 2: ditto zip (preserves extended attributes / symlinks).
    let zip_path = zip_path_for(bundle);
    if zip_path.exists() {
        std::fs::remove_file(&zip_path)
            .with_context(|| format!("remove stale zip: {}", zip_path.display()))?;
    }
    eprintln!("==> ditto -c -k --keepParent -> {}", zip_path.display());
    run_status(
        Command::new("ditto")
            .args(["-c", "-k", "--keepParent"])
            .arg(bundle)
            .arg(&zip_path),
        "ditto",
    )?;

    // Step 3: notarytool submit --wait.
    eprintln!("==> xcrun notarytool submit --wait");
    let mut cmd = Command::new("xcrun");
    cmd.args(["notarytool", "submit"]).arg(&zip_path);
    if let Some(profile) = keychain_profile {
        cmd.args(["--keychain-profile", profile]);
    } else {
        cmd.args([
            "--apple-id",
            apple_id.expect("validated in run()"),
            "--team-id",
            team_id,
            "--password",
            password.expect("validated in run()"),
        ]);
    }
    cmd.arg("--wait");
    run_status(&mut cmd, "notarytool submit")?;

    // Step 4: plugin bundles are not stapler targets; verify the online notary ticket instead.
    eprintln!("==> codesign --check-notarization");
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
    Ok(())
}

fn verify_display_metadata(bundle: &Bundle) -> Result<()> {
    let plist = bundle.path.join("Contents/Info.plist");
    if bundle.is_au {
        let expected = format!("Kirin: {}", bundle.display_name);
        let actual = plist_value(&plist, "AudioComponents:0:name")?;
        if actual != expected {
            bail!(
                "{} AudioComponents:0:name = {}, expected {}",
                bundle.path.display(),
                actual,
                expected
            );
        }
        return Ok(());
    }

    let physical_name = bundle_file_stem(&bundle.path)?;
    for key in ["CFBundleDisplayName", "CFBundleName"] {
        let actual = plist_value(&plist, key)?;
        if actual != physical_name {
            bail!(
                "{} {} = {}, expected {}. Rebuild with scripts/build_juce_universal.sh.",
                bundle.path.display(),
                key,
                actual,
                physical_name
            );
        }
    }
    let moduleinfo =
        std::fs::read_to_string(bundle.path.join("Contents/Resources/moduleinfo.json"))
            .with_context(|| format!("read VST3 moduleinfo for {}", bundle.path.display()))?;
    let cid = bundle.component_cid.context("VST3 component CID missing")?;
    if !moduleinfo.contains(&format!("\"CID\": \"{cid}\""))
        || !moduleinfo.contains(&format!("\"Name\": \"{}\"", bundle.display_name))
    {
        bail!(
            "{} VST3 CID/display contract mismatch",
            bundle.path.display()
        );
    }
    verify_binary_contains_display_name(
        &bundle
            .path
            .join("Contents/MacOS")
            .join(bundle_file_stem(&bundle.path)?),
        bundle.display_name,
    )
}

fn bundle_file_stem(bundle: &Path) -> Result<&str> {
    bundle
        .file_stem()
        .and_then(|s| s.to_str())
        .ok_or_else(|| anyhow::anyhow!("non-UTF8 bundle name: {}", bundle.display()))
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

fn verify_binary_contains_display_name(binary: &Path, expected: &str) -> Result<()> {
    let out = Command::new("strings")
        .arg(binary)
        .output()
        .with_context(|| format!("spawn strings for {}", binary.display()))?;
    if !out.status.success() {
        bail!(
            "strings failed for {}: {}",
            binary.display(),
            String::from_utf8_lossy(&out.stderr)
        );
    }
    let text = String::from_utf8_lossy(&out.stdout);
    if !text.contains(expected) {
        bail!(
            "{} does not contain display name {}",
            binary.display(),
            expected
        );
    }
    Ok(())
}

/// <bundle>.zip のパス（拡張子はそのまま、末尾に .zip）。
fn zip_path_for(bundle: &Path) -> PathBuf {
    let mut s = bundle.as_os_str().to_os_string();
    s.push(".zip");
    PathBuf::from(s)
}

/// --dry-run: 4 bundle の解決パス・存在・実行されるコマンド構成を表示する（署名/通信なし）。
fn dry_run_report(
    bundles: &[Bundle],
    identity: &str,
    keychain_profile: Option<&str>,
) -> Result<()> {
    let cred = match keychain_profile {
        Some(p) => format!("--keychain-profile {p}"),
        None => "--apple-id <EMAIL> --team-id <TEAM> --password <PW>".to_string(),
    };
    eprintln!("== notarize --dry-run (no signing, no Apple contact) ==");
    let mut missing = 0usize;
    for b in bundles {
        let exists = b.path.exists();
        if !exists {
            missing += 1;
        }
        let zip = zip_path_for(&b.path);
        eprintln!();
        eprintln!(
            "[{}] {}",
            b.label.trim(),
            if exists { "FOUND" } else { "MISSING" }
        );
        eprintln!("  bundle: {}", b.path.display());
        eprintln!(
            "  1. codesign --sign \"{identity}\" --timestamp --options runtime --deep --force \"{}\"",
            b.path.display()
        );
        eprintln!(
            "  2. ditto -c -k --keepParent \"{}\" \"{}\"",
            b.path.display(),
            zip.display()
        );
        eprintln!(
            "  3. xcrun notarytool submit \"{}\" {cred} --wait",
            zip.display()
        );
        eprintln!(
            "  4. codesign --verify --deep --strict --check-notarization --verbose=2 \"{}\"",
            b.path.display()
        );
    }
    eprintln!();
    eprintln!(
        "resolved {} bundle(s); {} missing. (dry-run: nothing signed/submitted.)",
        bundles.len(),
        missing
    );
    Ok(())
}

fn run_status(cmd: &mut Command, label: &str) -> Result<()> {
    let status = cmd.status().with_context(|| format!("spawn {label}"))?;
    if !status.success() {
        bail!("{label} failed with status: {status}");
    }
    Ok(())
}

fn print_help() {
    println!("Usage: cargo xtask notarize \\");
    println!("         [--identity <CERT>] [--team-id <TEAM>] \\");
    println!("         [(--password <PW> --apple-id <EMAIL>) | --keychain-profile <PROF>] \\");
    println!("         [--build-dir <DIR>] [--dry-run]");
    println!();
    println!("Signs/notarizes the 4 JUCE common-shell ship bundles:");
    println!("  AU   : KirinHypha{{PRE,POST}}_artefacts/Release/AU/*.component");
    println!("  VST3 : KirinHypha{{PRE,POST}}_artefacts/Release/VST3/*.vst3");
    println!("Build first: scripts/build_juce_universal.sh");
    println!();
    println!("Defaults for Daisuke's release machine:");
    println!("  identity           {DEFAULT_IDENTITY}");
    println!("  team-id            {DEFAULT_TEAM_ID}");
    println!("  keychain-profile   {DEFAULT_KEYCHAIN_PROFILE}");
    println!();
    println!("Credential setup:");
    println!("  xcrun notarytool store-credentials \"{DEFAULT_KEYCHAIN_PROFILE}\" \\");
    println!("    --apple-id \"<APPLE_ID>\" --team-id \"{DEFAULT_TEAM_ID}\"");
    println!();
    println!("Credential overrides (choose ONE, real run):");
    println!("  --password <PW> --apple-id <EMAIL>   App-specific password + Apple ID");
    println!("  --keychain-profile <PROF>            xcrun notarytool store-credentials profile");
    println!();
    println!("Optional:");
    println!(
        "  --build-dir <DIR>          JUCE common-shell build root (default {DEFAULT_JUCE_DIR})"
    );
    println!("  --dry-run                  Resolve paths + print commands; no signing, no");
    println!("                               credentials required.");
    println!();
    println!("Per-bundle pipeline:");
    println!("  1. codesign --sign <CERT> --timestamp --options runtime --deep --force <bundle>");
    println!("  2. ditto -c -k --keepParent <bundle> <bundle>.zip");
    println!("  3. xcrun notarytool submit <zip> ... --wait");
    println!("  4. codesign --verify --deep --strict --check-notarization --verbose=2 <bundle>");
}
