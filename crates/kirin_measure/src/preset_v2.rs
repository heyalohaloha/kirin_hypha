//! preset v2.0 reader — G-77-01 / G-77-03.
//!
//! `plugin_data/{project_hash}/preset/{bounce_id}.json` に書き出したものを、
//! Hypha の LED / GUI (T-E/T-F) が消費する経路。
//!
//! # 保護境界 (§8bis.3)
//! - v1.1 reader (`preset.rs`) は**不変更**。
//! - 音声信号処理経路 (Audio/Measure/IO Thread / 計測エンジン / 書込 / 排他
//!   / record_signal / close / identity.json) は触らない。
//! - 本モジュールは純粋な FS I/O + HMAC 検証 + スキーマデシリアライズのみ。
//!
//! # R-28 沈黙
//! `verify_preset_v2_checksum` / `verify_preset_v2` は不合格時に `VerifyErrorV2`
//! を返し、`scan_valid_presets_v2` は呼び出し元に沈黙で skip を通知する。
//!
//! # HMAC
//! トップレベル `"hmac_checksum"` フィールド。`hmac_checksum=""` で JSON 化した
//! 全バイトに対する HMAC-SHA256 hex。鍵は [`crate::identity`] と同じ（v1.1 と同値

use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use std::fs;
use std::path::{Path, PathBuf};

use crate::preset::PRESET_SUBDIR;

type HmacSha256 = Hmac<Sha256>;

// ── Schema v2.0 ──────────────────────────────────────────────────────────────

/// proposals schema v2.0 ルート。
///
/// フィールド順は Lens proposals.js の JSON 生成順と一致させる。順序が崩れると
/// HMAC が一致しないため、追加する場合は Lens 側と同期して調整する。
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PresetFileV2 {
    pub schema_version: String,
    pub installation_id: String,
    /// Hypha plugin_data の session_id (compact_wall_clock)。
    pub session_id: String,
    /// Lens / OS 側が proposals 生成時に発行した UUID v4。
    pub bounce_id: String,
    pub work_id: Option<String>,
    /// ISO 8601 UTC。
    pub generated_at: String,
    pub cards: Vec<Card>,
    pub section_boundaries: Vec<SectionBoundary>,
    pub summary: Summary,
    pub hmac_checksum: String,
}

impl PresetFileV2 {
    pub const SCHEMA_VERSION: &'static str = "2.0";
}

/// ADVISOR カード 1 件。
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Card {
    /// `"observation"` | `"suggestion"`
    pub card_type: String,
    pub slot: String,
    /// `section_context.schema.json` の confidence enum。
    /// `"MEASURED"` | `"INFERRED"` | `"ESTIMATED"` | `null`。
    pub confidence: Option<String>,
    /// R-22 テンプレート ID（自由文禁止）。
    pub message_key: String,
    /// テンプレートに埋め込む数値・文字列。デフォルト `{}`。
    #[serde(default = "default_empty_object")]
    pub message_params: serde_json::Value,
    /// `"info"` | `"suggestion"` | `"warning"`
    pub severity: String,
    pub section_ref: Option<String>,
}

fn default_empty_object() -> serde_json::Value {
    serde_json::Value::Object(serde_json::Map::new())
}

/// section_boundaries[] 1 件。`section_context.schema.json` の sections[] 7
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SectionBoundary {
    pub section_id: String,
    pub label: String,
    pub start_sec: f64,
    pub end_sec: f64,
    pub duration_sec: f64,
    pub metrics: Option<serde_json::Value>,
    pub deviation_from_track: Option<serde_json::Value>,
}

/// R-26 Gate 通過件数の集計。
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct Summary {
    pub total_generated: u32,
    pub silenced_by_gate: u32,
    pub delivered: u32,
    pub observations: u32,
    pub suggestions: u32,
}

// ── 検証 ─────────────────────────────────────────────────────────────────────

/// v2.0 検証失敗理由。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum VerifyErrorV2 {
    /// HMAC-SHA256 不一致。
    ChecksumMismatch,
    /// `installation_id` 不一致（他機生成の proposals）。
    InstallationIdMismatch,
    /// `schema_version` が `"2.0"` ではない。
    SchemaInvalid(String),
}

