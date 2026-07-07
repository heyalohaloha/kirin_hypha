//! plugin_data v1.1 writer — Record mode 計測結果の永続化。
//!
//!
//! # ファイル命名（A-3 修正後）
//! ```text
//! plugin_data/{project_hash}/{instance_id}/pre/{compact_wall_clock}.json
//! plugin_data/{project_hash}/{instance_id}/post/{compact_wall_clock}.json
//! ```
//! 旧 `{project_hash}/{bus}/{role}/...` から移行。bus 概念を path から外し、
//! 永続化される `instance_id`（plugin params 経由で project save に同梱）で
//! ディレクトリを区切ることで複数バス・複数インスタンスの衝突を回避。
//!
//! `compact_wall_clock` = `%Y%m%dT%H%M%S`（例: `20260417T143208`）
//!
//! # 書込方法
//! 1. 一時ファイル `{filename}.tmp` に追記
//! 2. 30秒毎に `rename()` で atomic flush（D-1 対策）
//! 3. 最後に `checksum` フィールドへ HMAC-SHA256 埋め込み
//! 4. 正常終了時に `status="closed"` 更新
//!
//! # 解像度（G-50-17）
//! | モード | frames[] | psb_snapshots[] |
//! |--------|---------|-----------------|
//! | Record リアルタイム | 10 fps | 2 fps |
//! | Record バウンス | 10 fps (audio-time) | 2 fps |
//!
//! caller（Plugin 側、T-6 で統合）が解像度に従って `append_frame` / `append_psb`
//! を呼ぶ。本モジュールは間引きロジックを持たない。
//!
//! # 数値精度（正本 行405-414）
//! append 時点で丸める（JSON に現れる数値がそのまま丸め後）。
//!
//! # HMAC checksum
//! トップレベル `"checksum"` フィールド。`checksum=""` で JSON 化した全バイトに対する
//! HMAC-SHA256 hex。鍵は [`crate::identity`] と同じ（ビルド時埋め込み）。
//!
//! # T-2 のスコープ
//! - スキーマ型定義（serde 相互変換）
//! - `PluginDataWriter`（append / heartbeat / flush / close / compact filename）
//!

use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

pub use crate::record_expected::ExpectedWavMetadata;

type HmacSha256 = Hmac<Sha256>;
const PAIR_RECORD_SESSIONS_DIR: &str = "record_sessions";
const PAIR_RECORD_MEMBERS_DIR: &str = ".pair_committed";
const PAIR_RECORD_SESSION_SCHEMA: &str = "pair_record_session.v1";

fn non_empty_string(value: Option<String>) -> Option<String> {
    value
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
}

// ── Role ─────────────────────────────────────────────────────────────────────

/// Plugin role（PRE / POST）。ファイルパス構築 + `role` フィールドに使用。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "UPPERCASE")]
pub enum Role {
    Pre,
    Post,
}

impl Role {
    /// ディレクトリ名（lowercase）。
    pub fn dir_name(self) -> &'static str {
        match self {
            Self::Pre => "pre",
            Self::Post => "post",
        }
    }

    /// JSON `role` フィールド値（uppercase）。
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Pre => "PRE",
            Self::Post => "POST",
        }
    }
}

// ── Status ───────────────────────────────────────────────────────────────────

/// 計測ファイルの生存状態（G-50-33）。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Status {
    Active,
    Closed,
}

// ── Schema 型 ────────────────────────────────────────────────────────────────

///
/// n_prime は **20 Bark 帯域別** のフィルタ後 N'(t,z)（sone）。
/// sharpness は **スカラー**（acum）。
///
/// PhaseDStream に投入されるため、本フィールドは常に `Some` で書き出される。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Frame {
    pub t_ms: u64,
    pub n_prime: Option<[f64; 20]>,
    pub sharpness: Option<f64>,
    pub lufs_m: f64,
    pub true_peak: f64,
    pub crest: f64,
    /// PSR: peak_dBFS − LUFS_S（B-043）。
    /// 3 秒未満は `MeasureResult.psr = None` のためここも `None`。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub psr: Option<f64>,
}

/// PSB スナップショット（2 fps 別ブロック）。
///
/// `interpolatable=true` は「前後スナップショット間を線形補間してよい」のマーカ。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PsbSnapshot {
    pub t_ms: u64,
    pub psb: [f64; 20],
    pub interpolatable: bool,
}

/// 利用者メモ（「メモを残す」タップ時に追加）。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Annotation {
    /// ISO 8601 wall clock.
    pub t: String,
    pub memo: String,
}

/// バウンスマーカ（G-50-18）。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BounceMarker {
    pub wall_clock_start: String,
    pub wall_clock_end: String,
    pub duration_samples: u64,
    pub first_block_hash: String,
    pub last_block_hash: String,
}

/// WAV と照合するための完成 take 情報。
///
/// `Record` 全体は手動 Keep/Stop の都合で長くなり得るが、Kirin OS に渡す正本は
/// WAV の 0 sample から `duration_samples` までに対応する take であることを明示する。
/// `duration_frames_48k` は Hypha 内部計測時間軸（48 kHz）上の長さで、DAW の出力
/// sample rate へ換算した値が `duration_samples`。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BounceTake {
    pub source: String,
    pub time_axis: String,
    pub alignment_status: String,
    pub sample_rate: u32,
    pub wav_start_sample: u64,
    pub wav_end_sample: u64,
    pub duration_samples: u64,
    pub duration_frames_48k: u64,
    pub start_t_ms: u64,
    pub end_t_ms: u64,
    pub trace_sample_count: u64,
    pub frame_count: u64,
}

/// TRACE bake diagnostics. These fields separate a real measured silence frame
/// from a missing TRACE slot so downstream UI never has to infer absence from
/// `-100`.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct TraceDiagnostics {
    pub raw_trace_count: u64,
    pub expected_frame_count: u64,
    pub measured_frame_count: u64,
    pub missing_slots: u64,
    pub explicit_silence_frame_count: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RecordQuality {
    /// `complete` = sample-count aligned full product.
    /// `usable_fallback` = draw-able measured data with explicit degradation reasons.
    /// `failed` = diagnostic artifact only.
    pub status: String,
    pub complete: bool,
    pub usable: bool,
    pub expected_wav_ready: bool,
    pub sample_count_ready: bool,
    pub trace_slots_complete: bool,
    pub expected_frame_count: u64,
    pub measured_frame_count: u64,
    pub missing_trace_slots: u64,
}

/// plugin_data/ 1 ファイル分のルート（現行 v1.3）。
///
/// v1.2 (A-3 (a)): `instance_id` field 追加 + `paired_pre_instance_id` /
/// `paired_post_instance_id` field 追加（cross-instance pair 復元の決定論的キー）。
/// v1.3 (B-043 / B-076): session aggregate と integrity field を additive 追加。
/// B-141: `started_at_ms` を additive 追加（schema_version は 1.3 維持）。
/// B-142: `pair_name` / `pair_pre_name` を additive 追加（schema_version は 1.3 維持）。
///
/// 各 field の詳細仕様は [`PluginDataFile::new`] の doc コメント参照。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginDataFile {
    pub schema_version: String,
    pub installation_id: String,
    pub project_hash: String,
    /// Plugin Default 起動時に `Uuid::new_v4` で生成され、VST3 state として
    /// 永続化される plugin インスタンス UUID。同一 plugin instance の PRE/POST
    /// ペア復元の一次キー。Lens reader.js で `paired_*_instance_id` との一致比較に
    /// 使用される（A-3 (a) v1.2）。
    pub instance_id: String,
    pub timestamp: String,
    pub role: Role,
    /// Bus 名は Phase 1 では path から外され（{project_hash}/{instance_id}/{role}/）
    /// PluginDataFile content では `Option<String>` として残す。Phase 1 では `None` を
    /// 書き込み（field omitted）、Lens 側 schema は optional として読む。
    /// 将来 Bus メタデータが復活した場合は `Some(bus_name)` として書き込み再開する。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub bus: Option<String>,
    pub mode: String,
    pub chain_memo: String,
    pub sample_rate: u32,
    #[serde(default)]
    pub source_format: u32,
    pub bounce_marker: BounceMarker,
    pub heartbeat: String,
    pub status: Status,
    pub frames: Vec<Frame>,
    pub psb_snapshots: Option<Vec<PsbSnapshot>>,
    pub annotations: Vec<Annotation>,
    /// EBU R128 Integrated Loudness [LUFS] (B-043 / セッション集計)。
    /// `close()` 直前に `set_session_aggregates` で注入。値が無い場合は省略。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub lufs_i: Option<f64>,
    /// EBU R128 Loudness Range [LU] (B-043 / セッション集計)。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub lra: Option<f64>,
    /// PLR = max_true_peak − lufs_i [dB] (B-043 / セッション集計)。
    /// `lufs_i` か `max_true_peak` のどちらかが欠ければ `None`。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub plr: Option<f64>,
    /// POST 側でのみ書き込み: Keep タップ時に選定した PRE 候補の instance_id。
    /// 不在時 None。Lens 側 cross-instance pair 復元の決定論的キー（A-3 (a) v1.2）。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub paired_pre_instance_id: Option<String>,
    /// PRE 側でのみ書き込み: Record 開始時に受信した record_signal の
    /// `requested_by`（POST 側 instance_id）。不在時 None。
    /// Lens 側 cross-instance pair 復元の決定論的キー（A-3 (a) v1.2）。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub paired_post_instance_id: Option<String>,
    /// Lens 表示用の pair 名。通常は対 PRE 名（POST）または自身の PRE 名（PRE）。
    /// 空文字は書かない。Lens 側 reader が名前表示へ使える optional metadata。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub pair_name: Option<String>,
    /// Lens 表示用の PRE 名。POST は選択中の `pair_pre_name`、PRE は自身の Name。
    /// 旧 JSON 互換のため optional。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub pair_pre_name: Option<String>,
    /// POST Keep 1 回ごとの Record session UUID。
    ///
    /// POST が `record_signal` に書いた session_id を PRE/POST の plugin_data に転記する。
    /// PRE/POST が同じ録音として閉じたかを後段で検証する pair-level key。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub record_session_id: Option<String>,
    /// Record 開始 wall-clock（epoch ms）。frame t_ms の原点。
    /// PRE=相手 POST の started_at、POST=自身の started_at を epoch ms 化（同一原点）。
    /// signal 不在/壊れ時の fallback では PRE/POST で異なり得る。
    /// 旧 .kirin 互換のため `#[serde(default)]`（0 = 未記録/旧版センチネル）。
    #[serde(default)]
    pub started_at_ms: i64,
    /// B-076: この Record 中に ring 満杯で測定 ring に push できなかったサンプル数
    /// （per-Record 差分 = Record 開始時 overflow snapshot との差）。計測値は汚さない露出のみ。
    /// 旧 .kirin 互換のため `#[serde(default)]`（schema_version "1.3" 維持・additive）。
    #[serde(default)]
    pub dropped_samples: u64,
    /// B-076: `dropped_samples > 0` → true（1 sample でも立てる・閾値なし）。count を併記し
    /// 程度を透明化する（隠さない / ZSA / integrity）。旧 .kirin 互換のため `#[serde(default)]`。
    #[serde(default)]
    pub integrity_degraded: bool,
    /// Record commit gate の結果。通常棚に publish された JSON は `committed`、`.failed/`
    /// 配下に残した診断 JSON は `failed`。旧 JSON 互換のため optional。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub commit_status: Option<String>,
    /// `commit_status=failed` または integrity degraded の理由。通常成功 JSON では省略。
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub integrity_reasons: Vec<String>,
    /// WAV header と突き合わせるための sample-accurate take metadata。
    /// 旧 JSON 互換のため optional additive field。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub bounce_take: Option<BounceTake>,
    /// Kirin OS/Hub から Record 開始前に渡された dropped WAV metadata。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub expected_wav: Option<ExpectedWavMetadata>,
    /// TRACE 欠損診断。missing は frames[] に測定値として入れない。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_diagnostics: Option<TraceDiagnostics>,
    /// Kirin OS 表示用の品質分類。厳密判定は Hypha 内に閉じ、OS はこの分類で
    /// complete / usable fallback / failed を静かに扱い分ける。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub record_quality: Option<RecordQuality>,
    pub validity: bool,
    pub checksum: String,
}

impl PluginDataFile {
    pub const SCHEMA_VERSION: &'static str = "1.3";

