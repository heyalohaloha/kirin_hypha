//! `cargo xtask install --release` — deploy the SIGNED construction-C ship bundles
//! (egui VST3 + JUCE AU; JUCE VST3 excluded) to the system-level plugin folders.
//!
//! # B-099 rewrite
//! Rewritten from the B-022 nih-plug VST3-only installer (source `target/bundled/*.vst3`,
//! VST3-only) to the JUCE universal AU + VST3 source. The B-022 **system-level model is
//! retained**: the build output is deployed to the single canonical system path
//! (`/Library/Audio/Plug-Ins/{Components,VST3}`) and the user-level copies are removed first —
//! because a stale user-level binary makes a DAW load the wrong plugin (the original B-022
//! finding for Studio One). user-level is never the install target.
//!
//! # Source (B-098 signed / B-137 construction-C / G-115-344)
//! egui VST3: `target/bundled/Kirin Hypha {PRE,POST}.vst3` (bundle-universal + stamp-egui-version).
//! JUCE AU:   `juce_shell/build-universal/KirinHypha{PRE,POST}_artefacts/Release/AU/*.component`.
//! JUCE VST3 (`build-universal/.../Release/VST3/*.vst3`) is EXCLUDED from the ship set
//! (GUID continuity / existing+Peach session protection). All sources must be Developer-ID
//! signed + notarized.
//!
//! # Signed-source guard (B-099, required)
//! Each source bundle MUST be Developer-ID signed (`codesign` `TeamIdentifier=7N8BSMA684`, not
//! ad-hoc / unsigned) **and** notarized, or install aborts. `build_juce_universal.sh` re-signs
//! ad-hoc during a fresh build, so this guard refuses to deploy an unsigned/ad-hoc rebuild.
//!
//! # sudo
//! System dirs (`/Library/...`) need admin rights. The tool invokes `sudo` for the rm/cp steps
//! only (running `cargo` as root corrupts the cargo cache). If `SUDO_ASKPASS` is set, those
//! invocations use `sudo -A` so GUI/agent sessions without a visible terminal can still install.
//! B-022 discipline, unchanged.
//!
//! # VST3 / AU search paths (R-11)
//! - VST3: `/Library/Audio/Plug-Ins/VST3` (system) / `~/Library/Audio/Plug-Ins/VST3` (user)
//! - AU:   `/Library/Audio/Plug-Ins/Components` (system) / `~/Library/Audio/Plug-Ins/Components`

use anyhow::{anyhow, bail, Context, Result};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::macos_codesign;

/// Developer ID team. Source bundles must be signed by this team (not ad-hoc / unsigned).
const TEAM_ID: &str = "7N8BSMA684";

/// universal ship build root (scripts/build_juce_universal.sh output) — JUCE AU の source。
const BUILD_UNIVERSAL: &str = "juce_shell/build-universal";

/// 構成C (G-115-344): egui VST3 の source root（cargo xtask bundle-universal + stamp-egui-version 出力）。
const EGUI_BUNDLED: &str = "target/bundled";

/// system-level plugin dirs (root-owned / sudo required).
const SYSTEM_AU_DIR: &str = "/Library/Audio/Plug-Ins/Components";
const SYSTEM_VST3_DIR: &str = "/Library/Audio/Plug-Ins/VST3";

/// One installable bundle (PRE/POST × AU/VST3).
struct Bundle {
    /// e.g. "Kirin Hypha PRE".
    name: String,
    /// e.g. "PRE Kirin Hypha" — the role-first name DAWs/scanners must surface.
    display_name: String,
    /// bundle extension: "component" (AU) | "vst3" (VST3).
    ext: &'static str,
    /// source bundle path under build-universal.
    src: PathBuf,
    /// system-level destination dir.
    system_dir: &'static str,
}

impl Bundle {
    /// "Kirin Hypha PRE.component" etc.
    fn file(&self) -> String {
        format!("{}.{}", self.name, self.ext)
    }
    /// system-level destination bundle path.
    fn system_dest(&self) -> PathBuf {
        Path::new(self.system_dir).join(self.file())
    }
    /// user-level bundle path (removed first; never the install target).
    fn user_dest(&self) -> Result<PathBuf> {
        let home = std::env::var_os("HOME").context("HOME env var not set")?;
        let leaf = if self.ext == "component" {
            "Library/Audio/Plug-Ins/Components"
        } else {
            "Library/Audio/Plug-Ins/VST3"
        };
        Ok(PathBuf::from(home).join(leaf).join(self.file()))
    }
}

