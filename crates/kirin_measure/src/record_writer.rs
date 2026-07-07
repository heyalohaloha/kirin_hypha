//! Record 時の PluginDataWriter 共通ライフサイクル（PRE / POST 共有）。
//!
//! 責務:
//! - Watch↔Record 遷移に合わせた writer の生成・破棄
//! - 30 秒間隔で heartbeat + atomic flush
//!
//! PRE / POST どちらも同じロジックで動く。違いは Role の指定と、
//! state machine を駆動するトリガ（POST=GUI ボタン、PRE=record_signal poll）のみ。
//!
//! # A-3 修正後
//! `project_hash` と `instance_id` は plugin から引数として渡る。旧
//! `BUS_PHASE1="MIX"` / `PROJECT_HASH_PHASE1="default"` 定数依存は廃止。
//!
//! # t_ms 軸
//! `started_at_ms` は record_signal.json の `started_at` を epoch ms に変換したもの。
//! POST / PRE が同じ軸上で frame を並べるため、record_signal を単一真実として参照する。
//! 不在・パース失敗時は現在時刻にフォールバック（defensive）。

use std::collections::{BTreeMap, VecDeque};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime};

use crate::engine::SessionSummary;
use crate::plugin_data::{
    compute_checksum, verify_checksum, BounceTake, ExpectedWavMetadata, PluginDataFile,
    PluginDataWriter, Role, Status, TraceDiagnostics, WriterPaths,
};
use crate::record::RecordStateMachine;
use crate::record_signal;
use crate::record_take::{RecordTakeSnapshot, RecordTakeTracker, RECORD_TAKE_SOURCE_WAV_CLOCK};
use crate::storage::{load_installation_id_safe, StoragePaths};
use crate::MeasureResult;
use chrono::TimeZone;

/// Frame サンプリング間隔（10 fps / G-50-17 リアルタイム Record）。
pub const FRAME_INTERVAL_MS: u64 = 100;

/// TRACE の無音床。frames[] は Lens/TRACE が読む唯一の時系列なので、Record TRACE
/// では音が無い時間も「無音として測定されたフレーム」として焼く。
pub const TRACE_SILENCE_LUFS: f64 = -100.0;
pub const TRACE_SILENCE_TRUE_PEAK_DBTP: f64 = -100.0;
pub const TRACE_SILENCE_CREST_DB: f64 = 0.0;
pub const TRACE_SILENCE_PEAK_LINEAR: f64 = 0.00001;

/// PSB スナップショット間隔（2 fps / G-50-17）。
pub const PSB_INTERVAL_MS: u64 = 500;

/// Record TRACE の内部時間軸。MeasureEngine は 48 kHz に正規化して処理する。
pub const TRACE_TIMEBASE_HZ: u64 = 48_000;

/// heartbeat + atomic flush 間隔（正本 30 秒）。
pub const FLUSH_INTERVAL: Duration = Duration::from_secs(30);

/// Measure Thread → IO Thread の Record TRACE サンプル。
///
/// `t_ms` は壁時計ではなく、MeasureEngine が処理した音声フレーム数から作る音声時間。
/// Offline bounce では壁時計がほとんど進まないため、このキューを正本にして TRACE を焼く。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RecordTraceKind {
    /// MeasureEngine が実際に計測したフレーム。真の無音も core metrics を持つ measured frame。
    Measured,
    /// Measure Thread が時間軸だけを前進させる marker。frames[] には焼かない。
    Missing,
}

#[derive(Debug, Clone)]
pub struct RecordTraceSample {
    pub kind: RecordTraceKind,
    pub generation: u64,
    pub t_ms: u64,
    pub t_frames_48k: u64,
    /// DAW/WAV 側の native sample frame 位置（1ch あたりの sample frame 数）。
    ///
    /// 44.1 kHz など非48k環境では、WAV header と一致させる正本は 48k resample 後の
    /// frame 数ではなく、この native frame 数。
    pub t_native_frames: Option<u64>,
    pub result: MeasureResult,
    pub include_psb: bool,
}

impl RecordTraceSample {
    pub fn measured(
        generation: u64,
        t_ms: u64,
        t_frames_48k: u64,
        t_native_frames: Option<u64>,
        result: MeasureResult,
        include_psb: bool,
    ) -> Self {
        Self {
            kind: RecordTraceKind::Measured,
            generation,
            t_ms,
            t_frames_48k,
            t_native_frames,
            result,
            include_psb,
        }
    }

    pub fn measured_observed(
        generation: u64,
        t_ms: u64,
        t_frames_48k: u64,
        t_native_frames: Option<u64>,
        result: MeasureResult,
        observed_silence_floor: bool,
        include_psb: bool,
    ) -> Self {
        let result = if observed_silence_floor && !measure_has_core_metrics(&result) {
            with_trace_silence_core(result)
        } else {
            result
        };
        Self::measured(
            generation,
            t_ms,
            t_frames_48k,
            t_native_frames,
            result,
            include_psb,
        )
    }

    pub fn missing_marker(
        generation: u64,
        t_ms: u64,
        t_frames_48k: u64,
        t_native_frames: Option<u64>,
    ) -> Self {
        Self {
            kind: RecordTraceKind::Missing,
            generation,
            t_ms,
            t_frames_48k,
            t_native_frames,
            result: MeasureResult::default(),
            include_psb: false,
        }
    }

    pub fn has_measured_core(&self) -> bool {
        self.kind == RecordTraceKind::Measured && measure_has_core_metrics(&self.result)
    }
}

pub fn samples_are_trace_silence_floor(samples: &[f64]) -> bool {
    !samples.is_empty()
        && samples
            .iter()
            .all(|sample| sample.abs() <= TRACE_SILENCE_PEAK_LINEAR)
}

pub type RecordTraceQueue = Arc<Mutex<VecDeque<RecordTraceSample>>>;

pub fn new_record_trace_queue() -> RecordTraceQueue {
    Arc::new(Mutex::new(VecDeque::new()))
}

pub fn clear_record_trace_queue(queue: &RecordTraceQueue) {
    if let Ok(mut q) = queue.lock() {
        q.clear();
    }
}

pub fn push_record_trace_sample(queue: &RecordTraceQueue, sample: RecordTraceSample) {
    if let Ok(mut q) = queue.lock() {
        q.push_back(sample);
    }
}

pub fn drain_record_trace_queue(queue: &RecordTraceQueue) -> Vec<RecordTraceSample> {
    match queue.lock() {
        Ok(mut q) => q.drain(..).collect(),
        Err(_) => Vec::new(),
    }
}

pub fn drain_record_trace_queue_for_generation(
    queue: &RecordTraceQueue,
    generation: u64,
) -> Vec<RecordTraceSample> {
    if generation == 0 {
        return drain_record_trace_queue(queue);
    }
    match queue.lock() {
        Ok(mut q) => {
            let mut matched = Vec::new();
            let mut retained = VecDeque::new();
            for sample in q.drain(..) {
                if sample.generation == generation {
                    matched.push(sample);
                } else if sample.generation > generation {
                    retained.push_back(sample);
                }
            }
            *q = retained;
            matched
        }
        Err(_) => Vec::new(),
    }
}

/// B-132 (G-115-382 共通B): bake arm が Measure Thread の drain-completion seal 前進を待つ
/// **上限**。Record→Watch エッジでは Measure Thread が ring 残量を優先 drain するため、
/// 健全時は ~1 loop（≤100ms）で
/// seal が前進する。本値は measure 死 / shutdown / stall で finalize 不能なときの天井で、
/// 超過したら integrity_degraded に倒す（共通B / silent truncation を残さない）。
pub const SEAL_WAIT_TIMEOUT: Duration = Duration::from_millis(500);

/// B-132: seal wait のポーリング間隔（lock-free / atomic load のみ）。
pub const SEAL_WAIT_POLL: Duration = Duration::from_millis(10);

/// TRACE 密度不足を integrity として扱い始める最小音声時間。
const TRACE_DENSITY_MIN_DURATION_MS: u64 = 5_000;

/// 音声時間ベースの想定 TRACE サンプル数に対する下限比率。
///
/// 実際の numeric frame は無音で減り得るため判定には使わず、Measure Thread から届いた
/// RecordTraceSample 数を優先して見る。ただし TRACE が 0 件でも、fallback numeric frame が
/// 実用密度で残っているリアルタイム収録は救済する。長時間で両方が粗すぎる場合だけ
/// `integrity_degraded` に倒す。
const TRACE_DENSITY_MIN_RATIO_NUM: usize = 1;
const TRACE_DENSITY_MIN_RATIO_DEN: usize = 4;

/// `sample_count_ready` として通常運用へ渡せる最小 Record 長。
///
/// 21ms / 1 frame などの短片は、たとえ native sample count が取れていても
/// WAV と対応する take ではなく lifecycle fragment とみなす。
const CLEAN_TAKE_MIN_DURATION_MS: u64 = 1_000;

/// clean take を `sample_count_ready` と呼ぶための TRACE 密度。
///
/// 通常の offline bounce では Measure Thread が 100ms grid + 終端 marker を入れるため
/// ほぼ 100% になる。75% 未満は Record grid の欠落として扱い、通常棚へ混ぜない。
const CLEAN_TAKE_MIN_TRACE_RATIO_NUM: usize = 3;
const CLEAN_TAKE_MIN_TRACE_RATIO_DEN: usize = 4;

/// frames[] が WAV 0..duration を覆っているとみなす許容幅。
const FRAME_COVERAGE_MAX_GAP_MS: u64 = FRAME_INTERVAL_MS * 2;
const FRAME_COVERAGE_EDGE_TOLERANCE_MS: u64 = FRAME_INTERVAL_MS * 2;
const TRACE_SLOT_ASSIGNMENT_TOLERANCE_MS: u64 = FRAME_INTERVAL_MS / 10;

