//! all_keep_signal.json — POST → 同 project_hash 全 POST broadcast (G-115-46 / α-7)。
//!
//! # ファイル構造 (案 2 / DEV INBOX §6)
//! ```text
//! plugin_data/{project_hash}/all_keep_signal/{originator_post_instance_id}.json
//! ```
//! originator (= All Keep を押した POST) ごとに別ファイル。並列 originator 共存可
//! (β-3 衝突許容)。同一 originator が連打しても同 path に atomic rename で last-wins。
//!
//! # スキーマ
//! ```json
//! {
//!   "v": 1,
//!   "originator_post_instance_id": "POST 永続 instance_id (= filename stem)",
//!   "daw_session_id": "DAW プロセス UUID (cross-process 防壁)",
//!   "host_process_id": "DAW host process ID (same-project bridge when DAW identity is instance-scoped)",
//!   "started_at": "ISO 8601 (broadcast 配置時刻 / 重複処理回避 key)",
//!   "heartbeat": "ISO 8601 (将来 throttled re-publish 用 / 当面は started_at と同値)"
//! }
//! ```
//!
//! `started_at` は originator が `now_iso8601()` で書き込み、受信側は文字列等価比較
//! のみで「同 originator + 同 broadcast」を判別する (clock-skew 完全耐性 / Q-A8-6)。
//!
//! 1. [`read_current_broadcast`] で `{project_hash}/all_keep_signal/current.json` を1件読込
//! 2. `daw_session_id` を主境界として filter。両側 nonempty で一致すれば通し、
//!    同一 project shelf 内では instance-scoped DAW ID を `host_process_id` で橋渡しする。
//! 3. memory cache `HashMap<originator_iid, started_at>` を引いて
//!    - 未登録 / 値が異なる → 新 broadcast → 自身の `trigger_keep_internal(toast=None)` 発火
//!    - 登録済 + 値が一致 → 既処理 skip (file mutation race を構造的に回避)
//!
//! # originator 側ライフサイクル
//! 1. ComboBox 先頭行 "All Keep: N ready POST(s)" 押下 → [`write_broadcast`] で配置
//! 2. originator 自身の `trigger_keep_internal` も同 frame で発火 (cache に self seed)
//! 3. `record_sm.exit_record()` / `Drop` / IO Thread shutdown のいずれかで
//! 4. orphan broadcast は受信側 cache 30 秒 stale fallback で ignore (即時 delete は
//!    受信側pollとの race のため不採用)
//!
//! # cdylib 越境通信制約
//! filesystem 経由のみ (`OnceLock` 不触 / 申し送り #22 適合)。同 OS process 内の
//! 別 cdylib (PRE.vst3 / POST.vst3) も `$HOME` 共有で `plugin_data_dir` の値が一致
//! するため、filesystem path レベルで cross-instance 通信が成立する (G-115-49 撤回
//! 根拠と整合)。

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// `all_keep_signal/` ディレクトリ名。`exclusion::check_record_exclusion_at` の
/// 予約名 list に追加して instance_id dir 走査から除外する (α-7-4-B)。
pub const ALL_KEEP_SIGNAL_SUBDIR: &str = "all_keep_signal";
pub const CURRENT_BROADCAST_FILENAME: &str = "current.json";

/// migration は `#[serde(default)]` で旧 schema 互換維持)。
pub const ALL_KEEP_SCHEMA_VERSION: u32 = 2;

/// broadcast の stale 判定閾値 (秒)。30 秒経過 broadcast は受信側 cache 検出時
/// ACK_TIMEOUT_SECONDS` (= 30) と同値で対称性を維持。
pub const ALL_KEEP_BROADCAST_STALE_SECS: i64 = 30;

// ── スキーマ ─────────────────────────────────────────────────────────────────

/// all_keep_signal.json ルート構造。
///
/// # フィールド
/// - `v`: schema version (現行 2)
/// - `originator_post_instance_id`: filename stem と同値 / check 用
/// - `daw_session_id`: 別 DAW process からの誤受信防止 (record_signal と同位相)
/// - `host_process_id`: 同一 project shelf 内で instance-scoped DAW ID を橋渡しする補助 scope
/// - `capture_generation_id`: generation-aware受信側の重複処理回避 key
/// - `started_at`: legacy互換とAll Stopとの順序判定だけに使用
/// - `heartbeat`: 将来 throttled re-publish 用 / 当面は `started_at` と同値で書込
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AllKeepBroadcast {
    pub v: u32,
    pub originator_post_instance_id: String,
    pub daw_session_id: String,
    #[serde(default)]
    pub host_process_id: u32,
    /// One producer transaction shared by every POST armed by this All Keep.
    /// Empty only when reading a legacy v1 broadcast; legacy broadcasts are not
    /// eligible to create a new generation-aware Record.
    #[serde(default)]
    pub capture_generation_id: String,
    #[serde(default)]
    pub generation_started_at_ms: i64,
    pub started_at: String,
    /// 旧 schema 互換のため `#[serde(default)]`（不在で空文字）。
    /// 当面は `started_at` と同値で書込される。
    #[serde(default)]
    pub heartbeat: String,
}

