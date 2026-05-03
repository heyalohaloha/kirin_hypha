//! record_signal.json — POST → PRE 自動追従シグナル（G-50-34 / G-50-35）。
//!
//! # ファイル構造（A-3 修正後 / Q2 平坦化）
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
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

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
    base_dir.join(project_hash).join(SIGNALS_SUBDIR)
}

/// `{base}/{project_hash}/record_signal/{post_instance_id}.json`。
pub fn signal_path(base_dir: &Path, project_hash: &str, post_instance_id: &str) -> PathBuf {
    signals_dir(base_dir, project_hash).join(format!("{post_instance_id}.json"))
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

/// `{project_hash}/record_signal/` 配下の `*.json` を全件読み込んで返す。
///
/// 各要素は `(post_instance_id, RecordSignal)`。filename stem を post_instance_id
/// として返すため、呼び出し側はファイル名と signal 内の `requested_by` の整合
/// を別途確認しなくてよい。
///
/// パース不能・I/O 失敗ファイルは silently skip。返値は post_instance_id 辞書順。
pub fn scan_signals_dir(
    base_dir: &Path,
    project_hash: &str,
) -> Vec<(String, RecordSignal)> {
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

// ── PRE ペアリング（A-3 後: instance_id 横断走査）─────────────────────────────

/// `/tmp/kirin/{ph}/{instance_id}/pre.json` 1 件分のパース結果。
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
        let Ok(bytes) = fs::read(&pre_file) else { continue };
        let Ok(parsed): Result<PreTmpJson, _> = serde_json::from_slice(&bytes) else {
            continue;
        };
        out.push(PreCandidate {
            instance_id: parsed.instance_id,
            lufs_m: parsed.lufs_m,
            true_peak: parsed.true_peak,
            crest: parsed.crest,
            path: pre_file,
        });
    }
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
/// - 複数 + いずれかメトリック不在 → 最初の候補を返す（ソート済みで安定）
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
        let s = RecordSignal::new_pending(
            "post-001".into(),
            "pre-xyz".into(),
            "daw-uuid-1".into(),
        );
        let json = serde_json::to_string(&s).unwrap();
        let back: RecordSignal = serde_json::from_str(&json).unwrap();
        assert_eq!(s, back);
    }

    #[test]
    fn signal_path_uses_post_instance_id_as_filename() {
        let p = signal_path(Path::new("/tmp/kb"), "phash", "post-uuid-A");
        assert_eq!(
            p,
            Path::new("/tmp/kb/phash/record_signal/post-uuid-A.json")
        );
    }

    // ── I/O ─────────────────────────────────────────────────

    #[test]
    fn write_pending_creates_file_with_pending_status() {
        let base = isolated_dir();
        let s = write_pending(
            &base,
            "ph",
            "post-1",
            "pre-1".into(),
            "daw-1".into(),
        )
        .unwrap();
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
        assert_eq!(loaded.daw_session_id, "daw-1", "daw_session_id preserved on transition");
    }

    #[test]
    fn started_at_preserved_across_transitions() {
        let base = isolated_dir();
        let initial =
            write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
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
        let changed =
            mark_acknowledged_with_name(&base, "ph", "post-1", "Studio Mix").unwrap();
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
        let mut sig =
            RecordSignal::new_pending("p".into(), "r".into(), "d".into());
        sig.t = iso_ago(30, now);
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
        sig.t = iso_ago(31, now);
        assert!(is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    }

    #[test]
    fn timeout_not_considered_for_non_pending() {
        let now = Utc::now();
        let mut sig =
            RecordSignal::new_pending("p".into(), "r".into(), "d".into());
        sig.t = iso_ago(3600, now);
        sig.status = SignalStatus::Acknowledged;
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
        sig.status = SignalStatus::Released;
        assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    }

    #[test]
    fn timeout_invalid_iso_is_false() {
        let now = Utc::now();
        let mut sig =
            RecordSignal::new_pending("p".into(), "r".into(), "d".into());
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
        let dir = tmp_base.join(ph).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("pre.json");
        let json = if let Some((l, t, c)) = metrics {
            format!(
                r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","t":"now","lufs_m":{l},"true_peak":{t},"crest":{c},"psr":8.0}}"#
            )
        } else {
            format!(
                r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"inactive","t":"now"}}"#
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

    // ── シーケンス統合 ──────────────────────────────────────

    #[test]
    fn full_post_to_pre_handshake_sequence() {
        let base = isolated_dir();
        let sig =
            write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
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
}