/// B-132 (G-115-382 共通B): Record→Watch close 時に Measure Thread の post-drain seal が
/// `seal_at_start` を超えるまで **lock-free bounded** に待つ。
///
/// 返値 `true`: seal が前進した（= measure が tight-drain + 最終 finalize + session_summary 書込を
/// 完了）→ bake arm は post-drain 確定スナップショットを take してよい。
/// 返値 `false`: `SEAL_WAIT_TIMEOUT` 超過（measure 死 / shutdown / stall）→ 呼出側は
/// integrity_degraded を立てる。
///
/// teardown 安全性: atomic load のみで lock を跨がない・timeout 有限。Drop が
/// measure.shutdown を立てて join する順序（watchdog IO→measure）と循環しない
/// （measure が seal を進めなくても最大 `SEAL_WAIT_TIMEOUT` で抜ける）。
pub fn wait_for_seal(
    record_sm: &RecordStateMachine,
    seal_at_start: u64,
    timeout: Duration,
) -> bool {
    let deadline = Instant::now() + timeout;
    loop {
        if record_sm.seal() > seal_at_start {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        std::thread::sleep(SEAL_WAIT_POLL);
    }
}

/// B-025 Group B-2 / Gap-19 + Group B-3 / Gap-20: flush 連続失敗の診断しきい値。
/// B-245 以降、この値はログと degraded 記録のためだけに使う。
/// storage 失敗そのものには Record 停止権限を持たせない。
pub const CONSECUTIVE_FAILURE_THRESHOLD: usize = 3;

/// Record 中の writer + タイミング状態。
///
/// IO Thread ローカル（他スレッドと共有しない）。
pub struct RecordingCtx {
    pub(crate) writer: PluginDataWriter,
    pub final_path: PathBuf,
    pub staging_path: PathBuf,
    pub failed_path: PathBuf,
    /// Record 開始 wall-clock（epoch ms）。frame t_ms はこの値からの差分。
    pub started_at_ms: i64,
    /// 次に Frame を書く最小 t_ms。
    pub next_frame_ms: u64,
    /// 次に PSB を書く最小 t_ms。
    pub next_psb_ms: u64,
    /// 次に flush/heartbeat する Instant。
    pub next_flush: Instant,
    /// 最初の Frame 書込をログしたか（1 Record セッションにつき 1 回）。
    pub first_frame_logged: bool,
    /// B-025 Gap-20 / B-245: flush() が `Io` / `Serde` を返した連続回数。
    /// 成功時 0 にリセット。
    pub consecutive_write_error: usize,
    /// B-076: Record 開始時の overflow snapshot（累積 push_overflow）。close 時に
    /// 現在値との差で per-Record dropped_samples を算出する（累積を持ち越さない）。
    pub overflow_start: u64,
    /// B-125: Record 開始時の oversized_drop snapshot（累積 oversized block drop）。
    /// overflow とは独立カウンタ（metric truthfulness）。close 時に差分を取り合算する。
    pub oversized_drop_start: u64,
    /// B-132 (G-115-382): Record 開始時の drain-completion seal snapshot。close 時に
    /// `wait_for_seal` がこの値の前進を bounded 待ちして post-drain 確定を確認する。
    pub seal_at_start: u64,
    /// RecordStateMachine の Record 世代。Audio Thread 側の clean take tracker と照合する。
    pub record_generation: u64,
    /// Audio Thread が積んだ実レンダー長。手動 Keep/Stop の余白ではなく WAV と対応する
    /// `bounce_take` の正本として使う。
    pub clean_take: Option<RecordTakeSnapshot>,
    /// 最後に Measure Thread から届いた Record TRACE 音声時間。
    ///
    /// `frames[]` は LUFS/TP/Crest が揃った実測点だけを書くが、Record 中の無音区間では
    /// 数値フレームが成立しないことがある。offline bounce の収録長は壁時計ではなく、
    /// 数値フレームの有無と独立したこの音声時間から焼く。
    pub last_trace_t_ms: Option<u64>,
    /// 最後に Measure Thread から届いた Record TRACE の 48 kHz 内部フレーム位置。
    ///
    /// 計測内部時間軸の長さとして `bounce_take.duration_frames_48k` に残す。
    pub last_trace_frame_48k: Option<u64>,
    /// 最後に Measure Thread から届いた DAW/WAV native sample frame 位置。
    ///
    /// `bounce_take.duration_samples` と `bounce_marker.duration_samples` はこの値を正本にする。
    /// 非48k環境で resampler の pending/latency/chunk 境界に引きずられないための主軸。
    pub last_trace_native_frames: Option<u64>,
    /// Measure Thread から届いた audio-time TRACE サンプル総数。
    ///
    /// `frames[]` は無音/ウォームアップで減るため、密度検査はこの raw sample count で行う。
    pub trace_sample_count: usize,
    /// Measure Thread から届いた raw TRACE samples。
    ///
    /// Writer の逐次 append は、flush 用の中間表示としては便利だが、Record の正本ではない。
    /// close 時にこの raw list を duration grid へ焼き直し、順序揺れや遅延 floor marker が
    /// そのまま `frames[]` 欠落にならないようにする。
    pub trace_samples: Vec<RecordTraceSample>,
    /// close 時に連続 grid へ焼く前の raw TRACE が duration を覆っていたか。
    ///
    /// `frames[]` は close 時に補完されるため、ready/integrity 判定では補完後の見た目ではなく
    /// 補完前の raw timeline coverage を見る。
    pub raw_trace_timeline_covers_duration: Option<bool>,
}

impl RecordingCtx {
    #[cfg(test)]
    pub fn data(&self) -> &crate::plugin_data::PluginDataFile {
        self.writer.data()
    }
}

/// 現在時刻を epoch ms で返す。
pub fn now_epoch_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

/// epoch ms を UTC ISO 8601（ミリ秒精度）へ変換する。
pub fn epoch_ms_to_iso8601(ms: i64) -> String {
    chrono::Utc
        .timestamp_millis_opt(ms)
        .single()
        .unwrap_or_else(chrono::Utc::now)
        .format("%Y-%m-%dT%H:%M:%S%.3fZ")
        .to_string()
}

/// ISO 8601 / RFC 3339 文字列を epoch ms に変換。パース失敗時は `None`。
pub fn parse_iso8601_to_epoch_ms(s: &str) -> Option<i64> {
    if s.is_empty() {
        return None;
    }
    chrono::DateTime::parse_from_rfc3339(s)
        .ok()
        .map(|dt| dt.with_timezone(&chrono::Utc).timestamp_millis())
}

/// 指定 `post_instance_id` の record_signal.json から `started_at` を epoch ms 化。
///
/// 失敗時（ファイル不在 / `started_at` 空 / パース不能）は現在時刻 + 警告ログ。
/// t_ms = 0 起点にフォールバックすることで Record 続行可能。
pub fn resolve_started_at_ms(
    base: &std::path::Path,
    project_hash: &str,
    post_instance_id: &str,
) -> i64 {
    let signal_path = record_signal::signal_path(base, project_hash, post_instance_id);
    match record_signal::read_signal(base, project_hash, post_instance_id) {
        Some(sig) => parse_iso8601_to_epoch_ms(&sig.started_at).unwrap_or_else(|| {
            log::warn!(
                "[signal] started_at missing, using now() as fallback ({})",
                signal_path.display()
            );
            now_epoch_ms()
        }),
        None => {
            log::warn!(
                "[signal] started_at missing, using now() as fallback ({})",
                signal_path.display()
            );
            now_epoch_ms()
        }
    }
}

/// 指定 `post_instance_id` の record_signal.json から pair-wide Record session id を取得。
pub fn resolve_record_session_id(
    base: &std::path::Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Option<String> {
    record_signal::read_signal(base, project_hash, post_instance_id)
        .map(|signal| signal.session_id)
        .and_then(|session_id| {
            let trimmed = session_id.trim().to_string();
            (!trimmed.is_empty()).then_some(trimmed)
        })
}

/// 指定 `post_instance_id` の record_signal.json から expected WAV metadata を取得。
pub fn resolve_expected_wav_metadata(
    base: &std::path::Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Option<ExpectedWavMetadata> {
    record_signal::read_signal(base, project_hash, post_instance_id)
        .and_then(|signal| signal.expected_wav)
        .filter(ExpectedWavMetadata::is_usable)
}

/// Record 開始: `PluginDataWriter` を生成し、空ファイルで初回 flush する。
///
/// # 引数
/// - `role`: Pre / Post
/// - `sample_rate`: writer メタデータに埋め込む sample_rate
/// - `started_at_ms`: record_signal.started_at を epoch ms に変換したもの
/// - `project_hash`, `instance_id`: 新構造 path 構築用
/// - `paired_pre_instance_id`: POST 側でのみ Some。trigger_keep の target_id
///   （v1.2 (a) cross-instance pair 復元キー）
/// - `paired_post_instance_id`: PRE 側でのみ Some。record_signal の requested_by
///   （v1.2 (a) cross-instance pair 復元キー）
/// - `pair_name` / `pair_pre_name`: Lens 表示用の pair 名 snapshot（空なら省略）
///
/// # 失敗要因（いずれも `None` を返してログ記録）
/// - `$HOME` 未解決
/// - identity.json 不在 or `installation_id` フィールド欠落
/// - `PluginDataWriter::create` が IO エラー
/// - 初回 flush 失敗
#[allow(clippy::too_many_arguments)]
pub fn writer_start(
    role: Role,
    sample_rate: u32,
    started_at_ms: i64,
    project_hash: &str,
    instance_id: &str,
    paired_pre_instance_id: Option<String>,
    paired_post_instance_id: Option<String>,
    pair_name: Option<String>,
    pair_pre_name: Option<String>,
) -> Option<RecordingCtx> {
    let paths = match StoragePaths::default_platform() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[writer] StoragePaths error: {:?}", e);
            return None;
        }
    };
    let base = paths.plugin_data_dir();
    let installation_id = load_installation_id_safe()?;
    let wall_clock_iso = epoch_ms_to_iso8601(started_at_ms);
    let signal_post_instance_id = match role {
        Role::Pre => paired_post_instance_id.as_deref(),
        Role::Post => Some(instance_id),
    };
    let record_session_id = signal_post_instance_id
        .and_then(|post_iid| resolve_record_session_id(&base, project_hash, post_iid));
    let expected_wav = signal_post_instance_id
        .and_then(|post_iid| resolve_expected_wav_metadata(&base, project_hash, post_iid));
    let writer_paths = WriterPaths::build(&base, project_hash, instance_id, role, &wall_clock_iso);
    let final_path = writer_paths.final_path.clone();
    let staging_path = writer_paths.staging_path.clone();
    let failed_path = writer_paths.failed_path.clone();
    let mut w = match PluginDataWriter::create(
        writer_paths,
        installation_id,
        project_hash.to_string(),
        instance_id.to_string(),
        role,
        None,
        sample_rate,
        paired_pre_instance_id,
        paired_post_instance_id,
        pair_name,
        pair_pre_name,
    ) {
        Ok(w) => w,
        Err(e) => {
            log::warn!("[writer] create failed: {}", e);
            return None;
        }
    };
    w.set_record_start_wall_clock(wall_clock_iso);
    w.set_started_at_ms(started_at_ms);
    w.set_record_session_id(record_session_id);
    w.set_expected_wav(expected_wav);
    if let Err(e) = w.flush() {
        log::warn!("[writer] initial flush failed: {}", e);
        return None;
    }
    log::info!(
        "[writer] created ({:?}): {} (started_at_ms={})",
        role,
        final_path.display(),
        started_at_ms
    );
    Some(RecordingCtx {
        writer: w,
        final_path,
        staging_path,
        failed_path,
        started_at_ms,
        next_frame_ms: 0,
        next_psb_ms: 0,
        next_flush: Instant::now() + FLUSH_INTERVAL,
        first_frame_logged: false,
        consecutive_write_error: 0,
        overflow_start: 0, // B-076: run_record_tick が Record 開始時に snapshot を入れる
        oversized_drop_start: 0, // B-125: 同上（oversized_drop の Record 開始 snapshot）
        seal_at_start: 0,  // B-132: run_record_tick が Record 開始時に seal を snapshot する
        record_generation: 0,
        clean_take: None,
        last_trace_t_ms: None,
        last_trace_frame_48k: None,
        last_trace_native_frames: None,
        trace_sample_count: 0,
        trace_samples: Vec::new(),
        raw_trace_timeline_covers_duration: None,
    })
}

/// Record 終了: status=closed で最終 flush、ログ出力。
pub fn writer_close(mut ctx: RecordingCtx) {
    let final_path = ctx.final_path.clone();
    let failed_path = ctx.failed_path.clone();
    refresh_pair_record_metadata_if_missing(&mut ctx);
    bake_continuous_record_timeline(&mut ctx);
    clip_clean_timeline_to_duration(&mut ctx);
    mark_integrity_if_record_is_too_short(&mut ctx);
    mark_integrity_if_trace_density_is_sparse(&mut ctx);
    mark_integrity_if_record_clock_is_not_wav_bounded(&mut ctx);
    seal_bounce_marker(&mut ctx);
    match ctx.writer.close() {
        Ok(()) if final_path.exists() => log::info!("[writer] released: {}", final_path.display()),
        Ok(()) if failed_path.exists() => {
            log::warn!("[writer] released diagnostic: {}", failed_path.display())
        }
        Ok(()) => log::info!("[writer] released: {}", final_path.display()),
        Err(e) => log::warn!("[writer] close failed ({}): {}", final_path.display(), e),
    }
}

pub fn apply_record_take_snapshot(ctx: &mut RecordingCtx, tracker: Option<&RecordTakeTracker>) {
    let Some(tracker) = tracker else {
        return;
    };
    if let Some(snapshot) = tracker.snapshot(ctx.record_generation) {
        ctx.clean_take = Some(snapshot);
    }
}

fn refresh_pair_record_metadata_if_missing(ctx: &mut RecordingCtx) {
    if ctx.writer.data().record_session_id.is_some() && ctx.writer.data().expected_wav.is_some() {
        return;
    }
    let Ok(paths) = StoragePaths::default_platform() else {
        return;
    };
    let Some(post_instance_id) = signal_post_instance_id(ctx).map(str::to_string) else {
        return;
    };
    refresh_pair_record_metadata_from_base(ctx, &paths.plugin_data_dir(), &post_instance_id);
}

fn signal_post_instance_id(ctx: &RecordingCtx) -> Option<&str> {
    let data = ctx.writer.data();
    match data.role {
        Role::Post => Some(data.instance_id.as_str()),
        Role::Pre => data.paired_post_instance_id.as_deref(),
    }
}

fn refresh_pair_record_metadata_from_base(
    ctx: &mut RecordingCtx,
    base: &Path,
    post_instance_id: &str,
) {
    let project_hash = ctx.writer.data().project_hash.clone();
    if ctx.writer.data().record_session_id.is_none() {
        let session_id = resolve_record_session_id(base, &project_hash, post_instance_id);
        ctx.writer.set_record_session_id(session_id);
    }
    if ctx.writer.data().expected_wav.is_none() {
        let expected_wav = resolve_expected_wav_metadata(base, &project_hash, post_instance_id);
        ctx.writer.set_expected_wav(expected_wav);
    }
}

fn seal_bounce_marker(ctx: &mut RecordingCtx) {
    let end_ms = now_epoch_ms();
    let duration_ms = record_duration_ms(ctx);
    let duration_samples = record_duration_samples(ctx);
    ctx.writer
        .set_bounce_end(epoch_ms_to_iso8601(end_ms), duration_samples, String::new());
    ctx.writer
        .set_bounce_take(build_bounce_take(ctx, duration_ms, duration_samples));
}

fn expected_trace_samples(duration_ms: u64) -> usize {
    (duration_ms / FRAME_INTERVAL_MS) as usize
}

fn record_duration_ms(ctx: &RecordingCtx) -> u64 {
    if let Some(expected) = expected_wav(ctx) {
        return expected_duration_ms(expected);
    }
    let sample_rate = ctx.writer.data().sample_rate as u64;
    if let Some(clean) = ctx.clean_take {
        if sample_rate > 0 {
            return clean.duration_samples.saturating_mul(1_000) / sample_rate;
        }
    }
    if sample_rate > 0 {
        if let Some(native_frames) = ctx.last_trace_native_frames {
            return native_frames.saturating_mul(1_000) / sample_rate;
        }
    }
    if let Some(frames_48k) = ctx.last_trace_frame_48k {
        return frames_48k.saturating_mul(1_000) / TRACE_TIMEBASE_HZ;
    }
    if let Some(t_ms) = ctx.last_trace_t_ms {
        return t_ms;
    }
    let end_ms = now_epoch_ms();
    if ctx.started_at_ms > 0 {
        end_ms.saturating_sub(ctx.started_at_ms).max(0) as u64
    } else {
        0
    }
}

fn record_duration_samples(ctx: &RecordingCtx) -> u64 {
    if let Some(expected) = expected_wav(ctx) {
        return expected.expected_duration_samples;
    }
    let sample_rate = ctx.writer.data().sample_rate as u64;
    if let Some(clean) = ctx.clean_take {
        return clean.duration_samples;
    }
    if let Some(native_frames) = ctx.last_trace_native_frames {
        return native_frames;
    }
    if let Some(frames_48k) = ctx.last_trace_frame_48k {
        return frames_48k.saturating_mul(sample_rate) / TRACE_TIMEBASE_HZ;
    }
    record_duration_ms(ctx).saturating_mul(sample_rate) / 1_000
}

fn build_bounce_take(ctx: &RecordingCtx, duration_ms: u64, duration_samples: u64) -> BounceTake {
    let sample_rate = ctx.writer.data().sample_rate;
    let clean_source = ctx.clean_take.map(|take| take.source);
    let duration_frames_48k = if ctx.clean_take.is_some() {
        let sr = sample_rate as u64;
        if sr > 0 {
            duration_samples.saturating_mul(TRACE_TIMEBASE_HZ) / sr
        } else {
            duration_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000
        }
    } else {
        ctx.last_trace_frame_48k
            .unwrap_or_else(|| duration_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000)
    };
    let exact_native_time = ctx.last_trace_native_frames.is_some();
    let exact_audio_time = exact_native_time || ctx.last_trace_frame_48k.is_some();
    let expected_ready = expected_take_is_sample_count_ready(ctx, duration_ms);
    let clean_take_ready = clean_take_is_sample_count_ready(ctx, duration_ms);
    BounceTake {
        source: if expected_wav(ctx).is_some() {
            "expected_wav_duration_native".to_string()
        } else if let Some(source) = clean_source {
            source.to_string()
        } else if exact_native_time {
            "audio_time_trace_native".to_string()
        } else if exact_audio_time {
            "audio_time_trace".to_string()
        } else {
            "wall_clock_fallback".to_string()
        },
        time_axis: if expected_wav(ctx).is_some() || clean_source.is_some() || exact_native_time {
            "native_samples".to_string()
        } else if exact_audio_time {
            "frames_48k".to_string()
        } else {
            "wall_clock_ms".to_string()
        },
        alignment_status: if expected_ready || clean_take_ready {
            "sample_count_ready".to_string()
        } else if exact_audio_time {
            "trace_span_only".to_string()
        } else {
            "estimated".to_string()
        },
        sample_rate,
        wav_start_sample: 0,
        wav_end_sample: duration_samples,
        duration_samples,
        duration_frames_48k,
        start_t_ms: 0,
        end_t_ms: duration_ms,
        trace_sample_count: ctx.trace_sample_count as u64,
        frame_count: ctx.writer.data().frames.len() as u64,
    }
}

fn expected_wav(ctx: &RecordingCtx) -> Option<&ExpectedWavMetadata> {
    ctx.writer.data().expected_wav.as_ref()
}

fn expected_duration_ms(expected: &ExpectedWavMetadata) -> u64 {
    if expected.expected_sample_rate == 0 {
        return 0;
    }
    expected.expected_duration_samples.saturating_mul(1_000) / expected.expected_sample_rate as u64
}

fn bake_continuous_record_timeline(ctx: &mut RecordingCtx) {
    let duration_ms = record_duration_ms(ctx);
    if duration_ms == 0 {
        return;
    }
    if !should_bake_continuous_record_timeline(ctx) {
        return;
    }
    if ctx.trace_samples.is_empty() && ctx.writer.data().frames.is_empty() {
        return;
    }
    if ctx.trace_sample_count == 0 {
        ctx.trace_sample_count = ctx
            .trace_samples
            .len()
            .saturating_add(ctx.writer.data().frames.len());
    }
    if ctx.raw_trace_timeline_covers_duration.is_none() {
        ctx.raw_trace_timeline_covers_duration =
            Some(raw_trace_timeline_covers_duration(ctx, duration_ms));
    }

    let slots = continuous_timeline_slots(duration_ms);
    if slots.is_empty() {
        return;
    }

    let mut best_by_ms: BTreeMap<u64, MeasureResult> = BTreeMap::new();
    for frame in &ctx.writer.data().frames {
        if frame.t_ms <= duration_ms {
            if let Some(slot_ms) = nearest_timeline_slot(&slots, frame.t_ms) {
                insert_best_measure(&mut best_by_ms, slot_ms, measure_from_frame(frame));
            }
        }
    }
    for sample in &ctx.trace_samples {
        if sample.t_ms <= duration_ms && sample.has_measured_core() {
            if let Some(slot_ms) = nearest_timeline_slot(&slots, sample.t_ms) {
                insert_best_measure(&mut best_by_ms, slot_ms, sample.result.clone());
            }
        }
    }

    ctx.writer.clear_frames();
    ctx.first_frame_logged = false;
    let expected_frame_count = slots.len() as u64;
    let fill_missing_silence_slots =
        should_backfill_silent_trace_slots(ctx, slots.len(), &best_by_ms);
    let mut missing_slots = 0_u64;
    let mut backfilled_silence_slots = 0_u64;
    for t_ms in slots {
        if let Some(result) = best_by_ms.get(&t_ms) {
            let _ = writer_append_trace_frame(ctx, t_ms, result);
        } else if fill_missing_silence_slots {
            let _ = writer_append_trace_frame(ctx, t_ms, &trace_silence_measure_result());
            backfilled_silence_slots = backfilled_silence_slots.saturating_add(1);
        } else {
            missing_slots = missing_slots.saturating_add(1);
        }
    }
    if backfilled_silence_slots > 0 && missing_slots == 0 {
        ctx.raw_trace_timeline_covers_duration = Some(true);
    }
    let measured_frame_count = ctx.writer.data().frames.len() as u64;
    let explicit_silence_frame_count = ctx
        .writer
        .data()
        .frames
        .iter()
        .filter(|frame| {
            frame.lufs_m <= TRACE_SILENCE_LUFS + 0.1
                && frame.true_peak <= TRACE_SILENCE_TRUE_PEAK_DBTP + 0.1
                && frame.crest.abs() <= 0.1
        })
        .count() as u64;
    ctx.writer.set_trace_diagnostics(TraceDiagnostics {
        raw_trace_count: ctx.trace_sample_count as u64,
        expected_frame_count,
        measured_frame_count,
        missing_slots,
        explicit_silence_frame_count,
    });
    if missing_slots > 0 {
        ctx.writer.mark_integrity_degraded();
        ctx.writer.add_integrity_reason("missing_trace_slots");
    }
}

fn should_backfill_silent_trace_slots(
    ctx: &RecordingCtx,
    expected_frame_count: usize,
    best_by_ms: &BTreeMap<u64, MeasureResult>,
) -> bool {
    if expected_frame_count == 0 || best_by_ms.is_empty() {
        return false;
    }
    if expected_wav(ctx).is_none() && ctx.clean_take.is_none() {
        return false;
    }
    if !best_by_ms.values().all(is_trace_silence_floor) {
        return false;
    }
    if !observed_trace_slots_are_contiguous(best_by_ms) {
        return false;
    }
    best_by_ms
        .len()
        .saturating_mul(CLEAN_TAKE_MIN_TRACE_RATIO_DEN)
        >= expected_frame_count.saturating_mul(CLEAN_TAKE_MIN_TRACE_RATIO_NUM)
}

fn observed_trace_slots_are_contiguous(best_by_ms: &BTreeMap<u64, MeasureResult>) -> bool {
    let mut previous = None;
    for t_ms in best_by_ms.keys().copied() {
        if let Some(previous_t_ms) = previous {
            if t_ms.saturating_sub(previous_t_ms) > FRAME_COVERAGE_MAX_GAP_MS {
                return false;
            }
        }
        previous = Some(t_ms);
    }
    true
}

fn should_bake_continuous_record_timeline(ctx: &RecordingCtx) -> bool {
    ctx.clean_take.is_some() || !ctx.trace_samples.is_empty()
}

fn continuous_timeline_slots(duration_ms: u64) -> Vec<u64> {
    let mut slots = Vec::with_capacity(expected_trace_samples(duration_ms));
    let end_grid = (duration_ms / FRAME_INTERVAL_MS) * FRAME_INTERVAL_MS;
    let mut t_ms = FRAME_INTERVAL_MS;
    while t_ms <= end_grid {
        slots.push(t_ms);
        t_ms = t_ms.saturating_add(FRAME_INTERVAL_MS);
    }
    slots
}

fn nearest_timeline_slot(slots: &[u64], t_ms: u64) -> Option<u64> {
    if slots.is_empty() {
        return None;
    }
    match slots.binary_search(&t_ms) {
        Ok(idx) => Some(slots[idx]),
        Err(idx) => {
            let prev = idx.checked_sub(1).map(|i| slots[i]);
            let next = slots.get(idx).copied();
            let best = match (prev, next) {
                (Some(a), Some(b)) => {
                    let da = t_ms.saturating_sub(a);
                    let db = b.saturating_sub(t_ms);
                    if da <= db {
                        a
                    } else {
                        b
                    }
                }
                (Some(a), None) => a,
                (None, Some(b)) => b,
                (None, None) => return None,
            };
            let distance = best.abs_diff(t_ms);
            (distance <= TRACE_SLOT_ASSIGNMENT_TOLERANCE_MS).then_some(best)
        }
    }
}

fn measure_from_frame(frame: &crate::plugin_data::Frame) -> MeasureResult {
    MeasureResult {
        lufs_m: Some(frame.lufs_m),
        true_peak: Some(frame.true_peak),
        crest: Some(frame.crest),
        psr: frame.psr,
        n_prime: frame.n_prime,
        sharpness: frame.sharpness,
        ..MeasureResult::default()
    }
}

fn insert_best_measure(
    map: &mut BTreeMap<u64, MeasureResult>,
    t_ms: u64,
    candidate: MeasureResult,
) {
    let should_replace = map.get(&t_ms).is_none_or(|existing| {
        measure_quality_score(&candidate) >= measure_quality_score(existing)
    });
    if should_replace {
        map.insert(t_ms, candidate);
    }
}

fn measure_has_core_metrics(m: &MeasureResult) -> bool {
    m.lufs_m.is_some() && m.true_peak.is_some() && m.crest.is_some()
}

fn with_trace_silence_core(mut result: MeasureResult) -> MeasureResult {
    result.lufs_m = Some(TRACE_SILENCE_LUFS);
    result.true_peak = Some(TRACE_SILENCE_TRUE_PEAK_DBTP);
    result.crest = Some(TRACE_SILENCE_CREST_DB);
    result
}

fn trace_silence_measure_result() -> MeasureResult {
    with_trace_silence_core(MeasureResult::default())
}

fn measure_quality_score(m: &MeasureResult) -> u8 {
    let core_fields =
        m.lufs_m.is_some() as u8 + m.true_peak.is_some() as u8 + m.crest.is_some() as u8;
    let phase_fields = m.n_prime.is_some() as u8 + m.sharpness.is_some() as u8;
    let signal_bonus = if is_trace_silence_floor(m) { 0 } else { 8 };
    signal_bonus + core_fields + phase_fields
}

fn is_trace_silence_floor(m: &MeasureResult) -> bool {
    m.lufs_m.is_some_and(|v| v <= TRACE_SILENCE_LUFS + 0.1)
        && m.true_peak
            .is_some_and(|v| v <= TRACE_SILENCE_TRUE_PEAK_DBTP + 0.1)
        && m.crest.is_some_and(|v| v.abs() <= 0.1)
}

fn clip_clean_timeline_to_duration(ctx: &mut RecordingCtx) {
    if ctx.clean_take.is_some() {
        let duration_ms = record_duration_ms(ctx);
        ctx.writer.clip_timeline_to_duration(duration_ms);
    }
}

fn frame_timeline_covers_duration(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let frames = &ctx.writer.data().frames;
    let Some(first) = frames.first() else {
        return false;
    };
    if first.t_ms > FRAME_COVERAGE_EDGE_TOLERANCE_MS {
        return false;
    }
    if duration_ms > 0 {
        let Some(last) = frames.last() else {
            return false;
        };
        if last.t_ms.saturating_add(FRAME_COVERAGE_EDGE_TOLERANCE_MS) < duration_ms {
            return false;
        }
    }
    frames
        .windows(2)
        .all(|pair| pair[1].t_ms.saturating_sub(pair[0].t_ms) <= FRAME_COVERAGE_MAX_GAP_MS)
}

fn frame_timeline_has_gap(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    !frame_timeline_covers_duration(ctx, duration_ms)
}

fn raw_trace_timeline_covers_duration(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let mut times = Vec::with_capacity(
        ctx.trace_samples
            .len()
            .saturating_add(ctx.writer.data().frames.len()),
    );
    for frame in &ctx.writer.data().frames {
        if frame.t_ms <= duration_ms {
            times.push(frame.t_ms);
        }
    }
    for sample in &ctx.trace_samples {
        if sample.t_ms <= duration_ms && sample.has_measured_core() {
            times.push(sample.t_ms);
        }
    }
    timeline_times_cover_duration(&mut times, duration_ms)
}

fn timeline_times_cover_duration(times: &mut Vec<u64>, duration_ms: u64) -> bool {
    if times.is_empty() {
        return false;
    }
    times.sort_unstable();
    times.dedup();
    let Some(first) = times.first() else {
        return false;
    };
    if *first > FRAME_COVERAGE_EDGE_TOLERANCE_MS {
        return false;
    }
    if duration_ms > 0 {
        let Some(last) = times.last() else {
            return false;
        };
        if last.saturating_add(FRAME_COVERAGE_EDGE_TOLERANCE_MS) < duration_ms {
            return false;
        }
    }
    times
        .windows(2)
        .all(|pair| pair[1].saturating_sub(pair[0]) <= FRAME_COVERAGE_MAX_GAP_MS)
}

fn clean_take_is_sample_count_ready(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let raw_timeline_ready = ctx
        .raw_trace_timeline_covers_duration
        .unwrap_or_else(|| frame_timeline_covers_duration(ctx, duration_ms));
    ctx.clean_take
        .is_some_and(|take| take.source == RECORD_TAKE_SOURCE_WAV_CLOCK)
        && duration_ms >= CLEAN_TAKE_MIN_DURATION_MS
        && frame_timeline_covers_duration(ctx, duration_ms)
        && raw_timeline_ready
        && clean_take_trace_density_ready(ctx, duration_ms)
}

fn expected_take_is_sample_count_ready(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let Some(expected) = expected_wav(ctx) else {
        return false;
    };
    if !expected.is_usable() || duration_ms < CLEAN_TAKE_MIN_DURATION_MS {
        return false;
    }
    if ctx.writer.data().sample_rate != expected.expected_sample_rate {
        return false;
    }
    if let Some(diag) = &ctx.writer.data().trace_diagnostics {
        return diag.missing_slots == 0
            && diag.measured_frame_count == diag.expected_frame_count
            && diag.expected_frame_count > 0;
    }
    frame_timeline_covers_duration(ctx, duration_ms)
}

fn clean_take_trace_density_ready(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let expected = expected_trace_samples(duration_ms);
    if expected == 0 {
        return false;
    }
    let raw_count = if !ctx.trace_samples.is_empty() {
        measured_trace_sample_count(ctx)
    } else if ctx.trace_sample_count > 0 {
        ctx.trace_sample_count
    } else {
        ctx.writer.data().frames.len()
    };
    raw_count.saturating_mul(CLEAN_TAKE_MIN_TRACE_RATIO_DEN)
        >= expected.saturating_mul(CLEAN_TAKE_MIN_TRACE_RATIO_NUM)
}

fn trace_density_is_sparse(ctx: &RecordingCtx) -> bool {
    if ctx.writer.data().frames.is_empty() {
        return true;
    }
    let duration_ms = record_duration_ms(ctx);
    if ctx.raw_trace_timeline_covers_duration == Some(false) {
        return true;
    }
    if frame_timeline_has_gap(ctx, duration_ms) {
        return true;
    }
    if duration_ms < TRACE_DENSITY_MIN_DURATION_MS {
        return false;
    }
    if ctx.trace_sample_count > 0 {
        let expected = expected_trace_samples(duration_ms);
        let measured = if !ctx.trace_samples.is_empty() {
            measured_trace_sample_count(ctx)
        } else {
            ctx.trace_sample_count
        };
        return measured.saturating_mul(TRACE_DENSITY_MIN_RATIO_DEN)
            < expected.saturating_mul(TRACE_DENSITY_MIN_RATIO_NUM);
    }
    fallback_frame_density_is_sparse(ctx, duration_ms)
}

fn measured_trace_sample_count(ctx: &RecordingCtx) -> usize {
    ctx.trace_samples
        .iter()
        .filter(|sample| sample.has_measured_core())
        .count()
}

fn fallback_frame_density_is_sparse(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let frame_count = ctx.writer.data().frames.len();
    if frame_count == 0 {
        return true;
    }
    if frame_timeline_has_gap(ctx, duration_ms) {
        return true;
    }
    let expected = expected_trace_samples(duration_ms);
    frame_count.saturating_mul(TRACE_DENSITY_MIN_RATIO_DEN)
        < expected.saturating_mul(TRACE_DENSITY_MIN_RATIO_NUM)
}

fn mark_integrity_if_trace_density_is_sparse(ctx: &mut RecordingCtx) {
    let duration_ms = record_duration_ms(ctx);
    let has_frame_gap = frame_timeline_has_gap(ctx, duration_ms);
    let has_raw_gap = ctx.raw_trace_timeline_covers_duration == Some(false);
    if trace_density_is_sparse(ctx) {
        log::warn!(
            "[writer] sparse Record density: trace_samples={} frames={} duration_ms={} - marking integrity_degraded",
            ctx.trace_sample_count,
            ctx.writer.data().frames.len(),
            duration_ms
        );
        ctx.writer.mark_integrity_degraded();
        if ctx.writer.data().frames.is_empty() {
            ctx.writer.add_integrity_reason("zero_trace_frames");
        } else if has_raw_gap {
            ctx.writer.add_integrity_reason("raw_trace_timeline_gap");
        } else if has_frame_gap {
            ctx.writer.add_integrity_reason("frame_timeline_gap");
        } else {
            ctx.writer.add_integrity_reason("sparse_trace_density");
        }
    }
}

fn mark_integrity_if_record_clock_is_not_wav_bounded(ctx: &mut RecordingCtx) {
    if expected_wav(ctx).is_some() {
        return;
    }
    let Some(clean_take) = ctx.clean_take else {
        return;
    };
    if clean_take.source == RECORD_TAKE_SOURCE_WAV_CLOCK {
        return;
    }
    log::warn!(
        "[writer] Record clean take source is {} rather than wav_clock_native - marking integrity_degraded",
        clean_take.source
    );
    ctx.writer.mark_integrity_degraded();
    ctx.writer
        .add_integrity_reason("record_clock_not_wav_bounded");
}

fn mark_integrity_if_record_is_too_short(ctx: &mut RecordingCtx) {
    let has_audio_time = ctx.clean_take.is_some()
        || ctx.last_trace_frame_48k.is_some()
        || ctx.last_trace_native_frames.is_some();
    if !has_audio_time {
        return;
    }
    let duration_ms = record_duration_ms(ctx);
    if duration_ms >= CLEAN_TAKE_MIN_DURATION_MS {
        return;
    }
    let fragment_without_clean_take = ctx.clean_take.is_none() && ctx.trace_sample_count <= 1;
    if ctx.clean_take.is_none() && !fragment_without_clean_take {
        return;
    }
    log::warn!(
        "[writer] short Record fragment: duration_ms={} frames={} trace_samples={} - marking integrity_degraded",
        duration_ms,
        ctx.writer.data().frames.len(),
        ctx.trace_sample_count
    );
    ctx.writer.mark_integrity_degraded();
    ctx.writer.add_integrity_reason("record_too_short");
}

/// Record 終了 (B-043 セッション集計注入版)。
///
/// `summary` が `Some` の場合、`set_session_aggregates()` で LUFS-I / LRA / PLR
/// を JSON に焼き込んでから close する。Measure Thread が Record 中に共有スロットに
/// 書いた最新値を呼出側が `take_session_summary` で読んで渡す。
pub fn writer_close_with_summary(mut ctx: RecordingCtx, summary: Option<SessionSummary>) {
    if let Some(s) = summary {
        ctx.writer.set_session_aggregates(s);
    }
    writer_close(ctx);
}

/// B-134 (G-115-391): degraded 終了専用 close。**best-effort** で
/// `integrity_degraded` を立ててから close する。
///
/// - storage 書込可 → file flag が永続し README:139「incomplete measurement is recorded as incomplete」を
///   degraded close 経路でも真にする（JD-README139 Path1 / 過少申告 gap 解消）。
/// - storage-loss（dir 喪失 / disk full）→ 最終 flush が失敗し flag は永続しないが、
///   Record停止権限は持たせない。明示Stop/10分無音の終了時に、書ける状態なら
///   degraded flag を永続化する。
///
/// `mark_integrity_degraded` は data field のみ・計測値（LUFS/TP/PSR/PSB）非接触。
pub fn writer_close_degraded(mut ctx: RecordingCtx) {
    ctx.writer.mark_integrity_degraded();
    ctx.writer.add_integrity_reason("degraded_close");
    writer_close(ctx);
}

/// `Arc<Mutex<Option<SessionSummary>>>` から最新値を取り出して `None` でクリアする (B-043)。
///
/// IO Thread が Record→Watch 遷移時に呼ぶ。次の Record セッションは
/// Measure Thread の Watch→Record 遷移ハンドラが上書き None するため、
/// ここでも一応クリアして冪等性を担保する。
pub fn take_session_summary(slot: &Arc<Mutex<Option<SessionSummary>>>) -> Option<SessionSummary> {
    match slot.lock() {
        Ok(mut g) => g.take(),
        Err(_) => None,
    }
}

/// TRACE の主要 3 フィールドが揃ったとき 1 frame を追記する。
/// Phase D は未ウォームアップでも optional のまま残し、core trace を欠落させない。
pub fn writer_append_frame(ctx: &mut RecordingCtx, t_ms: u64, m: &MeasureResult) -> bool {
    let (Some(lufs_m), Some(true_peak), Some(crest)) = (m.lufs_m, m.true_peak, m.crest) else {
        return false;
    };
    ctx.writer.append_frame_optional(
        t_ms,
        m.n_prime,
        m.sharpness,
        lufs_m,
        true_peak,
        crest,
        m.psr,
    );
    if !ctx.first_frame_logged {
        match m.n_prime {
            Some(n_prime) => log::info!(
                "[writer] frame written: t_ms={}, n_prime[0]={:.3}",
                t_ms,
                n_prime[0]
            ),
            None => log::info!("[writer] frame written: t_ms={}, phase_d=pending", t_ms),
        }
        ctx.first_frame_logged = true;
    }
    ctx.last_trace_t_ms = Some(ctx.last_trace_t_ms.map_or(t_ms, |prev| prev.max(t_ms)));
    true
}

fn writer_append_trace_frame(ctx: &mut RecordingCtx, t_ms: u64, m: &MeasureResult) -> bool {
    let (Some(lufs_m), Some(true_peak), Some(crest)) = (m.lufs_m, m.true_peak, m.crest) else {
        return false;
    };
    let n_prime = m.n_prime;
    let sharpness = m.sharpness;
    ctx.writer
        .append_frame_optional(t_ms, n_prime, sharpness, lufs_m, true_peak, crest, m.psr);
    if !ctx.first_frame_logged {
        match n_prime {
            Some(n_prime) => log::info!(
                "[writer] frame written: t_ms={}, n_prime[0]={:.3}",
                t_ms,
                n_prime[0]
            ),
            None => log::info!("[writer] frame written: t_ms={}, phase_d=pending", t_ms),
        }
        ctx.first_frame_logged = true;
    }
    ctx.last_trace_t_ms = Some(ctx.last_trace_t_ms.map_or(t_ms, |prev| prev.max(t_ms)));
    true
}

fn mark_trace_time(
    ctx: &mut RecordingCtx,
    t_ms: u64,
    t_frames_48k: u64,
    t_native_frames: Option<u64>,
) {
    ctx.last_trace_t_ms = Some(ctx.last_trace_t_ms.map_or(t_ms, |prev| prev.max(t_ms)));
    ctx.last_trace_frame_48k = Some(
        ctx.last_trace_frame_48k
            .map_or(t_frames_48k, |prev| prev.max(t_frames_48k)),
    );
    if let Some(native_frames) = t_native_frames {
        ctx.last_trace_native_frames = Some(
            ctx.last_trace_native_frames
                .map_or(native_frames, |prev| prev.max(native_frames)),
        );
    }
}

/// PSB スナップショット追記。`psb_bark` が None ならスキップ。
pub fn writer_append_psb(ctx: &mut RecordingCtx, t_ms: u64, m: &MeasureResult) -> bool {
    let Some(psb_bark) = m.psb_bark else {
        return false;
    };
    ctx.writer.append_psb(t_ms, psb_bark, true);
    true
}

/// Record モード 1 ティック: Watch↔Record 遷移と Frame/PSB 追記を処理する。
///
/// - 毎ループ呼ばれる（100ms 間隔）
/// - (false → true) 遷移 → `started_at_resolver` を呼び `writer_start`
/// - (true → false) 遷移 → `writer_close`
/// - Record 継続 → `next_frame_ms` / `next_psb_ms` に沿って append
/// - 30 秒経過 → `heartbeat_now` + `flush`
///
/// `started_at_resolver` は Watch→Record 遷移時にだけ呼ばれる（lazy）。
/// PRE は `partner_post_instance_id` の signal.started_at を、POST は自身の
/// signal.started_at を解決する用途で使う。
///
/// `paired_pre_resolver` / `paired_post_resolver` も Watch→Record 遷移時に
/// 1 度だけ呼ばれる。PRE は `paired_post` を返し、POST は `paired_pre` を返す
/// （v1.2 (a) cross-instance pair 復元キー）。
#[allow(clippy::too_many_arguments)]
pub fn run_record_tick(
    record_sm: &Arc<RecordStateMachine>,
    role: Role,
    sample_rate: u32,
    project_hash: &str,
    instance_id: &str,
    started_at_resolver: impl FnOnce() -> i64,
    paired_pre_resolver: impl FnOnce() -> Option<String>,
    paired_post_resolver: impl FnOnce() -> Option<String>,
    measure_result: &Arc<Mutex<MeasureResult>>,
    recording: &mut Option<RecordingCtx>,
    session_summary: Option<&Arc<Mutex<Option<SessionSummary>>>>,
    // B-076: 累積 push_overflow（Audio Thread が ring 満杯時に積む）。Record 開始で snapshot し、
    // close 時に差分を per-Record dropped_samples として JSON に焼き込む。
    overflow: &Arc<std::sync::atomic::AtomicU64>,
    // B-125: 累積 oversized_drop（JUCE 殻が prealloc-max 超の病的 block を drop した interleaved
    // sample 数）。overflow とは別カウンタ（混ぜない）。同様に Record 開始 snapshot → close 差分。
    oversized_drop: &Arc<std::sync::atomic::AtomicU64>,
    record_trace_queue: Option<&RecordTraceQueue>,
) -> Result<(), String> {
    run_record_tick_with_pair_names(
        record_sm,
        role,
        sample_rate,
        project_hash,
        instance_id,
        started_at_resolver,
        paired_pre_resolver,
        paired_post_resolver,
        || None,
        || None,
        measure_result,
        recording,
        session_summary,
        overflow,
        oversized_drop,
        record_trace_queue,
        None,
    )
}

/// [`run_record_tick`] に Lens 表示用 pair 名 resolver を追加した Record tick。
/// 既存 caller は名前なし wrapper を使えるが、PRE/POST IO Thread は Record 開始時の
/// 人間可読名をここで焼く。
#[allow(clippy::too_many_arguments)]
pub fn run_record_tick_with_pair_names(
    record_sm: &Arc<RecordStateMachine>,
    role: Role,
    sample_rate: u32,
    project_hash: &str,
    instance_id: &str,
    started_at_resolver: impl FnOnce() -> i64,
    paired_pre_resolver: impl FnOnce() -> Option<String>,
    paired_post_resolver: impl FnOnce() -> Option<String>,
    pair_name_resolver: impl FnOnce() -> Option<String>,
    pair_pre_name_resolver: impl FnOnce() -> Option<String>,
    measure_result: &Arc<Mutex<MeasureResult>>,
    recording: &mut Option<RecordingCtx>,
    session_summary: Option<&Arc<Mutex<Option<SessionSummary>>>>,
    // B-076: 累積 push_overflow（Audio Thread が ring 満杯時に積む）。Record 開始で snapshot し、
    // close 時に差分を per-Record dropped_samples として JSON に焼き込む。
    overflow: &Arc<std::sync::atomic::AtomicU64>,
    // B-125: 累積 oversized_drop（JUCE 殻が prealloc-max 超の病的 block を drop した interleaved
    // sample 数）。overflow とは別カウンタ（混ぜない）。同様に Record 開始 snapshot → close 差分。
    oversized_drop: &Arc<std::sync::atomic::AtomicU64>,
    record_trace_queue: Option<&RecordTraceQueue>,
    record_take_tracker: Option<&RecordTakeTracker>,
) -> Result<(), String> {
    let is_recording = record_sm.is_recording();
    let current_generation = record_sm.generation();
    let generation_changed = is_recording
        && recording.as_ref().is_some_and(|ctx| {
            ctx.record_generation > 0 && ctx.record_generation != current_generation
        });
    if (!is_recording || generation_changed) && recording.is_some() {
        if generation_changed {
            log::warn!(
                "[writer] Record generation changed before IO close (old={}, new={}); closing old context without mixing",
                recording.as_ref().map_or(0, |ctx| ctx.record_generation),
                current_generation
            );
        }
        if let Some(ctx) = recording.take() {
            close_recording_context(
                record_sm,
                measure_result,
                session_summary,
                overflow,
                oversized_drop,
                record_trace_queue,
                record_take_tracker,
                ctx,
            )?;
        }
    }
    let is_recording = record_sm.is_recording();
    match (is_recording, recording.is_some()) {
        (true, false) => {
            let paired_pre = paired_pre_resolver();
            let paired_post = paired_post_resolver();
            let started_at_ms = started_at_resolver();
            let pair_name = pair_name_resolver();
            let pair_pre_name = pair_pre_name_resolver();
            if let Some(mut ctx) = writer_start(
                role,
                sample_rate,
                started_at_ms,
                project_hash,
                instance_id,
                paired_pre,
                paired_post,
                pair_name,
                pair_pre_name,
            ) {
                // B-076: Record 開始時点の累積 overflow を記録（per-Record 差分の基点）。
                ctx.overflow_start = overflow.load(std::sync::atomic::Ordering::Relaxed);
                // B-125: oversized_drop も同位相で snapshot（別カウンタ・同じ差分方式）。
                ctx.oversized_drop_start =
                    oversized_drop.load(std::sync::atomic::Ordering::Relaxed);
                // B-132 (G-115-382): drain-completion seal の基点を snapshot。close 時に
                // この値の前進（measure の post-drain finalize 完了）を bounded 待ちする。
                ctx.seal_at_start = record_sm.seal();
                ctx.record_generation = record_sm.generation();
                *recording = Some(ctx);
            }
        }
        (false, true) => {
            if let Some(ctx) = recording.take() {
                close_recording_context(
                    record_sm,
                    measure_result,
                    session_summary,
                    overflow,
                    oversized_drop,
                    record_trace_queue,
                    record_take_tracker,
                    ctx,
                )?;
            }
        }
        (true, true) => {
            let ctx = recording.as_mut().expect("some because of match arm");
            if let Some(queue) = record_trace_queue {
                let _ = drain_trace_queue_into_writer(ctx, queue);
            } else {
                append_current_measure_snapshot(ctx, measure_result)?;
            }
            if Instant::now() >= ctx.next_flush {
                ctx.writer.heartbeat_now();
                match ctx.writer.flush() {
                    Ok(()) => {
                        // B-025 Gap-19/20: 成功で counter リセット (transient 失敗を吸収)。
                        ctx.consecutive_write_error = 0;
                    }
                    Err(e) => {
                        ctx.consecutive_write_error = ctx.consecutive_write_error.saturating_add(1);
                        ctx.writer.mark_integrity_degraded();
                        log::warn!(
                            "[writer] flush failed: {} (count={}/{}); Record remains armed",
                            e,
                            ctx.consecutive_write_error,
                            CONSECUTIVE_FAILURE_THRESHOLD
                        );
                        if ctx.consecutive_write_error >= CONSECUTIVE_FAILURE_THRESHOLD {
                            log::warn!(
                                "[writer] write failure persisted for {} flush attempts; keeping Record armed",
                                ctx.consecutive_write_error
                            );
                        }
                    }
                }
                ctx.next_flush = Instant::now() + FLUSH_INTERVAL;
            }
        }
        (false, false) => {}
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)]
fn close_recording_context(
    record_sm: &Arc<RecordStateMachine>,
    measure_result: &Arc<Mutex<MeasureResult>>,
    session_summary: Option<&Arc<Mutex<Option<SessionSummary>>>>,
    overflow: &Arc<std::sync::atomic::AtomicU64>,
    oversized_drop: &Arc<std::sync::atomic::AtomicU64>,
    record_trace_queue: Option<&RecordTraceQueue>,
    record_take_tracker: Option<&RecordTakeTracker>,
    mut ctx: RecordingCtx,
) -> Result<(), String> {
    // ── B-132 (G-115-382) P1/共通B: drain-completion barrier ──────────────
    // Measure Thread が Record→Watch エッジで ring 残量を tight-drain → 最終 finalize →
    // session_summary 書込を完了し seal を前進させるのを **bounded** に待ってから取り出す。
    // これで「post-drain の確定スナップショット」のみ焼く（live meter は元々別スロット）。
    let sealed = wait_for_seal(record_sm, ctx.seal_at_start, SEAL_WAIT_TIMEOUT);
    // B-043: Record→Watch 遷移時に Measure Thread の最新セッション集計を取り出して注入。
    let summary = session_summary.and_then(take_session_summary);
    // B-076: この Record 中に ring 満杯で落ちたサンプル数 = 現在の累積 - 開始時 snapshot。
    let push_dropped = overflow
        .load(std::sync::atomic::Ordering::Relaxed)
        .saturating_sub(ctx.overflow_start);
    // B-125: oversized block で落ちたサンプル数（別カウンタの per-Record 差分）。
    let oversized_dropped = oversized_drop
        .load(std::sync::atomic::Ordering::Relaxed)
        .saturating_sub(ctx.oversized_drop_start);
    // 別計数のまま set_integrity に渡し、JSON へは合算を焼く（ZSA / B-125 裁定）。
    ctx.writer.set_integrity(push_dropped, oversized_dropped);
    // B-132 共通B: seal が timeout（measure 死 / shutdown / stall で finalize 不能）なら
    // 「不完全を不完全と記録」する（set_integrity の後に OR で立てる / silent truncation 排除）。
    if !sealed {
        log::warn!(
            "[writer] drain seal not observed within {:?} — marking integrity_degraded (incomplete tail)",
            SEAL_WAIT_TIMEOUT
        );
        ctx.writer.mark_integrity_degraded();
        ctx.writer.add_integrity_reason("drain_seal_timeout");
    }
    if let Some(queue) = record_trace_queue {
        let _ = drain_trace_queue_into_writer(&mut ctx, queue);
    } else {
        append_current_measure_snapshot(&mut ctx, measure_result)?;
    }
    apply_record_take_snapshot(&mut ctx, record_take_tracker);
    writer_close_with_summary(ctx, summary);
    Ok(())
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
struct TraceDrainStats {
    samples: usize,
    frames: usize,
}

fn append_current_measure_snapshot(
    ctx: &mut RecordingCtx,
    measure_result: &Arc<Mutex<MeasureResult>>,
) -> Result<bool, String> {
    let m = measure_result
        .lock()
        .map_err(|e| format!("measure Mutex poisoned: {e}"))?
        .clone();
    let now_ms = now_epoch_ms();
    let t_ms = now_ms.saturating_sub(ctx.started_at_ms).max(0) as u64;

    let mut wrote_frame = false;
    if t_ms >= ctx.next_frame_ms && writer_append_frame(ctx, t_ms, &m) {
        ctx.next_frame_ms = (t_ms / FRAME_INTERVAL_MS + 1) * FRAME_INTERVAL_MS;
        wrote_frame = true;
    }
    if t_ms >= ctx.next_psb_ms && writer_append_psb(ctx, t_ms, &m) {
        ctx.next_psb_ms = (t_ms / PSB_INTERVAL_MS + 1) * PSB_INTERVAL_MS;
    }
    Ok(wrote_frame)
}

fn drain_trace_queue_into_writer(
    ctx: &mut RecordingCtx,
    queue: &RecordTraceQueue,
) -> TraceDrainStats {
    let mut stats = TraceDrainStats::default();
    for sample in drain_record_trace_queue_for_generation(queue, ctx.record_generation) {
        stats.samples += 1;
        let t_ms = sample.t_ms;
        mark_trace_time(ctx, t_ms, sample.t_frames_48k, sample.t_native_frames);
        ctx.trace_samples.push(sample.clone());
        if sample.has_measured_core()
            && t_ms >= ctx.next_frame_ms
            && writer_append_trace_frame(ctx, t_ms, &sample.result)
        {
            ctx.next_frame_ms = (t_ms / FRAME_INTERVAL_MS + 1) * FRAME_INTERVAL_MS;
            stats.frames += 1;
        }
        if sample.include_psb
            && sample.kind == RecordTraceKind::Measured
            && t_ms >= ctx.next_psb_ms
            && writer_append_psb(ctx, t_ms, &sample.result)
        {
            ctx.next_psb_ms = (t_ms / PSB_INTERVAL_MS + 1) * PSB_INTERVAL_MS;
        }
    }
    ctx.trace_sample_count = ctx.trace_sample_count.saturating_add(stats.samples);
    stats
}

// ── B-025 Group B-1 / Gap-8: Startup orphan .tmp recovery ───────────────────

/// `recover_orphan_tmps` の集計。GUI 通知不要 (起動時の 1 回処理 / R-28 機能的沈黙)
/// なので `log::info!` 経由でのみ可視化し、戻り値は呼出側のテスト/診断用。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct RecoveryReport {
    /// `.tmp` / stale `.partial` を `.json` または `.failed/*.json` に atomic publish した件数。
    pub recovered: usize,
    /// `.pair_pending` に残っていた PRE/POST session を committed/failed へ収束させた件数。
    pub pair_finalized: usize,
    /// HMAC-SHA256 / serde 整合性に失敗し放置した件数 (削除しない / 将来分析用)。
    pub orphaned: usize,
}

/// B-025 Group B-1 / Gap-8: 30 秒 flush 周期中の DAW crash で残った `*.json.tmp`
/// と、stale 化した `*.json.partial` を起動時に拾い、整合 (`verify_checksum`) すれば
/// 対応 `*.json` か `.failed/*.json` に atomic publish する。
/// 不整合は warn ログのみで残置 (削除しない / 将来分析用 / 約束 5 原則)。
///
/// 走査対象: `plugin_data_root` 配下を再帰的に walk して
/// `*.json.tmp` または `*.json.partial` を持つ通常ファイル全件。`.partial` は実行中の
/// Record と衝突しないよう mtime stale のものだけ救済する。
///
/// PRE / POST / IO Thread の `thread::spawn` 直後 1 回だけ呼ぶ (Group A
/// `clear_stale_self_acks_at_startup` と同位相)。loop 内で繰り返さない。
///
/// R-28 機能的沈黙: storage path 解決失敗 / read_dir 失敗 / ファイル read 失敗
/// は当該 dir/file のみ skip。UI エラー出さず log のみ。
pub fn recover_orphan_tmps(plugin_data_root: &Path) -> RecoveryReport {
    let mut report = RecoveryReport::default();
    if !plugin_data_root.is_dir() {
        return report;
    }
    walk_tmp_recover(plugin_data_root, &mut report);
    report.pair_finalized = crate::plugin_data::finalize_pair_pending_sessions(plugin_data_root);
    if report.recovered > 0 || report.pair_finalized > 0 || report.orphaned > 0 {
        log::info!(
            "[recover] startup record recovery: recovered={} pair_finalized={} orphaned={} root={}",
            report.recovered,
            report.pair_finalized,
            report.orphaned,
            plugin_data_root.display()
        );
    }
    report
}

/// `recover_orphan_tmps` の実走査。`fs::read_dir` 失敗は当該 dir のみ skip。
fn walk_tmp_recover(dir: &Path, report: &mut RecoveryReport) {
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let ftype = match entry.file_type() {
            Ok(t) => t,
            Err(_) => continue,
        };
        if ftype.is_dir() {
            walk_tmp_recover(&path, report);
            continue;
        }
        if !ftype.is_file() {
            continue;
        }
        // `.json.tmp` / `.json.partial` で終わるファイルのみ対象。
        let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
            continue;
        };
        if name.ends_with(".json.tmp") {
            recover_one_tmp(&path, report);
        } else if name.ends_with(".json.partial") {
            recover_one_staging(&path, report);
        }
    }
}

