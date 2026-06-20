//! Watchdog Thread — Measure Thread / IO Thread の自動再起動（ T-8）。
//!
//! # 再起動フロー
//! ```text
//! Watchdog (50ms ループ、1秒1回チェック)
//!   ├─ Measure Thread.is_finished() → true
//!   │     measure_alive = false         (GUI LED 黄)
//!   │     新 RingBuffer 生成
//!   │     spawn_measure_thread()
//!   │     pending_producer スロットへ新 Producer を書く
//!   │     measure_alive = true          (GUI LED 青復帰)
//!   └─ IO Thread.is_finished() → true
//!         restart_io() クロージャを呼ぶ
//! ```
//!
//! Audio Thread は process() で pending_producer を低頻度チェックし、
//! Some なら ring_producer を差し替える。

use crate::engine::SessionSummary;
use crate::record::RecordStateMachine;
use crate::{spawn_measure_thread, LivenessEvaluator, MeasureResult};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

/// Watchdog の内部ループ間隔。
/// シャットダウン応答を速くするため短め。実際のスレッドチェックは TICKS_PER_CHECK 回ごと。
const TICK: Duration = Duration::from_millis(50);

/// 何ティック毎にスレッドを検査するか（50ms × 20 = 1秒）
const TICKS_PER_CHECK: u32 = 20;

/// IO Thread のハンドル束（停止フラグ + JoinHandle）。
///
/// B-118: FFI が共有 slot（`Arc<Mutex<Option<IoThreadHandle>>>`）越しに watchdog へ io を
/// 受け渡すための型を kirin_measure に置く（watchdog が FFI 固有型を知れないため）。各 io 世代は
/// 自分の `shutdown` を持ち、再起動は新世代を生成して slot を差し替える。
pub struct IoThreadHandle {
    pub shutdown: Arc<AtomicBool>,
    pub handle: JoinHandle<()>,
}

/// IO Thread を再起動するクロージャ。新しい `IoThreadHandle`（新 shutdown を内包）を返す。
pub type RestartIoFn = Box<dyn Fn() -> IoThreadHandle + Send + 'static>;

/// IO Thread 再起動クロージャ（eager 用 / 既存 io_shutdown Arc を受けて JoinHandle を返す）。
pub type RestartIoEagerFn = Box<dyn Fn(Arc<AtomicBool>) -> JoinHandle<()> + Send + 'static>;

/// Watchdog が管理する IO Thread の所有モード。
///
/// B-118: egui は `initialize` で io を eager spawn し handle を直接渡す（既存挙動）。FFI は
/// `enable_*_writes` で io を後発 spawn するため、engine の io_thread slot を共有し、enable が
/// restart-closure slot をセットする（lazy）。
pub enum WatchdogIo {
    /// egui: spawn 済み io handle を直接所有。`io_shutdown` を共有して restart で再利用。
    Eager {
        io_shutdown: Arc<AtomicBool>,
        io_handle: JoinHandle<()>,
        restart_io: RestartIoEagerFn,
    },
    /// FFI: io は後発 spawn。watchdog は共有 slot を監視し、restart-closure slot 経由で再生成。
    /// 両 slot とも engine 側と Arc 共有（Drop / enable が触る）。
    Lazy {
        io_slot: Arc<Mutex<Option<IoThreadHandle>>>,
        io_restart_slot: Arc<Mutex<Option<RestartIoFn>>>,
    },
}

/// Watchdog Thread の起動パラメータ。
pub struct WatchdogParams {
    /// 計測スレッドのサンプルレート（再起動時の ring buffer 再生成に使う）
    pub sample_rate: u32,
    /// 計測スレッドの入力チャンネル数（1=mono / 2=stereo）。再起動後も同じ layout を維持する。
    pub n_channels: usize,
    /// ring buffer 容量（samples）
    pub ring_capacity: usize,
    /// 計測結果共有（再起動した Measure Thread に渡す）
    pub measure_result: Arc<Mutex<MeasureResult>>,
    /// SignalState 共有（再起動した Measure Thread に渡す）
    pub signal_state: Arc<AtomicU8>,
    /// B-118: 単一鮮度評価器（再起動した Measure Thread が同一評価器を読み続ける）。
    /// heartbeat は評価器が内部で観測するため、再 spawn 引数としては評価器のみ渡す。
    pub evaluator: Arc<LivenessEvaluator>,
    /// Measure Thread 停止フラグ（再起動時にリセット）
    pub measure_shutdown: Arc<AtomicBool>,
    /// Measure Thread 生存フラグ。false=停止中（LED 黄）、true=稼働中（LED 青）
    pub measure_alive: Arc<AtomicBool>,
    /// Audio Thread に新しい Producer を渡すスロット。
    /// Watchdog が Some を書き、process() が取り出す。
    pub pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    /// 最初の Measure Thread の JoinHandle（watchdog が所有・管理する）
    pub measure_handle: JoinHandle<()>,
    /// B-118: IO Thread の所有モード（egui=Eager / FFI=Lazy）。
    pub io: WatchdogIo,
    /// Watchdog 自身の停止フラグ
    pub watchdog_shutdown: Arc<AtomicBool>,
    /// B-118: シャットダウン時に io→measure を join するか。
    /// egui=false（既存どおり detach）/ FFI=true（free 前に全 thread join＝共有 Arc UAF 回避）。
    pub join_on_shutdown: bool,
    /// B-043: Record mode 状態機械（再起動した Measure Thread に渡す）
    pub record_sm: Arc<RecordStateMachine>,
    /// B-043: Record セッション集計値共有スロット（再起動した Measure Thread に渡す）
    pub session_summary: Arc<Mutex<Option<SessionSummary>>>,
}

