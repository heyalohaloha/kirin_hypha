//! `cargo xtask install --release` — deploy the SIGNED universal JUCE bundles (AU + VST3) to the
//! system-level plugin folders.
//!
//! # B-099 rewrite
//! Rewritten from the B-022 nih-plug VST3-only installer (source `target/bundled/*.vst3`,
//! VST3-only) to the JUCE universal AU + VST3 source. The B-022 **system-level model is
//! retained**: the build output is deployed to the single canonical system path
//! (`/Library/Audio/Plug-Ins/{Components,VST3}`) and the user-level copies are removed first —
//! because a stale user-level binary makes a DAW load the wrong plugin (the original B-022
//! finding for Studio One). user-level is never the install target.
//!
//! # Source (B-098 signed)
//! `juce_shell/build-universal/KirinHypha{PRE,POST}_artefacts/Release/{AU/*.component,
//! VST3/*.vst3}` — the Developer-ID-signed + notarized + stapled bundles.
//!
//! # Signed-source guard (B-099, required)
//! Each source bundle MUST be Developer-ID signed (`codesign` `TeamIdentifier=7N8BSMA684`, not
//! ad-hoc / unsigned) **and** stapled, or install aborts. `build_juce_universal.sh` re-signs
//! ad-hoc during a fresh build, so this guard refuses to deploy an unsigned/ad-hoc rebuild.
//!
//! # sudo
//! System dirs (`/Library/...`) need admin rights. The tool invokes `sudo` for the rm/cp steps
//! only (running `cargo` as root corrupts the cargo cache). B-022 discipline, unchanged.
//!
//! # VST3 / AU search paths (R-11)
//! - VST3: `/Library/Audio/Plug-Ins/VST3` (system) / `~/Library/Audio/Plug-Ins/VST3` (user)
//! - AU:   `/Library/Audio/Plug-Ins/Components` (system) / `~/Library/Audio/Plug-Ins/Components`

use anyhow::{anyhow, bail, Context, Result};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Developer ID team. Source bundles must be signed by this team (not ad-hoc / unsigned).
const TEAM_ID: &str = "7N8BSMA684";

/// universal ship build root (scripts/build_juce_universal.sh output).
const BUILD_UNIVERSAL: &str = "juce_shell/build-universal";

/// system-level plugin dirs (root-owned / sudo required).
const SYSTEM_AU_DIR: &str = "/Library/Audio/Plug-Ins/Components";
const SYSTEM_VST3_DIR: &str = "/Library/Audio/Plug-Ins/VST3";

/// One installable bundle (PRE/POST × AU/VST3).
struct Bundle {
    /// e.g. "Kirin Hypha PRE".
    name: String,
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

/// The 4 ship bundles with source paths under `root` (workspace root, normally ".").
fn bundles(root: &Path) -> Vec<Bundle> {
    const SPECS: [(&str, &str, &str, &str); 4] = [
        ("PRE", "AU", "component", SYSTEM_AU_DIR),
        ("PRE", "VST3", "vst3", SYSTEM_VST3_DIR),
        ("POST", "AU", "component", SYSTEM_AU_DIR),
        ("POST", "VST3", "vst3", SYSTEM_VST3_DIR),
    ];
    SPECS
        .iter()
        .map(|(role, fmt_dir, ext, system_dir)| {
            let name = format!("Kirin Hypha {role}");
            let src = root
                .join(BUILD_UNIVERSAL)
                .join(format!("KirinHypha{role}_artefacts/Release/{fmt_dir}"))
                .join(format!("{name}.{ext}"));
            Bundle {
                name,
                ext,
                src,
                system_dir,
            }
        })
        .collect()
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

    // 1. all 4 sources must exist and be Developer-ID signed + stapled (B-099 guard).
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
    }
    eprintln!("[install] all 4 source bundles are Developer-ID signed ({TEAM_ID}) + stapled");

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
         Deploys the SIGNED universal JUCE bundles to the system plugin folders:\n\
         \x20 AU  -> /Library/Audio/Plug-Ins/Components/Kirin Hypha {{PRE,POST}}.component\n\
         \x20 VST3-> /Library/Audio/Plug-Ins/VST3/Kirin Hypha {{PRE,POST}}.vst3\n\n\
         Source: juce_shell/build-universal/KirinHypha{{PRE,POST}}_artefacts/Release/\n\
         Each source must be Developer-ID signed ({TEAM_ID}) + stapled (B-098), else install\n\
         aborts. user-level copies are removed first (sudo prompt for the system deploy).\n\n\
         Run scripts/build_juce_universal.sh + `cargo xtask notarize ...` beforehand."
    );
}

