//! kirin_measure — Kirin Hypha 共通計測ライブラリ。
//!
//! napi-rs 依存を持たない純粋な Rust ライブラリ。
//! nih-plug の Audio Thread から独立した Measure Thread / IO Thread で使用する。

pub mod all_keep_signal;
pub mod all_stop_signal;
mod atomic_claim;
pub mod atomic_file;
pub mod cleanup;
pub mod delta;
pub mod engine;
pub mod exclusion;
pub mod hardware;
pub mod identity;
pub mod io_thread_post;
pub mod io_thread_pre;
pub mod license;
pub mod measure_thread;
pub mod pairing_scope;
pub mod path_identity;
pub mod phase_d;
pub mod plugin_data;
pub mod post_candidates;
pub mod pre_candidates;
pub mod pre_discovery;
pub mod pre_self_discovery;
pub mod preset;
pub mod preset_dispatch;
pub mod preset_v2;
pub mod record;
pub mod record_clock;
mod record_entry_lock;
pub mod record_expected;
pub mod record_signal;
pub mod record_take;
pub mod record_writer;
mod record_writer_claim;
pub mod resampler;
pub mod reservation;
pub mod storage;
pub mod watch_playback_pass;
mod watch_tmp_cleanup;
pub mod watchdog;

pub use all_keep_signal::{
    delete_broadcast, is_broadcast_stale, read_broadcast, scan_broadcasts_dir,
    signal_path as all_keep_signal_path, signals_dir as all_keep_signals_dir, write_broadcast,
    write_broadcast_signal, AllKeepBroadcast, AllKeepError, ALL_KEEP_BROADCAST_STALE_SECS,
    ALL_KEEP_SCHEMA_VERSION, ALL_KEEP_SIGNAL_SUBDIR,
};
pub use all_stop_signal::{
    delete_stop_broadcast, is_stop_broadcast_stale, read_stop_broadcast, scan_stop_broadcasts_dir,
    stop_signal_path as all_stop_signal_path, stop_signals_dir as all_stop_signals_dir,
    write_stop_broadcast, write_stop_broadcast_signal, AllStopBroadcast, AllStopError,
    ALL_STOP_BROADCAST_STALE_SECS, ALL_STOP_SCHEMA_VERSION, ALL_STOP_SIGNAL_SUBDIR,
};
pub use cleanup::{clear_pair_label, exit_record_full, exit_record_preserve_pair};
pub use delta::{DeltaMode, DeltaResult, DeltaSnapshot};
pub use engine::{MeasureEngine, SessionSummary};
pub use exclusion::{
    check_record_exclusion, check_record_exclusion_at, count_distinct_pairings,
    count_distinct_pairings_at, is_heartbeat_fresh, ExclusionResult, MAX_ACTIVE_PER_PROJECT,
    STALE_SECONDS,
};
pub use hardware::{HardwareComponents, Match};
pub use identity::{Identity, License};
pub use io_thread_post::{
    format_pair_label, serialize_post_json, spawn_io_thread_post, TriggerPairResolutionFn,
    TriggerStopResolutionFn,
};
pub use io_thread_pre::{serialize_pre_json, spawn_io_thread_pre};
pub use license::{
    can_enter_record, can_read_preset, can_write_plugin_data, load_license_safe, show_note_button,
    show_save_button, show_stop_record_button, SENSE_RECORD_HINT, SENSE_UPSELL_URL,
};
pub use measure_thread::{live_window, pair_lock_active, spawn_measure_thread, LivenessEvaluator};
pub use pairing_scope::{
    discover_pre_dirs_for_post_project, enumerate_active_pre_pair_candidates_for_post_project,
    enumerate_active_pre_pair_candidates_for_post_project_in_session, read_pre_at,
    resolve_arm_target, resolve_arm_target_for_post_project,
    resolve_arm_target_for_post_project_in_session, select_target_pre, select_target_pre_for_arm,
    select_target_pre_for_arm_for_post_project,
    select_target_pre_for_arm_for_post_project_in_session, select_target_pre_for_post_project,
    select_target_pre_for_post_project_in_session, LatchedPre, LatchedPreState, SelectedPre,
};
pub use path_identity::{
    drain_path_events, guard_path_component, is_path_safe_component, materialize_observation_id,
    materialize_restore_field, normalize_restore_cell, surface_path_event, surface_path_event_for,
    take_path_event, MAX_COMPONENT_LEN,
};
pub use plugin_data::{
    append_annotation_to_latest, compact_wall_clock, verify_checksum, Annotation, BounceMarker,
    Frame, PluginDataFile, PluginDataWriter, PsbSnapshot, Role as PluginDataRole,
    Status as PluginDataStatus, WriterError as PluginDataWriterError, WriterPaths,
};
pub use post_candidates::{
    active_post_project_uuids_for_broadcast_scope, active_post_project_uuids_for_daw_session,
    broadcast_scope_ids_match, current_host_process_id, enumerate_active_post_pair_candidates,
    enumerate_active_post_pair_candidates_for_broadcast_scope,
    enumerate_active_post_pair_candidates_for_daw_session,
    host_scope_has_other_active_post_project, PostCandidate,
};
pub use pre_candidates::{
    enumerate_active_pre_pair_candidates, filter_candidates_by_name, pick_closest_pre,
    scan_pre_candidates, scan_pre_candidates_in, PostMetrics, PreCandidate,
};
pub use pre_discovery::{
    discover_active_pre_dir_for_pair, discover_active_pre_dirs, PostDiscoveryState,
    DISCOVERY_STALE_SECS,
};
pub use pre_self_discovery::{
    discover_pair_post_project_dir, PreSelfDiscoveryState,
    DISCOVERY_STALE_SECS as PRE_SELF_DISCOVERY_STALE_SECS,
};
pub use preset::{
    compute_preset_checksum, preset_dir, region_resolved, scan_valid_presets, verify_preset,
    PresetFile, Region as PresetRegion, VerifyError as PresetVerifyError, PRESET_SUBDIR,
};
pub use preset_dispatch::{
    dispatch_one as dispatch_preset_one, scan_any_presets, scan_latest_v2_preset,
    DispatchError as PresetDispatchError, PresetVariant,
};
pub use preset_v2::{
    compute_preset_v2_checksum, lookup_section_label, preset_dir_v2, scan_valid_presets_v2,
    verify_preset_v2, Card as PresetV2Card, PresetFileV2,
    SectionBoundary as PresetV2SectionBoundary, Summary as PresetV2Summary, VerifyErrorV2,
};
pub use record::{RecordState, RecordStateMachine, TransitionError};
pub use record_clock::{
    record_clock_bounds_for_record, record_window_for_buffer, record_window_for_buffer_with_bounds,
    RecordClockBounds, RecordWindow,
};
pub use record_expected::{
    claim_expected_metadata_for_session, expected_dir, expected_path,
    mark_expected_metadata_consumed, read_expected_metadata, write_expected_metadata,
    ExpectedMetadataError, ExpectedWavMetadata, EXPECTED_FILENAME, EXPECTED_SUBDIR,
};
pub use record_signal::{
    delete_signal, is_timed_out, mark_acknowledged, mark_released, mark_released_with_reason,
    read_signal, scan_signals_dir, signal_path, signals_dir, write_pending,
    write_pending_claiming_expected_and_clock, write_pending_with_expected,
    write_pending_with_expected_and_clock, write_signal, RecordSignal, ReleaseReason, SignalError,
    SignalStatus, ACK_TIMEOUT_SECONDS, RECORD_START_BARRIER_DELAY_MS, SIGNALS_SUBDIR,
    SIGNAL_FILENAME,
};
pub use record_take::{
    new_record_take_tracker, RecordTakeBlock, RecordTakeSnapshot, RecordTakeTracker,
    RECORD_TAKE_SOURCE_RENDER_CLOCK,
};
pub use record_writer::{
    new_record_trace_queue, RecordTraceKind, RecordTraceQueue, RecordTraceSample,
};
pub use storage::{
    cleanup_legacy_v1, load_installation_id_safe, load_or_recover, read_identity, write_both,
    write_identity_atomic, CleanupReport, IdentityCache, LoadStatus, LoadedIdentity, PlatformKind,
    PlatformPaths, StorageError, StoragePaths, CLEANUP_V1_DONE_FILENAME,
};
pub use watch_playback_pass::{
    add_watch_ring_cursor_samples, advance_watch_playback_pass_id,
    publish_watch_playback_pass_boundary, reset_watch_ring_cursor,
    watch_playback_block_duration_secs, watch_playback_pass_should_start,
    watch_ring_cursor_samples_for_pass,
};
pub use watchdog::{spawn_watchdog, IoThreadHandle, RestartIoFn, WatchdogIo, WatchdogParams};

