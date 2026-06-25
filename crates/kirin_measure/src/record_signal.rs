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
//!   "t": "ISO 8601（最終遷移時刻。status 更新で書き換わる）",
//!   "started_at": "ISO 8601（pending 配置時に固定、以後不変）"
//! }
//! ```
//!
//! `t` は status の更新時刻、`started_at` は Record 開始時刻。
//! `daw_session_id` は POST 側 [`crate::daw_session_id`] の値で、PRE 側で
//! 自身の `daw_session_id` と一致しない signal は **必ず無視する** こと
//! （別 DAW プロセスからの誤 ack 防止）。
//!
//! # PRE 側 polling（Q1 (b) 厳格化）
//! 1. [`scan_signals_dir`] で `{project_hash}/record_signal/*.json` を全件読み込む
//! 2. 各 signal について以下を **両方** 満たすもののみ処理:
//!    - `signal.target_pre_instance_id == self.instance_id`
//!    - `signal.daw_session_id == self.daw_session_id`
//! 3. 1 つも条件一致がなければ Record 状態を維持（pending 検出なしと同義）
//!
//! # POST 側 ライフサイクル
//! 1. POST「Keep」→ 排他 OK → [`write_pending`] で自身の post_instance_id 用 signal 配置
//! 2. PRE が ack → [`mark_acknowledged`]
//! 3. POST「Stop」→ [`mark_released`] → 30 秒後に自動 [`delete_signal`]
//!
//! # ペアリング距離（G-50-35）
//! `/tmp/kirin/{project_hash}/{instance_id}/pre.json` を全 instance_id 横断で走査。
//! distance = `|ΔLUFS| + |ΔTP| + |ΔCrest|` 最小を選択（同値は instance_id 辞書順先頭）。

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

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
    /// POST 側の `daw_session_id`。PRE 側 ack 条件 2（cross-process 防壁）。
    /// 旧 schema からの読込で欠落している場合は空文字で defaulted。
    #[serde(default)]
    pub daw_session_id: String,
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

/// 任意のシグナルを atomic 書込（`.tmp` → rename）。
pub fn write_signal(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    signal: &RecordSignal,
) -> Result<(), SignalError> {
    let final_path = signal_path(base_dir, project_hash, post_instance_id);
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

/// B-103: 起動時に掃除する dead Pending record_signal のしきい値（秒）。
///
/// 生きている POST は `ACK_TIMEOUT_SECONDS`(30s) で自身の Pending を `mark_released` する
/// （io_thread_post の poll_ack_timeout）。よってそれを大きく超えて Pending のまま残るファイルは
/// 書込 POST の消失（クラッシュ / DAW 終了 / target PRE 不在のまま放置）とみなせる。生記録の
/// 誤掃除を避けるため、自 release(30s) の 4 倍を保守的しきい値とする。
pub const STALE_PENDING_SECS: i64 = ACK_TIMEOUT_SECONDS * 4;

/// B-103: `plugin_data_root` 配下の全 project の `record_signal/` を走査し、status==Pending
/// かつ age(`now` - `t`) > `stale_secs` のファイルだけを削除する（起動時 dead-pending 掃除）。
///
/// 保守条件: **Pending のみ**（Acknowledged / Released は対象外）かつ **`stale_secs` 超過のみ**
/// （fresh Pending = 進行中の Keep は保持）。`t` が parse 不能なものは安全側で保持。戻り = 削除件数。
/// テスト容易性のため `plugin_data_root` / `now` を注入する純粋ロジック版。
pub fn sweep_stale_pending_in(
    plugin_data_root: &Path,
    now: DateTime<Utc>,
    stale_secs: i64,
) -> usize {
    let project_entries = match fs::read_dir(plugin_data_root) {
        Ok(e) => e,
        Err(_) => return 0,
    };
    let mut cleared: usize = 0;
    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        let project_hash = match project_dir.file_name().and_then(|n| n.to_str()) {
            Some(s) => s.to_string(),
            None => continue,
        };
        if project_hash.as_str() == SIGNALS_SUBDIR {
            continue;
        }
        for (post_iid, sig) in scan_signals_dir(plugin_data_root, &project_hash) {
            if sig.status != SignalStatus::Pending {
                continue;
            }
            let stale = match DateTime::parse_from_rfc3339(&sig.t) {
                Ok(t) => {
                    now.signed_duration_since(t.with_timezone(&Utc))
                        .num_seconds()
                        > stale_secs
                }
                Err(_) => false, // 安全側: parse 不能は保持
            };
            if !stale {
                continue;
            }
            if delete_signal(plugin_data_root, &project_hash, &post_iid).is_ok() {
                cleared += 1;
                log::info!(
                    "[record_signal] startup: swept stale Pending (project_hash={}, post_iid={})",
                    project_hash,
                    post_iid
                );
            }
        }
    }
    cleared
}

/// B-103: 起動時 dead-pending 掃除の production ラッパー（StoragePaths / now を解決）。
/// PRE / POST 両 io_thread の起動時に呼ぶ（age ベースなので role 非依存・冪等）。
pub fn sweep_stale_pending_at_startup() {
    let Ok(paths) = crate::storage::StoragePaths::default_macos() else {
        return;
    };
    let n = sweep_stale_pending_in(&paths.plugin_data_dir(), Utc::now(), STALE_PENDING_SECS);
    if n > 0 {
        log::info!(
            "[record_signal] startup: swept {} stale Pending signal(s)",
            n
        );
    }
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

// ── PRE ペアリング─────────────────────────────

/// `/tmp/kirin/{ph}/{instance_id}/pre.json` 1 件分のパース結果。
#[derive(Debug, Clone, PartialEq)]
pub struct PreCandidate {
    pub instance_id: String,
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
    pub crest: Option<f64>,
    pub path: PathBuf,
    /// B-027 段階 2: PRE 表示用 Name (PRE GUI で入力 / chunk-persist)。
    /// 旧 schema (`name` field 不在の PRE 出力) では `None`。
    /// POST `pair_pre_name` filter で照合する識別子。
    pub name: Option<String>,
}

/// pre tmp JSON の抜粋（distance 計算 + signal_state filter に必要なフィールドのみ）。
///
/// B-024: `signal_state` を追加。
/// B-027 段階 1: `scan_pre_candidates_in` は `"bypassed"` のみ除外する (Bypass 防御)。
/// `"active"` / `"inactive"` / 旧 schema 不在 (=None) は pair 候補化される。
/// B-027 段階 2: `name` 追加。POST `pair_pre_name` filter で照合する識別子。
/// pre.json schema バージョン (`v`) は変更しない (旧 PRE 出力との互換維持)。
#[derive(Debug, Deserialize)]
struct PreTmpJson {
    instance_id: String,
    #[serde(default)]
    lufs_m: Option<f64>,
    #[serde(default)]
    true_peak: Option<f64>,
    #[serde(default)]
    crest: Option<f64>,
    #[serde(default)]
    signal_state: Option<String>,
    #[serde(default)]
    name: Option<String>,
}

/// `/tmp/kirin/{project_hash}/{instance_id}/pre.json` を全 instance_id 横断で走査。
///
/// `tmp_base` は通常 `std::env::temp_dir().join("kirin")`。走査失敗・パース失敗は
/// silently skip。返値は instance_id 辞書順（再現性確保）。
///
/// B-021 補足: 本関数は `tmp_base.join(project_hash)` 配下のみ scan する。
/// cdylib 隔離下で POST の `project_hash` が PRE と一致しない場合、結果は空に
/// なる。新コードでは `pre_discovery::discover_active_pre_dir` で project_dir
/// を動的検出してから [`scan_pre_candidates_in`] を直接呼ぶこと。
pub fn scan_pre_candidates(tmp_base: &Path, project_hash: &str) -> Vec<PreCandidate> {
    scan_pre_candidates_in(&tmp_base.join(project_hash))
}

/// 指定された `{project_uuid}/` dir 配下の `pre.json` を走査する。
///
/// `scan_pre_candidates` 内部実装かつ B-021 Phase 1A の Keep ハンドラ修正用 API。
/// `project_dir` は `pre_discovery::discover_active_pre_dir` の戻り値（または
/// 同等 path）を渡す。`record_signal/` 予約名 dir は除外する。
pub fn scan_pre_candidates_in(project_dir: &Path) -> Vec<PreCandidate> {
    let entries = match fs::read_dir(project_dir) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };
    let mut out = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        // record_signal/ ディレクトリは除外
        if path.file_name().and_then(|n| n.to_str()) == Some(SIGNALS_SUBDIR) {
            continue;
        }
        let pre_file = path.join("pre.json");
        let Ok(bytes) = fs::read(&pre_file) else {
            continue;
        };
        // B-077: serde 失敗（破損 / 旧形式 pre.json）を無言 skip せずログに残す（沈黙をやめる）。
        // UI には出さない（R-28: 他 instance の pre.json 不整合は利用者操作と非紐づき / log のみ可視化）。
        let parsed: PreTmpJson = match serde_json::from_slice(&bytes) {
            Ok(p) => p,
            Err(e) => {
                log::warn!(
                    "[pairing] skip unparseable pre.json {}: {}",
                    pre_file.display(),
                    e
                );
                continue;
            }
        };
        // B-027: Bypassed の PRE のみ pair 候補から除外。
        // Active / Inactive / 旧 schema (signal_state 不在) は候補化する。
        // 読込側 filter (二重防御の片側)。書込側 guard は
        // io_thread_pre.rs::poll_record_signal の signal_state チェックで担保。
        if parsed.signal_state.as_deref() == Some("bypassed") {
            continue;
        }
        out.push(PreCandidate {
            instance_id: parsed.instance_id,
            lufs_m: parsed.lufs_m,
            true_peak: parsed.true_peak,
            crest: parsed.crest,
            path: pre_file,
            name: parsed.name,
        });
    }
    out.sort_by(|a, b| a.instance_id.cmp(&b.instance_id));
    out
}