/// Watchdog Thread を起動して JoinHandle を返す。
pub fn spawn_watchdog(params: WatchdogParams) -> JoinHandle<()> {
    thread::spawn(move || {
        let WatchdogParams {
            sample_rate,
            n_channels,
            ring_capacity,
            measure_result,
            signal_state,
            evaluator,
            measure_shutdown,
            measure_alive,
            pending_producer,
            measure_handle: initial_m,
            io,
            watchdog_shutdown,
            join_on_shutdown,
            record_sm,
            session_summary,
        } = params;

        let mut cur_measure = initial_m;
        let mut io = io; // io 世代を更新するため mutable
        let mut tick: u32 = 0;

        log::info!("[Watchdog] started");

        loop {
            thread::sleep(TICK);

            // シャットダウン確認（50ms 精度で素早く応答）
            if watchdog_shutdown.load(Ordering::Relaxed) {
                break;
            }

            tick = tick.wrapping_add(1);
            if !tick.is_multiple_of(TICKS_PER_CHECK) {
                continue; // 1秒待つ
            }

            // ── Measure Thread チェック ─────────────────────────────
            // B-118: シャットダウン中は再起動しない（join_on_shutdown の post-loop join が
            // 復活した thread で hang するのを防ぐ）。
            if cur_measure.is_finished() && !watchdog_shutdown.load(Ordering::Relaxed) {
                log::warn!("[Watchdog] Measure Thread terminated unexpectedly. Restarting...");

                // LED 黄
                measure_alive.store(false, Ordering::Relaxed);

                // shutdown フラグをリセット（panic では true にならないが念のため）
                measure_shutdown.store(false, Ordering::Relaxed);

                // 新しい ring buffer と Measure Thread を生成
                let (producer, consumer) = rtrb::RingBuffer::new(ring_capacity);
                cur_measure = spawn_measure_thread(
                    consumer,
                    sample_rate,
                    n_channels,
                    Arc::clone(&measure_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&measure_shutdown),
                    Arc::clone(&evaluator),
                    Arc::clone(&record_sm),
                    Arc::clone(&session_summary),
                );

                // Audio Thread に新しい Producer を渡す（process() が取り出す）
                match pending_producer.lock() {
                    Ok(mut slot) => {
                        *slot = Some(producer);
                    }
                    Err(e) => {
                        log::error!("[Watchdog] pending_producer Mutex poisoned: {}", e);
                    }
                }

                // LED 青復帰
                measure_alive.store(true, Ordering::Relaxed);
                log::info!("[Watchdog] Measure Thread restarted successfully");
            }

            // ── IO Thread チェック（B-118: Eager=egui / Lazy=FFI）──
            match &mut io {
                WatchdogIo::Eager {
                    io_shutdown,
                    io_handle,
                    restart_io,
                } => {
                    if io_handle.is_finished() && !watchdog_shutdown.load(Ordering::Relaxed) {
                        log::warn!(
                            "[Watchdog] IO Thread terminated unexpectedly. Restarting (eager)..."
                        );
                        // shutdown Arc を plugin 側と共有するため既存 io_shutdown を再利用。
                        io_shutdown.store(false, Ordering::Relaxed);
                        *io_handle = restart_io(Arc::clone(io_shutdown));
                        log::info!("[Watchdog] IO Thread restarted successfully");
                    }
                }
                WatchdogIo::Lazy {
                    io_slot,
                    io_restart_slot,
                } => {
                    // enable がまだ io を slot に置いていなければ None（監視対象なし）。
                    if let Ok(mut slot) = io_slot.lock() {
                        let dead = slot
                            .as_ref()
                            .map(|h| h.handle.is_finished())
                            .unwrap_or(false);
                        if dead && !watchdog_shutdown.load(Ordering::Relaxed) {
                            log::warn!(
                                "[Watchdog] IO Thread terminated unexpectedly. Restarting (lazy)..."
                            );
                            if let Ok(rs) = io_restart_slot.lock() {
                                if let Some(restart) = rs.as_ref() {
                                    *slot = Some(restart()); // 新世代（新 shutdown 内包）
                                    log::info!("[Watchdog] IO Thread restarted successfully");
                                }
                            }
                        }
                    }
                }
            }
        }

        // B-118: join_on_shutdown=true（FFI）は free 前に io→measure 順で join する
        // （io が共有 Arc を読むため先 / その後 engine が安全に解放）。egui=false は detach（既存）。
        if join_on_shutdown {
            match io {
                WatchdogIo::Eager { io_handle, .. } => {
                    let _ = io_handle.join();
                }
                WatchdogIo::Lazy { io_slot, .. } => {
                    if let Ok(mut slot) = io_slot.lock() {
                        if let Some(h) = slot.take() {
                            let _ = h.handle.join();
                        }
                    }
                }
            }
            let _ = cur_measure.join();
        }

        log::info!("[Watchdog] terminated");
    })
}
