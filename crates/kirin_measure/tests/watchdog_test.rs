//! Watchdog Thread テスト（guardian_53 T-8 判断基準）
//!
//! 判断基準:
//! 1. Measure Thread 終了を検出 → 2秒以内に measure_alive=false
//! 2. 再起動後 → measure_alive=true に復帰
//! 3. pending_producer に新 Producer が入る（Audio Thread がスワップできる）
//! 4. watchdog_shutdown=true → Watchdog が終了する
//!
//! NOTE: IO Thread のファイル書き込みは io_thread_post_test で検証済み。
//! このテストでは IO Thread を最小構成にして MIX ディレクトリへの影響を避ける。

use kirin_measure::{
    spawn_io_thread_pre, spawn_measure_thread, spawn_watchdog, License, MeasureResult,
    RecordStateMachine, SignalState, WatchdogParams,
};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::JoinHandle;
use std::time::Duration;

/// watchdog_test 全体で共通の PRE IO Thread 起動ヘルパー。
/// B-022 段階 1 後: `spawn_io_thread_pre` の `instance_id` 引数は
/// `Arc<RwLock<String>>` 化された。テストは `Arc::new(RwLock::new(_))` で
/// 即時 wrap して渡す。
/// `project_hash` / `daw_session_id` はテスト内では検証対象外なので
/// instance_id ベースで決定論的に派生させる。
#[allow(clippy::too_many_arguments)]
fn spawn_pre_io(
    instance_id: String,
    measure_result: Arc<Mutex<MeasureResult>>,
    signal_state: Arc<AtomicU8>,
    io_shutdown: Arc<AtomicBool>,
) -> JoinHandle<()> {
    let project_hash = format!("test-proj-{}", instance_id);
    let daw_session_id = format!("test-daw-{}", instance_id);
    spawn_io_thread_pre(
        Arc::new(RwLock::new(instance_id)),
        project_hash,
        daw_session_id,
        48000,
        Arc::new(RecordStateMachine::new()),
        Arc::new(AtomicBool::new(false)),
        Arc::new(AtomicBool::new(false)),
        Arc::new(License::Unknown),
        measure_result,
        signal_state,
        io_shutdown,
        Arc::new(RwLock::new(String::new())),
        Arc::new(RwLock::new(None)),
    )
}

// ── 基本起動・停止 ────────────────────────────────────────────────────────

/// Watchdog が正常起動し、shutdown 後に join できること
#[test]
fn test_watchdog_starts_and_stops() {
    let measure_result = Arc::new(Mutex::new(MeasureResult::default()));
    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let measure_shutdown = Arc::new(AtomicBool::new(false));
    let measure_alive = Arc::new(AtomicBool::new(true));
    let pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>> =
        Arc::new(Mutex::new(None));
    let heartbeat = Arc::new(AtomicU32::new(0));

    let (producer, consumer) = rtrb::RingBuffer::<f32>::new(4096);
    let _ = producer; // Audio Thread 側として保持

    let measure_handle = spawn_measure_thread(
        consumer,
        48000,
        Arc::clone(&measure_result),
        Arc::clone(&signal_state),
        Arc::clone(&measure_shutdown),
        Arc::clone(&heartbeat),
    );

    // IO Thread は孤立したディレクトリへ書くためにユニーク id を使う
    let io_id = format!("wd-stop-{}", ts_id());
    let io_shutdown = Arc::new(AtomicBool::new(false));
    let io_handle = spawn_pre_io(
        io_id.clone(),
        Arc::clone(&measure_result),
        Arc::clone(&signal_state),
        Arc::clone(&io_shutdown),
    );

    let watchdog_shutdown = Arc::new(AtomicBool::new(false));
    let mr2 = Arc::clone(&measure_result);
    let ss2 = Arc::clone(&signal_state);
    let io_id2 = io_id.clone();

    let handle = spawn_watchdog(WatchdogParams {
        sample_rate: 48000,
        ring_capacity: 4096,
        measure_result: Arc::clone(&measure_result),
        signal_state: Arc::clone(&signal_state),
        heartbeat: Arc::clone(&heartbeat),
        measure_shutdown: Arc::clone(&measure_shutdown),
        measure_alive: Arc::clone(&measure_alive),
        pending_producer: Arc::clone(&pending_producer),
        measure_handle,
        io_shutdown: Arc::clone(&io_shutdown),
        io_handle,
        restart_io: Box::new(move |sd| {
            spawn_pre_io(io_id2.clone(), Arc::clone(&mr2), Arc::clone(&ss2), sd)
        }),
        watchdog_shutdown: Arc::clone(&watchdog_shutdown),
    });

    std::thread::sleep(Duration::from_millis(100));

    // shutdown
    watchdog_shutdown.store(true, Ordering::Relaxed);
    measure_shutdown.store(true, Ordering::Relaxed);
    io_shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("watchdog join");

    // IO Thread が PRE ファイルを削除するのを待つ
    std::thread::sleep(Duration::from_millis(150));
}

