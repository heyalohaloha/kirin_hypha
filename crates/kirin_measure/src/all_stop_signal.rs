//! all_stop_signal.json — POST → 同 project_hash 全 POST broadcast (α-7' / All Stop)。
//!
//! `all_keep_signal` と完全対称形。1 POST 押下 → broadcast → 同 project の全 POST が
//! `record_sm.exit_record()` + `mark_released` + `paired_pre_target=None` を実行。
//!
//! # ファイル構造
//! ```text
//! plugin_data/{project_hash}/all_stop_signal/{originator_post_instance_id}.json
//! ```
//!
//! # スキーマ (all_keep_signal と同型 / 5 フィールド)
//! - `v`: schema version (現行 1)
//! - `originator_post_instance_id`: filename stem と同値
//! - `daw_session_id`: 別 DAW process からの誤受信防止
//! - `started_at`: 重複処理回避 key (clock-skew 完全耐性 / 文字列等価比較)
//! - `heartbeat`: 将来 throttled re-publish 用 (当面 started_at と同値)
//!
//! # 受信側 polling (sub-tick)
//! 1. [`scan_stop_broadcasts_dir`] で `{project_hash}/all_stop_signal/*.json` 全件読込
//! 2. `broadcast.daw_session_id == self.daw_session_id` filter (cross-process 防壁)
//! 3. memory cache `HashMap<originator_iid, started_at>` で既処理 skip
//! 4. 新 broadcast 検出 → `trigger_stop_internal(toast=None)` 発火
//!
//! # originator 側ライフサイクル
//! 1. All Stop ボタン押下 → [`write_stop_broadcast`] 配置
//! 2. originator 自身の `trigger_stop_internal` も同 frame で発火
//! 3. `record_sm.exit_record()` / `Drop` / IO Thread shutdown のいずれかで
//!    [`delete_stop_broadcast`] 呼出 (3 統合点 / all_keep_signal と完全対称)
//! 4. orphan broadcast は 30 秒 stale fallback で ignore

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// `all_stop_signal/` ディレクトリ名。`exclusion::check_record_exclusion_at` の
/// 予約名 list に追加して instance_id dir 走査から除外する。
pub const ALL_STOP_SIGNAL_SUBDIR: &str = "all_stop_signal";

/// broadcast schema 現行 version。
pub const ALL_STOP_SCHEMA_VERSION: u32 = 1;

/// stale 判定閾値 (秒)。30 秒経過 broadcast は受信側 cache 検出時非発火。
pub const ALL_STOP_BROADCAST_STALE_SECS: i64 = 30;

// ── スキーマ ─────────────────────────────────────────────────────────────────

/// all_stop_signal.json ルート構造 (5 field / `AllKeepBroadcast` と同型)。
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AllStopBroadcast {
    pub v: u32,
    pub originator_post_instance_id: String,
    pub daw_session_id: String,
    pub started_at: String,
    #[serde(default)]
    pub heartbeat: String,
}

impl AllStopBroadcast {
    pub fn new(originator_post_instance_id: String, daw_session_id: String) -> Self {
        let now = now_iso8601();
        Self {
            v: ALL_STOP_SCHEMA_VERSION,
            originator_post_instance_id,
            daw_session_id,
            started_at: now.clone(),
            heartbeat: now,
        }
    }
}

// ── パス構築 ─────────────────────────────────────────────────────────────────

/// `{base}/{project_hash}/all_stop_signal/` ディレクトリ。
pub fn stop_signals_dir(base_dir: &Path, project_hash: &str) -> PathBuf {
    // B-128 (G-115-370): within-base wall。stop_signal_path も本関数経由で project_hash を guard。
    let ph = crate::path_identity::guard_path_component(project_hash, "all_stop_signal.stop_signals_dir.project_hash");
    base_dir.join(&*ph).join(ALL_STOP_SIGNAL_SUBDIR)
}

/// `{base}/{project_hash}/all_stop_signal/{originator_post_instance_id}.json`。
pub fn stop_signal_path(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
) -> PathBuf {
    let iid = crate::path_identity::guard_path_component(originator_post_instance_id, "all_stop_signal.stop_signal_path.originator");
    stop_signals_dir(base_dir, project_hash).join(format!("{iid}.json"))
}

fn tmp_path(final_path: &Path) -> PathBuf {
    let mut s = final_path.as_os_str().to_os_string();
    s.push(".tmp");
    PathBuf::from(s)
}

// ── I/O ──────────────────────────────────────────────────────────────────────