impl AllKeepBroadcast {
    fn new_for_generation(
        originator_post_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
        generation: &crate::capture_generation::CaptureGeneration,
    ) -> Self {
        let now = now_iso8601();
        Self {
            v: ALL_KEEP_SCHEMA_VERSION,
            originator_post_instance_id,
            daw_session_id,
            host_process_id,
            capture_generation_id: generation.capture_generation_id.clone(),
            generation_started_at_ms: generation.started_at_ms,
            started_at: now.clone(),
            heartbeat: now,
        }
    }
}

// ── パス構築 ─────────────────────────────────────────────────────────────────

/// `{base}/{project_hash}/all_keep_signal/` ディレクトリ。
pub fn signals_dir(base_dir: &Path, project_hash: &str) -> PathBuf {
    // B-128 (G-115-370): within-base wall。signal_path も本関数経由で project_hash を guard。
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "all_keep_signal.signals_dir.project_hash",
    );
    base_dir.join(&*ph).join(ALL_KEEP_SIGNAL_SUBDIR)
}

/// `{base}/{project_hash}/all_keep_signal/{originator_post_instance_id}.json`。
pub fn signal_path(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
) -> PathBuf {
    let iid = crate::path_identity::guard_path_component(
        originator_post_instance_id,
        "all_keep_signal.signal_path.originator",
    );
    signals_dir(base_dir, project_hash).join(format!("{iid}.json"))
}

/// One project-local commit pointer read by every POST. Its payload contains the originator,
/// so consumers never enumerate historical originator files.
pub fn current_broadcast_path(base_dir: &Path, project_hash: &str) -> PathBuf {
    signals_dir(base_dir, project_hash).join(CURRENT_BROADCAST_FILENAME)
}

// ── I/O ──────────────────────────────────────────────────────────────────────

/// I/O / serde エラー (record_signal::SignalError と同パターン)。
#[derive(Debug)]
pub enum AllKeepError {
    Io(io::Error),
    Serde(serde_json::Error),
}

impl std::fmt::Display for AllKeepError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "IO error: {e}"),
            Self::Serde(e) => write!(f, "JSON error: {e}"),
        }
    }
}

impl std::error::Error for AllKeepError {}