/// The 4 construction-C ship bundles (G-115-344): egui VST3 (PRE/POST, target/bundled) +
/// JUCE AU (PRE/POST, build-universal). JUCE VST3 (PRE/POST) は **出荷除外**（GUID 破壊回避・
/// 既存/Peach セッション保護）。dual-root: AU=build-universal/Release/AU、VST3=target/bundled。
fn bundles(root: &Path) -> Vec<Bundle> {
    let mut out = Vec::with_capacity(4);
    for role in ["PRE", "POST"] {
        let name = format!("Kirin Hypha {role}");
        let display_name = format!("{role} Kirin Hypha");
        // JUCE AU — build-universal/Release/AU → /Library/Audio/Plug-Ins/Components
        out.push(Bundle {
            src: root
                .join(BUILD_UNIVERSAL)
                .join(format!("KirinHypha{role}_artefacts/Release/AU"))
                .join(format!("{name}.component")),
            name: name.clone(),
            display_name: display_name.clone(),
            ext: "component",
            system_dir: SYSTEM_AU_DIR,
        });
        // egui VST3 — target/bundled → /Library/Audio/Plug-Ins/VST3（JUCE VST3 は出荷しない）
        out.push(Bundle {
            src: root.join(EGUI_BUNDLED).join(format!("{name}.vst3")),
            name,
            display_name,
            ext: "vst3",
            system_dir: SYSTEM_VST3_DIR,
        });
    }
    out
}

/// `cargo xtask install --release` のエントリポイント。
pub fn run(args: Vec<String>) -> Result<()> {
    let mut release_flag = false;
    for arg in args {
        match arg.as_str() {
            "--release" => release_flag = true,
            "-h" | "--help" => {
                print_usage();
                return Ok(());
            }
            other => bail!("unknown argument: {other}"),
        }
    }
    if !release_flag {
        bail!(
            "install requires --release (debug builds are unsuitable for host integration). \
             Run: cargo run --package xtask -- install --release"
        );
    }

    let bundles = bundles(Path::new("."));

    // 1. all 4 sources must exist and be Developer-ID signed + notarized (B-099 guard).
    for b in &bundles {
        if !b.src.is_dir() {
            bail!(
                "{} not found. Run scripts/build_juce_universal.sh, then sign with \
                 `cargo xtask notarize ...` (B-098) before installing.",
                b.src.display()
            );
        }
    }
    for b in &bundles {
        verify_signed(&b.src)?;
        verify_display_metadata(&b.src, b)?;
    }
    eprintln!(
        "[install] all 4 source bundles are Developer-ID signed ({TEAM_ID}) + notarized, display metadata OK"
    );

    // 2. remove user-level copies first (B-022: a stale user-level binary makes a DAW load the
    //    wrong plugin; the canonical install target is system-level only).
    for b in &bundles {
        remove_user_level(&b.user_dest()?)?;
    }

    // 3. deploy each bundle system-level (sudo).
    for b in &bundles {
        deploy_system_level(b)?;
    }

    // 4. verify the deployed copy matches the source.
    for b in &bundles {
        verify_deployment(b)?;
    }

    eprintln!("[install] done. 4 bundle(s) deployed to system-level. Restart your DAW to rescan.");
    Ok(())
}

fn print_usage() {
    eprintln!(
        "Usage: cargo run --package xtask -- install --release\n\n\
         Deploys the SIGNED construction-C ship bundles to the system plugin folders:\n\
         \x20 AU  -> /Library/Audio/Plug-Ins/Components/Kirin Hypha {{PRE,POST}}.component (JUCE)\n\
         \x20 VST3-> /Library/Audio/Plug-Ins/VST3/Kirin Hypha {{PRE,POST}}.vst3 (egui)\n\n\
         Source: JUCE AU = juce_shell/build-universal/.../Release/AU; egui VST3 = target/bundled/.\n\
         JUCE VST3 is EXCLUDED from the ship set (GUID continuity).\n\
         Each source must be Developer-ID signed ({TEAM_ID}) + notarized (B-098), else install\n\
         aborts. user-level copies are removed first (sudo prompt for the system deploy).\n\n\
         Build both shells + `cargo xtask notarize ...` beforehand."
    );
}

