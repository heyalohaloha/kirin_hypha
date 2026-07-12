//! Measure Thread 起動モジュール。
//!
//!  3層隔離:
//! - Audio Thread は ring buffer Producer に書くだけ（このファイルに触れない）
//! - Measure Thread（このファイル）はクラッシュしても Audio Thread を止めない
//! - IO Thread（T-4/T-5）は measure_result を読むだけ

use crate::phase_d::stream::PhaseDStream;
use crate::phase_d::tables::FieldType;
use crate::record::RecordStateMachine;
use crate::record_take::{
    CaptureClockPoint, CaptureClockSource, PresentationLatencySamples, RecordTakeTracker,
};
use crate::record_writer::{
    clear_record_trace_queue, now_epoch_ms, push_record_trace_sample,
    samples_are_trace_silence_floor, RecordTraceQueue, RecordTraceSample, FRAME_INTERVAL_MS,
    PSB_INTERVAL_MS,
};
use crate::resampler::ResamplerTo48k;
use crate::watch_playback_pass::watch_ring_cursor_samples_for_pass;
use crate::{
    engine::SessionSummary, load_signal_state, store_signal_state, MeasureEngine, MeasureResult,
    PsbSummary, SignalState, N_CHANNELS,
};

/// Watch core と PhaseD の内部処理 SR。Record core は host native SR で別 engine を動かし、
/// 100 ms native boundary をそのまま保持する。Record の PhaseD だけ有限長 resample を行い、
/// delay trim + finish で同じ 100 ms slot に結合する。
const ENGINE_SR: u32 = 48_000;
use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

/// Measure Thread の通常ループ間隔。
const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// Record 中の idle poll 間隔。
///
/// Offline bounce は wall-clock より速く Audio Thread が進むため、Record 中だけ短い sleep にして
/// Audio→Measure ring の滞留を減らす。Audio Thread からの通知や lock は増やさない。
const RECORD_LOOP_SLEEP: Duration = Duration::from_millis(1);

/// Watch→Record の検出が filesystem polling 分だけ遅れたときに、直前の Watch TRACE を
/// Record に復元する最大音声時間。60 秒なら 10 fps TRACE で約 600 点の低メモリ履歴に収まる。
const RECORD_PRE_ROLL_MAX_MS: u64 = 60_000;

/// G-115-245 決定文言: heartbeat が変化しないまま何 tick 経過したら process() 停止と判定するか。
/// **30 tick** × **TICK(=LOOP_SLEEP=100ms)** = **3s**（DAW の一時 stall を吸収）。30 / 100ms を
/// literal に保持し、live ウィンドウ Duration を `live_window()` で導出する。
const HEARTBEAT_STALE_TICKS: u32 = 30;
const TICK: Duration = LOOP_SLEEP; // 100ms

/// B-118 / G-115-245 執行: 鮮度の live ウィンドウ = 30 tick × 100ms = 3s。製品はこの導出値、
/// テストは `LivenessEvaluator::new` に任意 Duration を注入する。
pub fn live_window() -> Duration {
    TICK * HEARTBEAT_STALE_TICKS
}

#[inline]
fn idle_sleep_for_record_state(is_recording: bool) -> Duration {
    if is_recording {
        RECORD_LOOP_SLEEP
    } else {
        LOOP_SLEEP
    }
}

fn record_grid_alignment(position_start: i64, sample_rate: u32) -> (usize, i64) {
    let slot_frames = (sample_rate as i64 / 10).max(1);
    let phase_frames = position_start.rem_euclid(slot_frames) as usize;
    let remaining = if phase_frames == 0 {
        slot_frames
    } else {
        slot_frames.saturating_sub(phase_frames as i64)
    };
    (phase_frames, position_start.saturating_add(remaining))
}

#[derive(Debug, Clone)]
struct PreRollTraceSample {
    captured_at_ms: i64,
    position_samples: Option<i64>,
    clock_source: CaptureClockSource,
    presentation_latency: PresentationLatencySamples,
    frames_48k: u64,
    native_frames: u64,
    observed_silence_floor: bool,
    result: MeasureResult,
}

#[derive(Debug, Default)]
struct RecordTracePreRoll {
    samples: VecDeque<PreRollTraceSample>,
}

impl RecordTracePreRoll {
    fn clear(&mut self) {
        self.samples.clear();
    }

    #[cfg(test)]
    fn push(
        &mut self,
        captured_at_ms: i64,
        position_samples: Option<i64>,
        frames_48k: u64,
        native_frames: u64,
        observed_silence_floor: bool,
        result: &MeasureResult,
    ) {
        let source = position_samples.map_or(CaptureClockSource::Unknown, |_| {
            CaptureClockSource::ProjectTimeline
        });
        self.push_with_clock(
            captured_at_ms,
            position_samples,
            source,
            PresentationLatencySamples::default(),
            frames_48k,
            native_frames,
            observed_silence_floor,
            result,
        );
    }

    #[allow(clippy::too_many_arguments)]
    fn push_with_clock(
        &mut self,
        captured_at_ms: i64,
        position_samples: Option<i64>,
        clock_source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
        frames_48k: u64,
        native_frames: u64,
        observed_silence_floor: bool,
        result: &MeasureResult,
    ) {
        self.samples.push_back(PreRollTraceSample {
            captured_at_ms,
            position_samples,
            clock_source,
            presentation_latency,
            frames_48k,
            native_frames,
            observed_silence_floor,
            result: result.clone(),
        });
        self.trim(captured_at_ms, frames_48k);
    }

    fn trim(&mut self, now_ms: i64, latest_frames_48k: u64) {
        let max_frames = RECORD_PRE_ROLL_MAX_MS.saturating_mul(ENGINE_SR as u64) / 1_000;
        while self.samples.front().is_some_and(|front| {
            let wall_age_ms = now_ms.saturating_sub(front.captured_at_ms).max(0) as u64;
            let frame_age = latest_frames_48k.saturating_sub(front.frames_48k);
            wall_age_ms > RECORD_PRE_ROLL_MAX_MS || frame_age > max_frames
        }) {
            self.samples.pop_front();
        }
    }
}

/// B-115: POST pair 変更ロックの述語。**実再生中（playing）かつ live**（processBlock 進行中）の
/// ときだけロックする。playing が凍結値でも live=false ならロックしない（false-release 防止 /
/// G-115-248「実再生中は lock 維持・processing 停止中は解除」の精緻化。SignalState とは別軸＝
/// B-107 で無音再生中も state=Inactive になるため state を live の代用にしない）。
#[inline]
pub fn pair_lock_active(playing: bool, live: bool) -> bool {
    playing && live
}

/// B-118: 単一鮮度評価器（per-instance）。heartbeat counter の `last_seen` と最終変化時刻
/// （単調時計 `Instant`）を保持し、`is_live()` =「heartbeat の最終変化から live ウィンドウ
/// （G-115-245: 30×100ms=3s）以内」を返す。
///
/// **単一源**: 表示（measure loop の signal_state→Inactive 上書き）/ POST pair lock 述語 /
/// FFI getter / watchdog が全てこの 1 評価器を読む。**非 RT 読み手専用**（measure loop /
/// editor timer / watchdog / FFI getter）。Audio Thread からは呼ばない。アロケーションなし
/// （atomic + `Instant` 読みのみ）。複数 reader の並行 `is_live()` は benign（heartbeat 変化を
/// 最初に観測した reader が `last_change` を更新し、他 reader はその共有値で判定）。
pub struct LivenessEvaluator {
    /// 観測対象 heartbeat（Audio Thread が毎ブロック +1 / 評価器は読むだけ）。
    heartbeat: Arc<AtomicU32>,
    /// 直近に観測した heartbeat 値。
    last_seen: AtomicU32,
    /// heartbeat が最後に変化した `epoch` からの経過 ns（生成時 0 = epoch 起点）。
    last_change_nanos: AtomicU64,
    /// live ウィンドウ（ns）。製品 = `live_window()`、テスト = 注入値。
    window_nanos: u64,
    /// 単調時計の基点（生成時刻）。`is_live` は `epoch.elapsed()` で now を取る。
    epoch: Instant,
}

impl LivenessEvaluator {
    /// `heartbeat` は engine の共有カウンタ。`window` は製品 `live_window()`（3s）/ テスト任意。
    /// 生成直後は `last_change=0`（epoch 起点）なので、生成から window 内は live（既存 measure
    /// loop の「生成直後は live」意味論と一致）。
    pub fn new(heartbeat: Arc<AtomicU32>, window: Duration) -> Self {
        let last_seen = heartbeat.load(Ordering::Relaxed);
        Self {
            heartbeat,
            last_seen: AtomicU32::new(last_seen),
            last_change_nanos: AtomicU64::new(0),
            window_nanos: window.as_nanos() as u64,
            epoch: Instant::now(),
        }
    }

    /// 製品 reader 用: 内部 `epoch` で now を取り、heartbeat を観測して鮮度を返す。
    pub fn is_live(&self) -> bool {
        self.is_live_at(self.epoch.elapsed().as_nanos() as u64)
    }

    /// 実体（テストは `elapsed_nanos`＝epoch からの経過 ns を注入）。heartbeat 変化を観測したら
    /// `last_change` を更新し live。無変化なら `(now - last_change) < window` で判定。
    pub fn is_live_at(&self, elapsed_nanos: u64) -> bool {
        let cur = self.heartbeat.load(Ordering::Relaxed);
        let prev = self.last_seen.swap(cur, Ordering::Relaxed);
        if prev != cur {
            self.last_change_nanos
                .store(elapsed_nanos, Ordering::Relaxed);
            return true;
        }
        elapsed_nanos.saturating_sub(self.last_change_nanos.load(Ordering::Relaxed))
            < self.window_nanos
    }
}

