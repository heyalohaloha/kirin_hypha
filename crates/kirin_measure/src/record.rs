//! Record mode 状態機械 — Watch ↔ Record の状態遷移。
//!
//! .md T-1 対応。
//!
//! # 状態
//! | State | 計測 | 書込先 | LED |
//! |-------|------|--------|-----|
//! | `Watch`  | 4項目（LUFS-M / TP / Crest / PSR）| `$TMPDIR/kirin/...` | 青の淡い発光 |
//! | `Record` | + Phase D 3項目 | `plugin_data/...` | 緑の脈動 |
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
use std::sync::atomic::{AtomicU8, Ordering};

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
}

impl RecordStateMachine {
    /// Watch で初期化。
    pub fn new() -> Self {
        Self { state: AtomicU8::new(RecordState::Watch as u8) }
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
    /// 排他制御は呼び出し元が事前に実施する責務。本メソッドは
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
            Ok(_) => Ok(()),
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
}
