//! record_signal.json — POST → PRE 自動追従シグナル（G-50-34 / G-50-35）。
//!
//! guardian_58_hypha_step5_record_plugin_data.md T-4 対応。
//!
//! # ファイル
//! ```text
//! plugin_data/{project_hash}/{bus}/record_signal.json
//! ```
//!
//! # スキーマ
//! ```json
//! {
//!   "status": "pending | acknowledged | released",
//!   "requested_by": "instance_id of POST",
//!   "target_pre_instance_id": "instance_id of PRE (ペアリング結果)",
//!   "t": "ISO 8601（最終遷移時刻。status 変化で更新）",
//!   "started_at": "ISO 8601（Record 開始時刻。pending 配置時に固定、以後不変）"
//! }
//! ```
//!
//! `t` は status の更新時刻で、pending → acknowledged → released の度に書き換わる。
//! `started_at` は Record 開始（pending 配置）時に固定され、遷移では変わらない。
//! PRE / POST 双方が `started_at` を読んで共通の t_ms 軸を構築する（同じ Record
//! に属する両側のフレームが同じ経過時間で並ぶ）。
//!
//! # シーケンス
//! ```text
//! 1. POST「残す」→ 排他OK → pending 配置 + 自 Record 切替
//! 2. PRE IO Thread が 1 秒間隔で監視（T-6 側）
//!    → pending 検出 → 自 Record 切替 → acknowledged 更新
//! 3. POST「記録を止める」→ released 更新 → POST Watch → PRE が released 検出 → Watch
//!    → record_signal.json 削除
//! ```
//!
//! # タイムアウト
//! POST が pending 配置後 30 秒以内に acknowledged が返らなければ GUI 警告 +
//! POST 自身 Watch 復帰 + record_signal.json 削除。
//!
//! # POST→PRE ペアリング（G-50-35）
//! `/tmp/kirin/{project_hash}/{bus}/pre_*.json` を走査。
//! - 1 つ → 自動確定
//! - 複数 → `distance = |ΔLUFS| + |ΔTP| + |ΔCrest|` 最小を選択（同値は先頭優先）
//!
//! # Phase 1 制限（G-50-35）
//! Watch 複数ペア ✅、Record 複数ペア同一バス ❌ → 12 セット送り（Phase 2）。
//!
//! # T-4 のスコープ
//! 本モジュールは純粋な FS I/O + 距離計算のみ。1 秒間隔ポーリングや GUI 警告は
//! T-6 Plugin 統合で組み立てる。

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// pending→acknowledged タイムアウト（秒）。
pub const ACK_TIMEOUT_SECONDS: i64 = 30;

/// record_signal.json のファイル名（定数）。
pub const SIGNAL_FILENAME: &str = "record_signal.json";

// ── スキーマ ─────────────────────────────────────────────────────────────────

/// シグナル 3 状態。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum SignalStatus {
    Pending,
    Acknowledged,
    Released,
}

impl SignalStatus {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Pending => "pending",
            Self::Acknowledged => "acknowledged",
            Self::Released => "released",
        }
    }
}

/// record_signal.json ルート構造。
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RecordSignal {
    pub status: SignalStatus,
    pub requested_by: String,
    pub target_pre_instance_id: String,
    pub t: String,
    /// Record 開始時刻（pending 配置時に固定）。status 遷移でも更新されない。
    /// 旧バージョンで書かれたファイルには存在しないことがあるため `#[serde(default)]`。
    #[serde(default)]
    pub started_at: String,
}

impl RecordSignal {
    /// 新規 pending シグナル。`t` と `started_at` は現在時刻。
    pub fn new_pending(requested_by: String, target_pre_instance_id: String) -> Self {
        let now = now_iso8601();
        Self {
            status: SignalStatus::Pending,
            requested_by,
            target_pre_instance_id,
            t: now.clone(),
            started_at: now,
        }
    }
}

// ── パス構築 ─────────────────────────────────────────────────────────────────

/// `{base}/{project_hash}/{bus}/record_signal.json` を構築。
pub fn signal_path(base_dir: &Path, project_hash: &str, bus: &str) -> PathBuf {
    base_dir.join(project_hash).join(bus).join(SIGNAL_FILENAME)
}

fn tmp_path(final_path: &Path) -> PathBuf {
    let mut s = final_path.as_os_str().to_os_string();
    s.push(".tmp");
    PathBuf::from(s)
}

