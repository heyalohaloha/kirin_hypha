//! kirin_hypha_ffi — Kirin Hypha JUCE 移植 Phase 1 の C ABI ラッパ。
//!
//! 方式 B2: 検証済み Rust ランタイム(`kirin_measure`)を **無変更** で C ABI に包む。
//! C++/JUCE 側に DSP・計測ロジックを一切移さない（計測器は精度が製品そのもの）。
//!
//! # Phase 1 スコープ（Daisuke 確定 / 選択肢1）
//! - 実装: `create` / `set_signal_state` / `push_samples` / `poll_result`（RT メトリクス）/ `destroy`。
//! - `poll_session`(LUFS-I/LRA/max_true_peak) は **symbol のみ・常に false**。SessionSummary は
//!   `engine.finalize()` 由来で Record(=Phase 3) があって初めて成立する量のため、Phase 1 では
//!   埋めない（ABI を Phase 3 で壊さないため symbol は残す）。
//! - 触れない: Record / plugin_data / preset / license / PRE/POST ペアリング / IO(pre|post.json) /
//!   state chunk。これらに依存する関数を足さない。
//!
//! # スレッドモデル（本番 hypha_pre/post と同一の入口を使う）
//! `create` は本番の実運用入口 `kirin_measure::spawn_measure_thread`(measure_thread.rs:59) で
//! Measure Thread を起動する。Phase 1 では IO Thread / Watchdog は立てない（RT 計測に不要）。
//! - `push_samples`: **Audio Thread 単独**。rtrb Producer への lock-free push + heartbeat++。
//!   アロケーション/lock/syscall なし（RT-safe）。
//! - `poll_result` : **UI Thread**。`Arc<Mutex<MeasureResult>>` を `try_lock`（非ブロッキング）。
//!
//! ## heartbeat（必須配線）
//! Measure Thread は heartbeat が ~200ms 変化しないと signal_state を Inactive に上書きし結果を
//! clear する(measure_thread.rs:160-169)。本番は host の `process()` が毎回 `heartbeat.fetch_add(1)`
//! していた(hypha_pre.rs:390)。本 FFI では **`push_samples` が heartbeat を進める**。

use std::cell::UnsafeCell;
use std::os::raw::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, AtomicU64, AtomicU8, AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;

use kirin_measure::engine::SessionSummary;
use kirin_measure::{
    spawn_measure_thread, store_signal_state, MeasureResult, PsbSummary, RecordStateMachine,
    SignalState, N_CHANNELS, RING_BUFFER_SECONDS,
};

// ─────────────────────────────────────────────────────────────────────────────
// Rust-safe core（tests/parity.rs はこの API 経由で FFI を駆動する）
// ─────────────────────────────────────────────────────────────────────────────

/// RT 計測ランタイムのハンドル。C ABI からは不透明ポインタ。
pub struct KirinHyphaEngine {
    /// Audio Thread → Measure Thread の rtrb Producer。
    /// `UnsafeCell` 越しに **Audio Thread 単独** で push する（SPSC 契約）。
    ring_producer: UnsafeCell<rtrb::Producer<f32>>,
    /// Measure Thread が 100ms cadence で更新、UI Thread が読む。
    measure_result: Arc<Mutex<MeasureResult>>,
    /// Phase 1 では never written（Record 非依存 → 常に None）。
    session_summary: Arc<Mutex<Option<SessionSummary>>>,
    /// Audio Thread が宣言する信号状態（Measure Thread が読む）。
    signal_state: Arc<AtomicU8>,
    /// Measure Thread 停止フラグ（destroy でセット → join）。
    shutdown: Arc<AtomicBool>,
    /// process() 相当の heartbeat（push_samples が進める）。
    heartbeat: Arc<AtomicU32>,
    /// Record 状態機械。Phase 1 では Watch 固定（never recording）のダミー。
    _record_sm: Arc<RecordStateMachine>,
    /// Measure Thread の JoinHandle（drop で join）。
    measure_handle: Option<JoinHandle<()>>,
    /// ring 満杯で push できなかった回数（§8 RT-safety 検証用 / FFI 側のみ）。
    push_overflow: AtomicU64,
}