impl std::fmt::Display for VerifyErrorV2 {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::ChecksumMismatch => write!(f, "hmac_checksum mismatch"),
            Self::InstallationIdMismatch => write!(f, "installation_id mismatch"),
            Self::SchemaInvalid(s) => write!(f, "schema invalid: {s}"),
        }
    }
}

impl std::error::Error for VerifyErrorV2 {}

/// v2.0 preset を検証（schema_version + HMAC + installation_id）。
pub fn verify_preset_v2(
    preset: &PresetFileV2,
    own_installation_id: &str,
) -> Result<(), VerifyErrorV2> {
    if preset.schema_version != PresetFileV2::SCHEMA_VERSION {
        return Err(VerifyErrorV2::SchemaInvalid(format!(
            "schema_version={}",
            preset.schema_version
        )));
    }
    let expected = compute_preset_v2_checksum(preset);
    if !constant_time_eq(expected.as_bytes(), preset.hmac_checksum.as_bytes()) {
        return Err(VerifyErrorV2::ChecksumMismatch);
    }
    if preset.installation_id != own_installation_id {
        return Err(VerifyErrorV2::InstallationIdMismatch);
    }
    Ok(())
}

/// `hmac_checksum=""` にした JSON 全バイトに対する HMAC-SHA256 hex。
pub fn compute_preset_v2_checksum(preset: &PresetFileV2) -> String {
    let mut clone = preset.clone();
    clone.hmac_checksum = String::new();
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

// 54 bytes ASCII printable。v1.1 (preset.rs:153) と同値（同一 Hypha 鍵）。
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

// ── Section lookup (T-F) ─────────────────────────────────────────────────────

/// Return the label of the section containing `t_sec`, or `None` when `t_sec`
/// does not fall inside any half-open `[start_sec, end_sec)` interval.
///
/// Linear scan — proposals rarely exceed a few dozen boundaries, so binary
/// search is not worth the complexity.  Defensive behaviour:
///   - non-finite `t_sec` → `None` (playhead unavailable)
///   - malformed boundary (`start >= end` or NaN) → skipped
///   - overlapping boundaries → the first match wins (proposal generator
///     responsibility to keep ranges disjoint)
pub fn lookup_section_label(boundaries: &[SectionBoundary], t_sec: f64) -> Option<&str> {
    if !t_sec.is_finite() {
        return None;
    }
    for b in boundaries {
        if !b.start_sec.is_finite() || !b.end_sec.is_finite() {
            continue;
        }
        if b.start_sec >= b.end_sec {
            continue;
        }
        if t_sec >= b.start_sec && t_sec < b.end_sec {
            return Some(b.label.as_str());
        }
    }
    None
}

// ── FS 走査 ──────────────────────────────────────────────────────────────────

/// `{base}/{project_hash}/preset/` 配下の v2.0 proposals を検証通過分のみ返す。
/// v1.1 と物理パスを共有するため、schema_version を peek で判定し v2.0 以外は
/// この関数では skip する。
pub fn scan_valid_presets_v2(
    base: &Path,
    project_hash: &str,
    own_installation_id: &str,
) -> Vec<PresetFileV2> {
    let dir = preset_dir_v2(base, project_hash);
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
        let Ok(preset): Result<PresetFileV2, _> = serde_json::from_slice(&bytes) else {
            continue;
        };
        if preset.schema_version != PresetFileV2::SCHEMA_VERSION {
            continue;
        }
        if verify_preset_v2(&preset, own_installation_id).is_ok() {
            out.push(preset);
        }
    }
    out
}

/// v1.1 と同じパス規約。
pub fn preset_dir_v2(base: &Path, project_hash: &str) -> PathBuf {
    // B-128 (G-115-370): within-base wall（restore project_uuid 由来の preset path）。
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "preset_v2.preset_dir_v2.project_hash",
    );
    base.join(&*ph).join(PRESET_SUBDIR)
}

fn is_preset_json(path: &Path) -> bool {
    let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
        return false;
    };
    name.ends_with(".json") && !name.ends_with(".tmp")
}

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
#[path = "preset_v2_tests.rs"]
mod tests;