/// B-027 段階 2: POST 側 `pair_pre_name` で候補を filter する。
///
/// `pair_pre_name` が空文字の場合は filter を適用せず候補をそのまま返す
/// (後方互換 = 従来 `pick_closest_pre` 単独経路)。
/// 非空の場合は `candidate.name == Some(pair_pre_name)` のみ通過させる。
///
/// 同一 Name の PRE が複数残った場合は呼出側 `pick_closest_pre` で距離最小を選ぶ。
/// 旧 schema (`name = None`) の PRE は非空 filter 時に必ず除外される
/// (新 POST + 旧 PRE 混在環境では filter 不一致 → 0 件)。
pub fn filter_candidates_by_name(
    candidates: Vec<PreCandidate>,
    pair_pre_name: &str,
) -> Vec<PreCandidate> {
    if pair_pre_name.is_empty() {
        return candidates;
    }
    candidates
        .into_iter()
        .filter(|c| c.name.as_deref() == Some(pair_pre_name))
        .collect()
}

/// B-027 段階 3-A 修正 (G-115-49 / α-2 撤回): 全 active PRE dir を flatten 列挙する
/// (POST GUI ComboBox dropdown 描画用)。
///
/// `kirin_root` 配下を [`crate::pre_discovery::discover_active_pre_dirs`] で fresh
/// 全件 Vec として取得し、各 dir の `pre.json` を [`scan_pre_candidates_in`] で
/// 候補化して flatten した `Vec<PreCandidate>` を返す。
///
/// # α-2 撤回根拠 (G-115-49)
/// 旧 `enumerate_same_project_pair_candidates` は `file_name() == project_hash`
/// で同 project_uuid 配下のみに絞っていたが、PRE.vst3 / POST.vst3 が cdylib 隔離
/// (`kirin_measure::project_uuid_cell()` の `static OnceLock` が cdylib 単位で
/// 別実体) のため、両 cdylib の `params.project_uuid` は構造的に乖離する。
/// 結果として filter は常に 0 件しか返さず、ComboBox は永遠に "No candidates" を
/// 表示する状態だった (実機検証 #5-A-3 / 2026-05-04 NG)。
///
/// 本関数は project_hash 引数を取らず、全 fresh dir を flatten する。POST 利用者は
/// Name 識別 (B-027 段階 2 の `pair_pre_name` filter) で目的 PRE を選ぶ運用となる。
///
/// `trigger_keep` の Keep 経路 (B-027 段階 2 fix) も同じ flatten 経路で一貫する。
///
/// # 戻り順
/// `discover_active_pre_dirs` の mtime 降順 dir 順 → 各 dir 内
/// `scan_pre_candidates_in` の instance_id 辞書順。
///
/// # cdylib 越境通信
/// 含まない (filesystem enumerate のみ)。`project_uuid_cell()` / `peek_project_uuid`
/// / `set_project_uuid` のいずれも本関数からは触れない。
pub fn enumerate_active_pre_pair_candidates(kirin_root: &Path) -> Vec<PreCandidate> {
    crate::pre_discovery::discover_active_pre_dirs(kirin_root)
        .into_iter()
        .flat_map(|d| scan_pre_candidates_in(&d))
        .collect()
}

fn post_pair_names_in_project(kirin_root: &Path, post_project_hash: &str) -> HashSet<String> {
    if post_project_hash.is_empty() {
        return HashSet::new();
    }
    crate::io_thread_post::scan_post_candidates_in(&kirin_root.join(post_project_hash))
        .into_iter()
        .filter_map(|c| c.pair_pre_name)
        .filter(|name| !name.is_empty())
        .collect()
}

fn pre_names_in_project(project_dir: &Path) -> HashSet<String> {
    scan_pre_candidates_in(project_dir)
        .into_iter()
        .filter_map(|c| c.name)
        .filter(|name| !name.is_empty())
        .collect()
}

/// 現在の POST project_uuid から見える POST 群の `pair_pre_name` と最も整合する PRE
/// project_uuid 群だけを返す。
///
/// PRE.vst3 / POST.vst3 は別 cdylib のため project_uuid が直接一致しない。一方で同じ
/// Studio One セッション内では POST 群が保持する `pair_pre_name` 集合と PRE 群の `name`
/// 集合が対応するため、その重なりをセッション境界として使う。POST 側にまだ name 情報が
/// 無い初期状態では従来互換として全 PRE dir に fallback する。
///
/// 同点が複数ある場合は全て返す。後段の name 一意選定が曖昧として拒否するため、誤った
/// cross-session Keep を避けられる。
pub fn discover_pre_dirs_for_post_project(
    kirin_root: &Path,
    post_project_hash: &str,
) -> Vec<PathBuf> {
    let dirs = crate::pre_discovery::discover_active_pre_dirs(kirin_root);
    let post_names = post_pair_names_in_project(kirin_root, post_project_hash);
    if post_names.is_empty() {
        return dirs;
    }

    let scored: Vec<(PathBuf, usize)> = dirs
        .into_iter()
        .map(|dir| {
            let pre_names = pre_names_in_project(&dir);
            let score = pre_names.intersection(&post_names).count();
            (dir, score)
        })
        .collect();
    let best = scored.iter().map(|(_, score)| *score).max().unwrap_or(0);
    if best == 0 {
        return Vec::new();
    }
    scored
        .into_iter()
        .filter_map(|(dir, score)| (score == best).then_some(dir))
        .collect()
}

/// POST project_uuid でスコープした PRE 候補列挙（GUI/JUCE dropdown 用）。
///
/// `post_project_hash` から current session を推定できる場合は他セッションの PRE 名を出さない。
/// 推定材料（POST 側 pair_pre_name）が無い初期状態では既存の全件列挙に fallback する。
pub fn enumerate_active_pre_pair_candidates_for_post_project(
    kirin_root: &Path,
    post_project_hash: &str,
) -> Vec<PreCandidate> {
    discover_pre_dirs_for_post_project(kirin_root, post_project_hash)
        .into_iter()
        .flat_map(|d| scan_pre_candidates_in(&d))
        .collect()
}

/// POST 側計測値。距離計算用。
#[derive(Debug, Clone, Copy)]
pub struct PostMetrics {
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
    pub crest: Option<f64>,
}