use std::sync::atomic::{AtomicU8, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, OnceLock, RwLock};

// ── B-027 段階 2: PRE/POST 共通の Name 正規化 ────────────────────────────

/// Name 入力値を正規化 (R-28 機能的沈黙)。最大 16 文字。
///
/// B-077: 非 ASCII（日本語等 UTF-8 印字可能文字）を **保持**する。実害のある文字＝
/// 制御文字（`char::is_control`: 0x00-0x1F / 0x7F-0x9F 等）のみ除去し、先頭末尾の
/// 空白を trim する。`/` `\` `:` `"` 等は保持する: name はファイルシステムパスに
/// 使われず（pre.json 内容 + editor 表示のみ / WriterPaths は instance_id で構築）、
/// JSON 出力は `serialize_pre_json` が serde で escape するため安全。
///
/// chunk restore 時 / GUI 入力時の両方で使う。違反値は無言で正規化し UI エラーは出さない。
///
/// 用途:
/// - PRE 側 `params.name` (B-023 段階 1)
/// - POST 側 `params.pair_pre_name` (B-027 段階 2)
///
/// pairing 照合は両側が本関数で同一正規化するため、UTF-8 name でも exact 一致で当たる。
/// hypha_pre / hypha_post の両 cdylib から共通参照する単一情報源。
pub fn sanitize_name(raw: &str) -> String {
    let cleaned: String = raw.chars().filter(|c| !c.is_control()).collect();
    cleaned.trim().chars().take(16).collect()
}