/// Measure Thread を起動し、JoinHandle を返す。
///
/// # 引数
/// - `consumer`    : Audio Thread からサンプルを受け取る rtrb Consumer（所有権を移動）
/// - `sample_rate` : 現在のサンプルレート（Hz）
/// - `result`      : IO Thread / GUI と共有する計測結果（Arc<Mutex<MeasureResult>>）
/// - `signal_state`: Audio Thread が書き込む信号状態
/// - `shutdown`    : `true` に設定されたらループを終了するフラグ
/// - `evaluator`   : B-118 単一鮮度評価器（`LivenessEvaluator`）。measure loop は毎ループ
///   `evaluator.is_live()` を読み、not live（process() が live ウィンドウ=3s 変化なし）なら
///   signal_state を Inactive に上書きする consumer に徹する（独自 stale 計数は撤去）。同一
///   評価器を editor pair lock / FFI getter / watchdog も読む（単一源）。
///
/// # 3層隔離保証
/// このスレッドが panic しても Audio Thread は継続する。
/// panic → JoinHandle::is_finished() で検出 → T-8 で自動再起動する。
///
/// # B-043 (LUFS-I / LRA / PLR)
/// - `record_sm`       : Record mode 中の SS-8 reset 抑止判定に使う（案I-a）。
///   Watch→Record 遷移時は engine.reset() を明示実行してセッション開始時点で
///   ebur128 内部状態をクリアする。Record 中は SS-8 reset をスキップすることで
///   transport 停止/再開を跨いだ LUFS-I / LRA の通算性を確保する。
/// - `session_summary` : Record 中の各ループで `engine.finalize()` の最新値を
///   注入する共有スロット。IO Thread が Record→Watch 遷移時に読み出して
///   `PluginDataWriter::set_session_aggregates()` 経由で JSON に焼き込む。
#[allow(clippy::too_many_arguments)]
pub fn spawn_measure_thread(
    mut consumer: rtrb::Consumer<f32>,
    sample_rate: u32,
    n_channels: usize,
    result: Arc<Mutex<MeasureResult>>,
    watch_playback_pass_id: Arc<AtomicU64>,
    watch_playback_pass_cutover_samples: Arc<AtomicU64>,
    watch_ring_cursor_epoch: Arc<AtomicU64>,
    watch_ring_cursor_pass_id: Arc<AtomicU64>,
    watch_ring_cursor_samples: Arc<AtomicU64>,
    signal_state: Arc<AtomicU8>,
    shutdown: Arc<AtomicBool>,
    evaluator: Arc<LivenessEvaluator>,
    record_sm: Arc<RecordStateMachine>,
    session_summary: Arc<Mutex<Option<SessionSummary>>>,
    record_trace_queue: RecordTraceQueue,
    record_take_tracker: Arc<RecordTakeTracker>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        let n_channels = match n_channels {
            1 | 2 => n_channels,
            _ => N_CHANNELS,
        };

        //  v2: 内部処理は 48 kHz 固定。入力 SR が異なる場合のみ
        // ResamplerTo48k で変換して engine / phase_d に渡す。
        let mut engine = match MeasureEngine::new(ENGINE_SR, n_channels) {
            Ok(e) => e,
            Err(e) => {
                log::error!("[MeasureThread] MeasureEngine::new failed: {}", e);
                return;
            }
        };
        let mut record_trace_engine = match MeasureEngine::new(sample_rate, n_channels) {
            Ok(engine) => engine,
            Err(error) => {
                log::error!(
                    "[MeasureThread] native Record TRACE engine construction failed: {}",
                    error
                );
                return;
            }
        };
        let mut record_summary_engine = match MeasureEngine::new(sample_rate, n_channels) {
            Ok(engine) => engine,
            Err(error) => {
                log::error!(
                    "[MeasureThread] native Record summary engine construction failed: {}",
                    error
                );
                return;
            }
        };

        // 入力 SR が 48 kHz の場合はバイパス（ゼロオーバーヘッド経路を維持）。
        // 異なる場合のみ rubato Fft リサンプラを構築する。失敗時は Measure Thread のみ終了。
        let mut resampler: Option<ResamplerTo48k> = if sample_rate != ENGINE_SR {
            match ResamplerTo48k::new(sample_rate, n_channels) {
                Ok(r) => {
                    log::info!(
                        "[MeasureThread] Resampler {}->{} Hz constructed (channels={})",
                        sample_rate,
                        ENGINE_SR,
                        n_channels
                    );
                    Some(r)
                }
                Err(e) => {
                    log::error!(
                        "[MeasureThread] ResamplerTo48k::new({}) failed: {:?}",
                        sample_rate,
                        e
                    );
                    return;
                }
            }
        } else {
            None
        };

        //  streaming processor（ v2: 全 SR 対応のため常に Some 相当）。
        let mut phase_d = PhaseDStream::new(FieldType::Free);
        // Phase D は mono stream 入力。mono はそのまま、stereo は (L+R)/2 に落とす。
        let mut phase_d_mono_buf: Vec<f64> = Vec::with_capacity(ENGINE_SR as usize);
        let mut phase_d_slot_pending: Vec<f64> = Vec::with_capacity(ENGINE_SR as usize / 5);
        //  最新結果（ループをまたいで保持。engine が結果を返した時にマージ）
        let mut latest_pd: Option<crate::phase_d::stream::PhaseDResult> = None;
        let mut record_core_pending: VecDeque<PendingRecordCore> = VecDeque::new();
        let mut record_phase_pending = VecDeque::new();
        // Record TRACE windows are phase-locked to the host's absolute native-sample grid.
        // PRE and POST may begin delivering Record buffers on different callbacks (Studio One
        // PDC is a real example), but a 100 ms boundary at sample N must mean the same interval
        // in both instances. The cursor is Measure-thread-only; Audio Thread still publishes
        // samples and clocks through the existing lock-free ring/atomics.
        let mut record_grid_cursor: Option<i64> = None;
        let mut record_next_grid_end: Option<i64> = None;
        let mut record_alignment_silence =
            Vec::with_capacity((sample_rate as usize / 10).saturating_mul(n_channels));

        // f32 → f64 変換バッファ（ループをまたいで再利用。再アロケーションを避ける）
        let mut chunk_f64: Vec<f64> = Vec::with_capacity(sample_rate as usize);
        // リサンプル後 48kHz interleaved バッファ（resampler が Some のときのみ使う）
        let mut resampled_buf: Vec<f64> = Vec::with_capacity(ENGINE_SR as usize * n_channels / 4);
        // DAW/WAV 側 native sample frame clock。Record の duration_samples はこれを正本にする。
        let mut native_frames_total = 0_u64;
        let mut last_capture_position_end: Option<i64> = None;

        // 前回ループの SignalState を保持し、非Active→Active 遷移を検出する（SS-8）。
        let mut prev_active = false;
        let mut measure_sequence = 0_u64;
        let mut playback_pass_id = watch_playback_pass_id.load(Ordering::Acquire);
        // A spawned Measure Thread always receives a fresh Consumer. Samples already queued in
        // that Consumer are unread, even if the Audio/FFI side has advanced the cursor first.
        let mut consumed_samples = 0_u64;

        // B-043: Record mode 遷移を検出し、Watch→Record 開始時に engine をリセットする。
        // Record 中の SS-8 reset 抑止と組み合わせて、LUFS-I / LRA のセッション通算性を確保する。
        let mut prev_recording = false;
        let mut record_origin_frames = engine.total_frames();
        let mut record_origin_native_frames = native_frames_total;
        let mut record_trace_frame_offset_48k = 0_u64;
        let mut record_trace_native_frame_offset = 0_u64;
        let mut next_record_trace_ms = 0_u64;
        let mut next_record_psb_ms = 0_u64;
        let mut record_pre_roll = RecordTracePreRoll::default();

        // heartbeat stall detection: process() が停止したことを検出する。
        // Studio One 等、バイパス時に process() を呼ばなくなる DAW に対応。
        // B-118: 鮮度は単一評価器 `evaluator.is_live()` が判定する。measure loop は独自 stale
        // 計数を持たず、評価器の consumer として signal_state→Inactive 上書きを適用する。
        // `prev_live` はログのエッジ（stale 突入 / resumed）検出専用。生成直後は live。
        let mut prev_live = true;

        log::info!("[MeasureThread] started (sample_rate={})", sample_rate);

        loop {
            // シャットダウン確認（initialize() が呼ばれたか、プラグインが Drop されたか）
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            // ── B-043: Record mode 遷移ハンドリング ──────────────────
            // Watch→Record: engine を明示リセットして新セッション開始。
            //   - 前 Watch 期間で accumulate された LUFS-I / LRA / TP の running max
            //     を捨て、セッション通算値を 0 から積み直す。
            // Record→Watch: 共有 session_summary は IO Thread が読んだ後にクリア
            //   される（次の Watch→Record でここで上書き None する）。
            let is_recording = record_sm.is_recording();
            if !prev_recording && is_recording {
                engine.reset();
                record_trace_engine.reset();
                record_summary_engine.reset();
                phase_d.reset();
                if let Some(rs) = &mut resampler {
                    rs.reset();
                }
                latest_pd = None;
                phase_d_slot_pending.clear();
                record_core_pending.clear();
                record_phase_pending.clear();
                record_grid_cursor = None;
                record_next_grid_end = None;
                let drained_start_frames = drain_consumer_before_record_start_position(
                    &mut consumer,
                    &mut consumed_samples,
                    &mut native_frames_total,
                    &record_take_tracker,
                    record_sm.record_started_at_position_samples(),
                    n_channels,
                );
                if drained_start_frames > 0 {
                    log::info!(
                        "[MeasureThread] discarded {} queued frames before Record start sample barrier",
                        drained_start_frames
                    );
                }
                record_origin_frames =
                    native_frames_to_48k(record_trace_engine.total_frames(), sample_rate);
                record_origin_native_frames = native_frames_total;
                next_record_trace_ms = 0;
                next_record_psb_ms = 0;
                clear_record_trace_queue(&record_trace_queue);
                let seed_offsets = seed_record_trace_from_pre_roll(
                    &record_trace_queue,
                    &record_pre_roll,
                    record_sm.generation(),
                    record_sm.record_started_at_position_samples(),
                    sample_rate,
                    &mut next_record_trace_ms,
                    &mut next_record_psb_ms,
                );
                if seed_offsets.seeded {
                    let transition_native_offset =
                        native_frames_total.saturating_sub(seed_offsets.origin_native_frames);
                    record_trace_native_frame_offset = transition_native_offset;
                    record_trace_frame_offset_48k =
                        native_frames_to_48k(transition_native_offset, sample_rate);
                } else {
                    record_trace_frame_offset_48k = 0;
                    record_trace_native_frame_offset = 0;
                }
                record_pre_roll.clear();
                if let Ok(mut g) = session_summary.lock() {
                    *g = None;
                }
                record_sm.mark_measure_ready(record_sm.generation());
                log::info!("[MeasureThread] engine reset on Watch→Record transition");
            } else if prev_recording && !is_recording {
                // ── B-132 (G-115-382) P1: Record→Watch エッジ drain-completion barrier ──
                // is_recording gate（下方 line ~361）が閉じる前の最後の 1 pass で、ring 残量を
                // tight-drain → 最終 finalize → session_summary 書込（この pass のみ gate を跨ぐ）。
                // 「停止前に drain 済み」の clean 録音では ring 空 → finalize は同一サンプル集合を
                // 再抽出するだけ（parity abs=0 維持）。完了後に seal を 1 前進させ、IO の bake arm が
                // post-drain 確定スナップショットを読む handshake を成立させる（共通B）。
                let drained = drain_ring_into_session(
                    DrainRingSession {
                        consumer: &mut consumer,
                        resampler: &mut resampler,
                        trace_engine: &mut record_trace_engine,
                        summary_engine: &mut record_summary_engine,
                        session_summary: &session_summary,
                        chunk_f64: &mut chunk_f64,
                        resampled_buf: &mut resampled_buf,
                        phase_d: &mut phase_d,
                        phase_d_mono_buf: &mut phase_d_mono_buf,
                        phase_d_slot_pending: &mut phase_d_slot_pending,
                        latest_pd: &mut latest_pd,
                        record_core_pending: &mut record_core_pending,
                        record_phase_pending: &mut record_phase_pending,
                        sample_rate,
                        n_channels,
                        native_frames_total: &mut native_frames_total,
                        consumed_samples: &mut consumed_samples,
                        record_take_tracker: &record_take_tracker,
                        last_capture_position_end: &mut last_capture_position_end,
                        finalize_capture: true,
                    },
                    Some(RecordTraceDrain {
                        queue: &record_trace_queue,
                        timeline: RecordTraceTimeline {
                            generation: record_sm.generation(),
                            origin_frames_48k: record_origin_frames,
                            origin_native_frames: record_origin_native_frames,
                            offset_frames_48k: record_trace_frame_offset_48k,
                            offset_native_frames: record_trace_native_frame_offset,
                            origin_position_samples: record_sm.record_started_at_position_samples(),
                        },
                        cursor: RecordTraceCursor {
                            next_trace_ms: &mut next_record_trace_ms,
                            next_psb_ms: &mut next_record_psb_ms,
                        },
                    }),
                );
                let completed_trace = push_record_trace_to_record_take_clock(
                    &record_trace_queue,
                    record_sm.generation(),
                    &record_take_tracker,
                    &mut RecordTraceCursor {
                        next_trace_ms: &mut next_record_trace_ms,
                        next_psb_ms: &mut next_record_psb_ms,
                    },
                    sample_rate,
                );
                if drained {
                    record_sm.bump_seal();
                    consumed_samples = watch_ring_cursor_samples_for_pass(
                        &watch_ring_cursor_epoch,
                        &watch_ring_cursor_pass_id,
                        &watch_ring_cursor_samples,
                        playback_pass_id,
                    )
                    .unwrap_or(consumed_samples);
                    log::info!(
                        "[MeasureThread] Record→Watch tight-drain complete (seal bumped, clock_trace_completed={})",
                        completed_trace
                    );
                } else {
                    log::warn!(
                        "[MeasureThread] Record→Watch drain incomplete — seal NOT bumped (IO bake → integrity_degraded)"
                    );
                }
            }
            prev_recording = is_recording;

            // ── B-118: 単一鮮度評価器による stall detection（G-115-245: 3s）──────
            // measure loop は評価器の consumer。process() が live ウィンドウ(3s)変化しなければ
            // signal_state を Inactive に上書きする（process() 再開時に Audio Thread が即座に
            // 正しい state を書き戻す）。評価器は heartbeat を内部観測する（独自計数なし）。
            let live = evaluator.is_live();
            if !live {
                if prev_live {
                    log::info!(
                        "[MeasureThread] heartbeat stale (>{}s) — process() stopped, overriding to Inactive",
                        live_window().as_secs()
                    );
                }
                store_signal_state(&signal_state, SignalState::Inactive);
            } else if !prev_live {
                log::info!("[MeasureThread] heartbeat resumed — process() restarted");
            }
            prev_live = live;

            // ── SS-4: SignalState チェック ──────────────────────────
            let state = load_signal_state(&signal_state);
            let observed_playback_pass_id = watch_playback_pass_id.load(Ordering::Acquire);
            if observed_playback_pass_id != 0 && observed_playback_pass_id != playback_pass_id {
                playback_pass_id = observed_playback_pass_id;
                if is_recording {
                    log::info!(
                        "[MeasureThread] Watch playback pass reset suppressed in Record mode"
                    );
                } else {
                    engine.reset();
                    record_pre_roll.clear();
                    let cutover = watch_playback_pass_cutover_samples.load(Ordering::Acquire);
                    let stale =
                        drain_consumer_until_sample(&mut consumer, &mut consumed_samples, cutover);
                    if stale > 0 {
                        log::info!(
                            "[MeasureThread] discarded {} queued samples before Watch playback pass cutover {}",
                            stale,
                            cutover
                        );
                    }
                    phase_d.reset();
                    if let Some(rs) = &mut resampler {
                        rs.reset();
                    }
                    latest_pd = None;
                    phase_d_slot_pending.clear();
                    record_core_pending.clear();
                    record_phase_pending.clear();
                    record_grid_cursor = None;
                    record_next_grid_end = None;
                    consumed_samples = 0;
                    prev_active = state == SignalState::Active;
                    if state == SignalState::Active {
                        if let Ok(mut guard) = result.lock() {
                            let mut cleared = MeasureResult::default();
                            stamp_shared_measure_result(
                                &mut cleared,
                                &mut measure_sequence,
                                playback_pass_id,
                            );
                            *guard = cleared;
                        }
                    }
                }
                log::info!(
                    "[MeasureThread] Watch playback pass advanced (id={}, is_recording={})",
                    playback_pass_id,
                    is_recording
                );
                if state == SignalState::Active && !is_recording {
                    thread::sleep(idle_sleep_for_record_state(is_recording));
                    continue;
                }
            }
            if state != SignalState::Active {
                prev_active = false;
                // ── B-132 (G-115-382) 共通A: finalize-before-discard ──────────────
                // 録音継続中（is_recording）に transport stop / DAW stall で Inactive に落ちた場合、
                // ring 残量を破棄する前に finalize して session_summary に算入する。これで
                // (ii)① 自然 Inactive discard と (ii)② heartbeat-stale override（line ~243 で
                // state=Inactive に畳んで本ブロックに合流）の両経路をカバーする。純 Watch（非録音）の
                // silent 破棄は is_recording gate で従来通り維持。seal は進めない（セッション継続中 /
                // seal は Record→Watch エッジのみ）。`slots()>0` が冪等 latch（drain 後 ring 空 →
                // 後続ループは skip / 長時間 stall でも再 finalize しない）。
                if is_recording && consumer.slots() > 0 {
                    let _ = drain_ring_into_session(
                        DrainRingSession {
                            consumer: &mut consumer,
                            resampler: &mut resampler,
                            trace_engine: &mut record_trace_engine,
                            summary_engine: &mut record_summary_engine,
                            session_summary: &session_summary,
                            chunk_f64: &mut chunk_f64,
                            resampled_buf: &mut resampled_buf,
                            phase_d: &mut phase_d,
                            phase_d_mono_buf: &mut phase_d_mono_buf,
                            phase_d_slot_pending: &mut phase_d_slot_pending,
                            latest_pd: &mut latest_pd,
                            record_core_pending: &mut record_core_pending,
                            record_phase_pending: &mut record_phase_pending,
                            sample_rate,
                            n_channels,
                            native_frames_total: &mut native_frames_total,
                            consumed_samples: &mut consumed_samples,
                            record_take_tracker: &record_take_tracker,
                            last_capture_position_end: &mut last_capture_position_end,
                            finalize_capture: false,
                        },
                        Some(RecordTraceDrain {
                            queue: &record_trace_queue,
                            timeline: RecordTraceTimeline {
                                generation: record_sm.generation(),
                                origin_frames_48k: record_origin_frames,
                                origin_native_frames: record_origin_native_frames,
                                offset_frames_48k: record_trace_frame_offset_48k,
                                offset_native_frames: record_trace_native_frame_offset,
                                origin_position_samples: record_sm
                                    .record_started_at_position_samples(),
                            },
                            cursor: RecordTraceCursor {
                                next_trace_ms: &mut next_record_trace_ms,
                                next_psb_ms: &mut next_record_psb_ms,
                            },
                        }),
                    );
                    consumed_samples = watch_ring_cursor_samples_for_pass(
                        &watch_ring_cursor_epoch,
                        &watch_ring_cursor_pass_id,
                        &watch_ring_cursor_samples,
                        playback_pass_id,
                    )
                    .unwrap_or(consumed_samples);
                }
                // Bypassed / Inactive → compute() スキップ。
                // リングバッファに残っているサンプルは破棄する（共通A で drain 済なら no-op）。
                // （Active に戻ったとき古いデータで計測しないため）。
                drain_consumer_all(&mut consumer, &mut consumed_samples);
                // 計測結果をクリア（GUI が即座に `---` 表示できるようにする）
                match result.lock() {
                    Ok(mut guard) => {
                        let mut cleared = MeasureResult::default();
                        stamp_shared_measure_result(
                            &mut cleared,
                            &mut measure_sequence,
                            playback_pass_id,
                        );
                        *guard = cleared;
                    }
                    Err(e) => log::warn!("[MeasureThread] result Mutex poisoned: {}", e),
                }
                thread::sleep(idle_sleep_for_record_state(is_recording));
                continue;
            }

            // ── SS-8: 非Active→Active 遷移時にエンジンリセット ──────
            // 前セッションの ebur128 FIR 遅延ライン / tp_window / window_400ms /
            // / リサンプラ FFT overlap / pending 入力 をすべてクリアして、新セッション
            // 最初のチャンクが汚染されるのを防ぐ。
            //
            // B-043 (案I-a): Record mode 中は engine.reset() をスキップする。
            // transport 停止/再開を跨いだ LUFS-I / LRA / TP running max のセッション
            // 通算性を確保するため。Watch 中は従来通り reset する。
            // phase_d / resampler / latest_pd は Record 中でも reset してよい
            // （セッション集計に影響しないため）。
            if !prev_active {
                if !is_recording {
                    engine.reset();
                    record_trace_engine.reset();
                    record_summary_engine.reset();
                    record_pre_roll.clear();
                } else {
                    log::info!("[MeasureThread] SS-8 reset suppressed in Record mode (B-043)");
                }
                phase_d.reset();
                if let Some(rs) = &mut resampler {
                    rs.reset();
                }
                latest_pd = None;
                phase_d_slot_pending.clear();
                record_core_pending.clear();
                record_phase_pending.clear();
                prev_active = true;
                if let Ok(mut guard) = result.lock() {
                    let mut cleared = MeasureResult::default();
                    stamp_shared_measure_result(
                        &mut cleared,
                        &mut measure_sequence,
                        playback_pass_id,
                    );
                    *guard = cleared;
                }
                log::info!(
                    "[MeasureThread] Active transition handled (is_recording={})",
                    is_recording
                );
            }

            // ── Active: リングバッファから全サンプルを取得して計測 ────
            let available = if is_recording {
                consumer.slots()
            } else {
                let pushed_for_pass = watch_ring_cursor_samples_for_pass(
                    &watch_ring_cursor_epoch,
                    &watch_ring_cursor_pass_id,
                    &watch_ring_cursor_samples,
                    playback_pass_id,
                )
                .unwrap_or(consumed_samples);
                consumer.slots().min(samples_until_cursor_limit(
                    consumed_samples,
                    pushed_for_pass,
                ))
            };
            let capture_plan = capture_chunk_plan(
                &record_take_tracker,
                native_frames_total,
                available,
                n_channels,
                sample_rate,
            );
            let available = capture_plan.sample_count;
            if available > 0 {
                if capture_plan
                    .position_start_samples
                    .zip(last_capture_position_end)
                    .is_some_and(|(start, previous_end)| start != previous_end)
                {
                    engine.reset();
                    record_trace_engine.reset();
                    record_summary_engine.reset();
                    phase_d.reset();
                    if let Some(rs) = &mut resampler {
                        rs.reset();
                    }
                    latest_pd = None;
                    phase_d_slot_pending.clear();
                    record_core_pending.clear();
                    record_phase_pending.clear();
                    record_grid_cursor = None;
                    record_next_grid_end = None;
                    if is_recording {
                        record_origin_frames =
                            native_frames_to_48k(record_trace_engine.total_frames(), sample_rate);
                        record_origin_native_frames = native_frames_total;
                    }
                    log::info!(
                        "[MeasureThread] host sample discontinuity: measurement state reset at {:?}",
                        capture_plan.position_start_samples
                    );
                }
                chunk_f64.clear();
                for _ in 0..available {
                    match consumer.pop() {
                        Ok(s) => {
                            consumed_samples = consumed_samples.saturating_add(1);
                            chunk_f64.push(s as f64); // f32 → f64
                        }
                        Err(_) => break, // Consumer が空になった
                    }
                }
                let chunk_native_frames = (chunk_f64.len() / n_channels) as u64;
                let native_frames_before = native_frames_total;
                native_frames_total = native_frames_total.saturating_add(chunk_native_frames);
                let native_frames_after = native_frames_total;
                last_capture_position_end = capture_plan.position_end_samples;

                //  v2: 入力 SR が 48 kHz でない場合のみリサンプリング。
                // resampler 経由では 48 kHz interleaved f64 が `resampled_buf` に追記される。
                // 端数フレームは ResamplerTo48k 内部の pending に保持され次回呼出で消費。
                let chunk_48k: &[f64] = if let Some(rs) = resampler.as_mut() {
                    resampled_buf.clear();
                    if let Err(e) = rs.process(&chunk_f64, &mut resampled_buf) {
                        log::warn!(
                            "[MeasureThread] Resampler error ({}->48000): {:?}, dropping chunk",
                            sample_rate,
                            e
                        );
                        thread::sleep(idle_sleep_for_record_state(is_recording));
                        continue;
                    }
                    &resampled_buf
                } else {
                    &chunk_f64
                };

                // Establish the Record metric phase before feeding either the native EBU engine
                // or Phase D. Padding represents only the unavailable prefix of the absolute
                // 100 ms slot; it is never written to the audio buffer and never reaches the DAW.
                // The independent TRACE engine is causally primed with silence, so its first
                // published 400 ms window ends on the same absolute grid in every instance.
                if is_recording {
                    if let Some(position_start) = capture_plan.position_start_samples {
                        if record_grid_cursor != Some(position_start) {
                            record_trace_engine.reset();
                            phase_d.reset();
                            latest_pd = None;
                            phase_d_slot_pending.clear();
                            record_core_pending.clear();
                            record_phase_pending.clear();
                            let (phase_frames, next_grid_end) =
                                record_grid_alignment(position_start, sample_rate);
                            // TRACE has a causal 400 ms momentary window. Prime only the TRACE
                            // engine with the unavailable 300 ms prefix plus the absolute-grid
                            // phase. The independent summary engine receives real capture samples
                            // only, so LUFS-I/LRA remain unmodified.
                            let warmup_frames = (sample_rate as usize * 3) / 10;
                            let alignment_frames = warmup_frames.saturating_add(phase_frames);
                            record_alignment_silence.clear();
                            record_alignment_silence
                                .resize(alignment_frames.saturating_mul(n_channels), 0.0);
                            let _ = record_trace_engine.push(&record_alignment_silence);
                            let phase_frames_48k =
                                native_frames_to_48k(alignment_frames as u64, sample_rate) as usize;
                            record_alignment_silence.clear();
                            record_alignment_silence
                                .resize(phase_frames_48k.saturating_mul(n_channels), 0.0);
                            feed_phase_d_slots(
                                &record_alignment_silence,
                                n_channels,
                                &mut phase_d_mono_buf,
                                &mut phase_d_slot_pending,
                                &mut phase_d,
                                &mut latest_pd,
                                &mut record_phase_pending,
                                false,
                            );
                            record_next_grid_end = Some(next_grid_end);
                            record_grid_cursor = Some(position_start);
                        }
                    } else {
                        record_grid_cursor = None;
                        record_next_grid_end = None;
                    }
                }

                // Phase D: mono is identity; stereo is averaged to mono. Do not duplicate mono
                // into two channels, because that would also bias EBU loudness by +3 dB.
                feed_phase_d_slots(
                    chunk_48k,
                    n_channels,
                    &mut phase_d_mono_buf,
                    &mut phase_d_slot_pending,
                    &mut phase_d,
                    &mut latest_pd,
                    &mut record_phase_pending,
                    is_recording,
                );

                // 100ms チャンク単位で計測し、揃ったら結果を共有領域に書き込む。
                // Offline bounce では 1 drain 内で複数結果が出るため、observer で全件を
                // audio-time TRACE queue に渡す。
                let mut last_result = None;
                let latest_pd_snapshot = latest_pd.clone();
                let observed_at_ms = now_epoch_ms();
                let frames_48k_before = engine.total_frames();
                let chunk_48k_frames = (chunk_48k.len() / n_channels) as u64;
                let _ =
                    engine.push_observed(chunk_48k, |frames_48k, base_result, observed_chunk| {
                        let mut new_result = base_result.clone();
                        merge_phase_d_fields(&mut new_result, latest_pd_snapshot.as_ref());
                        stamp_shared_measure_result(
                            &mut new_result,
                            &mut measure_sequence,
                            playback_pass_id,
                        );
                        let observed_native_frames = estimate_observed_native_frames(
                            frames_48k,
                            frames_48k_before,
                            native_frames_before,
                            chunk_native_frames,
                            chunk_48k_frames,
                        )
                        .unwrap_or(native_frames_after);
                        let observed_silence_floor =
                            samples_are_trace_silence_floor(observed_chunk);
                        if !is_recording {
                            let clock_point = record_take_tracker
                                .clock_point_for_captured_frame(observed_native_frames);
                            record_pre_roll.push_with_clock(
                                observed_at_ms,
                                clock_point.map(|point| point.position_samples),
                                clock_point
                                    .map_or(CaptureClockSource::Unknown, |point| point.source),
                                record_take_tracker.presentation_latency(),
                                frames_48k,
                                observed_native_frames,
                                observed_silence_floor,
                                &new_result,
                            );
                        }
                        last_result = Some(new_result);
                    });
                if let Some(new_result) = last_result {
                    match result.lock() {
                        Ok(mut guard) => *guard = new_result,
                        Err(e) => {
                            log::warn!("[MeasureThread] result Mutex poisoned: {}", e);
                        }
                    }
                }

                if is_recording {
                    let _ = record_trace_engine.push_observed(
                        &chunk_f64,
                        |native_engine_frames, base_result, observed_chunk| {
                            let grid_position = record_next_grid_end;
                            if let Some(position) = grid_position {
                                record_next_grid_end =
                                    Some(position.saturating_add((sample_rate as i64 / 10).max(1)));
                            }
                            let observed_native_frames = native_frames_after;
                            let observed_silence_floor =
                                samples_are_trace_silence_floor(observed_chunk);
                            let clock_point = grid_position
                                .zip(capture_plan.clock_source())
                                .map(|(position_samples, source)| CaptureClockPoint {
                                    position_samples,
                                    source,
                                })
                                .or_else(|| {
                                    record_take_tracker
                                        .clock_point_for_captured_frame(observed_native_frames)
                                });
                            record_core_pending.push_back(PendingRecordCore {
                                frames_48k: native_frames_to_48k(native_engine_frames, sample_rate),
                                native_frames: observed_native_frames,
                                position_samples: clock_point.map(|point| point.position_samples),
                                clock_source: clock_point
                                    .map_or(CaptureClockSource::Unknown, |point| point.source),
                                presentation_latency: record_take_tracker.presentation_latency(),
                                observed_silence_floor,
                                result: base_result.clone(),
                            });
                        },
                    );
                    let _ = record_summary_engine.push(&chunk_f64);
                    if let Some(position_start) = capture_plan.position_start_samples {
                        record_grid_cursor =
                            Some(position_start.saturating_add(chunk_native_frames as i64));
                    }
                    let mut trace = RecordTraceDrain {
                        queue: &record_trace_queue,
                        timeline: RecordTraceTimeline {
                            generation: record_sm.generation(),
                            origin_frames_48k: record_origin_frames,
                            origin_native_frames: record_origin_native_frames,
                            offset_frames_48k: record_trace_frame_offset_48k,
                            offset_native_frames: record_trace_native_frame_offset,
                            origin_position_samples: record_sm.record_started_at_position_samples(),
                        },
                        cursor: RecordTraceCursor {
                            next_trace_ms: &mut next_record_trace_ms,
                            next_psb_ms: &mut next_record_psb_ms,
                        },
                    };
                    join_record_trace_slots(
                        &mut trace,
                        sample_rate,
                        &mut record_core_pending,
                        &mut record_phase_pending,
                    );
                }

                // B-043: Record 中は session_summary に毎ループの最新 finalize() を反映。
                // IO Thread が Record→Watch 遷移時に直近の値を読み出して JSON に焼く。
                // engine.push() 後に呼ぶことで最新チャンク反映後の値を取れる。
                if is_recording {
                    let summary = record_summary_engine.finalize();
                    if let Ok(mut g) = session_summary.lock() {
                        *g = Some(summary);
                    }
                }
            }

            if consumer.slots() > 0 {
                thread::yield_now();
            } else {
                thread::sleep(idle_sleep_for_record_state(is_recording));
            }
        }

        log::info!("[MeasureThread] terminated");
    })
}

