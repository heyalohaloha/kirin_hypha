//! B-109 / G-115-345（案X）: 出荷 egui VST3 の Info.plist 版番号を crate 版へ焼く（版統一 1.1.0）。
//! B-213: DAW/スキャナが bundle display metadata を読む場合でも PRE/POST が先頭で判別できる
//! よう、CFBundleDisplayName / CFBundleName も role-first に stamp する。
//!
//! 背景（不可避な tooling 追加の根拠）: `nih_plug_xtask` の bundle は Info.plist の
//! `CFBundleShortVersionString` / `CFBundleVersion` を **1.0.0 固定**で出力する
//! （nih_plug_xtask/src/lib.rs:781-784 / `bundler.toml` に version field 無し = 上流未実装の TODO）。
//! そのため crate(Cargo) 版を上げても egui VST3 のバンドル版は 1.0.0 のまま。本コマンドは
//! `bundle-universal` の **後**に走らせ、egui VST3 の Info.plist 版を crate 版に一致させる。
//! JUCE AU は CMake `project(... VERSION)` で整合し、AU plugin list 名は `AudioComponents:0:name`
//! が role-first なので対象外。計測・プラグイン挙動コードは無変更（tooling のみ / B-109/B-213）。
//!
//! 使い方: `cargo run -p xtask -- stamp-egui-version`
//!   （`crates/hypha_pre/Cargo.toml` の `version` を単一ソースに、PRE/POST 両 VST3 を stamp）。

use anyhow::{bail, Context, Result};
use std::path::Path;
use std::process::Command;

/// 出荷 egui VST3 バンドル名（`bundler.toml` の `name`）とDAW表示補助名。
struct BundleStamp {
    file_name: &'static str,
    display_name: &'static str,
}

const BUNDLES: &[BundleStamp] = &[
    BundleStamp {
        file_name: "Kirin Hypha PRE",
        display_name: "PRE Kirin Hypha",
    },
    BundleStamp {
        file_name: "Kirin Hypha POST",
        display_name: "POST Kirin Hypha",
    },
];

pub fn run(_args: Vec<String>) -> Result<()> {
    let version = read_egui_crate_version()?;
    eprintln!("[stamp-egui-version] egui crate version = {version}");
    for bundle in BUNDLES {
        let plist = format!(
            "target/bundled/{}.vst3/Contents/Info.plist",
            bundle.file_name
        );
        if !Path::new(&plist).exists() {
            bail!(
                "{plist} not found — run `cargo run -p xtask -- bundle-universal hypha_pre --release` and `cargo run -p xtask -- bundle-universal hypha_post --release` first"
            );
        }
        set_plist_string(&plist, "CFBundleShortVersionString", &version)?;
        set_plist_string(&plist, "CFBundleVersion", &version)?;
        set_plist_string(&plist, "CFBundleDisplayName", bundle.display_name)?;
        set_plist_string(&plist, "CFBundleName", bundle.display_name)?;
        eprintln!(
            "[stamp-egui-version] {}.vst3 → {version}, display {}",
            bundle.file_name, bundle.display_name
        );
    }
    Ok(())
}

/// `crates/hypha_pre/Cargo.toml` の `[package] version` を読む（hypha_post と同版前提・単一ソース）。
fn read_egui_crate_version() -> Result<String> {
    let toml = std::fs::read_to_string("crates/hypha_pre/Cargo.toml")
        .context("read crates/hypha_pre/Cargo.toml")?;
    for line in toml.lines() {
        let t = line.trim();
        // `[package]` セクション先頭の `version = "X"`（依存の `name = { version = .. }` は行頭が name）。
        if let Some(rest) = t.strip_prefix("version") {
            if let Some(v) = rest.split('"').nth(1) {
                return Ok(v.to_string());
            }
        }
    }
    bail!("version not found in crates/hypha_pre/Cargo.toml")
}

/// `PlistBuddy -c "Set :KEY VALUE"`（macOS 標準・既存キーの上書き）。
fn set_plist_string(plist: &str, key: &str, value: &str) -> Result<()> {
    let status = Command::new("/usr/libexec/PlistBuddy")
        .args(["-c", &format!("Set :{key} {value}"), plist])
        .status()
        .with_context(|| format!("run PlistBuddy on {plist}"))?;
    if !status.success() {
        bail!("PlistBuddy Set :{key} failed on {plist}");
    }
    Ok(())
}