// ── 共有定数 ────────────────────────────────────────────────────────────────

/// Audio Thread → Measure Thread リングバッファの保持長（秒）。
///
/// Offline bounce は DAW が実時間より速く Audio Thread を進めるため、短い ring では
/// Measure Thread が追いつく前に overflow し、Record の sample count が WAV と一致しなくなる。
/// Audio Thread はブロックできないので、余裕のある SPSC 容量で clean Record を優先する。
pub const RING_BUFFER_SECONDS: usize = 30;

/// 対応チャンネル数（ステレオ固定）。
pub const N_CHANNELS: usize = 2;

// ── プロセス単位識別子（B-020 / γ-3 chunk-persistent UUID 後）─────────────
//
// 履歴:
// - Phase 1.0: `PROJECT_HASH_PHASE1="default"` 固定値で全インスタンス共有 →
//   複数 Bus / 複数プロジェクトで衝突（致命級 A-3 / A-2）
// - A-3 中間策: プロセス単位 OnceLock<String> で起動時 1 度だけ生成 →
//   DAW 再起動で path が変わるためバウンス再計測の比較が困難
// - B-020 / γ-3 (本実装): nih-plug `#[persist = "project_uuid"]` でプロジェクト
//   chunk に UUID を保存。再オープンで同一値が復元され、PRE/POST が共有する
//
// 値は `OnceLock<Arc<RwLock<String>>>` セルにキャッシュされ、Plugin の
// `initialize()` から chunk-persist 値で `set_project_uuid()` / `set_daw_session_id()`
// により更新される。ファイル階層は `plugin_data/{project_uuid}/{instance_id}/{pre|post}/`
// で区切られる（bus 概念は path から削除済）。

fn project_uuid_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn daw_session_id_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

/// 新規 project_uuid を生成する（UUID v4 の文字列形式）。
///
/// chunk-persist 機構に保存される値の初期値として使用。Plugin が
/// `Default::default()` で `RwLock<String>` field を初期化する際の
/// fallback。プロジェクト保存後は chunk-persist 値が常に優先される。
pub fn generate_project_hash() -> String {
    uuid::Uuid::new_v4().to_string()
}

/// プロセス単位 `project_uuid` の現在値を読み取る。
///
/// セル空のときは lazy fallback で生成 UUID を返す（テスト・diagnostics 用途）。
/// 通常運用では Plugin の `initialize()` が `set_project_uuid()` で
/// chunk-persist 値をセットした直後に呼ばれる。
///
/// 関数名は backward compat のため `process_project_hash` のまま。値の
/// セマンティクスは「chunk-persistent project UUID」へ変わった。
pub fn process_project_hash() -> String {
    let cell = project_uuid_cell();
    let current = cell.read().map(|g| g.clone()).unwrap_or_default();
    if current.is_empty() {
        let fresh = generate_project_hash();
        if let Ok(mut g) = cell.write() {
            if g.is_empty() {
                *g = fresh.clone();
            }
            g.clone()
        } else {
            fresh
        }
    } else {
        current
    }
}

/// プロセス単位 `project_uuid` セルを上書きする（chunk-persist 値の反映用）。
///
/// Plugin の `initialize()` が `params.project_uuid`（`#[persist = "project_uuid"]`）
/// から取得した値で本関数を呼ぶ想定。空文字列を渡してもセルを空にする
/// （次回 `process_project_hash()` 呼び出しで lazy fallback が走る）。
pub fn set_project_uuid(uuid: String) {
    if let Ok(mut g) = project_uuid_cell().write() {
        *g = uuid;
    }
}