fn append_phase_d_mono(input_interleaved: &[f64], n_channels: usize, out: &mut Vec<f64>) {
    match n_channels {
        1 => out.extend_from_slice(input_interleaved),
        2 => {
            for frame in input_interleaved.chunks_exact(2) {
                out.push((frame[0] + frame[1]) * 0.5);
            }
        }
        _ => {
            for frame in input_interleaved.chunks_exact(n_channels) {
                out.push(frame.iter().sum::<f64>() / n_channels as f64);
            }
        }
    }
}

fn merge_phase_d_fields(
    result: &mut MeasureResult,
    pd: Option<&crate::phase_d::stream::PhaseDResult>,
) {
    if let Some(pd_r) = pd {
        result.n_prime_total = Some(pd_r.loudness);
        result.sharpness = Some(pd_r.sharpness);
        result.psb_summary = Some(compute_psb_summary(
            &pd_r.psb,
            &pd_r.psb_bark21_24,
            pd_r.psb_high_ext_15_5k_20k,
        ));
        result.n_prime = Some(pd_r.n_prime);
        result.psb_bark = Some(pd_r.psb);
    }
}

fn next_nonzero_counter(counter: &mut u64) -> u64 {
    *counter = counter.wrapping_add(1);
    if *counter == 0 {
        *counter = 1;
    }
    *counter
}

fn drain_consumer_all(consumer: &mut rtrb::Consumer<f32>, consumed_samples: &mut u64) -> usize {
    let available = consumer.slots();
    let mut drained = 0_usize;
    for _ in 0..available {
        if consumer.pop().is_err() {
            break;
        }
        *consumed_samples = (*consumed_samples).saturating_add(1);
        drained += 1;
    }
    drained
}

fn drain_consumer_until_sample(
    consumer: &mut rtrb::Consumer<f32>,
    consumed_samples: &mut u64,
    cutover_samples: u64,
) -> usize {
    let mut drained = 0_usize;
    while *consumed_samples < cutover_samples {
        if consumer.pop().is_err() {
            break;
        }
        *consumed_samples = (*consumed_samples).saturating_add(1);
        drained += 1;
    }
    drained
}