// SAFETY: `ring_producer`(UnsafeCell<rtrb::Producer>) は push_samples からのみ触れ、
// その push_samples は「Audio Thread 単独」という FFI 契約で単一スレッドアクセスに限定される。
// 他の全フィールドは Arc<Mutex>/Arc<Atomic>/AtomicU64 で Sync。よって `&KirinHyphaEngine` を
// Audio/UI 2 スレッドで共有しても（契約を守る限り）健全。
unsafe impl Sync for KirinHyphaEngine {}
// SAFETY: 内部状態はスレッド間移動可能（Producer/Arc は Send）。
unsafe impl Send for KirinHyphaEngine {}

impl KirinHyphaEngine {
    /// ランタイムを生成し Measure Thread を起動する。
    ///
    /// `sample_rate` ≠ 48000 のときの 48k 変換は Measure Thread 内 `ResamplerTo48k` が
    /// 既存どおり担う（新規変換コードは書かない / measure_thread.rs:82-101）。
    /// `num_channels` は stereo 前提（N_CHANNELS=2）。
    pub fn new(sample_rate: u32, _num_channels: u32) -> Self {
        // 本番 hypha_pre.rs:282-284 と同一の容量計算。
        let capacity = (sample_rate as usize) * RING_BUFFER_SECONDS * N_CHANNELS;
        let (producer, consumer) = rtrb::RingBuffer::new(capacity);

        let measure_result = Arc::new(Mutex::new(MeasureResult::default()));
        let session_summary: Arc<Mutex<Option<SessionSummary>>> = Arc::new(Mutex::new(None));
        let signal_state = Arc::new(AtomicU8::new(SignalState::Inactive as u8));
        let shutdown = Arc::new(AtomicBool::new(false));
        let heartbeat = Arc::new(AtomicU32::new(0));
        // Watch 固定のダミー（Record に遷移させない → finalize() は呼ばれない）。
        let record_sm = Arc::new(RecordStateMachine::new());

        let measure_handle = spawn_measure_thread(
            consumer,
            sample_rate,
            Arc::clone(&measure_result),
            Arc::clone(&signal_state),
            Arc::clone(&shutdown),
            Arc::clone(&heartbeat),
            Arc::clone(&record_sm),
            Arc::clone(&session_summary),
        );

        Self {
            ring_producer: UnsafeCell::new(producer),
            measure_result,
            session_summary,
            signal_state,
            shutdown,
            heartbeat,
            _record_sm: record_sm,
            measure_handle: Some(measure_handle),
            push_overflow: AtomicU64::new(0),
        }
    }

    /// 信号状態を設定する。引数は **C ABI コード**（0=Inactive 1=Active 2=Bypassed）。
    /// 内部 `SignalState` enum（Active=0/Bypassed=1/Inactive=2）へ翻訳する。
    pub fn set_signal_state(&self, abi_state: u8) {
        let s = match abi_state {
            1 => SignalState::Active,
            2 => SignalState::Bypassed,
            _ => SignalState::Inactive,
        };
        store_signal_state(&self.signal_state, s);
    }