/// セル現在値を peek する（lazy fallback なし）。
///
/// `process_project_hash` と異なり、セルが空（誰も `set_project_uuid` を
/// 呼んでいない状態）であれば空文字列を返す。POST 側の
/// `sync_project_uuid_from_pre` で「PRE が既に値をセットしたか」を判定する
/// のに使う。lazy fallback されると POST 自身が cell を初期化してしまい、
/// PRE 後発時に上書きされる race の起点になるため、明示的に区別する。
pub fn peek_project_uuid() -> String {
    project_uuid_cell()
        .read()
        .map(|g| g.clone())
        .unwrap_or_default()
}

/// プロセス単位 `daw_session_id` の現在値を読み取る。
///
/// B-020 以降は `#[persist = "daw_session_uuid"]` で chunk に保存される
/// 値が `set_daw_session_id()` 経由でセルに反映される。`record_signal.json`
/// の content に同梱されるが、PRE 側 ack filter には使わない。AU/VST3 や別
/// cdylib 境界で PRE/POST の static cell が一致しないため、record_signal の
/// 正本は永続 `target_pre_instance_id` 一致に寄せる。
pub fn daw_session_id() -> String {
    let cell = daw_session_id_cell();
    let current = cell.read().map(|g| g.clone()).unwrap_or_default();
    if current.is_empty() {
        let fresh = uuid::Uuid::new_v4().to_string();
        if let Ok(mut g) = cell.write() {
            if g.is_empty() {
                *g = fresh.clone();
            }
            g.clone()
        } else {
            fresh
        }
    } else {
        current
    }
}

/// プロセス単位 `daw_session_id` セルを上書きする（chunk-persist 値の反映用）。
pub fn set_daw_session_id(uuid: String) {
    if let Ok(mut g) = daw_session_id_cell().write() {
        *g = uuid;
    }
}

// ── B-110: 共有 identity セルの本番リセット（番人裁定 a / grace なし）─────────
//
// 問題: 1 つの DAW プロセスが複数プロジェクトを順次開くと、上記 `OnceLock` セルが
// 前プロジェクトの project_uuid / daw_session_id を保持し続け、新プロジェクトが
// 古い棚へ書く leak（全インスタンス破棄後も stale 値が残る / refcount・clear 不在）。
//
// 解: live インスタンス数の refcount を持ち、最後の 1 個が消えた（refcount 0）瞬間に
// 共有セルを clear する。次プロジェクトの最初のインスタンスが通常の seed 規則
// （egui: initialize の set_project_uuid / FFI: enable の first-wins resolve）で
// 新しい値を入れ直す。番人裁定 a = grace なし: 全削除→即追加は新 UUID を seed する。
//
// A-1 判定（otool -L / crate-type 実測）: kirin_measure は rlib（静的リンク）で、
// PRE/POST × egui/JUCE の各バンドルが自前の static コピーを持つ（**per-binary**）。
// したがって refcount もセルも per-binary。各バイナリは自分の leak を自分で解消し、
// 横断整合は既存の filesystem 経路（/tmp・plugin_data）が担う（ZSA / 意味論は同一）。
//
// clear が live io_thread の読みと競合しない理由（A-3 実測）:
// - egui PRE io_thread は `project_hash: String`（spawn 時 snapshot）を持つ＝セル非参照。
// - egui POST io_thread はインスタンス固有の `Arc<RwLock<String>>` field を読む＝global 非参照。
// - FFI POST io_thread は role-scoped セルの Arc clone を読むが、`Drop` が io_thread を
//   **join した後**に detach するため、refcount 0 時には生存 io_thread が無い。
// セル handle（`Arc<RwLock<String>>`）は io_thread の live-read 契約のため不変に保ち、
// 「clear」は内側 `String` を空にする（空 = 未 seed は既存 lazy-fallback 規約と一致）。

/// 共有 identity セルの本番リセットを駆動する refcount + lock。
///
/// `attach`（インスタンス生成）/`detach`（破棄）は **非 RT 経路**専用（create/Default・
/// destroy/Drop）。`detach` が refcount 0 へ遷移したときだけ、渡された `clear` を **lock 下**で
/// 実行する。lock は attach の +1 と detach の −1+clear を直列化し、「破棄→0→clear」と
/// 「生成→seed」のレースで新 seed が消える事故を防ぐ（B-3）。`processBlock` には一切持ち込まない。
///
/// `clear` closure に共有セルへの参照を閉じ込めることで、global static を触らずに単体テスト
/// できる（B-106 `resolve_shared_id` と同方針）。
struct IdentityLifecycle {
    refcount: AtomicUsize,
    lock: Mutex<()>,
}

