// B-042: macOS codesign + notarization pipeline for nih-plug VST3 bundles.
//
// Pipeline (R-11 verified against `man codesign` / `man notarytool` / `man stapler`):
//   1. cargo xtask bundle-universal <pkg> --release   (skippable via --skip-bundle)
//   2. codesign --sign <identity> --options runtime --timestamp --deep --force
//   3. ditto -c -k --keepParent <bundle> <bundle>.zip
//   4. xcrun notarytool submit <zip> --wait  (--password or --keychain-profile)
//   5. xcrun stapler staple <bundle>
//   6. xcrun stapler validate <bundle>
//
// Notes:
//   - Bundle output name follows bundler.toml `name`, which can differ from the
//     Cargo package name (e.g. `hypha_pre` → "Kirin Hypha PRE"). Use
//     --bundle-name to override when they differ.
//   - `spctl -t plugin` is NOT a valid type (`man spctl` allows only
//     execute|install|open). For notarized non-app bundles the authoritative
//     check is `xcrun stapler validate`.
//   - `--password` and `--keychain-profile` are mutually exclusive; the latter
//     is preferred when credentials are pre-stored via
//     `xcrun notarytool store-credentials`.

use anyhow::{bail, Context, Result};
use std::path::PathBuf;
use std::process::Command;

pub fn run(args: Vec<String>) -> Result<()> {
    let mut package: Option<String> = None;
    let mut bundle_name: Option<String> = None;
    let mut identity: Option<String> = None;
    let mut team_id: Option<String> = None;
    let mut apple_id: Option<String> = None;
    let mut password: Option<String> = None;
    let mut keychain_profile: Option<String> = None;
    let mut skip_bundle = false;

    let mut iter = args.into_iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--package" => {
                package = Some(iter.next().context("--package requires a value")?);
            }
            "--bundle-name" => {
                bundle_name = Some(iter.next().context("--bundle-name requires a value")?);
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
            "--skip-bundle" => skip_bundle = true,
            "-h" | "--help" => {
                print_help();
                return Ok(());
            }
            other => bail!("unknown argument: {other}"),
        }
    }

    let package = package.context("--package <CARGO_PKG> is required")?;
    let bundle_name = bundle_name.unwrap_or_else(|| package.clone());
    let identity = identity.context(
        "--identity is required \
         (e.g. 'Developer ID Application: Your Name (TEAMID)')",
    )?;
    let team_id = team_id.context("--team-id is required")?;

    match (&password, &keychain_profile) {
        (Some(_), Some(_)) => bail!("--password and --keychain-profile are mutually exclusive"),
        (None, None) => bail!("either --password or --keychain-profile is required"),
        (Some(_), None) if apple_id.is_none() => {
            bail!("--apple-id is required when --password is used")
        }
        _ => {}
    }

    let bundle_path = PathBuf::from("target/bundled").join(format!("{bundle_name}.vst3"));

    // ── Step 1: universal bundle (delegated to nih_plug_xtask) ─────────────
    if !skip_bundle {
        eprintln!("==> bundle-universal --release {package}");
        nih_plug_xtask::main_with_args(
            "cargo xtask",
            vec![
                "bundle-universal".to_string(),
                package.clone(),
                "--release".to_string(),
            ],
        )?;
    }

    if !bundle_path.exists() {
        bail!(
            "bundle not found: {}\n\
             Hint: pass --bundle-name <NAME> matching the bundler.toml `name` field, \
             or omit --skip-bundle to build first.",
            bundle_path.display()
        );
    }

    // ── Step 2: codesign with hardened runtime ─────────────────────────────
    eprintln!("==> codesign --options runtime --timestamp --deep --force");
    run_status(
        Command::new("codesign")
            .args(["--sign", &identity])
            .args(["--timestamp", "--options", "runtime", "--deep", "--force"])
            .arg(&bundle_path),
        "codesign",
    )?;

    // ── Step 3: ditto zip (preserves extended attributes / symlinks) ──────
    let zip_path = PathBuf::from("target/bundled").join(format!("{bundle_name}.vst3.zip"));
    if zip_path.exists() {
        std::fs::remove_file(&zip_path)
            .with_context(|| format!("remove stale zip: {}", zip_path.display()))?;
    }
    eprintln!("==> ditto -c -k --keepParent -> {}", zip_path.display());
    run_status(
        Command::new("ditto")
            .args(["-c", "-k", "--keepParent"])
            .arg(&bundle_path)
            .arg(&zip_path),
        "ditto",
    )?;

    // ── Step 4: notarytool submit --wait ───────────────────────────────────
    eprintln!("==> xcrun notarytool submit --wait");
    let mut cmd = Command::new("xcrun");
    cmd.args(["notarytool", "submit"]).arg(&zip_path);
    if let Some(profile) = &keychain_profile {
        cmd.args(["--keychain-profile", profile]);
    } else {
        cmd.args([
            "--apple-id",
            apple_id.as_deref().expect("validated above"),
            "--team-id",
            &team_id,
            "--password",
            password.as_deref().expect("validated above"),
        ]);
    }
    cmd.arg("--wait");
    run_status(&mut cmd, "notarytool submit")?;

    // ── Step 5: stapler staple ─────────────────────────────────────────────
    eprintln!("==> xcrun stapler staple");
    run_status(
        Command::new("xcrun")
            .args(["stapler", "staple"])
            .arg(&bundle_path),
        "stapler staple",
    )?;

    // ── Step 6: verify via stapler validate (R-11: spctl has no 'plugin' type) ─
    eprintln!("==> xcrun stapler validate");
    run_status(
        Command::new("xcrun")
            .args(["stapler", "validate"])
            .arg(&bundle_path),
        "stapler validate",
    )?;

    eprintln!();
    eprintln!("notarize complete: {}", bundle_path.display());
    Ok(())
}

