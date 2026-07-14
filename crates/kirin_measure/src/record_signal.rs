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
//!   "started_at": "ISO 8601（pending 配置時に固定、以後不変）",
//!   "started_at_position_samples": "明示された bounce/range の host native 開始位置（ある場合のみ）"
//! }
//! ```
//!
//! `t` は status の更新時刻、`started_at` は Record 開始の wall-clock barrier。
//! 通常の Keep はここにクリック時点の再生位置を入れず、Record を arm するだけにする。
//! 音声範囲の実開始位置は PRE/POST の Audio Thread が最初にキャプチャした
//! process window から確定する。`started_at_position_samples` は、Kirin OS/host から
//! 明示的な bounce/range 開始が渡された場合だけ shared native clock barrier として使う。
//! `daw_session_id` は POST 側 [`crate::daw_session_id`] の値を保存する。PRE/POST は
//! AU/VST3 や別 cdylib 境界で `static` 状態が一致しないため、PRE 側 ack は
//! `daw_session_id` では filter せず、永続 `target_pre_instance_id` 一致を正本にする。
//!
//! # PRE 側 polling
//! POST は canonical signal と同時に installation root の
//! `record_target/{pre_iid}.json` を publish する。inbox envelope が canonical の
//! `project_hash` を運ぶため、未接続 PRE は自身の 1 ファイルだけを読み、接続後は既知の
//! POST canonical 1 ファイルだけを読む。project/history directory scan は行わない。
//!
//! # POST 側 ライフサイクル
//! 1. POST「Keep」→ 排他 OK → [`write_pending`] で自身の post_instance_id 用 signal 配置
//! 2. PRE が ack → [`mark_acknowledged`]
//! 3. POST「Stop」→ [`mark_released_with_reason`] → 30 秒後に自動 [`delete_signal`]
//!
//! # ペアリング距離（G-50-35）
//! PRE 候補の走査・距離選択は [`crate::pre_candidates`] が所有する。

use chrono::{DateTime, Duration, Utc};
use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use uuid::Uuid;

use crate::record_expected::ExpectedWavMetadata;

mod stale_pending;

pub use stale_pending::{
    sweep_stale_pending_at_startup, sweep_stale_pending_in, STALE_PENDING_SECS,
};

/// pending → acknowledged タイムアウト（秒）。
pub const ACK_TIMEOUT_SECONDS: i64 = 30;
pub const RECORD_START_BARRIER_DELAY_MS: i64 = 500;

/// record_signal ディレクトリ名（`{project_hash}/record_signal/`）。
pub const SIGNALS_SUBDIR: &str = "record_signal";
pub const TARGET_SIGNALS_SUBDIR: &str = "record_target";

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

/// Released が Record 終了を意味する場合の明示理由。
///
/// 理由なし `released` は Drop / cleanup / 旧互換 marker として扱い、PRE は Record を
/// 閉じない。PRE 停止権限はこの enum の許可理由に限定する。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ReleaseReason {
    ManualStop,
    AllStop,
    IdleTimeout,
    DropCommitted,
}

