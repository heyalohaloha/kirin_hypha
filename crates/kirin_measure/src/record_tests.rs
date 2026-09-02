use super::*;
use std::sync::Arc;
use std::thread;

#[test]
fn initial_state_is_watch() {
    let sm = RecordStateMachine::new();
    assert_eq!(sm.current(), RecordState::Watch);
    assert!(!sm.is_recording());
}

#[test]
fn os_license_allows_record_transition() {
    let sm = RecordStateMachine::new();
    assert_eq!(sm.try_enter_record(License::Os), Ok(()));
    assert_eq!(sm.current(), RecordState::Record);
    assert!(sm.is_recording());
}

#[test]
fn record_started_at_is_stored_and_cleared() {
    let sm = RecordStateMachine::new();
    assert_eq!(sm.record_started_at_ms(), 0);
    assert_eq!(sm.record_started_at_position_samples(), None);
    assert_eq!(sm.record_expected_end_position_samples(), None);
    assert_eq!(sm.record_session_id(), None);
    assert_eq!(sm.measure_ready_generation(), 0);
    assert_eq!(
        sm.try_enter_record_started_at_clock(License::Os, 1_725_000_123_456, Some(96_000)),
        Ok(())
    );
    assert_eq!(sm.record_started_at_ms(), 1_725_000_123_456);
    assert_eq!(sm.record_started_at_position_samples(), Some(96_000));
    assert_eq!(sm.record_expected_end_position_samples(), None);
    assert_eq!(sm.record_session_id(), None);
    sm.mark_measure_ready(sm.generation());
    assert_eq!(sm.measure_ready_generation(), sm.generation());
    sm.exit_record();
    assert_eq!(sm.record_started_at_ms(), 0);
    assert_eq!(sm.record_started_at_position_samples(), None);
    assert_eq!(sm.record_expected_end_position_samples(), None);
    assert_eq!(sm.record_session_id(), None);
    assert_eq!(sm.measure_ready_generation(), 0);
}

#[test]
fn record_start_position_latches_from_first_captured_window_after_arm() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record_started_at_clock_window_transaction(
            License::Os,
            1_725_000_123_456,
            None,
            None,
            "session-a",
        ),
        Ok(())
    );
    assert_eq!(sm.record_started_at_position_samples(), None);

    assert!(sm.try_latch_record_started_at_position_samples(44_100));
    assert_eq!(sm.record_started_at_position_samples(), Some(44_100));
    assert!(!sm.try_latch_record_started_at_position_samples(88_200));
    assert_eq!(sm.record_started_at_position_samples(), Some(44_100));

    sm.exit_record();
    assert_eq!(sm.record_started_at_position_samples(), None);
    assert!(!sm.try_latch_record_started_at_position_samples(132_300));
}

#[test]
fn record_expected_end_position_is_stored_and_cleared() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record_started_at_clock_window_transaction(
            License::Os,
            1_725_000_123_456,
            Some(96_000),
            Some(192_000),
            "session-a",
        ),
        Ok(())
    );
    assert_eq!(sm.record_started_at_position_samples(), Some(96_000));
    assert_eq!(sm.record_expected_end_position_samples(), Some(192_000));
    sm.exit_record();
    assert_eq!(sm.record_started_at_position_samples(), None);
    assert_eq!(sm.record_expected_end_position_samples(), None);
}

#[test]
fn record_expected_end_position_rejects_non_positive_window() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record_started_at_clock_window(
            License::Os,
            1_725_000_123_456,
            Some(96_000),
            Some(96_000),
        ),
        Ok(())
    );
    assert_eq!(sm.record_expected_end_position_samples(), None);
}

#[test]
fn record_transaction_session_is_stored_and_cleared() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record_started_at_clock_transaction(
            License::Os,
            1_725_000_123_456,
            Some(96_000),
            " session-keep-1 ",
        ),
        Ok(())
    );
    assert_eq!(sm.record_session_id().as_deref(), Some("session-keep-1"));
    sm.exit_record();
    assert_eq!(sm.record_session_id(), None);
}

