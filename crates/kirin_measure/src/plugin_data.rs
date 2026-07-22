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
use std::collections::HashMap;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

pub use crate::record_expected::ExpectedWavMetadata;

type HmacSha256 = Hmac<Sha256>;
const PAIR_RECORD_SESSIONS_DIR: &str = "record_sessions";
const PAIR_RECORD_MEMBERS_DIR: &str = ".pair_committed";
const PAIR_RECORD_SESSION_SCHEMA: &str = "pair_record_session.v1";
const TRACE_SHELF_COLLISION_SEARCH_SECS: i64 = 300;
const BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY: &str = "sample_count_ready";
const BOUNCE_SOURCE_EXPECTED_WAV: &str = "expected_wav_duration_native";
const BOUNCE_SOURCE_WAV_CLOCK: &str = "wav_clock_native";
const BOUNCE_SOURCE_RENDER_CLOCK: &str = "render_clock_native";
const TRACE_FRAME_INTERVAL_MS: u64 = 100;
const TRACE_TIMEBASE_HZ: u64 = 48_000;
const LATE_EXPECTED_BEFORE_RECORD_GRACE_MS: i64 = 2_000;
const LATE_EXPECTED_AFTER_RECORD_GRACE_MS: i64 = 5 * 60 * 1_000;
const LATE_EXPECTED_METADATA_FUTURE_SKEW_MS: i64 = 60 * 1_000;

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
    /// ISO 532-1 total loudness N(t), as produced by Phase D. This is persisted separately from
    /// n_prime[20]; consumers must not reconstruct it by summing the Bark-band means.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub n_prime_total: Option<f64>,
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

/// Record MARK の sample-clock identity。
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AnnotationMark {
    pub schema_version: String,
    pub id: String,
    pub basis: String,
    pub producer_position_samples: i64,
    pub clock_source: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub wav_position_samples: Option<u64>,
}

/// 利用者 MARK。`mark=None` は旧 NOTE JSON の読み書き互換。
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Annotation {
    /// ISO 8601 wall clock.
    pub t: String,
    pub memo: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub mark: Option<AnnotationMark>,
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
    /// Producer-owned output-presentation range corresponding to WAV sample `0..N`.
    /// The legacy field name contains `host`, but current producers never store the untouched raw
    /// callback clock here; that independent diagnostic belongs to `raw_host_clock_range`.
    /// Legacy artifacts and hosts without an exact range omit both fields.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host_start_position_samples: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host_end_position_samples: Option<i64>,
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

/// Producer-owned absolute sample clock used to bake every TRACE frame.
///
/// `origin_position_samples` is the absolute callback position corresponding to `t_ms = 0`.
/// Every measured slot must independently derive the same origin before this metadata is written.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct TraceClock {
    pub basis: String,
    pub origin_position_samples: i64,
    pub end_position_samples: i64,
    pub sample_rate: u32,
    pub sources: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
pub struct HostPresentationLatencyObservation {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub input_samples: Option<u32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub output_samples: Option<u32>,
}

/// Record-only measurement candidate with the untouched host clock facts that formed it.
///
/// Unlike `trace_slot_positions`, this journal is not a public TRACE axis. It survives until the
/// dropped WAV is known so pair finalization can test host-specific presentation-clock models per
/// side and per latency epoch without touching metric values or retaining unbounded raw audio.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TraceClockObservation {
    pub frame: Frame,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub producer_position_samples: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub raw_host_position_samples: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub capture_epoch: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub clock_source: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub presentation_latency_source: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub input_presentation_latency_samples: Option<u32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub output_presentation_latency_samples: Option<u32>,
}

/// Unmodified host render range retained as diagnostics after TRACE is normalized to the output
/// presentation/WAV axis. It is never used by consumers to shift a curve.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub struct HostClockRange {
    pub start_position_samples: i64,
    pub end_position_samples: i64,
}

/// Final TRACE axis bound to the exact WAV selected by Drop.
///
/// Host callback positions remain in trace_clock as per-instance diagnostics. This reference is
/// the pair-wide coordinate system: WAV sample 0 through the exact header-derived sample count.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct TraceWavReference {
    pub basis: String,
    pub capture_basis: String,
    pub start_sample: u64,
    pub end_sample: u64,
    pub sample_rate: u32,
    pub record_session_id: String,
    pub bounce_id: String,
    pub wav_hash: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RecordQuality {
    /// `complete` = sample-count aligned full product.
    /// `usable_fallback` = identified measured data whose timing/integrity proof is incomplete;
    /// TRACE may display it while keeping exactness separate.
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
    /// Keep/All Keep producer transaction. New captures always carry this on
    /// both roles; empty is accepted only while reading legacy artifacts.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub capture_generation_id: Option<String>,
    #[serde(default)]
    pub generation_started_at_ms: i64,
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
    /// TRACE time axis. Before Drop this is the per-instance host diagnostic axis; normal pair
    /// publication replaces it with the exact dropped-WAV sample axis.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_time_axis: Option<String>,
    /// Per-instance host callback origin/range and provenance. This remains diagnostic and is not
    /// compared across PRE/POST because DAW PDC and wrapper scheduling can shift it.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_clock: Option<TraceClock>,
    /// Distinct VST3/AU presentation-latency states in first-observed order during this take.
    /// This diagnostic is independent of trace-clock completeness so failed/pending takes remain
    /// inspectable. Empty means the wrapper/host did not provide the optional interface; zero is
    /// preserved. `source` keeps VST3 and Audio Unit v2 evidence distinguishable.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub host_presentation_latency_observations: Vec<HostPresentationLatencyObservation>,
    /// 10 fps Record clock journal. One entry corresponds to one measured candidate; it
    /// is cleared after WAV binding and is never read by Kirin OS as a display axis.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_clock_observations: Vec<TraceClockObservation>,
    /// Clock model selected for this side at Drop, or the explicit chronological fallback.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_clock_resolution: Option<String>,
    /// Independent PRE/POST comparison qualification. This never changes the role-specific WAV
    /// placement above. Current captures publish the producer content-sample clock; legacy
    /// fallback captures may publish an untouched raw-host clock.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_comparison_resolution: Option<String>,
    /// Comparison-clock position corresponding one-to-one with every public `frames[]` entry.
    /// `producer_content_samples_exact` uses samples relative to the producer's factual WAV
    /// origin before role-specific output latency; `raw_host_clock_exact` uses absolute raw-host
    /// endpoints. Neither form may be inferred from metric shape.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_comparison_slot_positions: Vec<i64>,
    /// Producer-private raw host endpoint corresponding one-to-one with the factual Record frames.
    /// The initial list is baked at Record close. Late WAV binding windows frames and endpoints as
    /// one unit, then retains only an exact selected pair list so a repeated Drop resolves
    /// identically. Kirin OS never uses this field as a display axis or comparison proof.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_raw_host_slot_positions: Vec<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub raw_host_clock_range: Option<HostClockRange>,
    /// Producer-canonical native-sample endpoint of every `frames[]` entry. Before pair
    /// finalization this is the host callback position; afterwards both roles carry the same
    /// absolute host-sample endpoint selected by the producer-owned WAV-start clock.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_slot_positions: Vec<i64>,
    /// Pair-wide final axis. Present only after Drop supplies a complete WAV identity and length.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_wav_reference: Option<TraceWavReference>,
    /// PRE/POST が同一 Record session と dropped-WAV sample axis 上で完成済みである契約。
    /// consumer は内容推定も時刻補正も行わない。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_content_alignment: Option<crate::trace_alignment::TraceContentAlignment>,
    /// Legacy B-356 staging field. Current producers always leave this empty and current pair
    /// publication never reads it; it remains only so older on-disk JSON can deserialize safely.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_context_frames: Vec<Frame>,
    /// Legacy endpoints for `trace_context_frames`. Current producer truth is exclusively
    /// `frames + trace_slot_positions`; missing current slots are not repaired from this field.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_context_slot_positions: Vec<i64>,
    /// Current v3 pair-finalization proof. It records which factual start anchor selected the
    /// identical PRE/POST host-sample window and is removed when the pair is published.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_pair_wav_start_basis: Option<String>,
    /// Legacy v2 staging fields. Current producers never derive or publish a curve-based offset.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_pair_offset_samples: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_pair_alignment_score: Option<f64>,
    /// B-356 以前の staging JSON を読み戻すための legacy field。publish 前に必ず消去する。
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_context_psb_snapshots: Vec<PsbSnapshot>,
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
            capture_generation_id: None,
            generation_started_at_ms: 0,
            started_at_ms: 0,
            dropped_samples: 0,
            integrity_degraded: false,
            commit_status: None,
            integrity_reasons: Vec::new(),
            bounce_take: None,
            expected_wav: None,
            trace_diagnostics: None,
            trace_time_axis: None,
            trace_clock: None,
            host_presentation_latency_observations: Vec::new(),
            trace_clock_observations: Vec::new(),
            trace_clock_resolution: None,
            trace_comparison_resolution: None,
            trace_comparison_slot_positions: Vec::new(),
            trace_raw_host_slot_positions: Vec::new(),
            raw_host_clock_range: None,
            trace_slot_positions: Vec::new(),
            trace_wav_reference: None,
            trace_content_alignment: None,
            trace_context_frames: Vec::new(),
            trace_context_slot_positions: Vec::new(),
            trace_pair_wav_start_basis: None,
            trace_pair_offset_samples: None,
            trace_pair_alignment_score: None,
            trace_context_psb_snapshots: Vec::new(),
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
        self.data.trace_slot_positions.clear();
    }

    pub(crate) fn set_trace_context(&mut self, frames: Vec<Frame>, slot_positions: Vec<i64>) {
        self.data.trace_context_frames = frames;
        self.data.trace_context_slot_positions = slot_positions;
    }

    pub(crate) fn clear_psb_snapshots(&mut self) {
        self.data.psb_snapshots = None;
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
        if self.data.expected_wav.is_none() && self.data.trace_wav_reference.is_some() {
            self.data.trace_wav_reference = None;
            self.data.trace_content_alignment = None;
            self.data.trace_time_axis = self
                .data
                .trace_clock
                .as_ref()
                .map(|_| crate::trace_alignment::TRACE_HOST_TIME_AXIS.to_string());
        }
    }

    /// TRACE bake 診断を設定する。
    pub fn set_trace_diagnostics(&mut self, diagnostics: TraceDiagnostics) {
        self.data.trace_diagnostics = Some(diagnostics);
    }

    pub(crate) fn set_trace_time_axis(&mut self, time_axis: Option<String>) {
        self.data.trace_time_axis = time_axis;
    }

    pub(crate) fn set_trace_clock(&mut self, trace_clock: Option<TraceClock>) {
        self.data.trace_clock = trace_clock;
    }

    pub(crate) fn set_host_presentation_latency_observations(
        &mut self,
        observations: Vec<HostPresentationLatencyObservation>,
    ) {
        self.data.host_presentation_latency_observations = observations;
    }

    pub(crate) fn set_trace_clock_observations(
        &mut self,
        observations: Vec<TraceClockObservation>,
    ) {
        self.data.trace_clock_observations = observations;
    }

    pub(crate) fn set_raw_host_clock_range(&mut self, range: Option<HostClockRange>) {
        self.data.raw_host_clock_range = range;
    }

    pub(crate) fn set_trace_slot_positions(&mut self, positions: Vec<i64>) {
        self.data.trace_slot_positions = positions;
    }

    pub(crate) fn set_trace_raw_host_slot_positions(&mut self, positions: Vec<i64>) {
        self.data.trace_raw_host_slot_positions = positions;
    }

    #[cfg(test)]
    pub(crate) fn set_trace_wav_reference(
        &mut self,
        trace_wav_reference: Option<TraceWavReference>,
    ) {
        self.data.trace_wav_reference = trace_wav_reference;
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
            None,
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
        n_prime_total: Option<f64>,
        sharpness: Option<f64>,
        lufs_m: f64,
        true_peak: f64,
        crest: f64,
        psr: Option<f64>,
    ) {
        self.data.frames.push(make_frame_optional_with_n(
            t_ms,
            n_prime,
            n_prime_total,
            sharpness,
            lufs_m,
            true_peak,
            crest,
            psr,
        ));
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
            mark: None,
        });
    }

    /// POST IO writer が queue 済み MARK を冪等追記する。
    pub fn append_record_mark_if_absent(&mut self, annotation: Annotation) -> bool {
        let Some(id) = annotation.mark.as_ref().map(|mark| mark.id.as_str()) else {
            return false;
        };
        if self
            .data
            .annotations
            .iter()
            .any(|existing| existing.mark.as_ref().map(|mark| mark.id.as_str()) == Some(id))
        {
            return false;
        }
        self.data.annotations.push(annotation);
        true
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

    pub fn set_capture_generation(
        &mut self,
        capture_generation_id: Option<String>,
        generation_started_at_ms: i64,
    ) {
        self.data.capture_generation_id = non_empty_string(capture_generation_id);
        self.data.generation_started_at_ms = generation_started_at_ms.max(0);
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
            let self_paths = self_paths_for(&self.paths, &self.data);
            self.write_atomic_with_tmp(
                self_paths.failed_path.clone(),
                sibling_tmp_path(&self_paths.failed_path),
            )?;
            mark_pair_expected_metadata_consumed(&self.paths, &self.data);
        }
        let _ = fs::remove_file(&self.paths.staging_path);
        Ok(())
    }

    fn commit_failure_reasons(&self) -> Vec<&'static str> {
        side_visibility_failure_reasons(&self.data)
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

/// Facts whose absence makes a Record lane impossible to identify or display. Timing density,
/// canonical WAV proof and integrity quality are intentionally excluded: they remain diagnostics
/// and must not move measured user data to `.failed`.
fn side_visibility_failure_reasons(data: &PluginDataFile) -> Vec<&'static str> {
    let mut reasons = Vec::new();
    let belongs_to_capture_generation = data
        .capture_generation_id
        .as_deref()
        .is_some_and(|generation| !generation.is_empty())
        && data.generation_started_at_ms > 0;
    if data.frames.is_empty() && !belongs_to_capture_generation {
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

fn bounce_source_is_sample_count_ready(source: &str) -> bool {
    matches!(
        source,
        BOUNCE_SOURCE_EXPECTED_WAV | BOUNCE_SOURCE_WAV_CLOCK | BOUNCE_SOURCE_RENDER_CLOCK
    )
}

fn bounce_take_sample_count_ready(data: &PluginDataFile) -> bool {
    let Some(take) = data.bounce_take.as_ref() else {
        return false;
    };
    if take.alignment_status != BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY
        || data.sample_rate == 0
        || take.sample_rate != data.sample_rate
        || take.duration_samples == 0
        || take.wav_end_sample < take.wav_start_sample
        || take.frame_count == 0
    {
        return false;
    }
    match take.source.as_str() {
        BOUNCE_SOURCE_EXPECTED_WAV => expected_wav_matches_take(data, take),
        BOUNCE_SOURCE_WAV_CLOCK | BOUNCE_SOURCE_RENDER_CLOCK => true,
        _ => false,
    }
}

fn expected_wav_matches_take(data: &PluginDataFile, take: &BounceTake) -> bool {
    data.expected_wav.as_ref().is_some_and(|expected| {
        expected.is_usable()
            && data.sample_rate == expected.expected_sample_rate
            && take.sample_rate == expected.expected_sample_rate
            && take.duration_samples == expected.expected_duration_samples
    })
}

fn u128_to_u64_saturating(value: u128) -> u64 {
    value.min(u64::MAX as u128) as u64
}

fn bounce_take_duration_ms(take: &BounceTake) -> Option<u64> {
    if take.sample_rate == 0 {
        return None;
    }
    Some(u128_to_u64_saturating(
        (take.duration_samples as u128)
            .saturating_mul(1_000)
            .saturating_div(take.sample_rate as u128),
    ))
}

fn bounce_take_duration_frames_48k(take: &BounceTake) -> Option<u64> {
    if take.sample_rate == 0 {
        return None;
    }
    Some(u128_to_u64_saturating(
        (take.duration_samples as u128)
            .saturating_mul(TRACE_TIMEBASE_HZ as u128)
            .saturating_div(take.sample_rate as u128),
    ))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct LatencyMappedTraceCoverage {
    expected_frame_count: u64,
    measured_frame_count: u64,
    missing_slots: u64,
}

fn uses_latency_mapped_wav_grid(data: &PluginDataFile) -> bool {
    data.trace_clock_resolution.as_deref() == Some("producer_plus_output_latency")
}

fn latency_mapped_source_times(data: &PluginDataFile) -> Option<Vec<u64>> {
    if data.trace_clock_observations.is_empty() {
        return None;
    }
    let origin = data
        .expected_wav
        .as_ref()
        .and_then(|expected| expected.wav_time_reference_samples)
        .and_then(|origin| i64::try_from(origin).ok())
        .or_else(|| {
            data.trace_clock
                .as_ref()
                .map(|clock| clock.origin_position_samples)
        })?;
    let mut source_times_by_wav_slot = HashMap::new();
    for observation in &data.trace_clock_observations {
        let (Some(producer_position), Some(output_latency)) = (
            observation.producer_position_samples,
            observation.output_presentation_latency_samples,
        ) else {
            continue;
        };
        let aligned = producer_position.checked_add(i64::from(output_latency))?;
        let wav_slot = aligned.checked_sub(origin)?;
        if !data.trace_slot_positions.contains(&wav_slot) {
            continue;
        }
        if source_times_by_wav_slot
            .insert(wav_slot, observation.frame.t_ms)
            .is_some()
        {
            return None;
        }
    }
    let source_times = data
        .trace_slot_positions
        .iter()
        .map(|slot| source_times_by_wav_slot.get(slot).copied())
        .collect::<Option<Vec<_>>>()?;
    source_times
        .windows(2)
        .all(|pair| pair[0] < pair[1])
        .then_some(source_times)
}

/// Count only complete 100 ms windows that can exist on this side's factual latency-mapped
/// lattice. A non-zero presentation-latency phase normally leaves complementary partial ranges at
/// the WAV head and tail; those two edge remainders are not a missing measurement. Missing first,
/// middle, or last lattice positions remain explicit gaps.
fn latency_mapped_trace_coverage(
    data: &PluginDataFile,
    duration_samples: u64,
    sample_rate: u32,
) -> Option<LatencyMappedTraceCoverage> {
    if !uses_latency_mapped_wav_grid(data)
        || sample_rate == 0
        || data.frames.is_empty()
        || data.trace_slot_positions.len() != data.frames.len()
    {
        return None;
    }
    let slot_samples = i64::from(sample_rate / 10).max(1);
    let half_slot = (slot_samples / 2).max(1);
    let duration = i64::try_from(duration_samples).ok()?;
    let first = *data.trace_slot_positions.first()?;
    let last = *data.trace_slot_positions.last()?;
    if first < slot_samples || last > duration {
        return None;
    }
    if data
        .trace_slot_positions
        .iter()
        .zip(&data.frames)
        .any(|(slot, frame)| {
            u64::try_from(*slot).ok().is_none_or(|slot| {
                frame.t_ms != slot.saturating_mul(1_000) / u64::from(sample_rate)
            })
        })
    {
        return None;
    }
    if !data
        .trace_slot_positions
        .windows(2)
        .all(|pair| pair[0] < pair[1])
    {
        return None;
    }

    // The earliest complete window on any phase ends in [slot, 2*slot). Every full slot beyond
    // that boundary is a real missing head window, not an arbitrary phase remainder.
    let mut missing_slots = u64::try_from(first / slot_samples).ok()?.saturating_sub(1);
    if !data.trace_clock_observations.is_empty() {
        // Presentation latency may change by more than half a 100 ms interval between epochs.
        // Count internal gaps on the untouched producer journal, not by rounding the shifted WAV
        // distance. This preserves every factual latency change without hiding a missing frame.
        for pair in latency_mapped_source_times(data)?.windows(2) {
            let delta_ms = pair[1].checked_sub(pair[0])?;
            if delta_ms < TRACE_FRAME_INTERVAL_MS || delta_ms % TRACE_FRAME_INTERVAL_MS != 0 {
                return None;
            }
            missing_slots = missing_slots.checked_add(
                delta_ms
                    .checked_div(TRACE_FRAME_INTERVAL_MS)?
                    .saturating_sub(1),
            )?;
        }
    } else if let Some(diagnostics) = data.trace_diagnostics.as_ref().filter(|diagnostics| {
        diagnostics.measured_frame_count == data.frames.len() as u64
            && diagnostics.expected_frame_count
                == diagnostics
                    .measured_frame_count
                    .saturating_add(diagnostics.missing_slots)
    }) {
        // The journal is deliberately removed after commit. Its already-derived counts remain the
        // signed fact; re-inferring from shifted distances could disagree with the commit decision.
        return Some(LatencyMappedTraceCoverage {
            expected_frame_count: diagnostics.expected_frame_count,
            measured_frame_count: diagnostics.measured_frame_count,
            missing_slots: diagnostics.missing_slots,
        });
    } else {
        for pair in data.trace_slot_positions.windows(2) {
            let delta = pair[1].checked_sub(pair[0])?;
            if delta < half_slot {
                return None;
            }
            // Compatibility for an unsigned in-memory fixture with no producer journal or
            // diagnostics. Committed current artifacts always use one of the factual paths above.
            let cadence_count = delta.checked_add(half_slot)?.checked_div(slot_samples)?;
            missing_slots = missing_slots
                .checked_add(u64::try_from(cadence_count.max(1)).ok()?.saturating_sub(1))?;
        }
    }
    let tail_samples = duration.checked_sub(last)?;
    missing_slots = missing_slots.checked_add(u64::try_from(tail_samples / slot_samples).ok()?)?;
    let measured_frame_count = u64::try_from(data.frames.len()).ok()?;
    Some(LatencyMappedTraceCoverage {
        expected_frame_count: measured_frame_count.checked_add(missing_slots)?,
        measured_frame_count,
        missing_slots,
    })
}

fn trace_bounce_take_failure_reasons(data: &PluginDataFile) -> Vec<&'static str> {
    let Some(take) = data.bounce_take.as_ref() else {
        return Vec::new();
    };
    let mut reasons = Vec::new();
    let Some(duration_ms) = bounce_take_duration_ms(take) else {
        return reasons;
    };
    let mapped_coverage =
        latency_mapped_trace_coverage(data, take.duration_samples, take.sample_rate);
    let expected_frame_count = mapped_coverage
        .map_or(duration_ms / TRACE_FRAME_INTERVAL_MS, |coverage| {
            coverage.expected_frame_count
        });
    if take.wav_start_sample != 0 {
        reasons.push("bounce_take_start_sample_nonzero");
    }
    if take.wav_end_sample != take.duration_samples {
        reasons.push("bounce_take_end_sample_mismatch");
    }
    if take.start_t_ms != 0 {
        reasons.push("bounce_take_start_time_nonzero");
    }
    if take.end_t_ms != duration_ms {
        reasons.push("bounce_take_end_time_mismatch");
    }
    if bounce_take_duration_frames_48k(take)
        .is_some_and(|expected| take.duration_frames_48k != expected)
    {
        reasons.push("bounce_take_48k_duration_mismatch");
    }
    if let Some(diag) = &data.trace_diagnostics {
        if diag.expected_frame_count != expected_frame_count {
            reasons.push("trace_expected_frame_count_bounce_take_mismatch");
        }
        if take.frame_count != diag.measured_frame_count {
            reasons.push("bounce_take_trace_frame_count_mismatch");
        }
    }
    if take.frame_count != data.frames.len() as u64 {
        reasons.push("bounce_take_frame_payload_count_mismatch");
    }
    if take.trace_sample_count < take.frame_count {
        reasons.push("bounce_take_trace_sample_count_mismatch");
    }
    match usize::try_from(expected_frame_count) {
        Ok(expected_len) => {
            if data.frames.len() != expected_len {
                reasons.push("trace_frame_payload_count_bounce_take_mismatch");
            }
            let timeline_mismatch = if mapped_coverage.is_some() {
                data.trace_slot_positions
                    .iter()
                    .zip(&data.frames)
                    .any(|(slot, frame)| {
                        u64::try_from(*slot).ok().is_none_or(|slot| {
                            frame.t_ms != slot.saturating_mul(1_000) / u64::from(take.sample_rate)
                        })
                    })
            } else {
                data.frames
                    .iter()
                    .enumerate()
                    .any(|(idx, frame)| frame.t_ms != (idx as u64 + 1) * TRACE_FRAME_INTERVAL_MS)
            };
            if timeline_mismatch {
                reasons.push("trace_frame_timeline_mismatch");
            }
        }
        Err(_) => reasons.push("trace_expected_frame_count_overflow"),
    }
    dedup_reasons(reasons)
}

fn trace_slots_are_complete(data: &PluginDataFile) -> bool {
    trace_publish_failure_reasons(data).is_empty() && trace_slot_identity_is_complete(data)
}

fn trace_slot_identity_is_complete(data: &PluginDataFile) -> bool {
    if data.sample_rate == 0
        || data.frames.is_empty()
        || data.trace_slot_positions.len() != data.frames.len()
    {
        return false;
    }
    let slot_samples = (data.sample_rate as i64 / 10).max(1);
    match data.trace_time_axis.as_deref() {
        Some(axis) if axis == crate::trace_alignment::TRACE_HOST_TIME_AXIS => {
            if !data
                .trace_slot_positions
                .windows(2)
                .all(|pair| pair[1].checked_sub(pair[0]) == Some(slot_samples))
            {
                return false;
            }
            let (Some(first), Some(last), Some(clock)) = (
                data.trace_slot_positions.first().copied(),
                data.trace_slot_positions.last().copied(),
                data.trace_clock.as_ref(),
            ) else {
                return false;
            };
            clock.basis == crate::trace_alignment::TRACE_CLOCK_BASIS
                && clock.sample_rate == data.sample_rate
                && !clock.sources.is_empty()
                && first.checked_sub(slot_samples) == Some(clock.origin_position_samples)
                && last == clock.end_position_samples
        }
        Some(axis) if axis == crate::trace_alignment::TRACE_TIME_AXIS => {
            if !crate::trace_alignment::has_canonical_wav_reference(data) {
                return false;
            }
            if uses_latency_mapped_wav_grid(data) {
                let Some(reference) = data.trace_wav_reference.as_ref() else {
                    return false;
                };
                let Some(duration_samples) =
                    reference.end_sample.checked_sub(reference.start_sample)
                else {
                    return false;
                };
                return latency_mapped_trace_coverage(data, duration_samples, data.sample_rate)
                    .is_some_and(|coverage| {
                        coverage.missing_slots == 0
                            && coverage.measured_frame_count == coverage.expected_frame_count
                    });
            }
            data.trace_slot_positions
                .windows(2)
                .all(|pair| pair[1].checked_sub(pair[0]) == Some(slot_samples))
                && data.trace_slot_positions.first().copied() == Some(slot_samples)
                && i64::try_from(data.trace_slot_positions.len())
                    .ok()
                    .and_then(|count| count.checked_mul(slot_samples))
                    == data.trace_slot_positions.last().copied()
        }
        _ => false,
    }
}

fn trace_publish_failure_reasons(data: &PluginDataFile) -> Vec<&'static str> {
    let mut reasons = Vec::new();
    match &data.trace_diagnostics {
        Some(diag) => {
            if diag.expected_frame_count == 0 {
                reasons.push("zero_expected_trace_frames");
            }
            if diag.missing_slots > 0 {
                reasons.push("missing_trace_slots");
            }
            if diag.measured_frame_count != diag.expected_frame_count {
                reasons.push("trace_frame_count_mismatch");
            }
            if diag.measured_frame_count as usize != data.frames.len() {
                reasons.push("trace_frame_payload_count_mismatch");
            }
        }
        None => reasons.push("missing_trace_diagnostics"),
    }
    reasons.extend(trace_bounce_take_failure_reasons(data));
    dedup_reasons(reasons)
}