impl ReleaseReason {
    pub fn authorizes_pre_stop(self) -> bool {
        matches!(
            self,
            Self::ManualStop | Self::AllStop | Self::IdleTimeout | Self::DropCommitted
        )
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
    /// Producer transaction identity. PRE, POST, expected claim, pair manifest,
    /// and Drop commit must carry this exact value.
    #[serde(default)]
    pub capture_generation_id: String,
    #[serde(default)]
    pub generation_started_at_ms: i64,
    /// 状態遷移の最終時刻（ISO 8601 / RFC 3339, ミリ秒精度 / UTC）。
    pub t: String,
    /// pending 配置時刻（以後 status 遷移で更新されない）。
    /// 旧バージョン互換のため `#[serde(default)]`（不在で空文字）。
    #[serde(default)]
    pub started_at: String,
    /// Host native sample position 上の明示 Record 開始 barrier。
    ///
    /// 通常の Keep はクリック位置をここに保存しない。Record に入った後、最初に
    /// 実キャプチャされた process window の先頭位置を各インスタンスがラッチする。
    /// 明示 bounce/range 開始が事前に渡された場合だけ、この値を timeline の
    /// origin として優先する。旧 schema / transport position 不明時は None。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub started_at_position_samples: Option<i64>,
    /// PRE 側 ack 時に書き込む PRE 表示用 Name (B-023 段階 3 / G-115-40 案 A-3)。
    /// 旧 schema からの読込 / POST write_pending 時は空文字で defaulted。
    /// 空 = POST GUI 側で UUID 短縮 8 文字 fallback 表示。
    #[serde(default)]
    pub paired_pre_name: String,
    /// `status=released` の停止理由。None は cleanup / 旧 schema の released marker。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub release_reason: Option<ReleaseReason>,
    /// Legacy explicit pre-bound WAV metadata. Normal Keep leaves this absent so a previous
    /// `current.json` generation can never arm a new Record; Drop-time reconciliation owns the
    /// current contract.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub expected_wav: Option<ExpectedWavMetadata>,
}

/// One direct delivery addressed to a PRE. The envelope owns the only cross-project routing
/// information PRE needs; the signal itself remains identical to its canonical POST record.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
struct TargetRecordSignal {
    project_hash: String,
    signal: RecordSignal,
}

impl RecordSignal {
    /// 新規 pending シグナルを生成。`t` と `started_at` は現在時刻、
    /// `daw_session_id` は呼び出し側が責任を持って渡す。
    pub fn new_pending(
        requested_by: String,
        target_pre_instance_id: String,
        daw_session_id: String,
        expected_wav: Option<ExpectedWavMetadata>,
        started_at_position_samples: Option<i64>,
    ) -> Self {
        Self::new_pending_for_generation(
            requested_by,
            target_pre_instance_id,
            daw_session_id,
            expected_wav,
            started_at_position_samples,
            String::new(),
            0,
            Uuid::new_v4().to_string(),
        )
    }

    #[allow(clippy::too_many_arguments)]
    pub fn new_pending_for_generation(
        requested_by: String,
        target_pre_instance_id: String,
        daw_session_id: String,
        expected_wav: Option<ExpectedWavMetadata>,
        started_at_position_samples: Option<i64>,
        capture_generation_id: String,
        generation_started_at_ms: i64,
        session_id: String,
    ) -> Self {
        let now = Utc::now();
        let t = iso8601(now);
        let started_at = iso8601(now + Duration::milliseconds(RECORD_START_BARRIER_DELAY_MS));
        Self {
            status: SignalStatus::Pending,
            requested_by,
            target_pre_instance_id,
            daw_session_id,
            session_id,
            capture_generation_id,
            generation_started_at_ms,
            t,
            started_at,
            started_at_position_samples,
            paired_pre_name: String::new(),
            release_reason: None,
            expected_wav,
        }
    }

    pub fn released_authorizes_pre_stop(&self) -> bool {
        self.status == SignalStatus::Released
            && self
                .release_reason
                .is_some_and(ReleaseReason::authorizes_pre_stop)
    }