/// PRE 候補から最近 PRE を選ぶ（距離ベース）。
///
/// **B-059: production 非経路。** 表示=commit 一本化（厳格）により距離 auto-pick は
/// 廃止された（曖昧は沈黙＝選定しない）。本関数は単体テスト保持のため温存する。
/// production 選定は [`select_target_pre`] を使うこと。
///
/// - 0 件 → None
/// - 1 件 → その 1 つを返す（計測値無くても自動確定。G-50-35）
/// - 複数 + POST/PRE 双方に全 3 メトリック有 → distance 最小を返す
/// - 複数 + いずれかメトリック不在 → 最初の候補を返す（ソート済みで安定）
#[doc(hidden)]
pub fn pick_closest_pre(candidates: &[PreCandidate], post: PostMetrics) -> Option<&PreCandidate> {
    match candidates.len() {
        0 => None,
        1 => candidates.first(),
        _ => {
            let (Some(pl), Some(pt), Some(pc)) = (post.lufs_m, post.true_peak, post.crest) else {
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

/// 表示Δ・commit 両経路が共有する単一選定結果（B-059）。
pub struct SelectedPre {
    pub instance_id: String,
    /// 選定 PRE の `pre.json`（`{kirin_root}/{project_uuid}/{instance_id}/pre.json`）。
    pub pre_json: PathBuf,
    /// `compute_delta_with_state` に渡す `{project_uuid}` ディレクトリ。
    pub project_dir: PathBuf,
}

/// 表示Δ（`compute_delta_with_state`）と Arm（`trigger_keep_internal`）が共有する**厳格 PRE 選定**
/// の共通コア（B-059 / 表示=Arm 一本化・B-104 で用途別 gate 化）。`require_active`:
/// - **Display（Δ計算）= `true`**: `signal_state == "active"` + fresh + name 一致ちょうど1件（ZSA・不変）。
/// - **Arm（keep/trigger_keep/broadcast 受信/keep_all）= `false`**: 非Bypassed（`scan_pre_candidates_in`
///   で Bypassed 除外済）+ fresh + name 一致ちょうど1件。Active 要求を外し、v1.0.0 の「アーム→
///   再生」（停止中・無音でもアーム可）を復元する（B-104）。auto-pick 廃止・一意性・**単一コア**
///   という B-059 の骨格は維持する。
///
/// 1. `pair_pre_name` 空 → `None`（明示名必須）。
/// 2. fresh project_dir（[`crate::pre_discovery::discover_active_pre_dirs`]）→ [`scan_pre_candidates_in`]
///    （Bypassed 除外）を flatten。
/// 3. gate: Display のみ `signal_state == "active"` を要求。両経路とも `t` age <
///    [`crate::io_thread_post::NO_PRE_SECS`]（10s）。停止中 PRE も io_thread が毎 tick `t` を更新する
///    ため fresh 判定は維持される（io_thread_pre::serialize_pre_json_minimal）。
/// 4. name 一致（`candidate.name == pair_pre_name`）。
/// 5. **ちょうど 1 件 → `Some` / 0 件 or 2 件以上 → `None`**（曖昧は沈黙）。
fn select_target_pre_core(
    kirin_root: &Path,
    pair_pre_name: &str,
    require_active: bool,
) -> Option<SelectedPre> {
    let dirs = crate::pre_discovery::discover_active_pre_dirs(kirin_root);
    select_target_pre_core_from_dirs(&dirs, pair_pre_name, require_active)
}

fn select_target_pre_core_from_dirs(
    dirs: &[PathBuf],
    pair_pre_name: &str,
    require_active: bool,
) -> Option<SelectedPre> {
    if pair_pre_name.is_empty() {
        return None;
    }
    let candidates: Vec<PreCandidate> = dirs
        .iter()
        .flat_map(|d| scan_pre_candidates_in(d))
        .collect();
    let named = filter_candidates_by_name(candidates, pair_pre_name);

    let mut valid: Vec<SelectedPre> = Vec::new();
    for c in named {
        let Ok(content) = fs::read_to_string(&c.path) else {
            continue;
        };
        let Ok(v) = serde_json::from_str::<serde_json::Value>(&content) else {
            continue;
        };
        // gate (Active): Display のみ。Arm は scan で Bypassed 除外済の非Bypassed（Active+Inactive）
        // を許容する（B-104 / v1.0.0 アーム意味論の復元）。
        if require_active && v.get("signal_state").and_then(|x| x.as_str()) != Some("active") {
            continue;
        }
        // gate (freshness): t age < NO_PRE_SECS（両経路共通）。
        let Some(t_str) = v.get("t").and_then(|x| x.as_str()) else {
            continue;
        };
        if !t_age_within(t_str, crate::io_thread_post::NO_PRE_SECS) {
            continue;
        }
        let Some(project_dir) = c
            .path
            .parent()
            .and_then(|p| p.parent())
            .map(Path::to_path_buf)
        else {
            continue;
        };
        valid.push(SelectedPre {
            instance_id: c.instance_id,
            pre_json: c.path,
            project_dir,
        });
    }

    if valid.len() == 1 {
        valid.pop()
    } else {
        None // 0 件 or 2 件以上 = 曖昧 → 沈黙（表示 NoPre / Arm 拒否）。
    }
}

/// 表示Δ用の厳格 PRE 選定（Active + fresh + 一意 / B-059・ZSA 不変）。`run_tick` の Δ が呼ぶ。
pub fn select_target_pre(kirin_root: &Path, pair_pre_name: &str) -> Option<SelectedPre> {
    select_target_pre_core(kirin_root, pair_pre_name, true)
}

/// Arm 用の厳格 PRE 選定（非Bypassed + fresh + 一意 / Active 要求なし・B-104）。`keep` /
/// `trigger_keep_internal` / broadcast 受信 / `keep_all` が呼ぶ。v1.0.0 の「アーム→再生」
/// （停止中・無音でもアーム可）を復元する。Display（[`select_target_pre`]）は変更しない。
pub fn select_target_pre_for_arm(kirin_root: &Path, pair_pre_name: &str) -> Option<SelectedPre> {
    select_target_pre_core(kirin_root, pair_pre_name, false)
}

/// POST project_uuid でスコープした表示Δ用 PRE 選定。
pub fn select_target_pre_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
) -> Option<SelectedPre> {
    let dirs = discover_pre_dirs_for_post_project(kirin_root, post_project_hash);
    select_target_pre_core_from_dirs(&dirs, pair_pre_name, true)
}

/// POST project_uuid でスコープした Arm 用 PRE 選定。
pub fn select_target_pre_for_arm_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
) -> Option<SelectedPre> {
    let dirs = discover_pre_dirs_for_post_project(kirin_root, post_project_hash);
    select_target_pre_core_from_dirs(&dirs, pair_pre_name, false)
}

/// B-108: 一度成立した PRE↔POST 結合（ラッチ）。display tick と keep/Arm が共有する単一実体。
/// 解除は (a) 利用者が pair 名を変更/クリア、(b) ラッチ先 PRE の実消滅（pre.json 削除 or
/// stale > `NO_PRE_SECS`）のみ。無音・停止・一時的鮮度揺らぎ・同名2台目では解除しない
/// （B-108 / B-059「display previews commit」の改訂）。曖昧性（同名複数）は初回ラッチ時のみ評価。
#[derive(Clone, Debug)]
pub struct LatchedPre {
    /// ラッチ時の pair 名（POST の pair_pre_name と一致する間だけ保持）。
    pub name: String,
    /// ラッチ先 PRE の永続 instance_id（keep の record_signal target）。
    pub instance_id: String,
    /// `compute_delta_with_state` に渡す `{project_uuid}` ディレクトリ。
    pub project_dir: PathBuf,
    /// ラッチ先 PRE の `pre.json` 絶対パス（毎 tick 直読する固定先）。
    pub pre_json: PathBuf,
}

/// B-108: ラッチ先 `pre.json` の単一直読結果。`None` = ファイル不在/読込不可/parse 不可
/// （= PRE 実消滅 → アンラッチ）。
pub struct LatchedPreState {
    /// `pre.json` の `name` field（ラッチ名と一致するか判定 / renamed → アンラッチ）。
    pub name: Option<String>,
    /// `signal_state == "active"`（true→Δ 計算 / false→latched-idle = Stale+NaN）。
    pub active: bool,
    /// `t` age < `NO_PRE_SECS`（10s）= fresh（プラグイン生存）。stale → アンラッチ。
    pub fresh: bool,
}

/// B-108: ラッチ先の **1 個**の `pre.json` を直読し、ラッチ判定に必要な状態を返す。
/// `select_target_pre` のような全棚 scan / 曖昧性評価は行わない（ラッチ後は再選定しない）。
/// 計測エンジン非依存・schema 0 変更（既存 `name`/`signal_state`/`t` field のみ読む）。
pub fn read_pre_at(pre_json: &Path) -> Option<LatchedPreState> {
    let content = fs::read_to_string(pre_json).ok()?;
    let v: serde_json::Value = serde_json::from_str(&content).ok()?;
    let name = v.get("name").and_then(|x| x.as_str()).map(str::to_string);
    let active = v.get("signal_state").and_then(|x| x.as_str()) == Some("active");
    let fresh = v
        .get("t")
        .and_then(|x| x.as_str())
        .map(|t| t_age_within(t, crate::io_thread_post::NO_PRE_SECS))
        .unwrap_or(false);
    Some(LatchedPreState {
        name,
        active,
        fresh,
    })
}

/// B-108: keep/Arm の target 解決。**ラッチ済み**（名前一致 & ラッチ先 pre.json が fresh & 同名）
/// ならラッチ先を直接返す（同名2台目が現れても結合不変）。未ラッチ時のみ
/// [`select_target_pre_for_arm`] にフォールバックする。`keep` / `trigger_keep_internal` /
/// broadcast 受信 / `keep_all`（egui/JUCE 両殻）が select_target_pre_for_arm の代わりに呼ぶ。
pub fn resolve_arm_target(
    kirin_root: &Path,
    pair_pre_name: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> Option<SelectedPre> {
    if let Ok(g) = latched.lock() {
        if let Some(l) = g.as_ref() {
            if l.name == pair_pre_name {
                if let Some(st) = read_pre_at(&l.pre_json) {
                    if st.fresh && st.name.as_deref() == Some(pair_pre_name) {
                        return Some(SelectedPre {
                            instance_id: l.instance_id.clone(),
                            pre_json: l.pre_json.clone(),
                            project_dir: l.project_dir.clone(),
                        });
                    }
                }
            }
        }
    }
    select_target_pre_for_arm(kirin_root, pair_pre_name)
}

/// POST project_uuid でスコープした keep/Arm target 解決。
///
/// ラッチ済みでも、現在 POST セッションから推定した PRE scope 外のラッチは採用しない。
/// これにより別セッション由来の同名 PRE へ Keep が流れる事故を防ぐ。
pub fn resolve_arm_target_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> Option<SelectedPre> {
    let dirs = discover_pre_dirs_for_post_project(kirin_root, post_project_hash);
    if let Ok(g) = latched.lock() {
        if let Some(l) = g.as_ref() {
            if l.name == pair_pre_name && dirs.iter().any(|d| d == &l.project_dir) {
                if let Some(st) = read_pre_at(&l.pre_json) {
                    if st.fresh && st.name.as_deref() == Some(pair_pre_name) {
                        return Some(SelectedPre {
                            instance_id: l.instance_id.clone(),
                            pre_json: l.pre_json.clone(),
                            project_dir: l.project_dir.clone(),
                        });
                    }
                }
            }
        }
    }
    select_target_pre_core_from_dirs(&dirs, pair_pre_name, false)
}

/// `t`(RFC3339) が現在から `max_secs` 秒以内か。parse 失敗は false（安全側で除外）。
/// `io_thread_post::freshness_mode` と同一の age 算出（`now - t` の秒）。
fn t_age_within(t_str: &str, max_secs: i64) -> bool {
    match chrono::DateTime::parse_from_rfc3339(t_str) {
        Ok(t) => (chrono::Utc::now() - t.with_timezone(&chrono::Utc)).num_seconds() < max_secs,
        Err(_) => false,
    }
}

// ── ヘルパ ────────────────────────────────────────────────────────────────────

fn now_iso8601() -> String {
    chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
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
        let s = RecordSignal::new_pending("post-001".into(), "pre-xyz".into(), "daw-uuid-1".into());
        let json = serde_json::to_string(&s).unwrap();
        let back: RecordSignal = serde_json::from_str(&json).unwrap();
        assert_eq!(s, back);
    }

    #[test]
    fn signal_path_uses_post_instance_id_as_filename() {
        let p = signal_path(Path::new("/tmp/kb"), "phash", "post-uuid-A");
        assert_eq!(p, Path::new("/tmp/kb/phash/record_signal/post-uuid-A.json"));
    }

    // ── I/O ─────────────────────────────────────────────────

    #[test]
    fn write_pending_creates_file_with_pending_status() {
        let base = isolated_dir();
        let s = write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        assert_eq!(s.status, SignalStatus::Pending);
        assert_eq!(s.requested_by, "post-1");
        assert_eq!(s.target_pre_instance_id, "pre-1");
        assert_eq!(s.daw_session_id, "daw-1");
        let loaded = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(loaded, s);
    }

    #[test]
    fn read_signal_missing_returns_none() {
        let base = isolated_dir();
        assert!(read_signal(&base, "ph", "post-x").is_none());
    }

    #[test]
    fn read_signal_corrupt_returns_none() {
        let base = isolated_dir();
        let dir = signals_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("post-x.json"), b"not json").unwrap();
        assert!(read_signal(&base, "ph", "post-x").is_none());
    }

    #[test]
    fn mark_acknowledged_updates_status_and_returns_true() {
        let base = isolated_dir();
        write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        let changed = mark_acknowledged(&base, "ph", "post-1").unwrap();
        assert!(changed);
        let loaded = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(loaded.status, SignalStatus::Acknowledged);
        assert_eq!(
            loaded.daw_session_id, "daw-1",
            "daw_session_id preserved on transition"
        );
    }

    #[test]
    fn started_at_preserved_across_transitions() {
        let base = isolated_dir();
        let initial = write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        let first_started = initial.started_at.clone();
        assert!(!first_started.is_empty(), "started_at must be set");
        std::thread::sleep(std::time::Duration::from_millis(1100));
        mark_acknowledged(&base, "ph", "post-1").unwrap();
        let acked = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(acked.started_at, first_started);
        assert_ne!(acked.t, first_started, "t should have advanced");
        mark_released(&base, "ph", "post-1").unwrap();
        let released = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(released.started_at, first_started);
    }

    #[test]
    fn legacy_schema_without_daw_session_id_defaults_to_empty() {
        let base = isolated_dir();
        let dir = signals_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        let legacy = r#"{"status":"pending","requested_by":"post-1","target_pre_instance_id":"pre-1","t":"2026-01-01T00:00:00Z","started_at":"2026-01-01T00:00:00Z"}"#;
        fs::write(dir.join("post-1.json"), legacy).unwrap();
        let loaded = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(loaded.daw_session_id, "");
    }

    /// B-023 段階 3: paired_pre_name 不在の旧 schema 読込で空文字 default に
    /// なること (daw_session_id / started_at と同パターン / R-28 機能的沈黙)。
    #[test]
    fn legacy_schema_without_paired_pre_name_defaults_to_empty() {
        let base = isolated_dir();
        let dir = signals_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        let legacy = r#"{"status":"acknowledged","requested_by":"post-1","target_pre_instance_id":"pre-1","daw_session_id":"daw-1","t":"2026-01-01T00:00:00Z","started_at":"2026-01-01T00:00:00Z"}"#;
        fs::write(dir.join("post-1.json"), legacy).unwrap();
        let loaded = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(loaded.paired_pre_name, "");
        assert_eq!(loaded.status, SignalStatus::Acknowledged);
    }

    /// B-023 段階 3: mark_acknowledged_with_name で渡した name が
    /// signal.paired_pre_name に永続化されること。
    #[test]
    fn mark_acknowledged_with_name_persists_name() {
        let base = isolated_dir();
        write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        let changed = mark_acknowledged_with_name(&base, "ph", "post-1", "Studio Mix").unwrap();
        assert!(changed);
        let loaded = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(loaded.status, SignalStatus::Acknowledged);
        assert_eq!(loaded.paired_pre_name, "Studio Mix");
    }

    /// B-023 段階 3: 既存 mark_acknowledged 呼出 (= wrapper) は paired_pre_name
    /// を空文字で書く (新旧呼出共存の確認)。
    #[test]
    fn mark_acknowledged_legacy_calls_with_name_empty() {
        let base = isolated_dir();
        write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        let changed = mark_acknowledged(&base, "ph", "post-1").unwrap();
        assert!(changed);
        let loaded = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(loaded.status, SignalStatus::Acknowledged);
        assert_eq!(loaded.paired_pre_name, "");
    }

    #[test]
    fn mark_released_updates_status() {
        let base = isolated_dir();
        write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        mark_released(&base, "ph", "post-1").unwrap();
        let loaded = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(loaded.status, SignalStatus::Released);
    }

    #[test]
    fn transition_on_missing_returns_false() {
        let base = isolated_dir();
        let changed = mark_acknowledged(&base, "ph", "post-x").unwrap();
        assert!(!changed);
    }

    #[test]
    fn delete_signal_removes_file() {
        let base = isolated_dir();
        write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        delete_signal(&base, "ph", "post-1").unwrap();
        assert!(read_signal(&base, "ph", "post-1").is_none());
    }

    #[test]
    fn delete_signal_on_missing_is_ok() {
        let base = isolated_dir();
        delete_signal(&base, "ph", "post-x").unwrap();
    }

    /// B-027 段階 3-B α-7 / Group 2 (Gap-6 局所対処): 統合点 #2/#3/#4 の
    /// 3 経路から重複呼出されても全て Ok で終わり、file が不在のまま安定する。
    /// (#2 trigger_stop / #3 HyphaPost::drop / #4 IO Thread terminate)
    #[test]
    fn delete_signal_idempotent_under_repeated_calls() {
        let base = isolated_dir();
        write_pending(&base, "ph", "post-rep", "pre-1".into(), "daw-1".into()).unwrap();
        // 1 回目: 実削除
        delete_signal(&base, "ph", "post-rep").unwrap();
        assert!(read_signal(&base, "ph", "post-rep").is_none());
        // 2 回目以降: NotFound → Ok (冪等)
        delete_signal(&base, "ph", "post-rep").unwrap();
        delete_signal(&base, "ph", "post-rep").unwrap();
        assert!(read_signal(&base, "ph", "post-rep").is_none());
    }

    #[test]
    fn atomic_write_leaves_no_tmp_behind() {
        let base = isolated_dir();
        write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        let final_path = signal_path(&base, "ph", "post-1");
        let tmp = tmp_path(&final_path);
        assert!(final_path.exists());
        assert!(!tmp.exists(), "tmp must be renamed away: {tmp:?}");
    }

    // ── scan_signals_dir ────────────────────────────────────

    #[test]
    fn scan_signals_returns_empty_when_dir_missing() {
        let base = isolated_dir();
        let v = scan_signals_dir(&base, "ph");
        assert!(v.is_empty());
    }

    #[test]
    fn scan_signals_returns_all_in_alphabetical_order() {
        let base = isolated_dir();
        write_pending(&base, "ph", "post-b", "pre-1".into(), "daw-1".into()).unwrap();
        write_pending(&base, "ph", "post-a", "pre-1".into(), "daw-1".into()).unwrap();
        write_pending(&base, "ph", "post-c", "pre-2".into(), "daw-1".into()).unwrap();
        let v = scan_signals_dir(&base, "ph");
        assert_eq!(v.len(), 3);
        assert_eq!(v[0].0, "post-a");
        assert_eq!(v[1].0, "post-b");
        assert_eq!(v[2].0, "post-c");
    }

    #[test]
    fn scan_signals_skips_corrupt_and_non_json() {
        let base = isolated_dir();
        let dir = signals_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("bad.json"), b"{ invalid").unwrap();
        fs::write(dir.join("readme.txt"), b"ignore me").unwrap();
        write_pending(&base, "ph", "post-good", "pre-1".into(), "daw-1".into()).unwrap();
        let v = scan_signals_dir(&base, "ph");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].0, "post-good");
    }

    // ── タイムアウト ────────────────────────────────────────

    fn iso_ago(secs: i64, now: DateTime<Utc>) -> String {
        let t = now - chrono::Duration::seconds(secs);
        t.format("%Y-%m-%dT%H:%M:%SZ").to_string()
    }

    #[test]
    fn timeout_fires_strictly_after_30s() {
        let now = Utc::now();
        let mut sig = RecordSignal::new_pending("p".into(), "r".into(), "d".into());
        sig.t = iso_ago(30, now);
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
        sig.t = iso_ago(31, now);
        assert!(is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    }

    #[test]
    fn timeout_not_considered_for_non_pending() {
        let now = Utc::now();
        let mut sig = RecordSignal::new_pending("p".into(), "r".into(), "d".into());
        sig.t = iso_ago(3600, now);
        sig.status = SignalStatus::Acknowledged;
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
        sig.status = SignalStatus::Released;
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    }

    #[test]
    fn timeout_invalid_iso_is_false() {
        let now = Utc::now();
        let mut sig = RecordSignal::new_pending("p".into(), "r".into(), "d".into());
        sig.t = "not-iso".to_string();
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    }

    // ── ペアリング（新構造: {project_hash}/{instance_id}/pre.json）──

    fn write_pre_tmp(
        tmp_base: &Path,
        ph: &str,
        instance_id: &str,
        metrics: Option<(f64, f64, f64)>,
    ) -> PathBuf {
        // B-024: metrics=None ブランチは「Active だが ebur128 がまだ十分な
        // サンプルを積んでいない瞬間」(= signal_state="active" + 計測値 None)
        // を表現する。signal_state="inactive" は別 helper write_pre_tmp_inactive
        // でテストする (scan_pre_candidates_in の filter 動作)。
        let dir = tmp_base.join(ph).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("pre.json");
        let json = if let Some((l, t, c)) = metrics {
            format!(
                r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","t":"now","lufs_m":{l},"true_peak":{t},"crest":{c},"psr":8.0}}"#
            )
        } else {
            format!(
                r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","t":"now"}}"#
            )
        };
        fs::write(&path, json).unwrap();
        path
    }

    #[test]
    fn scan_with_no_dir_returns_empty() {
        let base = isolated_dir();
        let v = scan_pre_candidates(&base, "ph");
        assert!(v.is_empty());
    }

    #[test]
    fn scan_skips_record_signal_subdir() {
        let base = isolated_dir();
        // record_signal/ をプロジェクト直下に作成（ペアリング走査の対象外）
        write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        write_pre_tmp(&base, "ph", "good-pre", Some((-14.0, -1.0, 12.0)));
        let v = scan_pre_candidates(&base, "ph");
        assert_eq!(v.len(), 1, "record_signal/ must not be treated as instance");
        assert_eq!(v[0].instance_id, "good-pre");
    }

    #[test]
    fn scan_skips_corrupt_pre_json() {
        let base = isolated_dir();
        let dir = base.join("ph").join("bad-pre");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("pre.json"), b"{ not valid").unwrap();
        write_pre_tmp(&base, "ph", "ok-pre", Some((-14.0, -1.0, 12.0)));
        let v = scan_pre_candidates(&base, "ph");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].instance_id, "ok-pre");
    }

    /// B-021 Phase 1A 追加修正: Keep ハンドラ経路の cross-project シナリオ。
    ///
    /// cdylib 隔離下で PRE と POST が別 `project_uuid` 配下に書く状況を再現。
    /// 旧 `scan_pre_candidates(&base, post_ph)` は POST の project_hash 配下のみ
    /// 走査するため空 Vec を返してしまう（= 実機 NG #8 の根本）。
    /// 新 `scan_pre_candidates_in(pre_project_dir)` は discovery で得た PRE 側
    /// dir を直接受け取るため、project_uuid が乖離していても候補を返す。
    #[test]
    fn scan_pre_candidates_in_works_across_project_uuids() {
        let base = isolated_dir();
        // PRE 側: project_uuid="pre-proj", POST 側: project_uuid="post-proj"
        write_pre_tmp(&base, "pre-proj", "pre-A", Some((-14.0, -1.0, 12.0)));

        // 旧 API (POST の project_hash で scan) → 空（cdylib 隔離下の誤動作再現）
        let old_path_result = scan_pre_candidates(&base, "post-proj");
        assert!(
            old_path_result.is_empty(),
            "scan_pre_candidates(post_ph) must return empty when PRE is under different project_uuid"
        );

        // 新 API (discovery が返した PRE dir を直接渡す) → 1 件返る
        let pre_project_dir = base.join("pre-proj");
        let new_path_result = scan_pre_candidates_in(&pre_project_dir);
        assert_eq!(
            new_path_result.len(),
            1,
            "scan_pre_candidates_in(pre_project_dir) must find PRE across project_uuid boundaries"
        );
        assert_eq!(new_path_result[0].instance_id, "pre-A");
        assert_eq!(new_path_result[0].lufs_m, Some(-14.0));
    }

    #[test]
    fn pick_zero_returns_none() {
        let v: Vec<PreCandidate> = Vec::new();
        let r = pick_closest_pre(
            &v,
            PostMetrics {
                lufs_m: Some(-14.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
            },
        );
        assert!(r.is_none());
    }

    #[test]
    fn pick_single_auto_selects() {
        let base = isolated_dir();
        write_pre_tmp(&base, "ph", "solo", Some((-14.0, -1.0, 12.0)));
        let v = scan_pre_candidates(&base, "ph");
        let r = pick_closest_pre(
            &v,
            PostMetrics {
                lufs_m: Some(-14.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
            },
        );
        assert_eq!(r.unwrap().instance_id, "solo");
    }

    #[test]
    fn pick_single_with_no_metrics_still_auto_selects() {
        let base = isolated_dir();
        write_pre_tmp(&base, "ph", "silent", None);
        let v = scan_pre_candidates(&base, "ph");
        let r = pick_closest_pre(
            &v,
            PostMetrics {
                lufs_m: None,
                true_peak: None,
                crest: None,
            },
        );
        assert_eq!(r.unwrap().instance_id, "silent");
    }

    #[test]
    fn pick_multi_picks_minimum_distance() {
        let base = isolated_dir();
        write_pre_tmp(&base, "ph", "a", Some((-20.0, -5.0, 15.0))); // distance=13
        write_pre_tmp(&base, "ph", "b", Some((-13.0, -1.5, 12.0))); // distance=1.5 (closest)
        write_pre_tmp(&base, "ph", "c", Some((-14.0, -2.0, 14.0))); // distance=3
        let v = scan_pre_candidates(&base, "ph");
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

    // ── B-024: signal_state filter (読込側 / 二重防御の片側) ─────────────

    /// 任意の signal_state 文字列で pre.json を書く helper。
    /// `state` には "active" / "bypassed" / "inactive" のいずれかを渡す。
    fn write_pre_tmp_with_state(tmp_base: &Path, ph: &str, instance_id: &str, state: &str) {
        let dir = tmp_base.join(ph).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let json = format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"{state}","t":"now","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
        );
        fs::write(dir.join("pre.json"), json).unwrap();
    }

    /// signal_state="active" の PRE は scan で返却される (regression 防止)。
    #[test]
    fn scan_pre_candidates_in_keeps_active() {
        let base = isolated_dir();
        write_pre_tmp_with_state(&base, "ph", "active-a", "active");
        write_pre_tmp_with_state(&base, "ph", "active-b", "active");
        let v = scan_pre_candidates(&base, "ph");
        assert_eq!(v.len(), 2, "Active な PRE 2 件を返すこと");
        let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
        assert!(ids.contains(&"active-a"));
        assert!(ids.contains(&"active-b"));
    }

    /// B-027: signal_state="bypassed" のみ除外される (緩和後)。
    /// active 1 件 + bypassed 1 件 + inactive 1 件 → active + inactive の 2 件返却。
    #[test]
    fn scan_pre_candidates_in_filters_only_bypassed() {
        let base = isolated_dir();
        write_pre_tmp_with_state(&base, "ph", "alive", "active");
        write_pre_tmp_with_state(&base, "ph", "off-bypass", "bypassed");
        write_pre_tmp_with_state(&base, "ph", "off-inactive", "inactive");
        let v = scan_pre_candidates(&base, "ph");
        assert_eq!(
            v.len(),
            2,
            "Bypassed のみ除外され、Active / Inactive は候補化されること"
        );
        let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
        assert!(ids.contains(&"alive"));
        assert!(ids.contains(&"off-inactive"));
    }

    /// B-027: 旧 schema (signal_state フィールド不在) も候補化される (緩和後)。
    /// Bypassed のみ除外する filter のため、None (旧 schema) は通過する。
    #[test]
    fn scan_pre_candidates_in_keeps_legacy_no_signal_state() {
        let base = isolated_dir();
        // 旧 schema: signal_state フィールドが存在しない pre.json
        let dir = base.join("ph").join("legacy");
        fs::create_dir_all(&dir).unwrap();
        let legacy_json = r#"{"v":2,"role":"PRE","instance_id":"legacy","t":"now","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
        fs::write(dir.join("pre.json"), legacy_json).unwrap();
        write_pre_tmp_with_state(&base, "ph", "new-active", "active");

        let v = scan_pre_candidates(&base, "ph");
        assert_eq!(
            v.len(),
            2,
            "signal_state 不在 (旧 schema) も Active 新 schema も候補化される"
        );
    }

    // ── B-027 段階 2: name field + filter_candidates_by_name ────────────

    /// pre.json に `name` 任意指定で書く helper (B-027 段階 2)。
    fn write_pre_tmp_with_name(
        tmp_base: &Path,
        ph: &str,
        instance_id: &str,
        state: &str,
        name: &str,
    ) {
        let dir = tmp_base.join(ph).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let json = format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"{state}","t":"now","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0,"name":"{name}"}}"#
        );
        fs::write(dir.join("pre.json"), json).unwrap();
    }

    /// pre.json に `name` field がある場合 PreCandidate.name に反映される。
    #[test]
    fn pre_candidate_includes_name_field() {
        let base = isolated_dir();
        write_pre_tmp_with_name(&base, "ph", "iid-1", "active", "Snare");
        let v = scan_pre_candidates(&base, "ph");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].name.as_deref(), Some("Snare"));
    }

    /// 旧 schema (`name` field 不在) は PreCandidate.name == None。
    #[test]
    fn pre_candidate_name_legacy_schema_is_none() {
        let base = isolated_dir();
        write_pre_tmp_with_state(&base, "ph", "legacy-no-name", "active");
        let v = scan_pre_candidates(&base, "ph");
        assert_eq!(v.len(), 1);
        assert!(v[0].name.is_none(), "name field 不在は None");
    }

    /// filter_candidates_by_name: 一致 1 件のみ通過。
    #[test]
    fn filter_candidates_by_name_keeps_match() {
        let base = isolated_dir();
        write_pre_tmp_with_name(&base, "ph", "iid-a", "active", "Snare");
        write_pre_tmp_with_name(&base, "ph", "iid-b", "active", "Kick");
        let v = scan_pre_candidates(&base, "ph");
        let filtered = filter_candidates_by_name(v, "Snare");
        assert_eq!(filtered.len(), 1);
        assert_eq!(filtered[0].instance_id, "iid-a");
    }

    /// B-077: 日本語 name で pairing 照合（UTF-8 exact 一致）が当たる。
    #[test]
    fn filter_candidates_by_name_matches_japanese() {
        let base = isolated_dir();
        write_pre_tmp_with_name(&base, "ph", "iid-jp", "active", "日本語Snare");
        write_pre_tmp_with_name(&base, "ph", "iid-other", "active", "Kick");
        let v = scan_pre_candidates(&base, "ph");
        let filtered = filter_candidates_by_name(v, "日本語Snare");
        assert_eq!(filtered.len(), 1, "日本語 name が exact 一致で 1 件通過");
        assert_eq!(filtered[0].instance_id, "iid-jp");
    }

    /// filter_candidates_by_name: 不一致は 0 件。
    #[test]
    fn filter_candidates_by_name_drops_mismatch() {
        let base = isolated_dir();
        write_pre_tmp_with_name(&base, "ph", "iid-a", "active", "Snare");
        write_pre_tmp_with_name(&base, "ph", "iid-b", "active", "Kick");
        let v = scan_pre_candidates(&base, "ph");
        let filtered = filter_candidates_by_name(v, "Hat");
        assert!(filtered.is_empty(), "存在しない Name 指定で 0 件");
    }

    /// filter_candidates_by_name: 空文字 pair_pre_name は filter しない (pass-through)。
    /// B-027 段階 1 受入 (停止中 1/2 セット紐付け) 維持の構造保証。
    #[test]
    fn filter_candidates_by_name_empty_passes_through() {
        let base = isolated_dir();
        write_pre_tmp_with_name(&base, "ph", "iid-a", "active", "Snare");
        write_pre_tmp_with_state(&base, "ph", "iid-legacy", "active");
        let v = scan_pre_candidates(&base, "ph");
        assert_eq!(v.len(), 2);
        let filtered = filter_candidates_by_name(v, "");
        assert_eq!(
            filtered.len(),
            2,
            "空文字は filter 不適用 (legacy schema 含め全候補通過)"
        );
    }

    /// filter_candidates_by_name: 同 Name 複数 PRE は全件通過 (pick_closest_pre 委譲)。
    #[test]
    fn filter_candidates_by_name_keeps_multiple_matches() {
        let base = isolated_dir();
        write_pre_tmp_with_name(&base, "ph", "iid-a", "active", "Snare");
        write_pre_tmp_with_name(&base, "ph", "iid-b", "active", "Snare");
        write_pre_tmp_with_name(&base, "ph", "iid-c", "active", "Kick");
        let v = scan_pre_candidates(&base, "ph");
        let filtered = filter_candidates_by_name(v, "Snare");
        assert_eq!(
            filtered.len(),
            2,
            "同 Name 複数は全件通過 (距離選定は呼出側)"
        );
        let ids: Vec<&str> = filtered.iter().map(|c| c.instance_id.as_str()).collect();
        assert!(ids.contains(&"iid-a"));
        assert!(ids.contains(&"iid-b"));
    }

    // ── B-027 段階 3-A 修正 (G-115-49 / α-2 撤回):
    //    enumerate_active_pre_pair_candidates ────────────────────────────────

    /// 全 fresh PRE dir を flatten 列挙する。複数 project_uuid 配下の PRE が
    /// 混在しても、project_hash filter なしで全件返却される (cdylib 隔離下の
    /// 構造的整合)。
    #[test]
    fn enumerate_active_pre_pair_candidates_flattens_all_active_pre_dirs() {
        let base = isolated_dir();
        // project A 配下: 2 PRE
        write_pre_tmp_with_name(&base, "uuid_a", "iid-a1", "active", "Snare");
        write_pre_tmp_with_name(&base, "uuid_a", "iid-a2", "active", "Kick");
        // project B 配下: 1 PRE (旧 enumerate_same_project_* なら除外されたが
        // 新仕様では候補化される)
        write_pre_tmp_with_name(&base, "uuid_b", "iid-b1", "active", "Hat");

        let v = enumerate_active_pre_pair_candidates(&base);
        let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
        assert_eq!(v.len(), 3, "全 project_uuid 配下を flatten: {ids:?}");
        assert!(ids.contains(&"iid-a1"));
        assert!(ids.contains(&"iid-a2"));
        assert!(
            ids.contains(&"iid-b1"),
            "別 project_uuid 配下も候補化される (α-2 撤回)"
        );
    }

    /// kirin_root 配下に active PRE dir が 1 件もない場合は空 Vec。
    #[test]
    fn enumerate_active_pre_pair_candidates_returns_empty_when_no_active_pre_dir() {
        let base = isolated_dir();
        // pre.json なしの空 instance dir のみ作成 (active PRE 不在)
        std::fs::create_dir_all(base.join("uuid_x").join("iid_x")).unwrap();

        let v = enumerate_active_pre_pair_candidates(&base);
        assert!(v.is_empty(), "active PRE 不在 → 空 Vec");
    }

    // ── シーケンス統合 ──────────────────────────────────────

    #[test]
    fn full_post_to_pre_handshake_sequence() {
        let base = isolated_dir();
        let sig = write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
        assert_eq!(sig.status, SignalStatus::Pending);
        assert_eq!(sig.daw_session_id, "daw-1");

        let scanned = scan_signals_dir(&base, "ph");
        assert_eq!(scanned.len(), 1);
        assert_eq!(scanned[0].0, "post-1");
        assert_eq!(scanned[0].1.target_pre_instance_id, "pre-1");

        mark_acknowledged(&base, "ph", "post-1").unwrap();
        mark_released(&base, "ph", "post-1").unwrap();
        let after = read_signal(&base, "ph", "post-1").unwrap();
        assert_eq!(after.status, SignalStatus::Released);
        delete_signal(&base, "ph", "post-1").unwrap();
        assert!(read_signal(&base, "ph", "post-1").is_none());
    }

    // ── B-059: select_target_pre 厳格選定（表示=commit 一本化）─────────────────

    /// RFC3339 の正規 `t` を持つ pre.json fixture（select_target_pre の freshness gate 対応）。
    fn write_pre_for_select(
        kirin_root: &Path,
        project_uuid: &str,
        instance_id: &str,
        name: &str,
        signal_state: &str,
        t_rfc3339: &str,
    ) {
        let dir = kirin_root.join(project_uuid).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let json = format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","name":"{name}","signal_state":"{signal_state}","t":"{t_rfc3339}","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0}}"#
        );
        fs::write(dir.join("pre.json"), json).unwrap();
    }

    fn write_post_for_scope(
        kirin_root: &Path,
        project_uuid: &str,
        instance_id: &str,
        pair_pre_name: &str,
    ) {
        let dir = kirin_root.join(project_uuid).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let json = serde_json::json!({
            "v": 2,
            "role": "POST",
            "instance_id": instance_id,
            "signal_state": "inactive",
            "t": now_rfc3339(),
            "pair_pre_name": pair_pre_name,
            "pair_claimed_at": 1.0
        });
        fs::write(dir.join("post.json"), json.to_string()).unwrap();
    }

    fn now_rfc3339() -> String {
        Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
    }
    fn old_rfc3339(secs_ago: i64) -> String {
        (Utc::now() - chrono::Duration::seconds(secs_ago))
            .format("%Y-%m-%dT%H:%M:%SZ")
            .to_string()
    }

    /// (d) 一意・Active・fresh → Some(その instance_id)。
    #[test]
    fn select_target_pre_unique_active_fresh_returns_some() {
        let root = isolated_dir();
        write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now_rfc3339());
        let sel = select_target_pre(&root, "snare").expect("unique active fresh → Some");
        assert_eq!(sel.instance_id, "iid-A");
        assert!(sel.pre_json.ends_with("pre.json"));
    }

    /// (a) 同名 2 件（Active/fresh・別 project_uuid）→ 曖昧 → None。
    #[test]
    fn select_target_pre_ambiguous_same_name_returns_none() {
        let root = isolated_dir();
        let now = now_rfc3339();
        write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now);
        write_pre_for_select(&root, "puid-2", "iid-B", "snare", "active", &now);
        assert!(
            select_target_pre(&root, "snare").is_none(),
            "同名 2 件は None（曖昧沈黙）"
        );
    }

    /// (b) pair_pre_name 空 → None（距離 auto-pick 廃止）。
    #[test]
    fn select_target_pre_empty_name_returns_none() {
        let root = isolated_dir();
        write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now_rfc3339());
        assert!(select_target_pre(&root, "").is_none(), "空名は None");
    }

    /// (c1) Inactive → 除外 → None。
    #[test]
    fn select_target_pre_inactive_excluded() {
        let root = isolated_dir();
        write_pre_for_select(
            &root,
            "puid-1",
            "iid-A",
            "snare",
            "inactive",
            &now_rfc3339(),
        );
        assert!(
            select_target_pre(&root, "snare").is_none(),
            "Inactive は除外"
        );
    }

    /// (c2) t age ≥ NO_PRE_SECS(10s) → 除外 → None。
    #[test]
    fn select_target_pre_stale_t_excluded() {
        let root = isolated_dir();
        write_pre_for_select(
            &root,
            "puid-1",
            "iid-A",
            "snare",
            "active",
            &old_rfc3339(20),
        );
        assert!(
            select_target_pre(&root, "snare").is_none(),
            "古 t（≥10s）は除外"
        );
    }

    /// display==commit: 両経路は同一 select_target_pre を呼ぶため、同一 fixture に対し
    /// 同一 instance_id を返す（commit target == 表示 Δ 対象 / name で一意分離）。
    #[test]
    fn select_target_pre_display_equals_commit() {
        let root = isolated_dir();
        let now = now_rfc3339();
        write_pre_for_select(&root, "puid-1", "iid-snare", "snare", "active", &now);
        write_pre_for_select(&root, "puid-2", "iid-kick", "kick", "active", &now);
        assert_eq!(
            select_target_pre(&root, "snare").unwrap().instance_id,
            "iid-snare"
        );
        assert_eq!(
            select_target_pre(&root, "kick").unwrap().instance_id,
            "iid-kick"
        );
        assert!(
            select_target_pre(&root, "vocal").is_none(),
            "不在 name は両経路 None"
        );
    }

    /// POST 側 project_uuid と PRE 側 project_uuid は直接一致しないため、現在 POST 群の
    /// pair_pre_name 集合と最も重なる PRE 群だけを候補化する。
    #[test]
    fn enumerate_active_pre_pair_candidates_for_post_project_picks_best_name_overlap() {
        let root = isolated_dir();
        let now = now_rfc3339();
        write_pre_for_select(&root, "pre-old", "iid-old-snare", "Snare", "inactive", &now);
        write_pre_for_select(&root, "pre-old", "iid-old-kick", "Kick", "inactive", &now);
        write_pre_for_select(
            &root,
            "pre-current",
            "iid-current-vocal",
            "Vocal",
            "inactive",
            &now,
        );
        write_pre_for_select(
            &root,
            "pre-current",
            "iid-current-bass",
            "Bass",
            "inactive",
            &now,
        );
        write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");
        write_post_for_scope(&root, "post-current", "post-bass", "Bass");

        let v = enumerate_active_pre_pair_candidates_for_post_project(&root, "post-current");
        let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
        assert_eq!(ids, vec!["iid-current-bass", "iid-current-vocal"]);
    }

    /// 別セッション側に同じ Name が残っていても、現在 POST 群の name 集合で PRE 群を
    /// スコープできれば Arm 対象は現在側に決まる。
    #[test]
    fn select_target_pre_for_arm_for_post_project_avoids_other_session_same_name() {
        let root = isolated_dir();
        let now = now_rfc3339();
        write_pre_for_select(&root, "pre-old", "iid-old-vocal", "Vocal", "inactive", &now);
        write_pre_for_select(
            &root,
            "pre-current",
            "iid-current-vocal",
            "Vocal",
            "inactive",
            &now,
        );
        write_pre_for_select(
            &root,
            "pre-current",
            "iid-current-bass",
            "Bass",
            "inactive",
            &now,
        );
        write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");
        write_post_for_scope(&root, "post-current", "post-bass", "Bass");

        assert!(
            select_target_pre_for_arm(&root, "Vocal").is_none(),
            "unscoped Arm は同名2件で曖昧"
        );
        let sel = select_target_pre_for_arm_for_post_project(&root, "Vocal", "post-current")
            .expect("scoped Arm は現在 session 側の Vocal に決まる");
        assert_eq!(sel.instance_id, "iid-current-vocal");
    }

    /// name 集合の overlap が同点なら、後段の name 一意選定で曖昧として拒否する。
    #[test]
    fn select_target_pre_for_arm_for_post_project_tie_remains_ambiguous() {
        let root = isolated_dir();
        let now = now_rfc3339();
        write_pre_for_select(&root, "pre-a", "iid-a-vocal", "Vocal", "inactive", &now);
        write_pre_for_select(&root, "pre-a", "iid-a-bass", "Bass", "inactive", &now);
        write_pre_for_select(&root, "pre-b", "iid-b-vocal", "Vocal", "inactive", &now);
        write_pre_for_select(&root, "pre-b", "iid-b-bass", "Bass", "inactive", &now);
        write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");
        write_post_for_scope(&root, "post-current", "post-bass", "Bass");

        assert!(
            select_target_pre_for_arm_for_post_project(&root, "Vocal", "post-current").is_none(),
            "同点 session は誤選択せず曖昧拒否"
        );
    }

    /// POST 側に pair_pre_name があるのに一致する PRE 群が無い場合は、古い別セッションへ
    /// fallback せず空候補にする。
    #[test]
    fn discover_pre_dirs_for_post_project_does_not_fallback_when_names_disagree() {
        let root = isolated_dir();
        let now = now_rfc3339();
        write_pre_for_select(&root, "pre-old", "iid-old-snare", "Snare", "inactive", &now);
        write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");

        assert!(
            discover_pre_dirs_for_post_project(&root, "post-current").is_empty(),
            "POST name がある状態で不一致なら cross-session fallback しない"
        );
    }

    // ── B-104: Arm 経路（select_target_pre_for_arm）は Active 要求を外す ─────────────────

    /// 核: Inactive + fresh + 一意 → Arm=Some（v1.0.0 アーム→再生）/ Display=None（不変）。
    #[test]
    fn select_target_pre_for_arm_inactive_fresh_returns_some() {
        let root = isolated_dir();
        write_pre_for_select(
            &root,
            "puid-1",
            "iid-A",
            "snare",
            "inactive",
            &now_rfc3339(),
        );
        // Display は Active 要求のまま除外（B-059 / ZSA 不変）。
        assert!(
            select_target_pre(&root, "snare").is_none(),
            "Display: Inactive は除外（不変）"
        );
        // Arm は Inactive を許容（B-104 / 停止中アーム）。
        let sel =
            select_target_pre_for_arm(&root, "snare").expect("Arm: Inactive+fresh+一意 → Some");
        assert_eq!(sel.instance_id, "iid-A");
    }

    // ── B-108: read_pre_at / resolve_arm_target（ラッチ keep 経路）─────────────────

    #[test]
    fn read_pre_at_fresh_active() {
        let root = isolated_dir();
        write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now_rfc3339());
        let p = root.join("puid-1").join("iid-A").join("pre.json");
        let st = read_pre_at(&p).expect("fresh active → Some");
        assert_eq!(st.name.as_deref(), Some("snare"));
        assert!(st.active && st.fresh);
    }

    #[test]
    fn read_pre_at_idle_stale_gone() {
        let root = isolated_dir();
        let p = root.join("puid-1").join("iid-A").join("pre.json");
        // idle: fresh だが signal_state!=active。
        write_pre_for_select(
            &root,
            "puid-1",
            "iid-A",
            "snare",
            "inactive",
            &now_rfc3339(),
        );
        let st = read_pre_at(&p).unwrap();
        assert!(!st.active && st.fresh, "idle: active=false fresh=true");
        // stale: t age > NO_PRE_SECS。
        write_pre_for_select(
            &root,
            "puid-1",
            "iid-A",
            "snare",
            "active",
            &old_rfc3339(20),
        );
        assert!(!read_pre_at(&p).unwrap().fresh, "stale>10s → fresh=false");
        // gone。
        fs::remove_file(&p).unwrap();
        assert!(read_pre_at(&p).is_none(), "不在 → None");
    }

    /// T6 系: ラッチ済みなら同名 2 台目（曖昧）が居ても resolve_arm_target はラッチ先 instance_id を返す
    /// （素の select_target_pre_for_arm は曖昧で None になるところを、ラッチが結合を保持する）。
    #[test]
    fn resolve_arm_target_uses_latch_over_ambiguous() {
        let root = isolated_dir();
        let now = now_rfc3339();
        write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now);
        let pre_json = root.join("puid-1").join("iid-A").join("pre.json");
        let latched = std::sync::Mutex::new(Some(LatchedPre {
            name: "snare".to_string(),
            instance_id: "iid-A".to_string(),
            project_dir: root.join("puid-1"),
            pre_json,
        }));
        // 同名 2 台目（曖昧）。
        write_pre_for_select(&root, "puid-2", "iid-B", "snare", "active", &now);
        assert!(
            select_target_pre_for_arm(&root, "snare").is_none(),
            "曖昧（同名2件）→ 素の Arm 選定は None"
        );
        let sel = resolve_arm_target(&root, "snare", &latched).expect("ラッチ済み → Some");
        assert_eq!(sel.instance_id, "iid-A", "ラッチ先を直接返す（曖昧を無視）");
    }

    /// 未ラッチ時は select_target_pre_for_arm にフォールバックする。
    #[test]
    fn resolve_arm_target_falls_back_when_unlatched() {
        let root = isolated_dir();
        write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now_rfc3339());
        let latched = std::sync::Mutex::new(None);
        let sel = resolve_arm_target(&root, "snare", &latched).expect("未ラッチ → fallback Some");
        assert_eq!(sel.instance_id, "iid-A");
    }

    /// Arm でも Bypassed は除外（scan_pre_candidates_in が Bypassed のみ落とす）。
    #[test]
    fn select_target_pre_for_arm_bypassed_excluded() {
        let root = isolated_dir();
        write_pre_for_select(
            &root,
            "puid-1",
            "iid-A",
            "snare",
            "bypassed",
            &now_rfc3339(),
        );
        assert!(
            select_target_pre_for_arm(&root, "snare").is_none(),
            "Arm: Bypassed は除外（非Bypassed 要求）"
        );
    }

    /// Arm でも freshness は維持（古 t は除外）。
    #[test]
    fn select_target_pre_for_arm_stale_t_excluded() {
        let root = isolated_dir();
        write_pre_for_select(
            &root,
            "puid-1",
            "iid-A",
            "snare",
            "inactive",
            &old_rfc3339(20),
        );
        assert!(
            select_target_pre_for_arm(&root, "snare").is_none(),
            "Arm: 古 t（≥10s）は除外（fresh 要求は維持）"
        );
    }

    /// Arm でも一意性は維持（同名 2 件は曖昧沈黙）。Inactive 同士でも None。
    #[test]
    fn select_target_pre_for_arm_ambiguous_returns_none() {
        let root = isolated_dir();
        let now = now_rfc3339();
        write_pre_for_select(&root, "puid-1", "iid-A", "snare", "inactive", &now);
        write_pre_for_select(&root, "puid-2", "iid-B", "snare", "inactive", &now);
        assert!(
            select_target_pre_for_arm(&root, "snare").is_none(),
            "Arm: 同名 2 件は None（曖昧沈黙・一意性維持）"
        );
    }

    /// Arm でも空名は None。
    #[test]
    fn select_target_pre_for_arm_empty_name_returns_none() {
        let root = isolated_dir();
        write_pre_for_select(
            &root,
            "puid-1",
            "iid-A",
            "snare",
            "inactive",
            &now_rfc3339(),
        );
        assert!(
            select_target_pre_for_arm(&root, "").is_none(),
            "Arm: 空名は None"
        );
    }

    // ── B-103: sweep_stale_pending_in（起動時 dead Pending 掃除）─────────────────────

    /// status を保ったまま `t` を `secs_ago` 古くする（stale fixture 作成）。`now` 共有で境界を厳密化。
    fn backdate_signal_t(base: &Path, ph: &str, post_iid: &str, now: DateTime<Utc>, secs_ago: i64) {
        let mut sig = read_signal(base, ph, post_iid).expect("signal exists");
        sig.t = iso_ago(secs_ago, now);
        write_signal(base, ph, post_iid, &sig).unwrap();
    }

    /// 古い Pending（age > STALE_PENDING_SECS）だけ掃除し、fresh Pending と Acknowledged は保持。
    #[test]
    fn sweep_stale_pending_removes_only_dead_pending() {
        let base = isolated_dir();
        let now = Utc::now();
        // (a) stale Pending（書込 POST 消失相当）: t を STALE_PENDING_SECS+10 古く。
        write_pending(&base, "ph", "post-stale", "pre-x".into(), "daw".into()).unwrap();
        backdate_signal_t(&base, "ph", "post-stale", now, STALE_PENDING_SECS + 10);
        // (b) fresh Pending（進行中 Keep）: t=now。
        write_pending(&base, "ph", "post-fresh", "pre-x".into(), "daw".into()).unwrap();
        // (c) Acknowledged（古くても対象外＝生記録扱い）。
        write_pending(&base, "ph", "post-ack", "pre-y".into(), "daw".into()).unwrap();
        mark_acknowledged(&base, "ph", "post-ack").unwrap();
        backdate_signal_t(&base, "ph", "post-ack", now, STALE_PENDING_SECS + 10);

        let cleared = sweep_stale_pending_in(&base, now, STALE_PENDING_SECS);
        assert_eq!(cleared, 1, "stale Pending 1 件のみ掃除");
        assert!(
            read_signal(&base, "ph", "post-stale").is_none(),
            "stale Pending は削除"
        );
        assert!(
            read_signal(&base, "ph", "post-fresh").is_some(),
            "fresh Pending は保持"
        );
        assert!(
            read_signal(&base, "ph", "post-ack").is_some(),
            "Acknowledged は保持（age 無関係）"
        );
    }

    /// しきい値ちょうど(=stale_secs)は保持、超過(+1)で掃除（is_timed_out と同じ strict-greater 境界）。
    #[test]
    fn sweep_stale_pending_boundary() {
        let base = isolated_dir();
        let now = Utc::now();
        write_pending(&base, "ph", "post-edge", "pre".into(), "daw".into()).unwrap();
        backdate_signal_t(&base, "ph", "post-edge", now, STALE_PENDING_SECS);
        assert_eq!(
            sweep_stale_pending_in(&base, now, STALE_PENDING_SECS),
            0,
            "境界ちょうど = 保持"
        );
        backdate_signal_t(&base, "ph", "post-edge", now, STALE_PENDING_SECS + 1);
        assert_eq!(
            sweep_stale_pending_in(&base, now, STALE_PENDING_SECS),
            1,
            "超過 = 掃除"
        );
    }
}