#[derive(Debug)]
pub enum AllStopError {
    Io(io::Error),
    Serde(serde_json::Error),
}

impl std::fmt::Display for AllStopError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "IO error: {e}"),
            Self::Serde(e) => write!(f, "JSON error: {e}"),
        }
    }
}

impl std::error::Error for AllStopError {}

impl From<io::Error> for AllStopError {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl From<serde_json::Error> for AllStopError {
    fn from(e: serde_json::Error) -> Self {
        Self::Serde(e)
    }
}

/// broadcast を atomic 書込 (`.tmp` → rename)。親ディレクトリが無ければ作成。
pub fn write_stop_broadcast(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
    daw_session_id: String,
) -> Result<AllStopBroadcast, AllStopError> {
    let broadcast =
        AllStopBroadcast::new(originator_post_instance_id.to_string(), daw_session_id);
    write_stop_broadcast_signal(base_dir, project_hash, originator_post_instance_id, &broadcast)?;
    Ok(broadcast)
}

/// 任意の broadcast を atomic 書込。
pub fn write_stop_broadcast_signal(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
    broadcast: &AllStopBroadcast,
) -> Result<(), AllStopError> {
    let final_path = stop_signal_path(base_dir, project_hash, originator_post_instance_id);
    if let Some(parent) = final_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let tmp = tmp_path(&final_path);
    let json = serde_json::to_vec(broadcast)?;
    fs::write(&tmp, json)?;
    fs::rename(&tmp, &final_path)?;
    Ok(())
}

/// broadcast 読込。存在しない / パース失敗時は None。
pub fn read_stop_broadcast(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
) -> Option<AllStopBroadcast> {
    let path = stop_signal_path(base_dir, project_hash, originator_post_instance_id);
    let bytes = fs::read(&path).ok()?;
    serde_json::from_slice(&bytes).ok()
}

/// broadcast file を削除。不在は成功扱い (R-28 機能的沈黙)。
///
/// 統合点: trigger_stop / HyphaPost::drop / IO Thread shutdown の 3 site で並列呼出。
pub fn delete_stop_broadcast(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
) -> Result<(), AllStopError> {
    let path = stop_signal_path(base_dir, project_hash, originator_post_instance_id);
    match fs::remove_file(&path) {
        Ok(()) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(e) => Err(AllStopError::Io(e)),
    }
}

/// `{project_hash}/all_stop_signal/` 配下の `*.json` を全件読み込んで返す。
///
/// 各要素は `(originator_post_instance_id, AllStopBroadcast)`。
/// パース不能 / I/O 失敗ファイルは silently skip。返値は instance_id 辞書順。
pub fn scan_stop_broadcasts_dir(
    base_dir: &Path,
    project_hash: &str,
) -> Vec<(String, AllStopBroadcast)> {
    let dir = stop_signals_dir(base_dir, project_hash);
    let entries = match fs::read_dir(&dir) {
        Ok(e) => e,
        Err(e) => {
            if e.kind() == io::ErrorKind::NotFound {
                log::debug!(
                    "[scan_stop_broadcasts_dir] dir not found: {}",
                    dir.display()
                );
            } else {
                log::warn!(
                    "[scan_stop_broadcasts_dir] read_dir failed: {} (kind={:?}, err={})",
                    dir.display(),
                    e.kind(),
                    e
                );
            }
            return Vec::new();
        }
    };
    let mut out: Vec<(String, AllStopBroadcast)> = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|s| s.to_str()) != Some("json") {
            continue;
        }
        let Some(stem) = path
            .file_stem()
            .and_then(|s| s.to_str())
            .map(str::to_string)
        else {
            continue;
        };
        let bytes = match fs::read(&path) {
            Ok(b) => b,
            Err(e) => {
                log::warn!(
                    "[scan_stop_broadcasts_dir] file read failed: {} (err={})",
                    path.display(),
                    e
                );
                continue;
            }
        };
        match serde_json::from_slice::<AllStopBroadcast>(&bytes) {
            Ok(broadcast) => out.push((stem, broadcast)),
            Err(e) => {
                log::warn!(
                    "[scan_stop_broadcasts_dir] parse failed: {} (err={})",
                    path.display(),
                    e
                );
                continue;
            }
        }
    }
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out
}

/// `started_at` から `now` まで `stale_secs` 秒以上経過していれば true。
pub fn is_stop_broadcast_stale(
    broadcast: &AllStopBroadcast,
    now: DateTime<Utc>,
    stale_secs: i64,
) -> bool {
    let started = match DateTime::parse_from_rfc3339(&broadcast.started_at) {
        Ok(d) => d.with_timezone(&Utc),
        Err(_) => return false,
    };
    let age = now.signed_duration_since(started).num_seconds();
    if age < 0 {
        return false;
    }
    age > stale_secs
}

