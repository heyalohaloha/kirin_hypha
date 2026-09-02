//! preset/ 読込 — OS 側が生成するプリセット提案の検証と R-28 沈黙ゲート。
//!
//!
//! # ディレクトリ
//! ```text
//! plugin_data/{project_hash}/preset/{bounce_id}.json
//! ```
//!
//! # R-28 沈黙ゲート（G-50-04 / G-50-05 / G-50-36 改善I）
//! 以下 **2 段 gate 両方通過** したファイルのみ受理:
//! 2. `installation_id` 一致（5 層照合 層 1）
//!
//! いずれか NG → 無視・無言（LED 点灯させない）。
//!
//! # 解消通知（改善 I）
//! 前回 preset 指摘の閾値超過が最新 frames[] で解消されていれば「前回の指摘は
//! 解消されました」1 行表示。通常状態（提案なし）では何も表示しない（R-28）。
//!
//! # Sense 分岐
//! 本モジュールは license 非依存。caller（T-6 Plugin 統合）が
//! `crate::license::can_read_preset` で事前ゲートする。
//!
//! # T-5 のスコープ
//! 純粋な FS I/O + 検証 + 解消判定ロジックのみ。
//! 1 秒間隔ポーリング / LED 駆動 / 詳細 GUI は T-6 統合で組み立てる。

use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use std::fs;
use std::path::{Path, PathBuf};

type HmacSha256 = Hmac<Sha256>;

/// preset 格納サブディレクトリ名。
pub const PRESET_SUBDIR: &str = "preset";

// ── スキーマ v1.1 ────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PresetFile {
    pub schema_version: String,
    /// JSON キー `type`。Rust 予約語回避のため rename。
    #[serde(rename = "type")]
    pub type_tag: String,
    pub installation_id: String,
    pub bounce_id: String,
    pub checksum: String,
    pub regions: Vec<Region>,
}

impl PresetFile {
    pub const SCHEMA_VERSION: &'static str = "1.1";
    pub const TYPE_TAG: &'static str = "kirin_plugin_preset";
}

/// 指摘領域 1 件。
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Region {
    pub start_sec: f64,
    pub end_sec: f64,
    pub metric: String,
    /// 該当 Bark 帯域（PSB 系の場合）。metric によっては null。
    pub bark_band: Option<u32>,
    pub value: f64,
    pub delta: f64,
    pub threshold: f64,
    /// `"absolute"` | `"delta"`
    pub threshold_type: String,
    /// `"severity_L3"` | `"starting_points"` | `"measured"`
    pub threshold_source: String,
    /// `"ESTIMATED"` | `"MEASURED"`
    pub confidence: String,
}

// ── 検証 ─────────────────────────────────────────────────────────────────────

/// preset 検証失敗理由。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum VerifyError {
    /// HMAC-SHA256 不一致（改ざん疑い）。
    ChecksumMismatch,
    /// `installation_id` が自機と異なる（他機で生成された preset）。
    InstallationIdMismatch,
    /// `schema_version` が未対応 or `type` が想定外。
    SchemaInvalid(String),
}

impl std::fmt::Display for VerifyError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::ChecksumMismatch => write!(f, "checksum mismatch"),
            Self::InstallationIdMismatch => write!(f, "installation_id mismatch"),
            Self::SchemaInvalid(s) => write!(f, "schema invalid: {s}"),
        }
    }
}

impl std::error::Error for VerifyError {}

/// preset を検証（HMAC + installation_id + スキーマ）。
///
/// Phase 1 の HMAC 鍵は [`crate::identity`] と共通（ビルド時埋め込み）。
/// OS 側も同じ鍵で署名することで検証が通る。
pub fn verify_preset(preset: &PresetFile, own_installation_id: &str) -> Result<(), VerifyError> {
    // 1. スキーマ版チェック
    if preset.schema_version != PresetFile::SCHEMA_VERSION {
        return Err(VerifyError::SchemaInvalid(format!(
            "schema_version={}",
            preset.schema_version
        )));
    }
    if preset.type_tag != PresetFile::TYPE_TAG {
        return Err(VerifyError::SchemaInvalid(format!(
            "type={}",
            preset.type_tag
        )));
    }
    // 2. HMAC-SHA256 検証
    let expected = compute_preset_checksum(preset);
    if !constant_time_eq(expected.as_bytes(), preset.checksum.as_bytes()) {
        return Err(VerifyError::ChecksumMismatch);
    }
    // 3. installation_id 一致（5 層照合 層 1）
    if preset.installation_id != own_installation_id {
        return Err(VerifyError::InstallationIdMismatch);
    }
    Ok(())
}

