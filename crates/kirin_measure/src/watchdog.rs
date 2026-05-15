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

use crate::{spawn_measure_thread, MeasureResult};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

/// Watchdog の内部ループ間隔。
/// シャットダウン応答を速くするため短め。実際のスレッドチェックは TICKS_PER_CHECK 回ごと。
const TICK: Duration = Duration::from_millis(50);

/// 何ティック毎にスレッドを検査するか（50ms × 20 = 1秒）
const TICKS_PER_CHECK: u32 = 20;

/// Watchdog Thread の起動パラメータ。
pub struct WatchdogParams {
    /// 計測スレッドのサンプルレート（再起動時の ring buffer 再生成に使う）
    pub sample_rate: u32,
    /// ring buffer 容量（samples）
    pub ring_capacity: usize,
    /// 計測結果共有（再起動した Measure Thread に渡す）
    pub measure_result: Arc<Mutex<MeasureResult>>,
    /// SignalState 共有（再起動した Measure Thread に渡す）
    pub signal_state: Arc<AtomicU8>,
    /// Audio Thread heartbeat カウンタ（再起動した Measure Thread に渡す）
    pub heartbeat: Arc<AtomicU32>,
    /// Measure Thread 停止フラグ（再起動時にリセット）
    pub measure_shutdown: Arc<AtomicBool>,
    /// Measure Thread 生存フラグ。false=停止中（LED 黄）、true=稼働中（LED 青）
    pub measure_alive: Arc<AtomicBool>,
    /// Audio Thread に新しい Producer を渡すスロット。
    /// Watchdog が Some を書き、process() が取り出す。
    pub pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    /// 最初の Measure Thread の JoinHandle（watchdog が所有・管理する）
    pub measure_handle: JoinHandle<()>,
    /// IO Thread 停止フラグ（再起動時にリセット）
    pub io_shutdown: Arc<AtomicBool>,
    /// 最初の IO Thread の JoinHandle（watchdog が所有・管理する）
    pub io_handle: JoinHandle<()>,
    /// IO Thread 再起動クロージャ（PRE / POST で異なる実装を capture）
    /// 新しい io_shutdown Arc を受け取り、JoinHandle を返す。
    pub restart_io: Box<dyn Fn(Arc<AtomicBool>) -> JoinHandle<()> + Send + 'static>,
    /// Watchdog 自身の停止フラグ
    pub watchdog_shutdown: Arc<AtomicBool>,
}

/// Watchdog Thread を起動して JoinHandle を返す。
pub fn spawn_watchdog(params: WatchdogParams) -> JoinHandle<()> {
    thread::spawn(move || {
        let WatchdogParams {
            sample_rate,
            ring_capacity,
            measure_result,
            signal_state,
            heartbeat,
            measure_shutdown,
            measure_alive,
            pending_producer,
            measure_handle: initial_m,
            io_shutdown,
            io_handle: initial_io,
            restart_io,
            watchdog_shutdown,
        } = params;

        let mut cur_measure = initial_m;
        let mut cur_io = initial_io;
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
            if cur_measure.is_finished() {
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
                    Arc::clone(&measure_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&measure_shutdown),
                    Arc::clone(&heartbeat),
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

            // ── IO Thread チェック ──────────────────────────────────
            if cur_io.is_finished() {
                log::warn!("[Watchdog] IO Thread terminated unexpectedly. Restarting...");

                // 新しい shutdown フラグを生成（古い Arc は以前のスレッドが保持済み）
                let new_io_shutdown = Arc::new(AtomicBool::new(false));

                // shutdown Arc を plugin 側と共有するため、既存の io_shutdown を再利用する
                io_shutdown.store(false, Ordering::Relaxed);
                cur_io = restart_io(Arc::clone(&io_shutdown));
                let _ = new_io_shutdown; // 未使用 lint 抑制

                log::info!("[Watchdog] IO Thread restarted successfully");
            }
        }

        log::info!("[Watchdog] terminated");
    })
}
