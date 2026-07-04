//! Record mode 状態機械 — Watch ↔ Record の状態遷移。
//!
//!
//! # 状態
//! | State | 計測 | 書込先 | LED |
//! |-------|------|--------|-----|
//! | `Watch`  | 4項目（LUFS-M / TP / Crest / PSR）| `$TMPDIR/kirin/...` | 青の淡い発光 |
//!
//! # 遷移
//! ```text
//!   起動 → Watch（デフォルト）
//!   Watch  →[license="os" + 排他 OK + 「残す」タップ]→ Record
//!   Record →[「記録を止める」タップ / Drop / 別マシン検出]→ Watch
//! ```
//!
//! # 二重 gate（E-21 保険）
//! `try_enter_record` は `license` を必ずチェックする。GUI ボタン非表示だけに依存しない。
//! license 降格時（例: `"os" → "sense"`）、すでに Record 中であっても `try_enter_record`
//! を再度呼ぶと拒否される。
//!
//! # スレッド安全性
//! `AtomicU8` で状態を保持。Audio / Measure / IO / GUI / Watchdog の各スレッドから
//! ロックなしで読めるため、`Arc<RecordStateMachine>` で共有する。
//!
//! # T-1 のスコープ
//! 本モジュールは状態遷移ロジック + 単体テストのみ。Plugin 側の統合
//! （hypha_pre / hypha_post の `initialize` / `process`）は T-6 GUI 統合まで遅延する。
//! Watch 時の既存 Step 1 挙動への副作用をゼロに保つため。

use crate::identity::License;
use std::sync::atomic::{AtomicI64, AtomicU64, Ordering};

/// Record mode の状態。
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RecordState {
    /// Watch mode（デフォルト）。
    Watch = 0,
    /// Record mode。
    Record = 1,
}

const STATE_WATCH: u64 = RecordState::Watch as u64;
const STATE_RECORD: u64 = RecordState::Record as u64;
const STATE_ENTERING_TAG: u64 = 0b10;

fn entering_state(token: u64) -> u64 {
    (token.max(1) << 2) | STATE_ENTERING_TAG
}

impl RecordState {
    pub fn from_u8(v: u8) -> Self {
        match v {
            1 => Self::Record,
            _ => Self::Watch,
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Watch => "watch",
            Self::Record => "record",
        }
    }
}

/// 遷移失敗の理由。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransitionError {
    /// license が Record 許可外（`sense` / `unknown`）。保険 E-21 で拒否。
    LicenseDenied,
    /// 既に Record 中。冪等性のため「エラー」としつつ状態は変更しない。
    AlreadyRecording,
}

/// 状態機械本体。
///
/// # 使用例
/// ```ignore
/// let sm = Arc::new(RecordStateMachine::new());
///
/// // 「残す」タップ → GUI 側で呼ぶ
/// match sm.try_enter_record(License::Os) {
///     Ok(()) => { /* plugin_data 書込開始 */ }
///     Err(TransitionError::LicenseDenied) => show_sense_hint(),
///     Err(TransitionError::AlreadyRecording) => { /* 無視 */ }
/// }
///
/// // 「記録を止める」タップ
/// sm.exit_record();
/// ```
#[derive(Debug)]
pub struct RecordStateMachine {
    state: AtomicU64,
    /// Watch→Record の内部 Entering 状態を識別する単調トークン。
    /// exit/retry が同時に走っても、古い Entering が新しい Entering を Record に昇格しないようにする。
    entry_token: AtomicU64,
    /// B-132 (G-115-382): drain-completion seal。Record→Watch エッジで Measure Thread が
    /// 残量 ring を tight-drain → 最終 finalize → session_summary 書込を**完了した後**に
    /// 1 度だけ前進させる単調カウンタ。IO/record_writer の bake arm は Record 開始時に
    /// `seal()` を snapshot し、close 時に前進を lock-free bounded wait で待ってから
    /// session_summary を take する（post-drain の確定スナップショットのみ焼く保証）。
    /// timeout（measure 死/shutdown/stall で finalize 不能）時は integrity_degraded に倒す。
    seal: AtomicU64,
    /// Watch→Record が成功した時だけ前進する Record セッション世代。
    /// Audio Thread が「現在の Record で音声を見たか」を stale flag なしで判定するための
    /// lock-free セッション ID。
    generation: AtomicU64,
    /// Record 要求が作られた wall-clock epoch ms。
    ///
    /// PRE は POST の `record_signal.started_at` をここへ保存する。Measure Thread は
    /// Watch→Record を遅れて観測した場合、この時刻以降の pre-roll TRACE だけを Record
    /// に復元し、Keep 前の古い Watch 計測を混ぜない。
    record_started_at_ms: AtomicI64,
    /// Measure Thread が Watch→Record 遷移を観測し、Record TRACE を受けられる状態に
    /// なった最新 generation。PRE はこの値を待ってから `record_signal` を Acknowledged にする。
    measure_ready_generation: AtomicU64,
}

impl RecordStateMachine {
    /// Watch で初期化。
    pub fn new() -> Self {
        Self {
            state: AtomicU64::new(STATE_WATCH),
            entry_token: AtomicU64::new(0),
            seal: AtomicU64::new(0),
            generation: AtomicU64::new(0),
            record_started_at_ms: AtomicI64::new(0),
            measure_ready_generation: AtomicU64::new(0),
        }
    }

    /// B-132: 現在の drain-completion seal 値（IO 側 reader 用 / lock-free）。
    pub fn seal(&self) -> u64 {
        self.seal.load(Ordering::Acquire)
    }

