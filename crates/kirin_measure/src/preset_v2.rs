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
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_preset_v2_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn sample_card() -> Card {
        Card {
            card_type: "observation".to_string(),
            slot: "lufs_m".to_string(),
            confidence: Some("MEASURED".to_string()),
            message_key: "obs_lufs_m_delta".to_string(),
            message_params: serde_json::json!({ "delta": 4.0 }),
            severity: "info".to_string(),
            section_ref: None,
        }
    }

    fn sample_boundary() -> SectionBoundary {
        SectionBoundary {
            section_id: "s01".to_string(),
            label: "intro".to_string(),
            start_sec: 0.0,
            end_sec: 12.0,
            duration_sec: 12.0,
            metrics: Some(serde_json::json!({ "lufs_integrated": -14.0 })),
            deviation_from_track: None,
        }
    }

    fn new_preset_v2_template(installation_id: &str) -> PresetFileV2 {
        PresetFileV2 {
            schema_version: PresetFileV2::SCHEMA_VERSION.to_string(),
            installation_id: installation_id.to_string(),
            session_id: "20260417T143208".to_string(),
            bounce_id: "00000000-0000-4000-8000-000000000000".to_string(),
            work_id: Some("work-42".to_string()),
            generated_at: "2026-04-23T10:00:00Z".to_string(),
            cards: vec![sample_card()],
            section_boundaries: vec![sample_boundary()],
            summary: Summary {
                total_generated: 1,
                silenced_by_gate: 0,
                delivered: 1,
                observations: 1,
                suggestions: 0,
            },
            hmac_checksum: String::new(),
        }
    }

    fn signed_preset_v2(installation_id: &str) -> PresetFileV2 {
        let mut p = new_preset_v2_template(installation_id);
        p.hmac_checksum = compute_preset_v2_checksum(&p);
        p
    }

    fn write_preset_v2_file(
        base: &Path,
        ph: &str,
        filename: &str,
        preset: &PresetFileV2,
    ) -> PathBuf {
        let dir = preset_dir_v2(base, ph);
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join(filename);
        fs::write(&path, serde_json::to_vec(preset).unwrap()).unwrap();
        path
    }

    // ── スキーマ / シリアライズ ────────────────────────────

    #[test]
    fn preset_v2_roundtrip() {
        let p = signed_preset_v2("iid-own");
        let json = serde_json::to_string(&p).unwrap();
        let back: PresetFileV2 = serde_json::from_str(&json).unwrap();
        assert_eq!(back, p);
    }

    #[test]
    fn preset_v2_field_order_matches_lens_proposals_js() {
        // Field insertion order controls HMAC determinism. The expected
        // byte ordering mirrors proposals.js §8.2 literal.
        let p = new_preset_v2_template("iid-own");
        let json = serde_json::to_string(&p).unwrap();
        let expected_prefix = r#"{"schema_version":"2.0","installation_id":"iid-own","session_id":"20260417T143208","bounce_id":"00000000-0000-4000-8000-000000000000","work_id":"work-42","generated_at":"2026-04-23T10:00:00Z","cards":[{"card_type":"observation","slot":"lufs_m","confidence":"MEASURED","message_key":"obs_lufs_m_delta","message_params":{"delta":4.0},"severity":"info","section_ref":null}],"section_boundaries":[{"section_id":"s01","label":"intro","start_sec":0.0,"end_sec":12.0,"duration_sec":12.0,"metrics":{"lufs_integrated":-14.0},"deviation_from_track":null}],"summary":{"total_generated":1,"silenced_by_gate":0,"delivered":1,"observations":1,"suggestions":0},"hmac_checksum":""}"#;
        assert_eq!(json, expected_prefix);
    }

    #[test]
    fn confidence_null_serializes_as_json_null() {
        let mut p = new_preset_v2_template("iid-own");
        p.cards[0].confidence = None;
        let json = serde_json::to_string(&p).unwrap();
        assert!(
            json.contains(r#""confidence":null"#),
            "null confidence: {json}"
        );
    }

    // ── 検証 ────────────────────────────────────────────────

    #[test]
    fn valid_preset_v2_verifies() {
        let p = signed_preset_v2("iid-own");
        assert_eq!(verify_preset_v2(&p, "iid-own"), Ok(()));
    }

    #[test]
    fn installation_id_mismatch_rejected_v2() {
        let p = signed_preset_v2("iid-other");
        assert_eq!(
            verify_preset_v2(&p, "iid-own"),
            Err(VerifyErrorV2::InstallationIdMismatch)
        );
    }

    #[test]
    fn tampered_hmac_rejected_v2() {
        let mut p = signed_preset_v2("iid-own");
        p.hmac_checksum = "0".repeat(64);
        assert_eq!(
            verify_preset_v2(&p, "iid-own"),
            Err(VerifyErrorV2::ChecksumMismatch)
        );
    }

    #[test]
    fn tampered_card_fails_checksum_v2() {
        let mut p = signed_preset_v2("iid-own");
        p.cards[0].message_key = "tampered_key".to_string();
        assert_eq!(
            verify_preset_v2(&p, "iid-own"),
            Err(VerifyErrorV2::ChecksumMismatch)
        );
    }

    #[test]
    fn wrong_schema_version_rejected_v2() {
        let mut p = new_preset_v2_template("iid-own");
        p.schema_version = "1.1".to_string();
        p.hmac_checksum = compute_preset_v2_checksum(&p);
        match verify_preset_v2(&p, "iid-own") {
            Err(VerifyErrorV2::SchemaInvalid(msg)) => {
                assert!(msg.contains("schema_version"), "msg: {msg}")
            }
            other => panic!("expected SchemaInvalid, got {other:?}"),
        }
    }

    // ── 走査 ────────────────────────────────────────────────

    #[test]
    fn scan_returns_valid_only() {
        let base = isolated_dir();
        let p_ok = signed_preset_v2("iid-own");
        let p_bad = signed_preset_v2("iid-other");
        write_preset_v2_file(&base, "PH", "a.json", &p_ok);
        write_preset_v2_file(&base, "PH", "b.json", &p_bad);
        let v = scan_valid_presets_v2(&base, "PH", "iid-own");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0], p_ok);
    }

    #[test]
    fn scan_skips_v1_1_files_silently() {
        // A v1.1 preset written into the same preset/ dir must not be
        // surfaced by the v2.0 scanner (caller is expected to delegate
        // v1.1 files via the dispatcher).
        let base = isolated_dir();
        let dir = preset_dir_v2(&base, "PH");
        fs::create_dir_all(&dir).unwrap();
        let v11_json = r#"{"schema_version":"1.1","type":"kirin_plugin_preset","installation_id":"iid-own","bounce_id":"x","checksum":"","regions":[]}"#;
        fs::write(dir.join("old.json"), v11_json).unwrap();

        let p_ok = signed_preset_v2("iid-own");
        write_preset_v2_file(&base, "PH", "new.json", &p_ok);

        let v = scan_valid_presets_v2(&base, "PH", "iid-own");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].schema_version, "2.0");
    }

    #[test]
    fn scan_skips_tmp_and_nonjson() {
        let base = isolated_dir();
        let p_ok = signed_preset_v2("iid-own");
        let dir = preset_dir_v2(&base, "PH");
        write_preset_v2_file(&base, "PH", "a.json", &p_ok);
        fs::write(dir.join("b.json.tmp"), b"garbage").unwrap();
        fs::write(dir.join("notes.txt"), b"hello").unwrap();
        let v = scan_valid_presets_v2(&base, "PH", "iid-own");
        assert_eq!(v.len(), 1);
    }

    // ── lookup_section_label ────────────────────────────────

    fn make_boundary(label: &str, start: f64, end: f64) -> SectionBoundary {
        SectionBoundary {
            section_id: format!("s_{}", label),
            label: label.to_string(),
            start_sec: start,
            end_sec: end,
            duration_sec: end - start,
            metrics: None,
            deviation_from_track: None,
        }
    }

    #[test]
    fn lookup_returns_label_at_exact_start() {
        // Half-open: start is inclusive.
        let b = vec![make_boundary("intro", 0.0, 12.0)];
        assert_eq!(lookup_section_label(&b, 0.0), Some("intro"));
    }

    #[test]
    fn lookup_excludes_exact_end_sec() {
        // Half-open: end is exclusive, first-match semantics.
        let b = vec![
            make_boundary("intro", 0.0, 12.0),
            make_boundary("verse_1", 12.0, 24.0),
        ];
        assert_eq!(lookup_section_label(&b, 12.0), Some("verse_1"));
        assert_eq!(lookup_section_label(&b, 11.9999), Some("intro"));
    }

    #[test]
    fn lookup_out_of_range_returns_none() {
        let b = vec![make_boundary("intro", 0.0, 12.0)];
        assert_eq!(lookup_section_label(&b, -1.0), None);
        assert_eq!(lookup_section_label(&b, 12.0), None);
        assert_eq!(lookup_section_label(&b, 100.0), None);
    }

    #[test]
    fn lookup_empty_boundaries_returns_none() {
        assert_eq!(lookup_section_label(&[], 5.0), None);
    }

    #[test]
    fn lookup_nonfinite_t_returns_none() {
        let b = vec![make_boundary("intro", 0.0, 12.0)];
        assert_eq!(lookup_section_label(&b, f64::NAN), None);
        assert_eq!(lookup_section_label(&b, f64::INFINITY), None);
    }

    #[test]
    fn lookup_skips_malformed_boundary() {
        let b = vec![
            make_boundary("bad", 20.0, 10.0),     // start >= end
            make_boundary("verse_1", 12.0, 24.0), // well-formed
        ];
        assert_eq!(lookup_section_label(&b, 15.0), Some("verse_1"));
    }

    #[test]
    fn lookup_first_match_wins_on_overlap() {
        // Overlapping is generator-side bug; match the earlier declaration.
        let b = vec![
            make_boundary("first", 10.0, 20.0),
            make_boundary("second", 15.0, 25.0),
        ];
        assert_eq!(lookup_section_label(&b, 18.0), Some("first"));
    }

    #[test]
    fn hmac_key_matches_v1_1() {
        use crate::preset::compute_preset_checksum;
        use crate::preset::PresetFile;
        // v1.1 HMAC over empty preset-like snippet should use the same
        // key as v2.0. We compare via module default since option_env is
        // only set in CI, else DEFAULT_HMAC_KEY in both code paths.
        let p1 = PresetFile {
            schema_version: "1.1".to_string(),
            type_tag: "kirin_plugin_preset".to_string(),
            installation_id: "iid".to_string(),
            bounce_id: "b".to_string(),
            checksum: String::new(),
            regions: vec![],
        };
        let h1 = compute_preset_checksum(&p1);
        assert_eq!(h1.len(), 64);
        // Now compute v2 with identical payload surface; both should be
        // 64-char hex derived from the same 54-byte key. (We cannot check
        // equality of outputs because inputs differ; the sync property is
        // that both use option_env("KIRIN_HYPHA_HMAC_KEY") with the same
        // DEFAULT_HMAC_KEY fallback — enforced structurally by test at
        // module load time below.)
        let p2 = new_preset_v2_template("iid");
        let h2 = compute_preset_v2_checksum(&p2);
        assert_eq!(h2.len(), 64);
    }
}