/// `checksum=""` にした状態の JSON に対する HMAC-SHA256 hex。
pub fn compute_preset_checksum(preset: &PresetFile) -> String {
    let mut clone = preset.clone();
    clone.checksum = String::new();
    let bytes = serde_json::to_vec(&clone).expect("serializable");
    let mut mac =
        HmacSha256::new_from_slice(hmac_key()).expect("HMAC-SHA256 accepts any key length");
    mac.update(&bytes);
    hex::encode(mac.finalize().into_bytes())
}

fn hmac_key() -> &'static [u8] {
    match option_env!("KIRIN_HYPHA_HMAC_KEY") {
        Some(k) => k.as_bytes(),
        None => DEFAULT_HMAC_KEY,
    }
}

const DEFAULT_HMAC_KEY: &[u8] = b"kirin-hypha-phase1.0-hmac-key-deterrent-level-20260417";

fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}

// ── FS 走査 ──────────────────────────────────────────────────────────────────

/// `{base}/{project_hash}/preset/` を返す。
pub fn preset_dir(base: &Path, project_hash: &str) -> PathBuf {
    // B-128 (G-115-370): within-base wall（restore project_uuid 由来の preset path）。
    let ph =
        crate::path_identity::guard_path_component(project_hash, "preset.preset_dir.project_hash");
    base.join(&*ph).join(PRESET_SUBDIR)
}

/// preset/ 配下の `.json` を走査し、検証通過分のみ返す。
///
/// R-28 沈黙: 不合格・破損ファイルは無言で skip。
/// 呼び出し側は `Vec::is_empty()` で LED ON/OFF を判定する。
///
/// 戻り値順は `file_name` 昇順（安定）。
pub fn scan_valid_presets(
    base: &Path,
    project_hash: &str,
    own_installation_id: &str,
) -> Vec<PresetFile> {
    let dir = preset_dir(base, project_hash);
    let entries = match fs::read_dir(&dir) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };
    let mut paths: Vec<PathBuf> = entries
        .flatten()
        .map(|e| e.path())
        .filter(|p| is_preset_json(p))
        .collect();
    paths.sort();

    let mut out = Vec::new();
    for path in paths {
        let Ok(bytes) = fs::read(&path) else { continue };
        let Ok(preset): Result<PresetFile, _> = serde_json::from_slice(&bytes) else {
            continue;
        };
        if verify_preset(&preset, own_installation_id).is_ok() {
            out.push(preset);
        }
    }
    out
}

fn is_preset_json(path: &Path) -> bool {
    let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
        return false;
    };
    name.ends_with(".json") && !name.ends_with(".tmp")
}

// ── 解消判定（G-50-36 改善 I）────────────────────────────────────────────────

/// region の閾値超過が `latest_value` で解消されたか判定。
///
/// - `threshold_type = "absolute"`: `latest_value < threshold` で解消
/// - `threshold_type = "delta"`:    `|latest_value| < threshold` で解消
/// - 未知 type: false（安全側 — 解消主張しない）
///
/// `latest_value` は frames[] から caller が集約したその metric の最新値
/// （絶対値 or delta）。
pub fn region_resolved(region: &Region, latest_value: f64) -> bool {
    if !latest_value.is_finite() {
        return false;
    }
    match region.threshold_type.as_str() {
        "absolute" => latest_value < region.threshold,
        "delta" => latest_value.abs() < region.threshold,
        _ => false,
    }
}

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
#[path = "preset_tests.rs"]
mod tests;