/// Signed-source guard: the bundle must be Developer-ID signed by [`TEAM_ID`] (not ad-hoc /
/// unsigned) and stapled, or this errors (so an unsigned/ad-hoc rebuild is never deployed).
fn verify_signed(bundle: &Path) -> Result<()> {
    // codesign -dvv writes the signing info to stderr.
    let out = Command::new("codesign")
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
    let stapled = Command::new("xcrun")
        .args(["stapler", "validate"])
        .arg(bundle)
        .status()
        .with_context(|| format!("spawn stapler validate for {}", bundle.display()))?;
    if !stapled.success() {
        bail!(
            "{} is not stapled (stapler validate failed). Re-notarize + staple before install.",
            bundle.display()
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
        bail!("{} does not exist on this system. Cannot install.", sys_dir.display());
    }
    let dst = b.system_dest();
    eprintln!("[install]   sudo rm -rf {}", dst.display());
    run_sudo(&["rm", "-rf", path_str(&dst)?])
        .with_context(|| format!("sudo rm -rf failed for {}", dst.display()))?;
    eprintln!("[install]   sudo cp -R {} {}", b.src.display(), dst.display());
    run_sudo(&["cp", "-R", path_str(&b.src)?, path_str(&dst)?])
        .with_context(|| format!("sudo cp -R failed for {} -> {}", b.src.display(), dst.display()))?;
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
    eprintln!("[install]   verified {}: size={} bytes (src=dst)", b.file(), src_size);
    Ok(())
}

/// `Path::to_str` の Error 変換ヘルパ (sudo args は &str 必須)。
fn path_str(p: &Path) -> Result<&str> {
    p.to_str().ok_or_else(|| anyhow!("non-UTF8 path: {}", p.display()))
}

/// sudo を spawn して同期実行。non-zero exit は Err。
fn run_sudo(args: &[&str]) -> Result<()> {
    let status = Command::new("sudo")
        .args(args)
        .status()
        .context("failed to spawn sudo (is sudo installed and on PATH?)")?;
    if !status.success() {
        bail!("sudo {:?} exited with status {}", args, status);
    }
    Ok(())
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
    fn source_paths_under_build_universal_release() {
        for x in bundles(Path::new(".")) {
            let s = x.src.to_string_lossy();
            assert!(s.contains("juce_shell/build-universal/"), "src not under build-universal: {s}");
            assert!(s.contains("/Release/"), "src not Release: {s}");
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
    fn user_dest_resolves_under_home_per_format() {
        let original = std::env::var_os("HOME");
        unsafe {
            std::env::set_var("HOME", "/tmp/fake-home");
        }
        for x in bundles(Path::new(".")) {
            let ud = x.user_dest().expect("HOME set");
            let s = ud.to_string_lossy();
            if x.ext == "component" {
                assert_eq!(s, "/tmp/fake-home/Library/Audio/Plug-Ins/Components/Kirin Hypha {}.component".replace("{}", if x.name.ends_with("PRE") {"PRE"} else {"POST"}));
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
        let tmp = std::env::temp_dir().join(format!("kirin_install_unsigned_{}", std::process::id()));
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
