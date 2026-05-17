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
    let mut mac = HmacSha256::new_from_slice(hmac_key())
        .expect("HMAC-SHA256 accepts any key length");
    mac.update(&bytes);
    hex::encode(mac.finalize().into_bytes())
}

fn hmac_key() -> &'static [u8] {
    match option_env!("KIRIN_HYPHA_HMAC_KEY") {
        Some(k) => k.as_bytes(),
        None => DEFAULT_HMAC_KEY,
    }
}

const DEFAULT_HMAC_KEY: &[u8] =
    b"kirin-hypha-phase1.0-hmac-key-deterrent-level-20260417";

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
    base.join(project_hash).join(PRESET_SUBDIR)
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
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_preset_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn sample_region() -> Region {
        Region {
            start_sec: 151.4,
            end_sec: 151.6,
            metric: "sharpness".to_string(),
            bark_band: None,
            value: 3.2,
            delta: 0.6,
            threshold: 2.5,
            threshold_type: "absolute".to_string(),
            threshold_source: "severity_L3".to_string(),
            confidence: "ESTIMATED".to_string(),
        }
    }

    /// checksum が空のテンプレ。compute_preset_checksum で後から埋める。
    fn new_preset_template(installation_id: &str) -> PresetFile {
        PresetFile {
            schema_version: PresetFile::SCHEMA_VERSION.to_string(),
            type_tag: PresetFile::TYPE_TAG.to_string(),
            installation_id: installation_id.to_string(),
            bounce_id: "bounce-xyz".to_string(),
            checksum: String::new(),
            regions: vec![sample_region()],
        }
    }

    /// 正規 preset（HMAC 埋込済み）を作成。
    fn signed_preset(installation_id: &str) -> PresetFile {
        let mut p = new_preset_template(installation_id);
        p.checksum = compute_preset_checksum(&p);
        p
    }

    fn write_preset_file(base: &Path, ph: &str, filename: &str, preset: &PresetFile) -> PathBuf {
        let dir = preset_dir(base, ph);
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join(filename);
        fs::write(&path, serde_json::to_vec(preset).unwrap()).unwrap();
        path
    }

    // ── スキーマ ────────────────────────────────────────────

    #[test]
    fn preset_roundtrip_with_type_field_rename() {
        let p = signed_preset("iid-1");
        let json = serde_json::to_string(&p).unwrap();
        // JSON キーは `type`（rename 確認）
        assert!(
            json.contains(r#""type":"kirin_plugin_preset""#),
            "type key must be rendered: {json}"
        );
        let back: PresetFile = serde_json::from_str(&json).unwrap();
        assert_eq!(back, p);
    }

    #[test]
    fn region_serializes_bark_band_null() {
        let r = sample_region(); // bark_band = None
        let json = serde_json::to_string(&r).unwrap();
        assert!(json.contains(r#""bark_band":null"#), "null: {json}");
    }

    // ── 検証 ────────────────────────────────────────────────

    #[test]
    fn valid_preset_verifies() {
        let p = signed_preset("iid-own");
        assert_eq!(verify_preset(&p, "iid-own"), Ok(()));
    }

    #[test]
    fn installation_id_mismatch_rejected() {
        let p = signed_preset("iid-other");
        assert_eq!(
            verify_preset(&p, "iid-own"),
            Err(VerifyError::InstallationIdMismatch)
        );
    }

    #[test]
    fn tampered_checksum_rejected() {
        let mut p = signed_preset("iid-own");
        p.checksum = "0".repeat(64);
        assert_eq!(
            verify_preset(&p, "iid-own"),
            Err(VerifyError::ChecksumMismatch)
        );
    }

    #[test]
    fn tampered_region_value_fails_checksum() {
        let mut p = signed_preset("iid-own");
        p.regions[0].value = 999.0; // 改ざん
        assert_eq!(
            verify_preset(&p, "iid-own"),
            Err(VerifyError::ChecksumMismatch)
        );
    }

    #[test]
    fn wrong_schema_version_rejected() {
        let mut p = new_preset_template("iid-own");
        p.schema_version = "2.0".to_string();
        p.checksum = compute_preset_checksum(&p);
        match verify_preset(&p, "iid-own") {
            Err(VerifyError::SchemaInvalid(msg)) => {
                assert!(msg.contains("schema_version"), "msg: {msg}")
            }
            other => panic!("expected SchemaInvalid, got {other:?}"),
        }
    }

    #[test]
    fn wrong_type_tag_rejected() {
        let mut p = new_preset_template("iid-own");
        p.type_tag = "other_type".to_string();
        p.checksum = compute_preset_checksum(&p);
        match verify_preset(&p, "iid-own") {
            Err(VerifyError::SchemaInvalid(msg)) => assert!(msg.contains("type"), "msg: {msg}"),
            other => panic!("expected SchemaInvalid, got {other:?}"),
        }
    }

    // ── FS 走査（R-28 沈黙ゲート）────────────────────────────

    #[test]
    fn scan_empty_dir_returns_empty_vec() {
        let base = isolated_dir();
        let v = scan_valid_presets(&base, "ph", "iid-own");
        assert!(v.is_empty());
    }

    #[test]
    fn scan_returns_only_verified_presets() {
        let base = isolated_dir();
        // 有効
        let good = signed_preset("iid-own");
        write_preset_file(&base, "ph", "a.json", &good);
        // HMAC NG
        let mut tampered = signed_preset("iid-own");
        tampered.regions[0].value = 99.0;
        write_preset_file(&base, "ph", "b.json", &tampered);
        // installation_id NG
        let foreign = signed_preset("iid-other-machine");
        write_preset_file(&base, "ph", "c.json", &foreign);

        let v = scan_valid_presets(&base, "ph", "iid-own");
        assert_eq!(v.len(), 1, "only the good preset passes both gates");
        assert_eq!(v[0], good);
    }

    #[test]
    fn scan_ignores_corrupt_json_silently() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("broken.json"), b"{{not json").unwrap();

        let good = signed_preset("iid-own");
        write_preset_file(&base, "ph", "ok.json", &good);

        let v = scan_valid_presets(&base, "ph", "iid-own");
        assert_eq!(v.len(), 1);
    }

    #[test]
    fn scan_ignores_tmp_and_non_json_files() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("notes.txt"), b"hi").unwrap();
        fs::write(dir.join("in_progress.json.tmp"), b"{}").unwrap();
        let good = signed_preset("iid-own");
        write_preset_file(&base, "ph", "ok.json", &good);

        let v = scan_valid_presets(&base, "ph", "iid-own");
        assert_eq!(v.len(), 1);
    }

    #[test]
    fn scan_returns_sorted_order() {
        let base = isolated_dir();
        let g1 = signed_preset("iid-own");
        let g2 = signed_preset("iid-own");
        write_preset_file(&base, "ph", "z.json", &g1);
        write_preset_file(&base, "ph", "a.json", &g2);
        let v = scan_valid_presets(&base, "ph", "iid-own");
        // ファイル名 "a.json" < "z.json" → a が先
        assert_eq!(v.len(), 2);
        // 両方内容同じなので identity では比較不能。順序を作るため a の bounce_id を変更
        // → 下記別テストで詳細検証
    }

    #[test]
    fn scan_sort_stable_by_filename() {
        let base = isolated_dir();
        let mut p1 = new_preset_template("iid-own");
        p1.bounce_id = "FIRST".to_string();
        p1.checksum = compute_preset_checksum(&p1);
        let mut p2 = new_preset_template("iid-own");
        p2.bounce_id = "SECOND".to_string();
        p2.checksum = compute_preset_checksum(&p2);
        write_preset_file(&base, "ph", "b.json", &p2);
        write_preset_file(&base, "ph", "a.json", &p1);
        let v = scan_valid_presets(&base, "ph", "iid-own");
        assert_eq!(v[0].bounce_id, "FIRST");
        assert_eq!(v[1].bounce_id, "SECOND");
    }

    #[test]
    fn scan_missing_dir_is_silent() {
        // project_hash ディレクトリ自体が存在しないケース（新規プロジェクト等）
        let base = isolated_dir();
        let v = scan_valid_presets(&base, "no_such_ph", "iid-own");
        assert!(v.is_empty());
    }

    // ── 解消判定（改善 I）────────────────────────────────────

    #[test]
    fn region_resolved_absolute_below_threshold() {
        let mut r = sample_region();
        r.threshold_type = "absolute".into();
        r.threshold = 2.5;
        // value < threshold → 解消
        assert!(region_resolved(&r, 2.4));
        // value == threshold → 未解消（厳密 <）
        assert!(!region_resolved(&r, 2.5));
        // value > threshold → 未解消
        assert!(!region_resolved(&r, 3.0));
    }

    #[test]
    fn region_resolved_delta_absolute_value() {
        let mut r = sample_region();
        r.threshold_type = "delta".into();
        r.threshold = 1.0;
        assert!(region_resolved(&r, 0.5));
        assert!(region_resolved(&r, -0.5));
        assert!(!region_resolved(&r, 1.0));
        assert!(!region_resolved(&r, -1.5));
    }

    #[test]
    fn region_resolved_unknown_type_never_claims_resolution() {
        let mut r = sample_region();
        r.threshold_type = "something_new".into();
        r.threshold = 1.0;
        assert!(!region_resolved(&r, 0.0));
    }

    #[test]
    fn region_resolved_nan_or_inf_returns_false() {
        let r = sample_region();
        assert!(!region_resolved(&r, f64::NAN));
        assert!(!region_resolved(&r, f64::INFINITY));
        assert!(!region_resolved(&r, f64::NEG_INFINITY));
    }

    // ── 統合 ────────────────────────────────────────────────

    #[test]
    fn full_silence_gate_all_invalid_means_led_off() {
        // R-28: 全部不合格 → 空 Vec → LED 消灯判定
        let base = isolated_dir();
        let foreign = signed_preset("other-machine");
        write_preset_file(&base, "ph", "a.json", &foreign);
        let mut tampered = signed_preset("iid-own");
        tampered.checksum = "0".repeat(64);
        write_preset_file(&base, "ph", "b.json", &tampered);
        fs::write(
            preset_dir(&base, "ph").join("c.json"),
            b"garbage",
        )
        .unwrap();

        let v = scan_valid_presets(&base, "ph", "iid-own");
        assert!(v.is_empty(), "silence gate must reject all");
    }
}