impl IdentityLifecycle {
    const fn new() -> Self {
        Self {
            refcount: AtomicUsize::new(0),
            lock: Mutex::new(()),
        }
    }

    /// インスタンス生成。refcount を +1 し、遷移後の値を返す。
    fn attach(&self) -> usize {
        let _guard = self.lock.lock().unwrap_or_else(|e| e.into_inner());
        self.refcount.fetch_add(1, Ordering::SeqCst) + 1
    }

    /// インスタンス破棄。refcount を −1 し、0 到達時のみ `clear` を lock 下で実行する。
    /// 既に 0 のときは何もしない（過剰 destroy の underflow 防御 / 再 clear なし）。遷移後の値を返す。
    fn detach<F: FnOnce()>(&self, clear: F) -> usize {
        let _guard = self.lock.lock().unwrap_or_else(|e| e.into_inner());
        let prev = self.refcount.load(Ordering::SeqCst);
        if prev == 0 {
            return 0;
        }
        let now = prev - 1;
        self.refcount.store(now, Ordering::SeqCst);
        if now == 0 {
            clear();
        }
        now
    }

    fn count(&self) -> usize {
        self.refcount.load(Ordering::SeqCst)
    }
}

static IDENTITY_LIFECYCLE: IdentityLifecycle = IdentityLifecycle::new();

/// project_uuid / daw_session_id セルを空に戻す（内側 `String` を clear）。
///
/// セル handle（`Arc<RwLock<String>>`）は不変のまま中身だけ空にするため、io_thread の Arc clone
/// live-read 契約を壊さない。空セルは次 seed で `set_project_uuid` / first-wins resolve / lazy
/// 生成のいずれかにより埋め直される。
pub fn clear_shared_identity_cells() {
    if let Ok(mut g) = project_uuid_cell().write() {
        g.clear();
    }
    if let Ok(mut g) = daw_session_id_cell().write() {
        g.clear();
    }
}

/// プラグインインスタンス生成時に呼ぶ（refcount +1）。create / `Default` フックから。
/// `enable` には置かない（冪等 early-return で増減が崩れるため / A-3）。
pub fn identity_instance_attach() {
    IDENTITY_LIFECYCLE.attach();
}

/// プラグインインスタンス破棄時に呼ぶ（refcount −1）。destroy / `Drop` フックから、
/// かつ当該インスタンスの io/measure thread 停止（FFI は join）後に呼ぶこと。
///
/// refcount 0 到達時に kirin_measure 共有セル（project_uuid / daw_session_id）を clear し、
/// 続けて `clear_extra`（呼び出し側バイナリ固有のセル clear。FFI の role-scoped 4 セル等。
/// egui は追加セルが無いので no-op closure）を **同一 lock 下**で実行する。
pub fn identity_instance_detach<F: FnOnce()>(clear_extra: F) {
    IDENTITY_LIFECYCLE.detach(|| {
        clear_shared_identity_cells();
        clear_extra();
    });
}

/// 現在の live インスタンス refcount（テスト・diagnostics 用）。
pub fn identity_refcount() -> usize {
    IDENTITY_LIFECYCLE.count()
}

/// プロセス起動後 1 回だけ旧構造（`default/MIX/`）の cleanup を実行する。
///
/// `Plugin::default()` から呼び出す想定。OnceLock で 1 度きりの実行を保証。
/// 既に `.cleanup_v1_done` flag が立っていれば中身はノーオペ（再 cleanup なし）。
pub fn ensure_legacy_cleanup_done() {
    static DONE: OnceLock<()> = OnceLock::new();
    DONE.get_or_init(|| {
        if let Ok(paths) = storage::StoragePaths::default_platform() {
            let report = storage::cleanup_legacy_v1(&paths);
            log::info!(
                "[startup] cleanup_v1: ran={} removed={} errors={}",
                report.ran,
                report.removed,
                report.errors
            );
        }
    });
}

// ── SignalState（advisor_signal_state_spec SS-1）──────────────────────────

/// Audio Thread が宣言する信号状態。パイプライン全体がこの値に従う。
///
/// - Audio Thread が毎 `process()` で `AtomicU8` に書き込む（ロックフリー）
/// - Measure Thread が毎サイクル先頭で読み取り、Active 以外なら compute() スキップ
/// - IO Thread が JSON に `"signal_state"` として出力
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SignalState {
    /// 信号あり・バイパスなし → 計測する
    Active = 0,
    /// DAW バイパス中 → 計測しない
    Bypassed = 1,
    /// transport 停止 or バッファ全ゼロ → 計測しない
    Inactive = 2,
}