#[test]
fn record_transaction_requires_session() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record_started_at_clock_transaction(License::Os, 1, None, " "),
        Err(TransitionError::MissingRecordSession)
    );
    assert_eq!(sm.current(), RecordState::Watch);
}

/// ACK re-entry race 構造修正の回帰: 一度 exit_record で closed した session_id は、
/// on-disk signal が stale な Acknowledged を返し続けても二度と Record へ入れない。
/// これが無いと、io_thread_pre.rs / io_thread_post.rs の ACK poller が
/// `record_sm.exit_record()`（in-process）と `record_signal` の Released 更新
/// （on-disk）の間隙で同じ session_id を読み、Measure Thread の Watch→Record reset
/// （engine / trace queue clear）を誤って再トリガーしてしまう（2026-07-10 実障害）。
#[test]
fn closed_session_id_cannot_re_enter_record() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record_started_at_clock_transaction(License::Os, 1, None, "session-a"),
        Ok(())
    );
    sm.exit_record();
    assert_eq!(sm.current(), RecordState::Watch);

    // stale ACK が同じ session_id で再入場を試みても拒否される。
    assert_eq!(
        sm.try_enter_record_started_at_clock_transaction(License::Os, 2, None, "session-a"),
        Err(TransitionError::SessionAlreadyClosed)
    );
    assert_eq!(
        sm.current(),
        RecordState::Watch,
        "must remain in Watch, not re-enter"
    );
    assert_eq!(sm.record_session_id(), None);
}

/// 上のガードは「同じ session_id の再入場」だけを拒否する。新しい session_id を持つ
/// 正当な次の Keep/ACK サイクルは通常通り成功しなければならない。
#[test]
fn different_session_id_can_enter_after_previous_closed() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record_started_at_clock_transaction(License::Os, 1, None, "session-a"),
        Ok(())
    );
    sm.exit_record();

    assert_eq!(
        sm.try_enter_record_started_at_clock_transaction(License::Os, 2, None, "session-b"),
        Ok(())
    );
    assert_eq!(sm.record_session_id().as_deref(), Some("session-b"));
    assert_eq!(sm.current(), RecordState::Record);
}

#[test]
fn sense_license_denies_record_transition() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record(License::Sense),
        Err(TransitionError::LicenseDenied)
    );
    assert_eq!(sm.current(), RecordState::Watch);
}

#[test]
fn unknown_license_denies_record_transition() {
    let sm = RecordStateMachine::new();
    assert_eq!(
        sm.try_enter_record(License::Unknown),
        Err(TransitionError::LicenseDenied)
    );
    assert_eq!(sm.current(), RecordState::Watch);
}

#[test]
fn double_enter_is_idempotent_error() {
    let sm = RecordStateMachine::new();
    assert_eq!(sm.try_enter_record(License::Os), Ok(()));
    assert_eq!(
        sm.try_enter_record(License::Os),
        Err(TransitionError::AlreadyRecording)
    );
    // 状態は Record のまま（冪等）
    assert_eq!(sm.current(), RecordState::Record);
}

#[test]
fn record_generation_advances_only_on_successful_enter() {
    let sm = RecordStateMachine::new();
    assert_eq!(sm.generation(), 0);
    assert_eq!(
        sm.try_enter_record(License::Sense),
        Err(TransitionError::LicenseDenied)
    );
    assert_eq!(
        sm.generation(),
        0,
        "license denied must not advance generation"
    );

    assert_eq!(sm.try_enter_record(License::Os), Ok(()));
    assert_eq!(sm.generation(), 1);
    assert_eq!(
        sm.try_enter_record(License::Os),
        Err(TransitionError::AlreadyRecording)
    );
    assert_eq!(
        sm.generation(),
        1,
        "double enter must not advance generation"
    );

    sm.exit_record();
    assert_eq!(sm.generation(), 1, "exit keeps the completed session id");
    assert_eq!(sm.try_enter_record(License::Os), Ok(()));
    assert_eq!(sm.generation(), 2);
}