// ── I/O ──────────────────────────────────────────────────────────────────────

/// I/O エラー。
#[derive(Debug)]
pub enum SignalError {
    Io(io::Error),
    Serde(serde_json::Error),
}

impl std::fmt::Display for SignalError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "IO error: {e}"),
            Self::Serde(e) => write!(f, "JSON error: {e}"),
        }
    }
}

impl std::error::Error for SignalError {}

impl From<io::Error> for SignalError {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl From<serde_json::Error> for SignalError {
    fn from(e: serde_json::Error) -> Self {
        Self::Serde(e)
    }
}

/// pending シグナルを atomic 書込。親ディレクトリが無ければ作成。
pub fn write_pending(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
    requested_by: String,
    target_pre_instance_id: String,
) -> Result<RecordSignal, SignalError> {
    let signal = RecordSignal::new_pending(requested_by, target_pre_instance_id);
    write_signal(base_dir, project_hash, bus, &signal)?;
    Ok(signal)
}

/// 任意のシグナルを atomic 書込（`.tmp` → rename）。
pub fn write_signal(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
    signal: &RecordSignal,
) -> Result<(), SignalError> {
    let final_path = signal_path(base_dir, project_hash, bus);
    if let Some(parent) = final_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let tmp = tmp_path(&final_path);
    let json = serde_json::to_vec(signal)?;
    fs::write(&tmp, json)?;
    fs::rename(&tmp, &final_path)?;
    Ok(())
}

/// シグナル読込。存在しない / パース失敗時は None。
pub fn read_signal(base_dir: &Path, project_hash: &str, bus: &str) -> Option<RecordSignal> {
    let path = signal_path(base_dir, project_hash, bus);
    let bytes = fs::read(&path).ok()?;
    serde_json::from_slice(&bytes).ok()
}

/// 状態遷移: pending → acknowledged。`t` は現在時刻に更新。
///
/// 既存シグナルが無い / 読込失敗時は `Ok(false)` を返す（呼出側で扱い決定）。
pub fn mark_acknowledged(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
) -> Result<bool, SignalError> {
    transition_status(base_dir, project_hash, bus, SignalStatus::Acknowledged)
}

/// 状態遷移: * → released。
pub fn mark_released(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
) -> Result<bool, SignalError> {
    transition_status(base_dir, project_hash, bus, SignalStatus::Released)
}

fn transition_status(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
    next: SignalStatus,
) -> Result<bool, SignalError> {
    let Some(mut signal) = read_signal(base_dir, project_hash, bus) else {
        return Ok(false);
    };
    signal.status = next;
    signal.t = now_iso8601();
    write_signal(base_dir, project_hash, bus, &signal)?;
    Ok(true)
}

/// record_signal.json を削除。不在は成功扱い。
pub fn delete_signal(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
) -> Result<(), SignalError> {
    let path = signal_path(base_dir, project_hash, bus);
    match fs::remove_file(&path) {
        Ok(()) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(e) => Err(SignalError::Io(e)),
    }
}

/// pending 状態で `t` から `timeout_secs` 秒以上経過していれば true。
///
/// pending 以外 / t パース失敗時は false（タイムアウト扱いしない）。
pub fn is_timed_out(
    signal: &RecordSignal,
    now: DateTime<Utc>,
    timeout_secs: i64,
) -> bool {
    if signal.status != SignalStatus::Pending {
        return false;
    }
    let t = match DateTime::parse_from_rfc3339(&signal.t) {
        Ok(d) => d.with_timezone(&Utc),
        Err(_) => return false,
    };
    now.signed_duration_since(t).num_seconds() > timeout_secs
}

// ── PRE ペアリング ───────────────────────────────────────────────────────────

/// `/tmp/kirin/{ph}/{bus}/pre_*.json` 1 件分のパース結果。
#[derive(Debug, Clone, PartialEq)]
pub struct PreCandidate {
    pub instance_id: String,
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
    pub crest: Option<f64>,
    pub path: PathBuf,
}

/// pre tmp JSON の抜粋（distance 計算に必要なフィールドのみ）。
#[derive(Debug, Deserialize)]
struct PreTmpJson {
    instance_id: String,
    #[serde(default)]
    lufs_m: Option<f64>,
    #[serde(default)]
    true_peak: Option<f64>,
    #[serde(default)]
    crest: Option<f64>,
}

/// `/tmp/kirin/{project_hash}/{bus}/` 配下の `pre_*.json` を走査。
///
/// `tmp_base` は通常 `std::env::temp_dir().join("kirin")`。テストでは注入可。
/// 走査失敗・パース失敗は skip（バッシュ panic せず）。
pub fn scan_pre_candidates(
    tmp_base: &Path,
    project_hash: &str,
    bus: &str,
) -> Vec<PreCandidate> {
    let dir = tmp_base.join(project_hash).join(bus);
    let entries = match fs::read_dir(&dir) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };
    let mut out = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if !is_pre_json(&path) {
            continue;
        }
        let Ok(bytes) = fs::read(&path) else { continue };
        let Ok(parsed): Result<PreTmpJson, _> = serde_json::from_slice(&bytes) else {
            continue;
        };
        out.push(PreCandidate {
            instance_id: parsed.instance_id,
            lufs_m: parsed.lufs_m,
            true_peak: parsed.true_peak,
            crest: parsed.crest,
            path,
        });
    }
    // 再現性のため instance_id 順にソート（同値 distance の先頭選択を安定化）。
    out.sort_by(|a, b| a.instance_id.cmp(&b.instance_id));
    out
}

