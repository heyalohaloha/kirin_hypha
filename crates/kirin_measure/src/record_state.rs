//! Record state data model shared by the transition implementation.

use crate::record_display::RecordDisplayStore;
use std::sync::atomic::{AtomicI64, AtomicU64};
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

pub(super) const STATE_WATCH: u64 = RecordState::Watch as u64;
pub(super) const STATE_RECORD: u64 = RecordState::Record as u64;
const STATE_ENTERING_TAG: u64 = 0b10;

pub(super) fn entering_state(token: u64) -> u64 {
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
    /// この `record_session_id` は既に一度 Record→Watch で閉じられている。
    ///
    /// 同じ session_id は Pending→Recording→Closed の単方向にしか進めない。ACK poller が
    /// on-disk signal の後読みで stale な Acknowledged を見て同じ session_id へ再入場しようと
    /// しても、Watch へ既に closed 済みの session を復活させることはできない
    /// （2026-07-10 レビューで発覚した ACK re-entry race の構造修正）。
    SessionAlreadyClosed,
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
pub(super) struct SessionSlot {
    /// このセッションを書いた entrant の `entry_token`（0 = 所有者なし）。
    pub(super) owner_token: u64,
    /// PRE/POST が同じ Keep/ACK から受け取った不変 Record session。
    pub(super) session_id: Option<String>,
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
    pub(super) state: AtomicU64,
    /// Watch→Record の内部 Entering 状態を識別する単調トークン。
    /// exit/retry が同時に走っても、古い Entering が新しい Entering を Record に昇格しないようにする。
    pub(super) entry_token: AtomicU64,
    /// B-132 (G-115-382): drain-completion seal。Record→Watch エッジで Measure Thread が
    /// 残量 ring を tight-drain → 最終 finalize → session_summary 書込を**完了した後**に
    /// 1 度だけ前進させる単調カウンタ。IO/record_writer の bake arm は Record 開始時に
    /// `seal()` を snapshot し、close 時に前進を lock-free bounded wait で待ってから
    /// session_summary を take する（post-drain の確定スナップショットのみ焼く保証）。
    /// timeout（measure 死/shutdown/stall で finalize 不能）時は integrity_degraded に倒す。
    pub(super) seal: AtomicU64,
    /// Watch→Record が成功した時だけ前進する Record セッション世代。
    /// Audio Thread が「現在の Record で音声を見たか」を stale flag なしで判定するための
    /// lock-free セッション ID。
    pub(super) generation: AtomicU64,
    /// Record 要求が作られた wall-clock epoch ms。
    ///
    /// PRE は POST の `record_signal.started_at` をここへ保存する。Measure Thread は
    /// Watch→Record を遅れて観測した場合、この時刻以降の pre-roll TRACE だけを Record
    /// に復元し、Keep 前の古い Watch 計測を混ぜない。
    pub(super) record_started_at_ms: AtomicI64,
    /// Host native sample position 上の Record 開始 barrier。
    ///
    /// POST が Keep 時に `record_signal` へ保存し、PRE/POST 両方が同じ値を Record
    /// state に入れる。Measure Thread は取得できる場合、この値を pre-roll/timeline
    /// origin として wall-clock より優先する。
    pub(super) record_started_at_position_samples: AtomicI64,
    /// Host native sample position 上の Record 終了 barrier。
    ///
    /// Kirin OS が expected WAV duration を Keep 前に渡せた場合、`started + duration`
    /// をここへ保存する。Audio Thread はこの値を capture window の上限にし、実測に入る
    /// 音声そのものをサンプル境界で切る。
    pub(super) record_expected_end_position_samples: AtomicI64,
    /// PRE/POST が同じ Keep/ACK から受け取った不変 Record session（所有者 token と1つの
    /// lock 配下）。
    ///
    /// Audio Thread はこの値を読まない。IO Thread が writer を開始する直前に参照し、
    /// `/tmp` の record_signal が後から消える/変わるタイミングでも sessionless TRACE を
    /// 作らないための transaction snapshot。`SessionSlot` のドキュメント参照。
    pub(super) record_session: RwLock<SessionSlot>,
    /// 直近に `exit_record` で閉じられた `record_session_id`（1つだけ・上書き）。
    ///
    /// PRE/POST それぞれの ACK poller（`io_thread_pre.rs` / `io_thread_post.rs`）は on-disk
    /// `record_signal` が Acknowledged のままかどうかで Record 再入場を判断するが、Stop 時に
    /// `record_sm.exit_record()`（in-process）が `record_signal` の Released 更新（on-disk）より
    /// 先に走ると、その間隙で stale な Acknowledged を読んで同じ session_id へ再入場してしまう
    /// （2026-07-10 発覚）。この field は「一度 Watch へ closed した session_id」を記憶し、
    /// `try_enter_record_started_at_clock_transaction` がそれと同じ session_id での再入場を
    /// 拒否できるようにする。on-disk signal の更新順序に依存しない、状態機械自身が持つ
    /// 構造的ガード（B-322/B-323 の `SessionSlot` 修正と同じ発想: 外部の後読みタイミングに
    /// 正しさを委ねない）。単一値で十分（1インスタンスにつき同時に有効な session は常に1つ）。
    pub(super) closed_session_id: RwLock<Option<String>>,
    /// Measure Thread が Watch→Record 遷移を観測し、Record TRACE を受けられる状態に
    /// なった最新 generation。PRE はこの値を待ってから `record_signal` を Acknowledged にする。
    pub(super) measure_ready_generation: AtomicU64,
    /// GUI表示専用の世代付きRecord snapshot。計測・writer・pairingの正本には使わない。
    pub(super) record_display: RecordDisplayStore,
}