fn side_measurement_failure_reasons(data: &PluginDataFile) -> Vec<&'static str> {
    let mut reasons = Vec::new();
    match data.bounce_take.as_ref() {
        Some(take) => {
            if take.alignment_status != BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY {
                reasons.push("bounce_take_not_sample_count_ready");
            }
            if !bounce_source_is_sample_count_ready(&take.source) {
                reasons.push("bounce_take_source_not_sample_count_ready");
            }
            if data.sample_rate == 0 || take.sample_rate != data.sample_rate {
                reasons.push("bounce_take_sample_rate_mismatch");
            }
            if take.duration_samples == 0 || take.wav_end_sample < take.wav_start_sample {
                reasons.push("bounce_take_duration_invalid");
            }
            if take.frame_count == 0 {
                reasons.push("bounce_take_zero_frames");
            }
        }
        None => reasons.push("missing_bounce_take"),
    }
    reasons.extend(trace_publish_failure_reasons(data));
    if !bounce_take_sample_count_ready(data) {
        reasons.push("bounce_take_not_sample_count_ready");
    }
    if !trace_slots_are_complete(data) {
        reasons.push("trace_slots_not_complete");
    }
    dedup_reasons(reasons)
}

pub(crate) fn normal_publish_failure_reasons(data: &PluginDataFile) -> Vec<&'static str> {
    let mut reasons = side_publish_failure_reasons(data);
    reasons.extend(side_measurement_failure_reasons(data));
    reasons.extend(side_publish_integrity_reasons(data));
    dedup_reasons(reasons)
}

fn side_publish_integrity_reasons(data: &PluginDataFile) -> Vec<&'static str> {
    let mut reasons = Vec::new();
    let sample_count_ready = bounce_take_sample_count_ready(data);
    let expected = match &data.expected_wav {
        Some(expected) if expected.is_usable() => Some(expected),
        _ => {
            if !sample_count_ready {
                reasons.push("missing_expected_wav_metadata");
            }
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
    if expected.is_none() && take.is_some_and(|take| take.source == BOUNCE_SOURCE_EXPECTED_WAV) {
        reasons.push("missing_expected_wav_metadata");
    }
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
        if take.alignment_status != BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY {
            reasons.push("bounce_take_not_sample_count_ready");
        }
        if !bounce_source_is_sample_count_ready(&take.source) {
            reasons.push("bounce_take_source_not_sample_count_ready");
        }
    } else if let Some(take) = take {
        if take.alignment_status != BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY {
            reasons.push("bounce_take_not_sample_count_ready");
        }
        if !bounce_source_is_sample_count_ready(&take.source) {
            reasons.push("bounce_take_source_not_sample_count_ready");
        }
        if data.sample_rate == 0 || take.sample_rate != data.sample_rate {
            reasons.push("bounce_take_sample_rate_mismatch");
        }
    }
    if let Some(diag) = &data.trace_diagnostics {
        if diag.expected_frame_count == 0 {
            reasons.push("zero_expected_trace_frames");
        }
        if diag.missing_slots > 0 {
            reasons.push("missing_trace_slots");
        }
        if diag.measured_frame_count != diag.expected_frame_count {
            reasons.push("trace_frame_count_mismatch");
        }
        if diag.measured_frame_count as usize != data.frames.len() {
            reasons.push("trace_frame_payload_count_mismatch");
        }
    } else {
        reasons.push("missing_trace_diagnostics");
    }
    reasons.extend(trace_bounce_take_failure_reasons(data));
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
    let sample_count_ready = bounce_take_sample_count_ready(data);
    let (expected_frame_count, measured_frame_count, missing_trace_slots, trace_slots_complete) =
        match &data.trace_diagnostics {
            Some(diag) => (
                diag.expected_frame_count,
                diag.measured_frame_count,
                diag.missing_slots,
                trace_slots_are_complete(data),
            ),
            None => (0, data.frames.len() as u64, 0, false),
        };
    let usable = data.commit_status.as_deref() != Some("failed")
        && side_visibility_failure_reasons(data).is_empty();
    let complete = usable && normal_publish_failure_reasons(data).is_empty();
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
    let embedded_expected = match (&self_data.expected_wav, &peer_data.expected_wav) {
        (Some(left), Some(right)) if left == right && left.is_usable() => Some(left.clone()),
        _ => None,
    };
    let plugin_data_root = plugin_data_root_from_final_path(&paths.final_path);
    let late_expected = plugin_data_root.as_ref().and_then(|root| {
        let project = crate::path_identity::guard_path_component(
            &self_data.project_hash,
            "pair_record_session.expected.project_hash",
        );
        let project_dir = root.join(project.as_ref());
        read_late_expected_metadata_from_project_dir(&project_dir).filter(|expected| {
            pair_has_exact_expected_claim(root, &self_data, &peer_data, expected)
        })
    });
    let pair_expected = embedded_expected.or(late_expected);
    let needs_wav_binding = !crate::trace_alignment::has_canonical_wav_reference(&self_data)
        || !crate::trace_alignment::has_canonical_wav_reference(&peer_data)
        || self_data.trace_pair_wav_start_basis.is_none()
        || peer_data.trace_pair_wav_start_basis.is_none();
    if needs_wav_binding {
        if let Some(expected) = pair_expected.as_ref() {
            let mut normalized_self = self_data.clone();
            let mut normalized_peer = peer_data.clone();
            if normalize_late_expected_pair(&mut normalized_self, &mut normalized_peer, expected) {
                self_data = normalized_self;
                peer_data = normalized_peer;
            }
        }
    }
    synchronize_pair_annotations(&mut self_data, &mut peer_data);
    let content_alignment = crate::trace_alignment::canonical_wav_alignment(&self_data, &peer_data);
    let reasons = pair_publish_failure_reasons(&self_data, &peer_data);
    let visibility_reasons = pair_visibility_failure_reasons(&self_data, &peer_data);
    let timing_exact = content_alignment.is_some() && reasons.is_empty();
    let measured_wav_fallback = !timing_exact
        && visibility_reasons.is_empty()
        && self_data.trace_time_axis.as_deref() == Some(crate::trace_alignment::TRACE_TIME_AXIS)
        && peer_data.trace_time_axis.as_deref() == Some(crate::trace_alignment::TRACE_TIME_AXIS);
    if measured_wav_fallback && prepare_late_expected_pair(&mut self_data, &mut peer_data) {
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
        log::info!(
            "[pair_record_session] published measured fallback session={}",
            self_data.record_session_id.as_deref().unwrap_or("")
        );
        return Ok(());
    }
    if !timing_exact {
        let expected_metadata_was_lost = reasons.contains(&"missing_expected_wav_metadata")
            && [&self_data, &peer_data].iter().any(|data| {
                data.bounce_take
                    .as_ref()
                    .is_some_and(|take| take.source == BOUNCE_SOURCE_EXPECTED_WAV)
            });
        let wav_bound_nonempty_pair_is_invalid = !self_data.frames.is_empty()
            && !peer_data.frames.is_empty()
            && self_data.trace_time_axis.as_deref()
                == Some(crate::trace_alignment::TRACE_TIME_AXIS)
            && peer_data.trace_time_axis.as_deref()
                == Some(crate::trace_alignment::TRACE_TIME_AXIS)
            && !reasons.is_empty();
        if !expected_metadata_was_lost && !wav_bound_nonempty_pair_is_invalid {
            return Ok(());
        }
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
        return Ok(());
    }
    let content_alignment = content_alignment.expect("checked exact WAV alignment");
    self_data.trace_content_alignment = Some(content_alignment.clone());
    peer_data.trace_content_alignment = Some(content_alignment);
    self_data.trace_pair_offset_samples = None;
    peer_data.trace_pair_offset_samples = None;
    self_data.trace_pair_alignment_score = None;
    peer_data.trace_pair_alignment_score = None;
    self_data.trace_context_frames.clear();
    peer_data.trace_context_frames.clear();
    self_data.trace_context_slot_positions.clear();
    peer_data.trace_context_slot_positions.clear();
    self_data.trace_pair_wav_start_basis = None;
    peer_data.trace_pair_wav_start_basis = None;
    self_data.trace_context_psb_snapshots.clear();
    peer_data.trace_context_psb_snapshots.clear();
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
    let (self_trace_path, peer_trace_path) =
        pair_trace_shelf_paths(self_paths, self_data, peer_paths, peer_data)?;
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

#[cfg(test)]
fn pair_trace_shelf_path(paths: &PairPaths, data: &PluginDataFile) -> Result<PathBuf, WriterError> {
    pair_trace_shelf_path_from_member_path(&paths.member_path, data)
}

fn pair_trace_shelf_path_from_member_path(
    member_path: &Path,
    data: &PluginDataFile,
) -> Result<PathBuf, WriterError> {
    let role_dir = pair_role_dir_from_member_path(member_path)?;
    trace_shelf_path_for_role_dir(role_dir, data)
}

fn pair_trace_shelf_paths(
    self_paths: &PairPaths,
    self_data: &PluginDataFile,
    peer_paths: &PairPaths,
    peer_data: &PluginDataFile,
) -> Result<(PathBuf, PathBuf), WriterError> {
    let self_role_dir = pair_role_dir_from_member_path(&self_paths.member_path)?;
    let peer_role_dir = pair_role_dir_from_member_path(&peer_paths.member_path)?;
    for offset_secs in 0..=TRACE_SHELF_COLLISION_SEARCH_SECS {
        let self_path = trace_shelf_path_for_offset(self_role_dir, self_data, offset_secs)?;
        let peer_path = trace_shelf_path_for_offset(peer_role_dir, peer_data, offset_secs)?;
        if trace_shelf_slot_is_writable(&self_path, self_data)
            && trace_shelf_slot_is_writable(&peer_path, peer_data)
        {
            return Ok((self_path, peer_path));
        }
    }
    Err(io::Error::new(
        io::ErrorKind::AlreadyExists,
        "TRACE shelf collision window exhausted",
    )
    .into())
}

fn pair_role_dir_from_member_path(member_path: &Path) -> Result<&Path, WriterError> {
    member_path
        .parent()
        .and_then(Path::parent)
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "role dir missing").into())
}

fn trace_shelf_path_for_role_dir(
    role_dir: &Path,
    data: &PluginDataFile,
) -> Result<PathBuf, WriterError> {
    let mut first_vacant = None;
    for offset_secs in 0..=TRACE_SHELF_COLLISION_SEARCH_SECS {
        let path = trace_shelf_path_for_offset(role_dir, data, offset_secs)?;
        match trace_shelf_slot_state(&path, data) {
            TraceShelfSlotState::SameRecord => return Ok(path),
            TraceShelfSlotState::Vacant => {
                if first_vacant.is_none() {
                    first_vacant = Some(path);
                }
            }
            TraceShelfSlotState::Occupied => {}
        }
    }
    first_vacant.ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::AlreadyExists,
            "TRACE shelf collision window exhausted",
        )
        .into()
    })
}

fn trace_shelf_path_for_offset(
    role_dir: &Path,
    data: &PluginDataFile,
    offset_secs: i64,
) -> Result<PathBuf, WriterError> {
    let stamp = trace_shelf_compact_at(data, offset_secs)?;
    Ok(role_dir.join(format!("{stamp}.json")))
}

fn trace_shelf_compact_at(data: &PluginDataFile, offset_secs: i64) -> Result<String, WriterError> {
    let compact = compact_wall_clock(&data.timestamp);
    if compact.is_empty() {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "timestamp missing").into());
    }
    if offset_secs == 0 {
        return Ok(compact);
    }
    let naive = chrono::NaiveDateTime::parse_from_str(&compact, "%Y%m%dT%H%M%S").map_err(|e| {
        io::Error::new(
            io::ErrorKind::InvalidData,
            format!("compact timestamp parse failed: {e}"),
        )
    })?;
    Ok((naive + chrono::Duration::seconds(offset_secs))
        .format("%Y%m%dT%H%M%S")
        .to_string())
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum TraceShelfSlotState {
    Vacant,
    SameRecord,
    Occupied,
}

fn trace_shelf_slot_is_writable(path: &Path, data: &PluginDataFile) -> bool {
    matches!(
        trace_shelf_slot_state(path, data),
        TraceShelfSlotState::Vacant | TraceShelfSlotState::SameRecord
    )
}

fn trace_shelf_slot_state(path: &Path, data: &PluginDataFile) -> TraceShelfSlotState {
    if !path.exists() {
        return TraceShelfSlotState::Vacant;
    }
    let Ok(existing) = read_plugin_data_file(path) else {
        return TraceShelfSlotState::Occupied;
    };
    if verify_checksum(&existing) && trace_shelf_same_record(&existing, data) {
        TraceShelfSlotState::SameRecord
    } else {
        TraceShelfSlotState::Occupied
    }
}

fn trace_shelf_same_record(existing: &PluginDataFile, data: &PluginDataFile) -> bool {
    let Some(existing_session_id) = existing.record_session_id.as_deref().map(str::trim) else {
        return false;
    };
    let Some(session_id) = data.record_session_id.as_deref().map(str::trim) else {
        return false;
    };
    !session_id.is_empty()
        && existing_session_id == session_id
        && existing.project_hash == data.project_hash
        && existing.instance_id == data.instance_id
        && existing.role == data.role
        && (existing.capture_generation_id.is_none()
            || data.capture_generation_id.is_none()
            || existing.capture_generation_id == data.capture_generation_id)
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
    #[serde(default)]
    capture_generation_id: String,
    #[serde(default)]
    generation_started_at_ms: i64,
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

/// Kirin OS が Drop 後に `record_expected/current.json` を書いた場合でも、既に閉じた
/// Record を WAV の 0..duration sample 境界へ収束させる。
///
/// Hypha close 時点で expected WAV がまだ無い場合、pair は一度 `render_clock_native`
/// の暫定成果物として閉じる。そのままだと PRE/POST の微小な block 差で `.failed`
/// に落ちたり、TRACE が fallback warning 付きで表示されたりする。ここでは閉じた
/// artifact だけを対象に、WAV header truth が到着した後で同じ session を正本境界へ
/// 正規化する。計測中の writer / Audio Thread には触れない。
pub fn reconcile_late_expected_wav(plugin_data_root: &Path) -> usize {
    let mut reconciled = 0_usize;
    if !plugin_data_root.is_dir() {
        return reconciled;
    }
    let entries = match fs::read_dir(plugin_data_root) {
        Ok(entries) => entries,
        Err(_) => return reconciled,
    };
    for entry in entries.flatten() {
        let Ok(ftype) = entry.file_type() else {
            continue;
        };
        if !ftype.is_dir() {
            continue;
        }
        let project_dir = entry.path();
        reconciled = reconciled.saturating_add(reconcile_late_expected_wav_project_dir(
            plugin_data_root,
            &project_dir,
        ));
    }
    reconciled
}

/// IO Thread の定常 tick 用。現在プロジェクトだけを対象にし、複数 Hypha インスタンスが
/// plugin_data 全体を 1 秒ごとに再帰走査する状態を避ける。
pub fn reconcile_late_expected_wav_project(plugin_data_root: &Path, project_hash: &str) -> usize {
    if !plugin_data_root.is_dir() {
        return 0;
    }
    let project_dir_name = late_expected_project_dir_name(project_hash);
    let project_dir = plugin_data_root.join(project_dir_name.as_ref());
    reconcile_late_expected_wav_project_dir(plugin_data_root, &project_dir)
}

fn late_expected_project_dir_name(project_hash: &str) -> std::borrow::Cow<'_, str> {
    if crate::path_identity::is_path_safe_component(project_hash)
        || late_expected_is_materialized_quarantine_dir(project_hash)
    {
        std::borrow::Cow::Borrowed(project_hash)
    } else {
        crate::path_identity::guard_path_component(
            project_hash,
            "record_expected.late_reconcile.project_hash",
        )
    }
}

fn late_expected_is_materialized_quarantine_dir(component: &str) -> bool {
    let Some(hex) = component.strip_prefix(crate::path_identity::QUARANTINE_PREFIX) else {
        return false;
    };
    hex.len() == 16 && hex.chars().all(|c| c.is_ascii_hexdigit())
}

fn reconcile_late_expected_wav_project_dir(plugin_data_root: &Path, project_dir: &Path) -> usize {
    let drop_reconciled = reconcile_drop_committed_closed_project(plugin_data_root, project_dir);
    let Some(expected) = read_late_expected_metadata_from_project_dir(project_dir) else {
        return drop_reconciled;
    };
    drop_reconciled
        .saturating_add(finalize_pair_pending_sessions(project_dir))
        .saturating_add(reconcile_late_expected_committed_project(
            plugin_data_root,
            project_dir,
            &expected,
        ))
        .saturating_add(reconcile_late_expected_closed_project(
            plugin_data_root,
            project_dir,
            &expected,
        ))
}

/// Complete an exact Kirin OS Drop even when the user stopped Record before dropping the WAV.
///
/// The live IO path binds commits while Record is open. This recovery path starts from the two
/// closed producer artifacts instead: it proves their cross-links and sample window first, then
/// re-reads and binds only the transaction that names that same session. A project-wide
/// `current.json` is never used to choose the session.
fn reconcile_drop_committed_closed_project(plugin_data_root: &Path, project_dir: &Path) -> usize {
    let mut candidates: HashMap<String, LateExpectedClosedCandidate> = HashMap::new();
    collect_late_expected_closed_candidates(project_dir, &mut candidates);
    let mut reconciled = 0_usize;
    for (session_id, candidate) in candidates {
        let (Some((pre_path, mut pre_data)), Some((post_path, mut post_data))) =
            (candidate.pre, candidate.post)
        else {
            continue;
        };
        if !closed_pair_can_accept_exact_drop(&pre_data, &post_data, &session_id)
            || !late_expected_publish_paths_are_inside_root(plugin_data_root, &pre_path, &post_path)
        {
            continue;
        }
        let expected = match crate::record_drop_commit::inspect_drop_commit_for_closed_pair_session(
            plugin_data_root,
            &pre_data.project_hash,
            &session_id,
        ) {
            Ok(Some(expected)) => expected,
            Ok(None) => continue,
            Err(error) => {
                log::warn!(
                    "[record_drop_commit] closed-pair inspect skipped: session={} err={}",
                    session_id,
                    error
                );
                continue;
            }
        };
        if !normalize_late_expected_pair(&mut pre_data, &mut post_data, &expected)
            || !prepare_late_expected_pair(&mut pre_data, &mut post_data)
        {
            continue;
        }
        match crate::record_drop_commit::bind_drop_commit_for_closed_pair_session(
            plugin_data_root,
            &pre_data.project_hash,
            &session_id,
            &expected,
        ) {
            Ok(Some(bound)) if bound == expected => {}
            Ok(_) => continue,
            Err(error) => {
                log::warn!(
                    "[record_drop_commit] closed-pair bind skipped: session={} err={}",
                    session_id,
                    error
                );
                continue;
            }
        }
        match publish_prepared_late_expected_pair(
            plugin_data_root,
            &pre_path,
            &mut pre_data,
            &post_path,
            &mut post_data,
        ) {
            Ok(true) => {
                let _ = fs::remove_file(&pre_path);
                let _ = fs::remove_file(&post_path);
                reconciled = reconciled.saturating_add(2);
                log::info!(
                    "[record_drop_commit] promoted exact stopped pair: session={} root={}",
                    session_id,
                    plugin_data_root.display()
                );
            }
            Ok(false) => {}
            Err(error) => log::warn!(
                "[record_drop_commit] stopped-pair publish skipped: session={} err={}",
                session_id,
                error
            ),
        }
    }
    reconciled
}

/// Reconcile one stopped pair named by the producer transaction. This is the normal post-Stop
/// Drop path: callers already know project/session/PRE/POST identities, so only four deterministic
/// candidate files are considered. Recursive project recovery remains startup-only.
pub fn reconcile_drop_committed_closed_session(
    plugin_data_root: &Path,
    project_hash: &str,
    session_id: &str,
    pre_instance_id: &str,
    post_instance_id: &str,
) -> usize {
    if [project_hash, session_id, pre_instance_id, post_instance_id]
        .iter()
        .any(|value| value.trim().is_empty())
    {
        return 0;
    }
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "record_drop_reconcile.project_hash",
    );
    let session =
        crate::path_identity::guard_path_component(session_id, "record_drop_reconcile.session_id");
    let pre_iid = crate::path_identity::guard_path_component(
        pre_instance_id,
        "record_drop_reconcile.pre_instance_id",
    );
    let post_iid = crate::path_identity::guard_path_component(
        post_instance_id,
        "record_drop_reconcile.post_instance_id",
    );
    let expected = match crate::record_drop_commit::inspect_drop_commit_for_closed_pair_session(
        plugin_data_root,
        project_hash,
        session_id,
    ) {
        Ok(Some(expected)) => expected,
        _ => return 0,
    };
    let file_name = format!("{session}.json");
    let candidate = |instance_id: &str, role: Role| {
        let role_dir = plugin_data_root
            .join(&*ph)
            .join(instance_id)
            .join(role.dir_name());
        [
            role_dir.join(".failed").join(&file_name),
            role_dir.join(".pair_pending").join(&file_name),
        ]
        .into_iter()
        .find_map(|path| read_plugin_data_file(&path).ok().map(|data| (path, data)))
    };
    let (Some((pre_path, mut pre_data)), Some((post_path, mut post_data))) = (
        candidate(&pre_iid, Role::Pre),
        candidate(&post_iid, Role::Post),
    ) else {
        return 0;
    };
    if !closed_pair_can_accept_exact_drop(&pre_data, &post_data, session_id)
        || !late_expected_publish_paths_are_inside_root(plugin_data_root, &pre_path, &post_path)
    {
        return 0;
    }
    if !normalize_late_expected_pair(&mut pre_data, &mut post_data, &expected)
        || !prepare_late_expected_pair(&mut pre_data, &mut post_data)
    {
        return 0;
    }
    match crate::record_drop_commit::bind_drop_commit_for_closed_pair_session(
        plugin_data_root,
        project_hash,
        session_id,
        &expected,
    ) {
        Ok(Some(bound)) if bound == expected => {}
        _ => return 0,
    }
    match publish_prepared_late_expected_pair(
        plugin_data_root,
        &pre_path,
        &mut pre_data,
        &post_path,
        &mut post_data,
    ) {
        Ok(true) => {
            let _ = fs::remove_file(pre_path);
            let _ = fs::remove_file(post_path);
            2
        }
        _ => 0,
    }
}

