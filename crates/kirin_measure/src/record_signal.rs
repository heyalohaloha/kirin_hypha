//! record_signal.json — POST → PRE 自動追従シグナル（G-50-34 / G-50-35）。
//!
//! # ファイル構造
//! ```text
//! plugin_data/{project_hash}/record_signal/{post_instance_id}.json
//! ```
//! 旧 `{project_hash}/{bus}/record_signal.json` から移行。bus 概念を path から外し、
//! POST インスタンスごとに 1 ファイル。同一 project_hash 内に複数 POST が居ても
//! ファイル名で区別できる。
//!
//! # スキーマ
//! ```json
//! {
//!   "status": "pending | acknowledged | released",
//!   "requested_by": "post_instance_id (= filename stem)",
//!   "target_pre_instance_id": "PRE 永続 instance_id（ペアリング結果）",
//!   "daw_session_id": "DAW プロセス UUID（cross-process 防壁 / Q1 補強）",
//!   "session_id": "PRE/POST が同じ Record session を共有するための UUID",
//!   "t": "ISO 8601（最終遷移時刻。status 更新で書き換わる）",
//!   "started_at": "ISO 8601（pending 配置時に固定、以後不変）"
//! }
//! ```
//!
//! `t` は status の更新時刻、`started_at` は Record 開始時刻。
//! `daw_session_id` は POST 側 [`crate::daw_session_id`] の値を保存する。PRE/POST は
//! AU/VST3 や別 cdylib 境界で `static` 状態が一致しないため、PRE 側 ack は
//! `daw_session_id` では filter せず、永続 `target_pre_instance_id` 一致を正本にする。
//!
//! # PRE 側 polling（Q1 (b) 厳格化）
//! 1. [`scan_signals_dir`] で `{project_hash}/record_signal/*.json` を全件読み込む
//! 2. 各 signal について以下を満たすもののみ処理:
//!    - `signal.target_pre_instance_id == self.instance_id`
//! 3. 1 つも条件一致がなければ Record 状態を維持（pending 検出なしと同義）
//!
//! # POST 側 ライフサイクル
//! 1. POST「Keep」→ 排他 OK → [`write_pending`] で自身の post_instance_id 用 signal 配置
//! 2. PRE が ack → [`mark_acknowledged`]
//! 3. POST「Stop」→ [`mark_released`] → 30 秒後に自動 [`delete_signal`]
//!
//! # ペアリング距離（G-50-35）
//! PRE 候補の走査・距離選択は [`crate::pre_candidates`] が所有する。

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use uuid::Uuid;

mod stale_pending;

pub use stale_pending::{
    sweep_stale_pending_at_startup, sweep_stale_pending_in, STALE_PENDING_SECS,
};

/// pending → acknowledged タイムアウト（秒）。
pub const ACK_TIMEOUT_SECONDS: i64 = 30;

/// record_signal ディレクトリ名（`{project_hash}/record_signal/`）。
pub const SIGNALS_SUBDIR: &str = "record_signal";

/// 旧バージョン互換: 1 ファイルだけ置かれていた頃の filename。テストや残骸検出
/// にだけ参照され、新コードから書込先には使わない。
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
    /// POST インスタンス UUID。filename stem と同値。
    pub requested_by: String,
    /// ペアリングで選ばれた PRE 永続 instance_id（PRE 側 ack 条件 1）。
    pub target_pre_instance_id: String,
    /// POST 側の `daw_session_id`。PRE 側 ack filter には使わず、
    /// cross-format / cross-dylib 診断と PRE self-discovery の同期ヒントとして保持する。
    /// 旧 schema からの読込で欠落している場合は空文字で defaulted。
    #[serde(default)]
    pub daw_session_id: String,
    /// POST Keep 1 回ごとの Record session UUID。
    ///
    /// PRE / POST plugin_data に同じ値を焼き、後段が pair 単位で同じ録音か検証する。
    /// 旧 schema では空文字に default し、started_at + paired_* による互換経路を維持する。
    #[serde(default)]
    pub session_id: String,
    /// 状態遷移の最終時刻（ISO 8601 / RFC 3339, 秒精度 / UTC）。
    pub t: String,
    /// pending 配置時刻（以後 status 遷移で更新されない）。
    /// 旧バージョン互換のため `#[serde(default)]`（不在で空文字）。
    #[serde(default)]
    pub started_at: String,
    /// PRE 側 ack 時に書き込む PRE 表示用 Name (B-023 段階 3 / G-115-40 案 A-3)。
    /// 旧 schema からの読込 / POST write_pending 時は空文字で defaulted。
    /// 空 = POST GUI 側で UUID 短縮 8 文字 fallback 表示。
    #[serde(default)]
    pub paired_pre_name: String,
}