/// `.json.tmp` 1 件を試行する。checksum が正しく、最低 1 frame ある Record は
/// status=Closed として `.json` に救済する。frame なし等の未使用 record は
/// `.failed/*.json` に隔離し、Kirin OS の通常棚へ publish しない。
/// 不整合は `report.orphaned += 1` で記録 (削除しない)。
fn recover_one_tmp(tmp_path: &Path, report: &mut RecoveryReport) {
    let final_path = strip_tmp_suffix(tmp_path);
    recover_one_record_artifact(tmp_path, &final_path, report);
}

/// `.json.partial` 1 件を試行する。fresh partial は実行中 Record の可能性があるため触らない。
/// stale かつ usable frame を持つものだけ通常棚へ publish し、usable でないものは
/// `.failed/*.json` に隔離する。
fn recover_one_staging(staging_path: &Path, report: &mut RecoveryReport) {
    if !recovery_candidate_is_stale(staging_path) {
        return;
    }
    let final_path = strip_partial_suffix(staging_path);
    recover_one_record_artifact(staging_path, &final_path, report);
}

fn recover_one_record_artifact(source_path: &Path, final_path: &Path, report: &mut RecoveryReport) {
    let bytes = match fs::read(source_path) {
        Ok(b) => b,
        Err(e) => {
            log::warn!(
                "[recover] read failed (skip): {} ({})",
                source_path.display(),
                e
            );
            report.orphaned += 1;
            return;
        }
    };
    let mut data: PluginDataFile = match serde_json::from_slice(&bytes) {
        Ok(d) => d,
        Err(e) => {
            log::warn!(
                "[recover] parse failed (orphan kept): {} ({})",
                source_path.display(),
                e
            );
            report.orphaned += 1;
            return;
        }
    };
    if !verify_checksum(&data) {
        log::warn!(
            "[recover] checksum mismatch (orphan kept): {}",
            source_path.display()
        );
        report.orphaned += 1;
        return;
    }
    let failure_reasons = recovery_failure_reasons(&data);
    let publishable = failure_reasons.is_empty();
    let recovery_path = if publishable {
        final_path.to_path_buf()
    } else {
        failed_path_for_final(final_path)
    };
    // 既に `.json` 側がある場合 (= 直前 flush が成功してから DAW が落ちた稀ケース)
    // は source 側の方が新しい/古いの判定が困難なので、安全側で source を残置 +
    // orphaned 計上。GUI 通知不要 (起動時 R-28 機能的沈黙)。
    if recovery_path.exists() {
        log::warn!(
            "[recover] recovery target already exists (orphan kept): {}",
            source_path.display()
        );
        report.orphaned += 1;
        return;
    }
    if publishable {
        if data.status != Status::Closed {
            data.status = Status::Closed;
            data.integrity_degraded = true;
            if !data
                .integrity_reasons
                .iter()
                .any(|reason| reason == "startup_recovered_active")
            {
                data.integrity_reasons
                    .push("startup_recovered_active".to_string());
            }
        }
        data.commit_status = Some("committed".to_string());
        data.checksum = match compute_checksum(&data) {
            Ok(checksum) => checksum,
            Err(e) => {
                log::warn!(
                    "[recover] checksum recompute failed (orphan kept): {} ({})",
                    source_path.display(),
                    e
                );
                report.orphaned += 1;
                return;
            }
        };
        let json = match serde_json::to_string(&data) {
            Ok(json) => json,
            Err(e) => {
                log::warn!(
                    "[recover] serialize failed (orphan kept): {} ({})",
                    source_path.display(),
                    e
                );
                report.orphaned += 1;
                return;
            }
        };
        let recover_path = PathBuf::from(format!("{}.recover", source_path.to_string_lossy()));
        if let Err(e) = fs::write(&recover_path, json.as_bytes()) {
            log::warn!(
                "[recover] recovered write failed (orphan kept): {} ({})",
                recover_path.display(),
                e
            );
            report.orphaned += 1;
            return;
        }
        if let Err(e) = fs::rename(&recover_path, &recovery_path) {
            log::warn!(
                "[recover] recovered rename failed: {} → {} ({})",
                recover_path.display(),
                recovery_path.display(),
                e
            );
            let _ = fs::remove_file(&recover_path);
            report.orphaned += 1;
            return;
        }
        let _ = fs::remove_file(source_path);
        log::info!(
            "[recover] recovered: {} → {}",
            source_path.display(),
            recovery_path.display()
        );
        report.recovered += 1;
        return;
    }
    data.status = Status::Closed;
    data.commit_status = Some("failed".to_string());
    data.validity = false;
    data.integrity_degraded = true;
    for reason in failure_reasons {
        if !data
            .integrity_reasons
            .iter()
            .any(|existing| existing == reason)
        {
            data.integrity_reasons.push(reason.to_string());
        }
    }
    data.checksum = match compute_checksum(&data) {
        Ok(checksum) => checksum,
        Err(e) => {
            log::warn!(
                "[recover] failed checksum recompute failed (orphan kept): {} ({})",
                source_path.display(),
                e
            );
            report.orphaned += 1;
            return;
        }
    };
    let json = match serde_json::to_vec(&data) {
        Ok(json) => json,
        Err(e) => {
            log::warn!(
                "[recover] failed serialize failed (orphan kept): {} ({})",
                source_path.display(),
                e
            );
            report.orphaned += 1;
            return;
        }
    };
    if let Some(parent) = recovery_path.parent() {
        if let Err(e) = fs::create_dir_all(parent) {
            log::warn!(
                "[recover] failed dir create failed (orphan kept): {} ({})",
                parent.display(),
                e
            );
            report.orphaned += 1;
            return;
        }
    }
    let recover_path = PathBuf::from(format!("{}.recover", source_path.to_string_lossy()));
    if let Err(e) = fs::write(&recover_path, &json) {
        log::warn!(
            "[recover] failed write failed (orphan kept): {} ({})",
            recover_path.display(),
            e
        );
        report.orphaned += 1;
        return;
    }
    if let Err(e) = fs::rename(&recover_path, &recovery_path) {
        log::warn!(
            "[recover] rename failed: {} → {} ({})",
            recover_path.display(),
            recovery_path.display(),
            e
        );
        let _ = fs::remove_file(&recover_path);
        report.orphaned += 1;
        return;
    }
    let _ = fs::remove_file(source_path);
    log::info!(
        "[recover] recovered: {} → {}",
        source_path.display(),
        recovery_path.display()
    );
    report.recovered += 1;
}