    /// interleaved f32 サンプルを供給する（Audio Thread 単独・RT-safe）。
    ///
    /// 責務はこの 2 つのみ:
    /// 1. heartbeat を進める（200ms stall override 回避）。
    /// 2. interleaved サンプルを rtrb に push（満杯時は drop。本番 process() の
    ///    `let _ = producer.push(*sample)` と同挙動 / hypha_pre.rs:410）。
    ///
    /// stereo 前提（`num_channels` ≠ 2 のブロックは push しない＝R-28 機能的沈黙）。
    /// `interleaved.len()` は `num_frames * num_channels` を想定。
    pub fn push_samples(&self, interleaved: &[f32], num_channels: u32) {
        // (1) heartbeat は常に進める（空ブロック keepalive でも Active を維持できる）。
        self.heartbeat.fetch_add(1, Ordering::Relaxed);

        // (2) stereo 以外は既存 Rust の前提（2ch interleaved）に合わないため push しない。
        if num_channels != N_CHANNELS as u32 {
            return;
        }

        // SAFETY: push_samples は Audio Thread 単独という FFI 契約。Producer への
        // 排他アクセスは単一スレッドに限定される（SPSC）。
        let producer = unsafe { &mut *self.ring_producer.get() };
        for &s in interleaved {
            if producer.push(s).is_err() {
                self.push_overflow.fetch_add(1, Ordering::Relaxed);
            }
        }
    }

    /// 最新の RT 計測結果を取得（UI Thread / try_lock 非ブロッキング）。
    /// ロック競合中・未計測時は `None`。
    pub fn poll_result(&self) -> Option<MeasureResult> {
        match self.measure_result.try_lock() {
            Ok(g) => Some(g.clone()),
            Err(_) => None,
        }
    }

    /// セッション集計の取得。**Phase 1 では常に `None`**（SessionSummary は Record 経路でのみ
    /// 充填され、Phase 1 は Record 非依存）。Phase 3 で Record を contract に足した時に有効化する。
    pub fn poll_session(&self) -> Option<SessionSummary> {
        match self.session_summary.try_lock() {
            Ok(g) => *g,
            Err(_) => None,
        }
    }

    /// ring 満杯で drop した push 数（§8 RT-safety 検証用）。
    pub fn overflow_count(&self) -> u64 {
        self.push_overflow.load(Ordering::Relaxed)
    }
}

impl Drop for KirinHyphaEngine {
    fn drop(&mut self) {
        self.shutdown.store(true, Ordering::Relaxed);
        if let Some(h) = self.measure_handle.take() {
            let _ = h.join();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C ABI（§3 の契約。Option<f64> は NaN sentinel で表す）
// ─────────────────────────────────────────────────────────────────────────────

/// `KirinMeasureResult` — RT 計測結果（C struct）。Option は NaN で表す。
#[repr(C)]
pub struct KirinMeasureResult {
    pub lufs_m: f64,
    pub true_peak: f64,
    pub crest: f64,
    pub psr: f64,
    pub n_prime_total: f64,
    pub sharpness: f64,
    pub psb_low: f64,
    pub psb_mid: f64,
    pub psb_high: f64,
    pub n_prime: [f64; 20],
    pub psb_bark: [f64; 20],
}

/// `KirinSessionSummary` — セッション集計（C struct）。Phase 1 では未充填。
#[repr(C)]
pub struct KirinSessionSummary {
    pub lufs_i: f64,
    pub lra: f64,
    pub max_true_peak: f64,
}

#[inline]
fn opt_f64(v: Option<f64>) -> f64 {
    v.unwrap_or(f64::NAN)
}

#[inline]
fn opt_arr20(v: Option<[f64; 20]>) -> [f64; 20] {
    v.unwrap_or([f64::NAN; 20])
}

fn to_c_result(r: &MeasureResult) -> KirinMeasureResult {
    let (low, mid, high) = match &r.psb_summary {
        Some(PsbSummary { low, mid, high }) => (*low, *mid, *high),
        None => (f64::NAN, f64::NAN, f64::NAN),
    };
    KirinMeasureResult {
        lufs_m: opt_f64(r.lufs_m),
        true_peak: opt_f64(r.true_peak),
        crest: opt_f64(r.crest),
        psr: opt_f64(r.psr),
        n_prime_total: opt_f64(r.n_prime_total),
        sharpness: opt_f64(r.sharpness),
        psb_low: low,
        psb_mid: mid,
        psb_high: high,
        n_prime: opt_arr20(r.n_prime),
        psb_bark: opt_arr20(r.psb_bark),
    }
}

/// ランタイムを生成して不透明ポインタを返す（失敗時 null は返さない）。
///
/// # Safety
/// 返り値は `kirin_hypha_destroy` でのみ解放すること。
#[no_mangle]
pub extern "C" fn kirin_hypha_create(sample_rate: u32, num_channels: u32) -> *mut KirinHyphaEngine {
    // panic を C ABI 境界で止める。panic 時は null を返す（UB 回避）。論理は変えない。
    catch_unwind(AssertUnwindSafe(|| {
        Box::into_raw(Box::new(KirinHyphaEngine::new(sample_rate, num_channels)))
    }))
    .unwrap_or(std::ptr::null_mut())
}

/// 信号状態を設定（0=Inactive 1=Active 2=Bypassed）。
///
/// # Safety
/// `handle` は `kirin_hypha_create` の戻り値（非 null・未解放）であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_signal_state(handle: *mut KirinHyphaEngine, state: u8) {
    // panic 捕捉時は no-op（音声経路に影響させない）。
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).set_signal_state(state) };
    }));
}