impl SignalState {
    /// `AtomicU8` の値から変換。未知の値は `Inactive`（安全側に倒す）。
    pub fn from_u8(v: u8) -> Self {
        match v {
            0 => Self::Active,
            1 => Self::Bypassed,
            _ => Self::Inactive,
        }
    }

    /// JSON 出力用の文字列。
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Bypassed => "bypassed",
            Self::Inactive => "inactive",
        }
    }
}

/// `AtomicU8` から `SignalState` を読み取るヘルパー。
pub fn load_signal_state(atom: &AtomicU8) -> SignalState {
    SignalState::from_u8(atom.load(Ordering::Relaxed))
}

/// `AtomicU8` に `SignalState` を書き込むヘルパー。
pub fn store_signal_state(atom: &AtomicU8, state: SignalState) {
    atom.store(state as u8, Ordering::Relaxed);
}

// ── 計測結果型 ───────────────────────────────────────────────────────────────

/// Perceptual Spectral Balance 要約（low / mid / high）。
///
/// Kirin-original metric。
///
/// - low  : Bark 1–8   ISO 532-1 specific loudness 由来 [dB]   (~20 Hz–920 Hz)
/// - mid  : Bark 9–16  ISO 532-1 specific loudness 由来 [dB]   (~920 Hz–3400 Hz)
/// - high : Bark 21–24 + 15.5k–20kHz **FFT エネルギー由来** [dB] (5800 Hz–20000 Hz)
///
/// **重要**: high の意味論は C-3 で変更された。旧仕様 (Bark 17–20 specific loudness)
/// は廃止し、並存させない。high のみ FFT power → 10·log10 で dB 化しているため、
/// low/mid (sone/Bark 由来) と数値の order が異なる場合がある。
/// 比較・加算する際は単位が異なることに注意。
///
/// ISO 532-1 由来のフィールド (n_prime[20] / psb_bark[20] / sharpness / n_prime_total)
/// は引き続き Bark 1–20 specific loudness ベースで C-3 では変更されない。
#[derive(Debug, Clone, Default)]
pub struct PsbSummary {
    /// Bark 1–8 specific loudness sum (sone/Bark) → dB
    pub low: f64,
    /// Bark 9–16 specific loudness sum (sone/Bark) → dB
    pub mid: f64,
    /// Bark 21–24 + 15.5k–20kHz FFT energy (linear power) → dB
    pub high: f64,
}

///
/// Measure Thread が更新し、IO Thread と GUI Thread が読む。
/// `Arc<Mutex<MeasureResult>>` で共有する。
#[derive(Debug, Clone, Default)]
pub struct MeasureResult {
    /// Measure Thread が共有結果を書き込むたびに進める単調 sequence。
    /// GUI の freshness 判定専用で、Record/TRACE/plugin_data の正本値には使わない。
    pub measure_sequence: u64,

    /// Audio Thread が transport playback pass 境界で進める id。
    /// Watch 表示の「再生 pass ごとの最大値」reset 境界として使う。
    pub playback_pass_id: u64,

    /// LUFS-M: ITU-R BS.1770-4 Momentary Loudness（400ms sliding）。
    /// 信号なし・ウィンドウ未満は `None`（GUI 表示 `---`）。
    pub lufs_m: Option<f64>,

    /// True Peak「直近」tp_recent: ITU-R BS.1770-4 Annex 2（4× oversampling, dBTP）。
    /// 直近 400ms（LUFS-M と同窓・フレーム基準）の inter-sample 最大値（B-074）。Watch 表示用。
    /// （B-074 以前: wall-clock 2 秒窓を「400ms 累積max」と誤記していた。実体を 400ms に統一）。
    /// 0 dBTP 超 → GUI で赤表示（G-53-02）。
    pub true_peak: Option<f64>,

    /// True Peak「セッション最大」tp_session_max: init（reset）以降の inter-sample running
    /// max（dBTP / B-074）。Record/.kirin の正本（`SessionSummary.max_true_peak` と同一定義）。
    /// Watch でも live に算出される（compute 毎）。
    pub tp_session_max: Option<f64>,

    /// Crest Factor: peak_dBFS − RMS_dBFS（400ms window）。
    /// peak はサンプルピーク（True Peak ではない。PSR 計算との整合）。
    pub crest: Option<f64>,

    /// PSR: peak_dBFS − LUFS_S（3s Short-term Loudness）。
    /// Watch GUI では非表示。IO Thread が /tmp/ に書き込む（G-52-02）。
    /// 3 秒未満は `None`。
    pub psr: Option<f64>,