fn drain_consumer_before_record_start_position(
    consumer: &mut rtrb::Consumer<f32>,
    consumed_samples: &mut u64,
    native_frames_total: &mut u64,
    record_take_tracker: &RecordTakeTracker,
    record_start_position_samples: Option<i64>,
    n_channels: usize,
) -> usize {
    if n_channels == 0 {
        return 0;
    }
    let Some(record_start_position_samples) = record_start_position_samples else {
        let drained_samples = drain_consumer_all(consumer, consumed_samples);
        return drained_samples / n_channels;
    };

    let mut drained_frames = 0_usize;
    while consumer.slots() >= n_channels {
        let next_frame = native_frames_total.saturating_add(1);
        let Some(position_samples) =
            record_take_tracker.position_samples_for_captured_frame(next_frame)
        else {
            let drained_samples = drain_consumer_all(consumer, consumed_samples);
            return drained_frames.saturating_add(drained_samples / n_channels);
        };
        if position_samples >= record_start_position_samples {
            break;
        }
        for _ in 0..n_channels {
            if consumer.pop().is_err() {
                return drained_frames;
            }
            *consumed_samples = (*consumed_samples).saturating_add(1);
        }
        *native_frames_total = next_frame;
        drained_frames = drained_frames.saturating_add(1);
    }

    drained_frames
}

fn samples_until_cursor_limit(consumed_samples: u64, cursor_samples: u64) -> usize {
    cursor_samples
        .saturating_sub(consumed_samples)
        .min(usize::MAX as u64) as usize
}

fn stamp_shared_measure_result(
    result: &mut MeasureResult,
    sequence: &mut u64,
    playback_pass_id: u64,
) {
    result.measure_sequence = next_nonzero_counter(sequence);
    result.playback_pass_id = playback_pass_id;
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
struct RecordTraceSeedOffsets {
    frames_48k: u64,
    native_frames: u64,
    origin_frames_48k: u64,
    origin_native_frames: u64,
    seeded: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct RecordTraceTimeline {
    generation: u64,
    origin_frames_48k: u64,
    origin_native_frames: u64,
    offset_frames_48k: u64,
    offset_native_frames: u64,
    origin_position_samples: Option<i64>,
}

struct RecordTraceCursor<'a> {
    next_trace_ms: &'a mut u64,
    next_psb_ms: &'a mut u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct RecordTraceObserved {
    frames_48k: u64,
    native_frames: Option<u64>,
    position_samples: Option<i64>,
    clock_source: CaptureClockSource,
    presentation_latency: PresentationLatencySamples,
    observed_silence_floor: bool,
}

#[derive(Debug, Clone)]
struct PendingRecordCore {
    frames_48k: u64,
    native_frames: u64,
    position_samples: Option<i64>,
    clock_source: CaptureClockSource,
    presentation_latency: PresentationLatencySamples,
    observed_silence_floor: bool,
    result: MeasureResult,
}

#[allow(clippy::too_many_arguments)]
fn feed_phase_d_slots(
    input_48k: &[f64],
    n_channels: usize,
    mono_scratch: &mut Vec<f64>,
    slot_pending: &mut Vec<f64>,
    phase_d: &mut PhaseDStream,
    latest_pd: &mut Option<crate::phase_d::stream::PhaseDResult>,
    record_phase_pending: &mut VecDeque<crate::phase_d::stream::PhaseDResult>,
    is_recording: bool,
) {
    mono_scratch.clear();
    append_phase_d_mono(input_48k, n_channels, mono_scratch);
    slot_pending.extend_from_slice(mono_scratch);
    while slot_pending.len() >= ENGINE_SR as usize / 10 {
        let slot: Vec<f64> = slot_pending.drain(..ENGINE_SR as usize / 10).collect();
        if let Some(result) = phase_d.push(&slot).last().cloned() {
            *latest_pd = Some(result.clone());
            if is_recording {
                record_phase_pending.push_back(result);
            }
        }
    }
}

fn join_record_trace_slots(
    trace: &mut RecordTraceDrain<'_>,
    sample_rate: u32,
    core_pending: &mut VecDeque<PendingRecordCore>,
    phase_pending: &mut VecDeque<crate::phase_d::stream::PhaseDResult>,
) {
    while !core_pending.is_empty() && !phase_pending.is_empty() {
        let core = core_pending.pop_front().expect("checked non-empty");
        let phase = phase_pending.pop_front().expect("checked non-empty");
        let mut result = core.result;
        merge_phase_d_fields(&mut result, Some(&phase));
        let _ = maybe_push_record_trace(
            trace.queue,
            trace.timeline,
            &mut trace.cursor,
            sample_rate,
            RecordTraceObserved {
                frames_48k: core.frames_48k,
                native_frames: Some(core.native_frames),
                position_samples: core.position_samples,
                clock_source: core.clock_source,
                presentation_latency: core.presentation_latency,
                observed_silence_floor: core.observed_silence_floor,
            },
            &result,
        );
    }
}

fn seed_record_trace_from_pre_roll(
    queue: &RecordTraceQueue,
    pre_roll: &RecordTracePreRoll,
    generation: u64,
    requested_started_at_position_samples: Option<i64>,
    sample_rate: u32,
    next_trace_ms: &mut u64,
    next_psb_ms: &mut u64,
) -> RecordTraceSeedOffsets {
    let Some(start_position) = requested_started_at_position_samples else {
        return RecordTraceSeedOffsets::default();
    };
    let matches_boundary = |sample: &&PreRollTraceSample| {
        sample
            .position_samples
            // A metric stamped exactly at the Record start describes the preceding 400 ms
            // Watch window, not the first Record interval. Importing it shifts the bounded take
            // one slot earlier and can leave PRE/POST with only N-1 shared slots.
            .is_some_and(|sample_position| sample_position > start_position)
    };
    let Some(origin) = pre_roll.samples.iter().find(matches_boundary) else {
        return RecordTraceSeedOffsets::default();
    };
    let origin_frames = origin.frames_48k;
    let origin_native_frames = origin.native_frames;
    let mut last_seed_frames_48k = 0;
    let mut last_seed_native_frames = 0;
    let mut seeded = 0_usize;
    for sample in pre_roll.samples.iter().filter(matches_boundary) {
        let Some(position_samples) = sample.position_samples else {
            continue;
        };
        let t_native_frames = position_samples.saturating_sub(start_position) as u64;
        let t_frames_48k = native_frames_to_48k(t_native_frames, sample_rate);
        let t_ms = native_frames_to_ms(t_native_frames, sample_rate);
        let slot_ms = record_trace_slot_ms(t_ms);
        if slot_ms < *next_trace_ms {
            continue;
        }
        let include_psb = slot_ms >= *next_psb_ms;
        let trace_sample = RecordTraceSample::measured_observed(
            generation,
            slot_ms,
            t_frames_48k,
            Some(t_native_frames),
            sample.result.clone(),
            sample.observed_silence_floor,
            include_psb,
        )
        .with_position_samples(Some(position_samples));
        let trace_sample = trace_sample.with_clock_source(sample.clock_source);
        let trace_sample = trace_sample.with_presentation_latency(sample.presentation_latency);
        push_record_trace_sample(queue, trace_sample);
        *next_trace_ms = (slot_ms / FRAME_INTERVAL_MS + 1) * FRAME_INTERVAL_MS;
        if include_psb {
            *next_psb_ms = (slot_ms / PSB_INTERVAL_MS + 1) * PSB_INTERVAL_MS;
        }
        last_seed_frames_48k = t_frames_48k;
        last_seed_native_frames = t_native_frames;
        seeded += 1;
    }
    if seeded > 0 {
        log::info!(
            "[MeasureThread] seeded {} Record TRACE sample(s) from Watch pre-roll (started_at_position_samples={})",
            seeded,
            start_position
        );
    }
    RecordTraceSeedOffsets {
        frames_48k: last_seed_frames_48k,
        native_frames: last_seed_native_frames,
        origin_frames_48k: origin_frames,
        origin_native_frames,
        seeded: seeded > 0,
    }
}

fn maybe_push_record_trace(
    queue: &RecordTraceQueue,
    timeline: RecordTraceTimeline,
    cursor: &mut RecordTraceCursor<'_>,
    sample_rate: u32,
    observed: RecordTraceObserved,
    result: &MeasureResult,
) -> bool {
    let host_native_frames = observed
        .position_samples
        .zip(timeline.origin_position_samples)
        .and_then(|(position, origin)| {
            (position >= origin).then_some(position.saturating_sub(origin) as u64)
        });
    let host_clock = host_native_frames.is_some();
    let t_native_frames = host_native_frames.or_else(|| {
        observed.native_frames.map(|frames| {
            timeline
                .offset_native_frames
                .saturating_add(frames.saturating_sub(timeline.origin_native_frames))
        })
    });
    let t_frames_48k = host_native_frames.map_or_else(
        || {
            timeline.offset_frames_48k.saturating_add(
                observed
                    .frames_48k
                    .saturating_sub(timeline.origin_frames_48k),
            )
        },
        |frames| native_frames_to_48k(frames, sample_rate),
    );
    let t_ms = host_native_frames.map_or_else(
        || t_frames_48k.saturating_mul(1_000) / ENGINE_SR as u64,
        |frames| native_frames_to_ms(frames, sample_rate),
    );
    let slot_ms = record_trace_slot_ms(t_ms);
    if slot_ms >= *cursor.next_trace_ms {
        push_record_trace_floor_until(
            queue,
            timeline.generation,
            cursor,
            sample_rate,
            slot_ms,
            false,
        );
    } else if !host_clock {
        return false;
    }
    let include_psb = if host_clock {
        slot_ms > 0 && slot_ms.is_multiple_of(PSB_INTERVAL_MS)
    } else {
        slot_ms >= *cursor.next_psb_ms
    };
    let sample = RecordTraceSample::measured_observed(
        timeline.generation,
        slot_ms,
        t_frames_48k,
        t_native_frames,
        result.clone(),
        observed.observed_silence_floor,
        include_psb,
    )
    .with_position_samples(observed.position_samples);
    let sample = sample.with_clock_source(observed.clock_source);
    let sample = sample.with_presentation_latency(observed.presentation_latency);
    push_record_trace_sample(queue, sample);
    *cursor.next_trace_ms =
        (*cursor.next_trace_ms).max((slot_ms / FRAME_INTERVAL_MS + 1) * FRAME_INTERVAL_MS);
    if include_psb && !host_clock {
        *cursor.next_psb_ms = (slot_ms / PSB_INTERVAL_MS + 1) * PSB_INTERVAL_MS;
    }
    true
}

fn record_trace_slot_ms(t_ms: u64) -> u64 {
    (t_ms / FRAME_INTERVAL_MS) * FRAME_INTERVAL_MS
}

fn push_record_trace_floor_until(
    queue: &RecordTraceQueue,
    generation: u64,
    cursor: &mut RecordTraceCursor<'_>,
    sample_rate: u32,
    end_t_ms: u64,
    inclusive: bool,
) -> usize {
    let mut pushed = 0;
    while *cursor.next_trace_ms < end_t_ms || (inclusive && *cursor.next_trace_ms <= end_t_ms) {
        let t_ms = *cursor.next_trace_ms;
        push_record_trace_sample(
            queue,
            RecordTraceSample::missing_marker(
                generation,
                t_ms,
                t_ms_to_48k_frames(t_ms),
                Some(t_ms_to_native_frames(t_ms, sample_rate)),
            ),
        );
        advance_record_trace_cursor(cursor, t_ms, false);
        pushed += 1;
    }
    pushed
}

fn advance_record_trace_cursor(cursor: &mut RecordTraceCursor<'_>, t_ms: u64, include_psb: bool) {
    *cursor.next_trace_ms = (t_ms / FRAME_INTERVAL_MS + 1) * FRAME_INTERVAL_MS;
    if include_psb {
        *cursor.next_psb_ms = (t_ms / PSB_INTERVAL_MS + 1) * PSB_INTERVAL_MS;
    }
}

struct RecordTraceDrain<'a> {
    queue: &'a RecordTraceQueue,
    timeline: RecordTraceTimeline,
    cursor: RecordTraceCursor<'a>,
}

struct DrainRingSession<'a> {
    consumer: &'a mut rtrb::Consumer<f32>,
    resampler: &'a mut Option<ResamplerTo48k>,
    trace_engine: &'a mut MeasureEngine,
    summary_engine: &'a mut MeasureEngine,
    session_summary: &'a Arc<Mutex<Option<SessionSummary>>>,
    chunk_f64: &'a mut Vec<f64>,
    resampled_buf: &'a mut Vec<f64>,
    phase_d: &'a mut PhaseDStream,
    phase_d_mono_buf: &'a mut Vec<f64>,
    phase_d_slot_pending: &'a mut Vec<f64>,
    latest_pd: &'a mut Option<crate::phase_d::stream::PhaseDResult>,
    record_core_pending: &'a mut VecDeque<PendingRecordCore>,
    record_phase_pending: &'a mut VecDeque<crate::phase_d::stream::PhaseDResult>,
    sample_rate: u32,
    n_channels: usize,
    native_frames_total: &'a mut u64,
    consumed_samples: &'a mut u64,
    record_take_tracker: &'a RecordTakeTracker,
    last_capture_position_end: &'a mut Option<i64>,
    finalize_capture: bool,
}

fn native_frames_to_ms(frames: u64, sample_rate: u32) -> u64 {
    if sample_rate == 0 {
        return 0;
    }
    frames.saturating_mul(1_000) / sample_rate as u64
}

fn native_frames_to_48k(frames: u64, sample_rate: u32) -> u64 {
    if sample_rate == 0 {
        return 0;
    }
    frames.saturating_mul(ENGINE_SR as u64) / sample_rate as u64
}

fn t_ms_to_48k_frames(t_ms: u64) -> u64 {
    t_ms.saturating_mul(ENGINE_SR as u64) / 1_000
}

fn t_ms_to_native_frames(t_ms: u64, sample_rate: u32) -> u64 {
    t_ms.saturating_mul(sample_rate as u64) / 1_000
}