/// Signed-source guard: the bundle must be Developer-ID signed by [`TEAM_ID`] (not ad-hoc /
/// unsigned) and notarized, or this errors (so an unsigned/ad-hoc rebuild is never deployed).
fn verify_signed(bundle: &Path) -> Result<()> {
    // B-116/B-139: first verify the cryptographic seal and notarization ticket. On macOS 15,
    // querying display metadata (`codesign -dvv`) before verification can produce unstable
    // x86_64 bundle-level results for AU BNDL plugins, while the executable seal is valid.
    verify_codesign_seal(bundle).with_context(|| {
        format!(
            "{} is not Developer-ID signed by team {TEAM_ID}, or its seal is invalid",
            bundle.display()
        )
    })?;
    verify_notarization_ticket(bundle)?;

    // codesign -dvv writes the signing info to stderr.
    let out = macos_codesign::command()
        .arg("-dvv")
        .arg(bundle)
        .output()
        .with_context(|| format!("spawn codesign -dvv for {}", bundle.display()))?;
    let info = String::from_utf8_lossy(&out.stderr);
    if !info.contains(&format!("TeamIdentifier={TEAM_ID}")) {
        bail!(
            "{} is not Developer-ID signed by team {TEAM_ID} (unsigned or ad-hoc). \
             Re-sign with `cargo xtask notarize ...` (B-098) before install.\ncodesign -dvv:\n{}",
            bundle.display(),
            info.trim()
        );
    }
    Ok(())
}

/// B-139: `.component` / `.vst3` are `BNDL` plugin bundles. `stapler(1)` supports UDIF
/// disk images, signed flat packages, and certain executable bundles such as `.app`; on plugin
/// bundles it reports EX_NOINPUT / kLSDataUnavailableErr even when the Developer-ID seal is valid.
/// For source/destination install guards, use codesign's online notarization-ticket check instead.
fn verify_notarization_ticket(bundle: &Path) -> Result<()> {
    let out = macos_codesign::command()
        .args([
            "--verify",
            "--deep",
            "--strict",
            "--check-notarization",
            "--verbose=2",
        ])
        .arg(bundle)
        .output()
        .with_context(|| {
            format!(
                "spawn codesign --check-notarization for {}",
                bundle.display()
            )
        })?;
    if !out.status.success() {
        bail!(
            "{} failed codesign --check-notarization (not notarized / ticket unavailable). \
             Re-run `cargo xtask notarize ...` before install.\n{}",
            bundle.display(),
            macos_codesign::failure_message("codesign --check-notarization", &out)
        );
    }
    Ok(())
}

/// B-116: 署名封緘 (seal) の暗号検証。`codesign -dvv` は署名情報の表示に過ぎないため、別途
/// `codesign --verify --deep --strict --verbose=2` で封緘が有効か（改ざん・cp -R 破損がないか）を
/// 検証する。非破壊（読み取りのみ）。source guard と destination 検証の両方から呼ぶ。
fn verify_codesign_seal(bundle: &Path) -> Result<()> {
    let out = macos_codesign::command()
        .args(["--verify", "--deep", "--strict", "--verbose=2"])
        .arg(bundle)
        .output()
        .with_context(|| format!("spawn codesign --verify for {}", bundle.display()))?;
    if !out.status.success() {
        bail!(
            "{} failed codesign --verify --deep --strict (seal invalid / tampered / cp 破損). \
             Re-sign + notarize before install.\ncodesign --verify --deep --strict --verbose=2:\n{}",
            bundle.display(),
            macos_codesign::failure_message("codesign --verify", &out)
        );
    }
    Ok(())
}