    /// ISO 532-1 Zwicker の総ラウドネス N (specific loudness N'(z) ではない)
    /// を `temporal_weighting` LP IIR で時系列平滑化した値。GUI label は "N"。
    /// field 名 `n_prime_total` は履歴互換維持 (Phase 2 で rename 候補)。
    /// 48 kHz 以外は常に `None`。
    pub n_prime_total: Option<f64>,

    pub sharpness: Option<f64>,

    pub psb_summary: Option<PsbSummary>,

    /// plugin_data/.../post/*.json `Frame.n_prime[20]` に直接書き込む値。
    pub n_prime: Option<[f64; 20]>,

    /// plugin_data/.../post/*.json `psb_snapshots[].psb` に直接書き込む値。
    /// 3 帯域集約 (low/mid/high, dB) は `psb_summary` 側。
    pub psb_bark: Option<[f64; 20]>,
}

// ── B-110: 共有セル本番リセットの単体テスト（C-1〜C-5 / 全て非実機）─────────
//
// A-1 判定が **per-binary**（rlib 静的リンク・各バンドルが自前 static）のため、C-1〜C-5 は
// 単一バイナリ内の意味論として記述する（横断整合は filesystem 経路）。C-1〜C-5 はローカル
// `IdentityLifecycle` + ローカルセルで駆動し、global static を触らない（並列テスト安全 /
// B-106 と同方針）。global path（`clear_shared_identity_cells` が実セルを空にする）は専用 1
// テストで検証する（この 1 件のみが global セルを触る）。
#[cfg(test)]
mod b110_identity_reset_tests {
    use super::{
        clear_shared_identity_cells, daw_session_id_cell, peek_project_uuid, project_uuid_cell,
        set_daw_session_id, set_project_uuid, IdentityLifecycle,
    };
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::{Arc, RwLock};

    type Cell = Arc<RwLock<String>>;

    fn new_cell() -> Cell {
        Arc::new(RwLock::new(String::new()))
    }
    fn read(cell: &Cell) -> String {
        cell.read().unwrap().clone()
    }
    fn clear(cell: &Cell) {
        cell.write().unwrap().clear();
    }
    /// `set_project_uuid` 相当: 上書き seed（egui initialize の chunk-persist 反映）。
    fn set_value(cell: &Cell, v: &str) {
        *cell.write().unwrap() = v.to_string();
    }
    /// `resolve_shared_id` 相当: first-wins set-if-empty（FFI enable の seed）。
    fn seed_if_empty(cell: &Cell, candidate: &str) -> String {
        let mut g = cell.write().unwrap();
        if g.is_empty() {
            *g = candidate.to_string();
        }
        g.clone()
    }
    /// `process_project_hash` 相当: 空なら一意値を生成、非空なら現値（lazy fallback）。
    fn seed_or_generate(cell: &Cell, gen: &str) -> String {
        let mut g = cell.write().unwrap();
        if g.is_empty() {
            *g = gen.to_string();
        }
        g.clone()
    }

    // C-1 [番人固定 i]: 全削除→即追加 = 新 UUID（grace なし / 裁定 a のエッジ仕様化）。
    #[test]
    fn c1_delete_all_then_immediate_add_seeds_new_uuid() {
        let lc = IdentityLifecycle::new();
        let cell = new_cell();
        // インスタンス 1 生成 → lazy 生成で seed。
        lc.attach();
        let uuid1 = seed_or_generate(&cell, "uuid-gen-1");
        // 全削除（refcount 0）→ clear。
        lc.detach(|| clear(&cell));
        assert_eq!(lc.count(), 0);
        assert!(
            read(&cell).is_empty(),
            "refcount 0 で共有セルが clear される"
        );
        // 即追加（新インスタンス）→ 空セルなので新しい値を生成。
        lc.attach();
        let uuid2 = seed_or_generate(&cell, "uuid-gen-2");
        assert_ne!(
            uuid1, uuid2,
            "全削除→即追加は前 UUID を引き継がず新 UUID を seed する（grace なし）"
        );
    }

    // C-2 [番人固定 ii]: プロジェクト切替で clear（P1 回帰: 0→0 遷移後の enable が
    // 前プロジェクト値を引き継がない）。
    #[test]
    fn c2_project_switch_does_not_inherit_previous_value() {
        let lc = IdentityLifecycle::new();
        let cell = new_cell();
        // プロジェクト 1: initialize が chunk-persist 値を set。
        lc.attach();
        set_value(&cell, "project-1-uuid");
        assert_eq!(read(&cell), "project-1-uuid");
        // プロジェクト 1 を閉じる（全インスタンス破棄）→ clear。
        lc.detach(|| clear(&cell));
        // プロジェクト 2（chunk に project_uuid 未保存＝空）: lazy 生成。
        lc.attach();
        let v = seed_or_generate(&cell, "project-2-fresh");
        assert_ne!(
            v, "project-1-uuid",
            "次プロジェクトは前プロジェクトの project_uuid を引き継がない（leak 解消）"
        );
    }