/// interleaved f32 サンプルを供給（Audio Thread 単独・RT-safe）。
///
/// # Safety
/// `handle` は有効なハンドル。`interleaved` は `num_frames * num_channels` 個の f32 を指す
/// （`num_frames == 0` のときは null 可＝keepalive）。Audio Thread からのみ呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_push_samples(
    handle: *mut KirinHyphaEngine,
    interleaved: *const f32,
    num_frames: usize,
    num_channels: u32,
) {
    // panic 捕捉時は no-op（Audio Thread / 音声素通しに影響させない）。
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        let engine = unsafe { &*handle };
        let len = num_frames.saturating_mul(num_channels as usize);
        if len == 0 || interleaved.is_null() {
            // 0-frame keepalive（heartbeat のみ進める）。
            engine.push_samples(&[], num_channels);
            return;
        }
        let slice = unsafe { std::slice::from_raw_parts(interleaved, len) };
        engine.push_samples(slice, num_channels);
    }));
}

/// 最新 RT 計測結果を `out` に書く。値があれば true、未計測/競合なら false。
///
/// # Safety
/// `handle` は有効なハンドル、`out` は書込可能な `KirinMeasureResult`。UI Thread から呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_result(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinMeasureResult,
) -> bool {
    // panic 捕捉時は false（out は書かない）。
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        match unsafe { (*handle).poll_result() } {
            Some(r) => {
                unsafe { *out = to_c_result(&r) };
                true
            }
            None => false,
        }
    }))
    .unwrap_or(false)
}

/// セッション集計を `out` に書く。**Phase 1 では常に false**（symbol のみ）。
///
/// # Safety
/// `handle`/`out` は有効。Phase 1 では `out` を書かず false を返す（Phase 3 で有効化）。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_session(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinSessionSummary,
) -> bool {
    let _ = out;
    // panic 捕捉時も false。
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        // Phase 1: SessionSummary は Record 経路でのみ充填されるため常に None → false。
        debug_assert!(unsafe { (*handle).poll_session() }.is_none());
        false
    }))
    .unwrap_or(false)
}

/// ランタイムを破棄（shutdown → Measure Thread join）。
///
/// # Safety
/// `handle` は `kirin_hypha_create` の戻り値で、以後二重解放しないこと。null 可。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_destroy(handle: *mut KirinHyphaEngine) {
    // panic 捕捉時は no-op（二重解放はしない）。
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        drop(unsafe { Box::from_raw(handle) });
    }));
}

// `c_void` を未使用警告なく保持（将来 opaque alias 用の置き場）。
#[doc(hidden)]
pub type _KirinHyphaOpaque = c_void;
