//! all_stop_signal.json — POST → 同 project_hash 全 POST broadcast (α-7' / All Stop)。
//!
//! 1 POST 押下 → broadcast → 同 project の全 POST が `record_sm.exit_record()` +
//! `mark_released` を実行する。Stop は Record session の終了であり、pair selection は
//! 維持する。
//!
//! # ファイル構造
//! ```text
//! plugin_data/{project_hash}/all_stop_signal/{originator_post_instance_id}.json
//! ```
//!
//! # スキーマ (all_keep_signal と同型)
//! - `v`: schema version (現行 2)
//! - `originator_post_instance_id`: filename stem と同値
//! - `daw_session_id`: 別 DAW process からの誤受信防止
//! - `host_process_id`: 同一 project shelf 内で instance-scoped DAW ID を橋渡しする補助 scope
//! - `started_at`: 重複処理回避 key (clock-skew 完全耐性 / 文字列等価比較)
//! - `heartbeat`: 将来 throttled re-publish 用 (当面 started_at と同値)
//!
//! # 受信側 polling (sub-tick)
//! 1. [`read_current_stop_broadcast`] で `{project_hash}/all_stop_signal/current.json` を1件読込
//! 2. `daw_session_id` を主境界として filter。両側 nonempty で一致すれば通し、
//!    同一 project shelf 内では instance-scoped DAW ID を `host_process_id` で橋渡しする。
//! 3. memory cache `HashMap<originator_iid, started_at>` で既処理 skip
//! 4. 新 broadcast 検出 → `trigger_stop_internal(toast=None)` 発火
//!
//! # originator 側ライフサイクル
//! 1. All Stop ボタン押下 → [`write_stop_broadcast`] 配置
//! 2. originator 自身の `trigger_stop_internal` も同 frame で発火
//! 3. `Drop` / IO Thread shutdown で [`delete_stop_broadcast`] 呼出
//! 4. generation-addressed broadcast は永続 terminal fact で判定し、30秒では失効しない。
//!    generation を持たない旧版 broadcast だけ30秒 stale fallbackでignoreする。

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// `all_stop_signal/` ディレクトリ名。`exclusion::check_record_exclusion_at` の
/// 予約名 list に追加して instance_id dir 走査から除外する。
pub const ALL_STOP_SIGNAL_SUBDIR: &str = "all_stop_signal";
pub const CURRENT_STOP_FILENAME: &str = "current.json";

/// broadcast schema 現行 version。
pub const ALL_STOP_SCHEMA_VERSION: u32 = 2;

/// 旧版（generationなし）broadcastだけに適用する互換stale閾値。現行All Stopの権限は
/// generation terminal factであり、経過時間には依存しない。
pub const ALL_STOP_BROADCAST_STALE_SECS: i64 = 30;

// ── スキーマ ─────────────────────────────────────────────────────────────────

/// all_stop_signal.json ルート構造 (`AllKeepBroadcast` と同型)。
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AllStopBroadcast {
    pub v: u32,
    pub originator_post_instance_id: String,
    pub daw_session_id: String,
    #[serde(default)]
    pub host_process_id: u32,
    #[serde(default)]
    pub capture_generation_id: String,
    #[serde(default)]
    pub generation_started_at_ms: i64,
    pub started_at: String,
    #[serde(default)]
    pub heartbeat: String,
}

impl AllStopBroadcast {
    pub fn new(originator_post_instance_id: String, daw_session_id: String) -> Self {
        Self::new_with_scope(
            originator_post_instance_id,
            daw_session_id,
            std::process::id(),
        )
    }

    pub fn new_with_scope(
        originator_post_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
    ) -> Self {
        let now = now_iso8601();
        Self {
            v: ALL_STOP_SCHEMA_VERSION,
            originator_post_instance_id,
            daw_session_id,
            host_process_id,
            capture_generation_id: String::new(),
            generation_started_at_ms: 0,
            started_at: now.clone(),
            heartbeat: now,
        }
    }

    fn new_for_generation(
        originator_post_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
        generation: &crate::capture_generation::CaptureGeneration,
    ) -> Self {
        let now = now_iso8601();
        Self {
            v: ALL_STOP_SCHEMA_VERSION,
            originator_post_instance_id,
            daw_session_id,
            host_process_id,
            capture_generation_id: generation.capture_generation_id.clone(),
            generation_started_at_ms: generation.started_at_ms,
            started_at: now.clone(),
            heartbeat: now,
        }
    }

    pub fn has_generation(&self) -> bool {
        !self.capture_generation_id.trim().is_empty() && self.generation_started_at_ms > 0
    }
}

// ── パス構築 ─────────────────────────────────────────────────────────────────

/// `{base}/{project_hash}/all_stop_signal/` ディレクトリ。
pub fn stop_signals_dir(base_dir: &Path, project_hash: &str) -> PathBuf {
    // B-128 (G-115-370): within-base wall。stop_signal_path も本関数経由で project_hash を guard。
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "all_stop_signal.stop_signals_dir.project_hash",
    );
    base_dir.join(&*ph).join(ALL_STOP_SIGNAL_SUBDIR)
}