    // C-3 [番人固定 iii]: 同一 chunk 復元で同値に再収束。
    #[test]
    fn c3_chunk_restore_reconverges_to_same_value() {
        let lc = IdentityLifecycle::new();
        let cell = new_cell();
        lc.attach();
        set_value(&cell, "chunk-uuid-X");
        lc.detach(|| clear(&cell));
        assert!(read(&cell).is_empty(), "clear 後は空");
        // 同一プロジェクトを再オープン: chunk が同 UUID を復元 → set。
        lc.attach();
        set_value(&cell, "chunk-uuid-X");
        assert_eq!(
            read(&cell),
            "chunk-uuid-X",
            "同一 chunk の復元値で同値に再収束する"
        );
    }

    // C-4: destroy×create レース。0 遷移 clear と並行 create seed が lock で相互排除され、
    // 生存インスタンスの seed が消されない（生存 refcount のセルは決して空でない）。
    #[test]
    fn c4_destroy_create_race_preserves_surviving_seed() {
        use std::thread;
        for i in 0..400 {
            let lc = Arc::new(IdentityLifecycle::new());
            let cell = new_cell();
            // 初期状態: インスタンス 1 が生存・seed 済み。
            lc.attach();
            set_value(&cell, "old");

            // 破棄側: 旧インスタンスを detach（refcount 0 へ落ちれば clear）。
            let lc_d = Arc::clone(&lc);
            let cell_d = cell.clone();
            let d = thread::spawn(move || {
                lc_d.detach(move || clear(&cell_d));
            });
            // 生成側: 新インスタンスを attach し first-wins で seed。
            let lc_c = Arc::clone(&lc);
            let cell_c = cell.clone();
            let c = thread::spawn(move || {
                lc_c.attach();
                seed_if_empty(&cell_c, "new");
            });
            d.join().unwrap();
            c.join().unwrap();

            // attach 1（old）+ attach 1（new）− detach 1 = refcount 1（new が生存）。
            assert_eq!(lc.count(), 1, "iter {i}: 生存インスタンスは 1");
            assert!(
                !read(&cell).is_empty(),
                "iter {i}: 生存インスタンスのセルが空になってはならない（new seed が clear に消されない）"
            );
        }
    }

    // C-5: refcount 増減の単体（create/destroy で正しく増減・0 でのみ clear・二重 destroy 防御）。
    #[test]
    fn c5_refcount_inc_dec_and_double_detach_is_noop() {
        let lc = IdentityLifecycle::new();
        let cleared = AtomicUsize::new(0);
        let bump = || {
            cleared.fetch_add(1, Ordering::SeqCst);
        };
        assert_eq!(lc.count(), 0);
        assert_eq!(lc.attach(), 1);
        assert_eq!(lc.attach(), 2);
        assert_eq!(lc.detach(bump), 1);
        assert_eq!(
            cleared.load(Ordering::SeqCst),
            0,
            "0 でないので clear しない"
        );
        assert_eq!(lc.detach(bump), 0);
        assert_eq!(
            cleared.load(Ordering::SeqCst),
            1,
            "0 到達でちょうど 1 度 clear する"
        );
        // 過剰 destroy: underflow させず・再 clear もしない。
        assert_eq!(lc.detach(bump), 0);
        assert_eq!(lc.count(), 0);
        assert_eq!(
            cleared.load(Ordering::SeqCst),
            1,
            "二重 detach は no-op（再 clear なし / underflow なし）"
        );
    }

    // global path: `clear_shared_identity_cells` が実 global セルを空に戻す。
    // ※ 本テストのみが global project_uuid / daw_session_id セルを触る（並列衝突回避）。
    #[test]
    fn global_clear_empties_real_identity_cells() {
        set_project_uuid("global-proj".to_string());
        set_daw_session_id("global-daw".to_string());
        assert_eq!(peek_project_uuid(), "global-proj");
        assert_eq!(daw_session_id_cell().read().unwrap().clone(), "global-daw");

        clear_shared_identity_cells();

        assert!(
            peek_project_uuid().is_empty(),
            "clear 後 project_uuid セルは空（次 seed で埋め直し）"
        );
        assert!(
            project_uuid_cell().read().unwrap().is_empty(),
            "project_uuid セル内側が空"
        );
        assert!(
            daw_session_id_cell().read().unwrap().is_empty(),
            "clear 後 daw_session_id セルは空"
        );
    }
}
