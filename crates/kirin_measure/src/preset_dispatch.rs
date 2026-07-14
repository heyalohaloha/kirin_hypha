//! preset schema_version dispatch — G-77-03.
//!
//! Routes `plugin_data/{project_hash}/preset/*.json` to the correct reader:
//!
//! | schema_version | Handler                           |
//! |----------------|-----------------------------------|
//! | `"1.1"`        | [`crate::preset::verify_preset`]   |
//! | `"2.0"`        | [`crate::preset_v2::verify_preset_v2`] |
//! | anything else  | logged error + skip (bis **silent skip 絶対禁止**) |
//!
//! Unknown `schema_version` values must NOT be silently dropped. They go
//! through [`log_unknown_schema`] so operators can diagnose producer drift.
//! This is a deliberate departure from the R-28 silence rule: silence is
//! for *valid-but-null* states; unknown schema_version is an operational
//! anomaly that demands visibility.
//!
//! # 保護境界 (bis.3)
//! - 既存 v1.1 reader (`preset.rs`) を呼び出すのみ。改変しない。
//! - Audio Thread / Measure Thread / IO Thread / 計測エンジン / 書込 / 排他 /
//!   record_signal / close / identity.json には手を入れない。

use serde::Deserialize;
use std::fs;
use std::path::{Path, PathBuf};

use crate::preset::{self, verify_preset, PresetFile, VerifyError};
use crate::preset_v2::{self, verify_preset_v2, PresetFileV2, VerifyErrorV2};

// ── dispatched variant ───────────────────────────────────────────────────────

/// Unified carrier for either schema version.  GUI (T-E/T-F) pattern-matches
/// on this to decide which fields to render.
#[derive(Debug, Clone, PartialEq)]
pub enum PresetVariant {
    V1_1(PresetFile),
    V2_0(PresetFileV2),
}

impl PresetVariant {
    pub fn schema_version(&self) -> &str {
        match self {
            Self::V1_1(p) => p.schema_version.as_str(),
            Self::V2_0(p) => p.schema_version.as_str(),
        }
    }
}

/// Failure reason for a single file.  `Unknown` is the bis non-silence case.
#[derive(Debug, Clone, PartialEq)]
pub enum DispatchError {
    Io(String),
    Parse(String),
    Unknown(String),
    V1_1(VerifyError),
    V2_0(VerifyErrorV2),
}

impl std::fmt::Display for DispatchError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(s) => write!(f, "io: {s}"),
            Self::Parse(s) => write!(f, "parse: {s}"),
            Self::Unknown(v) => write!(f, "unknown schema_version: {v}"),
            Self::V1_1(e) => write!(f, "v1.1: {e}"),
            Self::V2_0(e) => write!(f, "v2.0: {e}"),
        }
    }
}

// ── JSON version peek ────────────────────────────────────────────────────────

#[derive(Deserialize)]
struct SchemaEnvelope {
    schema_version: String,
}

/// Read `schema_version` out of JSON bytes without fully decoding the payload.
fn peek_schema_version(bytes: &[u8]) -> Result<String, DispatchError> {
    let env: SchemaEnvelope =
        serde_json::from_slice(bytes).map_err(|e| DispatchError::Parse(e.to_string()))?;
    Ok(env.schema_version)
}

// ── single-file dispatch ─────────────────────────────────────────────────────

/// Parse + verify a single preset file, routing on schema_version.
///
/// Unknown values are reported via [`log_unknown_schema`] before returning
/// `Err(DispatchError::Unknown)`.
pub fn dispatch_one(
    path: &Path,
    own_installation_id: &str,
) -> Result<PresetVariant, DispatchError> {
    let bytes = fs::read(path).map_err(|e| DispatchError::Io(e.to_string()))?;
    let version = peek_schema_version(&bytes)?;
    match version.as_str() {
        "1.1" => {
            let preset: PresetFile =
                serde_json::from_slice(&bytes).map_err(|e| DispatchError::Parse(e.to_string()))?;
            verify_preset(&preset, own_installation_id).map_err(DispatchError::V1_1)?;
            Ok(PresetVariant::V1_1(preset))
        }
        "2.0" => {
            let preset: PresetFileV2 =
                serde_json::from_slice(&bytes).map_err(|e| DispatchError::Parse(e.to_string()))?;
            verify_preset_v2(&preset, own_installation_id).map_err(DispatchError::V2_0)?;
            Ok(PresetVariant::V2_0(preset))
        }
        other => {
            log_unknown_schema(path, other);
            Err(DispatchError::Unknown(other.to_string()))
        }
    }
}

/// Hypha-side visibility for an unknown `schema_version`. Using `log::warn!`
/// so operators and the watchdog capture it; `eprintln!` is the fallback
/// when the `log` facade is not configured in tests.
fn log_unknown_schema(path: &Path, version: &str) {
    let msg = format!(
        "[preset_dispatch] unknown schema_version={version} at {}",
        path.display()
    );
    log::warn!("{msg}");
    // Also surface on stderr so dev / CI runs see it without configuring
    // log output. bis requires this to be non-silent.
    eprintln!("{msg}");
}