fn run_status(cmd: &mut Command, label: &str) -> Result<()> {
    let status = cmd
        .status()
        .with_context(|| format!("spawn {label}"))?;
    if !status.success() {
        bail!("{label} failed with status: {status}");
    }
    Ok(())
}

fn print_help() {
    println!("Usage: cargo xtask notarize \\");
    println!("         --package <CARGO_PKG> --identity <CERT> --team-id <TEAM> \\");
    println!("         (--password <PW> --apple-id <EMAIL> | --keychain-profile <PROF>) \\");
    println!("         [--bundle-name <NAME>] [--skip-bundle]");
    println!();
    println!("Required:");
    println!("  --package <CARGO_PKG>      Cargo package name (passed to bundle-universal)");
    println!("  --identity <CERT>          codesign identity string, e.g.");
    println!("                               'Developer ID Application: Your Name (TEAMID)'");
    println!("  --team-id  <TEAM>          Apple Developer Team ID (10 chars)");
    println!();
    println!("Credentials (choose ONE):");
    println!("  --password <PW> --apple-id <EMAIL>");
    println!("                             App-specific password + Apple ID");
    println!("  --keychain-profile <PROF>  Profile stored via");
    println!("                               xcrun notarytool store-credentials");
    println!();
    println!("Optional:");
    println!("  --bundle-name <NAME>       Bundle output name (default: same as --package).");
    println!("                             Must match bundler.toml `name`, e.g.");
    println!("                               --bundle-name 'Kirin Hypha PRE'");
    println!("  --skip-bundle              Skip Step 1 (reuse existing target/bundled/*.vst3)");
    println!();
    println!("Pipeline:");
    println!("  1. cargo xtask bundle-universal <CARGO_PKG> --release   (unless --skip-bundle)");
    println!("  2. codesign --sign <CERT> --timestamp --options runtime --deep --force");
    println!("  3. ditto -c -k --keepParent <bundle> <bundle>.zip");
    println!("  4. xcrun notarytool submit <zip> ... --wait");
    println!("  5. xcrun stapler staple <bundle>");
    println!("  6. xcrun stapler validate <bundle>");
}