fn recovery_candidate_is_stale(path: &Path) -> bool {
    let Ok(metadata) = fs::metadata(path) else {
        return false;
    };
    let Ok(mtime) = metadata.modified() else {
        return false;
    };
    SystemTime::now()
        .duration_since(mtime)
        .map(|elapsed| elapsed > Duration::from_secs(STALE_ACTIVE_SWEEP_SECS))
        .unwrap_or(false)
}

fn recovery_failure_reasons(data: &PluginDataFile) -> Vec<&'static str> {
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
    if data.expected_wav.as_ref().is_none_or(|m| !m.is_usable()) {
        reasons.push("missing_expected_wav_metadata");
    }
    if data
        .bounce_take
        .as_ref()
        .is_none_or(|take| take.alignment_status != "sample_count_ready")
    {
        reasons.push("bounce_take_not_sample_count_ready");
    }
    if data
        .trace_diagnostics
        .as_ref()
        .is_none_or(|diag| diag.missing_slots > 0)
    {
        reasons.push("missing_trace_slots");
    }
    for reason in &data.integrity_reasons {
        match reason.as_str() {
            "raw_trace_timeline_gap" => reasons.push("raw_trace_timeline_gap"),
            "record_clock_not_wav_bounded" => reasons.push("record_clock_not_wav_bounded"),
            "trace_span_only" => reasons.push("trace_span_only"),
            "estimated" => reasons.push("estimated"),
            "missing_trace_slots" => reasons.push("missing_trace_slots"),
            "drain_seal_timeout" => reasons.push("drain_seal_timeout"),
            _ => {}
        }
    }
    reasons.sort_unstable();
    reasons.dedup();
    reasons
}

/// `{path}.json.tmp` から末尾 `.tmp` を取り除いた `{path}.json` を返す。
fn strip_tmp_suffix(tmp_path: &Path) -> PathBuf {
    // `.json.tmp` 不変の前提 (caller が ends_with 検査済)。
    let s = tmp_path.to_string_lossy();
    let trimmed = s.strip_suffix(".tmp").unwrap_or(&s);
    PathBuf::from(trimmed.to_string())
}

fn strip_partial_suffix(staging_path: &Path) -> PathBuf {
    let s = staging_path.to_string_lossy();
    let trimmed = s.strip_suffix(".partial").unwrap_or(&s);
    PathBuf::from(trimmed.to_string())
}

fn failed_path_for_final(final_path: &Path) -> PathBuf {
    let Some(parent) = final_path.parent() else {
        return PathBuf::from(format!("{}.failed", final_path.to_string_lossy()));
    };
    let Some(file_name) = final_path.file_name() else {
        return parent.join(".failed").join("unknown.json");
    };
    parent.join(".failed").join(file_name)
}

// ── B-026 / Gap-9: Startup stale active sweep ───────────────────────────────

/// `sweep_stale_active_at_startup` の stale 判定閾値 (秒)。
/// `io_thread_post::PRE_LIVENESS_STALE_SECS` (per-tick mtime 判定 / G-50-33) と
/// 同値。crate 跨ぎ重複定義を避ける独立宣言だが、両者は同じ意味論
/// ("PRE/POST が 60s 以上 mtime を更新できなかった = crash 残骸") を共有する。
pub const STALE_ACTIVE_SWEEP_SECS: u64 = 60;

/// `sweep_stale_active_at_startup` の集計。GUI 通知不要 (起動時 R-28 機能的沈黙)。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct StaleSweepReport {
    /// `status=Active && mtime>60s` の `.json` を `status=Closed` に書換成功した件数。
    pub closed: usize,
    /// 判定対象だが metadata / read / parse / write / rename いずれかに失敗して
    /// 残置した件数 (R-28 機能的沈黙でログのみ)。
    pub skipped: usize,
}

/// B-026 / Gap-9: Hypha 起動時に plugin_data 配下の crash 残骸 PluginDataFile
/// (`status=Active && mtime > STALE_ACTIVE_SWEEP_SECS`) を `status=Closed` に
/// 書き換える。Lens 側が「進行中 Record」と誤認するのを startup 時点で構造的に
/// 解消する (Daisuke Gap-9 仕様)。
///
/// # 対象スコープ
/// `plugin_data_root` を再帰 walk し、以下を全て満たすファイルのみ対象:
/// 1. 親ディレクトリ名が `pre` または `post` (= `Role::dir_name()`)
/// 2. ファイル拡張子が `.json` (`.tmp` は除外 / Gap-8 `recover_orphan_tmps`
///    と非重複)
/// 3. mtime 経過 > `STALE_ACTIVE_SWEEP_SECS`
/// 4. parse 成功かつ `status == Status::Active`
///
/// # 動作
/// `status` を `Closed` に書換 → `compute_checksum` で HMAC-SHA256 再計算 →
/// `{path}.tmp` に書込 → atomic `rename` で `{path}` 上書き。
/// 既存の append / flush 経路と同じ atomic rename パターン。
///
/// # 呼出位置
/// PRE / POST 両 IO Thread の `thread::spawn` 直後 1 回のみ
/// (`recover_orphan_tmps` と同位相)。loop 内で繰り返さない。
///
/// # 触れない領域 (B-026 Pass 15)
/// - `record_signal` / `all_keep_signal` / `all_stop_signal`: 別スコープ
/// - `*.json.tmp`: `recover_orphan_tmps` (Gap-8) の対象
/// - per-tick PRE liveness 判定 (`PRE_LIVENESS_STALE_SECS`): loop 内専用
/// - `cleanup::exit_record_full`: per-Record cleanup 専用
/// - `PluginDataFile.heartbeat` フィールド: 読まず mtime のみ使用
///
/// # R-28 機能的沈黙
/// `read_dir` / `metadata` / `read` / `parse` / `write` / `rename` 失敗は
/// `report.skipped += 1` + warn ログのみ。UI エラーは出さない。
pub fn sweep_stale_active_at_startup(plugin_data_root: &Path) -> StaleSweepReport {
    let mut report = StaleSweepReport::default();
    if !plugin_data_root.is_dir() {
        return report;
    }
    walk_stale_sweep(plugin_data_root, &mut report);
    if report.closed > 0 || report.skipped > 0 {
        log::info!(
            "[Gap-9] startup stale sweep: closed={} skipped={} root={}",
            report.closed,
            report.skipped,
            plugin_data_root.display()
        );
    }
    report
}

