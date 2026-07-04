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
use std::sync::atomic::{AtomicU64, AtomicU8, Ordering};

/// Record mode の状態。
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RecordState {
    /// Watch mode（デフォルト）。
    Watch = 0,
    /// Record mode。
    Record = 1,
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
    state: AtomicU8,
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
}

impl RecordStateMachine {
    /// Watch で初期化。
    pub fn new() -> Self {
        Self {
            state: AtomicU8::new(RecordState::Watch as u8),
            seal: AtomicU64::new(0),
            generation: AtomicU64::new(0),
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

    /// 現在の状態を取得。
    pub fn current(&self) -> RecordState {
        RecordState::from_u8(self.state.load(Ordering::Relaxed))
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
        if !matches!(license, License::Os) {
            return Err(TransitionError::LicenseDenied);
        }
        // compare_exchange で冪等性を保証（Watch → Record のみ成功）。
        match self.state.compare_exchange(
            RecordState::Watch as u8,
            RecordState::Record as u8,
            Ordering::AcqRel,
            Ordering::Relaxed,
        ) {
            Ok(_) => {
                self.generation.fetch_add(1, Ordering::AcqRel);
                Ok(())
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
        self.state
            .store(RecordState::Watch as u8, Ordering::Release);
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