fn estimate_observed_native_frames(
    frames_48k: u64,
    frames_48k_before: u64,
    native_frames_before: u64,
    chunk_native_frames: u64,
    chunk_48k_frames: u64,
) -> Option<u64> {
    if chunk_native_frames == 0 {
        return None;
    }
    if chunk_48k_frames == 0 {
        return Some(native_frames_before.saturating_add(chunk_native_frames));
    }
    let observed_delta_48k = frames_48k.saturating_sub(frames_48k_before);
    let observed_delta_native =
        observed_delta_48k.saturating_mul(chunk_native_frames) / chunk_48k_frames;
    Some(native_frames_before.saturating_add(observed_delta_native.min(chunk_native_frames)))
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
struct CaptureChunkPlan {
    sample_count: usize,
    position_start_samples: Option<i64>,
    position_end_samples: Option<i64>,
    clock_source: CaptureClockSource,
}

impl CaptureChunkPlan {
    fn clock_source(self) -> Option<CaptureClockSource> {
        (self.clock_source != CaptureClockSource::Unknown).then_some(self.clock_source)
    }
}

fn capture_chunk_plan(
    tracker: &RecordTakeTracker,
    captured_frames: u64,
    available_samples: usize,
    n_channels: usize,
    sample_rate: u32,
) -> CaptureChunkPlan {
    if n_channels == 0 || available_samples < n_channels {
        return CaptureChunkPlan::default();
    }
    let available_frames =
        ((available_samples / n_channels) as u64).min((sample_rate as u64 / 10).max(1));
    let Some(span) = tracker.capture_span_for_frame(captured_frames.saturating_add(1)) else {
        return CaptureChunkPlan {
            sample_count: available_frames as usize * n_channels,
            ..CaptureChunkPlan::default()
        };
    };
    let frames = available_frames.min(span.capture_end_frame.saturating_sub(captured_frames));
    let position_start_samples = span.position_at_capture_boundary(captured_frames);
    CaptureChunkPlan {
        sample_count: frames as usize * n_channels,
        position_start_samples,
        position_end_samples: position_start_samples
            .map(|position| position.saturating_add(frames as i64)),
        clock_source: span.source,
    }
}

fn push_record_timeline_markers_until(
    trace: &mut RecordTraceDrain<'_>,
    sample_rate: u32,
    native_frames_total: u64,
) -> usize {
    let native_delta = native_frames_total.saturating_sub(trace.timeline.origin_native_frames);
    let t_native_frames = trace
        .timeline
        .offset_native_frames
        .saturating_add(native_delta);
    let end_t_ms = native_frames_to_ms(t_native_frames, sample_rate);
    let mut pushed = push_record_trace_floor_until(
        trace.queue,
        trace.timeline.generation,
        &mut trace.cursor,
        sample_rate,
        end_t_ms,
        true,
    );
    let grid_native_frames = t_ms_to_native_frames(end_t_ms, sample_rate);
    if t_native_frames != grid_native_frames {
        let t_frames_48k = trace
            .timeline
            .offset_frames_48k
            .saturating_add(native_frames_to_48k(native_delta, sample_rate));
        push_record_trace_sample(
            trace.queue,
            RecordTraceSample::missing_marker(
                trace.timeline.generation,
                end_t_ms,
                t_frames_48k,
                Some(t_native_frames),
            ),
        );
        pushed += 1;
    }
    pushed
}

fn push_record_trace_to_record_take_clock(
    queue: &RecordTraceQueue,
    generation: u64,
    tracker: &RecordTakeTracker,
    cursor: &mut RecordTraceCursor<'_>,
    sample_rate: u32,
) -> usize {
    let Some(snapshot) = tracker.snapshot(generation) else {
        return 0;
    };
    push_record_trace_clock_markers_until(
        queue,
        generation,
        cursor,
        sample_rate,
        snapshot.duration_samples,
    )
}

fn push_record_trace_clock_markers_until(
    queue: &RecordTraceQueue,
    generation: u64,
    cursor: &mut RecordTraceCursor<'_>,
    sample_rate: u32,
    end_native_frames: u64,
) -> usize {
    let end_t_ms = native_frames_to_ms(end_native_frames, sample_rate);
    let mut pushed =
        push_record_trace_floor_until(queue, generation, cursor, sample_rate, end_t_ms, true);
    let grid_native_frames = t_ms_to_native_frames(end_t_ms, sample_rate);
    if end_native_frames != grid_native_frames {
        push_record_trace_sample(
            queue,
            RecordTraceSample::missing_marker(
                generation,
                end_t_ms,
                native_frames_to_48k(end_native_frames, sample_rate),
                Some(end_native_frames),
            ),
        );
        pushed += 1;
    }
    pushed
}

/// B-132 (G-115-382): 残量 ring を **Active ループ本体と同一**の convert/resample/engine.push
/// パイプラインに通して engine に算入し、最終 `engine.finalize()` を `session_summary` に書く。
///
/// 取りこぼし tail（transport-stop / DAW-stall で ring に残ったサンプル）を確定値に算入するための
/// 唯一の drain 経路。Common-A（mid-session Inactive discard 前）と P1（Record→Watch エッジ）の
/// 両方から呼ぶ。live meter（`result`）/ phase_d は触らない: `SessionSummary` は engine の
/// lufs_i/lra/max_true_peak のみで、per-sample DSP は engine.push/finalize 内で不変（R-12 / 変わるのは
/// 消費サンプル集合のみ）。
///
/// 戻り値: `engine.finalize()` を `session_summary` に書けたら `true`。resampler error /
/// Mutex poison で `false`（= drain 不能 → 呼び出し側は seal を進めず、IO 側 bounded wait が
/// timeout → integrity_degraded に倒れる / 共通B）。
fn drain_ring_into_session(
    ctx: DrainRingSession<'_>,
    mut record_trace: Option<RecordTraceDrain<'_>>,
) -> bool {
    while ctx.consumer.slots() > 0 {
        let plan = capture_chunk_plan(
            ctx.record_take_tracker,
            *ctx.native_frames_total,
            ctx.consumer.slots(),
            ctx.n_channels,
            ctx.sample_rate,
        );
        if plan.sample_count == 0 {
            break;
        }
        if plan
            .position_start_samples
            .zip(*ctx.last_capture_position_end)
            .is_some_and(|(start, previous_end)| start != previous_end)
        {
            ctx.trace_engine.reset();
            ctx.summary_engine.reset();
            ctx.phase_d.reset();
            if let Some(rs) = ctx.resampler.as_mut() {
                rs.reset();
            }
            *ctx.latest_pd = None;
            ctx.phase_d_slot_pending.clear();
            ctx.record_core_pending.clear();
            ctx.record_phase_pending.clear();
        }
        ctx.chunk_f64.clear();
        for _ in 0..plan.sample_count {
            match ctx.consumer.pop() {
                Ok(s) => {
                    *ctx.consumed_samples = (*ctx.consumed_samples).saturating_add(1);
                    ctx.chunk_f64.push(s as f64);
                }
                Err(_) => break,
            }
        }
        let chunk_native_frames = (ctx.chunk_f64.len() / ctx.n_channels) as u64;
        *ctx.native_frames_total = (*ctx.native_frames_total).saturating_add(chunk_native_frames);
        let native_frames_after = *ctx.native_frames_total;
        *ctx.last_capture_position_end = plan.position_end_samples;
        let chunk_48k: &[f64] = if let Some(rs) = ctx.resampler.as_mut() {
            ctx.resampled_buf.clear();
            if let Err(e) = rs.process(ctx.chunk_f64.as_slice(), ctx.resampled_buf) {
                log::warn!(
                    "[MeasureThread] drain resample error: {:?} — tail finalize skipped",
                    e
                );
                if let Some(trace) = record_trace.as_mut() {
                    push_record_timeline_markers_until(trace, ctx.sample_rate, native_frames_after);
                }
                return false;
            }
            ctx.resampled_buf.as_slice()
        } else {
            ctx.chunk_f64.as_slice()
        };
        feed_phase_d_slots(
            chunk_48k,
            ctx.n_channels,
            ctx.phase_d_mono_buf,
            ctx.phase_d_slot_pending,
            ctx.phase_d,
            ctx.latest_pd,
            ctx.record_phase_pending,
            true,
        );
        if let Some(trace) = record_trace.as_mut() {
            let _ = ctx.trace_engine.push_observed(
                ctx.chunk_f64,
                |native_engine_frames, base_result, observed_chunk| {
                    let observed_native_frames = native_frames_after;
                    let observed_silence_floor = samples_are_trace_silence_floor(observed_chunk);
                    let clock_point = ctx
                        .record_take_tracker
                        .clock_point_for_captured_frame(observed_native_frames);
                    ctx.record_core_pending.push_back(PendingRecordCore {
                        frames_48k: native_frames_to_48k(native_engine_frames, ctx.sample_rate),
                        native_frames: observed_native_frames,
                        position_samples: clock_point.map(|point| point.position_samples),
                        clock_source: clock_point
                            .map_or(CaptureClockSource::Unknown, |point| point.source),
                        presentation_latency: ctx.record_take_tracker.presentation_latency(),
                        observed_silence_floor,
                        result: base_result.clone(),
                    });
                },
            );
            join_record_trace_slots(
                trace,
                ctx.sample_rate,
                ctx.record_core_pending,
                ctx.record_phase_pending,
            );
            let _ = ctx.summary_engine.push(ctx.chunk_f64);
        } else {
            let _ = ctx.trace_engine.push(ctx.chunk_f64);
            let _ = ctx.summary_engine.push(ctx.chunk_f64);
        }
    }
    if ctx.finalize_capture {
        if let Some(resampler) = ctx.resampler.as_mut() {
            ctx.resampled_buf.clear();
            if let Err(error) = resampler.finish(ctx.resampled_buf) {
                log::warn!(
                    "[MeasureThread] Record phase tail flush failed: {:?}",
                    error
                );
                return false;
            }
            feed_phase_d_slots(
                ctx.resampled_buf,
                ctx.n_channels,
                ctx.phase_d_mono_buf,
                ctx.phase_d_slot_pending,
                ctx.phase_d,
                ctx.latest_pd,
                ctx.record_phase_pending,
                true,
            );
        }
        if let Some(trace) = record_trace.as_mut() {
            join_record_trace_slots(
                trace,
                ctx.sample_rate,
                ctx.record_core_pending,
                ctx.record_phase_pending,
            );
        }
    }
    if let Some(trace) = record_trace.as_mut() {
        push_record_timeline_markers_until(trace, ctx.sample_rate, *ctx.native_frames_total);
    }
    let summary = ctx.summary_engine.finalize();
    match ctx.session_summary.lock() {
        Ok(mut g) => {
            *g = Some(summary);
            true
        }
        Err(e) => {
            log::warn!(
                "[MeasureThread] drain session_summary Mutex poisoned: {}",
                e
            );
            false
        }
    }
}

/// PSB low / mid / high を集約して dB 表現にする。
///
///  C-3 (Daisuke 判断 経路A、破壊的変更):
/// - low : Bark 1–8   ISO 532-1 specific loudness (sone/Bark)         → dB
/// - mid : Bark 9–16  ISO 532-1 specific loudness (sone/Bark)         → dB
/// - high: Bark 21–24 + 15.5k–20kHz FFT energy (linear power)         → dB
///
/// 旧 high (Bark 17–20 specific loudness) は完全廃止し並存させない。
/// ISO 532-1 由来の psb[16..20] は計算に使わない（n_prime[20] / psb_bark[20]
/// 側では引き続き露出するため、外部から参照したい場合はそちらを使う）。
fn compute_psb_summary(
    psb: &[f64; 20],
    psb_bark21_24: &[f64; 4],
    psb_high_ext_15_5k_20k: f64,
) -> PsbSummary {
    let low: f64 = psb[0..8].iter().sum();
    let mid: f64 = psb[8..16].iter().sum();
    //  C-3: Bark 21–24 FFT power + 15.5k–20kHz 補完。
    // FFT 経路は別単位 (linear power) のため log10 で dB 化するのは
    // low/mid と同じ「対数スケール」に揃えるためのみで、絶対値の
    // 比較可能性を保証するものではない（PsbSummary doc 参照）。
    let high_lin: f64 = psb_bark21_24.iter().sum::<f64>() + psb_high_ext_15_5k_20k;
    let tiny = 1e-12;
    PsbSummary {
        low: 10.0 * (low + tiny).log10(),
        mid: 10.0 * (mid + tiny).log10(),
        high: 10.0 * (high_lin + tiny).log10(),
    }
}

#[cfg(test)]
pub mod tests {
    use super::{
        compute_psb_summary, push_record_trace_clock_markers_until,
        push_record_trace_to_record_take_clock, record_grid_alignment, RecordTraceCursor,
    };
    use crate::record_take::{
        CaptureClockSource, PresentationLatencySamples, RecordTakeBlock, RecordTakeTracker,
    };
    use crate::record_writer::{
        drain_record_trace_queue, new_record_trace_queue, FRAME_INTERVAL_MS,
    };

    /// Test-only public wrapper for compute_psb_summary.
    /// Used by stream.rs pink noise test.
    pub fn compute_psb_summary_pub(
        psb: &[f64; 20],
        psb_bark21_24: &[f64; 4],
        psb_high_ext_15_5k_20k: f64,
    ) -> crate::PsbSummary {
        compute_psb_summary(psb, psb_bark21_24, psb_high_ext_15_5k_20k)
    }

    #[test]
    fn record_mode_uses_short_idle_sleep_for_offline_bounce_catchup() {
        assert_eq!(super::idle_sleep_for_record_state(false), super::LOOP_SLEEP);
        assert_eq!(
            super::idle_sleep_for_record_state(true),
            super::RECORD_LOOP_SLEEP
        );
        assert!(
            super::idle_sleep_for_record_state(true) < super::LOOP_SLEEP,
            "Record mode must poll faster than Watch mode"
        );
    }

    #[test]
    fn studio_one_instance_start_skew_converges_to_one_absolute_100ms_grid() {
        let (post_padding, post_first_end) = record_grid_alignment(6_465_671, 96_000);
        let (pre_padding, pre_first_end) = record_grid_alignment(6_473_347, 96_000);

        assert_eq!(post_padding, 4_871);
        assert_eq!(pre_padding, 2_947);
        assert_eq!(post_first_end, 6_470_400);
        assert_eq!(pre_first_end, 6_480_000);
        assert_eq!(post_first_end.rem_euclid(9_600), 0);
        assert_eq!(pre_first_end.rem_euclid(9_600), 0);
        assert_eq!(post_first_end.saturating_add(9_600), pre_first_end);
    }

    #[test]
    fn watch_pass_cutover_drain_preserves_samples_pushed_after_cutover() {
        let (mut producer, mut consumer) = rtrb::RingBuffer::new(8);
        let mut pushed = 0_u64;
        for sample in [1.0_f32, 2.0] {
            producer.push(sample).unwrap();
            pushed += 1;
        }
        let cutover = pushed;
        for sample in [9.0_f32, 10.0] {
            producer.push(sample).unwrap();
            pushed += 1;
        }

        let mut consumed = 0_u64;
        let drained = super::drain_consumer_until_sample(&mut consumer, &mut consumed, cutover);
        assert_eq!(drained, 2);
        assert_eq!(consumed, cutover);
        assert_eq!(consumer.pop().unwrap(), 9.0);
        assert_eq!(consumer.pop().unwrap(), 10.0);
        assert_eq!(pushed, 4);
    }

    #[test]
    fn record_start_position_drain_drops_only_queued_frames_before_barrier() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 1_000, 8);
        let (mut producer, mut consumer) = rtrb::RingBuffer::new(16);
        for sample in 1..=16 {
            producer.push(sample as f32).unwrap();
        }

        let mut consumed_samples = 0_u64;
        let mut native_frames_total = 0_u64;
        let drained = super::drain_consumer_before_record_start_position(
            &mut consumer,
            &mut consumed_samples,
            &mut native_frames_total,
            &tracker,
            Some(1_005),
            2,
        );

        assert_eq!(drained, 4);
        assert_eq!(consumed_samples, 8);
        assert_eq!(native_frames_total, 4);
        assert_eq!(consumer.pop().unwrap(), 9.0);
        assert_eq!(consumer.pop().unwrap(), 10.0);
    }

    #[test]
    fn record_start_position_drain_drops_unmapped_queued_frames_before_barrier() {
        let tracker = RecordTakeTracker::new();
        let (mut producer, mut consumer) = rtrb::RingBuffer::new(4);
        for sample in [1.0_f32, 2.0] {
            producer.push(sample).unwrap();
        }

        let mut consumed_samples = 0_u64;
        let mut native_frames_total = 0_u64;
        let drained = super::drain_consumer_before_record_start_position(
            &mut consumer,
            &mut consumed_samples,
            &mut native_frames_total,
            &tracker,
            Some(1_005),
            2,
        );

        assert_eq!(drained, 1);
        assert_eq!(consumed_samples, 2);
        assert_eq!(native_frames_total, 0);
        assert_eq!(
            consumer.slots(),
            0,
            "unmapped queued samples must not become the Record origin"
        );
    }

    #[test]
    fn record_start_position_drain_drops_pending_watch_samples_when_start_is_unlatched() {
        let tracker = RecordTakeTracker::new();
        let (mut producer, mut consumer) = rtrb::RingBuffer::new(8);
        for sample in [1.0_f32, 2.0, 3.0, 4.0] {
            producer.push(sample).unwrap();
        }

        let mut consumed_samples = 0_u64;
        let mut native_frames_total = 0_u64;
        let drained = super::drain_consumer_before_record_start_position(
            &mut consumer,
            &mut consumed_samples,
            &mut native_frames_total,
            &tracker,
            None,
            2,
        );

        assert_eq!(drained, 2);
        assert_eq!(consumed_samples, 4);
        assert_eq!(
            native_frames_total, 0,
            "pre-start Watch samples must not advance the Record native origin"
        );
        assert_eq!(consumer.slots(), 0);
    }

    #[test]
    fn watch_active_pop_limit_preserves_new_pass_samples_already_in_ring() {
        let (mut producer, mut consumer) = rtrb::RingBuffer::new(8);
        for sample in [1.0_f32, 2.0, 9.0, 10.0] {
            producer.push(sample).unwrap();
        }

        let mut consumed = 0_u64;
        let available = consumer
            .slots()
            .min(super::samples_until_cursor_limit(consumed, 2));
        let mut popped = Vec::new();
        for _ in 0..available {
            popped.push(consumer.pop().unwrap());
            consumed += 1;
        }

        assert_eq!(popped, vec![1.0, 2.0]);
        assert_eq!(consumed, 2);
        assert_eq!(consumer.pop().unwrap(), 9.0);
        assert_eq!(consumer.pop().unwrap(), 10.0);
    }

    #[test]
    fn record_take_clock_closes_96k_15s_trace_grid() {
        let queue = new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let pushed = push_record_trace_clock_markers_until(
            &queue,
            42,
            &mut RecordTraceCursor {
                next_trace_ms: &mut next_trace_ms,
                next_psb_ms: &mut next_psb_ms,
            },
            96_000,
            1_440_000,
        );
        let frames = drain_record_trace_queue(&queue);

        assert_eq!(pushed, 151);
        assert_eq!(frames.len(), 151);
        assert_eq!(frames.first().map(|f| f.t_ms), Some(0));
        assert_eq!(frames.last().map(|f| f.t_ms), Some(15_000));
        assert_eq!(
            frames.last().and_then(|f| f.t_native_frames),
            Some(1_440_000)
        );
        for pair in frames.windows(2) {
            assert_eq!(pair[1].t_ms - pair[0].t_ms, FRAME_INTERVAL_MS);
        }
    }

    #[test]
    fn record_take_tracker_snapshot_closes_missing_measure_tail() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 7,
            recording: true,
            rendered: true,
            playing: false,
            offline: true,
            position_valid: true,
            position_samples: 0,
            num_frames: 1_440_000,
            clock_start_samples: 0,
            clock_end_samples: Some(1_440_000),
        });

        let queue = new_record_trace_queue();
        let mut next_trace_ms = 10_200;
        let mut next_psb_ms = 0;
        let pushed = push_record_trace_to_record_take_clock(
            &queue,
            7,
            &tracker,
            &mut RecordTraceCursor {
                next_trace_ms: &mut next_trace_ms,
                next_psb_ms: &mut next_psb_ms,
            },
            96_000,
        );
        let frames = drain_record_trace_queue(&queue);

        assert_eq!(pushed, 49);
        assert_eq!(frames.first().map(|f| f.t_ms), Some(10_200));
        assert_eq!(frames.last().map(|f| f.t_ms), Some(15_000));
        assert_eq!(
            frames.last().and_then(|f| f.t_native_frames),
            Some(1_440_000)
        );
        for pair in frames.windows(2) {
            assert_eq!(pair[1].t_ms - pair[0].t_ms, FRAME_INTERVAL_MS);
        }
    }

    #[test]
    fn pre_roll_seed_does_not_use_wall_clock_when_native_start_is_unlatched() {
        let mut pre_roll = super::RecordTracePreRoll::default();
        let before = crate::MeasureResult {
            lufs_m: Some(-30.0),
            ..Default::default()
        };
        let after_a = crate::MeasureResult {
            lufs_m: Some(-20.0),
            ..Default::default()
        };
        let after_b = crate::MeasureResult {
            lufs_m: Some(-19.0),
            ..Default::default()
        };

        pre_roll.push(900, None, 4_800, 4_410, false, &before);
        pre_roll.push(1_050, None, 9_600, 8_820, false, &after_a);
        pre_roll.push(1_150, None, 14_400, 13_230, false, &after_b);

        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let offset = super::seed_record_trace_from_pre_roll(
            &queue,
            &pre_roll,
            77,
            None,
            44_100,
            &mut next_trace_ms,
            &mut next_psb_ms,
        );
        let drained = crate::record_writer::drain_record_trace_queue(&queue);

        assert_eq!(offset, super::RecordTraceSeedOffsets::default());
        assert!(drained.is_empty());
        assert_eq!(next_trace_ms, 0);
        assert_eq!(next_psb_ms, 0);
    }

    #[test]
    fn pre_roll_seed_prefers_native_position_over_wall_clock() {
        let mut pre_roll = super::RecordTracePreRoll::default();
        let wall_after_but_native_before = crate::MeasureResult {
            lufs_m: Some(-30.0),
            ..Default::default()
        };
        let native_after_a = crate::MeasureResult {
            lufs_m: Some(-20.0),
            ..Default::default()
        };
        let native_after_b = crate::MeasureResult {
            lufs_m: Some(-19.0),
            ..Default::default()
        };

        pre_roll.push(
            1_200,
            Some(95_900),
            4_800,
            4_410,
            false,
            &wall_after_but_native_before,
        );
        pre_roll.push(900, Some(96_000), 9_600, 8_820, false, &native_after_a);
        pre_roll.push(950, Some(100_410), 14_400, 13_230, false, &native_after_b);

        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let offset = super::seed_record_trace_from_pre_roll(
            &queue,
            &pre_roll,
            77,
            Some(96_000),
            44_100,
            &mut next_trace_ms,
            &mut next_psb_ms,
        );
        let drained = crate::record_writer::drain_record_trace_queue(&queue);

        assert_eq!(drained.len(), 1);
        assert_eq!(drained[0].t_ms, 100);
        assert_eq!(drained[0].t_frames_48k, 4_800);
        assert_eq!(drained[0].t_native_frames, Some(4_410));
        assert_eq!(drained[0].result.lufs_m, Some(-19.0));
        assert_eq!(offset.origin_frames_48k, 14_400);
        assert_eq!(offset.origin_native_frames, 13_230);
        assert!(offset.seeded);
    }

    #[test]
    fn pre_roll_seed_transition_offset_includes_gap_after_last_seed() {
        let mut pre_roll = super::RecordTracePreRoll::default();
        let after_a = crate::MeasureResult {
            lufs_m: Some(-20.0),
            ..Default::default()
        };
        let after_b = crate::MeasureResult {
            lufs_m: Some(-19.0),
            ..Default::default()
        };
        pre_roll.push(1_050, Some(96_001), 9_600, 8_820, false, &after_a);
        pre_roll.push(1_150, Some(100_410), 14_400, 13_230, false, &after_b);

        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let offset = super::seed_record_trace_from_pre_roll(
            &queue,
            &pre_roll,
            77,
            Some(96_000),
            44_100,
            &mut next_trace_ms,
            &mut next_psb_ms,
        );
        let transition_native_frames = 17_640_u64;
        let transition_offset_native =
            transition_native_frames.saturating_sub(offset.origin_native_frames);
        assert_eq!(transition_offset_native, 8_820);
        assert!(
            transition_offset_native > offset.native_frames,
            "Record transition offset must cover audio time between the last seeded sample and the transition"
        );
        assert_eq!(
            super::native_frames_to_48k(transition_offset_native, 44_100),
            9_600
        );
    }

    #[test]
    fn pre_roll_seed_claims_100ms_slots_for_mid_slot_samples() {
        let mut pre_roll = super::RecordTracePreRoll::default();
        let after_a = crate::MeasureResult {
            lufs_m: Some(-20.0),
            ..Default::default()
        };
        let after_b = crate::MeasureResult {
            lufs_m: Some(-19.0),
            ..Default::default()
        };
        pre_roll.push(1_000, Some(48_000), 10_000, 20_000, false, &after_a);
        pre_roll.push(1_100, Some(62_400), 17_200, 34_400, false, &after_b);

        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let offset = super::seed_record_trace_from_pre_roll(
            &queue,
            &pre_roll,
            77,
            Some(48_000),
            96_000,
            &mut next_trace_ms,
            &mut next_psb_ms,
        );
        let drained = crate::record_writer::drain_record_trace_queue(&queue);

        assert_eq!(
            drained.iter().map(|sample| sample.t_ms).collect::<Vec<_>>(),
            vec![100]
        );
        assert_eq!(drained[0].t_frames_48k, 7_200);
        assert_eq!(drained[0].t_native_frames, Some(14_400));
        assert_eq!(offset.frames_48k, 7_200);
        assert_eq!(offset.native_frames, 14_400);
        assert_eq!(next_trace_ms, 200);
    }

    #[test]
    fn pre_roll_seed_is_disabled_without_record_started_at() {
        let mut pre_roll = super::RecordTracePreRoll::default();
        pre_roll.push(
            1_000,
            None,
            4_800,
            4_410,
            false,
            &crate::MeasureResult::default(),
        );
        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;

        let offset = super::seed_record_trace_from_pre_roll(
            &queue,
            &pre_roll,
            77,
            None,
            48_000,
            &mut next_trace_ms,
            &mut next_psb_ms,
        );

        assert_eq!(offset, super::RecordTraceSeedOffsets::default());
        assert!(crate::record_writer::drain_record_trace_queue(&queue).is_empty());
        assert_eq!(next_trace_ms, 0);
    }

    #[test]
    fn record_trace_observed_gap_is_filled_on_audio_time_grid() {
        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let timeline = super::RecordTraceTimeline {
            generation: 5,
            origin_frames_48k: 0,
            origin_native_frames: 0,
            offset_frames_48k: 0,
            offset_native_frames: 0,
            origin_position_samples: None,
        };
        let observed = crate::MeasureResult {
            lufs_m: Some(-18.0),
            ..Default::default()
        };

        {
            let mut cursor = super::RecordTraceCursor {
                next_trace_ms: &mut next_trace_ms,
                next_psb_ms: &mut next_psb_ms,
            };
            assert!(super::maybe_push_record_trace(
                &queue,
                timeline,
                &mut cursor,
                96_000,
                super::RecordTraceObserved {
                    frames_48k: 24_000,
                    native_frames: Some(48_000),
                    position_samples: None,
                    clock_source: CaptureClockSource::Unknown,
                    presentation_latency: PresentationLatencySamples::default(),
                    observed_silence_floor: false,
                },
                &observed,
            ));
        }

        let drained = crate::record_writer::drain_record_trace_queue(&queue);
        assert_eq!(drained.len(), 6);
        assert_eq!(
            drained.iter().map(|sample| sample.t_ms).collect::<Vec<_>>(),
            vec![0, 100, 200, 300, 400, 500]
        );
        assert_eq!(drained[4].t_native_frames, Some(38_400));
        assert_eq!(drained[4].result.lufs_m, None);
        assert_eq!(drained[5].t_native_frames, Some(48_000));
        assert_eq!(drained[5].result.lufs_m, Some(-18.0));
        assert_eq!(next_trace_ms, 600);
    }

    #[test]
    fn record_trace_mid_slot_measurement_claims_expected_slot() {
        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 2_200;
        let mut next_psb_ms = 3_000;
        let timeline = super::RecordTraceTimeline {
            generation: 5,
            origin_frames_48k: 0,
            origin_native_frames: 0,
            offset_frames_48k: 0,
            offset_native_frames: 0,
            origin_position_samples: None,
        };
        let observed = crate::MeasureResult {
            lufs_m: Some(-18.0),
            true_peak: Some(-3.0),
            crest: Some(8.0),
            ..Default::default()
        };

        {
            let mut cursor = super::RecordTraceCursor {
                next_trace_ms: &mut next_trace_ms,
                next_psb_ms: &mut next_psb_ms,
            };
            assert!(super::maybe_push_record_trace(
                &queue,
                timeline,
                &mut cursor,
                96_000,
                super::RecordTraceObserved {
                    frames_48k: 108_000,
                    native_frames: Some(216_000),
                    position_samples: None,
                    clock_source: CaptureClockSource::Unknown,
                    presentation_latency: PresentationLatencySamples::default(),
                    observed_silence_floor: false,
                },
                &observed,
            ));
        }

        let drained = crate::record_writer::drain_record_trace_queue(&queue);
        assert_eq!(drained.len(), 1);
        assert_eq!(
            drained[0].kind,
            crate::record_writer::RecordTraceKind::Measured
        );
        assert_eq!(drained[0].t_ms, 2_200);
        assert_eq!(drained[0].t_frames_48k, 108_000);
        assert_eq!(drained[0].t_native_frames, Some(216_000));
        assert_eq!(drained[0].result.lufs_m, Some(-18.0));
        assert_eq!(next_trace_ms, 2_300);
    }

    #[test]
    fn record_trace_origin_offset_keeps_long_bounce_dense() {
        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let timeline = super::RecordTraceTimeline {
            generation: 8,
            origin_frames_48k: 2_400,
            origin_native_frames: 4_800,
            offset_frames_48k: 0,
            offset_native_frames: 0,
            origin_position_samples: None,
        };
        let observed = crate::MeasureResult {
            lufs_m: Some(-18.0),
            true_peak: Some(-3.0),
            crest: Some(8.0),
            ..Default::default()
        };

        {
            let mut cursor = super::RecordTraceCursor {
                next_trace_ms: &mut next_trace_ms,
                next_psb_ms: &mut next_psb_ms,
            };
            for i in 1_u64..=151 {
                assert!(super::maybe_push_record_trace(
                    &queue,
                    timeline,
                    &mut cursor,
                    96_000,
                    super::RecordTraceObserved {
                        frames_48k: i * 4_800,
                        native_frames: Some(i * 9_600),
                        position_samples: None,
                        clock_source: CaptureClockSource::Unknown,
                        presentation_latency: PresentationLatencySamples::default(),
                        observed_silence_floor: false,
                    },
                    &observed,
                ));
            }
        }

        let drained = crate::record_writer::drain_record_trace_queue(&queue);
        assert_eq!(drained.len(), 151);
        assert_eq!(drained.first().map(|sample| sample.t_ms), Some(0));
        assert_eq!(drained.last().map(|sample| sample.t_ms), Some(15_000));
        assert!(drained
            .iter()
            .all(|sample| sample.kind == crate::record_writer::RecordTraceKind::Measured));
        for pair in drained.windows(2) {
            assert_eq!(pair[1].t_ms - pair[0].t_ms, FRAME_INTERVAL_MS);
        }
        assert_eq!(next_trace_ms, 15_100);
    }

    #[test]
    fn record_trace_observed_silence_floor_becomes_explicit_trace_frame() {
        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let timeline = super::RecordTraceTimeline {
            generation: 5,
            origin_frames_48k: 0,
            origin_native_frames: 0,
            offset_frames_48k: 0,
            offset_native_frames: 0,
            origin_position_samples: None,
        };

        {
            let mut cursor = super::RecordTraceCursor {
                next_trace_ms: &mut next_trace_ms,
                next_psb_ms: &mut next_psb_ms,
            };
            assert!(super::maybe_push_record_trace(
                &queue,
                timeline,
                &mut cursor,
                96_000,
                super::RecordTraceObserved {
                    frames_48k: 4_800,
                    native_frames: Some(9_600),
                    position_samples: None,
                    clock_source: CaptureClockSource::Unknown,
                    presentation_latency: PresentationLatencySamples::default(),
                    observed_silence_floor: true,
                },
                &crate::MeasureResult::default(),
            ));
        }

        let drained = crate::record_writer::drain_record_trace_queue(&queue);
        assert_eq!(drained.len(), 2);
        assert_eq!(drained[1].t_ms, 100);
        assert_eq!(
            drained[1].result.lufs_m,
            Some(crate::record_writer::TRACE_SILENCE_LUFS)
        );
        assert_eq!(
            drained[1].result.true_peak,
            Some(crate::record_writer::TRACE_SILENCE_TRUE_PEAK_DBTP)
        );
        assert_eq!(
            drained[1].result.crest,
            Some(crate::record_writer::TRACE_SILENCE_CREST_DB)
        );
    }

    #[test]
    fn record_trace_mixed_engine_batch_marks_only_silent_100ms_window() {
        const SR: u32 = 48_000;

        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let mut engine = crate::MeasureEngine::new(SR, 2).unwrap();
        let timeline = super::RecordTraceTimeline {
            generation: 6,
            origin_frames_48k: 0,
            origin_native_frames: 0,
            offset_frames_48k: 0,
            offset_native_frames: 0,
            origin_position_samples: None,
        };
        let mut mixed = vec![0.0; (SR / 10) as usize * 2];
        for frame in 0..(SR / 10) {
            let t = frame as f64 / SR as f64;
            let sample = 0.25 * (2.0 * std::f64::consts::PI * 1_000.0 * t).sin();
            mixed.push(sample);
            mixed.push(sample);
        }

        let frames_48k_before = engine.total_frames();
        let chunk_48k_frames = (mixed.len() / 2) as u64;
        let chunk_native_frames = chunk_48k_frames;
        {
            let mut cursor = super::RecordTraceCursor {
                next_trace_ms: &mut next_trace_ms,
                next_psb_ms: &mut next_psb_ms,
            };
            let _ = engine.push_observed(&mixed, |frames_48k, result, observed_chunk| {
                let observed_native_frames = super::estimate_observed_native_frames(
                    frames_48k,
                    frames_48k_before,
                    0,
                    chunk_native_frames,
                    chunk_48k_frames,
                )
                .expect("native frame estimate");
                assert!(super::maybe_push_record_trace(
                    &queue,
                    timeline,
                    &mut cursor,
                    SR,
                    super::RecordTraceObserved {
                        frames_48k,
                        native_frames: Some(observed_native_frames),
                        position_samples: None,
                        clock_source: CaptureClockSource::Unknown,
                        presentation_latency: PresentationLatencySamples::default(),
                        observed_silence_floor:
                            crate::record_writer::samples_are_trace_silence_floor(observed_chunk),
                    },
                    result,
                ));
            });
        }

        let drained = crate::record_writer::drain_record_trace_queue(&queue);
        assert_eq!(
            drained.iter().map(|sample| sample.t_ms).collect::<Vec<_>>(),
            vec![0, 100, 200]
        );
        assert_eq!(
            drained[1].result.lufs_m,
            Some(crate::record_writer::TRACE_SILENCE_LUFS)
        );
        assert_eq!(
            drained[1].result.true_peak,
            Some(crate::record_writer::TRACE_SILENCE_TRUE_PEAK_DBTP)
        );
        assert_eq!(
            drained[1].result.crest,
            Some(crate::record_writer::TRACE_SILENCE_CREST_DB)
        );
        let non_silent = &drained[2].result;
        let is_explicit_silence = non_silent
            .lufs_m
            .is_some_and(|v| v <= crate::record_writer::TRACE_SILENCE_LUFS + 0.1)
            && non_silent
                .true_peak
                .is_some_and(|v| v <= crate::record_writer::TRACE_SILENCE_TRUE_PEAK_DBTP + 0.1)
            && non_silent.crest.is_some_and(|v| v.abs() <= 0.1);
        assert!(
            !is_explicit_silence,
            "non-silent 100ms window must not be falsified as explicit silence"
        );
    }

    #[test]
    fn ninety_six_khz_record_engine_keeps_all_native_host_boundaries() {
        const NATIVE_SR: u32 = 96_000;
        let tracker = crate::record_take::RecordTakeTracker::new();
        let mut engine = crate::MeasureEngine::new(NATIVE_SR, 2).unwrap();
        let mut native_total = 0_u64;
        let mut observed_positions = Vec::new();

        for slot in 0..10_u64 {
            let frames = (NATIVE_SR / 10) as usize;
            tracker.note_capture_window(true, (slot * frames as u64) as i64, frames as u64);
            let input = vec![0.25_f64; frames * 2];
            native_total = native_total.saturating_add(frames as u64);
            engine.push_observed(&input, |_, _, _| {
                let native_frame = native_total;
                observed_positions.push(
                    tracker
                        .position_samples_for_captured_frame(native_frame)
                        .expect("host position"),
                );
            });
        }

        assert_eq!(observed_positions.len(), 10);
        assert_eq!(
            observed_positions,
            (1..=10)
                .map(|slot| slot * (NATIVE_SR as i64 / 10))
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn repeated_offline_host_range_replaces_preroll_at_96khz() {
        const SR: u32 = 96_000;
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 0, SR as u64);
        tracker.note_capture_window(true, 0, SR as u64);
        let silence = vec![0.0_f32; SR as usize * 2];
        let mut signal = Vec::with_capacity(SR as usize * 2);
        for frame in 0..SR as usize {
            let sample = 0.25 * (frame as f32 * 997.0 * std::f32::consts::TAU / SR as f32).sin();
            signal.extend_from_slice(&[sample, sample]);
        }
        let (mut producer, mut consumer) = rtrb::RingBuffer::new(silence.len() + signal.len() + 16);
        for sample in silence.iter().chain(signal.iter()) {
            producer.push(*sample).unwrap();
        }

        let queue = new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;
        let mut resampler = Some(crate::resampler::ResamplerTo48k::new(SR, 2).unwrap());
        let mut trace_engine = crate::MeasureEngine::new(SR, 2).unwrap();
        let mut summary_engine = crate::MeasureEngine::new(SR, 2).unwrap();
        let summary = std::sync::Arc::new(std::sync::Mutex::new(None));
        let mut chunk = Vec::new();
        let mut resampled = Vec::new();
        let mut phase =
            crate::phase_d::stream::PhaseDStream::new(crate::phase_d::tables::FieldType::Free);
        let mut phase_mono = Vec::new();
        let mut phase_slots = Vec::new();
        let mut latest_phase = None;
        let mut core_pending = std::collections::VecDeque::new();
        let mut phase_pending = std::collections::VecDeque::new();
        let mut native_frames = 0_u64;
        let mut consumed_samples = 0_u64;
        let mut last_position_end = None;
        let ok = super::drain_ring_into_session(
            super::DrainRingSession {
                consumer: &mut consumer,
                resampler: &mut resampler,
                trace_engine: &mut trace_engine,
                summary_engine: &mut summary_engine,
                session_summary: &summary,
                chunk_f64: &mut chunk,
                resampled_buf: &mut resampled,
                phase_d: &mut phase,
                phase_d_mono_buf: &mut phase_mono,
                phase_d_slot_pending: &mut phase_slots,
                latest_pd: &mut latest_phase,
                record_core_pending: &mut core_pending,
                record_phase_pending: &mut phase_pending,
                sample_rate: SR,
                n_channels: 2,
                native_frames_total: &mut native_frames,
                consumed_samples: &mut consumed_samples,
                record_take_tracker: &tracker,
                last_capture_position_end: &mut last_position_end,
                finalize_capture: true,
            },
            Some(super::RecordTraceDrain {
                queue: &queue,
                timeline: super::RecordTraceTimeline {
                    generation: 1,
                    origin_frames_48k: 0,
                    origin_native_frames: 0,
                    offset_frames_48k: 0,
                    offset_native_frames: 0,
                    origin_position_samples: Some(0),
                },
                cursor: super::RecordTraceCursor {
                    next_trace_ms: &mut next_trace_ms,
                    next_psb_ms: &mut next_psb_ms,
                },
            }),
        );
        assert!(ok);

        let samples = drain_record_trace_queue(&queue);
        for slot in 1..=10_u64 {
            let last = samples
                .iter()
                .rev()
                .find(|sample| sample.t_ms == slot * 100 && sample.has_measured_core())
                .expect("actual pass owns every host slot");
            assert!(
                last.result.lufs_m.is_some_and(|value| value > -100.0),
                "slot={slot} lufs={:?}",
                last.result.lufs_m
            );
            assert!(
                last.result.n_prime.is_some(),
                "Phase D must be slot-aligned"
            );
            assert_eq!(last.position_samples, Some((slot * 9_600) as i64));
        }
    }

    #[test]
    fn record_trace_final_marker_covers_96k_fifteen_second_grid() {
        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;

        let pushed = {
            let mut trace = super::RecordTraceDrain {
                queue: &queue,
                timeline: super::RecordTraceTimeline {
                    generation: 9,
                    origin_frames_48k: 0,
                    origin_native_frames: 0,
                    offset_frames_48k: 0,
                    offset_native_frames: 0,
                    origin_position_samples: None,
                },
                cursor: super::RecordTraceCursor {
                    next_trace_ms: &mut next_trace_ms,
                    next_psb_ms: &mut next_psb_ms,
                },
            };
            super::push_record_timeline_markers_until(&mut trace, 96_000, 1_440_000)
        };
        let drained = crate::record_writer::drain_record_trace_queue(&queue);

        assert_eq!(pushed, 151);
        assert_eq!(drained.len(), 151);
        assert_eq!(drained.first().map(|sample| sample.t_ms), Some(0));
        assert_eq!(drained.last().map(|sample| sample.t_ms), Some(15_000));
        assert_eq!(
            drained.last().and_then(|sample| sample.t_native_frames),
            Some(1_440_000)
        );
        assert_eq!(next_trace_ms, 15_100);
    }

    #[test]
    fn record_trace_terminal_marker_preserves_non_grid_native_end() {
        let queue = crate::record_writer::new_record_trace_queue();
        let mut next_trace_ms = 0;
        let mut next_psb_ms = 0;

        let pushed = {
            let mut trace = super::RecordTraceDrain {
                queue: &queue,
                timeline: super::RecordTraceTimeline {
                    generation: 10,
                    origin_frames_48k: 0,
                    origin_native_frames: 0,
                    offset_frames_48k: 0,
                    offset_native_frames: 0,
                    origin_position_samples: None,
                },
                cursor: super::RecordTraceCursor {
                    next_trace_ms: &mut next_trace_ms,
                    next_psb_ms: &mut next_psb_ms,
                },
            };
            super::push_record_timeline_markers_until(&mut trace, 96_000, 1_440_048)
        };
        let drained = crate::record_writer::drain_record_trace_queue(&queue);

        assert_eq!(pushed, 152);
        assert_eq!(drained.len(), 152);
        assert_eq!(drained[150].t_ms, 15_000);
        assert_eq!(drained[150].t_native_frames, Some(1_440_000));
        assert_eq!(drained[151].t_ms, 15_000);
        assert_eq!(drained[151].t_native_frames, Some(1_440_048));
        assert_eq!(next_trace_ms, 15_100);
    }

    // ── B-115: POST pair lock 述語（playing かつ live）+ heartbeat 鮮度の単体 ──
    // (a) playing=true + 鮮度内 → locked
    #[test]
    fn b115_pair_lock_a_playing_and_live_locks() {
        assert!(
            super::pair_lock_active(true, true),
            "(a) playing かつ live → locked"
        );
    }
    // (b) playing=true（凍結値）+ 鮮度切れ → unlocked（false-release）
    #[test]
    fn b115_pair_lock_b_frozen_playing_stale_unlocks() {
        assert!(
            !super::pair_lock_active(true, false),
            "(b) playing(凍結) かつ live=false → unlocked"
        );
    }
    // (c) playing=false → unlocked（live 値に依らず）
    #[test]
    fn b115_pair_lock_c_not_playing_unlocks() {
        assert!(
            !super::pair_lock_active(false, true),
            "(c) playing=false → unlocked"
        );
        assert!(
            !super::pair_lock_active(false, false),
            "(c) playing=false → unlocked（live 無関係）"
        );
    }
    // (i) B-118 評価器単体: 連続 beat→live / 停止→window(3s) 経過で false / 再開→true。
    //     duration（elapsed ns）注入で決定的に検証する。
    #[test]
    fn b118_evaluator_beat_then_stale_after_window_then_resume() {
        use std::sync::atomic::{AtomicU32, Ordering};
        use std::sync::Arc;
        let hb = Arc::new(AtomicU32::new(0));
        let win = std::time::Duration::from_secs(3);
        let ev = super::LivenessEvaluator::new(Arc::clone(&hb), win);
        let w = win.as_nanos() as u64;
        // beat: heartbeat 前進 → live
        hb.fetch_add(1, Ordering::Relaxed);
        assert!(ev.is_live_at(1_000), "beat 直後は live");
        // 停止: heartbeat 不変。window 未満は live 維持 / window 到達・超過は not live
        assert!(
            ev.is_live_at(1_000 + w - 1),
            "停止後 window 未満は live（<3s）"
        );
        assert!(
            !ev.is_live_at(1_000 + w),
            "停止後 window 到達で not live（=3s）"
        );
        assert!(
            !ev.is_live_at(1_000 + w + 1_000_000_000),
            "window 超過も not live"
        );
        // 再開: heartbeat 前進 → live 復帰
        hb.fetch_add(1, Ordering::Relaxed);
        assert!(
            ev.is_live_at(1_000 + w + 2_000_000_000),
            "再 beat で live 復帰"
        );
    }

    // (ii) B-115 回帰の 3s 化: 3s 未満のギャップで pair lock が false-release しない。
    #[test]
    fn b118_pair_lock_no_false_release_below_3s() {
        use std::sync::atomic::{AtomicU32, Ordering};
        use std::sync::Arc;
        let hb = Arc::new(AtomicU32::new(0));
        let win = std::time::Duration::from_secs(3);
        let ev = super::LivenessEvaluator::new(Arc::clone(&hb), win);
        let w = win.as_nanos() as u64;
        hb.fetch_add(1, Ordering::Relaxed);
        assert!(ev.is_live_at(0), "beat → live");
        // playing 凍結 + 3s 未満ギャップ: live 維持 → pair_lock_active(true, live)=true（lock 維持）
        assert!(
            super::pair_lock_active(true, ev.is_live_at(w - 1)),
            "3s 未満は lock 維持（false-release しない）"
        );
        // 3s 到達: not live → pair lock 解除
        assert!(
            !super::pair_lock_active(true, ev.is_live_at(w)),
            "3s 到達で lock 解除"
        );
    }

    // 製品定数の値検証: live_window = 30 tick × 100ms = 3s（G-115-245 決定文言）。
    #[test]
    fn b118_live_window_is_30_ticks_100ms_3s() {
        assert_eq!(super::HEARTBEAT_STALE_TICKS, 30, "G-115-245: 30 tick");
        assert_eq!(
            super::TICK,
            std::time::Duration::from_millis(100),
            "TICK = 100ms"
        );
        assert_eq!(
            super::live_window(),
            std::time::Duration::from_secs(3),
            "30 × 100ms = 3s"
        );
    }

    // (vi) 並行読者: beat 供給スレッド + 複数 poll スレッドで is_live() が発散しない（panic / 不整合なし）。
    //      heartbeat が回り続ける間（< window）は全 reader が live を返し続ける（swap の競合が benign）。
    #[test]
    fn b118_evaluator_concurrent_readers_no_divergence() {
        use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
        use std::sync::Arc;
        use std::thread;
        let hb = Arc::new(AtomicU32::new(0));
        // window=3s。テストは 50ms しか回さないので beat 中は常に window 内 = live。
        let ev = Arc::new(super::LivenessEvaluator::new(
            Arc::clone(&hb),
            std::time::Duration::from_secs(3),
        ));
        let stop = Arc::new(AtomicBool::new(false));
        // beat スレッド: heartbeat を高速 increment（再生中相当）。
        let beat = {
            let hb = Arc::clone(&hb);
            let stop = Arc::clone(&stop);
            thread::spawn(move || {
                while !stop.load(Ordering::Relaxed) {
                    hb.fetch_add(1, Ordering::Relaxed);
                }
            })
        };
        // 4 reader スレッド: is_live() を叩き続ける。beat 中は live が落ちてはならない。
        let mut readers = vec![];
        for _ in 0..4 {
            let ev = Arc::clone(&ev);
            let stop = Arc::clone(&stop);
            readers.push(thread::spawn(move || {
                let mut all_live = true;
                while !stop.load(Ordering::Relaxed) {
                    if !ev.is_live() {
                        all_live = false;
                    }
                }
                all_live
            }));
        }
        thread::sleep(std::time::Duration::from_millis(50));
        stop.store(true, Ordering::Relaxed);
        let _ = beat.join();
        for r in readers {
            assert!(
                r.join().unwrap(),
                "beat 中（< window）は並行 reader 全てが live を返す（divergence/panic なし）"
            );
        }
    }

    ///  C-3 ガード: PsbSummary.low / .mid は ISO 532-1 由来の
    /// psb[0..16] のみを使い、Bark 21–24 / 15.5k–20k FFT 値の影響を一切
    /// 受けないこと。
    #[test]
    fn psb_summary_low_mid_independent_of_fft_inputs() {
        let psb = [
            0.10, 0.12, 0.11, 0.13, 0.09, 0.10, 0.10, 0.10, // low (Bark 1-8)
            0.05, 0.06, 0.05, 0.04, 0.05, 0.05, 0.06, 0.05, // mid (Bark 9-16)
            0.20, 0.25, 0.22, 0.18, // 旧 high 帯域（C-3 で未使用）
        ];
        let s_a = compute_psb_summary(&psb, &[0.0; 4], 0.0);
        let s_b = compute_psb_summary(&psb, &[1.0e3, 2.0e3, 3.0e3, 4.0e3], 5.0e3);
        assert_eq!(s_a.low, s_b.low, "low must not depend on FFT inputs");
        assert_eq!(s_a.mid, s_b.mid, "mid must not depend on FFT inputs");
    }

    /// PsbSummary.high は Bark 21–24 + 15.5k–20k の合計 (linear power) を
    /// 10·log10 で dB 化した値であり、psb[16..20] の値には依存しない。
    #[test]
    fn psb_summary_high_uses_only_fft_inputs() {
        let psb_a = [0.0; 20];
        let mut psb_b = [0.0; 20];
        for v in psb_b[16..20].iter_mut() {
            *v = 0.5; // 旧 high 帯域に大きい値を入れても結果は変わらないこと
        }
        let bark21_24 = [10.0, 20.0, 30.0, 40.0];
        let ext = 50.0;
        let s_a = compute_psb_summary(&psb_a, &bark21_24, ext);
        let s_b = compute_psb_summary(&psb_b, &bark21_24, ext);
        assert_eq!(s_a.high, s_b.high, "high must NOT depend on psb[16..20]");

        // 期待値: 10·log10(10+20+30+40+50 + 1e-12) = 10·log10(150)
        let expected = 10.0 * (150.0_f64).log10();
        assert!(
            (s_a.high - expected).abs() < 1e-9,
            "high = {} expected ≈ {}",
            s_a.high,
            expected
        );
    }

    /// FFT 入力が全 0 のとき high は ≈ 10·log10(tiny) = -120 dB に張り付く。
    /// (STFT 未発火フレームが PsbSummary を出すケースの挙動定義)
    #[test]
    fn psb_summary_high_floor_when_no_fft_energy() {
        let psb = [0.0; 20];
        let s = compute_psb_summary(&psb, &[0.0; 4], 0.0);
        assert!(
            s.high < -100.0,
            "high should floor near -120 dB, got {}",
            s.high
        );
    }
}

