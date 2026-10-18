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
    wait_for_seal_with(
        record_sm,
        seal_at_start,
        record_trace_queue,
        (no_progress_timeout, absolute_timeout),
        Instant::now,
        || std::thread::sleep(SEAL_WAIT_POLL),
    )
}

/// [`wait_for_seal`] on an injected clock and pause, so the wait is tested by the drain's progress
/// instead of by how long a loaded machine takes to sleep.
fn wait_for_seal_with(
    record_sm: &RecordStateMachine,
    seal_at_start: u64,
    record_trace_queue: Option<&RecordTraceQueue>,
    (no_progress_timeout, absolute_timeout): (Duration, Duration),
    mut now: impl FnMut() -> Instant,
    mut pause: impl FnMut(),
) -> bool {
    let has_progress_detector = record_trace_queue.is_some();
    let absolute_deadline = now() + absolute_timeout;
    let mut no_progress_deadline = now() + no_progress_timeout;
    // TRACE samples and drained Record-lane chunks are both progress: a Keep's later passes
    // reach only the summary and add no TRACE, yet the drain is still advancing.
    let progress = || {
        (
            trace_queue_len(record_trace_queue),
            record_sm.measure_progress(),
        )
    };
    let mut last = progress();
    loop {
        if record_sm.seal() > seal_at_start {
            return true;
        }
        let current = now();
        if current >= no_progress_deadline
            || (!has_progress_detector && current >= absolute_deadline)
        {
            return false;
        }
        pause();
        let now_progress = progress();
        if has_progress_detector && now_progress != last {
            last = now_progress;
            no_progress_deadline = now() + no_progress_timeout;
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;

    /// A simulated drain: every pause is 300 ms on the clock, and the drain advances one chunk
    /// per pause (`steps` of them) before it seals. Nothing here depends on how fast this
    /// machine sleeps.
    fn drain_then_seal(steps: u32, summary_only: bool) -> bool {
        let sm = RecordStateMachine::new();
        let start = sm.seal();
        let queue = new_record_trace_queue();
        let clock = Cell::new(Instant::now());
        let paused = Cell::new(0_u32);
        wait_for_seal_with(
            &sm,
            start,
            Some(&queue),
            (Duration::from_secs(1), Duration::from_secs(5)),
            || clock.get(),
            || {
                clock.set(clock.get() + Duration::from_millis(300));
                paused.set(paused.get() + 1);
                if paused.get() <= steps {
                    if summary_only {
                        sm.note_measure_progress();
                    }
                } else if paused.get() == steps + 1 {
                    sm.bump_seal();
                }
            },
        )
    }

    /// A Keep's later passes reach only the summary: the drain advances while the TRACE queue
    /// stays still. Drained Record-lane chunks keep the wait alive, as growing TRACE does.
    #[test]
    fn drained_summary_only_chunks_keep_the_seal_wait_alive() {
        assert!(
            drain_then_seal(20, true),
            "a progressing drain without TRACE must reach its seal"
        );
    }

    /// A drain that stops advancing ends the wait after the no-progress bound, not at its seal.
    #[test]
    fn a_drain_that_stops_advancing_ends_the_wait_without_a_seal() {
        assert!(!drain_then_seal(20, false));
    }
}