// ── directory scan ───────────────────────────────────────────────────────────

/// Scan `{base}/{project_hash}/preset/` and return every successfully
/// dispatched variant, preserving filename-ascending order.
pub fn scan_any_presets(
    base: &Path,
    project_hash: &str,
    own_installation_id: &str,
) -> Vec<PresetVariant> {
    let dir = preset::preset_dir(base, project_hash);
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
        match dispatch_one(&path, own_installation_id) {
            Ok(v) => out.push(v),
            Err(DispatchError::Unknown(_)) => {
                // Already logged; keep scanning.
            }
            Err(_) => {
                // R-28: validation failures stay silent (malformed / tampered
                // / wrong installation — same semantics as the individual
                // v1.1 and v2.0 scanners).
            }
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

// Re-export the v2 scanner for callers that only want v2.0 results.
pub use preset_v2::scan_valid_presets_v2;

/// Return the single newest v2.0 proposals file, or `None` when the preset/
/// dir is empty / contains only v1.1 / all v2.0 files failed verification.
///
/// Ordering is by `generated_at` (ISO 8601 UTC strings compare lexically in
/// chronological order), with filename tie-break via the underlying sort in
/// [`scan_any_presets`] — so two proposals generated in the same UTC second
/// resolve deterministically.
pub fn scan_latest_v2_preset(
    base: &Path,
    project_hash: &str,
    own_installation_id: &str,
) -> Option<PresetFileV2> {
    let all = scan_any_presets(base, project_hash, own_installation_id);
    all.into_iter()
        .filter_map(|v| match v {
            PresetVariant::V2_0(p) => Some(p),
            PresetVariant::V1_1(_) => None,
        })
        .max_by(|a, b| a.generated_at.cmp(&b.generated_at))
}

/// Read the producer-owned latest proposal pointer. Runtime GUI code uses this deterministic path;
/// historical scanning remains available only to explicit migration/diagnostic callers.
pub fn read_current_v2_preset(
    base: &Path,
    project_hash: &str,
    own_installation_id: &str,
) -> Option<PresetFileV2> {
    let path = preset::preset_dir(base, project_hash).join("current.json");
    match dispatch_one(&path, own_installation_id).ok()? {
        PresetVariant::V2_0(preset) => Some(preset),
        PresetVariant::V1_1(_) => None,
    }
}

// ── tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::preset::{compute_preset_checksum, preset_dir, PresetFile, Region};
    use crate::preset_v2::{compute_preset_v2_checksum, PresetFileV2, Summary};
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_preset_dispatch_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn write_at(dir: &Path, name: &str, bytes: &[u8]) -> PathBuf {
        fs::create_dir_all(dir).unwrap();
        let p = dir.join(name);
        fs::write(&p, bytes).unwrap();
        p
    }

    fn signed_v1_1(installation_id: &str) -> PresetFile {
        let mut p = PresetFile {
            schema_version: "1.1".to_string(),
            type_tag: "kirin_plugin_preset".to_string(),
            installation_id: installation_id.to_string(),
            bounce_id: "b".to_string(),
            checksum: String::new(),
            regions: vec![Region {
                start_sec: 0.0,
                end_sec: 1.0,
                metric: "sharpness".to_string(),
                bark_band: None,
                value: 3.2,
                delta: 0.6,
                threshold: 2.5,
                threshold_type: "absolute".to_string(),
                threshold_source: "severity_L3".to_string(),
                confidence: "ESTIMATED".to_string(),
            }],
        };
        p.checksum = compute_preset_checksum(&p);
        p
    }

    fn signed_v2_0(installation_id: &str) -> PresetFileV2 {
        let mut p = PresetFileV2 {
            schema_version: "2.0".to_string(),
            installation_id: installation_id.to_string(),
            session_id: "20260417T143208".to_string(),
            bounce_id: "00000000-0000-4000-8000-000000000000".to_string(),
            work_id: None,
            generated_at: "2026-04-23T10:00:00Z".to_string(),
            cards: vec![],
            section_boundaries: vec![],
            summary: Summary::default(),
            hmac_checksum: String::new(),
        };
        p.hmac_checksum = compute_preset_v2_checksum(&p);
        p
    }

    // ── dispatch_one ────────────────────────────────────────

    #[test]
    fn dispatch_v1_1_routes_to_v1_1_reader() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        let p = signed_v1_1("iid-own");
        let path = write_at(&dir, "a.json", &serde_json::to_vec(&p).unwrap());
        let v = dispatch_one(&path, "iid-own").unwrap();
        assert!(matches!(v, PresetVariant::V1_1(_)));
        assert_eq!(v.schema_version(), "1.1");
    }

    #[test]
    fn dispatch_v2_0_routes_to_v2_0_reader() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        let p = signed_v2_0("iid-own");
        let path = write_at(&dir, "b.json", &serde_json::to_vec(&p).unwrap());
        let v = dispatch_one(&path, "iid-own").unwrap();
        assert!(matches!(v, PresetVariant::V2_0(_)));
        assert_eq!(v.schema_version(), "2.0");
    }

    #[test]
    fn dispatch_unknown_schema_is_surfaced_not_silent() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        let path = write_at(
            &dir,
            "x.json",
            br#"{"schema_version":"9.9","installation_id":"iid-own"}"#,
        );
        let r = dispatch_one(&path, "iid-own");
        assert!(
            matches!(r, Err(DispatchError::Unknown(ref v)) if v == "9.9"),
            "expected Unknown(9.9), got {r:?}"
        );
    }

    #[test]
    fn dispatch_malformed_json_returns_parse_err() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        let path = write_at(&dir, "bad.json", b"{not json}");
        let r = dispatch_one(&path, "iid-own");
        assert!(matches!(r, Err(DispatchError::Parse(_))));
    }

    #[test]
    fn dispatch_tampered_v2_returns_checksum_err_v2() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        let mut p = signed_v2_0("iid-own");
        p.hmac_checksum = "0".repeat(64);
        let path = write_at(&dir, "t.json", &serde_json::to_vec(&p).unwrap());
        let r = dispatch_one(&path, "iid-own");
        assert!(matches!(
            r,
            Err(DispatchError::V2_0(VerifyErrorV2::ChecksumMismatch))
        ));
    }

    // ── scan_any_presets ────────────────────────────────────

    #[test]
    fn scan_any_returns_both_versions_side_by_side() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        fs::create_dir_all(&dir).unwrap();
        let p1 = signed_v1_1("iid-own");
        let p2 = signed_v2_0("iid-own");
        fs::write(dir.join("a.json"), serde_json::to_vec(&p1).unwrap()).unwrap();
        fs::write(dir.join("b.json"), serde_json::to_vec(&p2).unwrap()).unwrap();
        let v = scan_any_presets(&base, "PH", "iid-own");
        assert_eq!(v.len(), 2);
        assert_eq!(v[0].schema_version(), "1.1");
        assert_eq!(v[1].schema_version(), "2.0");
    }

    // ── scan_latest_v2_preset ───────────────────────────────

    #[test]
    fn scan_latest_v2_newest_by_generated_at() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        fs::create_dir_all(&dir).unwrap();

        let mut old = signed_v2_0("iid-own");
        old.generated_at = "2026-04-20T00:00:00Z".to_string();
        old.hmac_checksum = compute_preset_v2_checksum(&old);

        let mut newer = signed_v2_0("iid-own");
        newer.generated_at = "2026-04-23T12:00:00Z".to_string();
        newer.hmac_checksum = compute_preset_v2_checksum(&newer);

        fs::write(dir.join("a.json"), serde_json::to_vec(&old).unwrap()).unwrap();
        fs::write(dir.join("b.json"), serde_json::to_vec(&newer).unwrap()).unwrap();

        let r = scan_latest_v2_preset(&base, "PH", "iid-own");
        assert!(r.is_some());
        assert_eq!(r.unwrap().generated_at, "2026-04-23T12:00:00Z");
    }

    #[test]
    fn scan_latest_v2_none_when_only_v1_1_present() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        fs::create_dir_all(&dir).unwrap();
        let p = signed_v1_1("iid-own");
        fs::write(dir.join("a.json"), serde_json::to_vec(&p).unwrap()).unwrap();
        assert!(scan_latest_v2_preset(&base, "PH", "iid-own").is_none());
    }

    #[test]
    fn scan_latest_v2_none_when_empty_dir() {
        let base = isolated_dir();
        assert!(scan_latest_v2_preset(&base, "PH", "iid-own").is_none());
    }

    #[test]
    fn current_v2_reads_only_the_producer_pointer() {
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        fs::create_dir_all(&dir).unwrap();
        let old = signed_v2_0("iid-own");
        let mut current = signed_v2_0("iid-own");
        current.generated_at = "2026-04-23T12:00:00Z".to_string();
        current.hmac_checksum = compute_preset_v2_checksum(&current);
        fs::write(dir.join("history.json"), serde_json::to_vec(&old).unwrap()).unwrap();
        fs::write(
            dir.join("current.json"),
            serde_json::to_vec(&current).unwrap(),
        )
        .unwrap();

        assert_eq!(
            read_current_v2_preset(&base, "PH", "iid-own")
                .unwrap()
                .generated_at,
            current.generated_at
        );
    }

    #[test]
    fn scan_any_skips_unknown_after_logging() {
        // Captured by cargo test output — stderr will carry one line per
        // unknown. This test asserts the valid file survives alongside
        // the unknown one being dropped.
        let base = isolated_dir();
        let dir = preset_dir(&base, "PH");
        fs::create_dir_all(&dir).unwrap();
        let p2 = signed_v2_0("iid-own");
        fs::write(dir.join("a.json"), serde_json::to_vec(&p2).unwrap()).unwrap();
        fs::write(
            dir.join("b.json"),
            br#"{"schema_version":"9.9","installation_id":"iid-own"}"#,
        )
        .unwrap();
        let v = scan_any_presets(&base, "PH", "iid-own");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].schema_version(), "2.0");
    }
}
