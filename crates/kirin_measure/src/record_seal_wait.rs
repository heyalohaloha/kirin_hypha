//! The IO Thread's bounded wait for the Measure Thread's post-drain seal (B-132).
use super::*;

/// B-132 (G-115-382 共通B): Record→Watch close 時に Measure Thread の post-drain seal が
/// `seal_at_start` を超えるまで **lock-free bounded** に待つ。
///
/// P3 (2026-07-10): 固定の合計待ち時間ではなく、`record_trace_queue` が伸び続けている
/// （= Measure Thread が実際に drain を進めている）限り無進捗タイマーを都度リセットする
/// deterministic な handshake にする。offline bounce のような backlog が大きいケースでも、
/// 進捗している間は正当な処理時間として待ち続け、進捗が本当に止まったときだけ
/// `no_progress_timeout` で諦める。`absolute_timeout` は `record_trace_queue` が無い
/// legacy/fallback 呼び出し元だけに適用する。
///
/// 返値 `true`: seal が前進した（= measure が tight-drain + 最終 finalize + session_summary 書込を
/// 完了）→ bake arm は post-drain 確定スナップショットを take してよい。
/// 返値 `false`: 無進捗のまま `no_progress_timeout` 超過、または進捗監視なしで
/// `absolute_timeout` 超過（measure 死 / shutdown / stall）→ 呼出側は integrity_degraded を立てる。
///
/// teardown 安全性: atomic load + Mutex 短時間 lock のみ。通常の Record TRACE 経路は
/// 「進捗が止まったら有限時間で抜ける」。進捗監視の無い legacy/fallback 経路だけは
/// `absolute_timeout` でも抜ける。
pub fn wait_for_seal(
    record_sm: &RecordStateMachine,
    seal_at_start: u64,
    record_trace_queue: Option<&RecordTraceQueue>,
    no_progress_timeout: Duration,
    absolute_timeout: Duration,
) -> bool {
    let has_progress_detector = record_trace_queue.is_some();
    let absolute_deadline = Instant::now() + absolute_timeout;
    let mut no_progress_deadline = Instant::now() + no_progress_timeout;
    let mut last_len = trace_queue_len(record_trace_queue);
    loop {
        if record_sm.seal() > seal_at_start {
            return true;
        }
        let now = Instant::now();
        if now >= no_progress_deadline || (!has_progress_detector && now >= absolute_deadline) {
            return false;
        }
        std::thread::sleep(SEAL_WAIT_POLL);
        let len = trace_queue_len(record_trace_queue);
        if has_progress_detector && len != last_len {
            last_len = len;
            no_progress_deadline = Instant::now() + no_progress_timeout;
        }
    }
}

/// `record_trace_queue` の現在長（lock 取得失敗時は進捗なしとみなし 0 を返す）。
fn trace_queue_len(record_trace_queue: Option<&RecordTraceQueue>) -> usize {
    record_trace_queue
        .and_then(|q| q.lock().ok())
        .map(|guard| guard.len())
        .unwrap_or(0)
}
