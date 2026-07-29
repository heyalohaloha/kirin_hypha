//! `cargo xtask install --release` — deploy the signed JUCE common-shell AU + VST3 bundles.
//!
//! # B-099 rewrite
//! Rewritten from the B-022 nih-plug VST3-only installer (source `target/bundled/*.vst3`,
//! VST3-only) to the JUCE universal AU + VST3 source. The B-022 **system-level model is
//! retained**: the build output is deployed to the single canonical system path
//! (`/Library/Audio/Plug-Ins/{Components,VST3}`) and the user-level copies are removed first —
//! because a stale user-level binary makes a DAW load the wrong plugin (the original B-022
//! finding for Studio One). user-level is never the install target.
//!
//! # Source
//! All four bundles come from `juce_shell/build-universal/KirinHypha{PRE,POST}_artefacts/Release`.
//! The JUCE VST3 wrapper is compiled with the original nih-plug component CIDs and migrates its
//! exact legacy state contract, preserving existing sessions while removing the dual GUI runtime.
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
#[cfg(test)]
use crate::ship_bundle::BundleKind;
use crate::ship_bundle::{self, MacBundleSpec};

/// Developer ID team. Source bundles must be signed by this team (not ad-hoc / unsigned).
const TEAM_ID: &str = "7N8BSMA684";

const SYSTEM_ROOT: &str = "/";

/// A manifest-defined bundle resolved against one build root.
struct InstallBundle {
    spec: MacBundleSpec,
    source: PathBuf,
}

impl InstallBundle {
    fn file(&self) -> Result<&str> {
        self.spec.installed_file_name()
    }

    fn system_dest(&self) -> PathBuf {
        self.spec.installed_path(Path::new(SYSTEM_ROOT))
    }

    fn user_dest(&self) -> Result<PathBuf> {
        let home = std::env::var_os("HOME").context("HOME env var not set")?;
        Ok(self.spec.installed_path(&PathBuf::from(home)))
    }

    fn legacy_user_dests(&self) -> Result<Vec<PathBuf>> {
        let home = std::env::var_os("HOME").context("HOME env var not set")?;
        Ok(self.spec.legacy_installed_paths(&PathBuf::from(home)))
    }
}

fn bundles(root: &Path) -> Result<Vec<InstallBundle>> {
    let build_root = root.join(ship_bundle::default_build_root()?);
    Ok(ship_bundle::macos_bundles()?
        .into_iter()
        .map(|spec| InstallBundle {
            source: spec.source_path(&build_root),
            spec,
        })
        .collect())
}