// ── B-132 (G-115-382): drain-completion barrier 感度確証 ──────────────────────────
#[cfg(test)]
mod b132_drain_tests {
    use super::{drain_ring_into_session, DrainRingSession};
    use crate::engine::MeasureEngine;
    use crate::phase_d::stream::PhaseDStream;
    use crate::phase_d::tables::FieldType;
    use crate::record_take::RecordTakeTracker;
    use std::sync::{Arc, Mutex};

    const SR: u32 = 48_000;

    /// interleaved stereo f32 を `n_frames` 分・振幅 `amp` の 1kHz 正弦で生成。
    fn sine_stereo(n_frames: usize, amp: f32) -> Vec<f32> {
        let mut v = Vec::with_capacity(n_frames * 2);
        for i in 0..n_frames {
            let s = amp * (2.0 * std::f32::consts::PI * 1000.0 * (i as f32) / SR as f32).sin();
            v.push(s); // L
            v.push(s); // R
        }
        v
    }

    fn to_f64(f32s: &[f32]) -> Vec<f64> {
        f32s.iter().map(|&s| s as f64).collect()
    }

    /// 感度確証（barrier OFF vs ON）: 録音末尾に **より大きい peak を持つ tail** を残した状態で
    /// transport-stop 相当を起こす。
    /// - OFF（旧挙動 = ring を finalize せず破棄）: max_true_peak が body だけになり full と乖離（abs≠0 / 取りこぼし再現）。
    /// - ON（drain_ring_into_session = barrier）: tail を engine に算入して finalize → full と一致（abs<1e-6）。
    ///
    /// これは「変えるのは finalize の消費サンプル集合（tail 算入）」であり per-sample DSP 不変
    /// （engine.push/finalize は同一）であることの直接確証（G-115-381②(i)/(iii)）。
    #[test]
    fn drain_includes_tail_off_undercounts_on_matches_full() {
        // body: 1.0s @ 0.1（静か）/ tail: 0.5s @ 0.5（大きい = peak は tail にある）
        let body = sine_stereo(SR as usize, 0.1);
        let tail = sine_stereo(SR as usize / 2, 0.5);
        let body_f64 = to_f64(&body);
        let tail_f64 = to_f64(&tail); // ON 側 drain と同じ f32→f64 経路を full にも適用（精度対称）

        // full 参照: body + tail を直接 push。
        let mut eng_full = MeasureEngine::new(SR, 2).unwrap();
        let _ = eng_full.push(&body_f64);
        let _ = eng_full.push(&tail_f64);
        let full = eng_full.finalize();

        // OFF（旧 discard）: body のみ。tail は破棄され finalize に入らない。
        let mut eng_off = MeasureEngine::new(SR, 2).unwrap();
        let _ = eng_off.push(&body_f64);
        let off = eng_off.finalize();

        // ON（barrier）: body push 済 + tail を ring に入れて drain。
        let mut eng_on = MeasureEngine::new(SR, 2).unwrap();
        let _ = eng_on.push(&body_f64);
        let mut trace_engine = MeasureEngine::new(SR, 2).unwrap();
        let (mut prod, mut cons) = rtrb::RingBuffer::<f32>::new(tail.len() + 16);
        for &s in &tail {
            prod.push(s).unwrap();
        }
        let ss: Arc<Mutex<Option<crate::engine::SessionSummary>>> = Arc::new(Mutex::new(None));
        let mut chunk = Vec::new();
        let mut resampled = Vec::new();
        let mut resampler = None;
        let mut native_frames_total = SR as u64;
        let mut consumed_samples = 0_u64;
        let mut phase_d = PhaseDStream::new(FieldType::Free);
        let mut phase_d_mono = Vec::new();
        let mut phase_d_slots = Vec::new();
        let mut latest_pd = None;
        let mut record_core_pending = std::collections::VecDeque::new();
        let mut record_phase_pending = std::collections::VecDeque::new();
        let tracker = RecordTakeTracker::new();
        let mut last_capture_position_end = None;
        let ok = drain_ring_into_session(
            DrainRingSession {
                consumer: &mut cons,
                resampler: &mut resampler,
                trace_engine: &mut trace_engine,
                summary_engine: &mut eng_on,
                session_summary: &ss,
                chunk_f64: &mut chunk,
                resampled_buf: &mut resampled,
                phase_d: &mut phase_d,
                phase_d_mono_buf: &mut phase_d_mono,
                phase_d_slot_pending: &mut phase_d_slots,
                latest_pd: &mut latest_pd,
                record_core_pending: &mut record_core_pending,
                record_phase_pending: &mut record_phase_pending,
                sample_rate: SR,
                n_channels: 2,
                native_frames_total: &mut native_frames_total,
                consumed_samples: &mut consumed_samples,
                record_take_tracker: &tracker,
                last_capture_position_end: &mut last_capture_position_end,
                finalize_capture: false,
            },
            None,
        );
        assert!(ok, "drain must succeed");
        let on = (*ss.lock().unwrap()).expect("session_summary written by drain");

        let fp = full.max_true_peak.expect("full tp");
        let offp = off.max_true_peak.expect("off tp");
        let onp = on.max_true_peak.expect("on tp");

        // OFF: tail の peak を取りこぼして max_true_peak が大きく過小（0.5→0.1 amp で ~14 dB）。
        assert!(
            (fp - offp).abs() > 1.0,
            "OFF must undercount max_true_peak (full={fp:.4} off={offp:.4})"
        );
        // ON（barrier）: tail 算入で full と一致（chunk 順序差 ~1e-12 のみ）。
        assert!(
            (fp - onp).abs() < 1e-6,
            "ON (drain) must match full within 1e-6 (full={fp:.9} on={onp:.9})"
        );
    }