    /// 空の v1.3 ファイルを生成（heartbeat = timestamp = now, status=active, checksum=空）。
    ///
    /// # source_format（v1.2 (α) 文言 / Daisuke 判断 2026-05-01）
    /// Hz 単位の DAW 入力サンプルレート。0 = 取得失敗（fallback）。
    /// リサンプリング前の監査トレイル用。
    /// 値は `sample_rate` と同一値で書き込まれる（plugin_data.rs L204）。
    /// コメントから前方参照されているのみで、Hypha リポジトリ内に
    ///
    /// # bus
    /// Phase 1 では `None` を渡す（A-3 修正後 / Lens schema optional）。
    ///
    /// # instance_id（v1.2 (a)）
    /// Plugin Default 起動時に `Uuid::new_v4` で生成され、VST3 state として
    /// 永続化される plugin インスタンス UUID。同一 plugin instance の
    /// PRE/POST ペア復元の一次キー。
    ///
    /// # paired_*_instance_id（v1.2 (a)）
    /// Record 開始時に既知なら Some を渡す。
    /// - PRE: `paired_post_instance_id` = record_signal の `requested_by` を Some で渡す
    ///   `paired_pre_instance_id` = None（自分が PRE なので相手 PRE は無い）
    /// - POST: `paired_pre_instance_id` = trigger_keep の `target_id` を Some で渡す
    ///   `paired_post_instance_id` = None（自分が POST）
    ///
    /// # pair_name / pair_pre_name（B-142）
    /// Lens 表示用 metadata。PRE/POST の instance_id linkage は `paired_*_instance_id`
    /// が正本だが、人間可読名は Lens 側では復元不能なため Record 開始時の snapshot を
    /// optional field として焼き込む。
    ///
    /// # sample_rate
    /// Record 開始時に ProcessContext から取得した値。本フィールドは
    /// JSON `sample_rate` および `source_format` 両方に同一値で記録される。
    /// 取得失敗時は 0 を渡す（fallback 仕様）。
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        installation_id: String,
        project_hash: String,
        instance_id: String,
        role: Role,
        bus: Option<String>,
        sample_rate: u32,
        paired_pre_instance_id: Option<String>,
        paired_post_instance_id: Option<String>,
        pair_name: Option<String>,
        pair_pre_name: Option<String>,
    ) -> Self {
        let now = now_iso8601();
        Self {
            schema_version: Self::SCHEMA_VERSION.to_string(),
            installation_id,
            project_hash,
            instance_id,
            timestamp: now.clone(),
            role,
            bus,
            mode: "record".to_string(),
            chain_memo: String::new(),
            sample_rate,
            source_format: sample_rate,
            bounce_marker: BounceMarker {
                wall_clock_start: now.clone(),
                wall_clock_end: now.clone(),
                duration_samples: 0,
                first_block_hash: String::new(),
                last_block_hash: String::new(),
            },
            heartbeat: now,
            status: Status::Active,
            frames: Vec::new(),
            psb_snapshots: Some(Vec::new()),
            annotations: Vec::new(),
            // B-043: セッション集計値。`close()` 前に `set_session_aggregates` で注入。
            lufs_i: None,
            lra: None,
            plr: None,
            paired_pre_instance_id,
            paired_post_instance_id,
            pair_name: non_empty_string(pair_name),
            pair_pre_name: non_empty_string(pair_pre_name),
            record_session_id: None,
            started_at_ms: 0,
            dropped_samples: 0,
            integrity_degraded: false,
            commit_status: None,
            integrity_reasons: Vec::new(),
            bounce_take: None,
            expected_wav: None,
            trace_diagnostics: None,
            record_quality: None,
            validity: true,
            checksum: String::new(),
        }
    }
}

// ── Writer ───────────────────────────────────────────────────────────────────

/// 書込エラー。
#[derive(Debug)]
pub enum WriterError {
    Io(io::Error),
    Serde(serde_json::Error),
}

impl std::fmt::Display for WriterError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "IO error: {e}"),
            Self::Serde(e) => write!(f, "JSON error: {e}"),
        }
    }
}

impl std::error::Error for WriterError {}

impl From<io::Error> for WriterError {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl From<serde_json::Error> for WriterError {
    fn from(e: serde_json::Error) -> Self {
        Self::Serde(e)
    }
}

/// plugin_data/ への書込ハンドル。
///
/// 1 Record セッション = 1 Writer。
///
/// # 書込フロー
/// ```ignore
/// let paths = WriterPaths::build(&base, &project_hash, &bus, Role::Pre, &wall_clock_start);
/// let mut w = PluginDataWriter::create(paths, installation_id, project_hash, Role::Pre, bus, 48000)?;
///
/// // 計測中: caller が解像度に従って append
/// w.append_frame(frame)?;
/// w.append_psb(psb)?;
///
/// // 30 秒毎に flush（atomic rename）
/// w.heartbeat_now();
/// w.flush()?;
///
/// // 終了時
/// w.close()?;
/// ```
#[derive(Debug)]
pub struct PluginDataWriter {
    paths: WriterPaths,
    data: PluginDataFile,
}

/// ファイルパス一式（tmp + final）。
#[derive(Debug, Clone)]
pub struct WriterPaths {
    /// 最終 JSON（`{compact_wall_clock}.json`）。
    pub final_path: PathBuf,
    /// 書込中の tmp（`{compact_wall_clock}.json.tmp`）。
    pub tmp_path: PathBuf,
    /// Record 中の staging JSON。通常 reader は `*.json` だけを見るため拾わない。
    pub staging_path: PathBuf,
    /// PairRecordSession finalizer 待ちの候補 JSON。通常 reader は role 直下だけを見るため拾わない。
    pub pair_pending_path: PathBuf,
    /// commit gate で失敗した診断 JSON。通常 reader は role 直下しか見ない。
    pub failed_path: PathBuf,
}

impl WriterPaths {
    /// `plugin_data/{project_hash}/{instance_id}/{role_dir}/{compact}.json` を構築。
    ///
    /// `wall_clock_start` は ISO 8601（秒精度）。compact 形式への変換は
    /// [`compact_wall_clock`] を使用。
    ///
    /// A-3 修正後: 旧 `bus` セグメントは `instance_id` に置換された
    /// （永続化された Plugin 永続 instance UUID。Lens 側読取り `reader.js` も同調）。
    pub fn build(
        base_dir: &Path,
        project_hash: &str,
        instance_id: &str,
        role: Role,
        wall_clock_start_iso: &str,
    ) -> Self {
        let compact = compact_wall_clock(wall_clock_start_iso);
        // B-128 (G-115-370): within-base wall。restore 由来の path-unsafe な identity を base 内へ畳む。
        let ph = crate::path_identity::guard_path_component(
            project_hash,
            "WriterPaths.build.project_hash",
        );
        let iid = crate::path_identity::guard_path_component(
            instance_id,
            "WriterPaths.build.instance_id",
        );
        let dir = base_dir.join(&*ph).join(&*iid).join(role.dir_name());
        let final_path = dir.join(format!("{compact}.json"));
        let tmp_path = dir.join(format!("{compact}.json.tmp"));
        let staging_path = dir.join(format!("{compact}.json.partial"));
        let pair_pending_path = dir.join(".pair_pending").join(format!("{compact}.json"));
        let failed_path = dir.join(".failed").join(format!("{compact}.json"));
        Self {
            final_path,
            tmp_path,
            staging_path,
            pair_pending_path,
            failed_path,
        }
    }
}

impl PluginDataWriter {
    /// tmp ディレクトリを作成して Writer を起動。最初の flush で空ファイルが rename される。
    ///
    /// `bus` は Phase 1 では `None` を渡す（A-3 修正後）。将来 bus メタデータが復活
    /// したら `Some(bus_name)` で content にだけ書き込む（path には影響しない）。
    ///
    /// `instance_id` / `paired_*_instance_id` は v1.2 (a) cross-instance pair 復元用。
    /// `pair_name` / `pair_pre_name` は Lens 表示用の optional metadata。
    /// 詳細は [`PluginDataFile::new`] の doc コメント参照。
    #[allow(clippy::too_many_arguments)]
    pub fn create(
        paths: WriterPaths,
        installation_id: String,
        project_hash: String,
        instance_id: String,
        role: Role,
        bus: Option<String>,
        sample_rate: u32,
        paired_pre_instance_id: Option<String>,
        paired_post_instance_id: Option<String>,
        pair_name: Option<String>,
        pair_pre_name: Option<String>,
    ) -> Result<Self, WriterError> {
        if let Some(parent) = paths.final_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let data = PluginDataFile::new(
            installation_id,
            project_hash,
            instance_id,
            role,
            bus,
            sample_rate,
            paired_pre_instance_id,
            paired_post_instance_id,
            pair_name,
            pair_pre_name,
        );
        Ok(Self { paths, data })
    }

    /// 書込対象の内部データへの読取参照（テスト / デバッグ用）。
    pub fn data(&self) -> &PluginDataFile {
        &self.data
    }

    /// Record close 時の bake gate 用に TRACE frames を入れ替える。
    ///
    /// 逐次 append 中に欠けや順序揺れがあっても、close 直前に同一 duration の
    /// 連続 timeline として焼き直すための専用 API。
    pub fn clear_frames(&mut self) {
        self.data.frames.clear();
    }

    /// chain_memo を設定（利用者記入。Record 開始時 or 途中更新）。
    pub fn set_chain_memo(&mut self, memo: String) {
        self.data.chain_memo = memo;
    }

    /// Record 開始時刻を単一原点として焼く。
    ///
    /// `timestamp` と bounce start/end の初期値を record_signal.started_at 由来の
    /// wall-clock に揃え、PRE/POST の session_id と JSON メタが同じ開始時刻を参照する。
    pub fn set_record_start_wall_clock(&mut self, wall_clock: String) {
        self.data.timestamp = wall_clock.clone();
        self.data.heartbeat = wall_clock.clone();
        self.data.bounce_marker.wall_clock_start = wall_clock.clone();
        self.data.bounce_marker.wall_clock_end = wall_clock;
    }

    /// バウンス開始時情報を記録（最初のブロックハッシュ等）。
    pub fn set_bounce_start(&mut self, wall_clock: String, first_block_hash: String) {
        self.data.bounce_marker.wall_clock_start = wall_clock;
        self.data.bounce_marker.first_block_hash = first_block_hash;
    }

    /// バウンス終了時情報を更新（呼出時点の累積情報）。
    pub fn set_bounce_end(
        &mut self,
        wall_clock: String,
        duration_samples: u64,
        last_block_hash: String,
    ) {
        self.data.bounce_marker.wall_clock_end = wall_clock;
        self.data.bounce_marker.duration_samples = duration_samples;
        self.data.bounce_marker.last_block_hash = last_block_hash;
    }

    /// WAV と照合するための完成 take 情報を設定する。
    pub fn set_bounce_take(&mut self, take: BounceTake) {
        self.data.bounce_take = Some(take);
    }

    /// Record 開始前に latch 済みの WAV metadata を設定する。
    pub fn set_expected_wav(&mut self, expected_wav: Option<ExpectedWavMetadata>) {
        self.data.expected_wav = expected_wav.filter(ExpectedWavMetadata::is_usable);
    }

    /// TRACE bake 診断を設定する。
    pub fn set_trace_diagnostics(&mut self, diagnostics: TraceDiagnostics) {
        self.data.trace_diagnostics = Some(diagnostics);
    }

    /// WAV と対応する clean take 長へ timeline payload を切り詰める。
    ///
    /// 手動 Keep/Stop の余白や Record TRACE marker は診断上有用だが、Kirin OS が
    /// TRACE に使う `frames[]` / `psb_snapshots[]` は bounce_take と同じ時間軸に揃える。
    pub fn clip_timeline_to_duration(&mut self, end_t_ms: u64) {
        self.data.frames.retain(|frame| frame.t_ms <= end_t_ms);
        if let Some(psb) = &mut self.data.psb_snapshots {
            psb.retain(|snapshot| snapshot.t_ms <= end_t_ms);
        }
    }

    /// 1 frame を追加。数値を精度表に従って丸める。
    ///
    #[allow(clippy::too_many_arguments)]
    pub fn append_frame(
        &mut self,
        t_ms: u64,
        n_prime: [f64; 20],
        sharpness: f64,
        lufs_m: f64,
        true_peak: f64,
        crest: f64,
        psr: Option<f64>,
    ) {
        self.append_frame_optional(
            t_ms,
            Some(n_prime),
            Some(sharpness),
            lufs_m,
            true_peak,
            crest,
            psr,
        );
    }

    /// 1 frame を追加。Phase D の値が未ウォームアップでも LUFS/TP/Crest の TRACE を残す。
    #[allow(clippy::too_many_arguments)]
    pub fn append_frame_optional(
        &mut self,
        t_ms: u64,
        n_prime: Option<[f64; 20]>,
        sharpness: Option<f64>,
        lufs_m: f64,
        true_peak: f64,
        crest: f64,
        psr: Option<f64>,
    ) {
        let rounded_n = n_prime.map(|arr| {
            let mut rounded = [0.0; 20];
            for (i, v) in arr.iter().enumerate() {
                rounded[i] = round1(*v);
            }
            rounded
        });
        self.data.frames.push(Frame {
            t_ms,
            n_prime: rounded_n,
            sharpness: sharpness.filter(|v| v.is_finite()).map(round2),
            lufs_m: round1(lufs_m),
            true_peak: round1(true_peak),
            crest: round1(crest),
            psr: psr.filter(|v| v.is_finite()).map(round1),
        });
    }

    /// Record セッション集計値を JSON に注入する (B-043)。
    ///
    /// IO Thread が `close()` を呼ぶ直前に Measure Thread から受け取った
    /// `SessionSummary` を渡す。PLR = max_true_peak − lufs_i を内部計算する。
    /// いずれかの集計値が `None` のときは `plr` も `None` になる。
    pub fn set_session_aggregates(&mut self, summary: crate::engine::SessionSummary) {
        self.data.lufs_i = summary.lufs_i.map(round1);
        self.data.lra = summary.lra.map(round1);
        self.data.plr = summary
            .lufs_i
            .and_then(|i| summary.max_true_peak.map(|tp| tp - i))
            .filter(|v| v.is_finite())
            .map(round1);
    }