/// B-116: 配置物が universal（x86_64 + arm64）であることを `lipo -archs` で実確認する。
/// どちらかの arch 欠落（single-arch ビルド混入）は明示エラーで停止。非破壊。
fn verify_universal(bin: &Path) -> Result<()> {
    let out = Command::new("lipo")
        .arg("-archs")
        .arg(bin)
        .output()
        .with_context(|| format!("spawn lipo -archs for {}", bin.display()))?;
    if !out.status.success() {
        bail!(
            "lipo -archs failed for {} (not a Mach-O binary?).\n{}",
            bin.display(),
            String::from_utf8_lossy(&out.stderr).trim()
        );
    }
    let archs = String::from_utf8_lossy(&out.stdout);
    let has_x86 = archs.split_whitespace().any(|a| a == "x86_64");
    let has_arm = archs.split_whitespace().any(|a| a == "arm64");
    if !(has_x86 && has_arm) {
        bail!(
            "{} is not universal (need x86_64 + arm64). lipo -archs = `{}` (single-arch build mixed in?).",
            bin.display(),
            archs.trim()
        );
    }
    Ok(())
}

/// user-level bundle を除去 (不在ならスキップ = 冪等)。所有者は user 自身なので sudo 不要。
fn remove_user_level(path: &Path) -> Result<()> {
    match fs::remove_dir_all(path) {
        Ok(()) => eprintln!("[install]   removed user-level {}", path.display()),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            eprintln!("[install]   skip {} (already absent)", path.display());
        }
        Err(e) => {
            return Err(anyhow!(
                "failed to remove {}: {} (kind={:?})",
                path.display(),
                e,
                e.kind()
            ));
        }
    }
    Ok(())
}

/// system-level 旧 bundle を sudo で除去 → 署名済 build 出力を sudo cp -R で配置 (B-022 踏襲)。
fn deploy_system_level(b: &Bundle) -> Result<()> {
    let sys_dir = Path::new(b.system_dir);
    if !sys_dir.exists() {
        bail!(
            "{} does not exist on this system. Cannot install.",
            sys_dir.display()
        );
    }
    let dst = b.system_dest();
    eprintln!("[install]   sudo rm -rf {}", dst.display());
    run_sudo(&["rm", "-rf", path_str(&dst)?])
        .with_context(|| format!("sudo rm -rf failed for {}", dst.display()))?;
    eprintln!(
        "[install]   sudo cp -R {} {}",
        b.src.display(),
        dst.display()
    );
    run_sudo(&["cp", "-R", path_str(&b.src)?, path_str(&dst)?]).with_context(|| {
        format!(
            "sudo cp -R failed for {} -> {}",
            b.src.display(),
            dst.display()
        )
    })?;
    Ok(())
}

/// 配置後の binary size を src と dst で比較 (受入確認支援)。
fn verify_deployment(b: &Bundle) -> Result<()> {
    let src_bin = b.src.join("Contents/MacOS").join(&b.name);
    let dst_bin = b.system_dest().join("Contents/MacOS").join(&b.name);
    let src_size = fs::metadata(&src_bin)
        .with_context(|| format!("stat src {}", src_bin.display()))?
        .len();
    let dst_size = fs::metadata(&dst_bin)
        .with_context(|| format!("stat dst {}", dst_bin.display()))?
        .len();
    if src_size != dst_size {
        bail!(
            "size mismatch after install: {} src={} dst={}",
            b.file(),
            src_size,
            dst_size
        );
    }
    eprintln!(
        "[install]   verified {}: size={} bytes (src=dst)",
        b.file(),
        src_size
    );
    // B-116: source / 配置物の両バイナリが universal (x86_64 + arm64) であることを lipo -archs で
    // 実確認する（single-arch ビルド混入の検出 / 非破壊）。
    verify_universal(&src_bin)
        .with_context(|| format!("B-116: source binary not universal: {}", src_bin.display()))?;
    verify_universal(&dst_bin).with_context(|| {
        format!(
            "B-116: installed binary not universal: {}",
            dst_bin.display()
        )
    })?;
    eprintln!(
        "[install]   verified {}: lipo -archs = x86_64 + arm64 (src + dst)",
        b.file()
    );
    // B-112/B-139: destination 側も Developer-ID 署名 + notarized を検証する（cp -R / sudo で署名や
    // notarization ticket 参照が破損していないこと、配置後に Gatekeeper が通る状態であることを確認）。
    // 検証失敗（codesign TeamIdentifier 不一致 / notarization ticket 不在）は明示エラーで停止する。
    verify_signed(&b.system_dest()).with_context(|| {
        format!(
            "B-112/B-139: destination verify failed for {} (installed copy not Developer-ID signed + notarized)",
            b.system_dest().display()
        )
    })?;
    verify_display_metadata(&b.system_dest(), b).with_context(|| {
        format!(
            "B-213: destination display metadata mismatch for {}",
            b.system_dest().display()
        )
    })?;
    eprintln!(
        "[install]   verified {}: destination codesign + notarization + display metadata OK",
        b.file()
    );
    Ok(())
}