    pub fn expected_end_position_samples_for_sample_rate(
        &self,
        native_sample_rate: u32,
    ) -> Option<i64> {
        if native_sample_rate == 0 {
            return None;
        }
        let start = self.started_at_position_samples?;
        let expected = self.expected_wav.as_ref()?;
        if expected.expected_sample_rate != native_sample_rate {
            return None;
        }
        let duration = expected.expected_duration_samples;
        if duration == 0 {
            return None;
        }
        let duration = duration.min(i64::MAX as u64) as i64;
        Some(start.saturating_add(duration))
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

/// `{base}/record_target/{pre_instance_id}.json`。PRE installation 内で一意の direct inbox。
pub fn target_signal_path(base_dir: &Path, pre_instance_id: &str) -> PathBuf {
    let iid = crate::path_identity::guard_path_component(
        pre_instance_id,
        "record_signal.target_path.pre_instance_id",
    );
    base_dir
        .join(TARGET_SIGNALS_SUBDIR)
        .join(format!("{iid}.json"))
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
    write_pending_with_expected(
        base_dir,
        project_hash,
        post_instance_id,
        target_pre_instance_id,
        daw_session_id,
        None,
    )
}

/// pending シグナルを expected WAV metadata とともに atomic 書込。
pub fn write_pending_with_expected(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    target_pre_instance_id: String,
    daw_session_id: String,
    expected_wav: Option<ExpectedWavMetadata>,
) -> Result<RecordSignal, SignalError> {
    write_pending_with_expected_and_clock(
        base_dir,
        project_hash,
        post_instance_id,
        target_pre_instance_id,
        daw_session_id,
        expected_wav,
        None,
    )
}

/// pending シグナルを expected WAV metadata + native start clock とともに atomic 書込。
#[allow(clippy::too_many_arguments)]
pub fn write_pending_with_expected_and_clock(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    target_pre_instance_id: String,
    daw_session_id: String,
    expected_wav: Option<ExpectedWavMetadata>,
    started_at_position_samples: Option<i64>,
) -> Result<RecordSignal, SignalError> {
    let signal = RecordSignal::new_pending(
        post_instance_id.to_string(),
        target_pre_instance_id,
        daw_session_id,
        expected_wav,
        started_at_position_samples,
    );
    write_signal(base_dir, project_hash, post_instance_id, &signal)?;
    Ok(signal)
}

/// pending シグナルと、WAV 世代をまだ持たない Record session lifecycle を生成する。
///
/// `record_expected/current.json` は前回 Drop の世代を保持し得るため、Keep 時には読まない。
/// 今回の Drop 後に reconciliation が同じ session_id へ exact WAV metadata を結合する。
#[allow(clippy::too_many_arguments)]
pub fn write_pending_claiming_expected_and_clock(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    target_pre_instance_id: String,
    daw_session_id: String,
    started_at_position_samples: Option<i64>,
) -> Result<RecordSignal, SignalError> {
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        project_hash.to_string(),
        post_instance_id.to_string(),
        target_pre_instance_id.clone(),
        daw_session_id.clone(),
        std::process::id(),
    );
    crate::capture_generation::publish_generation_roster(base_dir, &generation)
        .map_err(capture_generation_signal_error)?;
    write_pending_claiming_expected_and_clock_for_generation(
        base_dir,
        project_hash,
        post_instance_id,
        target_pre_instance_id,
        daw_session_id,
        started_at_position_samples,
        &generation,
    )
}

#[allow(clippy::too_many_arguments)]
pub fn write_pending_claiming_expected_and_clock_for_generation(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    target_pre_instance_id: String,
    daw_session_id: String,
    started_at_position_samples: Option<i64>,
    generation: &crate::capture_generation::CaptureGeneration,
) -> Result<RecordSignal, SignalError> {
    if !generation.is_valid() {
        return Err(SignalError::Io(io::Error::new(
            io::ErrorKind::InvalidData,
            "invalid capture generation",
        )));
    }
    let Some(member) = generation.member(project_hash, post_instance_id) else {
        return Err(SignalError::Io(io::Error::new(
            io::ErrorKind::InvalidData,
            "capture generation does not contain POST member",
        )));
    };
    if member.pre_instance_id != target_pre_instance_id {
        return Err(SignalError::Io(io::Error::new(
            io::ErrorKind::InvalidData,
            "capture generation PRE does not match Record target",
        )));
    }
    let signal = RecordSignal::new_pending_for_generation(
        post_instance_id.to_string(),
        target_pre_instance_id,
        daw_session_id,
        None,
        started_at_position_samples,
        generation.capture_generation_id.clone(),
        generation.started_at_ms,
        member.record_session_id.clone(),
    );
    write_signal(base_dir, project_hash, post_instance_id, &signal)?;
    if let Err(e) = crate::record_expected::begin_expected_session_for_generation(
        base_dir,
        project_hash,
        &signal.session_id,
        post_instance_id,
        &signal.capture_generation_id,
        signal.generation_started_at_ms,
    ) {
        log::warn!(
            "[record_signal] session lifecycle marker write failed; Keep continues: {}",
            e
        );
    }
    Ok(signal)
}

fn capture_generation_signal_error(
    error: crate::capture_generation::CaptureGenerationError,
) -> SignalError {
    match error {
        crate::capture_generation::CaptureGenerationError::Io(error) => SignalError::Io(error),
        crate::capture_generation::CaptureGenerationError::Serde(error) => {
            SignalError::Serde(error)
        }
        crate::capture_generation::CaptureGenerationError::Invalid => SignalError::Io(
            io::Error::new(io::ErrorKind::InvalidData, "invalid capture generation"),
        ),
    }
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
    if !signal.target_pre_instance_id.trim().is_empty() {
        let target_json = serde_json::to_vec(&TargetRecordSignal {
            project_hash: project_hash.to_string(),
            signal: signal.clone(),
        })?;
        if let Err(error) = crate::atomic_file::write_bytes_atomic(
            &target_signal_path(base_dir, &signal.target_pre_instance_id),
            &target_json,
        ) {
            // Pending is the publication transaction: the PRE inbox is its commit point.
            // If the commit cannot be published, leave neither a hidden canonical request nor a
            // half-started Record. Later status transitions retain canonical history because the
            // exact paired PRE already knows the POST key and reads that file directly.
            if signal.status == SignalStatus::Pending {
                let _ = fs::remove_file(&final_path);
            }
            return Err(SignalError::Io(error));
        }
    }
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

/// Read the one inbox owned by this PRE. The payload must name the same PRE and a non-empty
/// canonical POST key; corrupt/stale cross-target files are ignored without falling back to a
/// directory inventory.
pub fn read_target_signal(
    base_dir: &Path,
    pre_instance_id: &str,
) -> Option<(String, String, RecordSignal)> {
    let bytes = fs::read(target_signal_path(base_dir, pre_instance_id)).ok()?;
    let target: TargetRecordSignal = serde_json::from_slice(&bytes).ok()?;
    let signal = target.signal;
    if signal.target_pre_instance_id != pre_instance_id || signal.requested_by.trim().is_empty() {
        return None;
    }
    let project_hash = crate::path_identity::guard_path_component(
        &target.project_hash,
        "record_signal.read_target.project_hash",
    );
    Some((
        project_hash.into_owned(),
        signal.requested_by.clone(),
        signal,
    ))
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
    signal.release_reason = None;
    write_signal(base_dir, project_hash, post_instance_id, &signal)?;
    Ok(true)
}

/// 状態遷移: * → released。理由なし released は cleanup marker であり、PRE 停止権限を持たない。
pub fn mark_released(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Result<bool, SignalError> {
    mark_released_inner(base_dir, project_hash, post_instance_id, None)
}

/// 状態遷移: * → released + 明示停止理由。
pub fn mark_released_with_reason(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    reason: ReleaseReason,
) -> Result<bool, SignalError> {
    mark_released_inner(base_dir, project_hash, post_instance_id, Some(reason))
}

fn mark_released_inner(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    reason: Option<ReleaseReason>,
) -> Result<bool, SignalError> {
    transition_status(
        base_dir,
        project_hash,
        post_instance_id,
        SignalStatus::Released,
        reason,
    )
}

fn transition_status(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
    next: SignalStatus,
    release_reason: Option<ReleaseReason>,
) -> Result<bool, SignalError> {
    let Some(mut signal) = read_signal(base_dir, project_hash, post_instance_id) else {
        return Ok(false);
    };
    signal.status = next;
    signal.t = now_iso8601();
    signal.release_reason = if next == SignalStatus::Released {
        release_reason
    } else {
        None
    };
    write_signal(base_dir, project_hash, post_instance_id, &signal)?;
    Ok(true)
}

/// record_signal.json を削除。不在は成功扱い。
pub fn delete_signal(
    base_dir: &Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Result<(), SignalError> {
    let prior = read_signal(base_dir, project_hash, post_instance_id);
    let path = signal_path(base_dir, project_hash, post_instance_id);
    match fs::remove_file(&path) {
        Ok(()) => {}
        Err(e) if e.kind() == io::ErrorKind::NotFound => {}
        Err(e) => return Err(SignalError::Io(e)),
    }
    if let Some(prior) = prior.filter(|signal| !signal.target_pre_instance_id.trim().is_empty()) {
        let target = target_signal_path(base_dir, &prior.target_pre_instance_id);
        let owns_target = fs::read(&target)
            .ok()
            .and_then(|bytes| serde_json::from_slice::<TargetRecordSignal>(&bytes).ok())
            .is_some_and(|current| {
                current.project_hash == project_hash
                    && current.signal.requested_by == prior.requested_by
                    && current.signal.session_id == prior.session_id
            });
        if owns_target {
            match fs::remove_file(target) {
                Ok(()) => {}
                Err(e) if e.kind() == io::ErrorKind::NotFound => {}
                Err(e) => return Err(SignalError::Io(e)),
            }
        }
    }
    Ok(())
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
    iso8601(chrono::Utc::now())
}

fn iso8601(t: DateTime<Utc>) -> String {
    t.format("%Y-%m-%dT%H:%M:%S%.3fZ").to_string()
}

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests;