#[test]
fn exit_returns_to_watch() {
    let sm = RecordStateMachine::new();
    sm.try_enter_record(License::Os).unwrap();
    sm.exit_record();
    assert_eq!(sm.current(), RecordState::Watch);
}

#[test]
fn exit_from_watch_is_noop() {
    let sm = RecordStateMachine::new();
    sm.exit_record();
    assert_eq!(sm.current(), RecordState::Watch);
}

#[test]
fn record_state_as_str_consistent() {
    assert_eq!(RecordState::Watch.as_str(), "watch");
    assert_eq!(RecordState::Record.as_str(), "record");
}

#[test]
fn record_state_from_u8_unknown_falls_back_to_watch() {
    assert_eq!(RecordState::from_u8(0), RecordState::Watch);
    assert_eq!(RecordState::from_u8(1), RecordState::Record);
    // 未知値 → 安全側で Watch
    assert_eq!(RecordState::from_u8(2), RecordState::Watch);
    assert_eq!(RecordState::from_u8(255), RecordState::Watch);
}

#[test]
fn concurrent_enter_exits_are_safe() {
    // Arc<RecordStateMachine> で複数スレッドから操作しても破綻しないこと。
    let sm = Arc::new(RecordStateMachine::new());
    let mut handles = vec![];
    for _ in 0..16 {
        let s = Arc::clone(&sm);
        handles.push(thread::spawn(move || {
            for _ in 0..100 {
                let _ = s.try_enter_record(License::Os);
                s.exit_record();
            }
        }));
    }
    for h in handles {
        h.join().unwrap();
    }
    // 最後は全スレッドが exit_record を呼ぶので Watch のはず
    assert_eq!(sm.current(), RecordState::Watch);
}

#[test]
fn concurrent_enter_only_one_succeeds() {
    // compare_exchange の排他性: 同時に try_enter_record しても Ok は 1 つだけ。
    let sm = Arc::new(RecordStateMachine::new());
    let success_count = Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let mut handles = vec![];
    for _ in 0..32 {
        let s = Arc::clone(&sm);
        let counter = Arc::clone(&success_count);
        handles.push(thread::spawn(move || {
            if s.try_enter_record(License::Os).is_ok() {
                counter.fetch_add(1, Ordering::Relaxed);
            }
        }));
    }
    for h in handles {
        h.join().unwrap();
    }
    assert_eq!(success_count.load(Ordering::Relaxed), 1);
    assert_eq!(sm.current(), RecordState::Record);
}

/// P1 レース回帰: entering 中に負けた entrant（A）の cleanup が、A が entering の
/// 間に別の entrant（B）が完全成功させた新しい session を消してはならない。
///
/// 実スレッドで A を「2つの CAS の間」に確実に一時停止させる方法はない（OS
/// スケジューラ依存でフレーキーになる）ため、`begin_enter` / `finish_enter`
/// （割込み可能にするために分割した本番コードそのもの。
/// `try_enter_record_started_at_clock_window_with_session` はこの2つを続けて呼ぶだけ）を
/// 呼び出し順序だけ入れ替えて呼び、A の `finish_enter` を B の成功**後**まで遅延させる。
/// cleanup ロジック自体は本番の `finish_enter` をそのまま実行するので、
/// `clear_record_session_if_owned_by` のガードが退行すればこのテストは確実に落ちる。
#[test]
fn losing_entrant_cleanup_does_not_clobber_newer_committed_session() {
    let sm = RecordStateMachine::new();

    // A: begin_enter だけ実行し、finish_enter を意図的に呼ばずに保留する
    // （= A がまだ 2 つの CAS の間にいる状態）。
    let token_a = sm
        .begin_enter(1, None, None, Some("session-a".to_string()))
        .expect("A must win begin_enter from Watch");

    // 割込み: 「止める」タップ等が A の entering 中に飛んできて Watch へ戻す。
    sm.exit_record();

    // B: 完全に成功し、自分の session を確立する（正規の公開 API 経由）。
    assert_eq!(
        sm.try_enter_record_started_at_clock_transaction(License::Os, 2, None, "session-b"),
        Ok(())
    );
    assert_eq!(sm.record_session_id().as_deref(), Some("session-b"));

    // A の保留していた finish_enter がようやく走る。現在の state は B の
    // STATE_RECORD であって entering_a ではないため、本番の Err(_) cleanup 分岐に入る。
    assert_eq!(
        sm.finish_enter(token_a),
        Err(TransitionError::AlreadyRecording),
        "A's stale finish_enter must lose to B's commit"
    );

    // 修正前は record_session_id が None に潰され、is_recording()==true なのに
    // セッション無しという B-322 が閉じたはずの状態が別経路で再発していた。
    assert!(sm.is_recording(), "B's Record state must remain published");
    assert_eq!(
        sm.record_session_id().as_deref(),
        Some("session-b"),
        "a losing entrant's cleanup must not clobber a newer, already-committed session"
    );
}

