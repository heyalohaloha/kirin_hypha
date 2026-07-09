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
//! `AtomicU64` で状態を保持。Audio / Measure / IO / GUI / Watchdog の各スレッドから
//! ロックなしで読めるため、`Arc<RecordStateMachine>` で共有する。
//!
//! # T-1 のスコープ
//! 本モジュールは状態遷移ロジック + 単体テストのみ。Plugin 側の統合
//! （hypha_pre / hypha_post の `initialize` / `process`）は T-6 GUI 統合まで遅延する。
//! Watch 時の既存 Step 1 挙動への副作用をゼロに保つため。

use crate::identity::License;
use std::sync::atomic::{AtomicI64, AtomicU64, Ordering};
use std::sync::RwLock;

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
    /// PRE/POST 協調 Record の不変 session_id が無い。plugin_data writer は開始しない。
    MissingRecordSession,
    /// 既に Record 中。冪等性のため「エラー」としつつ状態は変更しない。
    AlreadyRecording,
}

/// `record_session_id` と、それを最後に書いた entrant の `entry_token` を1つの lock 配下で
/// 保持する。
///
/// この2つを別々の primitive（例: `AtomicU64` + `RwLock<Option<String>>`）で持つと、
/// 「自分がまだ所有者か」チェックと「session を書き換える」が2つの非原子操作に分かれ、
/// その間隙に別の entrant が割り込んで新しいセッションを確立すると、それを消してしまう
/// TOCTOU が生まれる（2026-07-09 レビューで発覚。B-322 / P1 レース修正の再発）。
/// token と session_id を常に同じ write guard 内で読み書きすることで、
/// チェックとクリアを1つの critical section に閉じ込め、この class の gap を構造的に
/// 起こり得なくする。
#[derive(Debug, Clone, Default)]
struct SessionSlot {
    /// このセッションを書いた entrant の `entry_token`（0 = 所有者なし）。
    owner_token: u64,
    /// PRE/POST が同じ Keep/ACK から受け取った不変 Record session。
    session_id: Option<String>,
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
    /// Host native sample position 上の Record 開始 barrier。
    ///
    /// POST が Keep 時に `record_signal` へ保存し、PRE/POST 両方が同じ値を Record
    /// state に入れる。Measure Thread は取得できる場合、この値を pre-roll/timeline
    /// origin として wall-clock より優先する。
    record_started_at_position_samples: AtomicI64,
    /// PRE/POST が同じ Keep/ACK から受け取った不変 Record session（所有者 token と1つの
    /// lock 配下）。
    ///
    /// Audio Thread はこの値を読まない。IO Thread が writer を開始する直前に参照し、
    /// `/tmp` の record_signal が後から消える/変わるタイミングでも sessionless TRACE を
    /// 作らないための transaction snapshot。`SessionSlot` のドキュメント参照。
    record_session: RwLock<SessionSlot>,
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
            record_started_at_position_samples: AtomicI64::new(i64::MIN),
            record_session: RwLock::new(SessionSlot::default()),
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

    /// 現 Record セッションの host native sample 開始位置。None は未設定/不明。
    pub fn record_started_at_position_samples(&self) -> Option<i64> {
        let value = self
            .record_started_at_position_samples
            .load(Ordering::Acquire);
        (value != i64::MIN).then_some(value)
    }

    /// 現 Record の不変 session_id。transaction 経由で入った Record だけが持つ。
    pub fn record_session_id(&self) -> Option<String> {
        match self.record_session.read() {
            Ok(guard) => guard.session_id.clone(),
            Err(poisoned) => poisoned.into_inner().session_id.clone(),
        }
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
        self.try_enter_record_started_at_clock(license, started_at_ms, None)
    }

    /// Record へ遷移し、wall-clock barrier と native sample barrier を保存する。
    pub fn try_enter_record_started_at_clock(
        &self,
        license: License,
        started_at_ms: i64,
        started_at_position_samples: Option<i64>,
    ) -> Result<(), TransitionError> {
        self.try_enter_record_started_at_clock_with_session(
            license,
            started_at_ms,
            started_at_position_samples,
            None,
        )
    }

    /// Record へ遷移し、PRE/POST 協調で合意した session_id も同時に保存する。
    ///
    /// この経路だけが TRACE writer の正規入口。session_id は Record state 公開前に確定
    /// するため、writer が `/tmp` の後続状態を読み直せない場合でも sessionless にならない。
    pub fn try_enter_record_started_at_clock_transaction(
        &self,
        license: License,
        started_at_ms: i64,
        started_at_position_samples: Option<i64>,
        record_session_id: impl Into<String>,
    ) -> Result<(), TransitionError> {
        let record_session_id = record_session_id.into();
        let record_session_id = record_session_id.trim();
        if record_session_id.is_empty() {
            return Err(TransitionError::MissingRecordSession);
        }
        self.try_enter_record_started_at_clock_with_session(
            license,
            started_at_ms,
            started_at_position_samples,
            Some(record_session_id.to_string()),
        )
    }

    fn try_enter_record_started_at_clock_with_session(
        &self,
        license: License,
        started_at_ms: i64,
        started_at_position_samples: Option<i64>,
        record_session_id: Option<String>,
    ) -> Result<(), TransitionError> {
        if !matches!(license, License::Os) {
            return Err(TransitionError::LicenseDenied);
        }
        // Watch → Entering → Record の2段階公開にする。
        // Measure Thread が Record を見る時点では started_at / generation が確定済み。
        let token =
            self.begin_enter(started_at_ms, started_at_position_samples, record_session_id)?;
        self.finish_enter(token)
    }