impl From<io::Error> for AllKeepError {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl From<serde_json::Error> for AllKeepError {
    fn from(e: serde_json::Error) -> Self {
        Self::Serde(e)
    }
}

/// Stage a broadcast on one project shelf for a generation transaction. All Keep callers write
/// every member shelf first and promote the installation-wide active pointer only afterwards.
pub fn write_broadcast_for_generation(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
    daw_session_id: String,
    host_process_id: u32,
    generation: &crate::capture_generation::CaptureGeneration,
) -> Result<AllKeepBroadcast, AllKeepError> {
    if !generation.is_valid() {
        return Err(AllKeepError::Io(io::Error::new(
            io::ErrorKind::InvalidData,
            "invalid capture generation",
        )));
    }
    let project_is_member = generation
        .members
        .iter()
        .any(|member| member.project_hash == project_hash);
    if !project_is_member
        || generation.originator_post_instance_id != originator_post_instance_id
        || generation.daw_session_id != daw_session_id
        || generation.host_process_id != host_process_id
    {
        return Err(AllKeepError::Io(io::Error::new(
            io::ErrorKind::InvalidData,
            "All Keep broadcast does not match capture generation",
        )));
    }
    let broadcast = AllKeepBroadcast::new_for_generation(
        originator_post_instance_id.to_string(),
        daw_session_id,
        host_process_id,
        generation,
    );
    write_broadcast_signal(
        base_dir,
        project_hash,
        originator_post_instance_id,
        &broadcast,
    )?;
    Ok(broadcast)
}

/// 任意の broadcast を atomic 書込 (unique tmp → rename)。
fn write_broadcast_signal(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
    broadcast: &AllKeepBroadcast,
) -> Result<(), AllKeepError> {
    let final_path = signal_path(base_dir, project_hash, originator_post_instance_id);
    let json = serde_json::to_vec(broadcast)?;
    crate::atomic_file::write_bytes_atomic(&final_path, &json)?;
    crate::atomic_file::write_bytes_atomic(&current_broadcast_path(base_dir, project_hash), &json)?;
    Ok(())
}

/// broadcast 読込。存在しない / パース失敗時は None。
pub fn read_broadcast(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
) -> Option<AllKeepBroadcast> {
    let path = signal_path(base_dir, project_hash, originator_post_instance_id);
    let bytes = fs::read(&path).ok()?;
    serde_json::from_slice(&bytes).ok()
}

pub fn read_current_broadcast(
    base_dir: &Path,
    project_hash: &str,
) -> Option<(String, AllKeepBroadcast)> {
    let bytes = fs::read(current_broadcast_path(base_dir, project_hash)).ok()?;
    let broadcast: AllKeepBroadcast = serde_json::from_slice(&bytes).ok()?;
    let originator = broadcast.originator_post_instance_id.trim();
    if originator.is_empty() {
        return None;
    }
    Some((originator.to_string(), broadcast))
}

/// broadcast file を削除。不在は成功扱い (R-28 機能的沈黙)。
///
/// - #2 `trigger_stop` (`record_sm.exit_record()` 直後)
/// - #3 `HyphaPost::drop` (`record_sm.exit_record()` 直後)
/// - #4 IO Thread shutdown (`fs::remove_file(post.json)` 直後)
pub fn delete_broadcast(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
) -> Result<(), AllKeepError> {
    let path = signal_path(base_dir, project_hash, originator_post_instance_id);
    match fs::remove_file(&path) {
        Ok(()) => {}
        Err(e) if e.kind() == io::ErrorKind::NotFound => {}
        Err(e) => return Err(AllKeepError::Io(e)),
    }
    let current = current_broadcast_path(base_dir, project_hash);
    let owns_current = fs::read(&current)
        .ok()
        .and_then(|bytes| serde_json::from_slice::<AllKeepBroadcast>(&bytes).ok())
        .is_some_and(|broadcast| {
            broadcast.originator_post_instance_id == originator_post_instance_id
        });
    if owns_current {
        match fs::remove_file(current) {
            Ok(()) => {}
            Err(e) if e.kind() == io::ErrorKind::NotFound => {}
            Err(e) => return Err(AllKeepError::Io(e)),
        }
    }
    Ok(())
}

/// `{project_hash}/all_keep_signal/` 配下の `*.json` を全件読み込んで返す。
///
/// 各要素は `(originator_post_instance_id, AllKeepBroadcast)`。filename stem を
/// originator_post_instance_id として返す (record_signal::scan_signals_dir と同位相)。
///
/// パース不能 / I/O 失敗ファイルは silently skip。返値は originator_post_instance_id
/// 辞書順 (受信側多重 broadcast 処理順序の決定論性 / Q-A8-6)。
///
/// 旧 plugin (本変更前 / dir 不在) 互換は `read_dir` Err → empty Vec で構造保証
/// (`pre_discovery::discover_active_pre_dirs` :181-184 と同位相)。
#[cfg(test)]
pub fn scan_broadcasts_dir(base_dir: &Path, project_hash: &str) -> Vec<(String, AllKeepBroadcast)> {
    let dir = signals_dir(base_dir, project_hash);
    let entries = match fs::read_dir(&dir) {
        Ok(e) => e,
        Err(e) => {
            // ENOENT (dir 不存在) は通常状態 (誰も All Keep を押していない) なので debug。
            // それ以外 (権限・FS lock 等の transient 失敗) のみ WARN で出す。
            if e.kind() == io::ErrorKind::NotFound {
                log::debug!(
                    "[scan_broadcasts_dir] dir not found: {} (kind={:?})",
                    dir.display(),
                    e.kind()
                );
            } else {
                log::warn!(
                    "[scan_broadcasts_dir] read_dir failed: {} (kind={:?}, err={})",
                    dir.display(),
                    e.kind(),
                    e
                );
            }
            return Vec::new();
        }
    };
    let mut out: Vec<(String, AllKeepBroadcast)> = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|s| s.to_str()) != Some("json") {
            continue;
        }
        if path.file_name().and_then(|name| name.to_str()) == Some(CURRENT_BROADCAST_FILENAME) {
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
                    "[scan_broadcasts_dir] file read failed: {} (kind={:?}, err={})",
                    path.display(),
                    e.kind(),
                    e
                );
                continue;
            }
        };
        match serde_json::from_slice::<AllKeepBroadcast>(&bytes) {
            Ok(broadcast) => out.push((stem, broadcast)),
            Err(e) => {
                log::warn!(
                    "[scan_broadcasts_dir] parse failed: {} (err={})",
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
///
/// パース失敗 / 未来時刻の場合は false (= 新鮮扱い / 安全側で誤 skip を避ける)。
/// 受信側 cache 検出時、`true` なら cache 登録のみ + `trigger_keep_internal` 非発火
pub fn is_broadcast_stale(
    broadcast: &AllKeepBroadcast,
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

// ── ヘルパ ────────────────────────────────────────────────────────────────────

fn now_iso8601() -> String {
    chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
}

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
#[path = "all_keep_signal_tests.rs"]
mod tests;