fn verify_display_metadata(bundle_path: &Path, bundle: &Bundle) -> Result<()> {
    let plist = bundle_path.join("Contents/Info.plist");
    if bundle.ext == "component" {
        let expected = format!("Kirin: {}", bundle.display_name);
        let actual = plist_value(&plist, "AudioComponents:0:name")?;
        if actual != expected {
            bail!(
                "{} AudioComponents:0:name = {}, expected {}",
                bundle_path.display(),
                actual,
                expected
            );
        }
        return Ok(());
    }

    for key in ["CFBundleDisplayName", "CFBundleName"] {
        let actual = plist_value(&plist, key)?;
        if actual != bundle.display_name {
            bail!(
                "{} {} = {}, expected {}. Run `cargo run -p xtask -- stamp-egui-version` after bundling.",
                bundle_path.display(),
                key,
                actual,
                bundle.display_name
            );
        }
    }
    verify_binary_contains_display_name(
        &bundle_path.join("Contents/MacOS").join(&bundle.name),
        &bundle.display_name,
    )
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

/// `Path::to_str` の Error 変換ヘルパ (sudo args は &str 必須)。
fn path_str(p: &Path) -> Result<&str> {
    p.to_str()
        .ok_or_else(|| anyhow!("non-UTF8 path: {}", p.display()))
}

/// sudo を spawn して同期実行。non-zero exit は Err。
fn run_sudo(args: &[&str]) -> Result<()> {
    let sudo_args = sudo_args_for(args, std::env::var_os("SUDO_ASKPASS").is_some());
    let status = Command::new("sudo")
        .args(&sudo_args)
        .status()
        .context("failed to spawn sudo (is sudo installed and on PATH?)")?;
    if !status.success() {
        bail!("sudo {:?} exited with status {}", sudo_args, status);
    }
    Ok(())
}

fn sudo_args_for<'a>(args: &'a [&'a str], use_askpass: bool) -> Vec<&'a str> {
    let mut out = Vec::with_capacity(args.len() + usize::from(use_askpass));
    if use_askpass {
        out.push("-A");
    }
    out.extend_from_slice(args);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bundles_returns_four_au_and_vst3() {
        let b = bundles(Path::new("."));
        assert_eq!(b.len(), 4);
        let names: Vec<_> = b.iter().map(|x| x.file()).collect();
        assert!(names.contains(&"Kirin Hypha PRE.component".to_string()));
        assert!(names.contains(&"Kirin Hypha PRE.vst3".to_string()));
        assert!(names.contains(&"Kirin Hypha POST.component".to_string()));
        assert!(names.contains(&"Kirin Hypha POST.vst3".to_string()));
    }

    #[test]
    fn au_goes_to_components_vst3_to_vst3_system_dir() {
        for x in bundles(Path::new(".")) {
            match x.ext {
                "component" => assert_eq!(x.system_dir, SYSTEM_AU_DIR),
                "vst3" => assert_eq!(x.system_dir, SYSTEM_VST3_DIR),
                other => panic!("unexpected ext {other}"),
            }
        }
    }

    #[test]
    fn source_paths_construction_c_dual_root() {
        // 構成C (G-115-344): AU=build-universal/Release/AU、egui VST3=target/bundled。
        // JUCE VST3 (build-universal/Release/VST3) は出荷除外＝どの src にも現れない。
        for x in bundles(Path::new(".")) {
            let s = x.src.to_string_lossy();
            match x.ext {
                "component" => {
                    assert!(
                        s.contains("juce_shell/build-universal/"),
                        "AU src not under build-universal: {s}"
                    );
                    assert!(s.contains("/Release/AU/"), "AU src not Release/AU: {s}");
                }
                "vst3" => {
                    assert!(
                        s.contains("target/bundled/"),
                        "egui VST3 src not under target/bundled: {s}"
                    );
                    assert!(
                        !s.contains("build-universal"),
                        "VST3 must NOT be the JUCE build-universal copy (構成C excludes JUCE VST3): {s}"
                    );
                }
                other => panic!("unexpected ext {other}"),
            }
        }
    }

    #[test]
    fn team_id_is_the_developer_id() {
        assert_eq!(TEAM_ID, "7N8BSMA684");
    }

    #[test]
    fn system_dirs_are_macos_standard() {
        assert_eq!(SYSTEM_AU_DIR, "/Library/Audio/Plug-Ins/Components");
        assert_eq!(SYSTEM_VST3_DIR, "/Library/Audio/Plug-Ins/VST3");
    }

    #[test]
    fn sudo_args_adds_askpass_flag_only_when_requested() {
        assert_eq!(
            sudo_args_for(&["rm", "-rf", "/tmp/x"], false),
            vec!["rm", "-rf", "/tmp/x"]
        );
        assert_eq!(
            sudo_args_for(&["rm", "-rf", "/tmp/x"], true),
            vec!["-A", "rm", "-rf", "/tmp/x"]
        );
    }

    #[test]
    fn user_dest_resolves_under_home_per_format() {
        let original = std::env::var_os("HOME");
        unsafe {
            std::env::set_var("HOME", "/tmp/fake-home");
        }
        for x in bundles(Path::new(".")) {
            let ud = x.user_dest().expect("HOME set");
            let s = ud.to_string_lossy();
            if x.ext == "component" {
                assert_eq!(
                    s,
                    "/tmp/fake-home/Library/Audio/Plug-Ins/Components/Kirin Hypha {}.component"
                        .replace(
                            "{}",
                            if x.name.ends_with("PRE") {
                                "PRE"
                            } else {
                                "POST"
                            }
                        )
                );
            } else {
                assert!(s.contains("/tmp/fake-home/Library/Audio/Plug-Ins/VST3/"));
            }
        }
        unsafe {
            match original {
                Some(v) => std::env::set_var("HOME", v),
                None => std::env::remove_var("HOME"),
            }
        }
    }

    #[test]
    fn run_rejects_missing_release_flag() {
        let err = run(vec![]).expect_err("must require --release");
        assert!(format!("{err}").contains("--release"));
    }

    #[test]
    fn run_rejects_unknown_argument() {
        let err = run(vec!["--unknown".into(), "--release".into()])
            .expect_err("unknown arg must be rejected");
        assert!(format!("{err}").contains("unknown argument"));
    }

    #[test]
    fn verify_signed_bails_on_unsigned() {
        // An unsigned plain directory: codesign -dvv reports "not signed", no TeamIdentifier ->
        // the guard must error (this is what protects against deploying an ad-hoc/unsigned rebuild).
        let tmp =
            std::env::temp_dir().join(format!("kirin_install_unsigned_{}", std::process::id()));
        let _ = fs::remove_dir_all(&tmp);
        fs::create_dir_all(&tmp).unwrap();
        let err = verify_signed(&tmp).expect_err("unsigned must be rejected");
        let msg = format!("{err}");
        assert!(
            msg.contains("Developer-ID") || msg.contains(TEAM_ID),
            "error must mention the signing requirement, got: {msg}"
        );
        let _ = fs::remove_dir_all(&tmp);
    }

    #[test]
    fn remove_user_level_is_idempotent_when_absent() {
        let tmp = std::env::temp_dir().join(format!("kirin_install_idem_{}", std::process::id()));
        let _ = fs::remove_dir_all(&tmp);
        // path does not exist
        remove_user_level(&tmp.join("Kirin Hypha PRE.component")).expect("absent must be ok");
    }

    #[test]
    fn remove_user_level_actually_removes_existing() {
        let tmp = std::env::temp_dir().join(format!("kirin_install_rm_{}", std::process::id()));
        let _ = fs::remove_dir_all(&tmp);
        let b = tmp.join("Kirin Hypha PRE.component");
        fs::create_dir_all(&b).unwrap();
        remove_user_level(&b).expect("removal ok");
        assert!(!b.exists(), "bundle must be gone");
        let _ = fs::remove_dir_all(&tmp);
    }
}