pub fn pair_record_session_manifest_exists(
    plugin_data_root: &Path,
    project_hash: &str,
    session_id: &str,
) -> bool {
    if project_hash.trim().is_empty() || session_id.trim().is_empty() {
        return false;
    }
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "pair_record_session.exists.project_hash",
    );
    let session = crate::path_identity::guard_path_component(
        session_id,
        "pair_record_session.exists.session_id",
    );
    plugin_data_root
        .join(&*ph)
        .join(PAIR_RECORD_SESSIONS_DIR)
        .join(format!("{session}.json"))
        .is_file()
}

fn closed_pair_can_accept_exact_drop(
    pre: &PluginDataFile,
    post: &PluginDataFile,
    session_id: &str,
) -> bool {
    pre.role == Role::Pre
        && post.role == Role::Post
        && pre.status == Status::Closed
        && post.status == Status::Closed
        && pre.project_hash == post.project_hash
        && pre.record_session_id.as_deref() == Some(session_id)
        && post.record_session_id.as_deref() == Some(session_id)
        && pre.paired_post_instance_id.as_deref() == Some(post.instance_id.as_str())
        && post.paired_pre_instance_id.as_deref() == Some(pre.instance_id.as_str())
        && matches!(
            pre.commit_status.as_deref(),
            Some("pair_pending" | "failed")
        )
        && matches!(
            post.commit_status.as_deref(),
            Some("pair_pending" | "failed")
        )
}

fn late_expected_publish_paths_are_inside_root(
    plugin_data_root: &Path,
    left_path: &Path,
    right_path: &Path,
) -> bool {
    [left_path, right_path].into_iter().all(|path| {
        let Some(paths) = pair_paths_from_closed_artifact_path(path) else {
            return false;
        };
        path.starts_with(plugin_data_root)
            && paths.final_path.starts_with(plugin_data_root)
            && paths.member_path.starts_with(plugin_data_root)
    })
}

fn read_late_expected_metadata_from_project_dir(project_dir: &Path) -> Option<ExpectedWavMetadata> {
    let path = project_dir
        .join(crate::record_expected::EXPECTED_SUBDIR)
        .join(crate::record_expected::EXPECTED_FILENAME);
    let bytes = fs::read(path).ok()?;
    let mut metadata: ExpectedWavMetadata = serde_json::from_slice(&bytes).ok()?;
    metadata.consumed_at_ms = None;
    metadata.consumed_by_session_id = None;
    if !metadata.is_complete() {
        return None;
    }
    let now_ms = chrono::Utc::now().timestamp_millis();
    if metadata.created_at_ms > now_ms.saturating_add(LATE_EXPECTED_METADATA_FUTURE_SKEW_MS) {
        return None;
    }
    if now_ms.saturating_sub(metadata.created_at_ms)
        > crate::record_expected::EXPECTED_METADATA_MAX_AGE_MS
    {
        return None;
    }
    Some(metadata)
}

fn reconcile_late_expected_committed_project(
    plugin_data_root: &Path,
    project_dir: &Path,
    expected: &ExpectedWavMetadata,
) -> usize {
    let sessions_dir = project_dir.join(PAIR_RECORD_SESSIONS_DIR);
    let entries = match fs::read_dir(sessions_dir) {
        Ok(entries) => entries,
        Err(_) => return 0,
    };
    let mut reconciled = 0_usize;
    for entry in entries.flatten() {
        let Ok(ftype) = entry.file_type() else {
            continue;
        };
        let path = entry.path();
        if !ftype.is_file() || path.extension().is_none_or(|ext| ext != "json") {
            continue;
        }
        match reconcile_late_expected_committed_manifest(plugin_data_root, &path, expected) {
            Ok(true) => reconciled = reconciled.saturating_add(2),
            Ok(false) => {}
            Err(e) => log::warn!(
                "[record_expected] late committed reconcile skipped: manifest={} err={}",
                path.display(),
                e
            ),
        }
    }
    reconciled
}

fn reconcile_late_expected_committed_manifest(
    plugin_data_root: &Path,
    manifest_path: &Path,
    expected: &ExpectedWavMetadata,
) -> Result<bool, WriterError> {
    let bytes = fs::read(manifest_path)?;
    let manifest: PairCommitManifest = serde_json::from_slice(&bytes)?;
    if manifest.schema_version != PAIR_RECORD_SESSION_SCHEMA {
        return Ok(false);
    }
    let pre_member = PathBuf::from(&manifest.pre.path);
    let post_member = PathBuf::from(&manifest.post.path);
    if !pre_member.starts_with(plugin_data_root) || !post_member.starts_with(plugin_data_root) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "pair commit member path escapes plugin_data base",
        )
        .into());
    }
    let mut pre_data = read_plugin_data_file(&pre_member)?;
    let mut post_data = read_plugin_data_file(&post_member)?;
    if !pair_has_exact_expected_claim(plugin_data_root, &pre_data, &post_data, expected) {
        return Ok(false);
    }
    if late_expected_record_is_aligned(&pre_data, expected)
        && late_expected_record_is_aligned(&post_data, expected)
    {
        return Ok(false);
    }
    if !normalize_late_expected_pair(&mut pre_data, &mut post_data, expected) {
        return Ok(false);
    }
    publish_late_expected_pair(
        plugin_data_root,
        &pre_member,
        &mut pre_data,
        &post_member,
        &mut post_data,
    )
}

#[derive(Default)]
struct LateExpectedClosedCandidate {
    pre: Option<(PathBuf, PluginDataFile)>,
    post: Option<(PathBuf, PluginDataFile)>,
}

fn reconcile_late_expected_closed_project(
    plugin_data_root: &Path,
    project_dir: &Path,
    expected: &ExpectedWavMetadata,
) -> usize {
    let mut candidates: HashMap<String, LateExpectedClosedCandidate> = HashMap::new();
    collect_late_expected_closed_candidates(project_dir, &mut candidates);
    let mut reconciled = 0_usize;
    for (session_id, candidate) in candidates {
        let (Some((pre_path, mut pre_data)), Some((post_path, mut post_data))) =
            (candidate.pre, candidate.post)
        else {
            continue;
        };
        if !pair_has_exact_expected_claim(plugin_data_root, &pre_data, &post_data, expected) {
            continue;
        }
        if !normalize_late_expected_pair(&mut pre_data, &mut post_data, expected) {
            continue;
        }
        match publish_late_expected_pair(
            plugin_data_root,
            &pre_path,
            &mut pre_data,
            &post_path,
            &mut post_data,
        ) {
            Ok(true) => {
                let _ = fs::remove_file(&pre_path);
                let _ = fs::remove_file(&post_path);
                reconciled = reconciled.saturating_add(2);
                log::info!(
                    "[record_expected] promoted late expected closed pair: session={} root={}",
                    session_id,
                    plugin_data_root.display()
                );
            }
            Ok(false) => {}
            Err(e) => log::warn!(
                "[record_expected] late closed reconcile skipped: session={} err={}",
                session_id,
                e
            ),
        }
    }
    reconciled
}

fn pair_has_exact_expected_claim(
    plugin_data_root: &Path,
    left: &PluginDataFile,
    right: &PluginDataFile,
    expected: &ExpectedWavMetadata,
) -> bool {
    let Some(left_session) = left.record_session_id.as_deref() else {
        return false;
    };
    let Some(right_session) = right.record_session_id.as_deref() else {
        return false;
    };
    if left_session.is_empty()
        || left_session != right_session
        || left.project_hash != right.project_hash
    {
        return false;
    }
    if left.expected_wav.as_ref() == Some(expected) && right.expected_wav.as_ref() == Some(expected)
    {
        return true;
    }
    crate::record_expected::read_claim_marker_for_session(
        plugin_data_root,
        &left.project_hash,
        left_session,
    )
    .ok()
    .flatten()
    .is_some_and(|marker| marker.metadata.as_ref() == Some(expected))
}

fn collect_late_expected_closed_candidates(
    dir: &Path,
    candidates: &mut HashMap<String, LateExpectedClosedCandidate>,
) {
    let entries = match fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let Ok(ftype) = entry.file_type() else {
            continue;
        };
        if ftype.is_dir() {
            collect_late_expected_closed_candidates(&path, candidates);
            continue;
        }
        if !ftype.is_file()
            || path.extension().is_none_or(|ext| ext != "json")
            || !matches!(
                path.parent()
                    .and_then(Path::file_name)
                    .and_then(|name| name.to_str()),
                Some(".failed" | ".pair_pending")
            )
        {
            continue;
        }
        let Ok(data) = read_plugin_data_file(&path) else {
            continue;
        };
        let Some(session_id) = data
            .record_session_id
            .as_deref()
            .map(str::trim)
            .filter(|session| !session.is_empty())
            .map(str::to_string)
        else {
            continue;
        };
        let entry = candidates.entry(session_id).or_default();
        let slot = match data.role {
            Role::Pre => &mut entry.pre,
            Role::Post => &mut entry.post,
        };
        let should_replace = slot.as_ref().is_none_or(|(existing_path, _)| {
            let new_precedence = closed_candidate_precedence(&path);
            let existing_precedence = closed_candidate_precedence(existing_path);
            new_precedence > existing_precedence
                || (new_precedence == existing_precedence
                    && path.as_path() < existing_path.as_path())
        });
        if should_replace {
            *slot = Some((path, data));
        }
    }
}

/// A `.failed` artifact is written from the fully evaluated pair and only then replaces its
/// `.pair_pending` source. A crash between those two operations can leave both files behind, so
/// directory iteration order must not decide which generation is reconciled.
fn closed_candidate_precedence(path: &Path) -> u8 {
    match path
        .parent()
        .and_then(Path::file_name)
        .and_then(|name| name.to_str())
    {
        Some(".failed") => 1,
        _ => 0,
    }
}

fn publish_late_expected_pair(
    plugin_data_root: &Path,
    left_path: &Path,
    left_data: &mut PluginDataFile,
    right_path: &Path,
    right_data: &mut PluginDataFile,
) -> Result<bool, WriterError> {
    if !prepare_late_expected_pair(left_data, right_data) {
        return Ok(false);
    }
    publish_prepared_late_expected_pair(
        plugin_data_root,
        left_path,
        left_data,
        right_path,
        right_data,
    )
}

fn prepare_late_expected_pair(
    left_data: &mut PluginDataFile,
    right_data: &mut PluginDataFile,
) -> bool {
    let (pre_data, post_data) = match (left_data.role, right_data.role) {
        (Role::Pre, Role::Post) => (left_data, right_data),
        (Role::Post, Role::Pre) => (right_data, left_data),
        _ => return false,
    };
    synchronize_pair_annotations(pre_data, post_data);
    let content_alignment = crate::trace_alignment::canonical_wav_alignment(pre_data, post_data);
    let timing_reasons = pair_publish_failure_reasons(pre_data, post_data);
    let alignment_exact = content_alignment.is_some();
    pre_data.trace_content_alignment = alignment_exact.then(|| {
        content_alignment
            .clone()
            .expect("exact timing has alignment")
    });
    post_data.trace_content_alignment =
        alignment_exact.then(|| content_alignment.expect("exact timing has alignment"));
    pre_data.trace_pair_offset_samples = None;
    post_data.trace_pair_offset_samples = None;
    pre_data.trace_pair_alignment_score = None;
    post_data.trace_pair_alignment_score = None;
    pre_data.trace_context_frames.clear();
    post_data.trace_context_frames.clear();
    pre_data.trace_context_slot_positions.clear();
    post_data.trace_context_slot_positions.clear();
    pre_data.trace_pair_wav_start_basis = None;
    post_data.trace_pair_wav_start_basis = None;
    pre_data.trace_context_psb_snapshots.clear();
    post_data.trace_context_psb_snapshots.clear();
    let publication_reasons = pair_visibility_failure_reasons(pre_data, post_data);
    if !publication_reasons.is_empty() {
        return false;
    }
    if !timing_reasons.is_empty() {
        add_integrity_reasons(pre_data, &timing_reasons);
        add_integrity_reasons(post_data, &timing_reasons);
    }
    pre_data.trace_clock_observations.clear();
    post_data.trace_clock_observations.clear();
    pre_data.commit_status = Some("committed".to_string());
    post_data.commit_status = Some("committed".to_string());
    refresh_record_quality(pre_data);
    refresh_record_quality(post_data);
    true
}

fn publish_prepared_late_expected_pair(
    plugin_data_root: &Path,
    left_path: &Path,
    left_data: &mut PluginDataFile,
    right_path: &Path,
    right_data: &mut PluginDataFile,
) -> Result<bool, WriterError> {
    let Some(left_paths) = pair_paths_from_closed_artifact_path(left_path) else {
        return Ok(false);
    };
    let Some(right_paths) = pair_paths_from_closed_artifact_path(right_path) else {
        return Ok(false);
    };
    let paths = writer_paths_from_pair_paths(&left_paths);
    let (pre_paths, pre_data, post_paths, post_data) = match (left_data.role, right_data.role) {
        (Role::Pre, Role::Post) => (&left_paths, left_data, &right_paths, right_data),
        (Role::Post, Role::Pre) => (&right_paths, right_data, &left_paths, left_data),
        _ => return Ok(false),
    };
    if !paths.final_path.starts_with(plugin_data_root)
        || !pre_paths.member_path.starts_with(plugin_data_root)
        || !post_paths.member_path.starts_with(plugin_data_root)
    {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "late expected publish path escapes plugin_data base",
        )
        .into());
    }
    publish_pair_committed_files(&paths, pre_paths, pre_data, post_paths, post_data)?;
    mark_pair_expected_metadata_consumed(&paths, pre_data);
    Ok(true)
}

fn normalize_late_expected_pair(
    left: &mut PluginDataFile,
    right: &mut PluginDataFile,
    expected: &ExpectedWavMetadata,
) -> bool {
    if left.sample_rate == 0
        || left.sample_rate != right.sample_rate
        || left.sample_rate != expected.expected_sample_rate
    {
        return false;
    }
    let Some(duration_ms) = expected_wav_duration_ms(expected) else {
        return false;
    };
    let Ok(expected_len) = usize::try_from(duration_ms / TRACE_FRAME_INTERVAL_MS) else {
        return false;
    };
    if expected_len == 0 {
        return false;
    }
    let slot_samples = (left.sample_rate as i64 / 10).max(1);
    let plan = match (left.role, right.role) {
        (Role::Pre, Role::Post) => crate::trace_content_clock::build_wav_start_clock_plan(
            left,
            right,
            expected,
            expected_len,
            slot_samples,
        ),
        (Role::Post, Role::Pre) => crate::trace_content_clock::build_wav_start_clock_plan(
            right,
            left,
            expected,
            expected_len,
            slot_samples,
        ),
        _ => return false,
    };
    let Some(plan) = plan else {
        return false;
    };
    let rebuild = |data: &mut PluginDataFile,
                   frames: Vec<Frame>,
                   producer_slots: &[i64],
                   public_wav_slots: &[i64],
                   comparison_slots: &[i64],
                   raw_host_slots: &[i64],
                   comparison_resolution: Option<&str>,
                   producer_origin: Option<i64>,
                   clock_model: &str| {
        data.frames = frames;
        debug_assert_eq!(data.frames.len(), producer_slots.len());
        debug_assert_eq!(data.frames.len(), public_wav_slots.len());
        data.trace_slot_positions = public_wav_slots.to_vec();
        data.trace_pair_wav_start_basis = Some(plan.start_basis.to_string());
        data.trace_clock_resolution = Some(clock_model.to_string());
        if let Some(resolution) = comparison_resolution {
            debug_assert_eq!(data.frames.len(), comparison_slots.len());
            data.trace_comparison_resolution = Some(resolution.to_string());
            data.trace_comparison_slot_positions = comparison_slots.to_vec();
        } else {
            data.trace_comparison_resolution = None;
            data.trace_comparison_slot_positions.clear();
        }
        data.trace_raw_host_slot_positions = if raw_host_slots.len() == data.frames.len() {
            raw_host_slots.to_vec()
        } else {
            Vec::new()
        };
        data.trace_pair_offset_samples = None;
        data.trace_pair_alignment_score = None;
        if plan.exact {
            if let (Some(clock), Some(origin)) = (data.trace_clock.as_mut(), producer_origin) {
                clock.origin_position_samples = origin;
                clock.end_position_samples = origin.saturating_add(
                    i64::try_from(expected.expected_duration_samples).unwrap_or(i64::MAX),
                );
                clock.sample_rate = expected.expected_sample_rate;
            }
        } else {
            // A chronological lane has a WAV-relative display order but no proven host clock.
            // Retaining the provisional absolute clock here would let a consumer overstate it.
            data.trace_clock = None;
        }
    };
    match (left.role, right.role) {
        (Role::Pre, Role::Post) => {
            rebuild(
                left,
                plan.pre_frames,
                &plan.pre_producer_slots,
                &plan.pre_wav_slots,
                &plan.pre_comparison_slots,
                &plan.pre_raw_host_slots,
                plan.comparison_resolution,
                plan.pre_origin_position_samples,
                plan.pre_clock_model,
            );
            rebuild(
                right,
                plan.post_frames,
                &plan.post_producer_slots,
                &plan.post_wav_slots,
                &plan.post_comparison_slots,
                &plan.post_raw_host_slots,
                plan.comparison_resolution,
                plan.post_origin_position_samples,
                plan.post_clock_model,
            );
        }
        (Role::Post, Role::Pre) => {
            rebuild(
                left,
                plan.post_frames,
                &plan.post_producer_slots,
                &plan.post_wav_slots,
                &plan.post_comparison_slots,
                &plan.post_raw_host_slots,
                plan.comparison_resolution,
                plan.post_origin_position_samples,
                plan.post_clock_model,
            );
            rebuild(
                right,
                plan.pre_frames,
                &plan.pre_producer_slots,
                &plan.pre_wav_slots,
                &plan.pre_comparison_slots,
                &plan.pre_raw_host_slots,
                plan.comparison_resolution,
                plan.pre_origin_position_samples,
                plan.pre_clock_model,
            );
        }
        _ => return false,
    }
    if plan.exact {
        let (Some(pre_origin), Some(post_origin)) = (
            plan.pre_origin_position_samples,
            plan.post_origin_position_samples,
        ) else {
            return false;
        };
        match (left.role, right.role) {
            (Role::Pre, Role::Post) => {
                bind_annotation_marks_to_wav(left, pre_origin, expected.expected_duration_samples);
                bind_annotation_marks_to_wav(
                    right,
                    post_origin,
                    expected.expected_duration_samples,
                );
            }
            (Role::Post, Role::Pre) => {
                bind_annotation_marks_to_wav(left, post_origin, expected.expected_duration_samples);
                bind_annotation_marks_to_wav(right, pre_origin, expected.expected_duration_samples);
            }
            _ => return false,
        }
    }
    normalize_late_expected_record(left, expected)
        && normalize_late_expected_record(right, expected)
}

fn bind_annotation_marks_to_wav(
    data: &mut PluginDataFile,
    wav_origin_samples: i64,
    wav_duration_samples: u64,
) {
    for annotation in &mut data.annotations {
        let Some(mark) = annotation.mark.as_mut() else {
            continue;
        };
        mark.wav_position_samples = mark
            .producer_position_samples
            .checked_sub(wav_origin_samples)
            .and_then(|position| u64::try_from(position).ok())
            .filter(|position| *position <= wav_duration_samples);
    }
}

fn synchronize_pair_annotations(left: &mut PluginDataFile, right: &mut PluginDataFile) {
    let mut merged = left.annotations.clone();
    for annotation in &right.annotations {
        let duplicate = merged.iter().any(|existing| {
            match (existing.mark.as_ref(), annotation.mark.as_ref()) {
                (Some(existing), Some(candidate)) => existing.id == candidate.id,
                (None, None) => existing.t == annotation.t && existing.memo == annotation.memo,
                _ => false,
            }
        });
        if !duplicate {
            merged.push(annotation.clone());
        }
    }
    merged.sort_by(|a, b| a.t.cmp(&b.t));
    left.annotations = merged.clone();
    right.annotations = merged;
}

fn normalize_late_expected_record(
    data: &mut PluginDataFile,
    expected: &ExpectedWavMetadata,
) -> bool {
    let expected_was_latched_by_record = data.expected_wav.as_ref() == Some(expected)
        && expected.is_usable()
        && data.sample_rate > 0
        && data.sample_rate == expected.expected_sample_rate
        && data
            .record_session_id
            .as_deref()
            .is_some_and(|id| !id.is_empty())
        && data.status == Status::Closed;
    if (!expected_was_latched_by_record && !late_expected_can_own_record(data, expected))
        || !late_expected_prior_reasons_are_reconcilable(data)
    {
        return false;
    }
    let Some(duration_ms) = expected_wav_duration_ms(expected) else {
        return false;
    };
    let expected_frame_count = duration_ms / TRACE_FRAME_INTERVAL_MS;
    let Ok(expected_len) = usize::try_from(expected_frame_count) else {
        return false;
    };
    if expected_len == 0 {
        return false;
    }

    data.frames.retain(|frame| frame.t_ms <= duration_ms);
    data.frames.truncate(expected_len);
    data.trace_slot_positions.truncate(expected_len);
    if data.trace_slot_positions.len() != data.frames.len() {
        return false;
    }
    let slots_are_factual_wav_positions = data.trace_slot_positions.iter().all(|slot| {
        *slot > 0
            && u64::try_from(*slot)
                .ok()
                .is_some_and(|slot| slot <= expected.expected_duration_samples)
    }) && data
        .trace_slot_positions
        .windows(2)
        .all(|pair| pair[0] < pair[1]);
    let frames_match_sparse_slots =
        data.frames
            .iter()
            .zip(&data.trace_slot_positions)
            .all(|(frame, slot)| {
                u64::try_from(*slot).ok().is_some_and(|slot| {
                    frame.t_ms == slot.saturating_mul(1_000) / expected.expected_sample_rate as u64
                })
            });
    if !slots_are_factual_wav_positions || !frames_match_sparse_slots {
        return false;
    }
    let comparison_proof_is_valid = match data.trace_comparison_resolution.as_deref() {
        Some(crate::trace_content_clock::TRACE_COMPARISON_PRODUCER_CONTENT_EXACT) => {
            data.trace_comparison_slot_positions.len() == data.frames.len()
                && data.trace_comparison_slot_positions.iter().all(|position| {
                    *position >= 0
                        && u64::try_from(*position)
                            .ok()
                            .is_some_and(|position| position <= expected.expected_duration_samples)
                })
                && data
                    .trace_comparison_slot_positions
                    .windows(2)
                    .all(|pair| pair[0] < pair[1])
        }
        Some(crate::trace_content_clock::TRACE_COMPARISON_RAW_HOST_EXACT) => {
            data.trace_comparison_slot_positions.len() == data.frames.len()
                && data
                    .trace_comparison_slot_positions
                    .windows(2)
                    .all(|pair| pair[0] < pair[1])
        }
        _ => false,
    };
    if !comparison_proof_is_valid {
        data.trace_comparison_resolution = None;
        data.trace_comparison_slot_positions.clear();
    }
    if let Some(psb) = &mut data.psb_snapshots {
        psb.retain(|snapshot| snapshot.t_ms <= duration_ms);
    }

    let raw_trace_count = data
        .trace_diagnostics
        .as_ref()
        .map(|diag| diag.raw_trace_count)
        .unwrap_or(data.frames.len() as u64)
        .max(data.frames.len() as u64);
    let explicit_silence_frame_count = data
        .frames
        .iter()
        .filter(|frame| late_expected_frame_is_explicit_silence(frame))
        .count() as u64;
    let duration_frames_48k = u128_to_u64_saturating(
        (expected.expected_duration_samples as u128)
            .saturating_mul(TRACE_TIMEBASE_HZ as u128)
            .saturating_div(expected.expected_sample_rate as u128),
    );

    let producer_host_range = data.bounce_take.as_ref().and_then(|take| {
        take.host_start_position_samples
            .zip(take.host_end_position_samples)
    });
    data.expected_wav = Some(expected.clone());
    data.bounce_marker.duration_samples = expected.expected_duration_samples;
    data.bounce_take = Some(BounceTake {
        source: BOUNCE_SOURCE_EXPECTED_WAV.to_string(),
        time_axis: "native_samples".to_string(),
        alignment_status: BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY.to_string(),
        sample_rate: expected.expected_sample_rate,
        wav_start_sample: 0,
        wav_end_sample: expected.expected_duration_samples,
        duration_samples: expected.expected_duration_samples,
        duration_frames_48k,
        start_t_ms: 0,
        end_t_ms: duration_ms,
        trace_sample_count: raw_trace_count,
        frame_count: data.frames.len() as u64,
        host_start_position_samples: producer_host_range.map(|range| range.0),
        host_end_position_samples: producer_host_range.map(|range| range.1),
    });
    let measured_frame_count = data.frames.len() as u64;
    let mapped_coverage = latency_mapped_trace_coverage(
        data,
        expected.expected_duration_samples,
        expected.expected_sample_rate,
    );
    let (expected_frame_count, measured_frame_count, missing_slots) = mapped_coverage.map_or(
        (
            expected_frame_count,
            measured_frame_count,
            expected_frame_count.saturating_sub(measured_frame_count),
        ),
        |coverage| {
            (
                coverage.expected_frame_count,
                coverage.measured_frame_count,
                coverage.missing_slots,
            )
        },
    );
    data.trace_diagnostics = Some(TraceDiagnostics {
        raw_trace_count,
        expected_frame_count,
        measured_frame_count,
        missing_slots,
        explicit_silence_frame_count,
    });
    let Some(record_session_id) = data
        .record_session_id
        .as_deref()
        .map(str::trim)
        .filter(|session| !session.is_empty())
        .map(str::to_string)
    else {
        return false;
    };
    let Some(wav_hash) = expected
        .wav_hash
        .as_deref()
        .map(str::trim)
        .filter(|hash| !hash.is_empty())
        .map(str::to_string)
    else {
        return false;
    };
    data.trace_time_axis = Some(crate::trace_alignment::TRACE_TIME_AXIS.to_string());
    data.trace_wav_reference = Some(TraceWavReference {
        basis: crate::trace_alignment::TRACE_WAV_REFERENCE_BASIS.to_string(),
        capture_basis: crate::trace_alignment::TRACE_CAPTURE_BASIS.to_string(),
        start_sample: 0,
        end_sample: expected.expected_duration_samples,
        sample_rate: expected.expected_sample_rate,
        record_session_id,
        bounce_id: expected.bounce_id.clone(),
        wav_hash,
    });
    data.integrity_reasons
        .retain(|reason| !late_expected_reconcilable_reason(reason));
    if missing_slots > 0 {
        data.integrity_reasons
            .push("missing_trace_slots".to_string());
    }
    data.integrity_degraded = data.dropped_samples > 0 || !data.integrity_reasons.is_empty();
    data.validity = !data.integrity_degraded;
    data.commit_status = Some("committed".to_string());
    refresh_record_quality(data);
    side_visibility_failure_reasons(data).is_empty()
}