/// POST 側計測値。距離計算用。
#[derive(Debug, Clone, Copy)]
pub struct PostMetrics {
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
    pub crest: Option<f64>,
}

/// PRE 候補から最近 PRE を選ぶ。
///
/// - 0 件 → None
/// - 1 件 → その 1 つを返す（計測値無くても自動確定。G-50-35）
/// - 複数 + POST/PRE 双方に全 3 メトリック有 → distance 最小を返す
/// - 複数 + いずれかメトリック不在 → 最初の候補を返す（ソート済みなので安定）
pub fn pick_closest_pre(
    candidates: &[PreCandidate],
    post: PostMetrics,
) -> Option<&PreCandidate> {
    match candidates.len() {
        0 => None,
        1 => candidates.first(),
        _ => {
            let (Some(pl), Some(pt), Some(pc)) = (post.lufs_m, post.true_peak, post.crest)
            else {
                return candidates.first();
            };
            let mut best: Option<(&PreCandidate, f64)> = None;
            for cand in candidates {
                let (Some(cl), Some(ct), Some(cc)) = (cand.lufs_m, cand.true_peak, cand.crest)
                else {
                    continue;
                };
                let dist = (pl - cl).abs() + (pt - ct).abs() + (pc - cc).abs();
                match &best {
                    Some((_, best_d)) if *best_d <= dist => {} // 同値は先頭優先
                    _ => best = Some((cand, dist)),
                }
            }
            best.map(|(c, _)| c).or_else(|| candidates.first())
        }
    }
}

// ── ヘルパ ────────────────────────────────────────────────────────────────────

fn now_iso8601() -> String {
    chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%SZ")
        .to_string()
}