    /// 2段階公開の前半: Watch→Entering CAS + started_at/position/session/generation の確定。
    /// 成功したら `finish_enter` で仕上げる責務を呼び出し元へ返す（token）。
    fn begin_enter(
        &self,
        started_at_ms: i64,
        started_at_position_samples: Option<i64>,
        record_session_id: Option<String>,
    ) -> Result<u64, TransitionError> {
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
                self.record_started_at_position_samples.store(
                    started_at_position_samples.unwrap_or(i64::MIN),
                    Ordering::Release,
                );
                self.set_record_session(token, record_session_id);
                self.generation.fetch_add(1, Ordering::AcqRel);
                Ok(token)
            }
            Err(_) => Err(TransitionError::AlreadyRecording),
        }
    }

    /// 2段階公開の後半: Entering(token)→Record CAS。負けたら cleanup する。
    ///
    /// cleanup は自分がまだ session の所有者（= 自分より後に完全成功した entrant が
    /// いない）場合だけ session を clear する。所有者トークンが既に他者へ移っていれば、
    /// それは新しい Record が正当に確立したセッションなので触れない（P1 レース修正。
    /// 所有権チェックと clear は `clear_record_session_if_owned_by` 内の1回の write guard
    /// で完結させ、間隙を作らない）。
    fn finish_enter(&self, token: u64) -> Result<(), TransitionError> {
        let entering = entering_state(token);
        match self.state.compare_exchange(
            entering,
            STATE_RECORD,
            Ordering::Release,
            Ordering::Acquire,
        ) {
            Ok(_) => Ok(()),
            Err(_) => {
                self.clear_record_session_if_owned_by(token);
                Err(TransitionError::AlreadyRecording)
            }
        }
    }

    /// `token` と `session_id` を1つの write guard 内でまとめて確定させる（`begin_enter` 用）。
    fn set_record_session(&self, token: u64, session_id: Option<String>) {
        let session_id = session_id
            .map(|value| value.trim().to_string())
            .filter(|value| !value.is_empty());
        let mut guard = match self.record_session.write() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        guard.owner_token = token;
        guard.session_id = session_id;
    }

    /// 自分（`token`）がまだ所有者の場合だけ、所有権チェックと clear を1つの write guard
    /// 内で完結させる（`finish_enter` の cleanup 用）。チェックと clear を分離すると、
    /// その間隙に別の entrant が割り込む TOCTOU が生まれるため、必ず単一の critical
    /// section にする（P1 レース修正）。
    fn clear_record_session_if_owned_by(&self, token: u64) {
        let mut guard = match self.record_session.write() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        if guard.owner_token == token {
            *guard = SessionSlot::default();
        }
    }

    /// 無条件に token と session_id をクリアする（`exit_record` 用）。
    fn clear_record_session(&self) {
        let mut guard = match self.record_session.write() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        *guard = SessionSlot::default();
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
        self.record_started_at_position_samples
            .store(i64::MIN, Ordering::Release);
        self.clear_record_session();
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
        assert_eq!(sm.record_started_at_position_samples(), None);
        assert_eq!(sm.record_session_id(), None);
        assert_eq!(sm.measure_ready_generation(), 0);
        assert_eq!(
            sm.try_enter_record_started_at_clock(License::Os, 1_725_000_123_456, Some(96_000)),
            Ok(())
        );
        assert_eq!(sm.record_started_at_ms(), 1_725_000_123_456);
        assert_eq!(sm.record_started_at_position_samples(), Some(96_000));
        assert_eq!(sm.record_session_id(), None);
        sm.mark_measure_ready(sm.generation());
        assert_eq!(sm.measure_ready_generation(), sm.generation());
        sm.exit_record();
        assert_eq!(sm.record_started_at_ms(), 0);
        assert_eq!(sm.record_started_at_position_samples(), None);
        assert_eq!(sm.record_session_id(), None);
        assert_eq!(sm.measure_ready_generation(), 0);
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

    /// P1 レース回帰: entering 中に負けた entrant（A）の cleanup が、A が entering の
    /// 間に別の entrant（B）が完全成功させた新しい session を消してはならない。
    ///
    /// 実スレッドで A を「2つの CAS の間」に確実に一時停止させる方法はない（OS
    /// スケジューラ依存でフレーキーになる）ため、`begin_enter` / `finish_enter`
    /// （割込み可能にするために分割した本番コードそのもの。
    /// `try_enter_record_started_at_clock_with_session` はこの2つを続けて呼ぶだけ）を
    /// 呼び出し順序だけ入れ替えて呼び、A の `finish_enter` を B の成功**後**まで遅延させる。
    /// cleanup ロジック自体は本番の `finish_enter` をそのまま実行するので、
    /// `clear_record_session_if_owned_by` のガードが退行すればこのテストは確実に落ちる。
    #[test]
    fn losing_entrant_cleanup_does_not_clobber_newer_committed_session() {
        let sm = RecordStateMachine::new();

        // A: begin_enter だけ実行し、finish_enter を意図的に呼ばずに保留する
        // （= A がまだ 2 つの CAS の間にいる状態）。
        let token_a = sm
            .begin_enter(1, None, Some("session-a".to_string()))
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
            .begin_enter(1, None, Some("session-a".to_string()))
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
}