/// `{base}/{project_hash}/all_stop_signal/{originator_post_instance_id}.json`。
pub fn stop_signal_path(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
) -> PathBuf {
    let iid = crate::path_identity::guard_path_component(
        originator_post_instance_id,
        "all_stop_signal.stop_signal_path.originator",
    );
    stop_signals_dir(base_dir, project_hash).join(format!("{iid}.json"))
}

pub fn current_stop_path(base_dir: &Path, project_hash: &str) -> PathBuf {
    stop_signals_dir(base_dir, project_hash).join(CURRENT_STOP_FILENAME)
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

/// broadcast を atomic 書込 (unique tmp → rename)。親ディレクトリが無ければ作成。
pub fn write_stop_broadcast(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
    daw_session_id: String,
) -> Result<AllStopBroadcast, AllStopError> {
    write_stop_broadcast_with_scope(
        base_dir,
        project_hash,
        originator_post_instance_id,
        daw_session_id,
        std::process::id(),
    )
}

pub fn write_stop_broadcast_with_scope(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
    daw_session_id: String,
    host_process_id: u32,
) -> Result<AllStopBroadcast, AllStopError> {
    let broadcast = AllStopBroadcast::new_with_scope(
        originator_post_instance_id.to_string(),
        daw_session_id,
        host_process_id,
    );
    write_stop_broadcast_signal(
        base_dir,
        project_hash,
        originator_post_instance_id,
        &broadcast,
    )?;
    Ok(broadcast)
}

/// Write a Stop addressed to one immutable generation on one exact roster shelf.
pub fn write_stop_broadcast_for_generation(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
    daw_session_id: String,
    host_process_id: u32,
    generation: &crate::capture_generation::CaptureGeneration,
) -> Result<AllStopBroadcast, AllStopError> {
    if !generation.is_valid()
        || generation.daw_session_id != daw_session_id
        || generation.host_process_id != host_process_id
        || !generation
            .members
            .iter()
            .any(|member| member.project_hash == project_hash)
        || !generation
            .members
            .iter()
            .any(|member| member.post_instance_id == originator_post_instance_id)
    {
        return Err(AllStopError::Io(io::Error::new(
            io::ErrorKind::InvalidData,
            "All Stop broadcast does not match capture generation",
        )));
    }
    let broadcast = AllStopBroadcast::new_for_generation(
        originator_post_instance_id.to_string(),
        daw_session_id,
        host_process_id,
        generation,
    );
    write_stop_broadcast_signal(
        base_dir,
        project_hash,
        originator_post_instance_id,
        &broadcast,
    )?;
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
    let json = serde_json::to_vec(broadcast)?;
    crate::atomic_file::write_bytes_atomic(&final_path, &json)?;
    crate::atomic_file::write_bytes_atomic(&current_stop_path(base_dir, project_hash), &json)?;
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

pub fn read_current_stop_broadcast(
    base_dir: &Path,
    project_hash: &str,
) -> Option<(String, AllStopBroadcast)> {
    let bytes = fs::read(current_stop_path(base_dir, project_hash)).ok()?;
    let broadcast: AllStopBroadcast = serde_json::from_slice(&bytes).ok()?;
    let originator = broadcast.originator_post_instance_id.trim();
    if originator.is_empty() {
        return None;
    }
    Some((originator.to_string(), broadcast))
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
        Ok(()) => {}
        Err(e) if e.kind() == io::ErrorKind::NotFound => {}
        Err(e) => return Err(AllStopError::Io(e)),
    }
    let current = current_stop_path(base_dir, project_hash);
    let owns_current = fs::read(&current)
        .ok()
        .and_then(|bytes| serde_json::from_slice::<AllStopBroadcast>(&bytes).ok())
        .is_some_and(|broadcast| {
            broadcast.originator_post_instance_id == originator_post_instance_id
        });
    if owns_current {
        match fs::remove_file(current) {
            Ok(()) => {}
            Err(e) if e.kind() == io::ErrorKind::NotFound => {}
            Err(e) => return Err(AllStopError::Io(e)),
        }
    }
    Ok(())
}

/// `{project_hash}/all_stop_signal/` 配下の `*.json` を全件読み込んで返す。
///
/// 各要素は `(originator_post_instance_id, AllStopBroadcast)`。
/// パース不能 / I/O 失敗ファイルは silently skip。返値は instance_id 辞書順。
#[cfg(test)]
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
        if path.file_name().and_then(|name| name.to_str()) == Some(CURRENT_STOP_FILENAME) {
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
        .format("%Y-%m-%dT%H:%M:%S%.3fZ")
        .to_string()
}

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
#[path = "all_stop_signal_tests.rs"]
mod tests;