// ── Measure Thread 再起動 ─────────────────────────────────────────────────

/// Measure Thread が終了したとき Watchdog が検出・再起動し、
/// pending_producer に新しい Producer が入ること（guardian_53 T-8 判断基準）
#[test]
fn test_watchdog_detects_and_restarts_measure_thread() {
    let measure_result = Arc::new(Mutex::new(MeasureResult::default()));
    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let measure_shutdown = Arc::new(AtomicBool::new(false));
    let measure_alive = Arc::new(AtomicBool::new(true));
    let pending: Arc<Mutex<Option<rtrb::Producer<f32>>>> = Arc::new(Mutex::new(None));
    let heartbeat = Arc::new(AtomicU32::new(0));

    // 即終了する Measure Thread（shutdown=true で即 exit）
    let dying_shutdown = Arc::new(AtomicBool::new(true));
    let (_dying_producer, dying_consumer) = rtrb::RingBuffer::<f32>::new(4096);
    let dying_handle = spawn_measure_thread(
        dying_consumer,
        48000,
        Arc::clone(&measure_result),
        Arc::clone(&signal_state),
        Arc::clone(&dying_shutdown),
        Arc::clone(&heartbeat),
    );

    // is_finished() が true になるまで待つ（最大 500ms）
    let start = std::time::Instant::now();
    while !dying_handle.is_finished() && start.elapsed() < Duration::from_millis(500) {
        std::thread::sleep(Duration::from_millis(10));
    }
    assert!(
        dying_handle.is_finished(),
        "dying_handle should be finished before watchdog starts"
    );

    // IO Thread は孤立 id を使って MIX dir への影響を最小化
    let io_id = format!("wd-restart-{}", ts_id());
    let io_shutdown = Arc::new(AtomicBool::new(false));
    let io_handle = spawn_pre_io(
        io_id.clone(),
        Arc::clone(&measure_result),
        Arc::clone(&signal_state),
        Arc::clone(&io_shutdown),
    );

    let watchdog_shutdown = Arc::new(AtomicBool::new(false));
    let restart_measure_shutdown = Arc::clone(&measure_shutdown);
    let restart_io_id = io_id.clone();
    let restart_io_result = Arc::clone(&measure_result);
    let restart_io_ss = Arc::clone(&signal_state);

    // Watchdog 起動（dying_handle を渡す → 最初のチェックで検出）
    let handle = spawn_watchdog(WatchdogParams {
        sample_rate: 48000,
        ring_capacity: 4096,
        measure_result: Arc::clone(&measure_result),
        signal_state: Arc::clone(&signal_state),
        heartbeat: Arc::clone(&heartbeat),
        measure_shutdown: Arc::clone(&restart_measure_shutdown),
        measure_alive: Arc::clone(&measure_alive),
        pending_producer: Arc::clone(&pending),
        measure_handle: dying_handle,
        io_shutdown: Arc::clone(&io_shutdown),
        io_handle,
        restart_io: Box::new(move |sd| {
            spawn_pre_io(restart_io_id.clone(), Arc::clone(&restart_io_result), Arc::clone(&restart_io_ss), sd)
        }),
        watchdog_shutdown: Arc::clone(&watchdog_shutdown),
    });

    // T-8 判断基準: Measure Thread 再起動 + pending_producer 配置 を検出する。
    //
    // measure_alive=false の window は数 ms と短く、50ms ポーリングでは捕捉できない
    // ことが多い（捕捉率 ≈ 20%）。代わりに pending_producer.is_some() を使う:
    //   - Watchdog が再起動を完了すると pending_producer に Some が書かれる
    //   - Audio Thread がスワップするまで Some を保持し続ける（ポーリングで確実に検出可能）
    //   - pending_producer.is_some() かつ measure_alive=true ならば
    //     「死亡検出 → 再起動完了 → LED 青復帰」のサイクルが実行済み
    //
    // タイムアウト 4 秒（= Watchdog チェック 1 秒 + 再起動処理 + 余裕）
    let start = std::time::Instant::now();
    let mut restarted = false;
    while start.elapsed() < Duration::from_secs(4) {
        let has_producer = pending.lock().map(|g| g.is_some()).unwrap_or(false);
        if has_producer && measure_alive.load(Ordering::Relaxed) {
            restarted = true;
            break;
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    assert!(
        restarted,
        "Watchdog must detect dead Measure Thread and restart it (pending_producer=Some, measure_alive=true) within 4 seconds"
    );

    // cleanup
    watchdog_shutdown.store(true, Ordering::Relaxed);
    restart_measure_shutdown.store(true, Ordering::Relaxed);
    io_shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("watchdog join");

    // IO Thread が PRE ファイルを削除するのを待つ
    std::thread::sleep(Duration::from_millis(150));
}

/// measure_alive: Measure Thread が生きている間は true のまま
#[test]
fn test_watchdog_leaves_alive_true_when_healthy() {
    let measure_result = Arc::new(Mutex::new(MeasureResult::default()));
    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let measure_shutdown = Arc::new(AtomicBool::new(false));
    let measure_alive = Arc::new(AtomicBool::new(true));
    let pending: Arc<Mutex<Option<rtrb::Producer<f32>>>> = Arc::new(Mutex::new(None));
    let heartbeat = Arc::new(AtomicU32::new(0));

    let (_producer, consumer) = rtrb::RingBuffer::<f32>::new(4096);
    let measure_handle = spawn_measure_thread(
        consumer,
        48000,
        Arc::clone(&measure_result),
        Arc::clone(&signal_state),
        Arc::clone(&measure_shutdown),
        Arc::clone(&heartbeat),
    );

    let io_id = format!("wd-healthy-{}", ts_id());
    let io_shutdown = Arc::new(AtomicBool::new(false));
    let io_handle = spawn_pre_io(
        io_id.clone(),
        Arc::clone(&measure_result),
        Arc::clone(&signal_state),
        Arc::clone(&io_shutdown),
    );

    let watchdog_shutdown = Arc::new(AtomicBool::new(false));
    let mr2 = Arc::clone(&measure_result);
    let ss2 = Arc::clone(&signal_state);
    let io_id2 = io_id.clone();

    let handle = spawn_watchdog(WatchdogParams {
        sample_rate: 48000,
        ring_capacity: 4096,
        measure_result: Arc::clone(&measure_result),
        signal_state: Arc::clone(&signal_state),
        heartbeat: Arc::clone(&heartbeat),
        measure_shutdown: Arc::clone(&measure_shutdown),
        measure_alive: Arc::clone(&measure_alive),
        pending_producer: Arc::clone(&pending),
        measure_handle,
        io_shutdown: Arc::clone(&io_shutdown),
        io_handle,
        restart_io: Box::new(move |sd| {
            spawn_pre_io(io_id2.clone(), Arc::clone(&mr2), Arc::clone(&ss2), sd)
        }),
        watchdog_shutdown: Arc::clone(&watchdog_shutdown),
    });

    // 1.5秒待っても measure_alive は true のまま（Measure Thread は生きている）
    std::thread::sleep(Duration::from_millis(1500));
    assert!(
        measure_alive.load(Ordering::Relaxed),
        "measure_alive must stay true when Measure Thread is healthy"
    );
    assert!(
        pending.lock().unwrap().is_none(),
        "pending_producer must stay None when Measure Thread is healthy"
    );

    watchdog_shutdown.store(true, Ordering::Relaxed);
    measure_shutdown.store(true, Ordering::Relaxed);
    io_shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("watchdog join");

    std::thread::sleep(Duration::from_millis(150));
}

// ── ヘルパー ─────────────────────────────────────────────────────────────

fn ts_id() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let t = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    format!("{}-{}", t.as_secs(), t.subsec_nanos())
}
