// B-042 / B-083: macOS codesign + notarization pipeline.
//
// B-083/B-137: reworked from a single nih-plug VST3 to construction-C:
//   - JUCE AU: juce_shell/build-universal/.../Release/AU/*.component
//   - egui VST3: target/bundled/*.vst3
// JUCE VST3 bundles under build-universal/.../Release/VST3 are intentionally excluded.
//
// B-137 (G-115-344): construction-C ship set = egui VST3 (PRE/POST, target/bundled) + JUCE AU
// (PRE/POST, build-universal). JUCE VST3 (PRE/POST) is EXCLUDED from the ship set (GUID continuity /
// existing+Peach session protection). dual-root: --build-dir = JUCE AU root, --egui-dir = egui VST3
// root (default target/bundled). The per-bundle codesign/notarytool/staple flow (notarize_one) is
// bundle-type agnostic and unchanged. Build order: build_juce_universal.sh (JUCE AU, patches 0001+0002)
// and `cargo xtask bundle-universal hypha_pre --release` /
// `cargo xtask bundle-universal hypha_post --release` + stamp-egui-version (egui VST3) must all run
// before notarize.
//
// Pipeline per bundle (R-11 verified against `man codesign` / `man notarytool` / `man stapler`):
//   1. codesign --sign <identity> --options runtime --timestamp --deep --force <bundle>
//   2. ditto -c -k --keepParent <bundle> <bundle>.zip
//   3. xcrun notarytool submit <zip> --wait  (--keychain-profile, or --apple-id/--team-id/--password)
//   4. xcrun stapler staple <bundle>
//   5. xcrun stapler validate <bundle>
//
// Notes:
//   - `spctl -t plugin` is NOT a valid type (`man spctl` allows only execute|install|open). For
//     notarized non-app bundles the authoritative check is `xcrun stapler validate`.
//   - `--password` and `--keychain-profile` are mutually exclusive; the latter is preferred when
//     credentials are pre-stored via `xcrun notarytool store-credentials`.
//   - The actual codesign/notarytool/staple steps need an Apple Developer identity + credentials
//     (Daisuke). `--dry-run` resolves the four bundle paths and prints the exact commands that
//     would run, WITHOUT signing or contacting Apple (no credentials required).

use anyhow::{bail, Context, Result};
use std::path::{Path, PathBuf};
use std::process::Command;

/// 構成C (G-115-344) source root（dual-root）。
/// - JUCE AU = `scripts/build_juce_universal.sh` 出力（B-082）。
/// - egui VST3 = `cargo xtask bundle-universal … + stamp-egui-version` 出力。
const DEFAULT_JUCE_DIR: &str = "juce_shell/build-universal";
const DEFAULT_EGUI_DIR: &str = "target/bundled";
const DEFAULT_IDENTITY: &str = "Developer ID Application: daisuke nishio (7N8BSMA684)";
const DEFAULT_TEAM_ID: &str = "7N8BSMA684";
const DEFAULT_KEYCHAIN_PROFILE: &str = "kirin-notarize";

/// 構成C bundle の source root 種別（dual-root）。
#[derive(Clone, Copy)]
enum Root {
    Juce,
    Egui,
}

/// 出荷対象の 4 bundle（構成C / G-115-344）: egui VST3（PRE/POST, target/bundled）と
/// JUCE AU（PRE/POST, build-universal）。**JUCE VST3（PRE/POST）は出荷除外**
/// （GUID 破壊回避・既存/Peach セッション保護）。
const BUNDLES_C: [(&str, Root, &str); 4] = [
    (
        "PRE  AU  ",
        Root::Juce,
        "KirinHyphaPRE_artefacts/Release/AU/Kirin Hypha PRE.component",
    ),
    (
        "POST AU  ",
        Root::Juce,
        "KirinHyphaPOST_artefacts/Release/AU/Kirin Hypha POST.component",
    ),
    ("PRE  VST3", Root::Egui, "Kirin Hypha PRE.vst3"),
    ("POST VST3", Root::Egui, "Kirin Hypha POST.vst3"),
];

struct Bundle {
    label: String,
    path: PathBuf,
}

/// dual-root 解決: `Root::Juce` → juce_dir、`Root::Egui` → egui_dir に relpath を join。
fn resolve_bundles(juce_dir: &Path, egui_dir: &Path) -> Vec<Bundle> {
    BUNDLES_C
        .iter()
        .map(|(label, root, rel)| {
            let base = match root {
                Root::Juce => juce_dir,
                Root::Egui => egui_dir,
            };
            Bundle {
                label: (*label).to_string(),
                path: base.join(rel),
            }
        })
        .collect()
}

pub fn run(args: Vec<String>) -> Result<()> {
    let mut build_dir: Option<String> = None;
    let mut egui_dir: Option<String> = None;
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
            "--egui-dir" => {
                egui_dir = Some(iter.next().context("--egui-dir requires a value")?);
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
    let egui_dir = PathBuf::from(egui_dir.unwrap_or_else(|| DEFAULT_EGUI_DIR.to_string()));
    let bundles = resolve_bundles(&juce_dir, &egui_dir);

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
        "notarize complete: {} bundle(s) (構成C: JUCE AU under {}, egui VST3 under {}; JUCE VST3 excluded)",
        bundles.len(),
        juce_dir.display(),
        egui_dir.display()
    );
    Ok(())
}

/// 1 bundle に対する codesign → zip → notarytool submit → staple → validate。
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
    run_status(
        Command::new("codesign")
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

    // Step 4: stapler staple.
    eprintln!("==> xcrun stapler staple");
    run_status(
        Command::new("xcrun")
            .args(["stapler", "staple"])
            .arg(bundle),
        "stapler staple",
    )?;

    // Step 5: stapler validate (R-11: spctl has no 'plugin' type).
    eprintln!("==> xcrun stapler validate");
    run_status(
        Command::new("xcrun")
            .args(["stapler", "validate"])
            .arg(bundle),
        "stapler validate",
    )?;
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
        eprintln!("  4. xcrun stapler staple \"{}\"", b.path.display());
        eprintln!("  5. xcrun stapler validate \"{}\"", b.path.display());
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
    println!("Signs/notarizes/staples the 4 construction-C ship bundles (G-115-344):");
    println!("  JUCE AU   : KirinHypha{{PRE,POST}}_artefacts/Release/AU/*.component (--build-dir)");
    println!("  egui VST3 : Kirin Hypha {{PRE,POST}}.vst3 (--egui-dir)");
    println!("JUCE VST3 is EXCLUDED from the ship set (GUID continuity).");
    println!("Build both first: scripts/build_juce_universal.sh (JUCE AU) and");
    println!("  `cargo xtask bundle-universal hypha_pre --release`");
    println!("  `cargo xtask bundle-universal hypha_post --release`");
    println!("  `cargo xtask stamp-egui-version`");
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
    println!("  --build-dir <DIR>          JUCE AU build root (default {DEFAULT_JUCE_DIR})");
    println!("  --egui-dir  <DIR>          egui VST3 bundle root (default {DEFAULT_EGUI_DIR})");
    println!("  --dry-run                  Resolve paths + print commands; no signing, no");
    println!("                               credentials required.");
    println!();
    println!("Per-bundle pipeline:");
    println!("  1. codesign --sign <CERT> --timestamp --options runtime --deep --force <bundle>");
    println!("  2. ditto -c -k --keepParent <bundle> <bundle>.zip");
    println!("  3. xcrun notarytool submit <zip> ... --wait");
    println!("  4. xcrun stapler staple <bundle>");
    println!("  5. xcrun stapler validate <bundle>");
}
