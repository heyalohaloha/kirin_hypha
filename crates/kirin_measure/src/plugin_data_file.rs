//! Serialized Record data.
use super::*;

/// One Record file, with additive schema fields.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginDataFile {
    pub schema_version: String,
    pub installation_id: String,
    pub project_hash: String,
    /// Plugin Default 起動時に `Uuid::new_v4` で生成され、VST3 state として
    /// 永続化される plugin インスタンス UUID。同一 plugin instance の PRE/POST
    /// ペア復元の一次キー。Lens reader.js で `paired_*_instance_id` との一致比較に
    /// 使用される（v1.2）。
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
    /// EBU R128 Integrated Loudness [LUFS] (/ セッション集計)。
    /// `close()` 直前に `set_session_aggregates` で注入。値が無い場合は省略。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub lufs_i: Option<f64>,
    /// EBU R128 Loudness Range [LU] (/ セッション集計)。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub lra: Option<f64>,
    /// PLR = max_true_peak − lufs_i [dB] (/ セッション集計)。
    /// `lufs_i` か `max_true_peak` のどちらかが欠ければ `None`。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub plr: Option<f64>,
    /// POST 側でのみ書き込み: Keep タップ時に選定した PRE 候補の instance_id。
    /// 不在時 None。Lens 側 cross-instance pair 復元の決定論的キー（v1.2）。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub paired_pre_instance_id: Option<String>,
    /// PRE 側でのみ書き込み: Record 開始時に受信した record_signal の
    /// `requested_by`（POST 側 instance_id）。不在時 None。
    /// Lens 側 cross-instance pair 復元の決定論的キー（v1.2）。
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
    /// この Record 中に ring 満杯で測定 ring に push できなかったサンプル数
    /// （per-Record 差分 = Record 開始時 overflow snapshot との差）。計測値は汚さない露出のみ。
    /// 旧 .kirin 互換のため `#[serde(default)]`（schema_version "1.3" 維持・additive）。
    #[serde(default)]
    pub dropped_samples: u64,
    /// `dropped_samples > 0` → true（1 sample でも立てる・閾値なし）。count を併記し
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
    /// この Record がどの配置を、どの map で測ったか。旧 .kirin 互換のため optional additive。
    /// 不在は「記録した版がこの事実を持っていなかった」であり、stereo の意味ではない。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub measurement_layout: Option<MeasurementLayout>,
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
    /// is preserved across WAV binding; canonical display placement uses the resolved slot axis.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_clock_observations: Vec<TraceClockObservation>,
    /// Producer-selected take anchor; absent in legacy records.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_selected_capture_epoch: Option<u64>,
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
    /// origin before role-specific output latency. A negative producer-content position is valid
    /// when downstream PDC presents that measured frame inside the public WAV range; it remains a
    /// private PRE/POST join key and never becomes a WAV display position. `raw_host_clock_exact`
    /// uses absolute raw-host endpoints. Neither form may be inferred from metric shape.
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
    /// Every pass of a Keep that went on past its first pass (a LOOP wrap, a seek, a restart), with
    /// host range, accepted frames and why it is not in the TRACE. A normal wrap degrades nothing.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub keep_passes: Vec<crate::record_take::KeepPass>,
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
    /// Legacy staging field. Current producers always leave this empty and current pair
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
    /// 以前の staging JSON を読み戻すための legacy field。publish 前に必ず消去する。
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub trace_context_psb_snapshots: Vec<PsbSnapshot>,
    /// Kirin OS 表示用の品質分類。厳密判定は Hypha 内に閉じ、OS はこの分類で
    /// complete / usable fallback / failed を静かに扱い分ける。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub record_quality: Option<RecordQuality>,
    pub validity: bool,
    pub checksum: String,
}