impl RecordSignal {
    /// 新規 pending シグナルを生成。`t` と `started_at` は現在時刻、
    /// `daw_session_id` は呼び出し側が責任を持って渡す。
    pub fn new_pending(
        requested_by: String,
        target_pre_instance_id: String,
        daw_session_id: String,
    ) -> Self {
        let now = now_iso8601();
        Self {
            status: SignalStatus::Pending,
            requested_by,
            target_pre_instance_id,
            daw_session_id,
            session_id: Uuid::new_v4().to_string(),
            t: now.clone(),
            started_at: now,
            paired_pre_name: String::new(),
        }
    }
}

// ── パス構築 ─────────────────────────────────────────────────────────────────

/// `{base}/{project_hash}/record_signal/` ディレクトリ。
pub fn signals_dir(base_dir: &Path, project_hash: &str) -> PathBuf {
    // B-128 (G-115-370): within-base wall。signal_path も本関数経由で project_hash を guard。
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "record_signal.signals_dir.project_hash",
    );
    base_dir.join(&*ph).join(SIGNALS_SUBDIR)
}

/// `{base}/{project_hash}/record_signal/{post_instance_id}.json`。
pub fn signal_path(base_dir: &Path, project_hash: &str, post_instance_id: &str) -> PathBuf {
    let iid = crate::path_identity::guard_path_component(
        post_instance_id,
        "record_signal.signal_path.post_instance_id",
    );
    signals_dir(base_dir, project_hash).join(format!("{iid}.json"))
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
    post_instance_id: &str,
    target_pre_instance_id: String,
    daw_session_id: String,
) -> Result<RecordSignal, SignalError> {
    let signal = RecordSignal::new_pending(
        post_instance_id.to_string(),
        target_pre_instance_id,
        daw_session_id,
    );
    write_signal(base_dir, project_hash, post_instance_id, &signal)?;
    Ok(signal)
}

/// 任意のシグナルを atomic 書込（unique tmp → rename）。
pub fn write_signal(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    signal: &RecordSignal,
) -> Result<(), SignalError> {
    let final_path = signal_path(base_dir, project_hash, post_instance_id);
    let json = serde_json::to_vec(signal)?;
    crate::atomic_file::write_bytes_atomic(&final_path, &json)?;
    Ok(())
}