fn is_pre_json(path: &Path) -> bool {
    let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
        return false;
    };
    name.starts_with("pre_") && name.ends_with(".json") && !name.ends_with(".tmp")
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
        let dir = std::env::temp_dir().join(format!("kirin_signal_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    // ── スキーマ・パス ──────────────────────────────────────

    #[test]
    fn signal_status_serialization_is_lowercase() {
        let s = serde_json::to_string(&SignalStatus::Pending).unwrap();
        assert_eq!(s, "\"pending\"");
        let s = serde_json::to_string(&SignalStatus::Acknowledged).unwrap();
        assert_eq!(s, "\"acknowledged\"");
        let s = serde_json::to_string(&SignalStatus::Released).unwrap();
        assert_eq!(s, "\"released\"");
    }

    #[test]
    fn signal_roundtrip_preserves_all_fields() {
        let s = RecordSignal::new_pending("post-001".into(), "pre-xyz".into());
        let json = serde_json::to_string(&s).unwrap();
        let back: RecordSignal = serde_json::from_str(&json).unwrap();
        assert_eq!(s, back);
    }

    #[test]
    fn signal_path_hierarchy() {
        let p = signal_path(Path::new("/tmp/kb"), "phash", "MIX");
        assert_eq!(p, Path::new("/tmp/kb/phash/MIX/record_signal.json"));
    }

    // ── I/O ─────────────────────────────────────────────────

    #[test]
    fn write_pending_creates_file_with_pending_status() {
        let base = isolated_dir();
        let s = write_pending(&base, "ph", "MIX", "post-1".into(), "pre-1".into()).unwrap();
        assert_eq!(s.status, SignalStatus::Pending);
        assert_eq!(s.requested_by, "post-1");
        assert_eq!(s.target_pre_instance_id, "pre-1");
        let loaded = read_signal(&base, "ph", "MIX").unwrap();
        assert_eq!(loaded, s);
    }

    #[test]
    fn read_signal_missing_returns_none() {
        let base = isolated_dir();
        assert!(read_signal(&base, "ph", "MIX").is_none());
    }

    #[test]
    fn read_signal_corrupt_returns_none() {
        let base = isolated_dir();
        let dir = base.join("ph").join("MIX");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join(SIGNAL_FILENAME), b"not json").unwrap();
        assert!(read_signal(&base, "ph", "MIX").is_none());
    }

    #[test]
    fn mark_acknowledged_updates_status_and_returns_true() {
        let base = isolated_dir();
        write_pending(&base, "ph", "MIX", "p".into(), "r".into()).unwrap();
        let changed = mark_acknowledged(&base, "ph", "MIX").unwrap();
        assert!(changed);
        let loaded = read_signal(&base, "ph", "MIX").unwrap();
        assert_eq!(loaded.status, SignalStatus::Acknowledged);
    }

    #[test]
    fn started_at_preserved_across_transitions() {
        let base = isolated_dir();
        let initial = write_pending(&base, "ph", "MIX", "p".into(), "r".into()).unwrap();
        let first_started = initial.started_at.clone();
        assert!(!first_started.is_empty(), "started_at must be set on new_pending");
        // 遷移ごとに t は変わっても started_at は変わらない
        std::thread::sleep(std::time::Duration::from_millis(1100)); // ISO 秒精度なので 1s 待つ
        mark_acknowledged(&base, "ph", "MIX").unwrap();
        let acked = read_signal(&base, "ph", "MIX").unwrap();
        assert_eq!(acked.started_at, first_started);
        assert_ne!(acked.t, first_started, "t should have advanced");
        mark_released(&base, "ph", "MIX").unwrap();
        let released = read_signal(&base, "ph", "MIX").unwrap();
        assert_eq!(released.started_at, first_started);
    }

    #[test]
    fn started_at_missing_field_defaults_to_empty() {
        // 旧バージョン (started_at 無し) の JSON を読み込んでもパースできること
        let base = isolated_dir();
        let dir = base.join("ph").join("MIX");
        fs::create_dir_all(&dir).unwrap();
        let legacy = r#"{"status":"pending","requested_by":"p","target_pre_instance_id":"r","t":"2026-01-01T00:00:00Z"}"#;
        fs::write(dir.join(SIGNAL_FILENAME), legacy).unwrap();
        let loaded = read_signal(&base, "ph", "MIX").unwrap();
        assert_eq!(loaded.started_at, "");
    }

    #[test]
    fn mark_released_updates_status() {
        let base = isolated_dir();
        write_pending(&base, "ph", "MIX", "p".into(), "r".into()).unwrap();
        mark_released(&base, "ph", "MIX").unwrap();
        let loaded = read_signal(&base, "ph", "MIX").unwrap();
        assert_eq!(loaded.status, SignalStatus::Released);
    }

    #[test]
    fn transition_on_missing_returns_false() {
        let base = isolated_dir();
        let changed = mark_acknowledged(&base, "ph", "MIX").unwrap();
        assert!(!changed);
    }

    #[test]
    fn delete_signal_removes_file() {
        let base = isolated_dir();
        write_pending(&base, "ph", "MIX", "p".into(), "r".into()).unwrap();
        delete_signal(&base, "ph", "MIX").unwrap();
        assert!(read_signal(&base, "ph", "MIX").is_none());
    }

    #[test]
    fn delete_signal_on_missing_is_ok() {
        let base = isolated_dir();
        // エラー無く成功すること（冪等）
        delete_signal(&base, "ph", "MIX").unwrap();
    }

    #[test]
    fn atomic_write_leaves_no_tmp_behind() {
        let base = isolated_dir();
        write_pending(&base, "ph", "MIX", "p".into(), "r".into()).unwrap();
        let final_path = signal_path(&base, "ph", "MIX");
        let tmp = tmp_path(&final_path);
        assert!(final_path.exists());
        assert!(!tmp.exists(), "tmp must be renamed away: {tmp:?}");
    }

    // ── タイムアウト ────────────────────────────────────────

    fn iso_ago(secs: i64, now: DateTime<Utc>) -> String {
        let t = now - chrono::Duration::seconds(secs);
        t.format("%Y-%m-%dT%H:%M:%SZ").to_string()
    }

    #[test]
    fn timeout_fires_strictly_after_30s() {
        let now = Utc::now();
        let mut sig = RecordSignal::new_pending("p".into(), "r".into());
        sig.t = iso_ago(30, now);
        // 30 秒ちょうどは timed_out=false（> 判定）
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
        sig.t = iso_ago(31, now);
        assert!(is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    }

    #[test]
    fn timeout_not_considered_for_non_pending() {
        let now = Utc::now();
        let mut sig = RecordSignal::new_pending("p".into(), "r".into());
        sig.t = iso_ago(3600, now);
        sig.status = SignalStatus::Acknowledged;
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
        sig.status = SignalStatus::Released;
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    }

    #[test]
    fn timeout_invalid_iso_is_false() {
        let now = Utc::now();
        let mut sig = RecordSignal::new_pending("p".into(), "r".into());
        sig.t = "not-iso".to_string();
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    }

    // ── ペアリング ──────────────────────────────────────────

    fn write_pre_tmp(
        tmp_base: &Path,
        ph: &str,
        bus: &str,
        instance_id: &str,
        metrics: Option<(f64, f64, f64)>,
    ) -> PathBuf {
        let dir = tmp_base.join(ph).join(bus);
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join(format!("pre_{instance_id}.json"));
        let json = if let Some((l, t, c)) = metrics {
            format!(
                r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","bus":"{bus}","signal_state":"active","t":"now","lufs_m":{l},"true_peak":{t},"crest":{c},"psr":8.0}}"#
            )
        } else {
            format!(
                r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","bus":"{bus}","signal_state":"inactive","t":"now"}}"#
            )
        };
        fs::write(&path, json).unwrap();
        path
    }

    #[test]
    fn scan_with_no_dir_returns_empty() {
        let base = isolated_dir();
        let v = scan_pre_candidates(&base, "ph", "MIX");
        assert!(v.is_empty());
    }

    #[test]
    fn scan_ignores_non_pre_json_and_tmp() {
        let base = isolated_dir();
        let dir = base.join("ph").join("MIX");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("post_x.json"), b"{}").unwrap();
        fs::write(dir.join("pre_y.json.tmp"), b"{}").unwrap();
        fs::write(dir.join("unrelated.txt"), b"{}").unwrap();
        write_pre_tmp(&base, "ph", "MIX", "good", Some((-14.0, -1.0, 12.0)));
        let v = scan_pre_candidates(&base, "ph", "MIX");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].instance_id, "good");
    }

    #[test]
    fn scan_skips_corrupt_json() {
        let base = isolated_dir();
        let dir = base.join("ph").join("MIX");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("pre_bad.json"), b"{ not valid").unwrap();
        write_pre_tmp(&base, "ph", "MIX", "ok", Some((-14.0, -1.0, 12.0)));
        let v = scan_pre_candidates(&base, "ph", "MIX");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].instance_id, "ok");
    }

    #[test]
    fn pick_zero_returns_none() {
        let v: Vec<PreCandidate> = Vec::new();
        let r = pick_closest_pre(&v, PostMetrics { lufs_m: Some(-14.0), true_peak: Some(-1.0), crest: Some(12.0) });
        assert!(r.is_none());
    }

    #[test]
    fn pick_single_auto_selects() {
        let base = isolated_dir();
        write_pre_tmp(&base, "ph", "MIX", "solo", Some((-14.0, -1.0, 12.0)));
        let v = scan_pre_candidates(&base, "ph", "MIX");
        let r = pick_closest_pre(&v, PostMetrics { lufs_m: Some(-14.0), true_peak: Some(-1.0), crest: Some(12.0) });
        assert_eq!(r.unwrap().instance_id, "solo");
    }

    #[test]
    fn pick_single_with_no_metrics_still_auto_selects() {
        // G-50-35: PRE 1 つなら計測値がなくても自動確定
        let base = isolated_dir();
        write_pre_tmp(&base, "ph", "MIX", "silent", None);
        let v = scan_pre_candidates(&base, "ph", "MIX");
        let r = pick_closest_pre(&v, PostMetrics { lufs_m: None, true_peak: None, crest: None });
        assert_eq!(r.unwrap().instance_id, "silent");
    }

    #[test]
    fn pick_multi_picks_minimum_distance() {
        let base = isolated_dir();
        // POST: (-14, -1, 12)
        // A: (-20, -5, 15) → distance = 6+4+3 = 13
        // B: (-13, -1.5, 12) → distance = 1+0.5+0 = 1.5 ← closest
        // C: (-14, -2, 14) → distance = 0+1+2 = 3
        write_pre_tmp(&base, "ph", "MIX", "a", Some((-20.0, -5.0, 15.0)));
        write_pre_tmp(&base, "ph", "MIX", "b", Some((-13.0, -1.5, 12.0)));
        write_pre_tmp(&base, "ph", "MIX", "c", Some((-14.0, -2.0, 14.0)));
        let v = scan_pre_candidates(&base, "ph", "MIX");
        let r = pick_closest_pre(
            &v,
            PostMetrics {
                lufs_m: Some(-14.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
            },
        );
        assert_eq!(r.unwrap().instance_id, "b");
    }

    #[test]
    fn pick_multi_tie_picks_first_by_sorted_order() {
        // 同値 distance は sort 済みの先頭を返す（安定性）。
        let base = isolated_dir();
        // POST: (-14, -1, 12)
        // a と b がどちらも distance=1（偏り方向が異なる）
        write_pre_tmp(&base, "ph", "MIX", "a", Some((-15.0, -1.0, 12.0))); // 1+0+0
        write_pre_tmp(&base, "ph", "MIX", "b", Some((-14.0, -2.0, 12.0))); // 0+1+0
        let v = scan_pre_candidates(&base, "ph", "MIX");
        let r = pick_closest_pre(
            &v,
            PostMetrics {
                lufs_m: Some(-14.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
            },
        );
        assert_eq!(r.unwrap().instance_id, "a");
    }

    #[test]
    fn pick_multi_post_missing_metrics_falls_back_to_first() {
        let base = isolated_dir();
        write_pre_tmp(&base, "ph", "MIX", "a", Some((-20.0, -5.0, 15.0)));
        write_pre_tmp(&base, "ph", "MIX", "b", Some((-14.0, -1.0, 12.0)));
        let v = scan_pre_candidates(&base, "ph", "MIX");
        let r = pick_closest_pre(
            &v,
            PostMetrics {
                lufs_m: None,
                true_peak: None,
                crest: None,
            },
        );
        // distance 計算不能 → 先頭（sorted: a）
        assert_eq!(r.unwrap().instance_id, "a");
    }

    #[test]
    fn pick_multi_pre_missing_metrics_skipped_for_distance() {
        // a: メトリック無し, b: distance=2, c: distance=10
        let base = isolated_dir();
        write_pre_tmp(&base, "ph", "MIX", "a", None);
        write_pre_tmp(&base, "ph", "MIX", "b", Some((-15.0, -2.0, 12.0))); // 1+1+0
        write_pre_tmp(&base, "ph", "MIX", "c", Some((-20.0, -5.0, 15.0))); // 6+4+3
        let v = scan_pre_candidates(&base, "ph", "MIX");
        let r = pick_closest_pre(
            &v,
            PostMetrics {
                lufs_m: Some(-14.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
            },
        );
        assert_eq!(r.unwrap().instance_id, "b");
    }

    // ── シーケンス統合 ──────────────────────────────────────

    #[test]
    fn full_post_to_pre_handshake_sequence() {
        let base = isolated_dir();
        // 1. POST: write pending
        let sig = write_pending(&base, "ph", "MIX", "post-1".into(), "pre-1".into()).unwrap();
        assert_eq!(sig.status, SignalStatus::Pending);
        // 2. PRE reads + acknowledges
        let seen = read_signal(&base, "ph", "MIX").unwrap();
        assert_eq!(seen.target_pre_instance_id, "pre-1");
        mark_acknowledged(&base, "ph", "MIX").unwrap();
        // 3. POST: release
        mark_released(&base, "ph", "MIX").unwrap();
        // 4. PRE: detects released → Watch + delete
        let after = read_signal(&base, "ph", "MIX").unwrap();
        assert_eq!(after.status, SignalStatus::Released);
        delete_signal(&base, "ph", "MIX").unwrap();
        assert!(read_signal(&base, "ph", "MIX").is_none());
    }
}