/// `sweep_stale_active_at_startup` の実走査。`fs::read_dir` 失敗は当該 dir のみ skip。
fn walk_stale_sweep(dir: &Path, report: &mut StaleSweepReport) {
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let ftype = match entry.file_type() {
            Ok(t) => t,
            Err(_) => continue,
        };
        if ftype.is_dir() {
            walk_stale_sweep(&path, report);
            continue;
        }
        if !ftype.is_file() {
            continue;
        }
        // 拡張子 .json (`.tmp` は対象外 / Gap-8 と非重複)
        let Some(ext) = path.extension().and_then(|s| s.to_str()) else {
            continue;
        };
        if ext != "json" {
            continue;
        }
        // 親ディレクトリ名が "pre" または "post" (= `Role::dir_name()`)
        let Some(parent_name) = path
            .parent()
            .and_then(|p| p.file_name())
            .and_then(|s| s.to_str())
        else {
            continue;
        };
        if parent_name != "pre" && parent_name != "post" {
            continue;
        }
        try_close_one_stale(&path, report);
    }
}

/// 1 ファイルの sweep 試行。stale 判定 (mtime / status) → atomic rewrite。
///
/// fresh / Closed / mtime 取得不能 (将来時刻含む) は **skipped にも数えず** 静かに
/// スキップする (touch 不要の対象は report に現れないことが「正常」)。
/// metadata / read / parse / write / rename 失敗は warn ログ + `skipped += 1`。
fn try_close_one_stale(path: &Path, report: &mut StaleSweepReport) {
    let metadata = match fs::metadata(path) {
        Ok(m) => m,
        Err(e) => {
            log::warn!("[Gap-9] metadata failed (skip): {} ({})", path.display(), e);
            report.skipped += 1;
            return;
        }
    };
    let mtime = match metadata.modified() {
        Ok(t) => t,
        Err(e) => {
            log::warn!(
                "[Gap-9] mtime unavailable (skip): {} ({})",
                path.display(),
                e
            );
            report.skipped += 1;
            return;
        }
    };
    let elapsed = match SystemTime::now().duration_since(mtime) {
        Ok(d) => d,
        // mtime が future (時計巻戻し / NFS 時刻ズレ) → fresh とみなし対象外。
        Err(_) => return,
    };
    if elapsed <= Duration::from_secs(STALE_ACTIVE_SWEEP_SECS) {
        return;
    }
    let bytes = match fs::read(path) {
        Ok(b) => b,
        Err(e) => {
            log::warn!("[Gap-9] read failed (skip): {} ({})", path.display(), e);
            report.skipped += 1;
            return;
        }
    };
    let mut data: PluginDataFile = match serde_json::from_slice(&bytes) {
        Ok(d) => d,
        Err(e) => {
            log::warn!("[Gap-9] parse failed (skip): {} ({})", path.display(), e);
            report.skipped += 1;
            return;
        }
    };
    if data.status != Status::Active {
        // Closed は対象外 (二重 close を回避)。
        return;
    }
    data.status = Status::Closed;
    data.checksum = match compute_checksum(&data) {
        Ok(c) => c,
        Err(e) => {
            log::warn!(
                "[Gap-9] checksum recompute failed (skip): {} ({})",
                path.display(),
                e
            );
            report.skipped += 1;
            return;
        }
    };
    let json = match serde_json::to_vec(&data) {
        Ok(v) => v,
        Err(e) => {
            log::warn!(
                "[Gap-9] serialize failed (skip): {} ({})",
                path.display(),
                e
            );
            report.skipped += 1;
            return;
        }
    };
    let mut tmp_os = path.as_os_str().to_os_string();
    tmp_os.push(".tmp");
    let tmp = PathBuf::from(tmp_os);
    if let Err(e) = fs::write(&tmp, &json) {
        log::warn!(
            "[Gap-9] tmp write failed (skip): {} ({})",
            path.display(),
            e
        );
        report.skipped += 1;
        return;
    }
    if let Err(e) = fs::rename(&tmp, path) {
        log::warn!("[Gap-9] rename failed (skip): {} ({})", path.display(), e);
        report.skipped += 1;
        return;
    }
    log::info!("[Gap-9] closed stale Active: {}", path.display());
    report.closed += 1;
}

/// 別スレッドで Record 停止シグナルを受けたときに writer を即座に閉じる。
pub fn drain_on_shutdown(shutdown: &Arc<AtomicBool>, recording: &mut Option<RecordingCtx>) -> bool {
    use std::sync::atomic::Ordering;
    if shutdown.load(Ordering::Relaxed) {
        if let Some(ctx) = recording.take() {
            writer_close(ctx);
        }
        return true;
    }
    false
}