    /// B-076 / B-125: この Record の欠落サンプル数を JSON に焼き込む。`close()` 直前に呼ぶ。
    /// 2 つの独立カウンタの per-Record 差分を受ける（混ぜずに別計数＝metric truthfulness）。
    /// `push_dropped` = ring 満杯 drop（push_overflow 差分 / B-076）、`oversized_dropped` =
    /// prealloc-max 超の病的 block で測定 ring に渡せなかった interleaved sample 数（B-125 専用
    /// カウンタ oversized_drop 差分）。JSON へは合算を焼く（無記録欠落の解消＝ZSA）。
    /// `integrity_degraded` は合算 > 0 なら OR で立てる（閾値なし・1 sample でも立てる）。
    /// 既に検出済みの degraded reason を dropped_samples=0 で消さない。計測値（LUFS/TP/PSR/PSB）
    /// には一切触れない。
    pub fn set_integrity(&mut self, push_dropped: u64, oversized_dropped: u64) {
        let total = push_dropped.saturating_add(oversized_dropped);
        self.data.dropped_samples = total;
        if total > 0 {
            self.data.integrity_degraded = true;
        }
    }

    /// B-132 (G-115-382 共通B): drain-completion seal の bounded wait が timeout した
    /// （Measure Thread の死 / shutdown / stall で post-drain finalize が確定できなかった）場合に
    /// 「不完全を不完全と記録」するため `integrity_degraded` を **OR で立てる**（dropped_samples の
    /// 計数とは独立 / set_integrity の後に呼んで上書きされない）。silent truncation を残さない（R-28 /
    /// README:139）。計測値（LUFS/TP/PSR/PSB）には一切触れない。
    pub fn mark_integrity_degraded(&mut self) {
        self.data.integrity_degraded = true;
    }

    /// integrity degraded の理由を重複なしで記録する。
    pub fn add_integrity_reason(&mut self, reason: &str) {
        if reason.is_empty() {
            return;
        }
        if !self.data.integrity_reasons.iter().any(|r| r == reason) {
            self.data.integrity_reasons.push(reason.to_string());
        }
    }

    /// 1 PSB スナップショットを追加。
    ///
    /// ため本メソッドは無条件で push する。
    pub fn append_psb(&mut self, t_ms: u64, psb: [f64; 20], interpolatable: bool) {
        let snapshots = self.data.psb_snapshots.get_or_insert_with(Vec::new);
        let mut rounded = [0.0; 20];
        for (i, v) in psb.iter().enumerate() {
            rounded[i] = round1(*v);
        }
        snapshots.push(PsbSnapshot {
            t_ms,
            psb: rounded,
            interpolatable,
        });
    }

    /// 利用者メモを追加（「メモを残す」タップ）。
    pub fn append_annotation(&mut self, memo: String) {
        self.data.annotations.push(Annotation {
            t: now_iso8601(),
            memo,
        });
    }

    /// heartbeat を現在時刻に更新（T-3 と共通。30 秒毎に caller が呼ぶ）。
    pub fn heartbeat_now(&mut self) {
        self.data.heartbeat = now_iso8601();
    }

    /// validity フラグ（T-8 セルフチェック結果）。
    pub fn set_validity(&mut self, valid: bool) {
        self.data.validity = valid;
    }

    /// B-141: TRACE 側の PRE/POST time-origin 検証用に Record 開始 epoch ms を公開する。
    /// 既存 .kirin 互換の sentinel は 0。writer_start だけが初回 flush 前に実値へ更新する。
    pub fn set_started_at_ms(&mut self, started_at_ms: i64) {
        self.data.started_at_ms = started_at_ms;
    }

    /// PRE/POST 共通の Record session UUID を焼く。
    pub fn set_record_session_id(&mut self, session_id: Option<String>) {
        self.data.record_session_id = non_empty_string(session_id);
    }

    /// atomic flush: checksum 計算 → `.tmp` 書込 → `rename()` で最終パスに置換。
    ///
    /// rename は POSIX では同一 FS 内で atomic。途中クラッシュ時も最終ファイルは
    /// 旧状態 or 新状態のどちらかにしかならない（D-1 対策）。
    ///
    /// B-245: 書込前に親 dir 不在を検査し、消えていれば再作成する。
    /// storage の一時消失だけで Record を止めないため、復旧可能な欠落はここで吸収する。
    pub fn flush(&mut self) -> Result<(), WriterError> {
        self.write_atomic(self.paths.staging_path.clone())
    }

    fn write_atomic(&mut self, target_path: PathBuf) -> Result<(), WriterError> {
        self.write_atomic_with_tmp(target_path, self.paths.tmp_path.clone())
    }

    fn write_atomic_with_tmp(
        &mut self,
        target_path: PathBuf,
        tmp_path: PathBuf,
    ) -> Result<(), WriterError> {
        if let Some(parent) = target_path.parent() {
            if !parent.is_dir() {
                fs::create_dir_all(parent)?;
            }
        }
        self.data.checksum = compute_checksum(&self.data)?;
        let json = serde_json::to_string(&self.data)?;
        fs::write(&tmp_path, json.as_bytes())?;
        fs::rename(&tmp_path, target_path)?;
        Ok(())
    }

    /// status=closed に変更して最終 flush。正常終了時に呼ぶ。
    pub fn close(mut self) -> Result<(), WriterError> {
        self.data.status = Status::Closed;
        let integrity_reasons = self.commit_integrity_reasons();
        if !integrity_reasons.is_empty() {
            self.data.integrity_degraded = true;
            for reason in integrity_reasons {
                self.add_integrity_reason(reason);
            }
        }
        let reasons = self.commit_failure_reasons();
        if reasons.is_empty() {
            self.data.commit_status = Some("pair_pending".to_string());
            refresh_record_quality(&mut self.data);
            let self_paths = self_paths_for(&self.paths, &self.data);
            self.write_atomic_with_tmp(
                self_paths.pair_pending_path.clone(),
                sibling_tmp_path(&self_paths.pair_pending_path),
            )?;
            if let Err(e) = try_finalize_pair_session(&self.paths, &self.data) {
                log::warn!("[pair_record_session] finalize attempt failed: {}", e);
            }
        } else {
            self.data.commit_status = Some("failed".to_string());
            self.data.validity = false;
            self.data.integrity_degraded = true;
            for reason in reasons {
                self.add_integrity_reason(reason);
            }
            refresh_record_quality(&mut self.data);
            self.write_atomic(self.paths.failed_path.clone())?;
        }
        let _ = fs::remove_file(&self.paths.staging_path);
        Ok(())
    }

    fn commit_failure_reasons(&self) -> Vec<&'static str> {
        side_publish_failure_reasons(&self.data)
    }

    fn commit_integrity_reasons(&self) -> Vec<&'static str> {
        side_publish_integrity_reasons(&self.data)
    }
}

fn side_publish_failure_reasons(data: &PluginDataFile) -> Vec<&'static str> {
    let mut reasons = Vec::new();
    if !data.validity {
        reasons.push("validity_false");
    }
    if data.frames.is_empty() {
        reasons.push("zero_trace_frames");
    }
    if data.sample_rate == 0 {
        reasons.push("missing_sample_rate");
    }
    if data.record_session_id.as_deref().is_none_or(str::is_empty) {
        reasons.push("missing_record_session_id");
    }
    match data.role {
        Role::Pre
            if data
                .paired_post_instance_id
                .as_deref()
                .is_none_or(str::is_empty) =>
        {
            reasons.push("missing_paired_post_instance_id")
        }
        Role::Post
            if data
                .paired_pre_instance_id
                .as_deref()
                .is_none_or(str::is_empty) =>
        {
            reasons.push("missing_paired_pre_instance_id")
        }
        _ => {}
    }
    dedup_reasons(reasons)
}

fn side_publish_integrity_reasons(data: &PluginDataFile) -> Vec<&'static str> {
    let mut reasons = Vec::new();
    let expected = match &data.expected_wav {
        Some(expected) if expected.is_usable() => Some(expected),
        _ => {
            reasons.push("missing_expected_wav_metadata");
            None
        }
    };
    let take = match &data.bounce_take {
        Some(take) => Some(take),
        None => {
            reasons.push("missing_bounce_take");
            None
        }
    };
    if let (Some(expected), Some(take)) = (expected, take) {
        if data.sample_rate != expected.expected_sample_rate {
            reasons.push("sample_rate_expected_mismatch");
        }
        if take.sample_rate != expected.expected_sample_rate {
            reasons.push("bounce_take_sample_rate_mismatch");
        }
        if take.duration_samples != expected.expected_duration_samples {
            reasons.push("bounce_take_duration_mismatch");
        }
        if take.alignment_status != "sample_count_ready" {
            reasons.push("bounce_take_not_sample_count_ready");
        }
        if take.source != "expected_wav_duration_native" {
            reasons.push("bounce_take_source_not_expected_wav");
        }
    }
    if let Some(diag) = &data.trace_diagnostics {
        if diag.missing_slots > 0 {
            reasons.push("missing_trace_slots");
        }
        if diag.measured_frame_count != diag.expected_frame_count {
            reasons.push("trace_frame_count_mismatch");
        }
    } else {
        reasons.push("missing_trace_diagnostics");
    }
    if data.integrity_degraded {
        reasons.push("integrity_degraded");
    }
    for reason in &data.integrity_reasons {
        match reason.as_str() {
            "zero_trace_frames" => reasons.push("zero_trace_frames"),
            "frame_timeline_gap" => reasons.push("frame_timeline_gap"),
            "raw_trace_timeline_gap" => reasons.push("raw_trace_timeline_gap"),
            "sparse_trace_density" => reasons.push("sparse_trace_density"),
            "record_too_short" => reasons.push("record_too_short"),
            "record_clock_not_wav_bounded" => reasons.push("record_clock_not_wav_bounded"),
            "drain_seal_timeout" => reasons.push("drain_seal_timeout"),
            "missing_trace_slots" => reasons.push("missing_trace_slots"),
            _ => {}
        }
    }
    dedup_reasons(reasons)
}

pub(crate) fn refresh_record_quality(data: &mut PluginDataFile) {
    let expected_wav_ready = data
        .expected_wav
        .as_ref()
        .is_some_and(ExpectedWavMetadata::is_usable);
    let sample_count_ready = match (&data.expected_wav, &data.bounce_take) {
        (Some(expected), Some(take)) if expected.is_usable() => {
            take.alignment_status == "sample_count_ready"
                && take.source == "expected_wav_duration_native"
                && take.sample_rate == expected.expected_sample_rate
                && take.duration_samples == expected.expected_duration_samples
                && data.sample_rate == expected.expected_sample_rate
        }
        _ => false,
    };
    let (expected_frame_count, measured_frame_count, missing_trace_slots, trace_slots_complete) =
        match &data.trace_diagnostics {
            Some(diag) => (
                diag.expected_frame_count,
                diag.measured_frame_count,
                diag.missing_slots,
                diag.missing_slots == 0 && diag.measured_frame_count == diag.expected_frame_count,
            ),
            None => (0, data.frames.len() as u64, 0, false),
        };
    let usable = side_publish_failure_reasons(data).is_empty();
    let complete = usable && side_publish_integrity_reasons(data).is_empty();
    let status = if complete {
        "complete"
    } else if usable {
        "usable_fallback"
    } else {
        "failed"
    };
    data.record_quality = Some(RecordQuality {
        status: status.to_string(),
        complete,
        usable,
        expected_wav_ready,
        sample_count_ready,
        trace_slots_complete,
        expected_frame_count,
        measured_frame_count,
        missing_trace_slots,
    });
}