    /// B-132: drain-completion seal を 1 前進させる（Measure Thread writer 用）。
    /// **必ず** session_summary 書込が完了した後にのみ呼ぶこと（post-drain 確定の合図）。
    pub fn bump_seal(&self) {
        self.seal.fetch_add(1, Ordering::Release);
    }

    /// 現在の Record セッション世代。0 はまだ Record に入っていない状態。
    pub fn generation(&self) -> u64 {
        self.generation.load(Ordering::Acquire)
    }

    /// 現 Record セッションの要求開始時刻（epoch ms）。0 は未設定。
    pub fn record_started_at_ms(&self) -> i64 {
        self.record_started_at_ms.load(Ordering::Acquire)
    }

    /// Measure Thread が Record generation を観測済みかどうかを見る。
    pub fn measure_ready_generation(&self) -> u64 {
        self.measure_ready_generation.load(Ordering::Acquire)
    }

    /// Measure Thread 側から、Record TRACE 受入準備ができた generation を公開する。
    pub fn mark_measure_ready(&self, generation: u64) {
        self.measure_ready_generation
            .fetch_max(generation, Ordering::AcqRel);
    }

    /// 現在の状態を取得。
    pub fn current(&self) -> RecordState {
        match self.state.load(Ordering::Acquire) {
            STATE_RECORD => RecordState::Record,
            _ => RecordState::Watch,
        }
    }

    /// Record 中かどうか（ショートハンド）。
    pub fn is_recording(&self) -> bool {
        self.current() == RecordState::Record
    }

    /// Record へ遷移を試みる。license 二重 gate + 冪等性チェック。
    ///
    /// 排他制御（T-3）は呼び出し元が事前に実施する責務。本メソッドは
    /// license のみ判定する。
    pub fn try_enter_record(&self, license: License) -> Result<(), TransitionError> {
        self.try_enter_record_started_at(license, 0)
    }

    /// Record へ遷移し、Record 要求時刻も保存する。
    ///
    /// `started_at_ms` は POST 自身なら Keep 操作時刻、PRE なら POST が書いた
    /// `record_signal.started_at`。Measure Thread の pre-roll 復元境界としてのみ使い、
    /// 0 以下なら従来通り復元しない。
    pub fn try_enter_record_started_at(
        &self,
        license: License,
        started_at_ms: i64,
    ) -> Result<(), TransitionError> {
        if !matches!(license, License::Os) {
            return Err(TransitionError::LicenseDenied);
        }
        // Watch → Entering → Record の2段階公開にする。
        // Measure Thread が Record を見る時点では started_at / generation が確定済み。
        let token = self.entry_token.fetch_add(1, Ordering::Relaxed) + 1;
        let entering = entering_state(token);
        match self.state.compare_exchange(
            STATE_WATCH,
            entering,
            Ordering::AcqRel,
            Ordering::Relaxed,
        ) {
            Ok(_) => {
                self.record_started_at_ms
                    .store(started_at_ms.max(0), Ordering::Release);
                self.generation.fetch_add(1, Ordering::AcqRel);
                match self.state.compare_exchange(
                    entering,
                    STATE_RECORD,
                    Ordering::Release,
                    Ordering::Acquire,
                ) {
                    Ok(_) => Ok(()),
                    Err(_) => Err(TransitionError::AlreadyRecording),
                }
            }
            Err(_) => Err(TransitionError::AlreadyRecording),
        }
    }

    /// Watch へ戻す。無条件・冪等。
    ///
    /// 用途:
    /// - 「記録を止める」タップ
    /// - `Drop`（プラグインアンロード）
    /// - 別マシン検出による計測停止
    /// - license 降格時の保険
    pub fn exit_record(&self) {
        self.record_started_at_ms.store(0, Ordering::Release);
        self.measure_ready_generation.store(0, Ordering::Release);
        self.state.store(STATE_WATCH, Ordering::Release);
    }

    /// license 降格時の強制 Watch（保険 G-50-47）。
    ///
    /// `current_license` が `os` でないのに Record 中なら Watch に戻す。
    /// `os` であれば何もしない。
    pub fn enforce_license(&self, current_license: License) {
        if self.is_recording() && !matches!(current_license, License::Os) {
            self.exit_record();
        }
    }
}

impl Default for RecordStateMachine {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
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
        assert_eq!(sm.measure_ready_generation(), 0);
        assert_eq!(
            sm.try_enter_record_started_at(License::Os, 1_725_000_123_456),
            Ok(())
        );
        assert_eq!(sm.record_started_at_ms(), 1_725_000_123_456);
        sm.mark_measure_ready(sm.generation());
        assert_eq!(sm.measure_ready_generation(), sm.generation());
        sm.exit_record();
        assert_eq!(sm.record_started_at_ms(), 0);
        assert_eq!(sm.measure_ready_generation(), 0);
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
    fn enforce_license_os_keeps_record() {
        let sm = RecordStateMachine::new();
        sm.try_enter_record(License::Os).unwrap();
        sm.enforce_license(License::Os);
        assert_eq!(sm.current(), RecordState::Record);
    }

    #[test]
    fn enforce_license_sense_forces_watch() {
        let sm = RecordStateMachine::new();
        sm.try_enter_record(License::Os).unwrap();
        // 降格（Os → Sense）
        sm.enforce_license(License::Sense);
        assert_eq!(sm.current(), RecordState::Watch);
    }

    #[test]
    fn enforce_license_unknown_forces_watch() {
        let sm = RecordStateMachine::new();
        sm.try_enter_record(License::Os).unwrap();
        sm.enforce_license(License::Unknown);
        assert_eq!(sm.current(), RecordState::Watch);
    }

    #[test]
    fn enforce_license_on_watch_is_noop() {
        let sm = RecordStateMachine::new();
        sm.enforce_license(License::Sense);
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
}