// ── Tests ─────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{PluginDataFile, PluginDataWriter, Role, WriterPaths};
    use crate::record::RecordStateMachine;
    use crate::{License, MeasureResult, PsbSummary};
    use std::fs;
    use std::sync::atomic::{AtomicU64, Ordering};

    const TEST_PH: &str = "ph";
    const TEST_IID: &str = "iid-test";

    fn isolated_base() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_record_writer_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn resolve_record_session_id_reads_post_signal() {
        let base = isolated_base();
        let signal = crate::record_signal::RecordSignal {
            status: crate::record_signal::SignalStatus::Pending,
            requested_by: "post-x".to_string(),
            target_pre_instance_id: "pre-x".to_string(),
            daw_session_id: "daw-session".to_string(),
            session_id: "record-session-x".to_string(),
            t: "2026-07-05T00:00:00Z".to_string(),
            started_at: "2026-07-05T00:00:00Z".to_string(),
            started_at_position_samples: Some(96_000),
            paired_pre_name: "Music".to_string(),
            release_reason: None,
            expected_wav: None,
        };
        crate::record_signal::write_signal(&base, TEST_PH, "post-x", &signal).unwrap();

        assert_eq!(
            resolve_record_session_id(&base, TEST_PH, "post-x").as_deref(),
            Some("record-session-x")
        );
    }

    #[test]
    fn refresh_pair_record_metadata_reads_expected_wav_from_signal() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 48_000);
        let expected = expected_wav_metadata(48_000, 48_000, "bounce-refresh");
        let signal = crate::record_signal::RecordSignal {
            status: crate::record_signal::SignalStatus::Acknowledged,
            requested_by: TEST_IID.to_string(),
            target_pre_instance_id: "pre-x".to_string(),
            daw_session_id: "daw-refresh".to_string(),
            session_id: "record-session-refresh".to_string(),
            t: "2026-07-05T00:00:01Z".to_string(),
            started_at: "2026-07-05T00:00:00Z".to_string(),
            started_at_position_samples: Some(0),
            paired_pre_name: "PRE".to_string(),
            release_reason: None,
            expected_wav: Some(expected.clone()),
        };
        crate::record_signal::write_signal(&base, TEST_PH, TEST_IID, &signal).unwrap();

        refresh_pair_record_metadata_from_base(&mut ctx, &base, TEST_IID);

        assert_eq!(
            ctx.writer.data().record_session_id.as_deref(),
            Some("record-session-refresh")
        );
        assert_eq!(ctx.writer.data().expected_wav.as_ref(), Some(&expected));
    }

    /// B-076: テスト用の overflow=0 カウンタ（欠落なし）。`&no_overflow()` で run_record_tick へ。
    fn no_overflow() -> Arc<std::sync::atomic::AtomicU64> {
        Arc::new(std::sync::atomic::AtomicU64::new(0))
    }

    fn make_ctx(base: &std::path::Path, role: Role, started_at_ms: i64) -> RecordingCtx {
        make_ctx_with_sample_rate(base, role, started_at_ms, 48_000)
    }

    fn make_ctx_with_sample_rate(
        base: &std::path::Path,
        role: Role,
        started_at_ms: i64,
        sample_rate: u32,
    ) -> RecordingCtx {
        let start_iso = epoch_ms_to_iso8601(started_at_ms);
        let paths = WriterPaths::build(base, TEST_PH, TEST_IID, role, &start_iso);
        let final_path = paths.final_path.clone();
        let staging_path = paths.staging_path.clone();
        let failed_path = paths.failed_path.clone();
        let mut writer = PluginDataWriter::create(
            paths,
            "test-installation-id".to_string(),
            TEST_PH.to_string(),
            TEST_IID.to_string(),
            role,
            None,
            sample_rate,
            (role == Role::Post).then(|| "paired-pre-test".to_string()),
            (role == Role::Pre).then(|| "paired-post-test".to_string()),
            None,
            None,
        )
        .unwrap();
        writer.set_record_start_wall_clock(start_iso);
        writer.set_started_at_ms(started_at_ms);
        writer.flush().unwrap();
        RecordingCtx {
            writer,
            final_path,
            staging_path,
            failed_path,
            started_at_ms,
            next_frame_ms: 0,
            next_psb_ms: 0,
            next_flush: Instant::now() + FLUSH_INTERVAL,
            first_frame_logged: false,
            consecutive_write_error: 0,
            overflow_start: 0,
            oversized_drop_start: 0, // B-125
            seal_at_start: 0,        // B-132
            record_generation: 0,
            clean_take: None,
            last_trace_t_ms: None,
            last_trace_frame_48k: None,
            last_trace_native_frames: None,
            trace_sample_count: 0,
            trace_samples: Vec::new(),
            raw_trace_timeline_covers_duration: None,
        }
    }

    fn read_output_for_final(final_path: &Path) -> PluginDataFile {
        let failed_path = failed_path_for_final(final_path);
        let pending_path = pair_pending_path_for_final(final_path);
        let path = if final_path.exists() {
            final_path.to_path_buf()
        } else if failed_path.exists() {
            failed_path
        } else if pending_path.exists() {
            pending_path
        } else {
            any_pair_pending_for_final(final_path).unwrap_or(pending_path)
        };
        let bytes = fs::read(&path).unwrap_or_else(|e| {
            panic!(
                "expected output at final/failed/pair_pending for {}, err={}",
                final_path.display(),
                e
            )
        });
        serde_json::from_slice(&bytes).unwrap()
    }

    fn pair_pending_path_for_final(final_path: &Path) -> PathBuf {
        let parent = final_path.parent().expect("role dir");
        let file_name = final_path.file_name().expect("file name");
        parent.join(".pair_pending").join(file_name)
    }

    fn any_pair_pending_for_final(final_path: &Path) -> Option<PathBuf> {
        let dir = final_path.parent()?.join(".pair_pending");
        fs::read_dir(dir)
            .ok()?
            .flatten()
            .map(|entry| entry.path())
            .find(|path| path.extension().is_some_and(|ext| ext == "json"))
    }

    fn full_measure_result() -> MeasureResult {
        MeasureResult {
            lufs_m: Some(-14.2),
            true_peak: Some(-1.1),
            tp_session_max: Some(-1.0), // B-074: session max ≥ recent
            crest: Some(12.3),
            psr: Some(8.0),
            n_prime_total: Some(5.0),
            sharpness: Some(1.2),
            psb_summary: Some(PsbSummary {
                low: -10.0,
                mid: -12.0,
                high: -14.0,
            }),
            n_prime: Some([0.5; 20]),
            psb_bark: Some([0.05; 20]),
            ..MeasureResult::default()
        }
    }

    fn measured_silence_result() -> MeasureResult {
        MeasureResult {
            lufs_m: Some(TRACE_SILENCE_LUFS),
            true_peak: Some(TRACE_SILENCE_TRUE_PEAK_DBTP),
            crest: Some(TRACE_SILENCE_CREST_DB),
            ..MeasureResult::default()
        }
    }

    fn expected_wav_metadata(
        sample_rate: u32,
        duration_samples: u64,
        bounce_id: &str,
    ) -> ExpectedWavMetadata {
        let now_ms = now_epoch_ms();
        ExpectedWavMetadata {
            expected_duration_samples: duration_samples,
            expected_sample_rate: sample_rate,
            wav_path: format!("/tmp/{bounce_id}.wav"),
            bounce_id: bounce_id.to_string(),
            created_at_ms: now_ms,
            wav_file_size: Some(duration_samples.saturating_mul(4).saturating_add(44)),
            wav_mtime_ms: now_ms,
            wav_hash: Some(format!("hash-{bounce_id}")),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    fn missing_trace_marker(t_ms: u64) -> RecordTraceSample {
        RecordTraceSample::missing_marker(
            1,
            t_ms,
            t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
            None,
        )
    }

    fn trace_sample(t_ms: u64, result: MeasureResult, include_psb: bool) -> RecordTraceSample {
        trace_sample_frames(
            t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
            result,
            include_psb,
        )
    }

    fn trace_sample_frames(
        t_frames_48k: u64,
        result: MeasureResult,
        include_psb: bool,
    ) -> RecordTraceSample {
        trace_sample_frames_with_native(t_frames_48k, None, result, include_psb)
    }

    fn trace_sample_frames_with_native(
        t_frames_48k: u64,
        t_native_frames: Option<u64>,
        result: MeasureResult,
        include_psb: bool,
    ) -> RecordTraceSample {
        RecordTraceSample {
            kind: RecordTraceKind::Measured,
            generation: 1,
            t_ms: t_frames_48k.saturating_mul(1_000) / TRACE_TIMEBASE_HZ,
            t_frames_48k,
            t_native_frames,
            result,
            include_psb,
        }
    }

    fn trace_sample_for_generation(
        generation: u64,
        t_ms: u64,
        result: MeasureResult,
        include_psb: bool,
    ) -> RecordTraceSample {
        let mut sample = trace_sample(t_ms, result, include_psb);
        sample.generation = generation;
        sample
    }

    #[test]
    fn parse_iso8601_to_epoch_ms_basic() {
        let ms_a = parse_iso8601_to_epoch_ms("2026-04-19T12:00:00Z").unwrap();
        let ms_b = parse_iso8601_to_epoch_ms("2026-04-19T12:00:01Z").unwrap();
        assert_eq!(ms_b - ms_a, 1000);
    }

    #[test]
    fn epoch_ms_to_iso8601_basic() {
        let ms = parse_iso8601_to_epoch_ms("2026-04-19T12:00:00Z").unwrap();
        assert_eq!(epoch_ms_to_iso8601(ms), "2026-04-19T12:00:00.000Z");
    }

    #[test]
    fn parse_iso8601_to_epoch_ms_empty_returns_none() {
        assert!(parse_iso8601_to_epoch_ms("").is_none());
    }

    #[test]
    fn parse_iso8601_to_epoch_ms_invalid_returns_none() {
        assert!(parse_iso8601_to_epoch_ms("not-iso-8601").is_none());
    }

    #[test]
    fn resolve_started_at_ms_reads_signal_file() {
        let base = isolated_base();
        record_signal::write_pending(&base, TEST_PH, "post-1", "pre-1".into(), "daw-1".into())
            .unwrap();
        let ms = resolve_started_at_ms(&base, TEST_PH, "post-1");
        let now = now_epoch_ms();
        assert!((now - ms).abs() < 5_000, "ms={}, now={}", ms, now);
    }

    #[test]
    fn resolve_started_at_ms_missing_falls_back_to_now() {
        let base = isolated_base();
        let ms = resolve_started_at_ms(&base, TEST_PH, "post-x");
        let now = now_epoch_ms();
        assert!((now - ms).abs() < 1_000);
    }

    #[test]
    fn append_frame_skips_when_crest_missing() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let mut m = full_measure_result();
        m.crest = None;
        assert!(!writer_append_frame(&mut ctx, 100, &m));
        assert_eq!(ctx.data().frames.len(), 0);
    }

    #[test]
    fn append_frame_writes_core_metrics_when_phase_d_missing() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let mut m = full_measure_result();
        m.n_prime = None;
        m.sharpness = None;
        assert!(writer_append_frame(&mut ctx, 100, &m));
        assert_eq!(ctx.data().frames.len(), 1);
        assert!(ctx.data().frames[0].n_prime.is_none());
        assert!(ctx.data().frames[0].sharpness.is_none());
        assert_eq!(ctx.data().frames[0].lufs_m, -14.2);
        assert!(ctx.first_frame_logged);
    }

    #[test]
    fn append_frame_writes_when_all_fields_present() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let m = full_measure_result();
        assert!(writer_append_frame(&mut ctx, 200, &m));
        let frames = &ctx.data().frames;
        assert_eq!(frames.len(), 1);
        assert_eq!(frames[0].t_ms, 200);
        assert_eq!(frames[0].n_prime.unwrap()[0], 0.5);
        assert_eq!(frames[0].lufs_m, -14.2);
        assert!(ctx.first_frame_logged);
    }

    #[test]
    fn measured_observed_silence_core_preserves_phase_fields() {
        let mut m = MeasureResult {
            n_prime: Some([0.25; 20]),
            sharpness: Some(1.25),
            psb_bark: Some([0.125; 20]),
            psr: Some(6.0),
            ..MeasureResult::default()
        };
        m.tp_session_max = Some(-80.0);

        let sample = RecordTraceSample::measured_observed(
            1,
            100,
            TRACE_TIMEBASE_HZ / 10,
            Some(9_600),
            m,
            true,
            true,
        );

        assert_eq!(sample.result.lufs_m, Some(TRACE_SILENCE_LUFS));
        assert_eq!(sample.result.true_peak, Some(TRACE_SILENCE_TRUE_PEAK_DBTP));
        assert_eq!(sample.result.crest, Some(TRACE_SILENCE_CREST_DB));
        assert_eq!(sample.result.n_prime.unwrap()[0], 0.25);
        assert_eq!(sample.result.sharpness, Some(1.25));
        assert_eq!(sample.result.psb_bark.unwrap()[0], 0.125);
        assert_eq!(sample.result.psr, Some(6.0));
        assert_eq!(sample.result.tp_session_max, Some(-80.0));
        assert!(sample.include_psb);
    }

    #[test]
    fn append_psb_skips_when_missing() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let mut m = full_measure_result();
        m.psb_bark = None;
        assert!(!writer_append_psb(&mut ctx, 500, &m));
        assert_eq!(ctx.data().psb_snapshots.as_ref().map(|v| v.len()), Some(0));
    }

    #[test]
    fn append_psb_writes_when_present() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let m = full_measure_result();
        assert!(writer_append_psb(&mut ctx, 500, &m));
        let snaps = ctx.data().psb_snapshots.as_ref().unwrap();
        assert_eq!(snaps.len(), 1);
        assert_eq!(snaps[0].t_ms, 500);
        assert!(snaps[0].interpolatable);
    }

    #[test]
    fn writer_close_writes_status_closed_and_verifies_checksum() {
        let base = isolated_base();
        let started_at_ms = 1_781_234_000_000;
        let mut ctx = make_ctx(&base, Role::Post, started_at_ms);
        let m = full_measure_result();
        assert!(writer_append_frame(&mut ctx, 100, &m));
        let final_path = ctx.final_path.clone();
        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.status, crate::plugin_data::Status::Closed);
        assert_eq!(loaded.started_at_ms, started_at_ms);
        assert_eq!(loaded.frames.len(), 1);
        assert!(loaded.bounce_marker.duration_samples > 0);
        assert_ne!(
            loaded.bounce_marker.wall_clock_start,
            loaded.bounce_marker.wall_clock_end
        );
        assert!(crate::plugin_data::verify_checksum(&loaded));
    }

    #[test]
    fn writer_close_prefers_clean_take_and_clips_trace_to_wav_span_96k() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let m = full_measure_result();

        mark_trace_time(&mut ctx, 15_800, 758_400, Some(1_516_800));
        for t_ms in (0_u64..=15_000).step_by(FRAME_INTERVAL_MS as usize) {
            assert!(writer_append_frame(&mut ctx, t_ms, &m));
        }
        for t_ms in [15_100, 15_800] {
            assert!(writer_append_frame(&mut ctx, t_ms, &m));
        }
        assert!(writer_append_psb(&mut ctx, 15_100, &m));
        ctx.trace_sample_count = 159;
        ctx.record_generation = 42;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 42,
            duration_samples: 1_440_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        let final_path = ctx.final_path.clone();

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(loaded.bounce_marker.duration_samples, 1_440_000);
        assert_eq!(take.source, "wav_clock_native");
        assert_eq!(take.time_axis, "native_samples");
        assert_eq!(take.alignment_status, "sample_count_ready");
        assert_eq!(take.duration_samples, 1_440_000);
        assert_eq!(take.duration_frames_48k, 720_000);
        assert_eq!(take.end_t_ms, 15_000);
        assert_eq!(take.frame_count, 150);
        assert!(loaded.frames.iter().all(|frame| frame.t_ms <= 15_000));
        assert!(loaded
            .psb_snapshots
            .as_ref()
            .is_some_and(|psb| psb.is_empty()));
        assert!(crate::plugin_data::verify_checksum(&loaded));
    }

    #[test]
    fn render_clock_clean_take_is_not_sample_count_ready_without_wav_clock() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let m = full_measure_result();

        mark_trace_time(&mut ctx, 15_000, 720_000, Some(1_440_000));
        for t_ms in (0_u64..=15_000).step_by(FRAME_INTERVAL_MS as usize) {
            assert!(writer_append_frame(&mut ctx, t_ms, &m));
        }
        ctx.trace_sample_count = 151;
        ctx.record_generation = 142;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 142,
            duration_samples: 1_440_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_RENDER_CLOCK,
        });
        let final_path = ctx.final_path.clone();

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.source, "render_clock_native");
        assert_eq!(take.duration_samples, 1_440_000);
        assert_eq!(take.frame_count, 150);
        assert_eq!(take.alignment_status, "trace_span_only");
        assert!(loaded.integrity_degraded);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "record_clock_not_wav_bounded"));
    }

    #[test]
    fn clean_take_close_marks_trace_timeline_gaps_missing() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 48_000);
        let m = full_measure_result();

        mark_trace_time(&mut ctx, 20_000, 960_000, Some(960_000));
        assert!(writer_append_frame(&mut ctx, 100, &m));
        for t_ms in (10_100..=20_000).step_by(FRAME_INTERVAL_MS as usize) {
            assert!(writer_append_frame(&mut ctx, t_ms, &m));
        }
        ctx.trace_sample_count = 201;
        ctx.record_generation = 12;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 12,
            duration_samples: 480_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        let final_path = ctx.final_path.clone();

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.end_t_ms, 10_000);
        assert_eq!(take.frame_count, 1);
        assert_eq!(loaded.frames.len(), 1);
        assert_eq!(loaded.frames.first().map(|frame| frame.t_ms), Some(100));
        assert!(loaded.integrity_degraded);
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.expected_frame_count, 100);
        assert_eq!(diag.measured_frame_count, 1);
        assert_eq!(diag.missing_slots, 99);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_trace_slots"));
    }

    #[test]
    fn short_clean_take_fragment_is_not_sample_count_ready() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let m = full_measure_result();

        mark_trace_time(&mut ctx, 0, 0, Some(0));
        assert!(writer_append_frame(&mut ctx, 0, &m));
        ctx.trace_sample_count = 1;
        ctx.record_generation = 77;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 77,
            duration_samples: 2_048,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        let failed_path = ctx.failed_path.clone();

        writer_close(ctx);

        let loaded: PluginDataFile =
            serde_json::from_slice(&fs::read(&failed_path).unwrap()).unwrap();
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_ne!(take.alignment_status, "sample_count_ready");
        assert!(loaded.integrity_degraded);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "record_too_short"));
    }

    #[test]
    fn short_trace_only_fragment_is_quarantined() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let m = full_measure_result();

        mark_trace_time(&mut ctx, 100, 4_800, Some(9_600));
        assert!(writer_append_frame(&mut ctx, 100, &m));
        ctx.trace_sample_count = 1;
        let failed_path = ctx.failed_path.clone();

        writer_close(ctx);

        let loaded: PluginDataFile =
            serde_json::from_slice(&fs::read(&failed_path).unwrap()).unwrap();
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.alignment_status, "trace_span_only");
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "record_too_short"));
    }

    #[test]
    fn writer_close_keeps_native_sample_count_at_44k1() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 44_100);
        let m = full_measure_result();

        mark_trace_time(&mut ctx, 15_900, 763_200, Some(701_190));
        for t_ms in (0_u64..=15_000).step_by(FRAME_INTERVAL_MS as usize) {
            assert!(writer_append_frame(&mut ctx, t_ms, &m));
        }
        assert!(writer_append_frame(&mut ctx, 15_100, &m));
        ctx.trace_sample_count = 151;
        ctx.record_generation = 7;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 7,
            duration_samples: 661_500,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        let final_path = ctx.final_path.clone();

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(loaded.sample_rate, 44_100);
        assert_eq!(loaded.bounce_marker.duration_samples, 661_500);
        assert_eq!(take.source, "wav_clock_native");
        assert_eq!(take.time_axis, "native_samples");
        assert_eq!(take.alignment_status, "sample_count_ready");
        assert_eq!(take.duration_samples, 661_500);
        assert_eq!(take.duration_frames_48k, 720_000);
        assert_eq!(take.end_t_ms, 15_000);
        assert_eq!(take.frame_count, 150);
        assert!(loaded.frames.iter().all(|frame| frame.t_ms <= 15_000));
        assert!(crate::plugin_data::verify_checksum(&loaded));
    }

    // ── B-134 (G-115-391): degraded close is diagnostic-only ──
    #[test]
    fn b134_degraded_close_goes_to_failed_when_writable() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let m = full_measure_result();
        assert!(writer_append_frame(&mut ctx, 100, &m));
        let final_path = ctx.final_path.clone();
        let failed_path = ctx.failed_path.clone();
        writer_close_degraded(ctx);

        assert!(
            !final_path.exists(),
            "degraded close must not publish normal JSON"
        );
        assert!(
            failed_path.exists(),
            "degraded close must create a failed diagnostic JSON"
        );
        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(
            loaded.status,
            crate::plugin_data::Status::Closed,
            "degraded close でも status=Closed"
        );
        assert!(
            loaded.integrity_degraded,
            "B-134: storage 書込可 → integrity_degraded file flag が立つ"
        );
        assert!(crate::plugin_data::verify_checksum(&loaded));
        eprintln!(
            "[b134] writable degraded close → status={:?} integrity_degraded={} (failed diagnostic)",
            loaded.status, loaded.integrity_degraded
        );
    }

    #[test]
    fn b134_degraded_close_recreates_storage_and_writes_failed() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let m = full_measure_result();
        assert!(writer_append_frame(&mut ctx, 100, &m));
        let final_path = ctx.final_path.clone();
        let failed_path = ctx.failed_path.clone();
        fs::remove_dir_all(&base).unwrap();
        writer_close_degraded(ctx);

        assert!(
            !final_path.exists(),
            "degraded close must not recreate a normal JSON"
        );
        assert!(
            failed_path.exists(),
            "degraded close must recreate storage for failed diagnostic JSON"
        );
        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.status, Status::Closed);
        assert!(
            loaded.integrity_degraded,
            "B-245: recreated storage must persist degraded=true"
        );
        assert!(crate::plugin_data::verify_checksum(&loaded));
    }

    #[test]
    fn run_record_tick_watch_keeps_none() {
        let sm = Arc::new(RecordStateMachine::new());
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = None;
        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(), // B-125: oversized_drop（テストは欠落なし=0）
            None,
        )
        .unwrap();
        assert!(rec.is_none());
    }

    #[test]
    fn trace_queue_drain_keeps_future_generation_samples() {
        let queue = new_record_trace_queue();
        push_record_trace_sample(
            &queue,
            trace_sample_for_generation(30, 50, full_measure_result(), false),
        );
        push_record_trace_sample(
            &queue,
            trace_sample_for_generation(31, 100, full_measure_result(), false),
        );
        push_record_trace_sample(
            &queue,
            trace_sample_for_generation(32, 200, full_measure_result(), false),
        );

        let drained = drain_record_trace_queue_for_generation(&queue, 31);
        assert_eq!(drained.len(), 1);
        assert_eq!(drained[0].generation, 31);

        let remaining = drain_record_trace_queue(&queue);
        assert_eq!(remaining.len(), 1);
        assert_eq!(remaining[0].generation, 32);
    }

    #[test]
    fn post_record_start_does_not_wait_for_paired_pre_target() {
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = None;
        let started_at = now_epoch_ms();

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            || started_at,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            None,
        )
        .unwrap();

        let ctx = rec
            .as_ref()
            .expect("POST must start Record even before PRE target is latched");
        assert_eq!(ctx.data().started_at_ms, started_at);
        assert!(ctx.data().paired_pre_instance_id.is_none());
    }

    #[test]
    fn pre_record_start_does_not_wait_for_paired_post_partner() {
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = None;
        let started_at = now_epoch_ms();

        run_record_tick(
            &sm,
            Role::Pre,
            48000,
            TEST_PH,
            TEST_IID,
            || started_at,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            None,
        )
        .unwrap();

        let ctx = rec
            .as_ref()
            .expect("PRE must start Record even before POST is acknowledged");
        assert_eq!(ctx.data().started_at_ms, started_at);
        assert!(ctx.data().paired_post_instance_id.is_none());
    }

    #[test]
    fn run_record_tick_record_to_watch_closes_writer() {
        let base = isolated_base();
        let ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let final_path = ctx.final_path.clone();
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        sm.exit_record();
        // B-132: Measure Thread の Record→Watch tight-drain 完了を模擬（seal 前進）。
        // これで IO 側 close arm の bounded seal-wait が即 true を返す（実機ハンドシェイク再現）。
        sm.bump_seal();
        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(), // B-125: oversized_drop（テストは欠落なし=0）
            None,
        )
        .unwrap();

        assert!(rec.is_none());
        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.status, crate::plugin_data::Status::Closed);
    }

    #[test]
    fn run_record_tick_generation_change_closes_old_and_starts_new_without_queue_mix() {
        let base = isolated_base();
        let started = now_epoch_ms();
        let mut old_ctx = make_ctx(&base, Role::Post, started);
        let old_final_path = old_ctx.final_path.clone();
        let old_failed_path = old_ctx.failed_path.clone();
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let old_generation = sm.generation();
        old_ctx.record_generation = old_generation;
        old_ctx.seal_at_start = sm.seal();

        sm.exit_record();
        sm.bump_seal();
        sm.try_enter_record(License::Os).unwrap();
        let new_generation = sm.generation();

        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(old_ctx);
        let queue = new_record_trace_queue();
        push_record_trace_sample(
            &queue,
            trace_sample_for_generation(old_generation, 100, full_measure_result(), false),
        );
        push_record_trace_sample(
            &queue,
            trace_sample_for_generation(new_generation, 200, full_measure_result(), false),
        );

        run_record_tick_with_pair_names(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            || started + 1_000,
            || None,
            || None,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            Some(&queue),
            None,
        )
        .unwrap();

        let new_ctx = rec
            .as_ref()
            .expect("new generation should start a new writer");
        assert_eq!(new_ctx.record_generation, new_generation);
        let remaining = drain_record_trace_queue(&queue);
        assert_eq!(remaining.len(), 1);
        assert_eq!(remaining[0].generation, new_generation);

        let published = if old_final_path.exists() {
            old_final_path
        } else {
            old_failed_path
        };
        let loaded: PluginDataFile = serde_json::from_slice(&fs::read(published).unwrap()).unwrap();
        assert_eq!(loaded.frames.len(), 1);
        assert_eq!(loaded.frames[0].t_ms, 100);
    }

    #[test]
    fn run_record_tick_record_active_appends_frame_and_psb() {
        let base = isolated_base();
        let started = now_epoch_ms() - 600;
        let ctx = make_ctx(&base, Role::Post, started);
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(), // B-125: oversized_drop（テストは欠落なし=0）
            None,
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 1);
        assert_eq!(
            ctx_ref.data().psb_snapshots.as_ref().map(|v| v.len()),
            Some(1)
        );
        assert!(ctx_ref.next_frame_ms >= 600);
        assert!(ctx_ref.next_psb_ms >= 1000);
    }

    #[test]
    fn run_record_tick_drains_audio_time_trace_queue_for_offline_bounce() {
        let base = isolated_base();
        let ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(MeasureResult::default()));
        let mut rec: Option<RecordingCtx> = Some(ctx);
        let queue = new_record_trace_queue();
        push_record_trace_sample(&queue, trace_sample(100, full_measure_result(), true));
        push_record_trace_sample(&queue, trace_sample(200, full_measure_result(), false));

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            Some(&queue),
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 2);
        assert_eq!(ctx_ref.data().frames[0].t_ms, 100);
        assert_eq!(ctx_ref.data().frames[1].t_ms, 200);
        assert_eq!(
            ctx_ref.data().psb_snapshots.as_ref().map(|v| v.len()),
            Some(1)
        );
        assert!(drain_record_trace_queue(&queue).is_empty());
    }

    #[test]
    fn sparse_trace_sample_density_marks_integrity_degraded() {
        let base = isolated_base();
        let ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let final_path = ctx.final_path.clone();
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(MeasureResult::default()));
        let mut rec: Option<RecordingCtx> = Some(ctx);
        let queue = new_record_trace_queue();
        for t_ms in [100, 10_000] {
            push_record_trace_sample(&queue, trace_sample(t_ms, full_measure_result(), false));
        }

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            Some(&queue),
        )
        .unwrap();

        writer_close(rec.take().unwrap());
        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert!(
            loaded.integrity_degraded,
            "audio-time progressed but TRACE sample density was too sparse"
        );
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "raw_trace_timeline_gap"));
    }

    #[test]
    fn zero_trace_samples_and_zero_frames_over_min_duration_marks_integrity_degraded() {
        let base = isolated_base();
        let started = now_epoch_ms() - (TRACE_DENSITY_MIN_DURATION_MS as i64 + 1_000);
        let ctx = make_ctx(&base, Role::Pre, started);
        let failed_path = ctx.failed_path.clone();

        writer_close(ctx);

        let loaded: PluginDataFile =
            serde_json::from_slice(&fs::read(&failed_path).unwrap()).unwrap();
        assert_eq!(loaded.frames.len(), 0);
        assert!(
            loaded.integrity_degraded,
            "a long Record with no TRACE samples and no numeric frames must not look clean"
        );
    }

    #[test]
    fn zero_trace_samples_with_single_fallback_frame_over_min_duration_marks_integrity_degraded() {
        let base = isolated_base();
        let started = now_epoch_ms() - (TRACE_DENSITY_MIN_DURATION_MS as i64 + 1_000);
        let mut ctx = make_ctx(&base, Role::Pre, started);
        let failed_path = ctx.failed_path.clone();
        assert!(writer_append_frame(
            &mut ctx,
            TRACE_DENSITY_MIN_DURATION_MS + 500,
            &full_measure_result()
        ));

        writer_close(ctx);

        let loaded: PluginDataFile =
            serde_json::from_slice(&fs::read(&failed_path).unwrap()).unwrap();
        assert_eq!(loaded.frames.len(), 1);
        assert!(
            loaded.integrity_degraded,
            "a long Record with no audio-time TRACE samples must not look clean"
        );
    }

    #[test]
    fn zero_trace_samples_with_dense_fallback_frames_over_min_duration_is_degraded() {
        let base = isolated_base();
        let started = now_epoch_ms() - (TRACE_DENSITY_MIN_DURATION_MS as i64 + 1_000);
        let mut ctx = make_ctx(&base, Role::Pre, started);
        let final_path = ctx.final_path.clone();
        for t_ms in (0..=TRACE_DENSITY_MIN_DURATION_MS + 1_000).step_by(FRAME_INTERVAL_MS as usize)
        {
            assert!(writer_append_frame(&mut ctx, t_ms, &full_measure_result()));
        }

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert!(loaded.frames.len() > expected_trace_samples(TRACE_DENSITY_MIN_DURATION_MS) / 4);
        assert!(
            loaded.integrity_degraded,
            "dense fallback frames without audio-time TRACE must not be clean Record data"
        );
    }

    #[test]
    fn zero_trace_samples_under_min_duration_marks_integrity_degraded() {
        let base = isolated_base();
        let started = now_epoch_ms() - (TRACE_DENSITY_MIN_DURATION_MS as i64 - 1_000);
        let ctx = make_ctx(&base, Role::Pre, started);
        let failed_path = ctx.failed_path.clone();

        writer_close(ctx);

        let loaded: PluginDataFile =
            serde_json::from_slice(&fs::read(&failed_path).unwrap()).unwrap();
        assert!(
            loaded.integrity_degraded,
            "a Record with zero usable TRACE frames must not look clean even when short"
        );
    }

    #[test]
    fn one_frame_with_later_trace_time_under_min_duration_marks_frame_gap_degraded() {
        let base = isolated_base();
        let started = now_epoch_ms() - (TRACE_DENSITY_MIN_DURATION_MS as i64 - 1_000);
        let mut ctx = make_ctx(&base, Role::Pre, started);
        let failed_path = ctx.failed_path.clone();
        assert!(writer_append_frame(&mut ctx, 100, &full_measure_result()));
        mark_trace_time(
            &mut ctx,
            TRACE_DENSITY_MIN_DURATION_MS - 1_000,
            (TRACE_DENSITY_MIN_DURATION_MS - 1_000).saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
            None,
        );

        writer_close(ctx);

        let loaded: PluginDataFile =
            serde_json::from_slice(&fs::read(&failed_path).unwrap()).unwrap();
        assert_eq!(loaded.frames.len(), 1);
        assert!(
            loaded.integrity_degraded,
            "a Record with a usable point but a missing timeline must not look clean"
        );
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "frame_timeline_gap"));
    }

    #[test]
    fn dense_trace_samples_write_silent_floor_frames_for_numeric_gaps() {
        let base = isolated_base();
        let ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let final_path = ctx.final_path.clone();
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(MeasureResult::default()));
        let mut rec: Option<RecordingCtx> = Some(ctx);
        let queue = new_record_trace_queue();
        for i in 0..=100 {
            push_record_trace_sample(
                &queue,
                trace_sample(
                    i * FRAME_INTERVAL_MS,
                    if i == 1 {
                        full_measure_result()
                    } else {
                        measured_silence_result()
                    },
                    false,
                ),
            );
        }

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            Some(&queue),
        )
        .unwrap();

        writer_close(rec.take().unwrap());
        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(
            loaded.frames.len(),
            100,
            "every measured 100ms TRACE window should become a TRACE frame, including silence"
        );
        assert_eq!(loaded.frames[0].t_ms, 100);
        assert_eq!(loaded.frames[0].lufs_m, -14.2);
        assert_eq!(loaded.frames[1].lufs_m, TRACE_SILENCE_LUFS);
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.expected_frame_count, 100);
        assert_eq!(diag.measured_frame_count, 100);
        assert_eq!(diag.missing_slots, 0);
        assert!(
            loaded.integrity_degraded,
            "TRACE is complete, but missing Pair/expected metadata keeps it diagnostic-only"
        );
    }

    #[test]
    fn jittered_trace_samples_snap_to_nearest_100ms_slot() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 48_000);
        let final_path = ctx.final_path.clone();
        let queue = new_record_trace_queue();
        for slot_ms in (0_u64..=1_000).step_by(FRAME_INTERVAL_MS as usize) {
            let t_ms = if slot_ms == 0 || slot_ms == 1_000 {
                slot_ms
            } else {
                slot_ms + 1
            };
            push_record_trace_sample(
                &queue,
                trace_sample_frames_with_native(
                    t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                    Some(t_ms.saturating_mul(48_000) / 1_000),
                    full_measure_result(),
                    false,
                ),
            );
        }

        let drained = drain_trace_queue_into_writer(&mut ctx, &queue);
        assert_eq!(drained.samples, 11);
        ctx.record_generation = 33;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 33,
            duration_samples: 48_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.expected_frame_count, 10);
        assert_eq!(diag.measured_frame_count, 10);
        assert_eq!(diag.missing_slots, 0);
        assert_eq!(loaded.frames.first().map(|frame| frame.t_ms), Some(100));
        assert_eq!(loaded.frames.last().map(|frame| frame.t_ms), Some(1_000));
    }

    #[test]
    fn ninety_six_khz_fifteen_second_record_has_wav_duration_and_continuous_silent_trace() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let final_path = ctx.final_path.clone();
        let queue = new_record_trace_queue();
        for t_ms in (0_u64..=15_000).step_by(FRAME_INTERVAL_MS as usize) {
            let result = if (5_800..=12_300).contains(&t_ms) {
                measured_silence_result()
            } else {
                full_measure_result()
            };
            push_record_trace_sample(
                &queue,
                trace_sample_frames_with_native(
                    t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                    Some(t_ms.saturating_mul(96_000) / 1_000),
                    result,
                    false,
                ),
            );
        }

        let drained = drain_trace_queue_into_writer(&mut ctx, &queue);
        assert_eq!(drained.samples, 151);
        assert_eq!(drained.frames, 151);
        ctx.record_generation = 91;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 91,
            duration_samples: 1_440_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(loaded.bounce_marker.duration_samples, 1_440_000);
        assert_eq!(take.duration_samples, 1_440_000);
        assert_eq!(take.end_t_ms, 15_000);
        assert_eq!(take.alignment_status, "sample_count_ready");
        assert_eq!(loaded.frames.len(), 150);
        assert_eq!(loaded.frames.first().map(|frame| frame.t_ms), Some(100));
        assert_eq!(loaded.frames.last().map(|frame| frame.t_ms), Some(15_000));
        assert!(loaded
            .frames
            .windows(2)
            .all(|pair| pair[1].t_ms.saturating_sub(pair[0].t_ms) == FRAME_INTERVAL_MS));
        assert!(loaded
            .frames
            .iter()
            .any(|frame| frame.lufs_m == TRACE_SILENCE_LUFS));
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.expected_frame_count, 150);
        assert_eq!(diag.measured_frame_count, 150);
        assert_eq!(diag.missing_slots, 0);
        assert!(
            loaded.integrity_degraded,
            "TRACE is complete, but missing Pair/expected metadata keeps it diagnostic-only"
        );
    }

    #[test]
    fn high_density_all_silent_trace_backfills_missing_grid_slots() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let final_path = ctx.final_path.clone();
        let queue = new_record_trace_queue();
        for t_ms in (100_u64..=12_100).step_by(FRAME_INTERVAL_MS as usize) {
            push_record_trace_sample(
                &queue,
                trace_sample_frames_with_native(
                    t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                    Some(t_ms.saturating_mul(96_000) / 1_000),
                    measured_silence_result(),
                    false,
                ),
            );
        }

        let drained = drain_trace_queue_into_writer(&mut ctx, &queue);
        assert_eq!(drained.samples, 121);
        assert_eq!(drained.frames, 121);
        ctx.record_generation = 92;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 92,
            duration_samples: 1_460_224,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.end_t_ms, 15_210);
        assert_eq!(take.alignment_status, "sample_count_ready");
        assert_eq!(take.frame_count, 152);
        assert_eq!(loaded.frames.len(), 152);
        assert_eq!(loaded.frames.first().map(|frame| frame.t_ms), Some(100));
        assert_eq!(loaded.frames.last().map(|frame| frame.t_ms), Some(15_200));
        assert!(loaded.frames.iter().all(|frame| {
            frame.lufs_m == TRACE_SILENCE_LUFS
                && frame.true_peak == TRACE_SILENCE_TRUE_PEAK_DBTP
                && frame.crest == TRACE_SILENCE_CREST_DB
        }));
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.raw_trace_count, 121);
        assert_eq!(diag.expected_frame_count, 152);
        assert_eq!(diag.measured_frame_count, 152);
        assert_eq!(diag.missing_slots, 0);
        assert_eq!(diag.explicit_silence_frame_count, 152);
        assert!(!loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_trace_slots"));
        assert!(!loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "raw_trace_timeline_gap"));
    }

    #[test]
    fn high_density_all_silent_trace_with_interior_gap_stays_failed() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let final_path = ctx.final_path.clone();
        let queue = new_record_trace_queue();
        let observed_slots = (100_u64..=6_000)
            .step_by(FRAME_INTERVAL_MS as usize)
            .chain((9_200_u64..=15_200).step_by(FRAME_INTERVAL_MS as usize));
        for t_ms in observed_slots {
            push_record_trace_sample(
                &queue,
                trace_sample_frames_with_native(
                    t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                    Some(t_ms.saturating_mul(96_000) / 1_000),
                    measured_silence_result(),
                    false,
                ),
            );
        }

        let drained = drain_trace_queue_into_writer(&mut ctx, &queue);
        assert_eq!(drained.samples, 121);
        ctx.record_generation = 94;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 94,
            duration_samples: 1_460_224,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_ne!(take.alignment_status, "sample_count_ready");
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.raw_trace_count, 121);
        assert_eq!(diag.expected_frame_count, 152);
        assert_eq!(diag.measured_frame_count, 121);
        assert_eq!(diag.missing_slots, 31);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_trace_slots"));
    }

    #[test]
    fn sparse_all_silent_trace_does_not_backfill_grid_slots() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let final_path = ctx.final_path.clone();
        ctx.record_generation = 93;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 93,
            duration_samples: 1_440_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        for t_ms in [0_u64, 7_200, 15_000] {
            ctx.trace_samples.push(trace_sample_frames_with_native(
                t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                Some(t_ms.saturating_mul(96_000) / 1_000),
                measured_silence_result(),
                false,
            ));
            mark_trace_time(
                &mut ctx,
                t_ms,
                t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                Some(t_ms.saturating_mul(96_000) / 1_000),
            );
        }
        ctx.trace_sample_count = ctx.trace_samples.len();

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_ne!(take.alignment_status, "sample_count_ready");
        assert_eq!(loaded.frames.len(), 2);
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.expected_frame_count, 150);
        assert_eq!(diag.measured_frame_count, 2);
        assert_eq!(diag.missing_slots, 148);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_trace_slots"));
    }

    #[test]
    fn close_keeps_sparse_clean_take_measured_slots_only() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let final_path = ctx.final_path.clone();
        ctx.record_generation = 7;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 7,
            duration_samples: 1_440_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        for t_ms in [0_u64, 7_200, 15_000] {
            ctx.trace_samples.push(trace_sample_frames_with_native(
                t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                Some(t_ms.saturating_mul(96_000) / 1_000),
                full_measure_result(),
                false,
            ));
            mark_trace_time(
                &mut ctx,
                t_ms,
                t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                Some(t_ms.saturating_mul(96_000) / 1_000),
            );
        }
        ctx.trace_sample_count = ctx.trace_samples.len();

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.duration_samples, 1_440_000);
        assert_eq!(take.end_t_ms, 15_000);
        assert_ne!(take.alignment_status, "sample_count_ready");
        assert_eq!(loaded.frames.len(), 2);
        assert_eq!(loaded.frames.first().map(|frame| frame.t_ms), Some(7_200));
        assert_eq!(loaded.frames.last().map(|frame| frame.t_ms), Some(15_000));
        assert!(loaded.integrity_degraded);
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.expected_frame_count, 150);
        assert_eq!(diag.measured_frame_count, 2);
        assert_eq!(diag.missing_slots, 148);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_trace_slots"));
    }

    #[test]
    fn wav_clock_sparse_fallback_frames_do_not_become_ready_after_grid_bake() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let final_path = ctx.final_path.clone();
        ctx.record_generation = 71;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 71,
            duration_samples: 1_440_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });
        for t_ms in [0_u64, 7_200, 15_000] {
            assert!(writer_append_frame(&mut ctx, t_ms, &full_measure_result()));
            mark_trace_time(
                &mut ctx,
                t_ms,
                t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                Some(t_ms.saturating_mul(96_000) / 1_000),
            );
        }

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.duration_samples, 1_440_000);
        assert_eq!(take.end_t_ms, 15_000);
        assert_eq!(take.frame_count, 2);
        assert_eq!(loaded.frames.len(), 2);
        assert_ne!(take.alignment_status, "sample_count_ready");
        assert!(loaded.integrity_degraded);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "raw_trace_timeline_gap"));
    }

    #[test]
    fn close_recovers_out_of_order_trace_samples_before_publish() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let final_path = ctx.final_path.clone();
        let queue = new_record_trace_queue();
        for t_ms in [15_000_u64, 0, 100, 7_200] {
            push_record_trace_sample(
                &queue,
                trace_sample_frames_with_native(
                    t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                    Some(t_ms.saturating_mul(96_000) / 1_000),
                    full_measure_result(),
                    false,
                ),
            );
        }
        let drained = drain_trace_queue_into_writer(&mut ctx, &queue);
        assert_eq!(drained.samples, 4);
        assert!(
            ctx.writer.data().frames.len() < 4,
            "the incremental writer can still skip older arrivals"
        );
        ctx.record_generation = 9;
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 9,
            duration_samples: 1_440_000,
            source: crate::record_take::RECORD_TAKE_SOURCE_WAV_CLOCK,
        });

        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.frames.len(), 3);
        assert_eq!(loaded.frames[0].t_ms, 100);
        assert_eq!(loaded.frames[1].t_ms, 7_200);
        assert_eq!(loaded.frames.last().map(|frame| frame.t_ms), Some(15_000));
        assert!(loaded.integrity_degraded);
        let diag = loaded
            .trace_diagnostics
            .as_ref()
            .expect("trace diagnostics");
        assert_eq!(diag.expected_frame_count, 150);
        assert_eq!(diag.measured_frame_count, 3);
        assert_eq!(diag.missing_slots, 147);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_trace_slots"));
    }

    #[test]
    fn run_record_tick_does_not_mix_wall_clock_when_trace_queue_is_empty() {
        let base = isolated_base();
        let started = now_epoch_ms() - 600;
        let ctx = make_ctx(&base, Role::Pre, started);
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);
        let queue = new_record_trace_queue();

        run_record_tick(
            &sm,
            Role::Pre,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            Some(&queue),
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert!(
            ctx_ref.data().frames.is_empty(),
            "modern Record must not mix wall-clock fallback frames into audio-time TRACE"
        );
        assert_eq!(
            ctx_ref.data().psb_snapshots.as_ref().map(|v| v.len()),
            Some(0)
        );
    }

    #[test]
    fn run_record_tick_legacy_without_trace_queue_uses_live_snapshot() {
        let base = isolated_base();
        let started = now_epoch_ms() - 600;
        let ctx = make_ctx(&base, Role::Pre, started);
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        run_record_tick(
            &sm,
            Role::Pre,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            None,
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 1);
        assert!(
            ctx_ref.data().frames[0].t_ms >= 500,
            "legacy fallback snapshot keeps the wall-clock axis only when no TRACE queue exists"
        );
    }

    #[test]
    fn run_record_tick_writes_silent_floor_when_trace_queue_has_silent_samples() {
        let base = isolated_base();
        let ctx = make_ctx(&base, Role::Pre, now_epoch_ms());
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);
        let queue = new_record_trace_queue();
        push_record_trace_sample(&queue, trace_sample(100, measured_silence_result(), false));

        run_record_tick(
            &sm,
            Role::Pre,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            Some(&queue),
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(
            ctx_ref.data().frames.len(),
            1,
            "a drained silent audio-time sample must become an explicit silence frame"
        );
        assert_eq!(ctx_ref.data().frames[0].lufs_m, TRACE_SILENCE_LUFS);
        assert_eq!(
            ctx_ref.data().frames[0].true_peak,
            TRACE_SILENCE_TRUE_PEAK_DBTP
        );
        assert_eq!(ctx_ref.data().frames[0].crest, TRACE_SILENCE_CREST_DB);
        assert_eq!(ctx_ref.last_trace_t_ms, Some(100));
    }

    #[test]
    fn run_record_tick_writes_silent_floor_when_observed_trace_core_is_empty() {
        let base = isolated_base();
        let ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);
        let queue = new_record_trace_queue();
        push_record_trace_sample(
            &queue,
            RecordTraceSample::measured_observed(
                1,
                100,
                TRACE_TIMEBASE_HZ / 10,
                Some(9_600),
                MeasureResult::default(),
                true,
                false,
            ),
        );

        run_record_tick(
            &sm,
            Role::Post,
            96000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            Some(&queue),
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 1);
        assert_eq!(ctx_ref.data().frames[0].t_ms, 100);
        assert_eq!(ctx_ref.data().frames[0].lufs_m, TRACE_SILENCE_LUFS);
        assert_eq!(
            ctx_ref.data().frames[0].true_peak,
            TRACE_SILENCE_TRUE_PEAK_DBTP
        );
        assert_eq!(ctx_ref.data().frames[0].crest, TRACE_SILENCE_CREST_DB);
    }

    #[test]
    fn run_record_tick_preserves_audio_time_when_trace_sample_is_missing_marker() {
        let base = isolated_base();
        let ctx = make_ctx(&base, Role::Pre, now_epoch_ms());
        let final_path = ctx.final_path.clone();
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(MeasureResult::default()));
        let mut rec: Option<RecordingCtx> = Some(ctx);
        let queue = new_record_trace_queue();
        push_record_trace_sample(&queue, trace_sample(100, full_measure_result(), true));
        push_record_trace_sample(&queue, trace_sample(11_000, full_measure_result(), false));
        push_record_trace_sample(&queue, missing_trace_marker(15_400));

        run_record_tick(
            &sm,
            Role::Pre,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(),
            Some(&queue),
        )
        .unwrap();

        let ctx = rec.take().unwrap();
        assert_eq!(
            ctx.data().frames.len(),
            2,
            "missing trace marker must preserve time without becoming a silence frame"
        );
        assert_eq!(ctx.last_trace_t_ms, Some(15_400));
        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.frames.len(), 2);
        assert_eq!(loaded.frames.first().map(|frame| frame.t_ms), Some(100));
        assert_eq!(loaded.frames.last().map(|frame| frame.t_ms), Some(11_000));
        assert_eq!(loaded.bounce_marker.duration_samples, 739_200);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.source, "audio_time_trace");
        assert_eq!(take.time_axis, "frames_48k");
        assert_eq!(take.alignment_status, "trace_span_only");
        assert_eq!(take.duration_samples, 739_200);
        assert_eq!(take.wav_start_sample, 0);
        assert_eq!(take.wav_end_sample, 739_200);
        assert!(loaded.integrity_degraded);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_trace_slots"));
    }

    #[test]
    fn bounce_take_converts_48k_trace_frames_to_96k_wav_samples() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 96_000);
        let final_path = ctx.final_path.clone();
        let queue = new_record_trace_queue();
        for t_ms in (0_u64..=15_000).step_by(FRAME_INTERVAL_MS as usize) {
            push_record_trace_sample(
                &queue,
                trace_sample_frames(
                    t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                    full_measure_result(),
                    t_ms == 0,
                ),
            );
        }

        let drained = drain_trace_queue_into_writer(&mut ctx, &queue);
        assert_eq!(drained.samples, 151);
        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.bounce_marker.duration_samples, 1_440_000);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.duration_frames_48k, 720_000);
        assert_eq!(take.duration_samples, 1_440_000);
        assert_eq!(take.wav_start_sample, 0);
        assert_eq!(take.wav_end_sample, 1_440_000);
        assert_eq!(take.end_t_ms, 15_000);
        assert_eq!(take.alignment_status, "trace_span_only");
    }

    #[test]
    fn bounce_take_prefers_native_sample_frames_at_44100() {
        let base = isolated_base();
        let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 44_100);
        let final_path = ctx.final_path.clone();
        let queue = new_record_trace_queue();
        for t_ms in (0_u64..=15_000).step_by(FRAME_INTERVAL_MS as usize) {
            push_record_trace_sample(
                &queue,
                trace_sample_frames_with_native(
                    t_ms.saturating_mul(TRACE_TIMEBASE_HZ) / 1_000,
                    Some(t_ms.saturating_mul(44_100) / 1_000),
                    full_measure_result(),
                    t_ms == 0,
                ),
            );
        }

        let drained = drain_trace_queue_into_writer(&mut ctx, &queue);
        assert_eq!(drained.samples, 151);
        writer_close(ctx);

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.bounce_marker.duration_samples, 661_500);
        let take = loaded.bounce_take.as_ref().expect("bounce_take");
        assert_eq!(take.source, "audio_time_trace_native");
        assert_eq!(take.time_axis, "native_samples");
        assert_eq!(take.sample_rate, 44_100);
        assert_eq!(take.duration_frames_48k, 720_000);
        assert_eq!(take.duration_samples, 661_500);
        assert_eq!(take.wav_start_sample, 0);
        assert_eq!(take.wav_end_sample, 661_500);
        assert_eq!(take.end_t_ms, 15_000);
        assert_eq!(take.alignment_status, "trace_span_only");
    }

    #[test]
    fn run_record_tick_record_phase_d_warmup_keeps_core_frame() {
        let base = isolated_base();
        let started = now_epoch_ms() - 200;
        let ctx = make_ctx(&base, Role::Post, started);
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let mut m0 = full_measure_result();
        m0.n_prime = None;
        m0.sharpness = None;
        m0.psb_bark = None;
        let m = Arc::new(Mutex::new(m0));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(), // B-125: oversized_drop（テストは欠落なし=0）
            None,
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 1);
        assert!(ctx_ref.data().frames[0].n_prime.is_none());
        assert!(ctx_ref.data().frames[0].sharpness.is_none());
        assert_eq!(
            ctx_ref.data().psb_snapshots.as_ref().map(|v| v.len()),
            Some(0)
        );
    }

    #[test]
    fn pre_and_post_share_t_ms_axis() {
        let base = isolated_base();
        let started = now_epoch_ms() - 300;
        let mut pre = make_ctx(&base, Role::Pre, started);
        let mut post = make_ctx(&base, Role::Post, started);
        let m = full_measure_result();

        let t_ms = (now_epoch_ms() - started) as u64;
        assert!(writer_append_frame(&mut pre, t_ms, &m));
        assert!(writer_append_frame(&mut post, t_ms, &m));
        assert_eq!(pre.data().frames[0].t_ms, post.data().frames[0].t_ms);
    }

    /// POST → PRE ペアリング統合テスト (path mismatch 退行防止 / 新構造版)。
    #[test]
    fn post_write_pending_triggers_pre_record_entry() {
        let base = isolated_base();
        let post_iid = "post-iid-1";

        let written = record_signal::write_pending(
            &base,
            TEST_PH,
            post_iid,
            "pre-iid-1".into(),
            "daw-1".into(),
        )
        .expect("write_pending must succeed");
        assert!(!written.started_at.is_empty());
        assert_eq!(written.daw_session_id, "daw-1");

        let signal = record_signal::read_signal(&base, TEST_PH, post_iid)
            .expect("PRE must find signal at the same base POST wrote to");
        assert_eq!(signal.status, record_signal::SignalStatus::Pending);

        let sm = RecordStateMachine::new();
        sm.try_enter_record(License::Os)
            .expect("Os license must allow entering Record");
        assert!(sm.is_recording());

        let resolved = resolve_started_at_ms(&base, TEST_PH, post_iid);
        let expected = parse_iso8601_to_epoch_ms(&signal.started_at)
            .expect("started_at must be parseable ISO 8601");
        assert_eq!(resolved, expected);
    }

    // ── B-025 Group B-1 / Gap-8 (recover_orphan_tmps) ─────────────────────

    /// 有効な Active .tmp (整合 HMAC-SHA256) に frame があっても、Pair/expected gate を
    /// 通らなければ `.failed` に救済される。通常棚には出さない。
    #[test]
    fn recover_orphan_tmps_recovers_valid_tmp() {
        let base = isolated_base();
        // 1. frame を積んで flush する → bytes を採取。
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        assert!(writer_append_frame(&mut ctx, 100, &full_measure_result()));
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let staging_path = ctx.staging_path.clone();
        let valid_bytes = fs::read(&staging_path).unwrap();
        // 2. .partial を消し、同 dir に .json.tmp として配置 (DAW crash 直前 = .tmp 残置)。
        fs::remove_file(&staging_path).unwrap();
        let tmp_path = final_path.with_extension("json.tmp");
        fs::write(&tmp_path, &valid_bytes).unwrap();

        let report = recover_orphan_tmps(&base);
        assert_eq!(report.recovered, 1);
        assert_eq!(report.orphaned, 0);
        assert!(
            !final_path.exists(),
            ".json must not be published by tmp recovery without pair/expected gate"
        );
        let failed_path = failed_path_for_final(&final_path);
        assert!(failed_path.exists(), ".failed JSON must be written");
        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.status, Status::Closed);
        assert_eq!(loaded.commit_status.as_deref(), Some("failed"));
        assert!(
            loaded.integrity_degraded,
            "recovered Active tmp must be marked degraded instead of disappearing"
        );
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_expected_wav_metadata"));
        assert!(verify_checksum(&loaded));
        assert!(
            !staging_path.exists(),
            ".partial must not remain after publish recovery"
        );
        assert!(!tmp_path.exists(), ".tmp must be renamed away");
    }

    /// checksum 不整合の .tmp は orphaned として残置 (削除しない)。
    #[test]
    fn recover_orphan_tmps_keeps_invalid_tmp() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let staging_path = ctx.staging_path.clone();
        let mut bytes = fs::read(&staging_path).unwrap();
        // checksum 値を破壊 (HMAC mismatch)。
        // checksum field の hex string 末尾を別文字に置換。
        let s = String::from_utf8(bytes.clone()).unwrap();
        let mutated = s.replace(r#""validity":true"#, r#""validity":false"#);
        bytes = mutated.into_bytes();

        fs::remove_file(&staging_path).unwrap();
        let tmp_path = final_path.with_extension("json.tmp");
        fs::write(&tmp_path, &bytes).unwrap();

        let report = recover_orphan_tmps(&base);
        assert_eq!(report.recovered, 0);
        assert_eq!(report.orphaned, 1);
        assert!(!final_path.exists(), ".json must NOT exist for orphan");
        assert!(tmp_path.exists(), ".tmp must be kept (warn-only)");
    }

    /// fresh .partial は実行中 Record の可能性があるため no-op。
    #[test]
    fn recover_orphan_tmps_ignores_fresh_staging() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        assert!(writer_append_frame(&mut ctx, 100, &full_measure_result()));
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let staging_path = ctx.staging_path.clone();
        assert!(!final_path.exists());
        assert!(staging_path.exists());

        let report = recover_orphan_tmps(&base);
        assert_eq!(report.recovered, 0);
        assert_eq!(report.orphaned, 0);
        assert!(!final_path.exists());
        assert!(staging_path.exists());
    }

    /// stale .partial に usable frames があっても通常棚へは出さず `.failed` へ隔離する。
    #[test]
    fn recover_orphan_tmps_recovers_stale_staging_with_frames() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        assert!(writer_append_frame(&mut ctx, 100, &full_measure_result()));
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let staging_path = ctx.staging_path.clone();

        let stale = SystemTime::now() - Duration::from_secs(STALE_ACTIVE_SWEEP_SECS + 1);
        let f = std::fs::File::open(&staging_path).unwrap();
        f.set_modified(stale).unwrap();
        drop(f);

        let report = recover_orphan_tmps(&base);
        assert_eq!(report.recovered, 1);
        assert_eq!(report.orphaned, 0);
        assert!(
            !final_path.exists(),
            "stale usable partial must not publish without pair/expected gate"
        );
        assert!(
            failed_path_for_final(&final_path).exists(),
            "stale usable partial must become failed diagnostic"
        );
        assert!(
            !staging_path.exists(),
            "staging must be removed after publish"
        );

        let loaded: PluginDataFile = read_output_for_final(&final_path);
        assert_eq!(loaded.status, Status::Closed);
        assert_eq!(loaded.commit_status.as_deref(), Some("failed"));
        assert!(
            loaded.integrity_degraded,
            "startup recovered partial must be marked degraded but usable"
        );
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "missing_expected_wav_metadata"));
        assert!(verify_checksum(&loaded));
    }

    /// stale .partial でも frames が無ければ通常棚へは出さず `.failed/` に隔離する。
    #[test]
    fn recover_orphan_tmps_moves_stale_empty_staging_to_failed() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let staging_path = ctx.staging_path.clone();
        let failed_path = ctx.failed_path.clone();

        let stale = SystemTime::now() - Duration::from_secs(STALE_ACTIVE_SWEEP_SECS + 1);
        let f = std::fs::File::open(&staging_path).unwrap();
        f.set_modified(stale).unwrap();
        drop(f);

        let report = recover_orphan_tmps(&base);
        assert_eq!(report.recovered, 1);
        assert_eq!(report.orphaned, 0);
        assert!(!final_path.exists(), "empty partial must not publish");
        assert!(!staging_path.exists(), "empty partial must be removed");
        assert!(failed_path.exists(), "empty partial must leave diagnostics");

        let loaded: PluginDataFile =
            serde_json::from_slice(&fs::read(&failed_path).unwrap()).unwrap();
        assert_eq!(loaded.status, Status::Closed);
        assert_eq!(loaded.commit_status.as_deref(), Some("failed"));
        assert!(!loaded.validity);
        assert!(loaded.integrity_degraded);
        assert!(loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "zero_trace_frames"));
        assert!(verify_checksum(&loaded));
    }

    /// plugin_data root 自体が不在の場合は no-op で安全に終わる (R-28 機能的沈黙)。
    #[test]
    fn recover_orphan_tmps_missing_root_no_op() {
        let nonexistent =
            std::env::temp_dir().join(format!("kirin_recover_missing_{}", std::process::id()));
        let _ = fs::remove_dir_all(&nonexistent);
        let report = recover_orphan_tmps(&nonexistent);
        assert_eq!(report.recovered, 0);
        assert_eq!(report.orphaned, 0);
    }

    // ── B-026 / Gap-9 (sweep_stale_active_at_startup) ─────────────────────

    /// status=Active かつ mtime > 60s (= STALE_ACTIVE_SWEEP_SECS) のファイルは
    /// status=Closed に書き換えられ checksum が再計算される (verify_checksum 通過)。
    #[test]
    fn sweep_stale_active_closes_stale_file() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Pre, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let staging_path = ctx.staging_path.clone();
        fs::rename(&staging_path, &final_path).unwrap();
        drop(ctx);

        // mtime を 61 秒過去に巻き戻す。
        let stale = SystemTime::now() - Duration::from_secs(STALE_ACTIVE_SWEEP_SECS + 1);
        let f = std::fs::File::open(&final_path).unwrap();
        f.set_modified(stale).unwrap();
        drop(f);

        let report = sweep_stale_active_at_startup(&base);
        assert_eq!(report.closed, 1, "stale Active file must be closed");
        assert_eq!(report.skipped, 0);

        let bytes = fs::read(&final_path).unwrap();
        let data: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(
            data.status,
            Status::Closed,
            "status must flip Active → Closed"
        );
        assert!(
            verify_checksum(&data),
            "checksum must be recomputed and verify successfully"
        );
    }

    /// fresh (mtime=now) は touch されない (closed=0, skipped=0)。
    /// fresh は per-tick 経路 (PRE_LIVENESS_STALE_SECS) の範囲なので startup
    /// sweep が干渉しないことを構造的に保証する。
    #[test]
    fn sweep_stale_active_ignores_fresh_file() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Pre, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let staging_path = ctx.staging_path.clone();
        fs::rename(&staging_path, &final_path).unwrap();
        drop(ctx);

        let report = sweep_stale_active_at_startup(&base);
        assert_eq!(report.closed, 0, "fresh file must not be closed");
        assert_eq!(
            report.skipped, 0,
            "fresh file must be silently passed (not counted as skipped)"
        );

        let bytes = fs::read(&final_path).unwrap();
        let data: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(data.status, Status::Active, "fresh file must remain Active");
    }

    /// 既に status=Closed のファイルは mtime > 60s でも対象外 (二重 close 回避)。
    #[test]
    fn sweep_stale_active_ignores_closed_file() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let staging_path = ctx.staging_path.clone();
        fs::rename(&staging_path, &final_path).unwrap();
        drop(ctx);

        // 直接 status を Closed に書換 + checksum 再計算 (close() consumption 回避)。
        let bytes = fs::read(&final_path).unwrap();
        let mut data: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        data.status = Status::Closed;
        data.checksum = compute_checksum(&data).unwrap();
        let json = serde_json::to_vec(&data).unwrap();
        fs::write(&final_path, &json).unwrap();

        // mtime を 61 秒過去に巻き戻す。
        let stale = SystemTime::now() - Duration::from_secs(STALE_ACTIVE_SWEEP_SECS + 1);
        let f = std::fs::File::open(&final_path).unwrap();
        f.set_modified(stale).unwrap();
        drop(f);

        let report = sweep_stale_active_at_startup(&base);
        assert_eq!(report.closed, 0, "Closed file must not be re-closed");
        assert_eq!(report.skipped, 0);

        let bytes = fs::read(&final_path).unwrap();
        let data2: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(data2.status, Status::Closed);
    }

    // ── B-025 Group B-2 / Gap-19 + Group B-3 / Gap-20 ────────────────────

    /// role dir が消えても flush が親を再作成し、Record は止まらない。
    #[test]
    fn run_record_tick_recreates_missing_parent_and_keeps_recording() {
        let base = isolated_base();
        let started = now_epoch_ms() - 100;
        let mut ctx = make_ctx(&base, Role::Post, started);
        // role dir を消去 → flush で再作成。
        let parent = ctx.final_path.parent().unwrap().to_path_buf();
        let staging_path = ctx.staging_path.clone();
        fs::remove_dir_all(&parent).unwrap();
        // 即時 flush 実行のため next_flush を過去に。
        ctx.next_flush = Instant::now() - Duration::from_secs(1);

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(), // B-125: oversized_drop（テストは欠落なし=0）
            None,
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert!(sm.is_recording(), "missing parent must not stop Record");
        assert!(parent.is_dir(), "flush must recreate the missing role dir");
        assert!(
            staging_path.is_file(),
            "flush must recover and write the staging JSON"
        );
        assert_eq!(ctx_ref.consecutive_write_error, 0);
    }

    /// flush() が `Io` を 3 回連続で返しても Record は止めず、degraded を立てる。
    /// `tmp_path` の場所をディレクトリにすると `fs::write` が Io error を返す。
    #[test]
    fn run_record_tick_write_error_threshold_marks_degraded_without_exit() {
        let base = isolated_base();
        let started = now_epoch_ms() - 100;
        let mut ctx = make_ctx(&base, Role::Post, started);
        // tmp_path に同名 dir を作成 → fs::write が EISDIR で失敗。
        let tmp_path = ctx.final_path.with_extension("json.tmp");
        fs::create_dir_all(&tmp_path).unwrap();
        ctx.next_flush = Instant::now() - Duration::from_secs(1);

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        for i in 1..=CONSECUTIVE_FAILURE_THRESHOLD {
            run_record_tick(
                &sm,
                Role::Post,
                48000,
                TEST_PH,
                TEST_IID,
                now_epoch_ms,
                || None,
                || None,
                &m,
                &mut rec,
                None,
                &no_overflow(),
                &no_overflow(), // B-125: oversized_drop（テストは欠落なし=0）
                None,
            )
            .unwrap();
            let ctx_ref = rec.as_ref().unwrap();
            assert_eq!(
                ctx_ref.consecutive_write_error, i,
                "tick {} should bump write_error counter to {}",
                i, i
            );
            assert!(
                ctx_ref.data().integrity_degraded,
                "write failure must be visible as degraded data"
            );
            assert!(sm.is_recording(), "write failure must not stop Record");
            rec.as_mut().unwrap().next_flush = Instant::now() - Duration::from_secs(1);
        }
    }

    /// 失敗 → 成功で counter が 0 に戻る (transient 失敗の吸収)。
    /// tmp_path衝突 → 1 回失敗 → 衝突解消 → 1 回成功 で 0 リセット。
    #[test]
    fn run_record_tick_counter_resets_on_success() {
        let base = isolated_base();
        let started = now_epoch_ms() - 100;
        let mut ctx = make_ctx(&base, Role::Post, started);
        let tmp_path = ctx.final_path.with_extension("json.tmp");
        fs::create_dir_all(&tmp_path).unwrap();
        ctx.next_flush = Instant::now() - Duration::from_secs(1);

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        // 1 回失敗 (Io) → counter=1。
        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(), // B-125: oversized_drop（テストは欠落なし=0）
            None,
        )
        .unwrap();
        assert_eq!(rec.as_ref().unwrap().consecutive_write_error, 1);
        assert!(rec.as_ref().unwrap().data().integrity_degraded);
        assert!(
            sm.is_recording(),
            "transient write failure must not stop Record"
        );

        // tmp_path 衝突解消 + flush 即時化 → 成功で 0 リセット。
        fs::remove_dir_all(&tmp_path).unwrap();
        rec.as_mut().unwrap().next_flush = Instant::now() - Duration::from_secs(1);
        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
            None,
            &no_overflow(),
            &no_overflow(), // B-125: oversized_drop（テストは欠落なし=0）
            None,
        )
        .unwrap();
        assert_eq!(rec.as_ref().unwrap().consecutive_write_error, 0);
        assert!(sm.is_recording());
    }

    /// B-076: per-Record dropped_samples が .kirin JSON に出る + 複数 Record で累積を持ち越さない。
    #[test]
    fn dropped_samples_per_record_isolated_in_kirin_json() {
        use std::sync::atomic::Ordering;
        let base = isolated_base();
        let overflow = no_overflow(); // 累積 push_overflow を共有（Audio Thread 模擬）
        let m = Arc::new(Mutex::new(full_measure_result()));

        // ── Record 1: overflow_start=0、Record 中に 5 drop、close → dropped_samples=5。
        let mut ctx1 = make_ctx(&base, Role::Post, now_epoch_ms()); // overflow_start=0
        assert!(writer_append_frame(&mut ctx1, 100, &full_measure_result()));
        let path1 = ctx1.final_path.clone();
        let sm1 = Arc::new(RecordStateMachine::new());
        sm1.try_enter_record(License::Os).unwrap();
        let mut rec1 = Some(ctx1);
        overflow.fetch_add(5, Ordering::Relaxed); // この Record 中に 5 サンプル drop
        sm1.exit_record();
        sm1.bump_seal(); // B-132: measure の tight-drain 完了模擬（seal 前進 → close arm 即 true）
        run_record_tick(
            &sm1,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec1,
            None,
            &overflow,
            &no_overflow(), // B-125: oversized_drop なし（push_overflow 経路のみ検証）
            None,
        )
        .unwrap();
        let f1: PluginDataFile = read_output_for_final(&path1);
        assert_eq!(f1.dropped_samples, 5, "Record 1 の欠落数が .kirin に出る");
        assert!(f1.integrity_degraded, "dropped>0 → integrity_degraded");

        // ── Record 2: overflow_start=現累積(5) を snapshot、drop なし、close → dropped_samples=0。
        //    前 Record の累積 5 を持ち越さない（per-Record 差分）。read(f1) は ctx2 生成より前。
        let mut ctx2 = make_ctx(&base, Role::Post, now_epoch_ms());
        ctx2.overflow_start = overflow.load(Ordering::Relaxed); // Record 2 開始 snapshot = 5
        assert!(writer_append_frame(&mut ctx2, 100, &full_measure_result()));
        let path2 = ctx2.final_path.clone();
        let sm2 = Arc::new(RecordStateMachine::new());
        sm2.try_enter_record(License::Os).unwrap();
        let mut rec2 = Some(ctx2);
        // この Record 中は drop なし（overflow 据置 5）
        sm2.exit_record();
        sm2.bump_seal(); // B-132: measure の tight-drain 完了模擬（seal 前進 → close arm 即 true）
        run_record_tick(
            &sm2,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec2,
            None,
            &overflow,
            &no_overflow(), // B-125: oversized_drop なし（push_overflow 経路のみ検証）
            None,
        )
        .unwrap();
        let f2: PluginDataFile = read_output_for_final(&path2);
        assert_eq!(
            f2.dropped_samples, 0,
            "per-Record: 前 Record の累積 5 を持ち越さない"
        );
    }

    /// B-076 / B-141: additive field を持たない旧 .kirin を #[serde(default)] で読める。
    #[test]
    fn old_kirin_without_integrity_fields_reads_via_serde_default() {
        let base = isolated_base();
        let f = make_ctx(&base, Role::Post, now_epoch_ms()).data().clone();
        let mut v = serde_json::to_value(&f).unwrap();
        let obj = v.as_object_mut().unwrap();
        // 旧 .kirin を模擬（新 field を除去）
        obj.remove("started_at_ms");
        obj.remove("dropped_samples");
        obj.remove("integrity_degraded");
        obj.remove("bounce_take");
        let back: PluginDataFile = serde_json::from_value(v).unwrap();
        assert_eq!(back.started_at_ms, 0, "旧 .kirin → serde default 0");
        assert_eq!(back.dropped_samples, 0, "旧 .kirin → serde default 0");
        assert!(!back.integrity_degraded, "旧 .kirin → serde default false");
        assert!(back.bounce_take.is_none(), "旧 .kirin → serde default None");
    }

    // ── B-132 (G-115-382 共通B): bounded seal wait ──────────────────────────────
    use std::time::Duration;

    /// seal が前進すれば wait は true を返す（measure が post-drain finalize 完了を通知）。
    #[test]
    fn b132_wait_for_seal_true_when_seal_advances() {
        let sm = RecordStateMachine::new();
        let start = sm.seal();
        sm.bump_seal(); // measure 側 drain 完了相当
        assert!(super::wait_for_seal(&sm, start, Duration::from_millis(200)));
    }

    /// seal が前進しなければ wait は **bounded** に false を返す（measure 死/stall）。
    /// = deadlock 回帰防止: 待ちは必ず有限時間で抜ける（unbounded wait なら teardown deadlock）。
    #[test]
    fn b132_wait_for_seal_times_out_bounded_when_stalled() {
        let sm = RecordStateMachine::new();
        let start = sm.seal();
        let t0 = Instant::now();
        let sealed = super::wait_for_seal(&sm, start, Duration::from_millis(120));
        let elapsed = t0.elapsed();
        assert!(!sealed, "seal 不前進 → false（timeout）");
        assert!(
            elapsed >= Duration::from_millis(120) && elapsed < Duration::from_millis(2000),
            "bounded: timeout 近傍で抜ける（elapsed={elapsed:?}）"
        );
    }

    /// 先行 session の seal を今回 session の barrier が誤検出しない（seal_at_start で識別）。
    #[test]
    fn b132_wait_for_seal_ignores_prior_session_seal() {
        let sm = RecordStateMachine::new();
        sm.bump_seal(); // 先行 session の seal
        let start = sm.seal(); // 今回 session 開始 snapshot（= 1）
                               // 今回 session はまだ drain していない → 前進なし → timeout false。
        assert!(!super::wait_for_seal(&sm, start, Duration::from_millis(60)));
        sm.bump_seal(); // 今回 session の drain 完了
        assert!(super::wait_for_seal(&sm, start, Duration::from_millis(200)));
    }
}