/// シグナル読込。存在しない / パース失敗時は None。
pub fn read_signal(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Option<RecordSignal> {
    let path = signal_path(base_dir, project_hash, post_instance_id);
    let bytes = fs::read(&path).ok()?;
    serde_json::from_slice(&bytes).ok()
}

/// 状態遷移: pending → acknowledged。`t` は現在時刻に更新。
///
/// 既存シグナルが無い / 読込失敗時は `Ok(false)` を返す。
///
/// B-023 段階 3: 既存シグネチャは [`mark_acknowledged_with_name`] への薄い wrapper。
/// PRE Name を渡さない呼出 (テスト等) では空文字が書き込まれ、POST GUI 側で
/// UUID 短縮 8 文字 fallback 表示が適用される (R-28 機能的沈黙)。
pub fn mark_acknowledged(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Result<bool, SignalError> {
    mark_acknowledged_with_name(base_dir, project_hash, post_instance_id, "")
}

/// 状態遷移: pending → acknowledged + `paired_pre_name` を書込。
///
/// PRE 側 IO Thread が ack 時に呼ぶ正規経路 (B-023 段階 3)。`paired_pre_name`
/// は params.name の lazy-read 値 (sanitize 済 / ASCII 16 文字以内 / 空文字許容)
/// を透過コピーする。空文字渡しは [`mark_acknowledged`] 経由で起きる
/// (PRE 未設定 / 旧テスト経路)。
pub fn mark_acknowledged_with_name(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    paired_pre_name: &str,
) -> Result<bool, SignalError> {
    let Some(mut signal) = read_signal(base_dir, project_hash, post_instance_id) else {
        return Ok(false);
    };
    signal.status = SignalStatus::Acknowledged;
    signal.t = now_iso8601();
    signal.paired_pre_name = paired_pre_name.to_string();
    write_signal(base_dir, project_hash, post_instance_id, &signal)?;
    Ok(true)
}

/// 状態遷移: * → released。
pub fn mark_released(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Result<bool, SignalError> {
    transition_status(
        base_dir,
        project_hash,
        post_instance_id,
        SignalStatus::Released,
    )
}

fn transition_status(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    next: SignalStatus,
) -> Result<bool, SignalError> {
    let Some(mut signal) = read_signal(base_dir, project_hash, post_instance_id) else {
        return Ok(false);
    };
    signal.status = next;
    signal.t = now_iso8601();
    write_signal(base_dir, project_hash, post_instance_id, &signal)?;
    Ok(true)
}

/// record_signal.json を削除。不在は成功扱い。
pub fn delete_signal(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Result<(), SignalError> {
    let path = signal_path(base_dir, project_hash, post_instance_id);
    match fs::remove_file(&path) {
        Ok(()) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(e) => Err(SignalError::Io(e)),
    }
}

/// pending 状態で `t` から `timeout_secs` 秒以上経過していれば true。
///
/// pending 以外 / t パース失敗時は false（タイムアウト扱いしない）。
pub fn is_timed_out(signal: &RecordSignal, now: DateTime<Utc>, timeout_secs: i64) -> bool {
    if signal.status != SignalStatus::Pending {
        return false;
    }
    let t = match DateTime::parse_from_rfc3339(&signal.t) {
        Ok(d) => d.with_timezone(&Utc),
        Err(_) => return false,
    };
    now.signed_duration_since(t).num_seconds() > timeout_secs
}

/// `{project_hash}/record_signal/` 配下の `*.json` を全件読み込んで返す。
///
/// 各要素は `(post_instance_id, RecordSignal)`。filename stem を post_instance_id
/// として返すため、呼び出し側はファイル名と signal 内の `requested_by` の整合
/// を別途確認しなくてよい。
///
/// パース不能・I/O 失敗ファイルは silently skip。返値は post_instance_id 辞書順。
pub fn scan_signals_dir(base_dir: &Path, project_hash: &str) -> Vec<(String, RecordSignal)> {
    let dir = signals_dir(base_dir, project_hash);
    let entries = match fs::read_dir(&dir) {
        Ok(e) => e,
        // B-022 段階 5 P-1 #2: read_dir Err を WARN ログ (沈黙撤回)。
        // ENOENT (dir 不存在) は Watch 中の通常状態なので debug に落とし、
        // それ以外 (権限・FS lock 等の transient 失敗) のみ WARN で出す。
        // P-3 直接 read リトライの起点となるシグナルでもある。
        Err(e) => {
            if e.kind() == io::ErrorKind::NotFound {
                log::debug!(
                    "[scan_signals_dir] dir not found: {} (kind={:?})",
                    dir.display(),
                    e.kind()
                );
            } else {
                log::warn!(
                    "[scan_signals_dir] read_dir failed: {} (kind={:?}, err={})",
                    dir.display(),
                    e.kind(),
                    e
                );
            }
            return Vec::new();
        }
    };
    let mut out: Vec<(String, RecordSignal)> = Vec::new();
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
            // B-022 段階 5 P-1 #2 (補): 個別ファイル read 失敗も WARN。
            // atomic rename と read の race で transient に起こり得る。
            Err(e) => {
                log::warn!(
                    "[scan_signals_dir] file read failed: {} (kind={:?}, err={})",
                    path.display(),
                    e.kind(),
                    e
                );
                continue;
            }
        };
        match serde_json::from_slice::<RecordSignal>(&bytes) {
            Ok(signal) => out.push((stem, signal)),
            // B-022 段階 5 P-1 #3: serde parse 失敗を WARN ログ。
            // atomic rename と read の race で空ファイル / 部分書込を読むと
            // ここに落ちる (仮説 γ)。P-3 直接 read リトライの起点候補。
            Err(e) => {
                log::warn!(
                    "[scan_signals_dir] parse failed: {} (err={})",
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

// ── ヘルパ ────────────────────────────────────────────────────────────────────

fn now_iso8601() -> String {
    chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
}

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests;