fn add_integrity_reasons(data: &mut PluginDataFile, reasons: &[&'static str]) {
    if reasons.is_empty() {
        return;
    }
    data.integrity_degraded = true;
    for reason in reasons {
        if !data
            .integrity_reasons
            .iter()
            .any(|existing| existing == *reason)
        {
            data.integrity_reasons.push((*reason).to_string());
        }
    }
}

fn dedup_reasons(mut reasons: Vec<&'static str>) -> Vec<&'static str> {
    let mut out = Vec::with_capacity(reasons.len());
    for reason in reasons.drain(..) {
        if !out.contains(&reason) {
            out.push(reason);
        }
    }
    out
}

fn try_finalize_pair_session(
    paths: &WriterPaths,
    data: &PluginDataFile,
) -> Result<(), WriterError> {
    let self_paths = self_paths_for(paths, data);
    let Some(peer_paths) = peer_paths_for(paths, data) else {
        return Ok(());
    };
    if !peer_paths.pair_pending_path.exists() {
        return Ok(());
    }
    let self_pending = self_paths.pair_pending_path.clone();
    let mut self_data = read_plugin_data_file(&self_pending)?;
    let mut peer_data = read_plugin_data_file(&peer_paths.pair_pending_path)?;
    let integrity_reasons = pair_publish_integrity_reasons(&self_data, &peer_data);
    add_integrity_reasons(&mut self_data, &integrity_reasons);
    add_integrity_reasons(&mut peer_data, &integrity_reasons);
    let reasons = pair_publish_failure_reasons(&self_data, &peer_data);
    if reasons.is_empty() {
        self_data.commit_status = Some("committed".to_string());
        peer_data.commit_status = Some("committed".to_string());
        refresh_record_quality(&mut self_data);
        refresh_record_quality(&mut peer_data);
        publish_pair_committed_files(
            paths,
            &self_paths,
            &mut self_data,
            &peer_paths,
            &mut peer_data,
        )?;
        mark_pair_expected_metadata_consumed(paths, &self_data);
        let _ = fs::remove_file(&self_pending);
        let _ = fs::remove_file(&peer_paths.pair_pending_path);
        let manifest = pair_commit_manifest_path(paths, &self_data)
            .map(|path| path.display().to_string())
            .unwrap_or_else(|_| "<unknown>".to_string());
        log::info!(
            "[pair_record_session] published session={} manifest={}",
            self_data.record_session_id.as_deref().unwrap_or(""),
            manifest
        );
    } else {
        fail_pair_pending(
            &self_paths.failed_path,
            &self_pending,
            &mut self_data,
            reasons.as_slice(),
        )?;
        fail_pair_pending(
            &peer_paths.failed_path,
            &peer_paths.pair_pending_path,
            &mut peer_data,
            reasons.as_slice(),
        )?;
        mark_pair_expected_metadata_consumed(paths, &self_data);
        log::warn!(
            "[pair_record_session] quarantined pair session={:?} reasons={:?}",
            self_data.record_session_id,
            reasons
        );
    }
    Ok(())
}

fn publish_pair_committed_files(
    paths: &WriterPaths,
    self_paths: &PairPaths,
    self_data: &mut PluginDataFile,
    peer_paths: &PairPaths,
    peer_data: &mut PluginDataFile,
) -> Result<(), WriterError> {
    let manifest_path = pair_commit_manifest_path(paths, self_data)?;
    let self_trace_path = pair_trace_shelf_path(self_paths, self_data)?;
    let peer_trace_path = pair_trace_shelf_path(peer_paths, peer_data)?;
    let _ = fs::remove_file(&self_paths.final_path);
    let _ = fs::remove_file(&peer_paths.final_path);
    let _ = fs::remove_file(&self_trace_path);
    let _ = fs::remove_file(&peer_trace_path);
    write_plugin_data_atomic(&self_paths.member_path, self_data)?;
    if let Err(e) = write_plugin_data_atomic(&peer_paths.member_path, peer_data) {
        let _ = fs::remove_file(&self_paths.member_path);
        return Err(e);
    }
    if let Err(e) = write_pair_commit_manifest_atomic(
        &manifest_path,
        self_paths,
        self_data,
        peer_paths,
        peer_data,
    ) {
        let _ = fs::remove_file(&self_paths.member_path);
        let _ = fs::remove_file(&peer_paths.member_path);
        return Err(e);
    }
    let _ = fs::remove_file(&self_paths.final_path);
    let _ = fs::remove_file(&peer_paths.final_path);
    write_plugin_data_atomic(&self_trace_path, self_data)?;
    if let Err(e) = write_plugin_data_atomic(&peer_trace_path, peer_data) {
        let _ = fs::remove_file(&self_trace_path);
        return Err(e);
    }
    Ok(())
}

fn pair_trace_shelf_path(paths: &PairPaths, data: &PluginDataFile) -> Result<PathBuf, WriterError> {
    pair_trace_shelf_path_from_member_path(&paths.member_path, data)
}

fn pair_trace_shelf_path_from_member_path(
    member_path: &Path,
    data: &PluginDataFile,
) -> Result<PathBuf, WriterError> {
    let role_dir = member_path
        .parent()
        .and_then(Path::parent)
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "role dir missing"))?;
    let stamp = compact_wall_clock(&data.timestamp);
    if stamp.is_empty() {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "timestamp missing").into());
    }
    Ok(role_dir.join(format!("{stamp}.json")))
}

#[derive(Debug, Clone)]
struct PairPaths {
    final_path: PathBuf,
    member_path: PathBuf,
    pair_pending_path: PathBuf,
    failed_path: PathBuf,
}

#[derive(Debug, Serialize, Deserialize)]
struct PairCommitManifest {
    schema_version: String,
    session_id: String,
    project_hash: String,
    committed_at: String,
    pre: PairCommitSide,
    post: PairCommitSide,
}

#[derive(Debug, Serialize, Deserialize)]
struct PairCommitSide {
    instance_id: String,
    role: String,
    path: String,
    checksum: String,
}

fn self_paths_for(paths: &WriterPaths, data: &PluginDataFile) -> PairPaths {
    let Some(file_name) = pair_session_file_name(data) else {
        return PairPaths {
            final_path: paths.final_path.clone(),
            member_path: paths.final_path.clone(),
            pair_pending_path: paths.pair_pending_path.clone(),
            failed_path: paths.failed_path.clone(),
        };
    };
    let role_dir = paths.final_path.parent().unwrap_or_else(|| Path::new(""));
    PairPaths {
        final_path: role_dir.join(&file_name),
        member_path: role_dir.join(PAIR_RECORD_MEMBERS_DIR).join(&file_name),
        pair_pending_path: role_dir.join(".pair_pending").join(&file_name),
        failed_path: role_dir.join(".failed").join(&file_name),
    }
}

fn peer_paths_for(paths: &WriterPaths, data: &PluginDataFile) -> Option<PairPaths> {
    let root = plugin_data_root_from_final_path(&paths.final_path)?;
    let file_name = pair_session_file_name(data)?;
    let (peer_role, peer_iid) = match data.role {
        Role::Pre => (Role::Post, data.paired_post_instance_id.as_deref()?),
        Role::Post => (Role::Pre, data.paired_pre_instance_id.as_deref()?),
    };
    let ph = crate::path_identity::guard_path_component(
        &data.project_hash,
        "pair_record_session.peer.project_hash",
    );
    let peer_iid = crate::path_identity::guard_path_component(
        peer_iid,
        "pair_record_session.peer.instance_id",
    );
    let dir = root.join(&*ph).join(&*peer_iid).join(peer_role.dir_name());
    Some(PairPaths {
        final_path: dir.join(&file_name),
        member_path: dir.join(PAIR_RECORD_MEMBERS_DIR).join(&file_name),
        pair_pending_path: dir.join(".pair_pending").join(&file_name),
        failed_path: dir.join(".failed").join(&file_name),
    })
}

fn pair_commit_manifest_path(
    paths: &WriterPaths,
    data: &PluginDataFile,
) -> Result<PathBuf, WriterError> {
    let root = plugin_data_root_from_final_path(&paths.final_path).ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidData,
            "plugin_data root not resolvable",
        )
    })?;
    let file_name = pair_session_file_name(data)
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "record_session_id missing"))?;
    let ph = crate::path_identity::guard_path_component(
        &data.project_hash,
        "pair_record_session.manifest.project_hash",
    );
    Ok(root
        .join(&*ph)
        .join(PAIR_RECORD_SESSIONS_DIR)
        .join(file_name))
}

fn pair_session_file_name(data: &PluginDataFile) -> Option<String> {
    let session_id = data.record_session_id.as_deref()?.trim();
    if session_id.is_empty() {
        return None;
    }
    let safe = crate::path_identity::guard_path_component(
        session_id,
        "pair_record_session.record_session_id",
    );
    Some(format!("{safe}.json"))
}

/// Startup/recovery 時に残った PairRecordSession pending を再試行する。
///
/// peer 未到着の session は保持し、peer が揃った session だけ committed/failed の
/// どちらかへ収束させる。通常棚へ片側だけ publish しないための durable finalizer。
pub fn finalize_pair_pending_sessions(plugin_data_root: &Path) -> usize {
    let mut finalized = 0_usize;
    if !plugin_data_root.is_dir() {
        return finalized;
    }
    walk_pair_pending(plugin_data_root, &mut finalized);
    finalized
}

/// PairRecordSession manifest/hidden member を真実源として、Kirin OS が読む通常 TRACE shelf
/// (`{project}/{instance}/{pre|post}/{wall_clock}.json`) を収束させる。
///
/// 旧版が `.pair_committed` + `record_sessions` だけを残した場合、または publish 中の crash で
/// 通常 shelf が片側/両側欠けた場合でも、次回起動時に通常 TRACE へ戻すための自己修復器。
pub fn reconcile_pair_committed_trace_shelves(plugin_data_root: &Path) -> usize {
    let mut reconciled = 0_usize;
    if !plugin_data_root.is_dir() {
        return reconciled;
    }
    walk_pair_commit_manifests(plugin_data_root, plugin_data_root, &mut reconciled);
    reconciled
}

fn walk_pair_commit_manifests(base_dir: &Path, dir: &Path, reconciled: &mut usize) {
    let entries = match fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let ftype = match entry.file_type() {
            Ok(ftype) => ftype,
            Err(_) => continue,
        };
        if ftype.is_dir() {
            walk_pair_commit_manifests(base_dir, &path, reconciled);
            continue;
        }
        if !ftype.is_file() || path.extension().is_none_or(|ext| ext != "json") {
            continue;
        }
        if path
            .parent()
            .and_then(Path::file_name)
            .and_then(|name| name.to_str())
            != Some(PAIR_RECORD_SESSIONS_DIR)
        {
            continue;
        }
        let bytes = match fs::read(&path) {
            Ok(bytes) => bytes,
            Err(e) => {
                log::warn!(
                    "[pair_record_session] manifest read failed during reconcile: {} ({})",
                    path.display(),
                    e
                );
                continue;
            }
        };
        let manifest: PairCommitManifest = match serde_json::from_slice(&bytes) {
            Ok(manifest) => manifest,
            Err(e) => {
                log::warn!(
                    "[pair_record_session] manifest parse failed during reconcile: {} ({})",
                    path.display(),
                    e
                );
                continue;
            }
        };
        if manifest.schema_version != PAIR_RECORD_SESSION_SCHEMA {
            continue;
        }
        for side in [&manifest.pre, &manifest.post] {
            match reconcile_pair_commit_side(base_dir, side) {
                Ok(true) => *reconciled = reconciled.saturating_add(1),
                Ok(false) => {}
                Err(e) => log::warn!(
                    "[pair_record_session] TRACE shelf reconcile failed: manifest={} side={} err={}",
                    path.display(),
                    side.role,
                    e
                ),
            }
        }
    }
}

fn reconcile_pair_commit_side(base_dir: &Path, side: &PairCommitSide) -> Result<bool, WriterError> {
    let member_path = PathBuf::from(&side.path);
    if !member_path.starts_with(base_dir) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "pair commit member path escapes plugin_data base",
        )
        .into());
    }
    let mut data = read_plugin_data_file(&member_path)?;
    if !verify_checksum(&data) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "pair commit member checksum mismatch",
        )
        .into());
    }
    let trace_path = pair_trace_shelf_path_from_member_path(&member_path, &data)?;
    if trace_path.exists() {
        if let Ok(existing) = read_plugin_data_file(&trace_path) {
            if verify_checksum(&existing) && existing.checksum == data.checksum {
                return Ok(false);
            }
        }
    }
    write_plugin_data_atomic(&trace_path, &mut data)?;
    Ok(true)
}

fn walk_pair_pending(dir: &Path, finalized: &mut usize) {
    let entries = match fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let ftype = match entry.file_type() {
            Ok(ftype) => ftype,
            Err(_) => continue,
        };
        if ftype.is_dir() {
            walk_pair_pending(&path, finalized);
            continue;
        }
        if !ftype.is_file() || path.extension().is_none_or(|ext| ext != "json") {
            continue;
        }
        if path
            .parent()
            .and_then(Path::file_name)
            .and_then(|n| n.to_str())
            != Some(".pair_pending")
        {
            continue;
        }
        let Some(paths) = writer_paths_from_pair_pending_path(&path) else {
            continue;
        };
        let data = match read_plugin_data_file(&path) {
            Ok(data) => data,
            Err(e) => {
                log::warn!(
                    "[pair_record_session] pending read failed during sweep: {} ({})",
                    path.display(),
                    e
                );
                continue;
            }
        };
        if let Err(e) = try_finalize_pair_session(&paths, &data) {
            log::warn!(
                "[pair_record_session] pending finalize failed during sweep: {} ({})",
                path.display(),
                e
            );
            continue;
        }
        if !path.exists() {
            *finalized = finalized.saturating_add(1);
        }
    }
}

fn writer_paths_from_pair_pending_path(pair_pending_path: &Path) -> Option<WriterPaths> {
    let file_name = pair_pending_path.file_name()?.to_owned();
    let role_dir = pair_pending_path.parent()?.parent()?;
    Some(WriterPaths {
        final_path: role_dir.join(&file_name),
        tmp_path: role_dir.join(format!("{}.tmp", file_name.to_string_lossy())),
        staging_path: role_dir.join(format!("{}.partial", file_name.to_string_lossy())),
        pair_pending_path: pair_pending_path.to_path_buf(),
        failed_path: role_dir.join(".failed").join(&file_name),
    })
}

fn sibling_tmp_path(path: &Path) -> PathBuf {
    let file_name = path
        .file_name()
        .map(|name| name.to_string_lossy())
        .unwrap_or_default();
    path.with_file_name(format!("{file_name}.tmp"))
}

fn plugin_data_root_from_final_path(final_path: &Path) -> Option<PathBuf> {
    final_path
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .and_then(Path::parent)
        .map(Path::to_path_buf)
}

fn read_plugin_data_file(path: &Path) -> Result<PluginDataFile, WriterError> {
    let bytes = fs::read(path)?;
    let data: PluginDataFile = serde_json::from_slice(&bytes)?;
    Ok(data)
}

fn write_plugin_data_atomic(path: &Path, data: &mut PluginDataFile) -> Result<(), WriterError> {
    if data.commit_status.is_some() {
        refresh_record_quality(data);
    }
    data.checksum = compute_checksum(data)?;
    let json = serde_json::to_vec(data)?;
    crate::atomic_file::write_bytes_atomic(path, &json)?;
    Ok(())
}

