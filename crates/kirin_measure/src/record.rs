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
use crate::record_display::RecordDisplayStore;
use std::sync::atomic::{AtomicI64, AtomicU64, Ordering};
use std::sync::RwLock;

#[path = "record_state.rs"]
mod state;
use state::{entering_state, SessionSlot, STATE_RECORD, STATE_WATCH};
pub use state::{RecordState, RecordStateMachine, TransitionError};

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
            record_expected_end_position_samples: AtomicI64::new(i64::MIN),
            record_session: RwLock::new(SessionSlot::default()),
            closed_session_id: RwLock::new(None),
            measure_ready_generation: AtomicU64::new(0),
            record_display: RecordDisplayStore::default(),
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

    /// Keep は Record を arm するだけで、音声範囲の native 開始位置は
    /// 最初に実キャプチャされた process window で一度だけ確定する。
    ///
    /// 呼び出し側は「実際に Record へ入れる window か」を判定済みであること。
    /// Audio Thread から呼ぶため atomics のみ。明示的な bounce/range 境界が
    /// 既に入っている場合は上書きしない。
    pub fn try_latch_record_started_at_position_samples(&self, position_samples: i64) -> bool {
        if position_samples == i64::MIN || !self.is_recording() {
            return false;
        }
        self.record_started_at_position_samples
            .compare_exchange(
                i64::MIN,
                position_samples,
                Ordering::AcqRel,
                Ordering::Acquire,
            )
            .is_ok()
    }

    /// 現 Record セッションの host native sample 終了位置。None は未設定/不明。
    pub fn record_expected_end_position_samples(&self) -> Option<i64> {
        let value = self
            .record_expected_end_position_samples
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
        self.try_enter_record_started_at_clock_window_with_session(
            license,
            started_at_ms,
            started_at_position_samples,
            None,
            None,
        )
    }

    /// Record へ遷移し、wall-clock barrier と native sample 開始/終了 barrier を保存する。
    pub fn try_enter_record_started_at_clock_window(
        &self,
        license: License,
        started_at_ms: i64,
        started_at_position_samples: Option<i64>,
        expected_end_position_samples: Option<i64>,
    ) -> Result<(), TransitionError> {
        self.try_enter_record_started_at_clock_window_with_session(
            license,
            started_at_ms,
            started_at_position_samples,
            expected_end_position_samples,
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
        self.try_enter_record_started_at_clock_window_transaction(
            license,
            started_at_ms,
            started_at_position_samples,
            None,
            record_session_id,
        )
    }

    /// Record へ遷移し、PRE/POST 協調 session_id と native sample 開始/終了 barrier を
    /// 同時に保存する。
    pub fn try_enter_record_started_at_clock_window_transaction(
        &self,
        license: License,
        started_at_ms: i64,
        started_at_position_samples: Option<i64>,
        expected_end_position_samples: Option<i64>,
        record_session_id: impl Into<String>,
    ) -> Result<(), TransitionError> {
        let record_session_id = record_session_id.into();
        let record_session_id = record_session_id.trim();
        if record_session_id.is_empty() {
            return Err(TransitionError::MissingRecordSession);
        }
        self.try_enter_record_started_at_clock_window_with_session(
            license,
            started_at_ms,
            started_at_position_samples,
            expected_end_position_samples,
            Some(record_session_id.to_string()),
        )
    }

    fn try_enter_record_started_at_clock_window_with_session(
        &self,
        license: License,
        started_at_ms: i64,
        started_at_position_samples: Option<i64>,
        expected_end_position_samples: Option<i64>,
        record_session_id: Option<String>,
    ) -> Result<(), TransitionError> {
        if !matches!(license, License::Os) {
            return Err(TransitionError::LicenseDenied);
        }
        // Watch → Entering → Record の2段階公開にする。
        // Measure Thread が Record を見る時点では started_at / generation が確定済み。
        let token = self.begin_enter(
            started_at_ms,
            started_at_position_samples,
            expected_end_position_samples,
            record_session_id,
        )?;
        self.finish_enter(token)
    }

    /// 2段階公開の前半: Watch→Entering CAS + started_at/position/session/generation の確定。
    /// 成功したら `finish_enter` で仕上げる責務を呼び出し元へ返す（token）。
    ///
    /// `record_session_id` が既に closed 済みなら Watch へロールバックして
    /// `SessionAlreadyClosed` を返す。このチェックは **CAS 成功後**（= state が実際に
    /// Watch だったと確定した後）に行う。CAS より前でチェックすると、チェックと CAS の間に
    /// 別スレッドの `exit_record` が割り込んで closed 済みにする TOCTOU が残る。CAS 成功後の
    /// チェックなら、`exit_record`（`clear_record_session` で closed 記録 → その後
    /// `state.store(WATCH)`）と本関数の CAS（`Ordering::Acquire` で成功）が同じ `state` 変数を
    /// 介して happens-before を作るため、CAS が成功した時点で closed_session_id の最新値を
    /// 必ず観測できる（P1/ACK re-entry 修正）。
    fn begin_enter(
        &self,
        started_at_ms: i64,
        started_at_position_samples: Option<i64>,
        expected_end_position_samples: Option<i64>,
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
                if let Some(sid) = record_session_id.as_deref() {
                    if self.is_session_closed(sid) {
                        self.state.store(STATE_WATCH, Ordering::Release);
                        return Err(TransitionError::SessionAlreadyClosed);
                    }
                }
                self.record_started_at_ms
                    .store(started_at_ms.max(0), Ordering::Release);
                self.record_started_at_position_samples.store(
                    started_at_position_samples.unwrap_or(i64::MIN),
                    Ordering::Release,
                );
                let expected_end_position_samples = expected_end_position_samples
                    .filter(|end| started_at_position_samples.is_none_or(|start| *end > start))
                    .unwrap_or(i64::MIN);
                self.record_expected_end_position_samples
                    .store(expected_end_position_samples, Ordering::Release);
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
            Ok(_) => {
                self.record_display.begin(self.generation());
                Ok(())
            }
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

    /// 無条件に token と session_id をクリアする（`exit_record` 用）。クリアした session_id は
    /// `closed_session_id` に記録し、以後同じ session_id での re-entry を拒否できるようにする
    /// （ACK re-entry race の構造修正）。`exit_record` はこの後に `state.store(WATCH)` するため、
    /// 他スレッドから state=Watch が見える時点では closed_session_id は必ず先に確定している。
    fn clear_record_session(&self) {
        let closing_session_id = {
            let mut guard = match self.record_session.write() {
                Ok(guard) => guard,
                Err(poisoned) => poisoned.into_inner(),
            };
            let session_id = guard.session_id.take();
            *guard = SessionSlot::default();
            session_id
        };
        if let Some(session_id) = closing_session_id {
            self.mark_session_closed(session_id);
        }
    }

    /// `session_id` が既に closed（`exit_record` 済み）かどうか。
    fn is_session_closed(&self, session_id: &str) -> bool {
        let guard = match self.closed_session_id.read() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        guard.as_deref() == Some(session_id)
    }

    /// `session_id` を closed として記録する（直近1件のみ保持・上書き）。
    fn mark_session_closed(&self, session_id: String) {
        let mut guard = match self.closed_session_id.write() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        *guard = Some(session_id);
    }

    /// 直近に closed された `record_session_id`（1つだけ・上書き）。
    ///
    /// record_writer 側が「1 session = 1 writer」を独立に検証するための backstop 用
    /// （P2: この state machine 自身が既に `try_enter_record_started_at_clock_transaction`
    /// で同じ session_id の re-entry を拒否しているため、通常運用ではここが writer 側の
    /// dedup チェックと食い違うことはない）。
    pub fn last_closed_session_id(&self) -> Option<String> {
        match self.closed_session_id.read() {
            Ok(guard) => guard.clone(),
            Err(poisoned) => poisoned.into_inner().clone(),
        }
    }

    /// Watch へ戻す。無条件・冪等。
    ///
    /// 用途:
    /// - 「記録を止める」タップ
    /// - 同一 session の Kirin OS Drop commit
    /// - 10 分 idle timeout
    /// - プラグインアンロード時のローカル終了処理
    pub fn exit_record(&self) {
        // Watch を公開して次の entrant を許可する前に、閉じる世代を固定して表示側へ伝える。
        // state.store(WATCH) 後に self.generation() を読むと、その隙間で次の Keep が generation
        // を進め、新しい Live 表示を旧 Stop が閉じる ABA が成立する。
        let closing_generation = self.generation();
        self.record_started_at_ms.store(0, Ordering::Release);
        self.record_started_at_position_samples
            .store(i64::MIN, Ordering::Release);
        self.record_expected_end_position_samples
            .store(i64::MIN, Ordering::Release);
        self.clear_record_session();
        self.measure_ready_generation.store(0, Ordering::Release);
        self.record_display.request_stop(closing_generation);
        self.state.store(STATE_WATCH, Ordering::Release);
    }

    pub fn mark_record_display_measure_started(&self, generation: u64) {
        self.record_display.mark_measure_started(generation);
    }

    pub fn publish_record_display_measure(
        &self,
        generation: u64,
        measure: crate::MeasureResult,
        summary: crate::SessionSummary,
    ) {
        self.record_display
            .publish_measure(generation, measure, summary);
    }

    pub fn publish_record_display_delta(
        &self,
        generation: u64,
        delta: crate::DeltaResult,
        pair_pre_instance_id: Option<String>,
    ) {
        self.record_display
            .publish_delta(generation, delta, pair_pre_instance_id);
    }

    pub fn finalize_record_display(&self, generation: u64, summary: Option<crate::SessionSummary>) {
        self.record_display.finalize(generation, summary);
    }

    pub fn mark_record_display_unavailable(&self, generation: u64) {
        self.record_display.mark_unavailable(generation);
    }

    pub fn dismiss_record_display_on_watch_result(&self) {
        self.record_display.dismiss_on_watch_result();
    }

    pub fn try_record_display_snapshot(&self) -> Option<crate::RecordDisplaySnapshot> {
        self.record_display.try_snapshot()
    }
}

impl Default for RecordStateMachine {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
#[path = "record_tests.rs"]
mod tests;