/// TOCTOU 追加回帰（2026-07-09 レビューで発覚）: token と session_id が別々の
/// primitive に分かれていた旧設計では、「自分がまだ所有者か」チェックと「session を
/// clear する」実行が2つの非原子操作に分かれ、その間隙に別の entrant が割り込んで
/// 新しいセッションを確立すると、それを消してしまう gap が残っていた。
///
/// このテストは「所有権が移った後に古い token で cleanup を呼んでも安全」という
/// *機能的な*正しさを検証する（旧設計でも、チェックとクリアの間に他スレッドが
/// 割り込まない限りここは通っていた点に注意 — このテスト自体は並行実行中の
/// atomicity を証明しない）。atomicity の根拠はテストではなくコード構造そのもの:
/// `set_record_session` と `clear_record_session_if_owned_by` は必ず
/// `self.record_session.write()` の同一 guard の中でチェックと書き換えを完結させる
/// ため、チェックと変更の間に他スレッドが割り込む隙間がAPIとして存在しない
/// （旧バグを可能にしていた「別々に呼べる2つの操作」という前提そのものを消した）。
#[test]
fn stale_cleanup_call_after_ownership_moved_on_is_a_pure_noop() {
    let sm = RecordStateMachine::new();

    let token_a = sm
        .begin_enter(1, None, None, Some("session-a".to_string()))
        .expect("A must win begin_enter from Watch");
    sm.exit_record();

    // C は A の finish_enter が呼ばれるより前に、独立して完全に成功する。
    sm.try_enter_record_started_at_clock_transaction(License::Os, 2, None, "session-c")
        .expect("C must win the transaction after exit_record");
    assert_eq!(sm.record_session_id().as_deref(), Some("session-c"));

    // A の cleanup が遅れて呼ばれても、所有権チェックと clear が1つの critical
    // section なので必ず安全に no-op になる。
    sm.clear_record_session_if_owned_by(token_a);

    assert!(sm.is_recording(), "C's Record state must remain published");
    assert_eq!(
        sm.record_session_id().as_deref(),
        Some("session-c"),
        "a stale owner's cleanup call must never clobber the current owner's session"
    );
}

/// B-132 (G-115-382): seal は 0 開始・bump で単調前進・state とは独立。
#[test]
fn b132_seal_starts_zero_and_bumps_monotonic() {
    let sm = RecordStateMachine::new();
    assert_eq!(sm.seal(), 0, "seal は 0 開始");
    sm.bump_seal();
    assert_eq!(sm.seal(), 1);
    sm.bump_seal();
    assert_eq!(sm.seal(), 2, "bump は単調前進");
    // state 遷移は seal を動かさない（独立軸）。
    let before = sm.seal();
    sm.try_enter_record(License::Os).unwrap();
    sm.exit_record();
    assert_eq!(sm.seal(), before, "Record↔Watch 遷移は seal を変えない");
}