fn write_pair_commit_manifest_atomic(
    manifest_path: &Path,
    self_paths: &PairPaths,
    self_data: &PluginDataFile,
    peer_paths: &PairPaths,
    peer_data: &PluginDataFile,
) -> Result<(), WriterError> {
    let (pre_paths, pre_data, post_paths, post_data) = match (self_data.role, peer_data.role) {
        (Role::Pre, Role::Post) => (self_paths, self_data, peer_paths, peer_data),
        (Role::Post, Role::Pre) => (peer_paths, peer_data, self_paths, self_data),
        _ => {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "pair roles invalid").into());
        }
    };
    let session_id = self_data
        .record_session_id
        .as_deref()
        .unwrap_or_default()
        .to_string();
    let manifest = PairCommitManifest {
        schema_version: PAIR_RECORD_SESSION_SCHEMA.to_string(),
        session_id,
        project_hash: self_data.project_hash.clone(),
        committed_at: chrono::Utc::now()
            .format("%Y-%m-%dT%H:%M:%S%.3fZ")
            .to_string(),
        pre: pair_commit_side(pre_paths, pre_data, "PRE"),
        post: pair_commit_side(post_paths, post_data, "POST"),
    };
    let json = serde_json::to_vec(&manifest)?;
    crate::atomic_file::write_bytes_atomic(manifest_path, &json)?;
    Ok(())
}

fn pair_commit_side(
    paths: &PairPaths,
    data: &PluginDataFile,
    role: &'static str,
) -> PairCommitSide {
    PairCommitSide {
        instance_id: data.instance_id.clone(),
        role: role.to_string(),
        path: paths.member_path.to_string_lossy().to_string(),
        checksum: data.checksum.clone(),
    }
}

fn mark_pair_expected_metadata_consumed(paths: &WriterPaths, data: &PluginDataFile) {
    let Some(root) = plugin_data_root_from_final_path(&paths.final_path) else {
        return;
    };
    let Some(expected) = data.expected_wav.as_ref() else {
        return;
    };
    let Some(session_id) = data.record_session_id.as_deref() else {
        return;
    };
    if let Err(e) = crate::record_expected::mark_expected_metadata_consumed(
        &root,
        &data.project_hash,
        &expected.bounce_id,
        session_id,
    ) {
        log::warn!(
            "[pair_record_session] expected metadata consume marker failed: project={} bounce={} err={}",
            data.project_hash,
            expected.bounce_id,
            e
        );
    }
}

fn fail_pair_pending(
    failed_path: &Path,
    pending_path: &Path,
    data: &mut PluginDataFile,
    reasons: &[&'static str],
) -> Result<(), WriterError> {
    data.commit_status = Some("failed".to_string());
    data.validity = false;
    data.integrity_degraded = true;
    for reason in reasons {
        if !data
            .integrity_reasons
            .iter()
            .any(|existing| existing == *reason)
        {
            data.integrity_reasons.push((*reason).to_string());
        }
    }
    refresh_record_quality(data);
    write_plugin_data_atomic(failed_path, data)?;
    let _ = fs::remove_file(pending_path);
    Ok(())
}

fn pair_publish_failure_reasons(
    left: &PluginDataFile,
    right: &PluginDataFile,
) -> Vec<&'static str> {
    let mut reasons = side_publish_failure_reasons(left);
    reasons.extend(side_publish_failure_reasons(right));
    let (pre, post) = match (left.role, right.role) {
        (Role::Pre, Role::Post) => (left, right),
        (Role::Post, Role::Pre) => (right, left),
        _ => {
            reasons.push("pair_roles_not_pre_post");
            return dedup_reasons(reasons);
        }
    };
    if pre.project_hash != post.project_hash {
        reasons.push("pair_project_hash_mismatch");
    }
    if pre.record_session_id != post.record_session_id {
        reasons.push("pair_record_session_id_mismatch");
    }
    if pre.paired_post_instance_id.as_deref() != Some(post.instance_id.as_str()) {
        reasons.push("pair_pre_post_link_mismatch");
    }
    if post.paired_pre_instance_id.as_deref() != Some(pre.instance_id.as_str()) {
        reasons.push("pair_post_pre_link_mismatch");
    }
    dedup_reasons(reasons)
}

fn pair_publish_integrity_reasons(
    left: &PluginDataFile,
    right: &PluginDataFile,
) -> Vec<&'static str> {
    let mut reasons = side_publish_integrity_reasons(left);
    reasons.extend(side_publish_integrity_reasons(right));
    let (pre, post) = match (left.role, right.role) {
        (Role::Pre, Role::Post) => (left, right),
        (Role::Post, Role::Pre) => (right, left),
        _ => return dedup_reasons(reasons),
    };
    if pre.expected_wav != post.expected_wav {
        reasons.push("pair_expected_wav_mismatch");
    }
    if let (Some(pre_take), Some(post_take)) = (&pre.bounce_take, &post.bounce_take) {
        if pre_take.duration_samples != post_take.duration_samples {
            reasons.push("pair_bounce_take_duration_mismatch");
        }
        if pre_take.duration_frames_48k != post_take.duration_frames_48k {
            reasons.push("pair_bounce_take_frame_count_mismatch");
        }
    }
    if let (Some(pre_diag), Some(post_diag)) = (&pre.trace_diagnostics, &post.trace_diagnostics) {
        if pre_diag.expected_frame_count != post_diag.expected_frame_count {
            reasons.push("pair_trace_expected_frame_count_mismatch");
        }
        if pre_diag.measured_frame_count != post_diag.measured_frame_count {
            reasons.push("pair_trace_measured_frame_count_mismatch");
        }
    }
    dedup_reasons(reasons)
}

// ── Annotation 追記（サブ2-C / Note ボタン）──────────────────────────────────

/// `plugin_data/{project_hash}/{instance_id}/{role_dir}/` 配下の最新 `*.json` に
/// 1 件 annotation を追記して atomic rename で書き戻す。checksum は再計算される。
///
/// # 戻り値
/// - `Ok(true)`: 対象ファイルが見つかり追記成功
/// - `Ok(false)`: 対象ディレクトリが空 or `*.json` 不在（Record 開始前など）。
///   呼出側はスタブ動作として扱う（toast は表示するが実ファイル更新なし）。
/// - `Err(WriterError)`: 読み込み / JSON parse / 書込エラー
///
/// 「最新」は同一ディレクトリ内の filename 辞書順最大を使う（`{compact}.json` が
/// `YYYYMMDDTHHMMSS.json` 形式なので辞書順 = 時系列順）。mtime ではなく filename を
/// 使うことで再現性・テスタビリティを確保。
///
/// A-3 修正後: 旧 `bus` パスセグメントは `instance_id` に置換された。
pub fn append_annotation_to_latest(
    base_dir: &Path,
    project_hash: &str,
    instance_id: &str,
    role: Role,
    memo: String,
) -> Result<bool, WriterError> {
    // B-128 (G-115-370): within-base wall（annotation も path builder の一つ）。
    let ph =
        crate::path_identity::guard_path_component(project_hash, "append_annotation.project_hash");
    let iid =
        crate::path_identity::guard_path_component(instance_id, "append_annotation.instance_id");
    let dir = base_dir.join(&*ph).join(&*iid).join(role.dir_name());
    let latest = match find_latest_json(&dir) {
        Some(path) => Some(path),
        None => latest_pair_member_for_annotation(base_dir, &ph, instance_id, role)?,
    };
    let Some(latest) = latest else {
        return Ok(false);
    };
    let annotation = Annotation {
        t: now_iso8601(),
        memo,
    };
    append_annotation_to_file_and_mirror(&latest, annotation)?;
    Ok(true)
}

fn append_annotation_to_file_and_mirror(
    path: &Path,
    annotation: Annotation,
) -> Result<(), WriterError> {
    let mirror = pair_annotation_mirror_path(path)?;
    let source_is_pair_member = is_pair_member_path(path);
    append_annotation_to_file(path, annotation.clone())?;
    if let Some(mirror) = mirror.filter(|mirror| mirror != path) {
        if mirror.exists() {
            append_annotation_to_file(&mirror, annotation)?;
        } else if source_is_pair_member {
            let mut data = read_plugin_data_file(path)?;
            write_plugin_data_atomic(&mirror, &mut data)?;
        }
    }
    Ok(())
}

fn append_annotation_to_file(path: &Path, annotation: Annotation) -> Result<(), WriterError> {
    let bytes = fs::read(path)?;
    let mut data: PluginDataFile = serde_json::from_slice(&bytes)?;
    data.annotations.push(annotation);
    write_plugin_data_atomic(path, &mut data)?;
    Ok(())
}

fn pair_annotation_mirror_path(path: &Path) -> Result<Option<PathBuf>, WriterError> {
    let bytes = fs::read(path)?;
    let data: PluginDataFile = serde_json::from_slice(&bytes)?;
    if data.commit_status.as_deref() != Some("committed") {
        return Ok(None);
    }
    let Some(session_id) = data.record_session_id.as_deref().map(str::trim) else {
        return Ok(None);
    };
    if session_id.is_empty() {
        return Ok(None);
    }
    let Some(parent) = path.parent() else {
        return Ok(None);
    };
    let parent_name = parent.file_name().and_then(|name| name.to_str());
    if parent_name == Some(PAIR_RECORD_MEMBERS_DIR) {
        return Ok(Some(pair_trace_shelf_path_from_member_path(path, &data)?));
    }
    let safe = crate::path_identity::guard_path_component(
        session_id,
        "append_annotation.record_session_id",
    );
    Ok(Some(
        parent
            .join(PAIR_RECORD_MEMBERS_DIR)
            .join(format!("{safe}.json")),
    ))
}

fn is_pair_member_path(path: &Path) -> bool {
    path.parent()
        .and_then(|parent| parent.file_name())
        .and_then(|name| name.to_str())
        == Some(PAIR_RECORD_MEMBERS_DIR)
}

fn latest_pair_member_for_annotation(
    base_dir: &Path,
    guarded_project_hash: &str,
    instance_id: &str,
    role: Role,
) -> Result<Option<PathBuf>, WriterError> {
    let dir = base_dir
        .join(guarded_project_hash)
        .join(PAIR_RECORD_SESSIONS_DIR);
    let entries = match fs::read_dir(&dir) {
        Ok(entries) => entries,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(e.into()),
    };
    let mut best: Option<(String, PathBuf)> = None;
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().is_none_or(|ext| ext != "json") {
            continue;
        }
        let bytes = fs::read(&path)?;
        let manifest: PairCommitManifest = serde_json::from_slice(&bytes)?;
        let manifest_project_hash = crate::path_identity::guard_path_component(
            &manifest.project_hash,
            "append_annotation.manifest_project_hash",
        );
        if manifest_project_hash.as_ref() != guarded_project_hash
            || manifest.schema_version != PAIR_RECORD_SESSION_SCHEMA
        {
            continue;
        }
        let side = match role {
            Role::Pre => &manifest.pre,
            Role::Post => &manifest.post,
        };
        if side.role != role.as_str() || side.instance_id != instance_id {
            continue;
        }
        let member_path = PathBuf::from(&side.path);
        if !member_path.starts_with(base_dir) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "pair commit member path escapes plugin_data base",
            )
            .into());
        }
        let key = format!(
            "{}:{}",
            manifest.committed_at,
            path.file_name()
                .and_then(|n| n.to_str())
                .unwrap_or_default()
        );
        if best.as_ref().is_none_or(|(best_key, _)| key > *best_key) {
            best = Some((key, member_path));
        }
    }
    Ok(best.map(|(_, path)| path))
}

/// `dir` 直下の `*.json` のうち filename 辞書順最大を返す。
/// 不在・読込失敗は `None`。
fn find_latest_json(dir: &Path) -> Option<PathBuf> {
    let entries = fs::read_dir(dir).ok()?;
    entries
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|e| e == "json"))
        .max()
}

// ── ヘルパ ────────────────────────────────────────────────────────────────────

/// ISO 8601（秒精度・UTC）。
fn now_iso8601() -> String {
    chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
}

/// ISO 8601 (`2026-04-17T14:32:08Z`) → local compact (`YYYYMMDDTHHMMSS`).
///
/// plugin_data の session_id は G-50-30 により local wall clock。ISO/RFC3339
/// 入力はローカルタイムゾーンへ変換してから compact 化する。書式が想定と違えば
/// 旧実装と同じ「非数字・非 T を除去」の安全側に倒す。
pub fn compact_wall_clock(iso: &str) -> String {
    if let Ok(dt) = chrono::DateTime::parse_from_rfc3339(iso) {
        return dt
            .with_timezone(&chrono::Local)
            .format("%Y%m%dT%H%M%S")
            .to_string();
    }

    iso.chars()
        .take_while(|c| c.is_ascii_digit() || matches!(*c, '-' | ':' | 'T'))
        .filter(|c| c.is_ascii_digit() || *c == 'T')
        .collect()
}

/// 1 桁丸め。
fn round1(v: f64) -> f64 {
    if v.is_finite() {
        (v * 10.0).round() / 10.0
    } else {
        v
    }
}

/// 2 桁丸め。
fn round2(v: f64) -> f64 {
    if v.is_finite() {
        (v * 100.0).round() / 100.0
    } else {
        v
    }
}

/// `checksum=""` にした状態の JSON バイト列に対して HMAC-SHA256 を計算。
///
/// B-026 / Gap-9: 同 crate 内の `record_writer::sweep_stale_active_at_startup`
/// が startup sweep 時に再計算で利用する。HMAC 鍵 (`hmac_key`) は plugin_data
/// 内で閉じたままにしたいため `pub` ではなく `pub(crate)`。
pub(crate) fn compute_checksum(data: &PluginDataFile) -> Result<String, serde_json::Error> {
    let mut clone = data.clone();
    clone.checksum = String::new();
    let bytes = serde_json::to_vec(&clone)?;
    let mut mac =
        HmacSha256::new_from_slice(hmac_key()).expect("HMAC-SHA256 accepts any key length");
    mac.update(&bytes);
    Ok(hex::encode(mac.finalize().into_bytes()))
}

