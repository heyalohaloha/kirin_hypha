//! Heartbeat 200ms × 2 → Inactive override の挙動テスト。
//!
//! guardian_53 / SS-8 の heartbeat stall detection は Studio One 等
//! バイパス時に process() を停止する DAW に対応するための機構。
//! Measure Thread が `HEARTBEAT_STALE_THRESHOLD = 2` 回連続で
//! 同一 heartbeat を読み続けたら（= 200ms 無更新）、SignalState を
//! 強制的に Inactive に書き戻す。
//!
//! guardian_61 C-4: STFT 統合 (PhaseDStream) 後も heartbeat 機構が
//! 正しく動作することを直接検証する。
//!
//! 注意: このテストは並列 CPU contention の影響を受けにくい設計
//! （positive な遷移を検出する: stall → Inactive override）。
//! test_full_pipeline_tp_via_measure_thread と異なり、heartbeat 更新
//! ループの周期に依存しない。

use kirin_measure::{
    load_signal_state, spawn_measure_thread, MeasureResult, SignalState,
};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

/// heartbeat を更新せず 400 ms 待つ → SignalState が Inactive に上書きされる。
#[test]
fn heartbeat_stall_overrides_to_inactive() {
    let result = Arc::new(Mutex::new(MeasureResult::default()));
    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let shutdown = Arc::new(AtomicBool::new(false));
    let heartbeat = Arc::new(AtomicU32::new(0));

    let (_producer, consumer) = rtrb::RingBuffer::<f32>::new(4096);
    let handle = spawn_measure_thread(
        consumer,
        48000,
        Arc::clone(&result),
        Arc::clone(&signal_state),
        Arc::clone(&shutdown),
        Arc::clone(&heartbeat),
    );

    // ── heartbeat を一切更新せずに 400 ms 待つ。──
    // Measure Thread は LOOP_SLEEP=100ms × HEARTBEAT_STALE_THRESHOLD=2 で
    // 200ms 経過後に Inactive 上書きを行う。安全側の余裕として 400ms 待つ。
    let start = Instant::now();
    let mut overridden = false;
    while start.elapsed() < Duration::from_millis(800) {
        if load_signal_state(&signal_state) == SignalState::Inactive {
            overridden = true;
            break;
        }
        thread::sleep(Duration::from_millis(20));
    }

    shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("measure thread join");

    assert!(
        overridden,
        "Measure Thread must override SignalState to Inactive after heartbeat stall (>200ms)"
    );
}

/// heartbeat を 50ms ごとに更新し続ければ Active を維持する。
#[test]
fn heartbeat_updates_keep_active() {
    let result = Arc::new(Mutex::new(MeasureResult::default()));
    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let shutdown = Arc::new(AtomicBool::new(false));
    let heartbeat = Arc::new(AtomicU32::new(0));

    let (_producer, consumer) = rtrb::RingBuffer::<f32>::new(4096);
    let handle = spawn_measure_thread(
        consumer,
        48000,
        Arc::clone(&result),
        Arc::clone(&signal_state),
        Arc::clone(&shutdown),
        Arc::clone(&heartbeat),
    );

    // ── 50ms ごとに heartbeat 更新 × 12 回 = 600ms ──
    // この期間中、SignalState が Inactive に書き換わってはいけない。
    // 並列 CPU contention で heartbeat 更新が稀に遅延しても OK
    // （要求は「更新し続ける限り Active」なので 1 度の Inactive 検出で fail）。
    let mut saw_inactive = false;
    for _ in 0..12 {
        heartbeat.fetch_add(1, Ordering::Relaxed);
        thread::sleep(Duration::from_millis(50));
        if load_signal_state(&signal_state) == SignalState::Inactive {
            saw_inactive = true;
            break;
        }
    }

    shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("measure thread join");

    assert!(
        !saw_inactive,
        "SignalState must stay Active while heartbeat is being updated every 50ms"
    );
}

/// stall → Inactive 後、heartbeat 再開で stall_count がリセットされ、
/// 以降 SignalState=Active に戻ったときに新たな計測サイクルが開始可能。
///
/// このテストは「stall recovery」の Measure Thread 側ロジック健全性を
/// 確認する。Audio Thread (本テストでは test thread) が Active を再宣言した後、
/// 次の stall 判定までに 200ms 以上かかること（リセットが効いている証拠）を
/// 検証する。
#[test]
fn heartbeat_resume_resets_stale_counter() {
    let result = Arc::new(Mutex::new(MeasureResult::default()));
    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let shutdown = Arc::new(AtomicBool::new(false));
    let heartbeat = Arc::new(AtomicU32::new(0));

    let (_producer, consumer) = rtrb::RingBuffer::<f32>::new(4096);
    let handle = spawn_measure_thread(
        consumer,
        48000,
        Arc::clone(&result),
        Arc::clone(&signal_state),
        Arc::clone(&shutdown),
        Arc::clone(&heartbeat),
    );

    // ── Phase 1: stall → Inactive 上書きを待つ ──
    let start = Instant::now();
    while start.elapsed() < Duration::from_millis(800) {
        if load_signal_state(&signal_state) == SignalState::Inactive {
            break;
        }
        thread::sleep(Duration::from_millis(20));
    }
    assert_eq!(
        load_signal_state(&signal_state),
        SignalState::Inactive,
        "phase 1: must override to Inactive after stall"
    );

    // ── Phase 2: Active 再宣言 + heartbeat 連続更新 ──
    // Audio Thread が process() 再開した状況を模倣。
    signal_state.store(SignalState::Active as u8, Ordering::Relaxed);
    let resume_start = Instant::now();
    let mut stayed_active_for_ms: u128 = 0;
    while resume_start.elapsed() < Duration::from_millis(500) {
        heartbeat.fetch_add(1, Ordering::Relaxed);
        thread::sleep(Duration::from_millis(50));
        let s = load_signal_state(&signal_state);
        if s == SignalState::Active {
            stayed_active_for_ms = resume_start.elapsed().as_millis();
        } else {
            break;
        }
    }

    shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("measure thread join");

    // 連続 heartbeat 更新中に Active が安定して維持されたこと
    // (stall_count が正しくリセットされた証拠)。最低 300ms は維持される想定。
    assert!(
        stayed_active_for_ms >= 300,
        "after resume, Active must be sustained while heartbeat updates (got {}ms)",
        stayed_active_for_ms
    );
}