fn now_iso8601() -> String {
    chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%SZ")
        .to_string()
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
        let dir = std::env::temp_dir().join(format!("kirin_all_stop_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn stop_signals_dir_path_format() {
        let base = PathBuf::from("/base");
        let dir = stop_signals_dir(&base, "ph-A");
        assert_eq!(dir, PathBuf::from("/base/ph-A/all_stop_signal"));
    }

    #[test]
    fn stop_signal_path_format() {
        let base = PathBuf::from("/base");
        let path = stop_signal_path(&base, "ph-A", "iid-X");
        assert_eq!(
            path,
            PathBuf::from("/base/ph-A/all_stop_signal/iid-X.json")
        );
    }

    #[test]
    fn write_stop_broadcast_creates_atomic_file() {
        let base = isolated_dir();
        let result =
            write_stop_broadcast(&base, "ph", "originator-1", "session-A".to_string()).unwrap();
        assert_eq!(result.v, ALL_STOP_SCHEMA_VERSION);
        assert_eq!(result.originator_post_instance_id, "originator-1");
        assert_eq!(result.daw_session_id, "session-A");
        let path = stop_signal_path(&base, "ph", "originator-1");
        assert!(path.exists());
    }

    #[test]
    fn delete_stop_broadcast_removes_file() {
        let base = isolated_dir();
        write_stop_broadcast(&base, "ph", "originator-1", "session-A".to_string()).unwrap();
        let path = stop_signal_path(&base, "ph", "originator-1");
        assert!(path.exists());
        delete_stop_broadcast(&base, "ph", "originator-1").unwrap();
        assert!(!path.exists());
    }

    #[test]
    fn delete_stop_broadcast_missing_is_ok() {
        let base = isolated_dir();
        assert!(delete_stop_broadcast(&base, "ph", "no-such-originator").is_ok());
    }

    #[test]
    fn scan_stop_broadcasts_dir_returns_all_valid() {
        let base = isolated_dir();
        write_stop_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
        write_stop_broadcast(&base, "ph", "originator-B", "session-A".to_string()).unwrap();
        let v = scan_stop_broadcasts_dir(&base, "ph");
        assert_eq!(v.len(), 2);
        assert_eq!(v[0].0, "originator-A");
        assert_eq!(v[1].0, "originator-B");
    }

    #[test]
    fn stop_broadcast_persists_across_scan_calls() {
        // 寿命仕様 = originator が Stop/Drop/Shutdown を実行するまで保持。
        let base = isolated_dir();
        write_stop_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
        let path = stop_signal_path(&base, "ph", "originator-A");
        for i in 0..3 {
            let v = scan_stop_broadcasts_dir(&base, "ph");
            assert_eq!(v.len(), 1, "scan #{} で 1 件残存", i);
            assert!(path.exists());
        }
        delete_stop_broadcast(&base, "ph", "originator-A").unwrap();
        assert!(!path.exists());
    }

    #[test]
    fn stop_broadcast_independent_of_keep_broadcast() {
        // α-7' Step 6 主バグ修正後の lifecycle 不変条件:
        // all_keep_signal の delete (#2) は all_stop_signal を touch しない。
        // all_stop_signal の lifecycle は #3 Drop / #4 IO Thread shutdown のみで管理。
        use crate::all_keep_signal;
        let base = isolated_dir();
        // 同一 originator が両 broadcast を配置 (All Keep → All Stop の連続押下シナリオ)
        all_keep_signal::write_broadcast(&base, "ph", "originator-A", "session-A".to_string())
            .unwrap();
        write_stop_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
        let keep_path = all_keep_signal::signal_path(&base, "ph", "originator-A");
        let stop_path = stop_signal_path(&base, "ph", "originator-A");
        assert!(keep_path.exists());
        assert!(stop_path.exists());

        // #2 (trigger_stop) は all_keep_signal::delete_broadcast のみ呼ぶ → all_stop_signal 不触
        all_keep_signal::delete_broadcast(&base, "ph", "originator-A").unwrap();
        assert!(!keep_path.exists(), "keep broadcast deleted (#2)");
        assert!(stop_path.exists(), "stop broadcast preserved (主バグ修正後)");

        // #3/#4 で初めて all_stop_signal が削除される
        delete_stop_broadcast(&base, "ph", "originator-A").unwrap();
        assert!(!stop_path.exists());
    }
}