/// HMAC 鍵（identity.rs と同じ方針。ビルド時埋め込み）。
fn hmac_key() -> &'static [u8] {
    match option_env!("KIRIN_HYPHA_HMAC_KEY") {
        Some(k) => k.as_bytes(),
        None => DEFAULT_HMAC_KEY,
    }
}

const DEFAULT_HMAC_KEY: &[u8] = b"kirin-hypha-phase1.0-hmac-key-deterrent-level-20260417";

/// 検証用: 読み込んだ `PluginDataFile` の `checksum` が整合するか。
pub fn verify_checksum(data: &PluginDataFile) -> bool {
    match compute_checksum(data) {
        Ok(expected) => constant_time_eq(expected.as_bytes(), data.checksum.as_bytes()),
        Err(_) => false,
    }
}

/// 定数時間比較。
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

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    /// 各テスト専用ディレクトリ（並列実行でも衝突しない）。
    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_plugin_data_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// Test の固定 instance_id（path セグメント。新構造に追従）。
    const TEST_INSTANCE_ID: &str = "test-instance-aaaa";

    fn sample_writer(base: &Path, role: Role) -> PluginDataWriter {
        let paths = WriterPaths::build(
            base,
            "project_hash_test",
            TEST_INSTANCE_ID,
            role,
            "2026-04-17T14:32:08Z",
        );
        PluginDataWriter::create(
            paths,
            "11111111-2222-4333-8444-555555555555".to_string(),
            "project_hash_test".to_string(),
            TEST_INSTANCE_ID.to_string(),
            role,
            None,
            48000,
            (role == Role::Post).then(|| "paired-pre-test".to_string()),
            (role == Role::Pre).then(|| "paired-post-test".to_string()),
            None,
            None,
        )
        .unwrap()
    }

    fn expected_wav_fixture() -> ExpectedWavMetadata {
        let now_ms = 1_800_000_000_000;
        ExpectedWavMetadata {
            expected_duration_samples: 48_000,
            expected_sample_rate: 48_000,
            wav_path: "/tmp/kirin-plugin-data-test.wav".to_string(),
            bounce_id: "bounce-plugin-data-test".to_string(),
            created_at_ms: now_ms,
            wav_file_size: Some(192_044),
            wav_mtime_ms: now_ms,
            wav_hash: Some("hash-plugin-data-test".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    fn complete_pair_writer(
        base: &Path,
        role: Role,
        instance_id: &str,
        paired_pre_instance_id: Option<String>,
        paired_post_instance_id: Option<String>,
    ) -> PluginDataWriter {
        let paths = WriterPaths::build(
            base,
            "project_hash_test",
            instance_id,
            role,
            "2026-04-17T14:32:08.000Z",
        );
        let mut w = PluginDataWriter::create(
            paths,
            "11111111-2222-4333-8444-555555555555".to_string(),
            "project_hash_test".to_string(),
            instance_id.to_string(),
            role,
            None,
            48_000,
            paired_pre_instance_id,
            paired_post_instance_id,
            None,
            None,
        )
        .unwrap();
        w.set_record_session_id(Some("session-pair-atomic".to_string()));
        w.set_expected_wav(Some(expected_wav_fixture()));
        w.set_bounce_take(BounceTake {
            source: "expected_wav_duration_native".to_string(),
            time_axis: "native_samples".to_string(),
            alignment_status: "sample_count_ready".to_string(),
            sample_rate: 48_000,
            wav_start_sample: 0,
            wav_end_sample: 48_000,
            duration_samples: 48_000,
            duration_frames_48k: 48_000,
            start_t_ms: 0,
            end_t_ms: 1_000,
            trace_sample_count: 2,
            frame_count: 2,
        });
        w.set_trace_diagnostics(TraceDiagnostics {
            raw_trace_count: 2,
            expected_frame_count: 2,
            measured_frame_count: 2,
            missing_slots: 0,
            explicit_silence_frame_count: 0,
        });
        w.append_frame(0, [0.0; 20], 0.0, -20.0, -1.0, 12.0, Some(10.0));
        w.append_frame(1_000, [0.0; 20], 0.0, -20.0, -1.0, 12.0, Some(10.0));
        w
    }

    fn write_final_for_annotation_test(mut w: PluginDataWriter) -> PathBuf {
        let path = w.paths.final_path.clone();
        w.data.status = Status::Closed;
        w.data.commit_status = Some("committed".to_string());
        w.write_atomic(path.clone()).unwrap();
        path
    }

    #[test]
    fn role_string_conversion() {
        assert_eq!(Role::Pre.as_str(), "PRE");
        assert_eq!(Role::Post.as_str(), "POST");
        assert_eq!(Role::Pre.dir_name(), "pre");
        assert_eq!(Role::Post.dir_name(), "post");
    }

    fn local_compact(input: &str) -> String {
        chrono::DateTime::parse_from_rfc3339(input)
            .unwrap()
            .with_timezone(&chrono::Local)
            .format("%Y%m%dT%H%M%S")
            .to_string()
    }

    #[test]
    fn compact_wall_clock_uses_local_wall_clock_for_rfc3339() {
        assert_eq!(
            compact_wall_clock("2026-04-17T14:32:08Z"),
            local_compact("2026-04-17T14:32:08Z")
        );
        assert_eq!(
            compact_wall_clock("2026-04-17T14:32:08.123Z"),
            local_compact("2026-04-17T14:32:08.123Z")
        );
        assert_eq!(
            compact_wall_clock("2026-04-17T14:32:08+09:00"),
            local_compact("2026-04-17T14:32:08+09:00")
        );
    }

    #[test]
    fn compact_wall_clock_falls_back_to_separator_strip_for_non_rfc3339() {
        assert_eq!(compact_wall_clock("20260417T143208"), "20260417T143208");
    }

    #[test]
    fn writer_paths_build_hierarchy() {
        let base = Path::new("/tmp/kirin_base");
        let p = WriterPaths::build(base, "ph", "iid-1", Role::Pre, "2026-04-17T14:32:08Z");
        let compact = local_compact("2026-04-17T14:32:08Z");
        assert_eq!(
            p.final_path,
            Path::new(&format!("/tmp/kirin_base/ph/iid-1/pre/{compact}.json"))
        );
        assert_eq!(
            p.tmp_path,
            Path::new(&format!("/tmp/kirin_base/ph/iid-1/pre/{compact}.json.tmp"))
        );
        assert_eq!(
            p.staging_path,
            Path::new(&format!(
                "/tmp/kirin_base/ph/iid-1/pre/{compact}.json.partial"
            ))
        );
        assert_eq!(
            p.pair_pending_path,
            Path::new(&format!(
                "/tmp/kirin_base/ph/iid-1/pre/.pair_pending/{compact}.json"
            ))
        );
        assert_eq!(
            p.failed_path,
            Path::new(&format!(
                "/tmp/kirin_base/ph/iid-1/pre/.failed/{compact}.json"
            ))
        );
    }

    #[test]
    fn new_file_has_schema_1_3_defaults_with_optional_bus() {
        // Phase 1: bus = None → JSON では field omitted（skip_serializing_if）
        let f = PluginDataFile::new(
            "iid".to_string(),
            "ph".to_string(),
            TEST_INSTANCE_ID.to_string(),
            Role::Post,
            None,
            48000,
            None,
            None,
            None,
            None,
        );
        assert_eq!(f.schema_version, "1.3");
        assert_eq!(f.role, Role::Post);
        assert_eq!(f.instance_id, TEST_INSTANCE_ID);
        assert!(f.bus.is_none(), "Phase 1: bus must be None");
        assert_eq!(f.mode, "record");
        assert_eq!(f.status, Status::Active);
        assert!(f.frames.is_empty());
        assert_eq!(f.psb_snapshots.as_ref().map(|v| v.len()), Some(0));
        assert!(f.annotations.is_empty());
        assert!(f.paired_pre_instance_id.is_none());
        assert!(f.paired_post_instance_id.is_none());
        assert!(f.pair_name.is_none());
        assert!(f.pair_pre_name.is_none());
        assert!(f.record_session_id.is_none());
        assert_eq!(f.started_at_ms, 0);
        assert_eq!(f.sample_rate, 48000);
        assert_eq!(f.source_format, 48000);
        assert!(f.bounce_take.is_none());
        assert!(f.validity);
        assert!(f.checksum.is_empty());

        // JSON 出力に "bus" field が含まれない（skip_serializing_if）
        let json = serde_json::to_string(&f).unwrap();
        assert!(
            !json.contains("\"bus\""),
            "bus must be omitted when None: {json}"
        );
        // paired_*_instance_id も None の時は出ない（skip_serializing_if）
        assert!(
            !json.contains("paired_pre_instance_id"),
            "paired_pre_instance_id omitted when None: {json}"
        );
        assert!(
            !json.contains("paired_post_instance_id"),
            "paired_post_instance_id omitted when None: {json}"
        );
        assert!(
            !json.contains("pair_name"),
            "pair_name omitted when None: {json}"
        );
        assert!(
            !json.contains("pair_pre_name"),
            "pair_pre_name omitted when None: {json}"
        );
        assert!(
            !json.contains("record_session_id"),
            "record_session_id omitted when None: {json}"
        );
        assert!(
            !json.contains("bounce_take"),
            "bounce_take omitted when None: {json}"
        );
    }

    #[test]
    fn new_file_with_some_bus_includes_field() {
        // 将来 bus メタデータが復活した場合: Some(name) を渡せば JSON に出る
        let f = PluginDataFile::new(
            "iid".to_string(),
            "ph".to_string(),
            TEST_INSTANCE_ID.to_string(),
            Role::Post,
            Some("DRUM".to_string()),
            48000,
            None,
            None,
            None,
            None,
        );
        let json = serde_json::to_string(&f).unwrap();
        assert!(
            json.contains("\"bus\":\"DRUM\""),
            "bus included when Some: {json}"
        );
    }

    #[test]
    fn new_file_with_pair_names_includes_lens_labels() {
        let f = PluginDataFile::new(
            "iid".to_string(),
            "ph".to_string(),
            TEST_INSTANCE_ID.to_string(),
            Role::Post,
            None,
            48000,
            Some("pre-iid".to_string()),
            None,
            Some(" Mix ".to_string()),
            Some(" Mix ".to_string()),
        );
        assert_eq!(f.pair_name.as_deref(), Some("Mix"));
        assert_eq!(f.pair_pre_name.as_deref(), Some("Mix"));

        let json = serde_json::to_string(&f).unwrap();
        assert!(json.contains("\"pair_name\":\"Mix\""));
        assert!(json.contains("\"pair_pre_name\":\"Mix\""));
    }

    #[test]
    fn new_file_omits_blank_pair_names() {
        let f = PluginDataFile::new(
            "iid".to_string(),
            "ph".to_string(),
            TEST_INSTANCE_ID.to_string(),
            Role::Post,
            None,
            48000,
            Some("pre-iid".to_string()),
            None,
            Some("   ".to_string()),
            Some(String::new()),
        );
        let json = serde_json::to_string(&f).unwrap();
        assert!(f.pair_name.is_none());
        assert!(f.pair_pre_name.is_none());
        assert!(!json.contains("pair_name"));
        assert!(!json.contains("pair_pre_name"));
    }

    #[test]
    fn append_frame_applies_rounding() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        let n = [1.2345; 20];
        w.append_frame(123, n, 1.2345, -14.2345, -1.1234, 12.3456, None);
        let frame = &w.data().frames[0];
        assert_eq!(frame.t_ms, 123);
        assert_eq!(frame.n_prime.unwrap()[0], 1.2); // round1
        assert_eq!(frame.sharpness.unwrap(), 1.23); // round2
        assert_eq!(frame.lufs_m, -14.2);
        assert_eq!(frame.true_peak, -1.1);
        assert_eq!(frame.crest, 12.3);
    }

    // ── B-125: set_integrity（push_overflow と oversized_drop の合算）──────────────
    #[test]
    fn set_integrity_raises_degraded_on_oversized_drop_only() {
        // (i): push_overflow=0 でも oversized_drop>0 なら integrity_degraded が立つ。
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        assert!(!w.data().integrity_degraded); // 既定 false
        w.set_integrity(0, 7); // push=0, oversized=7
        assert!(w.data().integrity_degraded, "oversized_drop>0 で degraded");
        assert_eq!(w.data().dropped_samples, 7);
    }

    #[test]
    fn set_integrity_dropped_samples_is_sum_of_both_counters() {
        // (iii): dropped_samples = push_dropped + oversized_dropped（合算）。
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);

        w.set_integrity(0, 0); // 両者 0 → 欠落なし
        assert_eq!(w.data().dropped_samples, 0);
        assert!(!w.data().integrity_degraded);

        w.set_integrity(5, 11); // 5 + 11 = 16
        assert_eq!(w.data().dropped_samples, 16, "両カウンタの合算");
        assert!(w.data().integrity_degraded);

        w.set_integrity(3, 0); // push のみ（B-076 既存経路の不変性）
        assert_eq!(w.data().dropped_samples, 3);
        assert!(w.data().integrity_degraded);
    }

    #[test]
    fn append_psb_applies_rounding_and_flag() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        let psb = [-12.3456; 20];
        w.append_psb(500, psb, true);
        let snap = &w.data().psb_snapshots.as_ref().unwrap()[0];
        assert_eq!(snap.t_ms, 500);
        assert!(snap.interpolatable);
        assert_eq!(snap.psb[0], -12.3);
    }

    #[test]
    fn append_annotation_records_wallclock_and_memo() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_annotation("低域が重い".to_string());
        let a = &w.data().annotations[0];
        assert_eq!(a.memo, "低域が重い");
        assert!(!a.t.is_empty());
    }

    #[test]
    fn heartbeat_updates_on_demand() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        let before = w.data().heartbeat.clone();
        std::thread::sleep(std::time::Duration::from_secs(1));
        w.heartbeat_now();
        let after = w.data().heartbeat.clone();
        assert_ne!(before, after);
    }

    #[test]
    fn flush_writes_staging_file_atomically_without_publishing_final() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [0.5; 20], 1.0, -20.0, -3.0, 10.0, None);
        w.flush().unwrap();
        let final_path = &w.paths.final_path;
        let tmp_path = &w.paths.tmp_path;
        let staging_path = &w.paths.staging_path;
        assert!(
            !final_path.exists(),
            "flush must not publish final file before close: {final_path:?}"
        );
        assert!(
            staging_path.exists(),
            "staging file must exist: {staging_path:?}"
        );
        assert!(!tmp_path.exists(), "tmp file must be renamed away");
    }

    #[test]
    fn flush_produces_valid_checksum_roundtrip() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [1.0; 20], 1.5, -14.0, -1.0, 12.0, None);
        w.append_psb(0, [-10.0; 20], false);
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert!(verify_checksum(&loaded), "checksum must round-trip");
        assert_eq!(loaded.schema_version, "1.3");
        assert_eq!(loaded.frames.len(), 1);
    }

    #[test]
    fn tampered_frame_fails_checksum() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [1.0; 20], 1.5, -14.0, -1.0, 12.0, None);
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let mut loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        // 改ざん
        loaded.frames[0].lufs_m = -99.0;
        assert!(!verify_checksum(&loaded));
    }

    #[test]
    fn close_marks_status_closed_and_flushes() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0, None);
        w.append_frame(100, [1.1; 20], 1.1, -13.9, -0.9, 12.1, None);
        let failed_path = w.paths.failed_path.clone();
        w.close().unwrap();
        let bytes = fs::read(&failed_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.status, Status::Closed);
        assert_eq!(loaded.commit_status.as_deref(), Some("failed"));
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn post_writes_to_post_dir() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        w.flush().unwrap();
        let p = w.paths.final_path.to_string_lossy().to_string();
        assert!(p.contains("/post/"), "path should contain /post/: {p}");
    }

    #[test]
    fn bounce_marker_roundtrip() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.set_bounce_start("2026-04-17T14:32:08Z".to_string(), "hash_first".into());
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0, None);
        w.set_bounce_end(
            "2026-04-17T14:37:08Z".to_string(),
            14_400_000,
            "hash_last".into(),
        );
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(
            loaded.bounce_marker.wall_clock_start,
            "2026-04-17T14:32:08Z"
        );
        assert_eq!(loaded.bounce_marker.duration_samples, 14_400_000);
        assert_eq!(loaded.bounce_marker.first_block_hash, "hash_first");
        assert_eq!(loaded.bounce_marker.last_block_hash, "hash_last");
    }

    #[test]
    fn bounce_take_roundtrip_and_checksum() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0, None);
        w.set_bounce_take(BounceTake {
            source: "audio_time_trace".to_string(),
            time_axis: "frames_48k".to_string(),
            alignment_status: "sample_count_ready".to_string(),
            sample_rate: 96_000,
            wav_start_sample: 0,
            wav_end_sample: 1_440_000,
            duration_samples: 1_440_000,
            duration_frames_48k: 720_000,
            start_t_ms: 0,
            end_t_ms: 15_000,
            trace_sample_count: 150,
            frame_count: 150,
        });
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.sample_rate, 96_000);
        assert_eq!(take.duration_samples, 1_440_000);
        assert_eq!(take.duration_frames_48k, 720_000);
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn chain_memo_set_and_persisted() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.set_chain_memo("test memo".to_string());
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.chain_memo, "test memo");
    }

    #[test]
    fn validity_flag_defaults_true_can_flip_false() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        assert!(w.data().validity);
        w.set_validity(false);
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert!(!loaded.validity);
    }

    #[test]
    fn started_at_ms_serializes_and_checksum_roundtrips() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        let started_at_ms = 1_781_234_567_890;
        w.set_started_at_ms(started_at_ms);
        w.flush().unwrap();

        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let json = String::from_utf8(bytes.clone()).unwrap();
        assert!(
            json.contains("\"started_at_ms\":1781234567890"),
            "started_at_ms must be serialized: {json}"
        );
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.started_at_ms, started_at_ms);
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn record_session_id_serializes_only_when_set() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        w.set_record_session_id(Some("session-abc".to_string()));
        w.flush().unwrap();

        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let json = String::from_utf8(bytes.clone()).unwrap();
        assert!(
            json.contains("\"record_session_id\":\"session-abc\""),
            "record_session_id must be serialized when set: {json}"
        );
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.record_session_id.as_deref(), Some("session-abc"));
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn pre_post_started_at_ms_can_share_one_origin() {
        let base = isolated_dir();
        let started_at_ms = 1_781_000_000_123;
        let mut pre = sample_writer(&base, Role::Pre);
        let mut post = sample_writer(&base, Role::Post);
        pre.set_started_at_ms(started_at_ms);
        post.set_started_at_ms(started_at_ms);
        pre.flush().unwrap();
        post.flush().unwrap();

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre.paths.staging_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post.paths.staging_path).unwrap()).unwrap();
        assert_eq!(pre_data.started_at_ms, started_at_ms);
        assert_eq!(post_data.started_at_ms, started_at_ms);
        assert_eq!(pre_data.started_at_ms, post_data.started_at_ms);
        assert!(verify_checksum(&pre_data));
        assert!(verify_checksum(&post_data));
    }

    #[test]
    fn pair_finalize_publishes_manifest_hidden_members_and_trace_shelf() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-atomic",
            None,
            Some("iid-post-atomic".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-atomic",
            Some("iid-pre-atomic".to_string()),
            None,
        );
        crate::record_expected::write_expected_metadata(
            &base,
            "project_hash_test",
            &expected_wav_fixture(),
        )
        .unwrap();

        pre.data.status = Status::Closed;
        pre.data.commit_status = Some("pair_pending".to_string());
        post.data.status = Status::Closed;
        post.data.commit_status = Some("pair_pending".to_string());
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        let pre_trace_path = pair_trace_shelf_path(&pre_paths, &pre.data).unwrap();
        let post_trace_path = pair_trace_shelf_path(&post_paths, &post.data).unwrap();
        let mut stale_pre_final = pre.data.clone();
        let mut stale_post_final = post.data.clone();
        stale_pre_final.commit_status = Some("committed".to_string());
        stale_post_final.commit_status = Some("committed".to_string());
        write_plugin_data_atomic(&pre_paths.final_path, &mut stale_pre_final).unwrap();
        write_plugin_data_atomic(&post_paths.final_path, &mut stale_post_final).unwrap();
        write_plugin_data_atomic(&pre_trace_path, &mut stale_pre_final).unwrap();
        write_plugin_data_atomic(&post_trace_path, &mut stale_post_final).unwrap();
        assert!(pre_paths.final_path.exists());
        assert!(post_paths.final_path.exists());
        assert!(pre_trace_path.exists());
        assert!(post_trace_path.exists());
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();

        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();

        let manifest_path = pair_commit_manifest_path(&pre.paths, &pre.data).unwrap();
        assert!(!pre_paths.final_path.exists());
        assert!(!post_paths.final_path.exists());
        assert!(
            pre_paths.member_path.exists(),
            "PRE hidden member must exist"
        );
        assert!(
            post_paths.member_path.exists(),
            "POST hidden member must exist"
        );
        assert!(
            manifest_path.exists(),
            "pair commit manifest must be the publish barrier"
        );
        assert!(pre_trace_path.exists(), "PRE TRACE shelf JSON must exist");
        assert!(post_trace_path.exists(), "POST TRACE shelf JSON must exist");
        assert!(
            !pre_paths.pair_pending_path.exists() && !post_paths.pair_pending_path.exists(),
            "pending files are removed only after pair publish succeeds"
        );
        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        let pre_trace_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_trace_path).unwrap()).unwrap();
        let post_trace_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_trace_path).unwrap()).unwrap();
        assert_eq!(pre_data.commit_status.as_deref(), Some("committed"));
        assert_eq!(post_data.commit_status.as_deref(), Some("committed"));
        assert_eq!(pre_trace_data.commit_status.as_deref(), Some("committed"));
        assert_eq!(post_trace_data.commit_status.as_deref(), Some("committed"));
        assert!(pre_trace_data.validity);
        assert!(post_trace_data.validity);
        for data in [&pre_data, &post_data, &pre_trace_data, &post_trace_data] {
            let quality = data.record_quality.as_ref().expect("record_quality");
            assert_eq!(quality.status, "complete");
            assert!(quality.complete);
            assert!(quality.usable);
            assert!(quality.expected_wav_ready);
            assert!(quality.sample_count_ready);
            assert!(quality.trace_slots_complete);
            assert_eq!(quality.missing_trace_slots, 0);
        }
        assert!(verify_checksum(&pre_data));
        assert!(verify_checksum(&post_data));
        assert!(verify_checksum(&pre_trace_data));
        assert!(verify_checksum(&post_trace_data));
        assert_eq!(pre_trace_data.checksum, pre_data.checksum);
        assert_eq!(post_trace_data.checksum, post_data.checksum);
        let manifest = String::from_utf8(fs::read(&manifest_path).unwrap()).unwrap();
        assert!(manifest.contains("session-pair-atomic"));
        assert!(manifest.contains("iid-pre-atomic"));
        assert!(manifest.contains("iid-post-atomic"));
        assert!(manifest.contains(PAIR_RECORD_MEMBERS_DIR));
        assert!(matches!(
            crate::record_expected::read_expected_metadata(&base, "project_hash_test"),
            Err(crate::record_expected::ExpectedMetadataError::Consumed)
        ));
    }

    #[test]
    fn pair_finalize_commits_missing_expected_as_degraded_trace() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-no-expected",
            None,
            Some("iid-post-no-expected".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-no-expected",
            Some("iid-pre-no-expected".to_string()),
            None,
        );
        pre.set_expected_wav(None);
        post.set_expected_wav(None);
        pre.data.status = Status::Closed;
        pre.data.commit_status = Some("pair_pending".to_string());
        post.data.status = Status::Closed;
        post.data.commit_status = Some("pair_pending".to_string());

        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();

        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();

        let manifest_path = pair_commit_manifest_path(&pre.paths, &pre.data).unwrap();
        let pre_trace_path = pair_trace_shelf_path(&pre_paths, &pre.data).unwrap();
        let post_trace_path = pair_trace_shelf_path(&post_paths, &post.data).unwrap();
        assert!(manifest_path.exists());
        assert!(pre_paths.member_path.exists());
        assert!(post_paths.member_path.exists());
        assert!(pre_trace_path.exists());
        assert!(post_trace_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        let pre_trace_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_trace_path).unwrap()).unwrap();
        let post_trace_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_trace_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data, &pre_trace_data, &post_trace_data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert!(data.validity);
            assert!(data.integrity_degraded);
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "missing_expected_wav_metadata"));
            let quality = data.record_quality.as_ref().expect("record_quality");
            assert_eq!(quality.status, "usable_fallback");
            assert!(!quality.complete);
            assert!(quality.usable);
            assert!(!quality.expected_wav_ready);
            assert!(quality.trace_slots_complete);
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn pair_finalize_commits_missing_trace_slots_as_degraded_trace() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-missing-slots",
            None,
            Some("iid-post-missing-slots".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-missing-slots",
            Some("iid-pre-missing-slots".to_string()),
            None,
        );
        let missing_slot_diag = TraceDiagnostics {
            raw_trace_count: 121,
            expected_frame_count: 152,
            measured_frame_count: 121,
            missing_slots: 31,
            explicit_silence_frame_count: 0,
        };
        pre.set_trace_diagnostics(missing_slot_diag.clone());
        post.set_trace_diagnostics(missing_slot_diag);
        pre.data.status = Status::Closed;
        pre.data.commit_status = Some("pair_pending".to_string());
        post.data.status = Status::Closed;
        post.data.commit_status = Some("pair_pending".to_string());

        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();

        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();

        let manifest_path = pair_commit_manifest_path(&pre.paths, &pre.data).unwrap();
        let pre_trace_path = pair_trace_shelf_path(&pre_paths, &pre.data).unwrap();
        let post_trace_path = pair_trace_shelf_path(&post_paths, &post.data).unwrap();
        assert!(manifest_path.exists());
        assert!(pre_paths.member_path.exists());
        assert!(post_paths.member_path.exists());
        assert!(pre_trace_path.exists());
        assert!(post_trace_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        let pre_trace_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_trace_path).unwrap()).unwrap();
        let post_trace_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_trace_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data, &pre_trace_data, &post_trace_data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert!(data.validity);
            assert!(data.integrity_degraded);
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "missing_trace_slots"));
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "trace_frame_count_mismatch"));
            let quality = data.record_quality.as_ref().expect("record_quality");
            assert_eq!(quality.status, "usable_fallback");
            assert!(!quality.complete);
            assert!(quality.usable);
            assert!(quality.expected_wav_ready);
            assert!(quality.sample_count_ready);
            assert!(!quality.trace_slots_complete);
            assert_eq!(quality.expected_frame_count, 152);
            assert_eq!(quality.measured_frame_count, 121);
            assert_eq!(quality.missing_trace_slots, 31);
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn reconcile_pair_committed_trace_shelves_restores_hidden_only_commit() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-reconcile",
            None,
            Some("iid-post-reconcile".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-reconcile",
            Some("iid-pre-reconcile".to_string()),
            None,
        );
        crate::record_expected::write_expected_metadata(
            &base,
            "project_hash_test",
            &expected_wav_fixture(),
        )
        .unwrap();
        pre.data.status = Status::Closed;
        pre.data.commit_status = Some("pair_pending".to_string());
        post.data.status = Status::Closed;
        post.data.commit_status = Some("pair_pending".to_string());
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        let pre_trace_path = pair_trace_shelf_path(&pre_paths, &pre.data).unwrap();
        let post_trace_path = pair_trace_shelf_path(&post_paths, &post.data).unwrap();
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();
        assert!(pre_paths.member_path.exists());
        assert!(post_paths.member_path.exists());
        assert!(pre_trace_path.exists());
        assert!(post_trace_path.exists());

        fs::remove_file(&pre_trace_path).unwrap();
        fs::remove_file(&post_trace_path).unwrap();
        assert!(!pre_trace_path.exists());
        assert!(!post_trace_path.exists());

        assert_eq!(reconcile_pair_committed_trace_shelves(&base), 2);
        let pre_member: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let post_member: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        let pre_trace: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_trace_path).unwrap()).unwrap();
        let post_trace: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_trace_path).unwrap()).unwrap();
        assert_eq!(pre_trace.checksum, pre_member.checksum);
        assert_eq!(post_trace.checksum, post_member.checksum);
        assert!(verify_checksum(&pre_trace));
        assert!(verify_checksum(&post_trace));
        assert_eq!(reconcile_pair_committed_trace_shelves(&base), 0);
    }

    #[test]
    fn pair_finalize_quarantines_zero_frames_even_with_trace_diagnostics() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-zero-frames",
            None,
            Some("iid-post-zero-frames".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-zero-frames",
            Some("iid-pre-zero-frames".to_string()),
            None,
        );
        let diag = TraceDiagnostics {
            raw_trace_count: 121,
            expected_frame_count: 152,
            measured_frame_count: 0,
            missing_slots: 31,
            explicit_silence_frame_count: 0,
        };
        pre.clear_frames();
        post.clear_frames();
        pre.set_trace_diagnostics(diag.clone());
        post.set_trace_diagnostics(diag);
        pre.data.status = Status::Closed;
        pre.data.commit_status = Some("pair_pending".to_string());
        post.data.status = Status::Closed;
        post.data.commit_status = Some("pair_pending".to_string());

        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();

        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();

        let manifest_path = pair_commit_manifest_path(&pre.paths, &pre.data).unwrap();
        assert!(!manifest_path.exists());
        assert!(!pre_paths.member_path.exists());
        assert!(!post_paths.member_path.exists());
        assert!(pre_paths.failed_path.exists());
        assert!(post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.failed_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.failed_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("failed"));
            assert!(!data.validity);
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "zero_trace_frames"));
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "missing_trace_slots"));
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn fallback_started_at_ms_may_differ_between_pre_and_post() {
        let base = isolated_dir();
        let mut pre = sample_writer(&base, Role::Pre);
        let mut post = sample_writer(&base, Role::Post);
        pre.set_started_at_ms(1_781_000_010_000);
        post.set_started_at_ms(1_781_000_012_500);
        pre.flush().unwrap();
        post.flush().unwrap();

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre.paths.staging_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post.paths.staging_path).unwrap()).unwrap();
        assert_ne!(pre_data.started_at_ms, post_data.started_at_ms);
        assert!(verify_checksum(&pre_data));
        assert!(verify_checksum(&post_data));
    }

    #[test]
    fn multiple_flushes_keep_consistency() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0, None);
        w.flush().unwrap();
        w.append_frame(100, [1.5; 20], 1.2, -13.0, -0.5, 11.0, None);
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.frames.len(), 2);
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn round1_handles_negative_and_special() {
        assert_eq!(round1(-1.25), -1.3);
        assert_eq!(round1(0.0), 0.0);
        assert!(round1(f64::NAN).is_nan());
        assert_eq!(round1(f64::INFINITY), f64::INFINITY);
    }

    #[test]
    fn round2_handles_negative_and_special() {
        assert_eq!(round2(-1.235), -1.24);
        assert_eq!(round2(0.0), 0.0);
        assert!(round2(f64::NAN).is_nan());
    }

    // ── append_annotation_to_latest（サブ2-C / Note ボタン）─────────────────

    #[test]
    fn append_annotation_to_latest_returns_false_when_dir_missing() {
        let base = isolated_dir();
        // project_hash/instance_id/post ディレクトリ自体が無い状態
        let result = append_annotation_to_latest(
            &base,
            "ph",
            TEST_INSTANCE_ID,
            Role::Post,
            "Good".to_string(),
        )
        .unwrap();
        assert!(!result, "missing dir should yield stub (Ok(false))");
    }

    #[test]
    fn append_annotation_to_latest_returns_false_when_no_json() {
        let base = isolated_dir();
        let dir = base.join("ph").join(TEST_INSTANCE_ID).join("post");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("20260417T140000.json.tmp"), b"{}").unwrap();
        let result = append_annotation_to_latest(
            &base,
            "ph",
            TEST_INSTANCE_ID,
            Role::Post,
            "Good".to_string(),
        )
        .unwrap();
        assert!(!result);
    }

    #[test]
    fn append_annotation_to_latest_updates_file_and_checksum() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0, None);
        let path = write_final_for_annotation_test(w);

        let ok = append_annotation_to_latest(
            &base,
            "project_hash_test",
            TEST_INSTANCE_ID,
            Role::Post,
            "Fix".to_string(),
        )
        .unwrap();
        assert!(ok);

        let bytes = fs::read(&path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.annotations.len(), 1);
        assert_eq!(loaded.annotations[0].memo, "Fix");
        assert!(!loaded.annotations[0].t.is_empty());
        assert!(
            verify_checksum(&loaded),
            "checksum must re-sign after append"
        );
    }

    #[test]
    fn append_annotation_to_latest_picks_lexicographic_max_filename() {
        let base = isolated_dir();
        let dir = base.join("ph").join(TEST_INSTANCE_ID).join("post");
        fs::create_dir_all(&dir).unwrap();

        for stamp in ["20260417T140000", "20260417T150000", "20260417T143000"] {
            let paths = WriterPaths {
                final_path: dir.join(format!("{stamp}.json")),
                tmp_path: dir.join(format!("{stamp}.json.tmp")),
                staging_path: dir.join(format!("{stamp}.json.partial")),
                pair_pending_path: dir.join(".pair_pending").join(format!("{stamp}.json")),
                failed_path: dir.join(".failed").join(format!("{stamp}.json")),
            };
            let mut w = PluginDataWriter::create(
                paths,
                "iid".to_string(),
                "ph".to_string(),
                TEST_INSTANCE_ID.to_string(),
                Role::Post,
                None,
                48000,
                Some("paired-pre-test".to_string()),
                None,
                None,
                None,
            )
            .unwrap();
            w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0, None);
            let _ = write_final_for_annotation_test(w);
        }

        let ok = append_annotation_to_latest(
            &base,
            "ph",
            TEST_INSTANCE_ID,
            Role::Post,
            "Hold".to_string(),
        )
        .unwrap();
        assert!(ok);

        let latest = fs::read(dir.join("20260417T150000.json")).unwrap();
        let other = fs::read(dir.join("20260417T140000.json")).unwrap();
        let latest: PluginDataFile = serde_json::from_slice(&latest).unwrap();
        let other: PluginDataFile = serde_json::from_slice(&other).unwrap();
        assert_eq!(latest.annotations.len(), 1);
        assert_eq!(latest.annotations[0].memo, "Hold");
        assert!(other.annotations.is_empty());
    }

    #[test]
    fn append_annotation_to_latest_appends_to_existing_annotations() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        w.append_annotation("初期メモ".to_string());
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0, None);
        let final_path = write_final_for_annotation_test(w);

        let ok = append_annotation_to_latest(
            &base,
            "project_hash_test",
            TEST_INSTANCE_ID,
            Role::Post,
            "Good".to_string(),
        )
        .unwrap();
        assert!(ok);

        let bytes = fs::read(&final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.annotations.len(), 2);
        assert_eq!(loaded.annotations[0].memo, "初期メモ");
        assert_eq!(loaded.annotations[1].memo, "Good");
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn append_annotation_to_latest_updates_manifest_hidden_pair_member() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-note",
            None,
            Some("iid-post-note".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-note",
            Some("iid-pre-note".to_string()),
            None,
        );
        crate::record_expected::write_expected_metadata(
            &base,
            "project_hash_test",
            &expected_wav_fixture(),
        )
        .unwrap();
        pre.data.status = Status::Closed;
        pre.data.commit_status = Some("pair_pending".to_string());
        post.data.status = Status::Closed;
        post.data.commit_status = Some("pair_pending".to_string());
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        let post_trace_path = pair_trace_shelf_path(&post_paths, &post.data).unwrap();
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();
        assert!(post_trace_path.exists());
        assert!(post_paths.member_path.exists());

        let ok = append_annotation_to_latest(
            &base,
            "project_hash_test",
            "iid-post-note",
            Role::Post,
            "Manifest Note".to_string(),
        )
        .unwrap();
        assert!(ok);

        let member: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        let trace: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_trace_path).unwrap()).unwrap();
        for loaded in [&member, &trace] {
            assert_eq!(loaded.annotations.len(), 1);
            assert_eq!(loaded.annotations[0].memo, "Manifest Note");
            assert!(verify_checksum(loaded));
        }
    }

    #[test]
    fn append_annotation_to_latest_errors_on_corrupt_json() {
        let base = isolated_dir();
        let dir = base.join("ph").join(TEST_INSTANCE_ID).join("post");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("20260417T140000.json"), b"not a json").unwrap();
        let err = append_annotation_to_latest(
            &base,
            "ph",
            TEST_INSTANCE_ID,
            Role::Post,
            "Good".to_string(),
        );
        assert!(err.is_err(), "corrupt JSON should yield error");
    }

    #[test]
    fn source_format_48000_writes_phase_d_values() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre); // sample_writer は 48000 で create
        assert_eq!(w.data().source_format, 48000);

        w.append_frame(0, [1.5; 20], 1.5, -14.0, -1.0, 12.0, None);
        // PSB スナップショットも書き込み
        w.append_psb(0, [-10.0; 20], true);
        w.flush().unwrap();

        let bytes = fs::read(&w.paths.staging_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        // source_format が JSON に出力されている (S-1)
        assert_eq!(loaded.source_format, 48000);
        assert!(loaded.frames[0].n_prime.is_some());
        assert!(loaded.frames[0].sharpness.is_some());
        assert!(loaded.psb_snapshots.is_some());
        assert_eq!(loaded.psb_snapshots.as_ref().unwrap().len(), 1);
        // LUFS-M / TP / Crest は通常通り
        assert_eq!(loaded.frames[0].lufs_m, -14.0);
        assert_eq!(loaded.frames[0].true_peak, -1.0);
        assert_eq!(loaded.frames[0].crest, 12.0);

        // JSON 文字列上で `"source_format":48000` が含まれることも確認
        let json_str = std::str::from_utf8(&bytes).unwrap();
        assert!(json_str.contains("\"source_format\":48000"));
    }

    /// B-132 (G-115-382 共通B): mark_integrity_degraded は dropped_samples=0 でも degraded を立て、
    /// set_integrity(0,0) の後に呼んでも上書きされず true を保つ（drain timeout → 不完全と記録）。
    #[test]
    fn b132_mark_integrity_degraded_ors_on_top_of_set_integrity() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        // 欠落カウント 0（clean）→ set_integrity は degraded=false。
        w.set_integrity(0, 0);
        assert!(!w.data().integrity_degraded, "clean は degraded=false");
        // drain timeout 相当 → 強制 degraded。dropped_samples は 0 のまま（独立軸）。
        w.mark_integrity_degraded();
        assert!(
            w.data().integrity_degraded,
            "timeout で degraded=true（OR）"
        );
        assert_eq!(
            w.data().dropped_samples,
            0,
            "degraded は dropped_samples を動かさない"
        );
    }

    #[test]
    fn close_failed_record_writes_failed_diagnostic_without_final_publish() {
        let base = isolated_dir();
        let w = sample_writer(&base, Role::Pre);
        let final_path = w.paths.final_path.clone();
        let failed_path = w.paths.failed_path.clone();
        let staging_path = w.paths.staging_path.clone();

        w.close().unwrap();

        assert!(
            !final_path.exists(),
            "zero-frame Record must not be published as normal JSON"
        );
        assert!(
            !staging_path.exists(),
            "staging file must be removed after failed close"
        );
        assert!(failed_path.exists(), "failed diagnostic JSON must remain");
        let bytes = fs::read(&failed_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.commit_status.as_deref(), Some("failed"));
        assert!(!loaded.validity);
        assert!(loaded.integrity_degraded);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "zero_trace_frames"));
        assert!(verify_checksum(&loaded));
    }
}