fn late_expected_record_is_aligned(data: &PluginDataFile, expected: &ExpectedWavMetadata) -> bool {
    if data.expected_wav.as_ref() != Some(expected) {
        return false;
    }
    let Some(take) = data.bounce_take.as_ref() else {
        return false;
    };
    let Some(duration_ms) = expected_wav_duration_ms(expected) else {
        return false;
    };
    take.source == BOUNCE_SOURCE_EXPECTED_WAV
        && take.sample_rate == expected.expected_sample_rate
        && take.duration_samples == expected.expected_duration_samples
        && take.wav_start_sample == 0
        && take.wav_end_sample == expected.expected_duration_samples
        && take.end_t_ms == duration_ms
        && data
            .record_quality
            .as_ref()
            .is_some_and(|quality| quality.expected_wav_ready && quality.usable)
        && crate::trace_alignment::has_canonical_wav_reference(data)
}

fn late_expected_can_own_record(data: &PluginDataFile, expected: &ExpectedWavMetadata) -> bool {
    if !expected.is_usable()
        || data.sample_rate == 0
        || data.sample_rate != expected.expected_sample_rate
        || data.record_session_id.as_deref().is_none_or(str::is_empty)
        || data.started_at_ms <= 0
        || expected.created_at_ms < data.started_at_ms
        || data.status != Status::Closed
    {
        return false;
    }
    if let Some(existing) = &data.expected_wav {
        if existing.is_usable() && existing != expected {
            return false;
        }
    }
    if data.started_at_ms > 0 {
        if expected
            .wav_mtime_ms
            .saturating_add(LATE_EXPECTED_BEFORE_RECORD_GRACE_MS)
            < data.started_at_ms
        {
            return false;
        }
        if expected
            .created_at_ms
            .saturating_add(LATE_EXPECTED_BEFORE_RECORD_GRACE_MS)
            < data.started_at_ms
        {
            return false;
        }
    }
    if let Some(end_ms) = parse_plugin_data_wall_clock_ms(&data.bounce_marker.wall_clock_end) {
        if end_ms >= data.started_at_ms
            && expected.wav_mtime_ms > end_ms.saturating_add(LATE_EXPECTED_AFTER_RECORD_GRACE_MS)
        {
            return false;
        }
    }
    true
}

fn late_expected_prior_reasons_are_reconcilable(data: &PluginDataFile) -> bool {
    data.integrity_reasons
        .iter()
        .all(|reason| reason == "lifecycle_shutdown" || late_expected_reconcilable_reason(reason))
}

fn late_expected_reconcilable_reason(reason: &str) -> bool {
    matches!(
        reason,
        "validity_false"
            | "integrity_degraded"
            | "missing_expected_wav_metadata"
            | "record_clock_not_wav_bounded"
            | "missing_trace_slots"
            | "raw_trace_timeline_gap"
            | "frame_timeline_gap"
            | "sparse_trace_density"
            | "record_too_short"
            | "bounce_take_duration_mismatch"
            | "bounce_take_end_sample_mismatch"
            | "bounce_take_end_time_mismatch"
            | "bounce_take_48k_duration_mismatch"
            | "trace_expected_frame_count_bounce_take_mismatch"
            | "trace_frame_count_mismatch"
            | "trace_frame_payload_count_mismatch"
            | "trace_frame_payload_count_bounce_take_mismatch"
            | "trace_slots_not_complete"
            | "bounce_take_trace_frame_count_mismatch"
            | "bounce_take_frame_payload_count_mismatch"
            | "pair_expected_wav_mismatch"
            | "pair_bounce_take_source_mismatch"
            | "pair_bounce_take_time_axis_mismatch"
            | "pair_bounce_take_alignment_mismatch"
            | "pair_bounce_take_sample_rate_mismatch"
            | "pair_bounce_take_start_sample_mismatch"
            | "pair_bounce_take_end_sample_mismatch"
            | "pair_bounce_take_duration_mismatch"
            | "pair_bounce_take_48k_duration_mismatch"
            | "pair_bounce_take_start_time_mismatch"
            | "pair_bounce_take_end_time_mismatch"
            | "pair_bounce_take_frame_count_mismatch"
            | "pair_trace_expected_frame_count_mismatch"
            | "pair_trace_measured_frame_count_mismatch"
    )
}

fn late_expected_frame_is_explicit_silence(frame: &Frame) -> bool {
    frame.true_peak <= -99.9 && frame.lufs_m <= -99.9
}

fn expected_wav_duration_ms(expected: &ExpectedWavMetadata) -> Option<u64> {
    if expected.expected_sample_rate == 0 {
        return None;
    }
    Some(u128_to_u64_saturating(
        (expected.expected_duration_samples as u128)
            .saturating_mul(1_000)
            .saturating_div(expected.expected_sample_rate as u128),
    ))
}

fn parse_plugin_data_wall_clock_ms(value: &str) -> Option<i64> {
    chrono::DateTime::parse_from_rfc3339(value)
        .ok()
        .map(|dt| dt.timestamp_millis())
}

fn pair_paths_from_closed_artifact_path(path: &Path) -> Option<PairPaths> {
    let file_name = path.file_name()?.to_owned();
    let parent = path.parent()?;
    let role_dir = match parent.file_name().and_then(|name| name.to_str()) {
        Some(PAIR_RECORD_MEMBERS_DIR) | Some(".failed" | ".pair_pending") => parent.parent()?,
        _ => parent,
    };
    Some(PairPaths {
        final_path: role_dir.join(&file_name),
        member_path: role_dir.join(PAIR_RECORD_MEMBERS_DIR).join(&file_name),
        pair_pending_path: role_dir.join(".pair_pending").join(&file_name),
        failed_path: role_dir.join(".failed").join(&file_name),
    })
}