    /// clean path（steady-no-residual）: ring が空のとき drain は engine の現値を
    /// そのまま finalize して session_summary に書くだけで、値を乱さない（parity 不乱の保証）。
    #[test]
    fn drain_empty_ring_preserves_finalize_value() {
        let body = sine_stereo(SR as usize, 0.2);
        let body_f64 = to_f64(&body);
        let mut eng = MeasureEngine::new(SR, 2).unwrap();
        let _ = eng.push(&body_f64);
        let mut trace_engine = MeasureEngine::new(SR, 2).unwrap();
        let reference = eng.finalize();

        let (_prod, mut cons) = rtrb::RingBuffer::<f32>::new(16); // 空
        let ss: Arc<Mutex<Option<crate::engine::SessionSummary>>> = Arc::new(Mutex::new(None));
        let mut chunk = Vec::new();
        let mut resampled = Vec::new();
        let mut resampler = None;
        let mut native_frames_total = SR as u64;
        let mut consumed_samples = 0_u64;
        let mut phase_d = PhaseDStream::new(FieldType::Free);
        let mut phase_d_mono = Vec::new();
        let mut phase_d_slots = Vec::new();
        let mut latest_pd = None;
        let mut record_core_pending = std::collections::VecDeque::new();
        let mut record_phase_pending = std::collections::VecDeque::new();
        let tracker = RecordTakeTracker::new();
        let mut last_capture_position_end = None;
        let ok = drain_ring_into_session(
            DrainRingSession {
                consumer: &mut cons,
                resampler: &mut resampler,
                trace_engine: &mut trace_engine,
                summary_engine: &mut eng,
                session_summary: &ss,
                chunk_f64: &mut chunk,
                resampled_buf: &mut resampled,
                phase_d: &mut phase_d,
                phase_d_mono_buf: &mut phase_d_mono,
                phase_d_slot_pending: &mut phase_d_slots,
                latest_pd: &mut latest_pd,
                record_core_pending: &mut record_core_pending,
                record_phase_pending: &mut record_phase_pending,
                sample_rate: SR,
                n_channels: 2,
                native_frames_total: &mut native_frames_total,
                consumed_samples: &mut consumed_samples,
                record_take_tracker: &tracker,
                last_capture_position_end: &mut last_capture_position_end,
                finalize_capture: false,
            },
            None,
        );
        assert!(ok);
        let drained = (*ss.lock().unwrap()).unwrap();
        assert!(
            (reference.max_true_peak.unwrap() - drained.max_true_peak.unwrap()).abs() < 1e-9,
            "empty-ring drain must not disturb max_true_peak"
        );
        assert!(
            (reference.lufs_i.unwrap() - drained.lufs_i.unwrap()).abs() < 1e-9,
            "empty-ring drain must not disturb lufs_i"
        );
    }
}