/// `cargo xtask install --release` のエントリポイント。
pub fn run(args: Vec<String>) -> Result<()> {
    let mut release_flag = false;
    let mut verify_only = false;
    for arg in args {
        match arg.as_str() {
            "--release" => release_flag = true,
            "--verify-only" => verify_only = true,
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

    let bundles = bundles(Path::new("."))?;

    // 1. all 4 sources must exist and be Developer-ID signed + notarized (B-099 guard).
    for b in &bundles {
        if !b.source.is_dir() {
            bail!(
                "{} not found. Run scripts/build_juce_universal.sh, then sign with \
                 `cargo xtask notarize ...` (B-098) before installing.",
                b.source.display()
            );
        }
    }
    for b in &bundles {
        verify_signed(&b.source)?;
        ship_bundle::verify_bundle_contract(&b.source, &b.spec)?;
    }
    eprintln!(
        "[install] all 4 source bundles are Developer-ID signed ({TEAM_ID}) + notarized, display metadata OK"
    );

    if verify_only {
        for bundle in &bundles {
            verify_deployment(bundle)?;
        }
        eprintln!("[install] verify-only complete. 4 system-level bundle(s) match the source.");
        return Ok(());
    }

    // 2. Reproduce the exact role-first installed layout without privileges and verify it before
    //    removing or replacing any live plug-in.
    verify_staged_install_layout(&bundles)?;

    // 3. remove user-level copies first (B-022: a stale user-level binary makes a DAW load the
    //    wrong plugin; the canonical install target is system-level only).
    for b in &bundles {
        remove_user_level(&b.user_dest()?)?;
        for legacy in b.legacy_user_dests()? {
            remove_user_level(&legacy)?;
        }
    }

    // 4. deploy each bundle system-level (sudo).
    for b in &bundles {
        deploy_system_level(b)?;
    }

    // 5. verify the deployed copy matches the source.
    for b in &bundles {
        verify_deployment(b)?;
    }

    eprintln!("[install] done. 4 bundle(s) deployed to system-level. Restart your DAW to rescan.");
    Ok(())
}

fn print_usage() {
    eprintln!(
        "Usage: cargo run --package xtask -- install --release [--verify-only]\n\n\
         Deploys the signed JUCE common-shell bundles to the system plugin folders:\n\
         \x20 AU  -> /Library/Audio/Plug-Ins/Components/Kirin Hypha {{PRE,POST}}.component (JUCE)\n\
         \x20 VST3-> /Library/Audio/Plug-Ins/VST3/{{PRE,POST}} Kirin Hypha.vst3 (JUCE)\n\n\
         Source: juce_shell/build-universal/.../Release/{{AU,VST3}}.\n\
         VST3 preserves the original component CIDs and migrates old nih-plug state.\n\
         Each source must be Developer-ID signed ({TEAM_ID}) + notarized (B-098), else install\n\
         aborts. An exact non-privileged staging layout is verified before any removal or sudo.\n\
         --verify-only performs the source/system comparison without modifying installed files.\n\n\
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
fn deploy_system_level(b: &InstallBundle) -> Result<()> {
    let dst = b.system_dest();
    let sys_dir = dst
        .parent()
        .with_context(|| format!("system destination has no parent: {}", dst.display()))?;
    if !sys_dir.exists() {
        bail!(
            "{} does not exist on this system. Cannot install.",
            sys_dir.display()
        );
    }
    for legacy in b.spec.legacy_installed_paths(Path::new(SYSTEM_ROOT)) {
        eprintln!("[install]   sudo rm -rf {}", legacy.display());
        run_sudo(&["rm", "-rf", path_str(&legacy)?])
            .with_context(|| format!("sudo rm -rf failed for {}", legacy.display()))?;
    }
    eprintln!("[install]   sudo rm -rf {}", dst.display());
    run_sudo(&["rm", "-rf", path_str(&dst)?])
        .with_context(|| format!("sudo rm -rf failed for {}", dst.display()))?;
    eprintln!(
        "[install]   sudo cp -R {} {}",
        b.source.display(),
        dst.display()
    );
    run_sudo(&["cp", "-R", path_str(&b.source)?, path_str(&dst)?]).with_context(|| {
        format!(
            "sudo cp -R failed for {} -> {}",
            b.source.display(),
            dst.display()
        )
    })?;
    Ok(())
}

/// 配置後の実行ファイルを CFBundleExecutable から解決し、source と byte 同一であることを確認。
fn verify_deployment(b: &InstallBundle) -> Result<()> {
    let system_dest = b.system_dest();
    let (src_bin, dst_bin) = ship_bundle::verify_binary_copy(&b.source, &system_dest, &b.spec)?;
    let src_size = fs::metadata(&src_bin)?.len();
    eprintln!(
        "[install]   verified {}: byte-identical executable, size={} bytes",
        b.file()?,
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
        b.file()?
    );
    // B-112/B-139: destination 側も Developer-ID 署名 + notarized を検証する（cp -R / sudo で署名や
    // notarization ticket 参照が破損していないこと、配置後に Gatekeeper が通る状態であることを確認）。
    // 検証失敗（codesign TeamIdentifier 不一致 / notarization ticket 不在）は明示エラーで停止する。
    verify_signed(&system_dest).with_context(|| {
        format!(
            "B-112/B-139: destination verify failed for {} (installed copy not Developer-ID signed + notarized)",
            system_dest.display()
        )
    })?;
    ship_bundle::verify_bundle_contract(&system_dest, &b.spec).with_context(|| {
        format!(
            "B-213: destination display metadata mismatch for {}",
            system_dest.display()
        )
    })?;
    eprintln!(
        "[install]   verified {}: destination codesign + notarization + display metadata OK",
        b.file()?
    );
    Ok(())
}

struct StagingRoot(PathBuf);

impl StagingRoot {
    fn create() -> Result<Self> {
        let path = std::env::temp_dir().join(format!(
            "kirin_hypha_install_preflight_{}_{}",
            std::process::id(),
            rand::random::<u64>()
        ));
        fs::create_dir_all(&path)
            .with_context(|| format!("create install preflight root {}", path.display()))?;
        Ok(Self(path))
    }
}

impl Drop for StagingRoot {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn verify_staged_install_layout(bundles: &[InstallBundle]) -> Result<()> {
    let staging = StagingRoot::create()?;
    for bundle in bundles {
        let staged = bundle.spec.installed_path(&staging.0);
        let parent = staged
            .parent()
            .with_context(|| format!("staged destination has no parent: {}", staged.display()))?;
        fs::create_dir_all(parent)
            .with_context(|| format!("create staged install directory {}", parent.display()))?;
        let status = Command::new("ditto")
            .arg(&bundle.source)
            .arg(&staged)
            .status()
            .with_context(|| format!("stage {}", bundle.spec.label()))?;
        if !status.success() {
            bail!(
                "ditto staging failed for {} with status {status}",
                bundle.spec.label()
            );
        }
        let (_, staged_binary) =
            ship_bundle::verify_binary_copy(&bundle.source, &staged, &bundle.spec)?;
        verify_universal(&staged_binary)?;
        verify_signed(&staged)?;
        ship_bundle::verify_bundle_contract(&staged, &bundle.spec)?;
    }
    eprintln!(
        "[install] exact role-first staging layout verified before user/system-level changes"
    );
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
#[path = "install/tests.rs"]
mod tests;