fn writer_paths_from_pair_paths(paths: &PairPaths) -> WriterPaths {
    WriterPaths {
        final_path: paths.final_path.clone(),
        tmp_path: sibling_tmp_path(&paths.final_path),
        staging_path: paths.final_path.with_extension("json.partial"),
        pair_pending_path: paths.pair_pending_path.clone(),
        failed_path: paths.failed_path.clone(),
    }
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
        capture_generation_id: self_data.capture_generation_id.clone().unwrap_or_default(),
        generation_started_at_ms: self_data.generation_started_at_ms,
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
    let Some(session_id) = data.record_session_id.as_deref() else {
        return;
    };
    // `.failed` 等で expected_wav が無い場合は metadata-free lifecycle だけを閉じる。
    // Drop 後に exact WAV が結合された成功経路だけ bounce_id を渡して marker を完成させる。
    let bounce_id = data
        .expected_wav
        .as_ref()
        .map(|expected| expected.bounce_id.as_str());
    if let Err(e) = crate::record_expected::mark_expected_metadata_consumed(
        &root,
        &data.project_hash,
        bounce_id,
        session_id,
    ) {
        log::warn!(
            "[pair_record_session] expected metadata consume marker failed: project={} bounce={:?} err={}",
            data.project_hash,
            bounce_id,
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
    let mut reasons = normal_publish_failure_reasons(left);
    reasons.extend(normal_publish_failure_reasons(right));
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
    if pre.capture_generation_id != post.capture_generation_id
        && (pre.capture_generation_id.is_some() || post.capture_generation_id.is_some())
    {
        reasons.push("pair_capture_generation_mismatch");
    }
    if pre.generation_started_at_ms != post.generation_started_at_ms
        && (pre.capture_generation_id.is_some() || post.capture_generation_id.is_some())
    {
        reasons.push("pair_generation_start_mismatch");
    }
    if pre.paired_post_instance_id.as_deref() != Some(post.instance_id.as_str()) {
        reasons.push("pair_pre_post_link_mismatch");
    }
    if post.paired_pre_instance_id.as_deref() != Some(pre.instance_id.as_str()) {
        reasons.push("pair_post_pre_link_mismatch");
    }
    reasons.extend(pair_publish_consistency_failure_reasons(pre, post));
    dedup_reasons(reasons)
}

fn pair_visibility_failure_reasons(
    left: &PluginDataFile,
    right: &PluginDataFile,
) -> Vec<&'static str> {
    let mut reasons = side_visibility_failure_reasons(left);
    reasons.extend(side_visibility_failure_reasons(right));
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
    if pre.capture_generation_id != post.capture_generation_id
        && (pre.capture_generation_id.is_some() || post.capture_generation_id.is_some())
    {
        reasons.push("pair_capture_generation_mismatch");
    }
    if pre.paired_post_instance_id.as_deref() != Some(post.instance_id.as_str()) {
        reasons.push("pair_pre_post_link_mismatch");
    }
    if post.paired_pre_instance_id.as_deref() != Some(pre.instance_id.as_str()) {
        reasons.push("pair_post_pre_link_mismatch");
    }
    if pre.sample_rate != post.sample_rate {
        reasons.push("pair_sample_rate_mismatch");
    }
    if pre.expected_wav != post.expected_wav {
        reasons.push("pair_expected_wav_mismatch");
    }
    if !pre
        .expected_wav
        .as_ref()
        .is_some_and(ExpectedWavMetadata::is_usable)
        || !post
            .expected_wav
            .as_ref()
            .is_some_and(ExpectedWavMetadata::is_usable)
    {
        reasons.push("missing_expected_wav_metadata");
    }
    if !crate::trace_alignment::has_canonical_wav_reference(pre)
        || !crate::trace_alignment::has_canonical_wav_reference(post)
        || pre.trace_wav_reference != post.trace_wav_reference
    {
        reasons.push("pair_wav_identity_unavailable");
    }
    dedup_reasons(reasons)
}

fn pair_publish_consistency_failure_reasons(
    pre: &PluginDataFile,
    post: &PluginDataFile,
) -> Vec<&'static str> {
    let mut reasons = trace_publish_failure_reasons(pre);
    reasons.extend(trace_publish_failure_reasons(post));
    if pre.sample_rate != post.sample_rate {
        reasons.push("pair_sample_rate_mismatch");
    }
    if pre.expected_wav != post.expected_wav {
        reasons.push("pair_expected_wav_mismatch");
    }
    let role_specific_mapped_axes = uses_latency_mapped_wav_grid(pre)
        && uses_latency_mapped_wav_grid(post)
        && pre.trace_time_axis.as_deref() == Some(crate::trace_alignment::TRACE_TIME_AXIS)
        && post.trace_time_axis.as_deref() == Some(crate::trace_alignment::TRACE_TIME_AXIS);
    if let (Some(pre_take), Some(post_take)) = (&pre.bounce_take, &post.bounce_take) {
        if pre_take.source != post_take.source {
            reasons.push("pair_bounce_take_source_mismatch");
        }
        if pre_take.time_axis != post_take.time_axis {
            reasons.push("pair_bounce_take_time_axis_mismatch");
        }
        if pre_take.alignment_status != post_take.alignment_status {
            reasons.push("pair_bounce_take_alignment_mismatch");
        }
        if pre_take.sample_rate != post_take.sample_rate {
            reasons.push("pair_bounce_take_sample_rate_mismatch");
        }
        if pre_take.wav_start_sample != post_take.wav_start_sample {
            reasons.push("pair_bounce_take_start_sample_mismatch");
        }
        if pre_take.wav_end_sample != post_take.wav_end_sample {
            reasons.push("pair_bounce_take_end_sample_mismatch");
        }
        if pre_take.duration_samples != post_take.duration_samples {
            reasons.push("pair_bounce_take_duration_mismatch");
        }
        if pre_take.duration_frames_48k != post_take.duration_frames_48k {
            reasons.push("pair_bounce_take_48k_duration_mismatch");
        }
        if pre_take.start_t_ms != post_take.start_t_ms {
            reasons.push("pair_bounce_take_start_time_mismatch");
        }
        if pre_take.end_t_ms != post_take.end_t_ms {
            reasons.push("pair_bounce_take_end_time_mismatch");
        }
        if !role_specific_mapped_axes && pre_take.frame_count != post_take.frame_count {
            reasons.push("pair_bounce_take_frame_count_mismatch");
        }
    }
    if let (Some(pre_diag), Some(post_diag)) = (&pre.trace_diagnostics, &post.trace_diagnostics) {
        if !role_specific_mapped_axes
            && pre_diag.expected_frame_count != post_diag.expected_frame_count
        {
            reasons.push("pair_trace_expected_frame_count_mismatch");
        }
        if !role_specific_mapped_axes
            && pre_diag.measured_frame_count != post_diag.measured_frame_count
        {
            reasons.push("pair_trace_measured_frame_count_mismatch");
        }
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
    if pre.sample_rate != post.sample_rate {
        reasons.push("pair_sample_rate_mismatch");
    }
    if pre.expected_wav != post.expected_wav {
        reasons.push("pair_expected_wav_mismatch");
    }
    let role_specific_mapped_axes = uses_latency_mapped_wav_grid(pre)
        && uses_latency_mapped_wav_grid(post)
        && pre.trace_time_axis.as_deref() == Some(crate::trace_alignment::TRACE_TIME_AXIS)
        && post.trace_time_axis.as_deref() == Some(crate::trace_alignment::TRACE_TIME_AXIS);
    if let (Some(pre_take), Some(post_take)) = (&pre.bounce_take, &post.bounce_take) {
        if pre_take.source != post_take.source {
            reasons.push("pair_bounce_take_source_mismatch");
        }
        if pre_take.time_axis != post_take.time_axis {
            reasons.push("pair_bounce_take_time_axis_mismatch");
        }
        if pre_take.alignment_status != post_take.alignment_status {
            reasons.push("pair_bounce_take_alignment_mismatch");
        }
        if pre_take.sample_rate != post_take.sample_rate {
            reasons.push("pair_bounce_take_sample_rate_mismatch");
        }
        if pre_take.wav_start_sample != post_take.wav_start_sample {
            reasons.push("pair_bounce_take_start_sample_mismatch");
        }
        if pre_take.wav_end_sample != post_take.wav_end_sample {
            reasons.push("pair_bounce_take_end_sample_mismatch");
        }
        if pre_take.duration_samples != post_take.duration_samples {
            reasons.push("pair_bounce_take_duration_mismatch");
        }
        if pre_take.duration_frames_48k != post_take.duration_frames_48k {
            reasons.push("pair_bounce_take_48k_duration_mismatch");
        }
        if pre_take.start_t_ms != post_take.start_t_ms {
            reasons.push("pair_bounce_take_start_time_mismatch");
        }
        if pre_take.end_t_ms != post_take.end_t_ms {
            reasons.push("pair_bounce_take_end_time_mismatch");
        }
        if !role_specific_mapped_axes && pre_take.frame_count != post_take.frame_count {
            reasons.push("pair_bounce_take_frame_count_mismatch");
        }
    }
    if let (Some(pre_diag), Some(post_diag)) = (&pre.trace_diagnostics, &post.trace_diagnostics) {
        if !role_specific_mapped_axes
            && pre_diag.expected_frame_count != post_diag.expected_frame_count
        {
            reasons.push("pair_trace_expected_frame_count_mismatch");
        }
        if !role_specific_mapped_axes
            && pre_diag.measured_frame_count != post_diag.measured_frame_count
        {
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
        mark: None,
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
#[allow(clippy::too_many_arguments)]
#[cfg(test)]
pub(crate) fn make_frame_optional(
    t_ms: u64,
    n_prime: Option<[f64; 20]>,
    sharpness: Option<f64>,
    lufs_m: f64,
    true_peak: f64,
    crest: f64,
    psr: Option<f64>,
) -> Frame {
    make_frame_optional_with_n(
        t_ms, n_prime, None, sharpness, lufs_m, true_peak, crest, psr,
    )
}

#[allow(clippy::too_many_arguments)]
pub(crate) fn make_frame_optional_with_n(
    t_ms: u64,
    n_prime: Option<[f64; 20]>,
    n_prime_total: Option<f64>,
    sharpness: Option<f64>,
    lufs_m: f64,
    true_peak: f64,
    crest: f64,
    psr: Option<f64>,
) -> Frame {
    let rounded_n = n_prime.map(|arr| {
        let mut rounded = [0.0; 20];
        for (i, v) in arr.iter().enumerate() {
            rounded[i] = round1(*v);
        }
        rounded
    });
    Frame {
        t_ms,
        n_prime: rounded_n,
        n_prime_total: n_prime_total.filter(|v| v.is_finite()).map(round2),
        sharpness: sharpness.filter(|v| v.is_finite()).map(round2),
        lufs_m: round1(lufs_m),
        true_peak: round1(true_peak),
        crest: round1(crest),
        psr: psr.filter(|v| v.is_finite()).map(round1),
    }
}

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
            wav_time_reference_samples: None,
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

    fn fresh_expected_wav_fixture(duration_samples: u64) -> ExpectedWavMetadata {
        let now_ms = chrono::Utc::now().timestamp_millis();
        ExpectedWavMetadata {
            expected_duration_samples: duration_samples,
            expected_sample_rate: 48_000,
            wav_time_reference_samples: None,
            wav_path: "/tmp/kirin-plugin-data-late-expected.wav".to_string(),
            bounce_id: format!("bounce-late-expected-{now_ms}"),
            created_at_ms: now_ms,
            wav_file_size: Some(duration_samples.saturating_mul(8).saturating_add(44)),
            wav_mtime_ms: now_ms.saturating_sub(1_000),
            wav_hash: Some(format!("hash-late-expected-{now_ms}")),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    fn write_drop_commit_fixture(
        base: &Path,
        session_id: &str,
        metadata: &ExpectedWavMetadata,
        with_transaction: bool,
    ) {
        let drop_commit_id = format!("drop-{session_id}");
        let commit = crate::record_drop_commit::DropRecordCommit {
            schema_version: crate::record_drop_commit::DROP_COMMIT_SCHEMA.to_string(),
            drop_commit_id: drop_commit_id.clone(),
            project_hash: "project_hash_test".to_string(),
            record_session_id: session_id.to_string(),
            capture_generation_id: String::new(),
            generation_started_at_ms: 0,
            created_at_ms: metadata.created_at_ms,
            metadata: metadata.clone(),
        };
        crate::atomic_file::write_bytes_atomic(
            &crate::record_drop_commit::drop_commit_path(base, "project_hash_test", session_id),
            &serde_json::to_vec(&commit).unwrap(),
        )
        .unwrap();
        if !with_transaction {
            return;
        }
        let transaction = crate::record_drop_commit::DropRecordTransaction {
            schema_version: crate::record_drop_commit::DROP_TRANSACTION_SCHEMA.to_string(),
            drop_commit_id: drop_commit_id.clone(),
            project_hash: "project_hash_test".to_string(),
            capture_generation_id: String::new(),
            generation_started_at_ms: 0,
            created_at_ms: metadata.created_at_ms,
            bounce_id: metadata.bounce_id.clone(),
            wav_hash: metadata.wav_hash.clone().unwrap(),
            record_session_ids: vec![session_id.to_string()],
        };
        crate::atomic_file::write_bytes_atomic(
            &crate::record_drop_commit::drop_transaction_path(
                base,
                "project_hash_test",
                &drop_commit_id,
            ),
            &serde_json::to_vec(&transaction).unwrap(),
        )
        .unwrap();
        let generation_transaction = crate::record_drop_commit::DropRecordGenerationTransaction {
            schema_version: crate::record_drop_commit::DROP_GENERATION_TRANSACTION_SCHEMA
                .to_string(),
            drop_commit_id: drop_commit_id.clone(),
            capture_generation_id: transaction.capture_generation_id.clone(),
            generation_started_at_ms: transaction.generation_started_at_ms,
            created_at_ms: transaction.created_at_ms,
            bounce_id: transaction.bounce_id.clone(),
            wav_hash: transaction.wav_hash.clone(),
            projects: vec![crate::record_drop_commit::DropRecordGenerationProject {
                project_hash: "project_hash_test".to_string(),
                record_session_ids: transaction.record_session_ids.clone(),
            }],
        };
        crate::atomic_file::write_bytes_atomic(
            &crate::record_drop_commit::drop_generation_transaction_path(base, &drop_commit_id),
            &serde_json::to_vec(&generation_transaction).unwrap(),
        )
        .unwrap();
    }

    fn iso_ms(ms: i64) -> String {
        chrono::DateTime::<chrono::Utc>::from_timestamp_millis(ms)
            .unwrap()
            .format("%Y-%m-%dT%H:%M:%S%.3fZ")
            .to_string()
    }

    fn set_late_expected_record_time(writer: &mut PluginDataWriter, start_ms: i64, end_ms: i64) {
        writer.set_record_start_wall_clock(iso_ms(start_ms));
        writer.set_started_at_ms(start_ms);
        writer.data.bounce_marker.wall_clock_end = iso_ms(end_ms);
    }

    fn configure_render_clock_take(writer: &mut PluginDataWriter, duration_samples: u64) {
        let sample_rate = writer.data.sample_rate;
        let duration_ms = duration_samples
            .saturating_mul(1_000)
            .saturating_div(sample_rate as u64);
        let frame_count = duration_ms / TRACE_FRAME_INTERVAL_MS;
        writer.set_expected_wav(None);
        writer.clear_frames();
        for idx in 1..=frame_count {
            let t_ms = idx * TRACE_FRAME_INTERVAL_MS;
            writer.append_frame(
                t_ms,
                [idx as f64; 20],
                idx as f64,
                -24.0 + idx as f64 * 0.1,
                -6.0 + idx as f64 * 0.1,
                12.0,
                Some(9.0),
            );
        }
        writer.set_bounce_take(BounceTake {
            source: BOUNCE_SOURCE_RENDER_CLOCK.to_string(),
            time_axis: "native_samples".to_string(),
            alignment_status: BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY.to_string(),
            sample_rate,
            wav_start_sample: 0,
            wav_end_sample: duration_samples,
            duration_samples,
            duration_frames_48k: duration_samples,
            start_t_ms: 0,
            end_t_ms: duration_ms,
            trace_sample_count: frame_count,
            frame_count,
            host_start_position_samples: None,
            host_end_position_samples: None,
        });
        writer.set_trace_diagnostics(TraceDiagnostics {
            raw_trace_count: frame_count,
            expected_frame_count: frame_count,
            measured_frame_count: frame_count,
            missing_slots: 0,
            explicit_silence_frame_count: 0,
        });
        writer.set_trace_time_axis(Some(
            crate::trace_alignment::TRACE_HOST_TIME_AXIS.to_string(),
        ));
        writer.set_trace_wav_reference(None);
        writer.set_trace_clock(Some(TraceClock {
            basis: crate::trace_alignment::TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples: 0,
            end_position_samples: duration_samples.min(i64::MAX as u64) as i64,
            sample_rate,
            sources: vec!["project_timeline".to_string()],
        }));
        writer.set_trace_slot_positions(
            (1..=frame_count)
                .map(|index| {
                    index
                        .saturating_mul(sample_rate as u64 / 10)
                        .min(i64::MAX as u64) as i64
                })
                .collect(),
        );
    }

    #[test]
    fn one_missing_timing_slot_publishes_the_measured_pair_as_usable_fallback() {
        let base = isolated_dir();
        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = Some(0);
        let session_id = "session-one-missing-slot";
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-one-missing",
            None,
            Some("iid-post-one-missing".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-one-missing",
            Some("iid-pre-one-missing".to_string()),
            None,
        );
        let start_ms = expected.created_at_ms.saturating_sub(2_000);
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some(session_id.to_string()));
            writer
                .set_capture_generation(Some("generation-one-missing-slot".to_string()), start_ms);
            set_late_expected_record_time(writer, start_ms, expected.created_at_ms);
            configure_render_clock_take(writer, 48_000);
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        post.data.frames.remove(7);
        post.data.trace_slot_positions.remove(7);

        assert!(normalize_late_expected_pair(
            &mut pre.data,
            &mut post.data,
            &expected
        ));
        assert_eq!(pre.data.frames.len(), 10);
        assert_eq!(post.data.frames.len(), 9);
        assert_eq!(
            post.data.trace_slot_positions,
            vec![4_800, 9_600, 14_400, 19_200, 24_000, 28_800, 33_600, 43_200, 48_000],
            "the missing 800 ms slot must remain absent rather than shifting later frames"
        );
        assert_eq!(
            post.data
                .frames
                .iter()
                .map(|frame| frame.t_ms)
                .collect::<Vec<_>>(),
            vec![100, 200, 300, 400, 500, 600, 700, 900, 1_000]
        );
        assert_eq!(
            pre.data.trace_diagnostics.as_ref().unwrap().missing_slots,
            0
        );
        assert_eq!(
            post.data.trace_diagnostics.as_ref().unwrap().missing_slots,
            1
        );
        assert!(prepare_late_expected_pair(&mut pre.data, &mut post.data));
        for data in [&pre.data, &post.data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert_eq!(
                data.record_quality
                    .as_ref()
                    .map(|quality| quality.status.as_str()),
                Some("usable_fallback")
            );
            assert!(data.trace_content_alignment.is_none());
        }
    }

    #[test]
    fn non_bwf_length_mismatch_keeps_raw_host_comparison_proof_after_commit() {
        let base = isolated_dir();
        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = None;
        let session_id = "session-raw-host-comparison";
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-raw-host",
            None,
            Some("iid-post-raw-host".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-raw-host",
            Some("iid-pre-raw-host".to_string()),
            None,
        );
        let origin = 15_594_720_i64;
        let start_ms = expected.created_at_ms.saturating_sub(2_000);
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some(session_id.to_string()));
            writer.set_capture_generation(Some("generation-raw-host".to_string()), start_ms);
            set_late_expected_record_time(writer, start_ms, expected.created_at_ms);
            configure_render_clock_take(writer, 52_800);
            writer.set_raw_host_clock_range(Some(HostClockRange {
                start_position_samples: origin,
                end_position_samples: origin + 52_800,
            }));
            writer.set_trace_clock_observations(
                writer
                    .data
                    .frames
                    .iter()
                    .enumerate()
                    .map(|(index, frame)| TraceClockObservation {
                        frame: frame.clone(),
                        producer_position_samples: None,
                        raw_host_position_samples: Some(origin + (index as i64 + 1) * 4_800),
                        capture_epoch: Some(7),
                        clock_source: Some("audio_render_timeline".to_string()),
                        presentation_latency_source: Some("audio_unit_v2".to_string()),
                        input_presentation_latency_samples: Some(0),
                        output_presentation_latency_samples: Some(512),
                    })
                    .collect(),
            );
            writer.set_trace_raw_host_slot_positions(
                // Record close retained one factual 100 ms tail beyond the later 1 s WAV.
                // Frame/slot pairs must be windowed together during Drop, not rejected by count.
                (1_i64..=11).map(|index| origin + index * 4_800).collect(),
            );
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }

        assert!(normalize_late_expected_pair(
            &mut pre.data,
            &mut post.data,
            &expected
        ));
        for data in [&pre.data, &post.data] {
            assert_eq!(
                data.trace_comparison_resolution.as_deref(),
                Some(crate::trace_content_clock::TRACE_COMPARISON_RAW_HOST_EXACT)
            );
            assert_eq!(data.trace_comparison_slot_positions.len(), 10);
            assert_eq!(data.frames.len(), 10);
            assert!(data.trace_content_alignment.is_none());
        }
        assert_eq!(
            pre.data.trace_comparison_slot_positions,
            post.data.trace_comparison_slot_positions
        );

        assert!(prepare_late_expected_pair(&mut pre.data, &mut post.data));
        for data in [&pre.data, &post.data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert!(data.trace_clock_observations.is_empty());
            assert_eq!(data.trace_raw_host_slot_positions.len(), 10);
            assert_eq!(
                data.trace_raw_host_slot_positions,
                data.trace_comparison_slot_positions
            );
            assert_eq!(
                data.trace_comparison_resolution.as_deref(),
                Some(crate::trace_content_clock::TRACE_COMPARISON_RAW_HOST_EXACT)
            );
            assert_eq!(data.trace_comparison_slot_positions.len(), 10);
        }

        assert!(normalize_late_expected_pair(
            &mut pre.data,
            &mut post.data,
            &expected
        ));
        for data in [&pre.data, &post.data] {
            assert_eq!(
                data.trace_comparison_resolution.as_deref(),
                Some(crate::trace_content_clock::TRACE_COMPARISON_RAW_HOST_EXACT)
            );
            assert_eq!(data.trace_comparison_slot_positions.len(), 10);
            assert_eq!(
                data.trace_raw_host_slot_positions,
                data.trace_comparison_slot_positions
            );
        }

        let mut fallback_pre = pre.data.clone();
        let mut fallback_post = post.data.clone();
        fallback_post.trace_raw_host_slot_positions[5] += 1;
        assert!(normalize_late_expected_pair(
            &mut fallback_pre,
            &mut fallback_post,
            &expected
        ));
        for data in [&fallback_pre, &fallback_post] {
            assert!(data.trace_comparison_resolution.is_none());
            assert!(data.trace_comparison_slot_positions.is_empty());
            assert!(data.trace_raw_host_slot_positions.is_empty());
            assert!(!data.frames.is_empty());
        }
    }

    #[test]
    fn late_drop_publishes_role_wav_slots_and_producer_comparison_as_separate_facts() {
        let base = isolated_dir();
        let expected = fresh_expected_wav_fixture(48_000);
        let start_ms = expected.created_at_ms.saturating_sub(2_000);
        let origin = 100_000_i64;
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-producer-comparison",
            None,
            Some("iid-post-producer-comparison".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-producer-comparison",
            Some("iid-pre-producer-comparison".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some("session-producer-comparison".to_string()));
            writer.set_capture_generation(
                Some("generation-producer-comparison".to_string()),
                start_ms,
            );
            set_late_expected_record_time(writer, start_ms, expected.created_at_ms);
            configure_render_clock_take(writer, 48_000);
            let take = writer.data.bounce_take.as_mut().expect("render take");
            take.host_start_position_samples = Some(origin);
            take.host_end_position_samples = Some(origin + 48_000);
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        let observations = |first_slot: i64, output_latency: u32, base_value: f64| {
            (0_i64..10)
                .map(|index| {
                    let producer = origin + first_slot + index * 4_800;
                    TraceClockObservation {
                        frame: Frame {
                            t_ms: (index as u64 + 1) * TRACE_FRAME_INTERVAL_MS,
                            n_prime: Some([base_value + index as f64; 20]),
                            n_prime_total: Some(base_value),
                            sharpness: Some(base_value),
                            lufs_m: base_value,
                            true_peak: base_value,
                            crest: base_value.abs(),
                            psr: Some(base_value),
                        },
                        producer_position_samples: Some(producer),
                        raw_host_position_samples: producer.checked_add(i64::from(output_latency)),
                        capture_epoch: Some(7),
                        clock_source: Some("audio_render_timeline".to_string()),
                        presentation_latency_source: Some("vst3".to_string()),
                        input_presentation_latency_samples: Some(0),
                        output_presentation_latency_samples: Some(output_latency),
                    }
                })
                .collect::<Vec<_>>()
        };
        pre.set_trace_clock_observations(observations(4_800, 512, -30.0));
        post.set_trace_clock_observations(observations(9_600, 256, -20.0));

        assert!(normalize_late_expected_pair(
            &mut pre.data,
            &mut post.data,
            &expected
        ));
        assert_eq!(pre.data.trace_slot_positions.first(), Some(&5_312));
        assert_eq!(post.data.trace_slot_positions.first(), Some(&9_856));
        assert_eq!(
            pre.data.trace_comparison_slot_positions.first(),
            Some(&4_800)
        );
        assert_eq!(
            post.data.trace_comparison_slot_positions.first(),
            Some(&9_600)
        );
        for data in [&pre.data, &post.data] {
            assert_eq!(
                data.trace_comparison_resolution.as_deref(),
                Some(crate::trace_content_clock::TRACE_COMPARISON_PRODUCER_CONTENT_EXACT)
            );
            assert_eq!(
                data.trace_comparison_slot_positions.len(),
                data.frames.len(),
                "comparison proof must remain one-to-one with measured frames"
            );
            assert_eq!(data.trace_raw_host_slot_positions.len(), data.frames.len());
        }

        assert!(prepare_late_expected_pair(&mut pre.data, &mut post.data));
        for data in [&pre.data, &post.data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert_eq!(
                data.trace_comparison_resolution.as_deref(),
                Some(crate::trace_content_clock::TRACE_COMPARISON_PRODUCER_CONTENT_EXACT)
            );
            assert!(data.trace_clock_observations.is_empty());
            assert!(!data.trace_comparison_slot_positions.is_empty());
        }
    }

    #[test]
    fn zero_frame_side_keeps_the_exact_pair_identity_visible_after_drop() {
        let base = isolated_dir();
        let expected = fresh_expected_wav_fixture(48_000);
        let session_id = "session-zero-frame-side";
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-zero-frame",
            None,
            Some("iid-post-zero-frame".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-zero-frame",
            Some("iid-pre-zero-frame".to_string()),
            None,
        );
        let start_ms = expected.created_at_ms.saturating_sub(2_000);
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some(session_id.to_string()));
            writer.set_capture_generation(Some("generation-zero-frame-side".to_string()), start_ms);
            set_late_expected_record_time(writer, start_ms, expected.created_at_ms);
            configure_render_clock_take(writer, 48_000);
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        post.data.frames.clear();
        post.data.trace_slot_positions.clear();

        assert!(normalize_late_expected_pair(
            &mut pre.data,
            &mut post.data,
            &expected
        ));
        assert_eq!(pre.data.frames.len(), 10);
        assert!(post.data.frames.is_empty());
        assert_eq!(
            post.data.trace_diagnostics.as_ref().unwrap().missing_slots,
            10
        );
        assert!(prepare_late_expected_pair(&mut pre.data, &mut post.data));
        for data in [&pre.data, &post.data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert_eq!(
                data.record_quality
                    .as_ref()
                    .map(|quality| quality.status.as_str()),
                Some("usable_fallback")
            );
            assert!(data.trace_content_alignment.is_none());
            assert!(crate::trace_alignment::has_canonical_wav_reference(data));
        }
    }

    #[test]
    fn pair_normalization_uses_bwf_start_without_comparing_metric_shapes() {
        let base = isolated_dir();
        let mut expected = fresh_expected_wav_fixture(24_000);
        expected.wav_time_reference_samples = Some(480_000);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-content-clock",
            None,
            Some("iid-post-content-clock".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-content-clock",
            Some("iid-pre-content-clock".to_string()),
            None,
        );
        let positions: Vec<i64> = (1..=5).map(|index| 480_000 + index * 4_800).collect();
        let pre_shape = [-13.0, -22.0, -9.0, -18.0, -11.0];
        let post_shape = [-16.0, 31.0, -42.0, 7.0, -25.0];
        let pre_frames: Vec<Frame> = pre_shape
            .iter()
            .enumerate()
            .map(|(index, value)| {
                make_frame_optional(
                    (index as u64 + 1) * 100,
                    None,
                    None,
                    *value,
                    value + 10.0,
                    12.0,
                    None,
                )
            })
            .collect();
        let post_frames: Vec<Frame> = post_shape
            .iter()
            .copied()
            .enumerate()
            .map(|(index, value)| {
                make_frame_optional(
                    (index as u64 + 1) * 100,
                    None,
                    None,
                    value,
                    value + 10.0,
                    9.0,
                    None,
                )
            })
            .collect();
        for (writer, frames) in [(&mut pre, pre_frames), (&mut post, post_frames)] {
            configure_render_clock_take(writer, 38_400);
            writer.data.frames = frames;
            writer.data.trace_slot_positions = positions.clone();
            writer.data.trace_context_frames.clear();
            writer.data.trace_context_slot_positions.clear();
            writer.data.trace_clock = Some(TraceClock {
                basis: crate::trace_alignment::TRACE_CLOCK_BASIS.to_string(),
                origin_position_samples: 480_000,
                end_position_samples: 504_000,
                sample_rate: 48_000,
                sources: vec!["project_timeline".to_string()],
            });
            writer.data.expected_wav = Some(expected.clone());
            writer.data.record_session_id = Some("session-content-clock".to_string());
            writer.data.status = Status::Closed;
        }

        assert!(normalize_late_expected_pair(
            &mut pre.data,
            &mut post.data,
            &expected
        ));
        assert_eq!(
            pre.data.trace_pair_wav_start_basis.as_deref(),
            Some(crate::trace_alignment::TRACE_ALIGNMENT_START_BWF)
        );
        assert_eq!(
            post.data.trace_pair_wav_start_basis.as_deref(),
            Some(crate::trace_alignment::TRACE_ALIGNMENT_START_BWF)
        );
        assert!(pre.data.trace_pair_offset_samples.is_none());
        assert!(post.data.trace_pair_offset_samples.is_none());
        assert_eq!(
            pre.data.trace_slot_positions,
            post.data.trace_slot_positions
        );
        assert_eq!(
            pre.data.trace_slot_positions,
            vec![4_800, 9_600, 14_400, 19_200, 24_000]
        );
        assert_eq!(pre.data.frames.len(), 5);
        assert_eq!(post.data.frames.len(), 5);
        assert_eq!(pre.data.frames[0].lufs_m, pre_shape[0]);
        assert_eq!(post.data.frames[0].lufs_m, post_shape[0]);
        assert_ne!(
            pre_shape, post_shape,
            "metric shape never selects the time axis"
        );
        for (pre_frame, post_frame) in pre.data.frames.iter().zip(&post.data.frames) {
            assert_eq!(pre_frame.t_ms, post_frame.t_ms);
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
        w.set_record_start_wall_clock("2026-04-17T14:32:08.000Z".to_string());
        w.set_record_session_id(Some("session-pair-atomic".to_string()));
        let mut expected = expected_wav_fixture();
        expected.wav_time_reference_samples = Some(0);
        w.set_expected_wav(Some(expected.clone()));
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
            trace_sample_count: 10,
            frame_count: 10,
            host_start_position_samples: None,
            host_end_position_samples: None,
        });
        w.set_trace_diagnostics(TraceDiagnostics {
            raw_trace_count: 10,
            expected_frame_count: 10,
            measured_frame_count: 10,
            missing_slots: 0,
            explicit_silence_frame_count: 0,
        });
        w.set_trace_time_axis(Some(crate::trace_alignment::TRACE_TIME_AXIS.to_string()));
        w.set_trace_clock(Some(TraceClock {
            basis: crate::trace_alignment::TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples: 0,
            end_position_samples: 48_000,
            sample_rate: 48_000,
            sources: vec!["project_timeline".to_string()],
        }));
        w.set_host_presentation_latency_observations(vec![HostPresentationLatencyObservation {
            source: Some("vst3".to_string()),
            input_samples: Some(0),
            output_samples: Some(0),
        }]);
        w.set_trace_wav_reference(Some(TraceWavReference {
            basis: crate::trace_alignment::TRACE_WAV_REFERENCE_BASIS.to_string(),
            capture_basis: crate::trace_alignment::TRACE_CAPTURE_BASIS.to_string(),
            start_sample: 0,
            end_sample: expected.expected_duration_samples,
            sample_rate: expected.expected_sample_rate,
            record_session_id: "session-pair-atomic".to_string(),
            bounce_id: expected.bounce_id,
            wav_hash: expected.wav_hash.expect("complete expected WAV hash"),
        }));
        for t_ms in (100_u64..=1_000).step_by(TRACE_FRAME_INTERVAL_MS as usize) {
            w.append_frame(t_ms, [0.0; 20], 0.0, -20.0, -1.0, 12.0, Some(10.0));
        }
        w.set_trace_slot_positions((4_800_i64..=48_000).step_by(4_800).collect());
        w.data.trace_pair_wav_start_basis =
            Some(crate::trace_alignment::TRACE_ALIGNMENT_START_BWF.to_string());
        w
    }

    #[test]
    fn matching_frame_count_without_slot_identity_is_never_complete() {
        let base = isolated_dir();
        let mut writer = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-count-only",
            Some("iid-pre-count-only".to_string()),
            None,
        );
        assert!(trace_slots_are_complete(&writer.data));

        writer.data.trace_slot_positions.clear();
        refresh_record_quality(&mut writer.data);

        assert!(!trace_slots_are_complete(&writer.data));
        assert_eq!(
            writer
                .data
                .record_quality
                .as_ref()
                .map(|quality| (quality.complete, quality.trace_slots_complete)),
            Some((false, false))
        );
    }

    #[test]
    fn usable_sparse_record_is_already_aligned_for_same_drop_retry() {
        let base = isolated_dir();
        let mut writer = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-sparse-idempotent",
            Some("iid-pre-sparse-idempotent".to_string()),
            None,
        );
        let expected = writer
            .data
            .expected_wav
            .clone()
            .expect("complete fixture expected WAV");
        writer.data.frames.remove(4);
        writer.data.trace_slot_positions.remove(4);
        writer.data.trace_diagnostics = Some(TraceDiagnostics {
            raw_trace_count: 9,
            expected_frame_count: 10,
            measured_frame_count: 9,
            missing_slots: 1,
            explicit_silence_frame_count: 0,
        });
        writer.data.integrity_degraded = true;
        writer
            .data
            .integrity_reasons
            .push("missing_trace_slots".to_string());
        refresh_record_quality(&mut writer.data);

        let quality = writer.data.record_quality.as_ref().expect("record quality");
        assert!(quality.usable);
        assert!(!quality.complete);
        assert!(!quality.trace_slots_complete);
        assert!(late_expected_record_is_aligned(&writer.data, &expected));
    }

    fn latency_mapped_96k_data(start_sample: i64, frame_count: usize) -> PluginDataFile {
        let base = isolated_dir();
        let mut writer = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-latency-phase",
            None,
            Some("iid-post-latency-phase".to_string()),
        );
        let mut expected = fresh_expected_wav_fixture(1_440_000);
        expected.expected_sample_rate = 96_000;
        expected.wav_time_reference_samples = Some(6_480_000);
        expected.wav_file_size = Some(1_440_000 * 8 + 44);
        writer.data.sample_rate = 96_000;
        writer.data.source_format = 96_000;
        writer.data.expected_wav = Some(expected.clone());
        writer.data.status = Status::Closed;
        writer.data.commit_status = Some("committed".to_string());
        writer.data.validity = true;
        writer.data.integrity_degraded = false;
        writer.data.integrity_reasons.clear();
        writer.data.trace_clock_resolution = Some("producer_plus_output_latency".to_string());
        writer.data.trace_time_axis = Some(crate::trace_alignment::TRACE_TIME_AXIS.to_string());
        writer.data.trace_slot_positions = (0..frame_count)
            .map(|index| start_sample + index as i64 * 9_600)
            .collect();
        writer.data.frames = writer
            .data
            .trace_slot_positions
            .iter()
            .map(|position| {
                make_frame_optional(
                    u64::try_from(*position).unwrap() * 1_000 / 96_000,
                    Some([0.0; 20]),
                    Some(0.0),
                    -20.0,
                    -1.0,
                    12.0,
                    Some(10.0),
                )
            })
            .collect();
        let output_latency = u32::try_from(start_sample.saturating_sub(9_600)).unwrap();
        writer.data.trace_clock_observations = writer
            .data
            .trace_slot_positions
            .iter()
            .zip(&writer.data.frames)
            .enumerate()
            .map(|(index, (wav_slot, frame))| {
                let mut source_frame = frame.clone();
                source_frame.t_ms = (index as u64 + 1) * TRACE_FRAME_INTERVAL_MS;
                TraceClockObservation {
                    frame: source_frame,
                    producer_position_samples: Some(
                        6_480_000 + *wav_slot - i64::from(output_latency),
                    ),
                    raw_host_position_samples: Some(6_480_000 + *wav_slot),
                    capture_epoch: Some(1),
                    clock_source: Some("project_timeline".to_string()),
                    presentation_latency_source: Some("vst3".to_string()),
                    input_presentation_latency_samples: Some(0),
                    output_presentation_latency_samples: Some(output_latency),
                }
            })
            .collect();
        writer.data.trace_wav_reference = Some(TraceWavReference {
            basis: crate::trace_alignment::TRACE_WAV_REFERENCE_BASIS.to_string(),
            capture_basis: crate::trace_alignment::TRACE_CAPTURE_BASIS.to_string(),
            start_sample: 0,
            end_sample: expected.expected_duration_samples,
            sample_rate: expected.expected_sample_rate,
            record_session_id: writer
                .data
                .record_session_id
                .clone()
                .expect("fixture record session"),
            bounce_id: expected.bounce_id,
            wav_hash: expected.wav_hash.expect("fixture WAV hash"),
        });
        writer.data.bounce_take = Some(BounceTake {
            source: BOUNCE_SOURCE_EXPECTED_WAV.to_string(),
            time_axis: "native_samples".to_string(),
            alignment_status: BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY.to_string(),
            sample_rate: 96_000,
            wav_start_sample: 0,
            wav_end_sample: 1_440_000,
            duration_samples: 1_440_000,
            duration_frames_48k: 720_000,
            start_t_ms: 0,
            end_t_ms: 15_000,
            trace_sample_count: frame_count as u64 + 4,
            frame_count: frame_count as u64,
            host_start_position_samples: Some(6_459_473),
            host_end_position_samples: Some(7_921_745),
        });
        writer.data.trace_diagnostics = Some(TraceDiagnostics {
            raw_trace_count: frame_count as u64 + 4,
            expected_frame_count: frame_count as u64,
            measured_frame_count: frame_count as u64,
            missing_slots: 0,
            explicit_silence_frame_count: 0,
        });
        writer.data
    }

    #[test]
    fn latency_mapped_phase_edges_are_complete_without_inventing_a_150th_frame() {
        let mut data = latency_mapped_96k_data(13_874, 149);
        data.trace_clock_observations.push(TraceClockObservation {
            frame: make_frame_optional(
                1,
                Some([0.0; 20]),
                Some(0.0),
                -20.0,
                -1.0,
                12.0,
                Some(10.0),
            ),
            producer_position_samples: None,
            raw_host_position_samples: None,
            capture_epoch: None,
            clock_source: None,
            presentation_latency_source: None,
            input_presentation_latency_samples: None,
            output_presentation_latency_samples: None,
        });
        let coverage = latency_mapped_trace_coverage(&data, 1_440_000, 96_000)
            .expect("factual latency-mapped grid");
        assert_eq!(
            coverage,
            LatencyMappedTraceCoverage {
                expected_frame_count: 149,
                measured_frame_count: 149,
                missing_slots: 0,
            }
        );
        assert!(trace_slot_identity_is_complete(&data));
        refresh_record_quality(&mut data);
        assert_eq!(
            data.record_quality.as_ref().map(|quality| (
                quality.status.as_str(),
                quality.expected_frame_count,
                quality.measured_frame_count,
                quality.missing_trace_slots,
            )),
            Some(("complete", 149, 149, 0))
        );
    }

    #[test]
    fn latency_mapped_grid_keeps_real_head_middle_and_tail_gaps_strict() {
        let complete = latency_mapped_96k_data(13_874, 149);
        for missing_index in [0, 74, 148] {
            let mut missing = complete.clone();
            missing.frames.remove(missing_index);
            missing.trace_slot_positions.remove(missing_index);
            let coverage = latency_mapped_trace_coverage(&missing, 1_440_000, 96_000)
                .expect("remaining factual mapped slots");
            assert_eq!(coverage.expected_frame_count, 149);
            assert_eq!(coverage.measured_frame_count, 148);
            assert_eq!(coverage.missing_slots, 1);
            assert!(!trace_slot_identity_is_complete(&missing));
        }
    }

    #[test]
    fn latency_epoch_change_does_not_become_a_missing_measurement() {
        let mut data = latency_mapped_96k_data(13_874, 148);
        for index in 1..data.trace_slot_positions.len() {
            data.trace_slot_positions[index] += 6_000;
            data.frames[index].t_ms =
                u64::try_from(data.trace_slot_positions[index]).unwrap() * 1_000 / 96_000;
            data.trace_clock_observations[index].output_presentation_latency_samples = Some(10_274);
        }

        let coverage = latency_mapped_trace_coverage(&data, 1_440_000, 96_000)
            .expect("piecewise factual latency mapping");
        assert_eq!(
            coverage,
            LatencyMappedTraceCoverage {
                expected_frame_count: 148,
                measured_frame_count: 148,
                missing_slots: 0,
            }
        );

        data.frames.remove(74);
        data.trace_slot_positions.remove(74);
        let missing = latency_mapped_trace_coverage(&data, 1_440_000, 96_000)
            .expect("piecewise mapping with one factual gap");
        assert_eq!(missing.expected_frame_count, 148);
        assert_eq!(missing.measured_frame_count, 147);
        assert_eq!(missing.missing_slots, 1);
    }

    #[test]
    fn latency_mapped_normalization_replaces_the_old_false_missing_diagnostic() {
        let mut data = latency_mapped_96k_data(13_874, 149);
        let expected = data.expected_wav.clone().expect("latched expected WAV");
        data.trace_diagnostics = Some(TraceDiagnostics {
            raw_trace_count: 153,
            expected_frame_count: 150,
            measured_frame_count: 149,
            missing_slots: 1,
            explicit_silence_frame_count: 0,
        });
        data.validity = false;
        data.integrity_degraded = true;
        data.integrity_reasons = vec![
            "validity_false".to_string(),
            "integrity_degraded".to_string(),
            "missing_trace_slots".to_string(),
            "trace_frame_count_mismatch".to_string(),
            "trace_frame_payload_count_bounce_take_mismatch".to_string(),
            "trace_slots_not_complete".to_string(),
        ];

        assert!(normalize_late_expected_record(&mut data, &expected));
        assert_eq!(
            data.trace_diagnostics.as_ref().map(|diagnostics| (
                diagnostics.raw_trace_count,
                diagnostics.expected_frame_count,
                diagnostics.measured_frame_count,
                diagnostics.missing_slots,
            )),
            Some((153, 149, 149, 0))
        );
        assert!(data.validity);
        assert!(!data.integrity_degraded);
        assert!(data.integrity_reasons.is_empty());
        assert!(normal_publish_failure_reasons(&data).is_empty());
        assert_eq!(
            data.record_quality
                .as_ref()
                .map(|quality| (quality.status.as_str(), quality.complete)),
            Some(("complete", true))
        );
    }

    #[test]
    fn latency_mapped_pair_allows_factual_role_specific_edge_counts() {
        let mut pre = latency_mapped_96k_data(13_874, 149);
        pre.instance_id = "iid-pre-latency-phase".to_string();
        pre.paired_post_instance_id = Some("iid-post-latency-phase".to_string());
        pre.paired_pre_instance_id = None;

        let mut post = latency_mapped_96k_data(9_600, 150);
        post.role = Role::Post;
        post.instance_id = "iid-post-latency-phase".to_string();
        post.paired_pre_instance_id = Some("iid-pre-latency-phase".to_string());
        post.paired_post_instance_id = None;
        post.project_hash.clone_from(&pre.project_hash);
        post.record_session_id.clone_from(&pre.record_session_id);
        post.expected_wav.clone_from(&pre.expected_wav);
        post.trace_wav_reference
            .clone_from(&pre.trace_wav_reference);
        if let Some(reference) = post.trace_wav_reference.as_mut() {
            reference.record_session_id = post
                .record_session_id
                .clone()
                .expect("shared record session");
        }
        refresh_record_quality(&mut pre);
        refresh_record_quality(&mut post);

        assert!(normal_publish_failure_reasons(&pre).is_empty());
        assert!(normal_publish_failure_reasons(&post).is_empty());
        assert!(pair_publish_failure_reasons(&pre, &post).is_empty());
        assert!(pair_publish_integrity_reasons(&pre, &post).is_empty());
    }

    const RELEASE_GATE_SAMPLE_RATE: u32 = 96_000;
    const RELEASE_GATE_DURATION_SAMPLES: u64 = 1_440_000;
    const RELEASE_GATE_SLOT_SAMPLES: i64 = 9_600;
    const RELEASE_GATE_FRAME_COUNT: usize = 150;
    const RELEASE_GATE_WAV_START: i64 = 6_480_000;
    const RELEASE_GATE_PRESENTATION_LATENCY: u32 = 7_676;

    struct ReleaseGateClockFixture {
        epoch: u64,
        exact_slots: Vec<i64>,
        raw_wav_range: HostClockRange,
    }

    /// Exercise the Audio-Thread clock producer instead of manufacturing already-shifted slots.
    /// Keep's producer barrier has already armed every PRE/POST Record lane before this current
    /// render pass begins. A prior, distant pass remains in the tracker only to prove that a
    /// transport rewind creates a separate immutable epoch and cannot supply a missing slot.
    fn release_gate_clock_fixture(
        presentation_source: crate::record_take::PresentationLatencySource,
        output_latency: u32,
    ) -> ReleaseGateClockFixture {
        use crate::record_take::{
            CaptureClockSource, PresentationLatencySamples, RecordTakeTracker,
        };

        let tracker = RecordTakeTracker::new();
        let latency = PresentationLatencySamples {
            source: presentation_source,
            input: Some(0),
            output: Some(output_latency),
        };
        let raw_wav_start = RELEASE_GATE_WAV_START + i64::from(output_latency);
        let raw_wav_end = raw_wav_start + RELEASE_GATE_DURATION_SAMPLES as i64;
        let old_raw_start = raw_wav_start + RELEASE_GATE_DURATION_SAMPLES as i64 * 4;

        tracker.note_capture_window_with_presentation(
            true,
            old_raw_start,
            RELEASE_GATE_DURATION_SAMPLES,
            CaptureClockSource::ProjectTimeline,
            latency,
        );
        let old_epoch = tracker
            .clock_point_for_raw_host_position(old_raw_start + RELEASE_GATE_SLOT_SAMPLES)
            .expect("old render epoch")
            .epoch;

        tracker.note_capture_window_with_presentation(
            true,
            raw_wav_start,
            RELEASE_GATE_DURATION_SAMPLES,
            CaptureClockSource::ProjectTimeline,
            latency,
        );
        let current_start = tracker
            .clock_point_for_raw_host_position(raw_wav_start)
            .expect("current output-presentation WAV start");
        assert_eq!(current_start.position_samples, RELEASE_GATE_WAV_START);
        assert_eq!(current_start.raw_host_position_samples, raw_wav_start);
        assert_eq!(
            current_start
                .raw_host_position_samples
                .saturating_sub(current_start.position_samples),
            i64::from(output_latency),
            "latency is applied once at the producer boundary"
        );
        assert_ne!(
            old_epoch, current_start.epoch,
            "rewind must open a new epoch"
        );
        assert_eq!(
            tracker.capture_epoch_for_raw_host_range(raw_wav_start, raw_wav_end),
            Some(current_start.epoch)
        );
        assert_eq!(
            tracker.presentation_range_for_capture_epoch(
                current_start.epoch,
                raw_wav_start,
                raw_wav_end,
            ),
            Some((
                RELEASE_GATE_WAV_START,
                RELEASE_GATE_WAV_START + RELEASE_GATE_DURATION_SAMPLES as i64,
            ))
        );
        assert!(
            tracker
                .capture_epoch_for_raw_host_range(raw_wav_start, old_raw_start + 1)
                .is_none(),
            "a range crossing the transport jump must never become one WAV take"
        );

        let exact_slots = (1..=RELEASE_GATE_FRAME_COUNT)
            .map(|index| {
                let raw_position = raw_wav_start + index as i64 * RELEASE_GATE_SLOT_SAMPLES;
                let point = tracker
                    .clock_point_for_raw_host_position(raw_position)
                    .expect("current render slot");
                assert_eq!(point.epoch, current_start.epoch);
                assert_eq!(
                    point.position_samples,
                    RELEASE_GATE_WAV_START + index as i64 * RELEASE_GATE_SLOT_SAMPLES
                );
                point.position_samples
            })
            .collect();
        let old_point = tracker
            .clock_point_for_raw_host_position(old_raw_start + RELEASE_GATE_SLOT_SAMPLES)
            .expect("old render slot retained for audit");
        assert_eq!(old_point.epoch, old_epoch);

        ReleaseGateClockFixture {
            epoch: current_start.epoch,
            exact_slots,
            raw_wav_range: HostClockRange {
                start_position_samples: raw_wav_start,
                end_position_samples: raw_wav_end,
            },
        }
    }

    fn release_gate_expected_wav(session_id: &str) -> ExpectedWavMetadata {
        ExpectedWavMetadata {
            expected_duration_samples: RELEASE_GATE_DURATION_SAMPLES,
            expected_sample_rate: RELEASE_GATE_SAMPLE_RATE,
            wav_time_reference_samples: Some(RELEASE_GATE_WAV_START as u64),
            wav_path: format!("/tmp/{session_id}.wav"),
            bounce_id: format!("bounce-{session_id}"),
            created_at_ms: 1_800_000_000_000,
            wav_file_size: Some(RELEASE_GATE_DURATION_SAMPLES * 8 + 44),
            wav_mtime_ms: 1_800_000_000_000,
            wav_hash: Some(format!("hash-{session_id}")),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn release_gate_pair_writer(
        base: &Path,
        generation: &crate::capture_generation::CaptureGeneration,
        member: &crate::capture_generation::CaptureGenerationMember,
        role: Role,
        presentation_source: crate::record_take::PresentationLatencySource,
        output_latency: u32,
        missing_exact_slot: Option<usize>,
    ) -> PluginDataWriter {
        let instance_id = match role {
            Role::Pre => member.pre_instance_id.as_str(),
            Role::Post => member.post_instance_id.as_str(),
        };
        let paths = WriterPaths::build(
            base,
            &member.project_hash,
            instance_id,
            role,
            "2026-07-15T00:00:00.000Z",
        );
        let mut writer = PluginDataWriter::create(
            paths,
            "release-gate-installation".to_string(),
            member.project_hash.clone(),
            instance_id.to_string(),
            role,
            None,
            RELEASE_GATE_SAMPLE_RATE,
            (role == Role::Post).then(|| member.pre_instance_id.clone()),
            (role == Role::Pre).then(|| member.post_instance_id.clone()),
            None,
            None,
        )
        .unwrap();
        writer.set_record_start_wall_clock("2026-07-15T00:00:00.000Z".to_string());
        writer.set_record_session_id(Some(member.record_session_id.clone()));
        writer.set_capture_generation(
            Some(generation.capture_generation_id.clone()),
            generation.started_at_ms,
        );
        let expected = release_gate_expected_wav(&member.record_session_id);
        writer.set_expected_wav(Some(expected));

        let clock = release_gate_clock_fixture(presentation_source, output_latency);
        assert!(clock.epoch > 0);
        let source = presentation_source
            .as_str()
            .expect("fixture uses AU or VST3");
        writer.set_host_presentation_latency_observations(vec![
            HostPresentationLatencyObservation {
                source: Some(source.to_string()),
                input_samples: Some(0),
                output_samples: Some(output_latency),
            },
        ]);
        writer.set_raw_host_clock_range(Some(clock.raw_wav_range));
        writer.set_trace_clock(Some(TraceClock {
            basis: crate::trace_alignment::TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples: RELEASE_GATE_WAV_START,
            end_position_samples: RELEASE_GATE_WAV_START + RELEASE_GATE_DURATION_SAMPLES as i64,
            sample_rate: RELEASE_GATE_SAMPLE_RATE,
            sources: vec!["project_timeline".to_string()],
        }));
        writer.set_trace_time_axis(Some(
            crate::trace_alignment::TRACE_HOST_TIME_AXIS.to_string(),
        ));

        let exact_positions: Vec<(usize, i64)> = clock
            .exact_slots
            .into_iter()
            .enumerate()
            .filter(|(index, _)| Some(*index) != missing_exact_slot)
            .collect();
        let provisional_count = exact_positions.len();
        for (index, _) in &exact_positions {
            let value = match role {
                Role::Pre => -30.0 + *index as f64 * 0.01,
                Role::Post => -20.0 + *index as f64 * 0.02,
            };
            writer.append_frame(
                (*index as u64 + 1) * TRACE_FRAME_INTERVAL_MS,
                [0.0; 20],
                0.0,
                value,
                value + 10.0,
                12.0,
                Some(9.0),
            );
        }
        writer.set_trace_clock_observations(
            exact_positions
                .iter()
                .zip(&writer.data.frames)
                .map(|((_, position), frame)| TraceClockObservation {
                    frame: frame.clone(),
                    producer_position_samples: Some(*position),
                    raw_host_position_samples: position.checked_add(i64::from(output_latency)),
                    capture_epoch: Some(clock.epoch),
                    clock_source: Some("project_timeline".to_string()),
                    presentation_latency_source: Some(source.to_string()),
                    input_presentation_latency_samples: Some(0),
                    output_presentation_latency_samples: Some(output_latency),
                })
                .collect(),
        );
        writer.set_trace_slot_positions(
            exact_positions
                .iter()
                .map(|(_, position)| *position)
                .collect(),
        );
        writer.set_bounce_take(BounceTake {
            source: BOUNCE_SOURCE_RENDER_CLOCK.to_string(),
            time_axis: "native_samples".to_string(),
            alignment_status: BOUNCE_ALIGNMENT_SAMPLE_COUNT_READY.to_string(),
            sample_rate: RELEASE_GATE_SAMPLE_RATE,
            wav_start_sample: 0,
            wav_end_sample: RELEASE_GATE_DURATION_SAMPLES,
            duration_samples: RELEASE_GATE_DURATION_SAMPLES,
            duration_frames_48k: RELEASE_GATE_DURATION_SAMPLES / 2,
            start_t_ms: 0,
            end_t_ms: 15_000,
            trace_sample_count: provisional_count as u64,
            frame_count: provisional_count as u64,
            host_start_position_samples: Some(clock.raw_wav_range.start_position_samples),
            host_end_position_samples: Some(clock.raw_wav_range.end_position_samples),
        });
        writer.set_trace_diagnostics(TraceDiagnostics {
            raw_trace_count: provisional_count as u64,
            expected_frame_count: RELEASE_GATE_FRAME_COUNT as u64,
            measured_frame_count: provisional_count as u64,
            missing_slots: (RELEASE_GATE_FRAME_COUNT - provisional_count) as u64,
            explicit_silence_frame_count: 0,
        });
        writer.data.status = Status::Closed;
        writer.data.commit_status = Some("pair_pending".to_string());
        writer
    }

    fn release_gate_mapped_wav_slots(
        output_latency: u32,
        missing_exact_slot: Option<usize>,
    ) -> Vec<i64> {
        (1..=RELEASE_GATE_FRAME_COUNT)
            .enumerate()
            .filter(|(index, _)| Some(*index) != missing_exact_slot)
            .filter_map(|(_, slot)| {
                let position = (slot as i64)
                    .checked_mul(RELEASE_GATE_SLOT_SAMPLES)?
                    .checked_add(i64::from(output_latency))?;
                (position <= RELEASE_GATE_DURATION_SAMPLES as i64).then_some(position)
            })
            .collect()
    }

    fn stage_and_finalize_release_gate_pair(
        pre: &mut PluginDataWriter,
        post: &mut PluginDataWriter,
    ) {
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path).unwrap();
        post.write_atomic(post_paths.pair_pending_path).unwrap();
        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();
    }

    fn release_gate_consumer_members<'a>(
        base: &Path,
        generation: &'a crate::capture_generation::CaptureGeneration,
    ) -> Vec<&'a crate::capture_generation::CaptureGenerationMember> {
        let ready = generation.members.iter().all(|member| {
            pair_record_session_manifest_exists(
                base,
                &member.project_hash,
                &member.record_session_id,
            )
        });
        if ready {
            generation.members.iter().collect()
        } else {
            Vec::new()
        }
    }

    fn release_gate_generation(
        member_count: usize,
    ) -> crate::capture_generation::CaptureGeneration {
        use crate::capture_generation::{CaptureGeneration, CaptureGenerationMember};
        assert!((1..=crate::capture_contract::MAX_CAPTURE_PAIRS).contains(&member_count));
        CaptureGeneration::new_for_named_members(
            "post-channel-00".to_string(),
            "studio-one-release-gate".to_string(),
            std::process::id(),
            (0..member_count)
                .map(|index| {
                    let key = format!("channel-{index:02}");
                    let name = match index {
                        0 => "2Mix".to_string(),
                        1 => "Drum".to_string(),
                        2 => "Music".to_string(),
                        _ => format!("Stem {index:02}"),
                    };
                    (
                        CaptureGenerationMember {
                            project_hash: "release-gate-project".to_string(),
                            post_instance_id: format!("post-{key}"),
                            pre_instance_id: format!("pre-{key}"),
                            record_session_id: String::new(),
                        },
                        Some(name),
                    )
                })
                .collect(),
        )
    }

    fn release_gate_format(
        index: usize,
    ) -> (
        crate::record_take::PresentationLatencySource,
        u32,
        crate::record_take::PresentationLatencySource,
        u32,
    ) {
        use crate::record_take::PresentationLatencySource::{AudioUnitV2, Vst3};
        match index % 3 {
            0 => (AudioUnitV2, RELEASE_GATE_PRESENTATION_LATENCY, Vst3, 0),
            1 => (Vst3, RELEASE_GATE_PRESENTATION_LATENCY, AudioUnitV2, 0),
            _ => (AudioUnitV2, 0, Vst3, RELEASE_GATE_PRESENTATION_LATENCY),
        }
    }

    fn assert_release_gate_producer_side(writer: &PluginDataWriter, expected_count: usize) {
        assert_eq!(writer.data.frames.len(), expected_count);
        assert_eq!(writer.data.trace_slot_positions.len(), expected_count);
        assert!(writer.data.trace_context_frames.is_empty());
        assert!(writer.data.trace_context_slot_positions.is_empty());
        if expected_count == RELEASE_GATE_FRAME_COUNT {
            assert_eq!(
                writer.data.trace_slot_positions.first(),
                Some(&(RELEASE_GATE_WAV_START + RELEASE_GATE_SLOT_SAMPLES))
            );
            assert_eq!(
                writer.data.trace_slot_positions.last(),
                Some(&(RELEASE_GATE_WAV_START + RELEASE_GATE_DURATION_SAMPLES as i64))
            );
            assert_eq!(
                writer.data.trace_diagnostics.as_ref().map(|diagnostics| (
                    diagnostics.expected_frame_count,
                    diagnostics.measured_frame_count,
                    diagnostics.missing_slots,
                )),
                Some((
                    RELEASE_GATE_FRAME_COUNT as u64,
                    RELEASE_GATE_FRAME_COUNT as u64,
                    0,
                ))
            );
        }
    }

    #[test]
    fn release_gate_three_pairs_require_exact_96k_15s_presentation_sets() {
        let complete_root = isolated_dir();
        let complete_generation = release_gate_generation(3);
        for (member, (pre_source, pre_latency, post_source, post_latency)) in complete_generation
            .members
            .iter()
            .enumerate()
            .map(|(index, member)| (member, release_gate_format(index)))
        {
            let mut pre = release_gate_pair_writer(
                &complete_root,
                &complete_generation,
                member,
                Role::Pre,
                pre_source,
                pre_latency,
                None,
            );
            let mut post = release_gate_pair_writer(
                &complete_root,
                &complete_generation,
                member,
                Role::Post,
                post_source,
                post_latency,
                None,
            );
            assert_release_gate_producer_side(&pre, RELEASE_GATE_FRAME_COUNT);
            assert_release_gate_producer_side(&post, RELEASE_GATE_FRAME_COUNT);
            stage_and_finalize_release_gate_pair(&mut pre, &mut post);

            let manifest_path = pair_commit_manifest_path(&pre.paths, &pre.data).unwrap();
            let manifest: PairCommitManifest =
                serde_json::from_slice(&fs::read(&manifest_path).unwrap()).unwrap();
            assert_eq!(
                manifest.capture_generation_id,
                complete_generation.capture_generation_id
            );
            for (member_path, expected_source, expected_latency) in [
                (manifest.pre.path, pre_source, pre_latency),
                (manifest.post.path, post_source, post_latency),
            ] {
                let published: PluginDataFile =
                    serde_json::from_slice(&fs::read(member_path).unwrap()).unwrap();
                let expected_slots = release_gate_mapped_wav_slots(expected_latency, None);
                assert_eq!(published.frames.len(), expected_slots.len());
                assert_eq!(published.trace_slot_positions, expected_slots);
                assert_eq!(
                    published
                        .trace_content_alignment
                        .as_ref()
                        .map(|alignment| alignment.method.as_str()),
                    Some(crate::trace_alignment::TRACE_ALIGNMENT_METHOD)
                );
                assert!(published.trace_context_frames.is_empty());
                assert!(published.trace_context_slot_positions.is_empty());
                assert_eq!(
                    published
                        .record_quality
                        .as_ref()
                        .map(|quality| (quality.complete, quality.measured_frame_count)),
                    Some((true, published.frames.len() as u64))
                );
                assert_eq!(
                    published.host_presentation_latency_observations,
                    vec![HostPresentationLatencyObservation {
                        source: expected_source.as_str().map(str::to_string),
                        input_samples: Some(0),
                        output_samples: Some(expected_latency),
                    }]
                );
                let raw_range = published
                    .raw_host_clock_range
                    .expect("raw host range audit");
                assert_eq!(
                    raw_range.start_position_samples - i64::from(expected_latency),
                    RELEASE_GATE_WAV_START,
                    "raw host and output-presentation coordinates stay separately auditable"
                );
                let trace_clock = published.trace_clock.as_ref().expect("presentation clock");
                assert_eq!(trace_clock.origin_position_samples, RELEASE_GATE_WAV_START);
                assert_eq!(
                    trace_clock.end_position_samples,
                    RELEASE_GATE_WAV_START + RELEASE_GATE_DURATION_SAMPLES as i64
                );
                let expected_first = if published.role == Role::Pre {
                    -30.0
                } else {
                    -20.0
                };
                assert_eq!(published.frames[0].lufs_m, expected_first);
            }
        }
        assert_eq!(
            release_gate_consumer_members(&complete_root, &complete_generation).len(),
            3,
            "all three exact pair manifests become one consumer-ready generation"
        );

        let incomplete_root = isolated_dir();
        let incomplete_generation = release_gate_generation(3);
        for (member_index, (member, (pre_source, pre_latency, post_source, post_latency))) in
            incomplete_generation
                .members
                .iter()
                .enumerate()
                .map(|(index, member)| (index, (member, release_gate_format(index))))
        {
            let mut pre = release_gate_pair_writer(
                &incomplete_root,
                &incomplete_generation,
                member,
                Role::Pre,
                pre_source,
                pre_latency,
                None,
            );
            let mut post = release_gate_pair_writer(
                &incomplete_root,
                &incomplete_generation,
                member,
                Role::Post,
                post_source,
                post_latency,
                (member_index == 1).then_some(74),
            );
            assert_release_gate_producer_side(&pre, RELEASE_GATE_FRAME_COUNT);
            assert_release_gate_producer_side(
                &post,
                if member_index == 1 {
                    RELEASE_GATE_FRAME_COUNT - 1
                } else {
                    RELEASE_GATE_FRAME_COUNT
                },
            );
            stage_and_finalize_release_gate_pair(&mut pre, &mut post);
        }
        assert_eq!(
            release_gate_consumer_members(&incomplete_root, &incomplete_generation).len(),
            3,
            "149/150 lowers timing quality but keeps the complete generation roster visible"
        );
        let missing_member = &incomplete_generation.members[1];
        assert!(
            pair_record_session_manifest_exists(
                &incomplete_root,
                &missing_member.project_hash,
                &missing_member.record_session_id,
            ),
            "the measured fallback pair must cross the identity manifest barrier"
        );
        let manifest_path = incomplete_root
            .join(&missing_member.project_hash)
            .join(PAIR_RECORD_SESSIONS_DIR)
            .join(format!("{}.json", missing_member.record_session_id));
        let manifest: PairCommitManifest =
            serde_json::from_slice(&fs::read(manifest_path).unwrap()).unwrap();
        let published_post: PluginDataFile =
            serde_json::from_slice(&fs::read(manifest.post.path).unwrap()).unwrap();
        assert_eq!(published_post.frames.len(), 149);
        assert_eq!(
            published_post.record_quality.as_ref().map(|quality| (
                quality.status.as_str(),
                quality.expected_frame_count,
                quality.measured_frame_count,
                quality.missing_trace_slots,
            )),
            Some(("usable_fallback", 150, 149, 1))
        );
    }

    #[test]
    fn release_gate_one_and_twelve_pair_rosters_are_exact_before_any_consumer_visibility() {
        for member_count in [1_usize, crate::capture_contract::MAX_CAPTURE_PAIRS] {
            let root = isolated_dir();
            let generation = release_gate_generation(member_count);
            assert!(release_gate_consumer_members(&root, &generation).is_empty());

            for (index, member) in generation.members.iter().enumerate() {
                let (pre_source, pre_latency, post_source, post_latency) =
                    release_gate_format(index);
                let mut pre = release_gate_pair_writer(
                    &root,
                    &generation,
                    member,
                    Role::Pre,
                    pre_source,
                    pre_latency,
                    None,
                );
                let mut post = release_gate_pair_writer(
                    &root,
                    &generation,
                    member,
                    Role::Post,
                    post_source,
                    post_latency,
                    None,
                );
                assert_release_gate_producer_side(&pre, RELEASE_GATE_FRAME_COUNT);
                assert_release_gate_producer_side(&post, RELEASE_GATE_FRAME_COUNT);
                stage_and_finalize_release_gate_pair(&mut pre, &mut post);

                let expected_visible = if index + 1 == member_count {
                    member_count
                } else {
                    0
                };
                assert_eq!(
                    release_gate_consumer_members(&root, &generation).len(),
                    expected_visible,
                    "a {member_count}-pair generation is visible only after its last exact pair"
                );
            }
        }
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

    #[test]
    fn total_loudness_n_is_persisted_without_reconstructing_it_from_n_prime() {
        let frame = make_frame_optional_with_n(
            100,
            Some([99.0; 20]),
            Some(4.256),
            Some(1.0),
            -14.0,
            -1.0,
            12.0,
            None,
        );
        assert_eq!(frame.n_prime_total, Some(4.26));
        let json = serde_json::to_string(&frame).unwrap();
        assert!(json.contains("\"n_prime_total\":4.26"));

        let legacy: Frame = serde_json::from_str(
            r#"{"t_ms":100,"n_prime":null,"sharpness":null,"lufs_m":-14.0,"true_peak":-1.0,"crest":12.0}"#,
        )
        .unwrap();
        assert_eq!(legacy.n_prime_total, None);
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
            host_start_position_samples: None,
            host_end_position_samples: None,
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
        for writer in [&mut pre, &mut post] {
            writer.data.trace_clock_resolution = Some("producer_plus_output_latency".to_string());
        }
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
            let alignment = data
                .trace_content_alignment
                .as_ref()
                .expect("pair publish must stamp content alignment");
            assert_eq!(
                alignment.status,
                crate::trace_alignment::TRACE_ALIGNMENT_STATUS
            );
            assert_eq!(
                alignment.method,
                crate::trace_alignment::TRACE_ALIGNMENT_METHOD
            );
            assert_eq!(alignment.confidence, 1.0);
            assert_eq!(
                alignment.start_basis.as_deref(),
                Some(crate::trace_alignment::TRACE_ALIGNMENT_START_BWF)
            );
            assert!(alignment.pre_to_post_offset_samples.is_none());
            assert!(data.trace_pair_wav_start_basis.is_none());
            assert!(data.trace_pair_offset_samples.is_none());
            assert!(data.trace_pair_alignment_score.is_none());
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
        let stored_expected: ExpectedWavMetadata = serde_json::from_slice(
            &fs::read(crate::record_expected::expected_path(
                &base,
                "project_hash_test",
            ))
            .unwrap(),
        )
        .unwrap();
        assert_eq!(stored_expected, expected_wav_fixture());
    }

    /// Timing diagnostics may be incomplete, but an identity-complete measured pair remains a
    /// visible fallback and still closes the Keep claim marker.
    #[test]
    fn closed_identity_complete_claim_remains_a_trace_candidate() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-failed-claim",
            None,
            Some("iid-post-failed-claim".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-failed-claim",
            Some("iid-pre-failed-claim".to_string()),
            None,
        );
        // POST 側の時刻診断だけを意図的に崩す。計測値とWAV identityは残る。
        post.data.trace_diagnostics = None;
        post.data.bounce_take = None;

        // claim_expected_metadata_for_session は fresh (10分以内) な current.json しか
        // claim できないため、`expected_wav_fixture()` の固定 created_at_ms ではなく
        // 現在時刻を使う（bounce_id は complete_pair_writer が焼いた値と一致させる）。
        let now_ms = chrono::Utc::now().timestamp_millis();
        let mut fresh_expected = expected_wav_fixture();
        fresh_expected.created_at_ms = now_ms;
        fresh_expected.wav_mtime_ms = now_ms;
        crate::record_expected::write_expected_metadata(
            &base,
            "project_hash_test",
            &fresh_expected,
        )
        .unwrap();
        // Keep 相当: claim marker を open (closed_at_ms=None) として作る。
        crate::record_expected::claim_expected_metadata_for_session(
            &base,
            "project_hash_test",
            "session-pair-atomic",
        )
        .unwrap();

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

        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());
        assert!(pre_paths.member_path.exists() && post_paths.member_path.exists());
        let manifest_path = pair_commit_manifest_path(&pre.paths, &pre.data).unwrap();
        assert!(manifest_path.exists());

        let closed_at_ms = crate::record_expected::claim_marker_closed_at_ms_for_session(
            &base,
            "project_hash_test",
            "session-pair-atomic",
        )
        .unwrap()
        .expect("claim marker must still exist after a failed close");
        assert!(
            closed_at_ms.is_some(),
            "a failed close must still stamp closed_at_ms — otherwise Kirin OS cannot \
             distinguish it from a session that is still open (stop忘れ)"
        );

        let published: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        assert_eq!(published.commit_status.as_deref(), Some("committed"));
        assert_eq!(
            published
                .record_quality
                .as_ref()
                .map(|quality| quality.status.as_str()),
            Some("usable_fallback")
        );
        assert!(published.trace_content_alignment.is_none());
        assert!(verify_checksum(&published));
    }

    #[test]
    fn pair_finalize_preserves_same_second_repeat_trace_shelves() {
        let base = isolated_dir();
        let mut first_pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-repeat",
            None,
            Some("iid-post-repeat".to_string()),
        );
        let mut first_post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-repeat",
            Some("iid-pre-repeat".to_string()),
            None,
        );
        first_pre.set_record_session_id(Some("session-repeat-first".to_string()));
        first_post.set_record_session_id(Some("session-repeat-first".to_string()));
        first_pre.data.status = Status::Closed;
        first_pre.data.commit_status = Some("pair_pending".to_string());
        first_post.data.status = Status::Closed;
        first_post.data.commit_status = Some("pair_pending".to_string());
        crate::record_expected::write_expected_metadata(
            &base,
            "project_hash_test",
            &expected_wav_fixture(),
        )
        .unwrap();
        let first_pre_paths = self_paths_for(&first_pre.paths, &first_pre.data);
        let first_post_paths = self_paths_for(&first_post.paths, &first_post.data);
        let first_pre_trace_path =
            pair_trace_shelf_path(&first_pre_paths, &first_pre.data).unwrap();
        let first_post_trace_path =
            pair_trace_shelf_path(&first_post_paths, &first_post.data).unwrap();
        first_pre
            .write_atomic(first_pre_paths.pair_pending_path.clone())
            .unwrap();
        first_post
            .write_atomic(first_post_paths.pair_pending_path.clone())
            .unwrap();

        try_finalize_pair_session(&first_pre.paths, &first_pre.data).unwrap();

        let mut second_pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-repeat",
            None,
            Some("iid-post-repeat".to_string()),
        );
        let mut second_post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-repeat",
            Some("iid-pre-repeat".to_string()),
            None,
        );
        second_pre.set_record_session_id(Some("session-repeat-second".to_string()));
        second_post.set_record_session_id(Some("session-repeat-second".to_string()));
        second_pre.data.status = Status::Closed;
        second_pre.data.commit_status = Some("pair_pending".to_string());
        second_post.data.status = Status::Closed;
        second_post.data.commit_status = Some("pair_pending".to_string());
        crate::record_expected::write_expected_metadata(
            &base,
            "project_hash_test",
            &expected_wav_fixture(),
        )
        .unwrap();
        let second_pre_paths = self_paths_for(&second_pre.paths, &second_pre.data);
        let second_post_paths = self_paths_for(&second_post.paths, &second_post.data);
        let expected_second_pre_trace =
            pair_trace_shelf_path(&second_pre_paths, &second_pre.data).unwrap();
        let expected_second_post_trace =
            pair_trace_shelf_path(&second_post_paths, &second_post.data).unwrap();
        let second_stamp = trace_shelf_compact_at(&second_pre.data, 1).unwrap();
        let second_file_name = format!("{second_stamp}.json");
        assert_eq!(
            expected_second_pre_trace
                .file_name()
                .and_then(|name| name.to_str()),
            Some(second_file_name.as_str())
        );
        assert_eq!(
            expected_second_post_trace
                .file_name()
                .and_then(|name| name.to_str()),
            Some(second_file_name.as_str())
        );
        second_pre
            .write_atomic(second_pre_paths.pair_pending_path.clone())
            .unwrap();
        second_post
            .write_atomic(second_post_paths.pair_pending_path.clone())
            .unwrap();

        try_finalize_pair_session(&second_pre.paths, &second_pre.data).unwrap();

        assert!(first_pre_trace_path.exists());
        assert!(first_post_trace_path.exists());
        assert!(expected_second_pre_trace.exists());
        assert!(expected_second_post_trace.exists());
        assert_ne!(first_pre_trace_path, expected_second_pre_trace);
        assert_ne!(first_post_trace_path, expected_second_post_trace);
        assert!(first_pre_paths.member_path.exists());
        assert!(first_post_paths.member_path.exists());
        assert!(second_pre_paths.member_path.exists());
        assert!(second_post_paths.member_path.exists());

        let first_pre_trace: PluginDataFile =
            serde_json::from_slice(&fs::read(&first_pre_trace_path).unwrap()).unwrap();
        let first_post_trace: PluginDataFile =
            serde_json::from_slice(&fs::read(&first_post_trace_path).unwrap()).unwrap();
        let second_pre_trace: PluginDataFile =
            serde_json::from_slice(&fs::read(&expected_second_pre_trace).unwrap()).unwrap();
        let second_post_trace: PluginDataFile =
            serde_json::from_slice(&fs::read(&expected_second_post_trace).unwrap()).unwrap();
        assert_eq!(
            first_pre_trace.record_session_id.as_deref(),
            Some("session-repeat-first")
        );
        assert_eq!(
            first_post_trace.record_session_id.as_deref(),
            Some("session-repeat-first")
        );
        assert_eq!(
            second_pre_trace.record_session_id.as_deref(),
            Some("session-repeat-second")
        );
        assert_eq!(
            second_post_trace.record_session_id.as_deref(),
            Some("session-repeat-second")
        );
        for data in [
            &first_pre_trace,
            &first_post_trace,
            &second_pre_trace,
            &second_post_trace,
        ] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn pair_finalize_keeps_render_clock_pending_until_drop_supplies_wav_axis() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-render-no-expected",
            None,
            Some("iid-post-render-no-expected".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-render-no-expected",
            Some("iid-pre-render-no-expected".to_string()),
            None,
        );
        pre.set_expected_wav(None);
        post.set_expected_wav(None);
        for writer in [&mut pre, &mut post] {
            let mut take = writer.data.bounce_take.clone().expect("bounce_take");
            take.source = BOUNCE_SOURCE_RENDER_CLOCK.to_string();
            writer.set_bounce_take(take);
            writer.set_trace_time_axis(Some(
                crate::trace_alignment::TRACE_HOST_TIME_AXIS.to_string(),
            ));
            writer.set_trace_wav_reference(None);
        }
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
        assert!(!manifest_path.exists());
        assert!(!pre_paths.member_path.exists());
        assert!(!post_paths.member_path.exists());
        assert!(!pre_trace_path.exists());
        assert!(!post_trace_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());
        assert!(pre_paths.pair_pending_path.exists());
        assert!(post_paths.pair_pending_path.exists());
    }

    #[test]
    fn pair_finalize_quarantines_expected_wav_source_without_expected_metadata() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-source-expected-missing",
            None,
            Some("iid-post-source-expected-missing".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-source-expected-missing",
            Some("iid-pre-source-expected-missing".to_string()),
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
        assert!(!manifest_path.exists());
        assert!(!pre_paths.member_path.exists());
        assert!(!post_paths.member_path.exists());
        assert!(!pre_trace_path.exists());
        assert!(!post_trace_path.exists());
        assert!(pre_paths.failed_path.exists());
        assert!(post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.failed_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.failed_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("failed"));
            assert!(!data.validity);
            assert!(data.integrity_degraded);
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "missing_expected_wav_metadata"));
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "bounce_take_not_sample_count_ready"));
            let quality = data.record_quality.as_ref().expect("record_quality");
            assert_eq!(quality.status, "failed");
            assert!(!quality.complete);
            assert!(!quality.usable);
            assert!(!quality.sample_count_ready);
            assert!(verify_checksum(data));
        }
    }

    /// 2026-07-10 (バグ修正): `mark_pair_expected_metadata_consumed` は
    /// `data.expected_wav` が Close 時点で失われている（`missing_expected_wav_metadata`
    /// で `.failed` になる）場合でも、`record_session_id` さえ分かれば claim marker の
    /// `closed_at_ms` を必ず刻まなければならない。marker 自身が bounce_id を覚えている
    /// ため、close 側が `expected_wav` を持っているかどうかに依存してはならない
    /// （さもないと、この失敗理由で落ちたセッションだけが永遠に open 扱いのまま残る）。
    #[test]
    fn failed_close_without_expected_wav_still_stamps_closed_at_ms() {
        let base = isolated_dir();
        let now_ms = chrono::Utc::now().timestamp_millis();
        let mut fresh_expected = expected_wav_fixture();
        fresh_expected.created_at_ms = now_ms;
        fresh_expected.wav_mtime_ms = now_ms;
        crate::record_expected::write_expected_metadata(
            &base,
            "project_hash_test",
            &fresh_expected,
        )
        .unwrap();
        // Keep 相当: claim marker を open (closed_at_ms=None) として作る。
        crate::record_expected::claim_expected_metadata_for_session(
            &base,
            "project_hash_test",
            "session-pair-atomic",
        )
        .unwrap();

        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-no-expected-wav",
            None,
            Some("iid-post-no-expected-wav".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-no-expected-wav",
            Some("iid-pre-no-expected-wav".to_string()),
            None,
        );
        // Close 時点では expected_wav を失っている（missing_expected_wav_metadata 経路）。
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

        assert!(pre_paths.failed_path.exists());
        let failed: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.failed_path).unwrap()).unwrap();
        assert!(failed
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_expected_wav_metadata"));

        let closed_at_ms = crate::record_expected::claim_marker_closed_at_ms_for_session(
            &base,
            "project_hash_test",
            "session-pair-atomic",
        )
        .unwrap()
        .expect("claim marker must exist after Keep");
        assert!(
            closed_at_ms.is_some(),
            "a failed close must stamp closed_at_ms even when expected_wav was lost by close \
             time — otherwise Kirin OS cannot tell this apart from a still-open (stop忘れ) session"
        );
    }

    #[test]
    fn pair_finalize_publishes_missing_trace_slots_as_visible_fallback() {
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
        assert!(manifest_path.exists());
        assert!(pre_paths.member_path.exists());
        assert!(post_paths.member_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
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
    fn pair_finalize_publishes_duration_mismatch_as_visible_fallback() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-duration-mismatch",
            None,
            Some("iid-post-duration-mismatch".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-duration-mismatch",
            Some("iid-pre-duration-mismatch".to_string()),
            None,
        );
        post.set_bounce_take(BounceTake {
            source: "expected_wav_duration_native".to_string(),
            time_axis: "native_samples".to_string(),
            alignment_status: "sample_count_ready".to_string(),
            sample_rate: 48_000,
            wav_start_sample: 0,
            wav_end_sample: 96_000,
            duration_samples: 96_000,
            duration_frames_48k: 96_000,
            start_t_ms: 0,
            end_t_ms: 2_000,
            trace_sample_count: 2,
            frame_count: 2,
            host_start_position_samples: None,
            host_end_position_samples: None,
        });
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
        assert!(manifest_path.exists());
        assert!(pre_paths.member_path.exists());
        assert!(post_paths.member_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "pair_bounce_take_duration_mismatch"));
            assert_eq!(
                data.record_quality
                    .as_ref()
                    .map(|quality| quality.status.as_str()),
                Some("usable_fallback")
            );
            assert!(data.trace_content_alignment.is_none());
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn pair_finalize_keeps_render_clock_shorter_than_expected_wav_visible() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-render-short-expected",
            None,
            Some("iid-post-render-short-expected".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-render-short-expected",
            Some("iid-pre-render-short-expected".to_string()),
            None,
        );
        let mut expected = expected_wav_fixture();
        expected.expected_duration_samples = 720_000;
        expected.wav_file_size = Some(2_880_044);
        pre.set_expected_wav(Some(expected.clone()));
        post.set_expected_wav(Some(expected));
        for writer in [&mut pre, &mut post] {
            let mut take = writer.data.bounce_take.clone().expect("bounce_take");
            take.source = BOUNCE_SOURCE_RENDER_CLOCK.to_string();
            writer.set_bounce_take(take);
        }
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
        assert!(manifest_path.exists());
        assert!(pre_paths.member_path.exists());
        assert!(post_paths.member_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            let quality = data.record_quality.as_ref().expect("record_quality");
            assert_eq!(quality.status, "usable_fallback");
            assert!(quality.usable);
            assert!(!quality.complete);
            assert!(data.trace_content_alignment.is_none());
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn pair_finalize_keeps_unbound_short_trace_grid_pending_for_drop() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-short-grid",
            None,
            Some("iid-post-short-grid".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-short-grid",
            Some("iid-pre-short-grid".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_expected_wav(None);
            let mut take = writer.data.bounce_take.clone().expect("bounce_take");
            take.source = BOUNCE_SOURCE_RENDER_CLOCK.to_string();
            take.duration_samples = 720_000;
            take.duration_frames_48k = 720_000;
            take.wav_end_sample = 720_000;
            take.end_t_ms = 15_000;
            writer.set_bounce_take(take);
        }
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
        assert!(pre_paths.pair_pending_path.exists());
        assert!(post_paths.pair_pending_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.pair_pending_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.pair_pending_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("pair_pending"));
            assert!(data.validity);
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn pair_finalize_keeps_unbound_sample_rate_mismatch_pending_for_drop() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-sample-rate-mismatch",
            None,
            Some("iid-post-sample-rate-mismatch".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-sample-rate-mismatch",
            Some("iid-pre-sample-rate-mismatch".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_expected_wav(None);
            let mut take = writer.data.bounce_take.clone().expect("bounce_take");
            take.source = BOUNCE_SOURCE_RENDER_CLOCK.to_string();
            writer.set_bounce_take(take);
        }
        post.data.sample_rate = 44_100;
        let mut post_take = post.data.bounce_take.clone().expect("post bounce_take");
        post_take.sample_rate = 44_100;
        post_take.duration_samples = 44_100;
        post_take.wav_end_sample = 44_100;
        post_take.duration_frames_48k = 48_000;
        post.set_bounce_take(post_take);
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
        assert!(pre_paths.pair_pending_path.exists());
        assert!(post_paths.pair_pending_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.pair_pending_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.pair_pending_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("pair_pending"));
            assert!(data.validity);
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn pair_finalize_publishes_derived_48k_mismatch_as_visible_fallback() {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-frame-count-mismatch",
            None,
            Some("iid-post-frame-count-mismatch".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-frame-count-mismatch",
            Some("iid-pre-frame-count-mismatch".to_string()),
            None,
        );
        let mut post_take = post.data.bounce_take.clone().expect("post bounce_take");
        post_take.duration_frames_48k = post_take.duration_frames_48k.saturating_add(4096);
        post.set_bounce_take(post_take);
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
        assert!(manifest_path.exists());
        assert!(pre_paths.member_path.exists());
        assert!(post_paths.member_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.failed_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert!(data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "pair_bounce_take_48k_duration_mismatch"));
            assert_eq!(
                data.record_quality
                    .as_ref()
                    .map(|quality| quality.status.as_str()),
                Some("usable_fallback")
            );
            assert!(data.trace_content_alignment.is_none());
            assert!(verify_checksum(data));
        }
    }

    #[test]
    fn late_expected_reconcile_promotes_pending_render_clock_pair_to_wav_boundaries() {
        let base = isolated_dir();
        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = Some(0);
        let start_ms = expected.wav_mtime_ms.saturating_sub(2_000);
        let end_ms = expected.wav_mtime_ms.saturating_add(500);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-late-committed",
            None,
            Some("iid-post-late-committed".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-late-committed",
            Some("iid-pre-late-committed".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some("session-late-committed".to_string()));
            set_late_expected_record_time(writer, start_ms, end_ms);
            configure_render_clock_take(writer, 57_600);
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();

        assert!(!pre_paths.member_path.exists());
        assert!(pre_paths.pair_pending_path.exists());
        let before: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.pair_pending_path).unwrap()).unwrap();
        assert_eq!(
            before.bounce_take.as_ref().map(|take| take.source.as_str()),
            Some(BOUNCE_SOURCE_RENDER_CLOCK)
        );
        assert_eq!(before.frames.len(), 12);
        assert_eq!(before.commit_status.as_deref(), Some("pair_pending"));

        crate::record_expected::write_expected_metadata(&base, "project_hash_test", &expected)
            .unwrap();
        crate::record_expected::write_claim_marker(
            &base,
            "project_hash_test",
            &expected,
            "session-late-committed",
            start_ms,
            Some(end_ms),
        )
        .unwrap();
        assert_eq!(reconcile_late_expected_wav(&base), 1);

        let manifest_path = pair_commit_manifest_path(&pre.paths, &pre.data).unwrap();
        let pre_trace_path = pair_trace_shelf_path(&pre_paths, &pre.data).unwrap();
        let post_trace_path = pair_trace_shelf_path(&post_paths, &post.data).unwrap();
        assert!(manifest_path.exists());
        assert!(pre_trace_path.exists());
        assert!(post_trace_path.exists());
        for path in [
            &pre_paths.member_path,
            &post_paths.member_path,
            &pre_trace_path,
            &post_trace_path,
        ] {
            let data: PluginDataFile = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert_eq!(data.frames.len(), 10);
            let take = data.bounce_take.as_ref().expect("bounce_take");
            assert_eq!(take.source, BOUNCE_SOURCE_EXPECTED_WAV);
            assert_eq!(take.duration_samples, expected.expected_duration_samples);
            assert_eq!(take.frame_count, 10);
            let quality = data.record_quality.as_ref().expect("record_quality");
            assert_eq!(quality.status, "complete");
            assert!(quality.expected_wav_ready);
            assert!(quality.trace_slots_complete);
            assert!(verify_checksum(&data));
        }
        assert_eq!(reconcile_late_expected_wav(&base), 0);
    }

    #[test]
    fn stopped_record_drop_never_repairs_missing_current_slots_from_legacy_context() {
        let base = isolated_dir();
        let session_id = "session-stopped-drop-au-partial";
        crate::record_expected::begin_expected_session(&base, "project_hash_test", session_id)
            .unwrap();
        crate::record_expected::mark_expected_metadata_consumed(
            &base,
            "project_hash_test",
            None,
            session_id,
        )
        .unwrap();
        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = Some(96_000);
        let start_ms = expected.created_at_ms.saturating_sub(2_000);
        let end_ms = expected.created_at_ms.saturating_sub(500);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-stopped-drop",
            None,
            Some("iid-post-stopped-drop".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-stopped-drop",
            Some("iid-pre-stopped-drop".to_string()),
            None,
        );
        let positions: Vec<i64> = std::iter::once(95_940)
            .chain(std::iter::once(96_000))
            .chain((1..=11).map(|index| 96_000 + index * 4_800))
            .collect();
        for (writer, role_offset) in [(&mut pre, 0.0), (&mut post, 20.0)] {
            writer.set_record_session_id(Some(session_id.to_string()));
            set_late_expected_record_time(writer, start_ms, end_ms);
            configure_render_clock_take(writer, 57_600);
            writer.data.trace_context_slot_positions = positions.clone();
            writer.data.trace_context_frames = positions
                .iter()
                .enumerate()
                .map(|(index, _)| {
                    make_frame_optional(
                        (index as u64 + 1) * TRACE_FRAME_INTERVAL_MS,
                        None,
                        None,
                        -30.0 + index as f64 + role_offset,
                        -6.0 + index as f64,
                        12.0,
                        None,
                    )
                })
                .collect();
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        post.data.trace_slot_positions.clear();
        post.data.trace_time_axis = None;
        post.data.trace_clock = None;
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        assert!(!crate::record_expected::expected_path(&base, "project_hash_test").exists());
        write_drop_commit_fixture(&base, session_id, &expected, true);
        assert!(closed_pair_can_accept_exact_drop(
            &pre.data, &post.data, session_id
        ));
        let inspected = crate::record_drop_commit::inspect_drop_commit_for_closed_pair_session(
            &base,
            "project_hash_test",
            session_id,
        )
        .unwrap()
        .expect("exact Drop transaction");
        assert_eq!(inspected, expected);
        let mut proof_pre = pre.data.clone();
        let mut proof_post = post.data.clone();
        assert!(normalize_late_expected_pair(
            &mut proof_pre,
            &mut proof_post,
            &inspected
        ));
        assert_eq!(
            proof_post.trace_clock_resolution.as_deref(),
            Some("chronological_fallback")
        );
        assert!(proof_post.trace_content_alignment.is_none());
        assert_ne!(
            proof_post.frames[0].lufs_m, proof_post.trace_context_frames[0].lufs_m,
            "legacy context must not manufacture the current producer side"
        );

        assert_eq!(
            reconcile_late_expected_wav_project(&base, "project_hash_test"),
            2
        );
        assert!(!pre_paths.pair_pending_path.exists());
        assert!(!post_paths.pair_pending_path.exists());
        assert!(pre_paths.member_path.exists());
        assert!(post_paths.member_path.exists());
        assert!(pair_commit_manifest_path(&pre.paths, &pre.data)
            .unwrap()
            .exists());
    }

    #[test]
    fn restart_after_daw_crash_promotes_only_durable_partial_facts_without_clean_stop() {
        let base = isolated_dir();
        let session_id = "session-daw-crash-no-clean-stop";
        crate::record_expected::begin_expected_session(&base, "project_hash_test", session_id)
            .unwrap();
        let marker_before = crate::record_expected::read_claim_marker_for_session(
            &base,
            "project_hash_test",
            session_id,
        )
        .unwrap()
        .expect("active Record marker");
        assert!(
            marker_before.closed_at_ms.is_none(),
            "the fixture must model a DAW exit where all_stop/clean Stop never closed the claim"
        );

        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = Some(0);
        let start_ms = expected.created_at_ms.saturating_sub(2_000);
        let crash_ms = expected.created_at_ms.saturating_sub(500);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-daw-crash",
            None,
            Some("iid-post-daw-crash".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-daw-crash",
            Some("iid-pre-daw-crash".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some(session_id.to_string()));
            set_late_expected_record_time(writer, start_ms, crash_ms);
            configure_render_clock_take(writer, 48_000);
            writer.add_integrity_reason("lifecycle_shutdown");
            writer.mark_integrity_degraded();
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        post.data.frames.remove(7);
        post.data.trace_slot_positions.remove(7);

        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        write_drop_commit_fixture(&base, session_id, &expected, true);

        // Restart recovery has no live plugin state and must use only the exact durable identities.
        assert_eq!(
            reconcile_drop_committed_closed_session(
                &base,
                "project_hash_test",
                session_id,
                "iid-pre-daw-crash",
                "iid-post-daw-crash",
            ),
            2
        );
        assert!(!pre_paths.pair_pending_path.exists());
        assert!(!post_paths.pair_pending_path.exists());
        assert!(pair_commit_manifest_path(&pre.paths, &pre.data)
            .unwrap()
            .exists());

        let committed_pre = read_plugin_data_file(&pre_paths.member_path).unwrap();
        let committed_post = read_plugin_data_file(&post_paths.member_path).unwrap();
        assert_eq!(committed_pre.frames.len(), 10);
        assert_eq!(committed_post.frames.len(), 9);
        assert_eq!(
            committed_post.trace_slot_positions,
            vec![4_800, 9_600, 14_400, 19_200, 24_000, 28_800, 33_600, 43_200, 48_000],
            "restart recovery must preserve the missing 800 ms slot as a gap"
        );
        assert_eq!(
            committed_post
                .frames
                .iter()
                .map(|frame| frame.t_ms)
                .collect::<Vec<_>>(),
            vec![100, 200, 300, 400, 500, 600, 700, 900, 1_000]
        );
        for committed in [&committed_pre, &committed_post] {
            assert_eq!(
                committed
                    .record_quality
                    .as_ref()
                    .map(|quality| quality.status.as_str()),
                Some("usable_fallback")
            );
            assert!(committed
                .integrity_reasons
                .iter()
                .any(|reason| reason == "lifecycle_shutdown"));
            assert!(verify_checksum(committed));
        }
        let marker_after = crate::record_expected::read_claim_marker_for_session(
            &base,
            "project_hash_test",
            session_id,
        )
        .unwrap()
        .expect("bound crash recovery marker");
        assert!(marker_after.closed_at_ms.is_some());
        assert_eq!(
            marker_after.metadata,
            Some(expected.without_consumed_marker())
        );
    }

    #[test]
    fn closed_candidate_selection_prefers_the_evaluated_failed_generation() {
        let base = isolated_dir();
        let project_dir = base.join("project_hash_test");
        let pending_path = project_dir
            .join("pre")
            .join(".pair_pending")
            .join("same-session.json");
        let failed_path = project_dir
            .join("pre")
            .join(".failed")
            .join("same-session.json");
        let mut pending = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-candidate-precedence",
            None,
            Some("iid-post-candidate-precedence".to_string()),
        )
        .data;
        pending.record_session_id = Some("same-session".to_string());
        pending.status = Status::Closed;
        pending.commit_status = Some("pair_pending".to_string());
        let mut failed = pending.clone();
        failed.commit_status = Some("failed".to_string());
        failed.integrity_reasons = vec!["evaluated-generation".to_string()];
        write_plugin_data_atomic(&pending_path, &mut pending).unwrap();
        write_plugin_data_atomic(&failed_path, &mut failed).unwrap();

        let mut candidates = HashMap::new();
        collect_late_expected_closed_candidates(&project_dir, &mut candidates);
        let selected = candidates
            .remove("same-session")
            .and_then(|candidate| candidate.pre)
            .expect("PRE candidate");

        assert_eq!(selected.0, failed_path);
        assert_eq!(selected.1.commit_status.as_deref(), Some("failed"));
        assert_eq!(selected.1.integrity_reasons, ["evaluated-generation"]);
    }

    #[test]
    fn stopped_record_drop_without_transaction_stays_pending_and_unclaimed() {
        let base = isolated_dir();
        let session_id = "session-stopped-drop-no-transaction";
        crate::record_expected::begin_expected_session(&base, "project_hash_test", session_id)
            .unwrap();
        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = Some(0);
        let start_ms = expected.created_at_ms.saturating_sub(2_000);
        let end_ms = expected.created_at_ms.saturating_sub(500);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-no-drop-transaction",
            None,
            Some("iid-post-no-drop-transaction".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-no-drop-transaction",
            Some("iid-pre-no-drop-transaction".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some(session_id.to_string()));
            set_late_expected_record_time(writer, start_ms, end_ms);
            configure_render_clock_take(writer, 57_600);
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        write_drop_commit_fixture(&base, session_id, &expected, false);

        assert_eq!(reconcile_late_expected_wav(&base), 0);
        assert!(pre_paths.pair_pending_path.exists());
        assert!(post_paths.pair_pending_path.exists());
        assert!(!crate::record_expected::expected_path(&base, "project_hash_test").exists());
        let marker = crate::record_expected::read_claim_marker_for_session(
            &base,
            "project_hash_test",
            session_id,
        )
        .unwrap()
        .unwrap();
        assert!(marker.metadata.is_none());
    }

    #[test]
    fn late_expected_requires_the_exact_session_claim_before_binding() {
        let base = isolated_dir();
        let expected = fresh_expected_wav_fixture(48_000);
        let mut other = fresh_expected_wav_fixture(48_000);
        other.bounce_id = "different-bounce".to_string();
        other.wav_hash = Some("different-hash".to_string());
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-exact-claim",
            None,
            Some("iid-post-exact-claim".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-exact-claim",
            Some("iid-pre-exact-claim".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some("session-exact-claim".to_string()));
            writer.set_expected_wav(None);
        }
        assert!(!pair_has_exact_expected_claim(
            &base, &pre.data, &post.data, &expected
        ));
        crate::record_expected::write_claim_marker(
            &base,
            "project_hash_test",
            &other,
            "session-exact-claim",
            other.created_at_ms.saturating_sub(1),
            None,
        )
        .unwrap();
        assert!(!pair_has_exact_expected_claim(
            &base, &pre.data, &post.data, &expected
        ));
        crate::record_expected::write_claim_marker(
            &base,
            "project_hash_test",
            &expected,
            "session-exact-claim",
            expected.created_at_ms.saturating_sub(1),
            None,
        )
        .unwrap();
        assert!(pair_has_exact_expected_claim(
            &base, &pre.data, &post.data, &expected
        ));
    }

    #[test]
    fn late_expected_reconcile_project_scope_does_not_scan_unrelated_projects() {
        let base = isolated_dir();
        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = Some(0);
        let start_ms = expected.wav_mtime_ms.saturating_sub(2_000);
        let end_ms = expected.wav_mtime_ms.saturating_add(500);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-late-scoped",
            None,
            Some("iid-post-late-scoped".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-late-scoped",
            Some("iid-pre-late-scoped".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some("session-late-scoped".to_string()));
            set_late_expected_record_time(writer, start_ms, end_ms);
            configure_render_clock_take(writer, 57_600);
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();
        crate::record_expected::write_expected_metadata(&base, "project_hash_test", &expected)
            .unwrap();
        crate::record_expected::write_claim_marker(
            &base,
            "project_hash_test",
            &expected,
            "session-late-scoped",
            start_ms,
            Some(end_ms),
        )
        .unwrap();

        assert_eq!(
            reconcile_late_expected_wav_project(&base, "unrelated_project"),
            0
        );
        assert_eq!(reconcile_late_expected_wav_project(&base, "_q_nothex"), 0);
        let before: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.pair_pending_path).unwrap()).unwrap();
        assert_eq!(
            before.bounce_take.as_ref().map(|take| take.source.as_str()),
            Some(BOUNCE_SOURCE_RENDER_CLOCK)
        );

        let materialized_project_dir =
            crate::path_identity::guard_path_component("project_hash_test", "test").to_string();
        assert_eq!(
            reconcile_late_expected_wav_project(&base, &materialized_project_dir),
            1
        );
        let after: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        assert_eq!(
            after.bounce_take.as_ref().map(|take| take.source.as_str()),
            Some(BOUNCE_SOURCE_EXPECTED_WAV)
        );
    }

    #[test]
    fn late_expected_reconcile_commits_mixed_failed_and_pending_pair() {
        let base = isolated_dir();
        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = Some(0);
        let start_ms = expected.wav_mtime_ms.saturating_sub(2_000);
        let end_ms = expected.wav_mtime_ms.saturating_add(500);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-late-failed",
            None,
            Some("iid-post-late-failed".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-late-failed",
            Some("iid-pre-late-failed".to_string()),
            None,
        );
        pre.set_record_session_id(Some("session-late-failed".to_string()));
        post.set_record_session_id(Some("session-late-failed".to_string()));
        set_late_expected_record_time(&mut pre, start_ms, end_ms);
        set_late_expected_record_time(&mut post, start_ms, end_ms);
        configure_render_clock_take(&mut pre, 52_800);
        configure_render_clock_take(&mut post, 57_600);
        {
            let clock = pre.data.trace_clock.as_mut().expect("PRE host clock");
            clock.origin_position_samples = 6_473_347;
            clock.end_position_samples = 6_526_147;
        }
        {
            let clock = post.data.trace_clock.as_mut().expect("POST host clock");
            clock.origin_position_samples = 6_465_671;
            clock.end_position_samples = 6_523_271;
        }
        for writer in [&mut pre, &mut post] {
            writer.data.status = Status::Closed;
        }
        pre.data.commit_status = Some("failed".to_string());
        pre.data.integrity_degraded = true;
        pre.data.integrity_reasons = vec![
            "missing_trace_slots".to_string(),
            "trace_slots_not_complete".to_string(),
            "integrity_degraded".to_string(),
        ];
        post.data.commit_status = Some("pair_pending".to_string());
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.failed_path.clone()).unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        assert!(pre_paths.failed_path.exists());
        assert!(post_paths.pair_pending_path.exists());
        assert!(!pair_commit_manifest_path(&pre.paths, &pre.data)
            .unwrap()
            .exists());

        crate::record_expected::write_expected_metadata(&base, "project_hash_test", &expected)
            .unwrap();
        crate::record_expected::write_claim_marker(
            &base,
            "project_hash_test",
            &expected,
            "session-late-failed",
            start_ms,
            Some(end_ms),
        )
        .unwrap();
        assert_eq!(reconcile_late_expected_wav(&base), 2);

        let manifest_path = pair_commit_manifest_path(&pre.paths, &pre.data).unwrap();
        let pre_trace_path = pair_trace_shelf_path(&pre_paths, &pre.data).unwrap();
        let post_trace_path = pair_trace_shelf_path(&post_paths, &post.data).unwrap();
        assert!(manifest_path.exists());
        assert!(!pre_paths.failed_path.exists());
        assert!(!post_paths.pair_pending_path.exists());
        for path in [
            &pre_paths.member_path,
            &post_paths.member_path,
            &pre_trace_path,
            &post_trace_path,
        ] {
            let data: PluginDataFile = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
            assert_eq!(data.commit_status.as_deref(), Some("committed"));
            assert!(data.validity);
            assert!(!data.integrity_degraded);
            assert!(data.integrity_reasons.is_empty());
            assert_eq!(data.frames.len(), 10);
            assert_eq!(
                data.bounce_take.as_ref().map(|take| take.duration_samples),
                Some(expected.expected_duration_samples)
            );
            assert!(
                data.record_quality
                    .as_ref()
                    .expect("record_quality")
                    .expected_wav_ready
            );
            assert!(verify_checksum(&data));
        }
        let committed_pre: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.member_path).unwrap()).unwrap();
        let committed_post: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.member_path).unwrap()).unwrap();
        assert_eq!(
            committed_pre
                .trace_clock
                .as_ref()
                .expect("PRE host clock")
                .origin_position_samples,
            0
        );
        assert_eq!(
            committed_post
                .trace_clock
                .as_ref()
                .expect("POST host clock")
                .origin_position_samples,
            0
        );
        assert_eq!(
            committed_pre.trace_slot_positions,
            committed_post.trace_slot_positions
        );
        assert_eq!(
            committed_pre.trace_wav_reference,
            committed_post.trace_wav_reference
        );
        let wav_reference = committed_pre
            .trace_wav_reference
            .expect("shared dropped WAV reference");
        assert_eq!(wav_reference.start_sample, 0);
        assert_eq!(wav_reference.end_sample, 48_000);
        assert_eq!(
            committed_pre.trace_time_axis.as_deref(),
            Some(crate::trace_alignment::TRACE_TIME_AXIS)
        );
        assert!(crate::record_expected::claim_marker_is_closed_for_session(
            &base,
            "project_hash_test",
            "session-late-failed"
        )
        .unwrap());
    }

    #[test]
    fn late_expected_pair_never_relabels_studio_one_7676_sample_skew_as_canonical() {
        let base = isolated_dir();
        let expected = fresh_expected_wav_fixture(48_000);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-shifted-slots",
            None,
            Some("iid-post-shifted-slots".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-shifted-slots",
            Some("iid-pre-shifted-slots".to_string()),
            None,
        );
        for writer in [&mut pre, &mut post] {
            writer.set_record_session_id(Some("session-shifted-slots".to_string()));
            configure_render_clock_take(writer, 57_600);
            writer.data.status = Status::Closed;
        }
        for position in &mut post.data.trace_slot_positions {
            *position = position.saturating_sub(7_676);
        }

        assert!(!normalize_late_expected_pair(
            &mut pre.data,
            &mut post.data,
            &expected
        ));
        assert!(pre.data.trace_wav_reference.is_none());
        assert!(post.data.trace_wav_reference.is_none());
    }

    #[test]
    fn late_expected_normalization_does_not_require_optional_host_clock() {
        let base = isolated_dir();
        let expected = fresh_expected_wav_fixture(48_000);
        let start_ms = expected.wav_mtime_ms.saturating_sub(2_000);
        let end_ms = expected.wav_mtime_ms.saturating_add(500);
        let mut writer = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-no-host-clock",
            None,
            Some("iid-post-no-host-clock".to_string()),
        );
        writer.set_record_session_id(Some("session-no-host-clock".to_string()));
        set_late_expected_record_time(&mut writer, start_ms, end_ms);
        configure_render_clock_take(&mut writer, 57_600);
        writer.data.status = Status::Closed;
        writer.data.commit_status = Some("pair_pending".to_string());
        writer.data.trace_time_axis = None;
        writer.data.trace_clock = None;

        let mut data = writer.data;
        assert!(normalize_late_expected_record(&mut data, &expected));
        assert!(data.trace_clock.is_none());
        assert_eq!(
            data.trace_time_axis.as_deref(),
            Some(crate::trace_alignment::TRACE_TIME_AXIS)
        );
        assert!(crate::trace_alignment::has_canonical_wav_reference(&data));
        assert_eq!(data.frames.len(), 10);
    }

    #[test]
    fn late_expected_normalization_rejects_previous_generation_even_one_ms_before_keep() {
        let base = isolated_dir();
        let mut expected = fresh_expected_wav_fixture(48_000);
        let start_ms = expected.created_at_ms.saturating_add(1);
        expected.wav_mtime_ms = start_ms;
        let mut writer = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-prior-generation",
            None,
            Some("iid-post-prior-generation".to_string()),
        );
        writer.set_record_session_id(Some("session-prior-generation".to_string()));
        set_late_expected_record_time(&mut writer, start_ms, start_ms.saturating_add(1_000));
        configure_render_clock_take(&mut writer, 57_600);
        writer.data.status = Status::Closed;
        writer.data.commit_status = Some("pair_pending".to_string());

        let mut data = writer.data;
        assert!(!normalize_late_expected_record(&mut data, &expected));
        assert!(data.expected_wav.is_none());
        assert!(data.trace_wav_reference.is_none());
    }

    #[test]
    fn late_expected_normalization_discards_integrity_faults_after_wav_end() {
        let base = isolated_dir();
        let expected = fresh_expected_wav_fixture(48_000);
        let start_ms = expected.wav_mtime_ms.saturating_sub(2_000);
        let end_ms = expected.wav_mtime_ms.saturating_add(500);
        let mut writer = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-tail-gap",
            Some("iid-pre-tail-gap".to_string()),
            None,
        );
        writer.set_record_session_id(Some("session-tail-gap".to_string()));
        set_late_expected_record_time(&mut writer, start_ms, end_ms);
        configure_render_clock_take(&mut writer, 57_600);
        writer.data.status = Status::Closed;
        writer.data.commit_status = Some("pair_pending".to_string());
        writer.data.trace_diagnostics = Some(TraceDiagnostics {
            raw_trace_count: 13,
            expected_frame_count: 13,
            measured_frame_count: 12,
            missing_slots: 1,
            explicit_silence_frame_count: 0,
        });
        writer.data.integrity_degraded = true;
        writer.data.integrity_reasons = vec![
            "missing_trace_slots".to_string(),
            "raw_trace_timeline_gap".to_string(),
            "sparse_trace_density".to_string(),
            "trace_slots_not_complete".to_string(),
        ];

        let mut data = writer.data;
        assert!(normalize_late_expected_record(&mut data, &expected));
        assert_eq!(data.frames.len(), 10);
        assert!(!data.integrity_degraded);
        assert!(data.integrity_reasons.is_empty());
        let diagnostics = data.trace_diagnostics.expect("WAV-scoped diagnostics");
        assert_eq!(diagnostics.expected_frame_count, 10);
        assert_eq!(diagnostics.measured_frame_count, 10);
        assert_eq!(diagnostics.missing_slots, 0);
    }

    #[test]
    fn late_expected_reconcile_rejects_expected_wav_older_than_record_start() {
        let base = isolated_dir();
        let mut expected = fresh_expected_wav_fixture(48_000);
        let start_ms = expected.wav_mtime_ms.saturating_add(10_000);
        let end_ms = start_ms.saturating_add(1_000);
        expected.created_at_ms = start_ms.saturating_add(500);
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-late-stale",
            None,
            Some("iid-post-late-stale".to_string()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-late-stale",
            Some("iid-pre-late-stale".to_string()),
            None,
        );
        pre.set_record_session_id(Some("session-late-stale".to_string()));
        post.set_record_session_id(Some("session-late-stale".to_string()));
        set_late_expected_record_time(&mut pre, start_ms, end_ms);
        set_late_expected_record_time(&mut post, start_ms, end_ms);
        configure_render_clock_take(&mut pre, 52_800);
        configure_render_clock_take(&mut post, 57_600);
        for writer in [&mut pre, &mut post] {
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".to_string());
        }
        let pre_paths = self_paths_for(&pre.paths, &pre.data);
        let post_paths = self_paths_for(&post.paths, &post.data);
        pre.write_atomic(pre_paths.pair_pending_path.clone())
            .unwrap();
        post.write_atomic(post_paths.pair_pending_path.clone())
            .unwrap();
        try_finalize_pair_session(&pre.paths, &pre.data).unwrap();
        crate::record_expected::write_expected_metadata(&base, "project_hash_test", &expected)
            .unwrap();

        assert_eq!(reconcile_late_expected_wav(&base), 0);
        assert!(pre_paths.pair_pending_path.exists());
        assert!(post_paths.pair_pending_path.exists());
        assert!(!pair_commit_manifest_path(&pre.paths, &pre.data)
            .unwrap()
            .exists());
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
        assert!(pre_paths.pair_pending_path.exists());
        assert!(post_paths.pair_pending_path.exists());

        let pre_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&pre_paths.pair_pending_path).unwrap()).unwrap();
        let post_data: PluginDataFile =
            serde_json::from_slice(&fs::read(&post_paths.pair_pending_path).unwrap()).unwrap();
        for data in [&pre_data, &post_data] {
            assert_eq!(data.commit_status.as_deref(), Some("pair_pending"));
            assert!(data.validity);
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

    #[test]
    fn close_failed_record_uses_session_keyed_diagnostic_path_when_session_exists() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        let wall_clock_failed_path = w.paths.failed_path.clone();
        w.set_record_session_id(Some("session-failed-diagnostic".to_string()));
        let session_failed_path = self_paths_for(&w.paths, w.data()).failed_path;

        w.close().unwrap();

        assert!(
            !wall_clock_failed_path.exists(),
            "session records must not leave wall-clock keyed failed fragments"
        );
        assert!(
            session_failed_path.exists(),
            "failed diagnostics must be keyed by record_session_id"
        );
        let loaded: PluginDataFile =
            serde_json::from_slice(&fs::read(&session_failed_path).unwrap()).unwrap();
        assert_eq!(
            loaded.record_session_id.as_deref(),
            Some("session-failed-diagnostic")
        );
        assert_eq!(loaded.commit_status.as_deref(), Some("failed"));
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn legacy_presentation_latency_observation_without_source_still_deserializes() {
        let observation: HostPresentationLatencyObservation =
            serde_json::from_str(r#"{"input_samples":0,"output_samples":9600}"#).unwrap();
        assert!(observation.source.is_none());
        assert_eq!(observation.input_samples, Some(0));
        assert_eq!(observation.output_samples, Some(9_600));
    }

    #[test]
    fn direct_failed_close_stamps_claim_marker_closed_at_ms() {
        let base = isolated_dir();
        let session_id = "session-direct-failed-close";
        let now_ms = chrono::Utc::now().timestamp_millis();
        let mut fresh_expected = expected_wav_fixture();
        fresh_expected.created_at_ms = now_ms;
        fresh_expected.wav_mtime_ms = now_ms;
        crate::record_expected::write_expected_metadata(
            &base,
            "project_hash_test",
            &fresh_expected,
        )
        .unwrap();
        crate::record_expected::claim_expected_metadata_for_session(
            &base,
            "project_hash_test",
            session_id,
        )
        .unwrap();

        let mut w = sample_writer(&base, Role::Post);
        w.set_record_session_id(Some(session_id.to_string()));
        let session_failed_path = self_paths_for(&w.paths, w.data()).failed_path;

        w.close().unwrap();

        assert!(
            session_failed_path.exists(),
            "direct failed close must still write a session-keyed failed diagnostic"
        );
        let closed_at_ms = crate::record_expected::claim_marker_closed_at_ms_for_session(
            &base,
            "project_hash_test",
            session_id,
        )
        .unwrap()
        .expect("claim marker must exist after Keep");
        assert!(
            closed_at_ms.is_some(),
            "direct .failed close must stamp closed_at_ms so Kirin OS does not treat it as \
             still recording"
        );
    }

    fn marked_annotation(id: &str, position: i64, memo: &str) -> Annotation {
        Annotation {
            t: "2026-07-14T00:00:00Z".to_string(),
            memo: memo.to_string(),
            mark: Some(AnnotationMark {
                schema_version: "record_mark.v1".to_string(),
                id: id.to_string(),
                basis: crate::record_mark::RECORD_MARK_BASIS.to_string(),
                producer_position_samples: position,
                clock_source: "project_timeline".to_string(),
                wav_position_samples: None,
            }),
        }
    }

    #[test]
    fn legacy_annotation_without_mark_still_deserializes() {
        let annotation: Annotation =
            serde_json::from_str(r#"{"t":"2026-07-14T00:00:00Z","memo":"Fix"}"#).unwrap();
        assert_eq!(annotation.memo, "Fix");
        assert!(annotation.mark.is_none());
    }

    #[test]
    fn record_mark_binds_only_inside_exact_wav_sample_range() {
        let base = isolated_dir();
        let mut data = sample_writer(&base, Role::Post).data;
        data.annotations = vec![
            marked_annotation("before", 9_999, "Fix"),
            marked_annotation("start", 10_000, "Good"),
            marked_annotation("inside", 10_256, "Hold"),
            marked_annotation("end", 11_000, "Good"),
            marked_annotation("after", 11_001, "Fix"),
        ];
        bind_annotation_marks_to_wav(&mut data, 10_000, 1_000);
        let positions: Vec<Option<u64>> = data
            .annotations
            .iter()
            .map(|annotation| {
                annotation
                    .mark
                    .as_ref()
                    .and_then(|mark| mark.wav_position_samples)
            })
            .collect();
        assert_eq!(positions, vec![None, Some(0), Some(256), Some(1_000), None]);
    }

    #[test]
    fn pair_annotation_sync_deduplicates_mark_id_on_both_members() {
        let base = isolated_dir();
        let mut pre = sample_writer(&base, Role::Pre).data;
        let mut post = sample_writer(&base, Role::Post).data;
        pre.annotations = vec![marked_annotation("shared", 10, "Good")];
        post.annotations = vec![
            marked_annotation("shared", 10, "Good"),
            marked_annotation("post", 20, "Fix"),
        ];
        synchronize_pair_annotations(&mut pre, &mut post);
        assert_eq!(pre.annotations, post.annotations);
        assert_eq!(pre.annotations.len(), 2);
    }
}
