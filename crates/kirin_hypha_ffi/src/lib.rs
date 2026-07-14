//! kirin_hypha_ffi — Kirin Hypha JUCE 移植の C ABI ラッパ。
//!
//! 方式 B2: 検証済み Rust ランタイム(`kirin_measure`)を **無変更** で C ABI に包む。
//! C++/JUCE 側に DSP・計測ロジックを一切移さない（計測器は精度が製品そのもの）。
//!
//! # C ABI surface（すべて実装済み）
//! - RT 計測: `create` / `set_signal_state` / `push_samples` / `poll_result` / `destroy`。
//! - Record: `set_license` / `exit_record` と `poll_session`
//!   (LUFS-I/LRA/max_true_peak)。SessionSummary は `engine.finalize()` 由来で Record 中にのみ
//!   成立する量で、Measure Thread が **自律的に** finalize して `session_summary` を充填する
//!   （measure_thread.rs:290-295）。FFI は RecordStateMachine を flip するだけ（exit で finalize
//!   を呼ばない＝finalize は Measure Thread のみ / engine.rs:161）。`poll_session` は Record
//!   finalize 後に値を返す（Record 前は false）。
//! - state chunk 識別子: `set_identity` / `get_identity`（方式A）。
//! - plugin_data IO: `enable_pre_writes`（PRE: Watch pre.json + Record frames/PSB）/
//!   `enable_post_writes`（POST: post.json の生メトリクス + Δ を select_target_pre 経由で算出）。
//!   filesystem 書込は kirin_measure の io_thread 内に閉じる（FFI は spawn と識別子注入のみ）。
//! - PRE-POST ペアリング: `set_pair_target` / `keep` / `stop` / `poll_delta` /
//!   `enumerate_post_pair_claims`
//!   （POST Keep → PRE が record_signal を ack して自律的に Record に入る）。
//! - Mark: `add_mark`（Record中のproducer sample位置へ Good/Fix/Hold を記録）。
//!
//! # スレッドモデル（本番 hypha_pre/post と同一の入口を使う）
//! `create` は本番の実運用入口 `kirin_measure::spawn_measure_thread`(measure_thread.rs:59) で
//! Measure Thread を起動し、B-118 で T-8 Watchdog を再採用する（Measure crash の自動再起動 +
//! io は Lazy 監視 / B-056 opt-out 撤回）。IO Thread は enable_*_writes で後発 spawn する。
//! - `push_samples`: **Audio Thread 単独**。rtrb Producer への lock-free push + heartbeat++。
//!   アロケーション/lock/syscall なし（RT-safe）。Record 中も読むだけ（R-12）。
//! - `poll_result` / `poll_session` : **UI Thread**。`try_lock`（非ブロッキング）。
//!
//! ## heartbeat（必須配線）
//! Measure Thread は heartbeat が ~3s 変化しないと（B-118/G-115-245: LivenessEvaluator の
//! live window）signal_state を Inactive に上書きし結果を clear する。本番は host の `process()`
//! が毎回 `heartbeat.fetch_add(1)`
//! していた(hypha_pre.rs:390)。本 FFI では **`push_samples` が heartbeat を進める**。

use std::cell::UnsafeCell;
use std::collections::HashMap;
use std::ffi::CStr;
use std::os::raw::{c_char, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU32, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, OnceLock, RwLock};
use std::thread::JoinHandle;
use std::time::{SystemTime, UNIX_EPOCH};

use uuid::Uuid;

use kirin_measure::engine::SessionSummary;
use kirin_measure::reservation; // B-127 (G-115-364): per-pairing O_EXCL reservation
use kirin_measure::{
    active_post_project_uuids_for_operation_group, append_annotation_to_latest,
    can_write_plugin_data, check_record_exclusion, count_distinct_pairings,
    current_host_process_id, enqueue_record_mark,
    enumerate_live_pre_pair_choices_for_post_project_in_session,
    enumerate_owned_post_pair_candidates_for_operation_group,
    enumerate_ready_post_pair_candidates_for_operation_group, identity_instance_attach,
    identity_instance_detach, latch_selected_pre, live_post_project_uuids_for_operation_group,
    live_window, load_license_safe, load_signal_state, mark_expected_metadata_consumed,
    mark_released, mark_released_with_reason, new_record_mark_queue, new_record_take_tracker,
    new_record_trace_queue, pair_status_for_post, pair_status_for_pre, paired_pre_instance_id,
    resolve_arm_target_for_post_project_in_session, sanitize_name,
    select_live_pre_pair_choice_by_instance_for_post_project_in_session, set_daw_session_id,
    set_project_uuid, spawn_io_thread_post, spawn_io_thread_pre, spawn_measure_thread,
    spawn_watchdog, store_signal_state, write_broadcast, write_expected_metadata,
    write_pending_claiming_expected_and_clock, write_stop_broadcast, CaptureClockSource, DeltaMode,
    DeltaResult, ExclusionResult, ExpectedWavMetadata, IoThreadHandle, LatchedPre, License,
    LiveLicense, LivenessEvaluator, MeasureResult, PairStatus, PlatformPaths, PluginDataRole,
    PresentationLatencySamples, PresentationLatencySource, PsbSummary, RecordMarkQueue,
    RecordStateMachine, RecordTakeBlock, RecordTakeTracker, RecordTraceQueue, ReleaseReason,
    RestartIoFn, SignalState, StoragePaths, WatchMaxTracker, WatchdogIo, WatchdogParams,
    MAX_ACTIVE_PER_PROJECT, N_CHANNELS, RING_BUFFER_SECONDS,
};
use kirin_measure::{
    add_watch_ring_cursor_samples, publish_watch_playback_pass_boundary, reset_watch_ring_cursor,
};

mod pair_binding;

use pair_binding::{PairBinding, PairTargetTransition};

/// state chunk 往復する識別子（方式A: JUCE が chunk bytes を所有・FFI は文字列 get/set のみ）。
/// `project_hash` は派生値（= 確定後の `project_uuid` / B-106 共有セル解決値）で永続対象外。
#[derive(Default, Clone)]
struct IdentityState {
    instance_id: String,
    project_uuid: String,
    daw_session_uuid: String,
    name: String,
    /// enable 時に確定する派生 project_hash（= project_uuid）。add_annotation の path に使う。
    project_hash: String,
}

#[derive(Debug, Clone)]
pub struct ExpectedWavMetadataInput {
    pub bounce_id: String,
    pub expected_duration_samples: u64,
    pub expected_sample_rate: u32,
    pub wav_path: String,
    pub wav_file_size: u64,
    pub wav_mtime_ms: i64,
    pub wav_hash: String,
}

fn supported_channel_count(num_channels: u32) -> usize {
    match num_channels {
        1 | 2 => num_channels as usize,
        _ => N_CHANNELS,
    }
}

/// C ABI 識別子バッファ長（UUID 36 + null に十分）。
const ID_BUF_LEN: usize = 64;

/// Rust `&str` を C 文字列バッファへ書く（truncate + null 終端）。
fn write_c_buf(dst: &mut [c_char; ID_BUF_LEN], src: &str) {
    let bytes = src.as_bytes();
    let n = bytes.len().min(ID_BUF_LEN - 1);
    for (i, b) in bytes.iter().take(n).enumerate() {
        dst[i] = *b as c_char;
    }
    dst[n] = 0;
}

/// C 文字列ポインタを Rust `String` に読む（null/不正は空文字）。
///
/// # Safety
/// `p` は null または有効な null 終端 C 文字列であること。
unsafe fn read_c_str(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

// B-118: IoThreadHandle は kirin_measure に移動（watchdog の Lazy slot 共有型）。FFI は import する。

// `set_license` の C ABI コード。identity.rs:46 の enum 宣言順に一致させる:
//   License { Os, Sense, Unknown } → 0=Os / 1=Sense / 2=Unknown。
// 未知値は安全側 Unknown（Record 不可）に倒す（License::parse_loose と同じ安全側設計）。
const LICENSE_OS: u8 = 0;
const LICENSE_SENSE: u8 = 1;
const LICENSE_UNKNOWN: u8 = 2;

/// C ABI コード → `License`（未知は安全側 Unknown）。
fn license_from_abi(abi: u8) -> License {
    match abi {
        LICENSE_OS => License::Os,
        LICENSE_SENSE => License::Sense,
        _ => License::Unknown,
    }
}

/// `License` → C ABI コード（`load_license` の戻り値）。
fn license_to_abi(license: License) -> u8 {
    match license {
        License::Os => LICENSE_OS,
        License::Sense => LICENSE_SENSE,
        License::Unknown => LICENSE_UNKNOWN,
    }
}

fn epoch_secs_now() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs_f64())
        .unwrap_or(0.0)
}

// ─────────────────────────────────────────────────────────────────────────────
// Rust-safe core（tests/parity.rs はこの API 経由で FFI を駆動する）
// ─────────────────────────────────────────────────────────────────────────────

/// RT 計測ランタイムのハンドル。C ABI からは不透明ポインタ。
pub struct KirinHyphaEngine {
    /// Audio Thread → Measure Thread の rtrb Producer。
    /// `UnsafeCell` 越しに **Audio Thread 単独** で push する（SPSC 契約）。
    ring_producer: UnsafeCell<rtrb::Producer<f32>>,
    /// Measure Thread が 100ms cadence で更新、UI Thread が読む。
    measure_result: Arc<Mutex<MeasureResult>>,
    /// POST の Δ 結果（B-060 3d-a）。POST io_thread の run_tick が select_target_pre で
    /// 選んだ PRE との差分を書き、`poll_delta` が読む（GUI 表示用）。PRE では未更新。
    delta_result: Arc<Mutex<DeltaResult>>,
    /// Record 中、Measure Thread が毎ループ `engine.finalize()` を書き込む
    /// （measure_thread.rs:290-295）。Watch では未更新（Record→Watch で直近値を保持）。
    session_summary: Arc<Mutex<Option<SessionSummary>>>,
    /// Offline bounce 用 TRACE queue（Measure → IO）。
    record_trace_queue: RecordTraceQueue,
    /// Audio Thread が積む実レンダー長。Record close 時に bounce_take の正本になる。
    record_take_tracker: Arc<RecordTakeTracker>,
    /// UI Thread → POST IO writer. PRE engines keep the queue empty.
    record_mark_queue: RecordMarkQueue,
    /// Audio Thread が宣言する信号状態（Measure Thread が読む）。
    signal_state: Arc<AtomicU8>,
    /// Measure Thread 停止フラグ（destroy でセット → join）。
    shutdown: Arc<AtomicBool>,
    /// process() 相当の heartbeat（push_samples が進める）。
    heartbeat: Arc<AtomicU32>,
    /// Watch MAX 用 playback pass id。Audio Thread の transport通知で進む。
    watch_playback_pass_id: Arc<AtomicU64>,
    watch_playback_pass_cutover_samples: Arc<AtomicU64>,
    watch_max: Mutex<WatchMaxTracker>,
    transport_previous_playing: AtomicBool,
    transport_previous_position_valid: AtomicBool,
    transport_previous_position_samples: AtomicI64,
    transport_previous_num_frames: AtomicU64,
    /// Watch ring cursor の seqlock epoch。
    watch_ring_cursor_epoch: Arc<AtomicU64>,
    /// 現在の ring cursor が属する full playback pass id。
    watch_ring_cursor_pass_id: Arc<AtomicU64>,
    /// 現在の ring 世代へ成功 push 済み sample 数。
    watch_ring_cursor_samples: Arc<AtomicU64>,
    /// Watchdog が新 ring を作り、FFI push 経路側の producer swap を待っている間 true。
    watch_ring_replacing: Arc<AtomicBool>,
    /// B-118: 単一鮮度評価器。editor が POST pair lock の live 述語として読み（`kirin_hypha_heartbeat_live`
    /// getter 経由）、Measure Thread / watchdog も同一評価器を読む（signal_state とは別軸）。
    liveness: Arc<LivenessEvaluator>,
    /// Record 状態機械。`enter_record`/`exit_record` で flip し、Measure Thread が
    /// `is_recording()` を見て自律 finalize する（Phase 3a で実配線）。
    record_sm: Arc<RecordStateMachine>,
    /// JUCE shell が最後に通知した host transport sample position。Keep 時に
    /// record_signal の native start barrier を作るために読む。
    latest_position_valid: Arc<AtomicBool>,
    latest_position_samples: Arc<AtomicI64>,
    /// 現ライセンス（C ABI コード: 0=Os 1=Sense 2=Unknown）。`enter_record` の
    /// 二重 gate（E-21）に使う。既定は Unknown（Record 不可）。
    /// B-102: `Arc<AtomicU8>` 化（broadcast 受信 closure が live に読むため / keep と同一 gate）。
    license: LiveLicense,
    /// `spawn_io_thread_pre` に渡す入力サンプルレート（create 時に保持）。
    sample_rate: u32,
    /// create 時に確定した入力チャンネル数。1=mono / 2=stereo。
    num_channels: usize,
    /// PRE/POST io_thread（B-057 3b / B-060 3d-a）。`enable_pre_writes` or
    /// `enable_post_writes` で 1 度だけ起動。B-118: watchdog（Lazy）と Arc 共有し、watchdog が
    /// is_finished 監視・crash 時 re-spawn・shutdown 時 join する。
    io_thread: Arc<Mutex<Option<IoThreadHandle>>>,
    /// B-118: io 再起動クロージャ（enable が全 spawn 引数 capture でセット）。watchdog が io crash 時に呼ぶ。
    io_restart_slot: Arc<Mutex<Option<RestartIoFn>>>,
    /// B-118: watchdog の measure 再起動が新 Producer を投函するスロット（push_samples が低頻度 take）。
    pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    /// B-118: Measure Thread 生存フラグ（watchdog が crash で false / 復帰で true）。measure_alive() が読む。
    measure_alive: Arc<AtomicBool>,
    /// B-118: watchdog 自身の停止フラグ（Drop でセット）。
    watchdog_shutdown: Arc<AtomicBool>,
    /// B-118: watchdog Thread の JoinHandle（Drop で shutdown→join / 内部で io→measure を join）。
    watchdog_handle: Mutex<Option<JoinHandle<()>>>,
    /// B-118 Phase 3 (③): io_thread 連続失敗時の固定文言（RecordError::ui_message / G-115-29）。
    /// enable_*_writes で io と Arc 共有し、`record_error_message()` getter（JUCE status label）が読む。
    record_error_message: Arc<RwLock<Option<String>>>,
    /// state chunk 往復する識別子（B-058 3c / 方式A）。`set_identity` で復元値を入れ、
    /// 未設定なら `enable_pre_writes` が生成する。`get_identity` で JUCE が読み戻す。
    identity: Mutex<IdentityState>,
    /// This engine's resolved project shelf. POST IO reads this Arc every tick; unlike the legacy
    /// role-wide cell, it is not shared across saved DAW documents with distinct session UUIDs.
    project_hash_cell: Arc<RwLock<String>>,
    /// This engine's resolved DAW session id, paired with `project_hash_cell`.
    daw_session_id_cell: Arc<RwLock<String>>,
    /// POST の対 PRE 名（B-061 3d-b）。`set_pair_target` で設定（identity.name 結合を解く）。
    /// `enable_post_writes` 時に空なら identity.name で seed し、io_thread と Arc 共有する
    /// （run_tick の select / keep() の write_pending target 解決に使う・live 反映）。
    /// Human-readable selection + exact PRE instance state. Name changes clear
    /// every exact-instance field as one transition.
    pair_binding: Arc<PairBinding>,
    /// Monotonic ownership ordering published with the current POST pair claim.
    pair_claimed_at: Arc<RwLock<f64>>,
    /// この engine の plugin_data 書込 role（B-067 / F3）。`enable_pre_writes`→Pre /
    /// `enable_post_writes`→Post で 1 度だけ確定する（io_thread slot と同じ first-wins）。
    /// `add_annotation` はこの role に書く。未 enable（None）時は add_annotation を no-op にする。
    write_role: Mutex<Option<PluginDataRole>>,
    /// ring 満杯で push できなかった累積回数（§8 RT-safety 検証 + B-075 live 露出）。
    /// B-076: io_thread と共有し per-Record dropped_samples を .kirin に焼き込むため Arc 化。
    push_overflow: Arc<AtomicU64>,
    /// B-125: prealloc-max 超の病的 block で測定 ring に渡せず drop した interleaved sample の
    /// 累積数（JUCE 殻が `kirin_hypha_note_oversized_drop` 経由で Audio Thread から積む）。
    /// `push_overflow` とは別カウンタ（混ぜない＝metric truthfulness）。io_thread と共有し
    /// run_record_tick が per-Record 差分を取り、push_overflow 差分と合算して integrity に反映する。
    oversized_drop: Arc<AtomicU64>,
    /// PRE の自名（B-054 / `set_pair_target` と完全対称）。`enable_pre_writes` 時に空なら
    /// identity.name で seed し io_thread_pre と Arc 共有する（pre.json の name に live 反映）。
    /// `set_pre_name` で enable 後でも上書き可。空のときは pre.json の name にそのまま空が書かれる
    /// （io_thread_pre は値を加工しない）。空名の instance_id 先頭8字 fallback は表示専用で、PRE
    /// editor の name 欄と POST 側 format_pair_label のレンダリングが担う。POST engine では未使用。
    pre_name: Arc<RwLock<String>>,
    /// PRE が POST の record_signal を discover→ack 済みか（B-054 LED poller）。`enable_pre_writes`
    /// で io_thread_pre と Arc 共有する。PRE の Keeping バナー（false→true エッジ）と RecordActive
    /// LED の判定に使う。POST engine では io_thread に渡らないため常に false（egui POST と一致）。
    record_acknowledged: Arc<AtomicBool>,
    /// POST に pair 可能な PRE preset（record_signal）が居るか（B-054 LED poller）。
    /// `enable_post_writes` で io_thread_post と Arc 共有する。PresetAvailable LED の判定に使う。
    /// PRE engine では io_thread に渡らないため常に false（egui PRE と一致）。
    preset_available: Arc<AtomicBool>,
}

// SAFETY: `ring_producer`(UnsafeCell<rtrb::Producer>) は push_samples からのみ触れ、
// その push_samples は「Audio Thread 単独」という FFI 契約で単一スレッドアクセスに限定される。
// 他の全フィールドは Arc<Mutex>/Arc<Atomic>/AtomicU64/AtomicU8 で Sync。よって
// `&KirinHyphaEngine` を Audio/UI 2 スレッドで共有しても（契約を守る限り）健全。
unsafe impl Sync for KirinHyphaEngine {}
// SAFETY: 内部状態はスレッド間移動可能（Producer/Arc は Send）。
unsafe impl Send for KirinHyphaEngine {}

// ── B-106/B-301: FFI dylib 内の role fallback と saved-document identity group ──
//
// 空/legacy の `daw_session_uuid` は「明示 document identity なし」として runtime には空のまま
// 流し、host_process_id の legacy bridge に委ねる。一方、保存済み DAW document で非空
// `daw_session_uuid` がある場合は、その UUID を key に role 内 group を分ける。Studio One の
// ように同一 host process で複数 Song/Project を開ける DAW で、後発 document が先発 document
// の棚へ吸われるのを防ぐため。
//
// `spawn_io_thread_post` は `Arc<RwLock<String>>` を受け、io_thread が毎 tick
// `read_project_hash_arc` / `read_daw_session_id_arc` で deref する。B-301 では各 engine が
// 解決済み identity cell を持ち、空/legacy は project のみ role fallback・daw は空、
// 保存済み document は daw-session group と同値を live-read する。
//
// **PRE / POST で別セル**（role-scoped）にする理由: 本番では PRE と POST は別 cdylib
// （KirinHyphaPRE.dylib / KirinHyphaPOST.dylib / juce_shell CMakeLists.txt）として linkage され、
// 各々が staticlib の statics を 1 部ずつ持つ = dylib 境界 ⇔ role 境界。よって PRE 群は PRE 値に、
// POST 群は POST 値に独立収束し、PRE↔POST は別値のまま filesystem discovery（B-021/B-022 /
// PRE が POST の signal の project_hash を adopt）で橋渡しされる。role でセルを分けることで
// 単一テストバイナリ（parity.rs に PRE+POST 同居）でもこの本番境界を忠実に再現する。

fn shared_pre_project_hash_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn shared_pre_daw_session_id_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn shared_post_project_hash_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn shared_post_daw_session_id_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

type IdentityCellPair = (Arc<RwLock<String>>, Arc<RwLock<String>>);
type IdentityGroups = Mutex<HashMap<String, IdentityCellPair>>;

fn shared_pre_identity_groups() -> &'static IdentityGroups {
    static GROUPS: OnceLock<IdentityGroups> = OnceLock::new();
    GROUPS.get_or_init(|| Mutex::new(HashMap::new()))
}

fn shared_post_identity_groups() -> &'static IdentityGroups {
    static GROUPS: OnceLock<IdentityGroups> = OnceLock::new();
    GROUPS.get_or_init(|| Mutex::new(HashMap::new()))
}

/// 共有セルを **first-wins** で解決する（egui の lazy-once seeding と同位相）。
///
/// - 共有セルが空: `candidate`（chunk 復元値）が非空ならそれで seed、空なら新規生成して seed。
/// - 共有セルが既に非空: その値を採用（**上書きしない** = 毎回生成・上書きの全廃）。
///
/// 戻り値 = 採用された共有値。これが broadcast の write 棚パスであり、`spawn_io_thread_*` に
/// 渡す共有 Arc が live-read する scan 棚パスでもある（両者は同一実体なので恒等的に一致）。
fn resolve_shared_id(cell: &Arc<RwLock<String>>, candidate: &str) -> String {
    match cell.write() {
        Ok(mut g) => {
            if g.is_empty() {
                *g = if candidate.is_empty() {
                    Uuid::new_v4().to_string()
                } else {
                    candidate.to_string()
                };
            }
            g.clone()
        }
        // poison fallback（R-28 機能的沈黙）: 収束は諦め candidate/生成値で前進する。
        Err(_) => {
            if candidate.is_empty() {
                Uuid::new_v4().to_string()
            } else {
                candidate.to_string()
            }
        }
    }
}

/// 共有セルの現在値を読む（panic-safe / poison は空文字 fallback）。
fn read_shared_id(cell: &Arc<RwLock<String>>) -> String {
    cell.read().map(|g| g.clone()).unwrap_or_default()
}

fn resolve_role_identity(
    fallback_project_cell: &Arc<RwLock<String>>,
    _fallback_daw_cell: &Arc<RwLock<String>>,
    grouped_cells: &IdentityGroups,
    project_candidate: &str,
    daw_candidate: &str,
) -> (String, String) {
    if daw_candidate.is_empty() {
        return (
            resolve_shared_id(fallback_project_cell, project_candidate),
            String::new(),
        );
    }

    let (project_cell, daw_cell) = {
        let mut groups = grouped_cells.lock().unwrap_or_else(|e| e.into_inner());
        groups
            .entry(daw_candidate.to_string())
            .or_insert_with(|| {
                (
                    Arc::new(RwLock::new(String::new())),
                    Arc::new(RwLock::new(String::new())),
                )
            })
            .clone()
    };

    (
        resolve_shared_id(&project_cell, project_candidate),
        resolve_shared_id(&daw_cell, daw_candidate),
    )
}

fn store_resolved_identity_cells(
    project_cell: &Arc<RwLock<String>>,
    daw_cell: &Arc<RwLock<String>>,
    project_hash: &str,
    daw_session_id: &str,
) {
    if let Ok(mut g) = project_cell.write() {
        *g = project_hash.to_string();
    }
    if let Ok(mut g) = daw_cell.write() {
        *g = daw_session_id.to_string();
    }
}

/// role-scoped 共有セル 4 つ（PRE/POST × project_hash/daw_session_id）の内側 `String` を空にする。
///
/// セル handle（`Arc<RwLock<String>>`）は不変のまま中身だけ空にするため、`spawn_io_thread_*` に
/// 渡した Arc clone の live-read 契約を壊さない。B-110 の refcount 0 detach（io_thread join 後）と、
/// テスト隔離（`__reset_shared_ids_for_tests`）の両方から使う。
fn clear_role_scoped_cells() {
    for cell in [
        shared_pre_project_hash_cell(),
        shared_pre_daw_session_id_cell(),
        shared_post_project_hash_cell(),
        shared_post_daw_session_id_cell(),
    ] {
        if let Ok(mut g) = cell.write() {
            g.clear();
        }
    }
    if let Ok(mut groups) = shared_pre_identity_groups().lock() {
        groups.clear();
    }
    if let Ok(mut groups) = shared_post_identity_groups().lock() {
        groups.clear();
    }
}

/// テスト専用: role-scoped 共有セルを全クリアして first-wins 状態を初期化する。
///
/// 本番は単一 DAW プロセス = 単一セッションで「最初の 1 回だけ seed」が正しいが、統合テスト
/// （`tests/parity.rs`）は 1 バイナリで多数のシナリオを連続実行し、各々が独自の project_uuid を
/// 期待する。旧実装は enable 毎に `set_project_uuid` で**上書き**していたため各テストが隔離されて
/// いたが、B-106 は first-wins（上書き廃止）に変えたため、テストは開始時に本関数で共有セルを
/// reset して隔離する（kirin_measure cell は enable 内の `set_*` が毎回上書きするので別途 reset 不要）。
#[doc(hidden)]
pub fn __reset_shared_ids_for_tests() {
    clear_role_scoped_cells();
}

/// B-102: keep の解決本体（`keep()` と broadcast 受信 closure が共有する単一実装）。
/// POST単独 Record を作らないため、ここでは RecordStateMachine を Record にしない。
/// double-keep guard → select_target_pre → reservation → write_pending の順で arming し、
/// PRE ACK 後に POST IO Thread が Record へ入る。Keep は dropped WAV metadata を読まず、
/// Drop 後の [`KirinHyphaEngine::set_expected_wav_metadata`] が今回世代だけを結び付ける。
#[allow(clippy::too_many_arguments)]
fn resolve_and_enter_keep(
    license: License,
    record_sm: &RecordStateMachine,
    pair_target: &RwLock<String>,
    paired_pre_target: &Mutex<Option<String>>,
    project_hash: &str,
    post_iid: &str,
    daw: &str,
    // B-108: ラッチ済みならラッチ先を直接 Arm target に使う（同名2台目でも結合不変）。未ラッチ時のみ
    // select_target_pre_for_arm にフォールバックする（resolve_arm_target 内で分岐）。
    latched: &Mutex<Option<LatchedPre>>,
    // B-127: cap 到達拒否を UI に通知する永続ステータス（両殻が B-118 で表示）。cap 到達時に
    // "Maximum 12 pairs reached" を書く（R-28: silent drop 禁止）。正常 enter で None に消す。
    record_error_message: &RwLock<Option<String>>,
    started_at_position_samples: Option<i64>,
) -> bool {
    // B-071 double-keep guard: 既に Record 中なら no-op（既存 linkage 温存）。
    if record_sm.is_recording() {
        return false;
    }
    if !matches!(license, License::Os) {
        return false;
    }
    let kirin_root = PlatformPaths::current_kirin_tmp_root();
    let pair = pair_target.read().map(|g| g.clone()).unwrap_or_default();
    // B-108/B-231: ラッチ済み（pair 名一致）はラッチ先を直接使用、未ラッチは B-104 Arm
    // ゲート（非Bypassed + fresh + 一意 / Active 要求なし）。v1.0.0 の「アーム→再生」を維持する。
    let Some(sel) = resolve_arm_target_for_post_project_in_session(
        &kirin_root,
        &pair,
        project_hash,
        daw,
        latched,
    ) else {
        return false; // 未ラッチ時の厳格選定 None: 空名/不在/曖昧/Bypassed/古t（Inactive は許容）。
    };
    let target = sel.instance_id.clone();
    let base = match StoragePaths::default_platform() {
        Ok(p) => p.plugin_data_dir(),
        Err(_) => return false,
    };
    let _ = reservation::sweep_stale_reservations(&base);
    // B-127 (G-115-365): per-pairing O_EXCL reservation で cross-process atomic に枠を確保する。
    // pairing key = (target=PRE iid, post_iid=POST iid)。cap の真実源は枠ファイルの**物理存在のみ**
    // （count_distinct_pairings = reservation::count_frames）。reservation を先に atomic-create
    // してから枠数を数えることで、active marker 出現前の TOCTOU 窓を cross-process で閉じる。cap は
    // 12 pairs。keep() / keep_all() / broadcast 受信 closure は全て本関数を通る（JUCE 殻 parity）。
    // reserve は 1 回だけ呼ぶ。Created = 本呼び出しが枠を作った（reject 時に解放する責務）。
    let reservation_created =
        match reservation::reserve_pairing(&base, project_hash, &target, post_iid) {
            Ok(reservation::ReserveOutcome::Created) => true,
            Ok(reservation::ReserveOutcome::AlreadyReserved) => false,
            // G-115-365 (3): 枠が取れない（write_all 失敗等の Err / 不完全枠は内部で unlink 済）= reject。
            // 枠なしで keep に入らない。
            Err(_) => {
                if let Ok(mut g) = record_error_message.write() {
                    *g = Some("Maximum 12 pairs reached".to_string());
                }
                return false;
            }
        };
    // count は自 reservation を含む枠数。自 reservation で 13 枠目になれば（`> MAX`）reject。既存 pairing
    // （自 reservation が AlreadyReserved）なら枠数は不変なので 12 枠目まで通る（`> MAX` であって `>= MAX`
    // ではない）。
    if count_distinct_pairings(&base, project_hash) > MAX_ACTIVE_PER_PROJECT {
        if reservation_created {
            reservation::release_pairing(&base, project_hash, &target, post_iid);
        }
        // R-28: 13 ペア目を hard reject し silent drop しない（既存文言流用）。
        if let Ok(mut g) = record_error_message.write() {
            *g = Some("Maximum 12 pairs reached".to_string());
        }
        return false;
    }
    // target_pre_instance_id = 選定 PRE。PRE が自宛て signal を発見し ack する。
    if write_pending_claiming_expected_and_clock(
        &base,
        project_hash,
        post_iid,
        target.clone(),
        daw.to_string(),
        started_at_position_samples,
    )
    .is_ok()
    {
        // B-127: 正常 enter で stale な cap/io-fail 通知を消す（新しい健全な Record が開始した）。
        if let Ok(mut g) = record_error_message.write() {
            *g = None;
        }
        if let Ok(mut g) = paired_pre_target.lock() {
            *g = Some(target.clone());
        }
        // B-140: 無音/停止中に Keep した場合、Record 開始時点では Watch 表示側のラッチが未成立な
        // ことがある。Record 中の Δ 表示は「ラッチ凍結」なので再探索しないため、keep が実際に
        // 選んだ PRE をここで display/Arm 共有ラッチへ確定させる。これにより無音でアーム→再生後も
        // 同じ PRE から Δ が出る（同名 PRE の後発出現では再選定しない）。
        if let Ok(mut g) = latched.lock() {
            *g = Some(LatchedPre {
                name: pair.clone(),
                instance_id: target,
                project_dir: sel.project_dir,
                pre_json: sel.pre_json,
                daw_session_id: sel.daw_session_id,
                host_process_id: sel.host_process_id,
            });
        }
        true
    } else {
        // write_pending 失敗時も予約枠を戻す（自分が作った場合のみ）。
        if reservation_created {
            reservation::release_pairing(&base, project_hash, &target, post_iid);
        }
        if let Ok(mut g) = paired_pre_target.lock() {
            *g = None;
        }
        false
    }
}

/// B-102: stop の解決本体（`stop()` と broadcast 受信 closure が共有）。exit_record + linkage
/// クリアは常に行い、`mark_released` は identity 非空のときだけ（元 `stop()` と同一）。
fn resolve_and_exit_stop(
    record_sm: &RecordStateMachine,
    paired_pre_target: &Mutex<Option<String>>,
    project_hash: &str,
    post_iid: &str,
    release_reason: Option<ReleaseReason>,
) {
    // B-127: linkage クリア前に対 PRE iid を捕捉し、本 pairing の O_EXCL reservation 枠を解放する
    // （両 marker が Closed/stale になる前でも stop で明示解放。孤児は sweep が age-based で回収）。
    let released_pre = paired_pre_target.lock().ok().and_then(|g| g.clone());
    if let Ok(mut g) = paired_pre_target.lock() {
        *g = None; // linkage クリア（次 Keep まで）。
    }
    // 2026-07-10 構造修正（ACK re-entry race）: shared signal を Released にしてから
    // record_sm.exit_record() する。逆順だと、record_sm が既に Watch なのに on-disk signal は
    // まだ Acknowledged のままという間隙が生まれ、その間に ACK poller（io_thread_pre.rs /
    // io_thread_post.rs）が stale な Acknowledged を読んで同じ session_id へ再入場して
    // しまう。record_sm 側の `closed_session_id` ガード（record.rs）が構造的な本丸だが、
    // この reorder はその隙間自体を縮める defense-in-depth。exit_record は
    // StoragePaths 解決の成否・早期 return 経路に関わらず必ず1回だけ呼ぶ（元の
    // 無条件呼び出しと同じ保証）。
    if project_hash.is_empty() || post_iid.is_empty() {
        record_sm.exit_record(); // 未 enable → released marker は書けないが exit は必須。
        return;
    }
    if let Ok(p) = StoragePaths::default_platform() {
        let base = p.plugin_data_dir();
        if let Some(pre) = released_pre.as_deref() {
            reservation::release_pairing(&base, project_hash, pre, post_iid);
        }
        let _ = match release_reason {
            Some(reason) => mark_released_with_reason(&base, project_hash, post_iid, reason),
            None => mark_released(&base, project_hash, post_iid),
        };
    }
    record_sm.exit_record();
}

/// 内部 `SignalState` を C ABI コード（0=Inactive 1=Active 2=Bypassed）へ写像する。
/// `set_signal_state` の逆。純粋関数（Measure Thread 非依存）なので決定的にテストできる（B-113）。
#[inline]
fn signal_state_to_abi(state: SignalState) -> u8 {
    match state {
        SignalState::Inactive => 0,
        SignalState::Active => 1,
        SignalState::Bypassed => 2,
    }
}

impl KirinHyphaEngine {
    /// ランタイムを生成し Measure Thread を起動する。
    ///
    /// `sample_rate` ≠ 48000 のときの 48k 変換は Measure Thread 内 `ResamplerTo48k` が
    /// 既存どおり担う（新規変換コードは書かない / measure_thread.rs:82-101）。
    /// `num_channels` は 1=mono / 2=stereo を受ける。mono は1chとして計測し、
    /// dual-mono 化による loudness +3.01 dB バイアスを入れない。
    pub fn new(sample_rate: u32, num_channels: u32) -> Self {
        let num_channels = supported_channel_count(num_channels);
        let capacity = (sample_rate as usize) * RING_BUFFER_SECONDS * num_channels;
        let (producer, consumer) = rtrb::RingBuffer::new(capacity);

        let measure_result = Arc::new(Mutex::new(MeasureResult::default()));
        let delta_result = Arc::new(Mutex::new(DeltaResult::default()));
        let session_summary: Arc<Mutex<Option<SessionSummary>>> = Arc::new(Mutex::new(None));
        let record_trace_queue = new_record_trace_queue();
        let record_take_tracker = new_record_take_tracker();
        let record_mark_queue = new_record_mark_queue();
        let signal_state = Arc::new(AtomicU8::new(SignalState::Inactive as u8));
        let shutdown = Arc::new(AtomicBool::new(false));
        let heartbeat = Arc::new(AtomicU32::new(0));
        let watch_playback_pass_id = Arc::new(AtomicU64::new(1));
        let watch_playback_pass_cutover_samples = Arc::new(AtomicU64::new(0));
        let watch_ring_cursor_epoch = Arc::new(AtomicU64::new(0));
        let watch_ring_cursor_pass_id = Arc::new(AtomicU64::new(1));
        let watch_ring_cursor_samples = Arc::new(AtomicU64::new(0));
        let watch_ring_replacing = Arc::new(AtomicBool::new(false));
        // B-118: 単一鮮度評価器。heartbeat を内部観測し is_live()（G-115-245: 3s window）を返す。
        // Measure Thread / editor pair lock / FFI getter / watchdog が同一評価器を読む。
        let liveness = Arc::new(LivenessEvaluator::new(
            Arc::clone(&heartbeat),
            live_window(),
        ));
        // 実 RecordStateMachine（既定 Watch）。FFI が enter/exit で flip する。
        let record_sm = Arc::new(RecordStateMachine::new());

        // B-118: watchdog（Lazy）と共有する slot / フラグ群。
        let io_thread: Arc<Mutex<Option<IoThreadHandle>>> = Arc::new(Mutex::new(None));
        let io_restart_slot: Arc<Mutex<Option<RestartIoFn>>> = Arc::new(Mutex::new(None));
        let pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>> = Arc::new(Mutex::new(None));
        let measure_alive = Arc::new(AtomicBool::new(true));
        let watchdog_shutdown = Arc::new(AtomicBool::new(false));

        let measure_handle = spawn_measure_thread(
            consumer,
            sample_rate,
            num_channels,
            Arc::clone(&measure_result),
            Arc::clone(&watch_playback_pass_id),
            Arc::clone(&watch_playback_pass_cutover_samples),
            Arc::clone(&watch_ring_cursor_epoch),
            Arc::clone(&watch_ring_cursor_pass_id),
            Arc::clone(&watch_ring_cursor_samples),
            Arc::clone(&signal_state),
            Arc::clone(&shutdown),
            Arc::clone(&liveness),
            Arc::clone(&record_sm),
            Arc::clone(&session_summary),
            Arc::clone(&record_trace_queue),
            Arc::clone(&record_take_tracker),
        );

        // B-118: T-8 watchdog 再採用（B-056 opt-out 撤回）。Measure Thread crash を再起動し、io は
        // Lazy（enable で後発 spawn → 共有 slot）で監視・再起動する。FFI は free 前に全 thread を
        // join するため join_on_shutdown=true（io→measure 順 / 共有 Arc UAF 回避）。
        let watchdog_handle = spawn_watchdog(WatchdogParams {
            sample_rate,
            n_channels: num_channels,
            ring_capacity: capacity,
            measure_result: Arc::clone(&measure_result),
            watch_playback_pass_id: Arc::clone(&watch_playback_pass_id),
            watch_playback_pass_cutover_samples: Arc::clone(&watch_playback_pass_cutover_samples),
            watch_ring_cursor_epoch: Arc::clone(&watch_ring_cursor_epoch),
            watch_ring_cursor_pass_id: Arc::clone(&watch_ring_cursor_pass_id),
            watch_ring_cursor_samples: Arc::clone(&watch_ring_cursor_samples),
            watch_ring_replacing: Arc::clone(&watch_ring_replacing),
            signal_state: Arc::clone(&signal_state),
            evaluator: Arc::clone(&liveness),
            measure_shutdown: Arc::clone(&shutdown),
            measure_alive: Arc::clone(&measure_alive),
            pending_producer: Arc::clone(&pending_producer),
            measure_handle,
            io: WatchdogIo::Lazy {
                io_slot: Arc::clone(&io_thread),
                io_restart_slot: Arc::clone(&io_restart_slot),
            },
            watchdog_shutdown: Arc::clone(&watchdog_shutdown),
            join_on_shutdown: true,
            record_sm: Arc::clone(&record_sm),
            session_summary: Arc::clone(&session_summary),
            record_trace_queue: Arc::clone(&record_trace_queue),
            record_take_tracker: Arc::clone(&record_take_tracker),
        });

        // B-110: live インスタンス refcount +1（破棄は Drop で −1）。enable ではなく create に置く
        // （enable は冪等 early-return のため）。
        identity_instance_attach();

        Self {
            ring_producer: UnsafeCell::new(producer),
            measure_result,
            delta_result,
            session_summary,
            record_trace_queue,
            record_take_tracker,
            record_mark_queue,
            signal_state,
            shutdown,
            heartbeat,
            watch_playback_pass_id,
            watch_playback_pass_cutover_samples,
            watch_max: Mutex::new(WatchMaxTracker::default()),
            transport_previous_playing: AtomicBool::new(false),
            transport_previous_position_valid: AtomicBool::new(false),
            transport_previous_position_samples: AtomicI64::new(0),
            transport_previous_num_frames: AtomicU64::new(0),
            watch_ring_cursor_epoch,
            watch_ring_cursor_pass_id,
            watch_ring_cursor_samples,
            watch_ring_replacing,
            liveness,
            record_sm,
            latest_position_valid: Arc::new(AtomicBool::new(false)),
            latest_position_samples: Arc::new(AtomicI64::new(i64::MIN)),
            // 既定 Unknown（set_license(Os) されるまで Record 不可・安全側）。
            license: LiveLicense::new(License::Unknown),
            sample_rate,
            num_channels,
            io_thread,
            io_restart_slot,
            pending_producer,
            measure_alive,
            watchdog_shutdown,
            watchdog_handle: Mutex::new(Some(watchdog_handle)),
            record_error_message: Arc::new(RwLock::new(None)),
            identity: Mutex::new(IdentityState::default()),
            project_hash_cell: Arc::new(RwLock::new(String::new())),
            daw_session_id_cell: Arc::new(RwLock::new(String::new())),
            pair_binding: Arc::new(PairBinding::new()),
            pair_claimed_at: Arc::new(RwLock::new(0.0)),
            write_role: Mutex::new(None),
            push_overflow: Arc::new(AtomicU64::new(0)),
            oversized_drop: Arc::new(AtomicU64::new(0)), // B-125: JUCE oversized block drop 専用
            // B-054: 既定空 / 既定 false。enable_*_writes が io_thread と Arc 共有する。
            pre_name: Arc::new(RwLock::new(String::new())),
            record_acknowledged: Arc::new(AtomicBool::new(false)),
            preset_available: Arc::new(AtomicBool::new(false)),
            // B-108: 未ラッチで起動。enable_post_writes で io_thread_post と Arc 共有する。
        }
    }

    /// 信号状態を設定する。引数は **C ABI コード**（0=Inactive 1=Active 2=Bypassed）。
    /// 内部 `SignalState` enum（Active=0/Bypassed=1/Inactive=2）へ翻訳する。
    pub fn set_signal_state(&self, abi_state: u8) {
        let s = match abi_state {
            1 => SignalState::Active,
            2 => SignalState::Bypassed,
            _ => SignalState::Inactive,
        };
        store_signal_state(&self.signal_state, s);
    }

    /// 現在の信号状態を **C ABI コード**（0=Inactive 1=Active 2=Bypassed）で返す。
    /// `set_signal_state` の逆写像（写像本体は純粋関数 `signal_state_to_abi`）。`self.signal_state` は
    /// Measure Thread が heartbeat 停止検出時に `Inactive` へ上書きするため、processBlock 停止後は
    /// stale な Active を返さない（B-113）。
    pub fn signal_state_abi(&self) -> u8 {
        signal_state_to_abi(load_signal_state(&self.signal_state))
    }

    /// ライセンスを設定（C ABI コード: 0=Os 1=Sense 2=Unknown / 未知は Unknown）。
    /// 次回 Keep の開始可否だけに使う。開始済み Keep をライセンス更新で停止しない。
    pub fn set_license(&self, abi: u8) {
        let code = match abi {
            LICENSE_OS => LICENSE_OS,
            LICENSE_SENSE => LICENSE_SENSE,
            _ => LICENSE_UNKNOWN,
        };
        self.license.store(license_from_abi(code));
    }

    /// 現ライセンスを取得。
    fn current_license(&self) -> License {
        self.license.load()
    }

    /// Record へ遷移を試みる。`License::Os` かつ Watch のとき `true`、それ以外 `false`。
    /// license 二重 gate（E-21）: `try_enter_record` が内部で `License::Os` を再判定する
    /// （record.rs:109-123）。`AlreadyRecording` / `LicenseDenied` は `false`。
    ///
    /// **C ABI には公開しない**（P1 finding 2026-07-09）。session_id を持たずに Record へ
    /// 入れてしまい、is_recording()==true だが strict writer
    /// （record_writer.rs: run_record_tick_with_pair_names_require_session）が
    /// session なしで起動を拒む＝TRACE が出ない状態を、B-322 が閉じたはずの経路とは別の
    /// 入口から再び作れてしまうため。Rust 側テストが状態機械を直接検証する用途にのみ残す。
    /// 本番の Record 開始は `keep()` 経由の `try_enter_record_started_at_clock_transaction`
    /// （session 必須）だけを通る。`pub` のままなのは `tests/parity.rs`（別クレート扱いの
    /// integration test）が状態機械を直接検証するために呼ぶため。C ABI 経由でこの crate の
    /// 外（JUCE 側）から呼べる経路は存在しない。
    pub fn enter_record(&self) -> bool {
        self.record_sm
            .try_enter_record_started_at_clock(
                self.current_license(),
                kirin_measure::record_writer::now_epoch_ms(),
                None,
            )
            .is_ok()
    }

    /// Record を終了し Watch へ戻す（無条件・冪等 / record.rs:132）。
    /// finalize は Measure Thread が自律実行・直近値を `session_summary` に保持するため
    /// ここでは呼ばない（B2 / finalize は Measure Thread のみ / engine.rs:161）。
    pub fn exit_record(&self) {
        let is_post = self.write_role.lock().ok().and_then(|g| *g) == Some(PluginDataRole::Post);
        if is_post {
            let (project_hash, post_iid) = match self.identity.lock() {
                Ok(id) => (id.project_hash.clone(), id.instance_id.clone()),
                Err(_) => (String::new(), String::new()),
            };
            let paired_pre_target = self.pair_binding.recording_pre();
            resolve_and_exit_stop(
                &self.record_sm,
                &paired_pre_target,
                &project_hash,
                &post_iid,
                Some(ReleaseReason::ManualStop),
            );
        } else {
            self.record_sm.exit_record();
        }
    }

    /// Record 中かどうか（read-only オブザーバ）。C ABI には公開しない（3a surface 厳守）。
    pub fn is_recording(&self) -> bool {
        self.record_sm.is_recording()
    }

    /// Audio Thread から Record take の実レンダー長を通知する。
    ///
    /// 計測 ring とは独立した sample-count clock で、手動 Keep/Stop の余白を
    /// `bounce_take` に混ぜないための正本。内部は atomic 操作のみ。
    pub fn note_record_block(&self, block: RecordTakeBlock) {
        let mut block = RecordTakeBlock {
            generation: self.record_sm.generation(),
            ..block
        };
        if block.position_valid {
            self.latest_position_samples
                .store(block.position_samples, Ordering::Relaxed);
            self.latest_position_valid.store(true, Ordering::Relaxed);
        } else {
            self.latest_position_valid.store(false, Ordering::Relaxed);
        }

        if block.rendered && block.recording && block.position_valid && block.num_frames > 0 {
            let start_samples = if block.clock_end_samples.is_some() {
                block.clock_start_samples
            } else {
                block.position_samples
            };
            let _ = self
                .record_sm
                .try_latch_record_started_at_position_samples(start_samples);
        }
        if let Some(start_samples) = self.record_sm.record_started_at_position_samples() {
            block.clock_start_samples = start_samples;
        }

        self.record_take_tracker.note_block(block);
    }

    /// Audio Thread が measurement ring へ投入する窓の host sample clock を通知する。
    /// `note_record_block` とは独立させ、Watch pre-roll と Record の両方を同じ clock に載せる。
    pub fn note_capture_window(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        clock_source: CaptureClockSource,
    ) {
        self.note_capture_window_with_presentation(
            position_valid,
            position_samples,
            num_frames,
            clock_source,
            PresentationLatencySamples::default(),
        );
    }

    pub fn note_capture_window_with_presentation(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        clock_source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
    ) {
        self.record_take_tracker
            .note_capture_window_with_presentation(
                position_valid,
                position_samples,
                num_frames,
                clock_source,
                presentation_latency,
            );
    }

    /// Publish one host transport block. Audio Thread only; atomics and the
    /// existing ring-cursor seqlock make this RT-safe.
    pub fn note_transport_block(
        &self,
        playing: bool,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
    ) {
        let previous_playing = self
            .transport_previous_playing
            .swap(playing, Ordering::AcqRel);
        let previous_valid = self
            .transport_previous_position_valid
            .swap(position_valid, Ordering::AcqRel);
        let previous_position = self
            .transport_previous_position_samples
            .swap(position_samples, Ordering::AcqRel);
        let previous_frames = self
            .transport_previous_num_frames
            .swap(num_frames, Ordering::AcqRel);

        let discontinuity = playing
            && previous_playing
            && position_valid
            && previous_valid
            && previous_position.checked_add(previous_frames as i64) != Some(position_samples);
        if playing && (!previous_playing || discontinuity) {
            publish_watch_playback_pass_boundary(
                &self.watch_playback_pass_id,
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                &self.watch_playback_pass_cutover_samples,
            );
        }
    }

    pub fn poll_watch_display(&self, playing: bool) -> Option<(MeasureResult, MeasureResult)> {
        let raw = self.poll_result()?;
        let pass_id = self.watch_playback_pass_id.load(Ordering::Acquire);
        let maximum = self.watch_max.try_lock().ok()?.update(
            &raw,
            playing,
            pass_id,
            self.record_sm.is_recording(),
        );
        Some((raw, maximum))
    }

    /// PRE の plugin_data 書込（Watch pre.json + Record frames/PSB）を有効化する（B-057 3b）。
    ///
    /// `kirin_measure::spawn_io_thread_pre`（io_thread_pre.rs:179）を engine 既存の共有
    /// 状態（record_sm / measure_result / signal_state / session_summary）に繋いで起動する。
    /// io_thread ロジック自体は kirin_measure のまま（呼ぶだけ）。filesystem 書込は全て
    /// その Rust スレッド内に閉じる（FFI は spawn と識別子注入のみ・B2 分離原則）。
    ///
    /// 前提・割り切り（3b）:
    /// - `set_license` の現在値と IO worker は同じ [`LiveLicense`] を共有する。
    ///   enable 後の Kirin OS 認識・降格も engine 再生成なしで反映される。
    /// - `instance_id` は `Uuid::new_v4` 生成（永続は 3c）。`project_uuid` / `daw_session_id`
    ///   は FFI 側で role identity として解決する。空/legacy session は runtime daw を空のまま
    ///   host fallback に委ね、保存済み document の非空 `daw_session_uuid` は B-301 の session group
    ///   で分離する。解決値は `set_project_uuid` で kirin_measure セルへも反映し、path のルートになる。
    /// - 2 度目以降の呼出は no-op（冪等）。
    ///
    /// PRE-POST discovery は POST 不在のとき inert（record_signal が無く ack 対象なし /
    /// io_thread_pre.rs:294-312）。pairing 実働は 3d。
    pub fn enable_pre_writes(&self) {
        let mut slot = match self.io_thread.lock() {
            Ok(g) => g,
            Err(_) => return,
        };
        if slot.is_some() {
            return; // 冪等: 既に PRE/POST io_thread 起動済み。
        }
        // B-067/F3: この engine の書込 role を PRE に確定（add_annotation が使う / 単一 role）。
        if let Ok(mut r) = self.write_role.lock() {
            *r = Some(PluginDataRole::Pre);
        }

        // 識別子: instance_id は set_identity 復元値、未設定はここで生成（3b フォールバック）。
        // project_uuid / daw_session_id は下記 role identity 解決で確定し、
        // identity に書き戻して get_identity が JUCE chunk へ返せるようにする。
        let (iid_str, name_str, project_hash, daw_uuid) = {
            let mut id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return,
            };
            if id.instance_id.is_empty() {
                id.instance_id = Uuid::new_v4().to_string();
            }
            // B-106/B-301/B-302: chunk 復元値（id.*）を candidate に渡し、空/legacy session は
            // runtime daw を空にして host fallback、非空 session は saved-document group で解決する。
            // 解決値を identity に書き戻して chunk 永続。kirin_measure cell への反映（set_*）は現状維持。
            let (resolved_project, resolved_daw) = resolve_role_identity(
                shared_pre_project_hash_cell(),
                shared_pre_daw_session_id_cell(),
                shared_pre_identity_groups(),
                &id.project_uuid,
                &id.daw_session_uuid,
            );
            id.project_uuid = resolved_project.clone();
            id.project_hash = resolved_project.clone();
            id.daw_session_uuid = resolved_daw.clone();
            store_resolved_identity_cells(
                &self.project_hash_cell,
                &self.daw_session_id_cell,
                &resolved_project,
                &resolved_daw,
            );
            set_project_uuid(resolved_project);
            set_daw_session_id(resolved_daw);
            (
                id.instance_id.clone(),
                id.name.clone(),
                id.project_hash.clone(),
                id.daw_session_uuid.clone(),
            )
        };

        let instance_id = Arc::new(RwLock::new(iid_str));
        // io_thread_pre が内部で set/管理する共有フラグ（FFI は false で生成して渡すだけ）。
        let recording = Arc::new(AtomicBool::new(false));
        // B-054: record_acknowledged は engine と共有（PRE Keeping バナー / RecordActive LED が poll）。
        let record_acknowledged = Arc::clone(&self.record_acknowledged);
        // B-054: PRE 名は engine.pre_name と Arc 共有（set_pair_target と完全対称・enable 後 live）。
        // 空のときのみ identity.name で seed（pre-enable の set_pre_name を温存）。空のままなら
        // pre.json には空 name が書かれる（io_thread_pre は加工しない / instance_id 先頭8字は表示専用）。
        if let Ok(mut pn) = self.pre_name.write() {
            if pn.is_empty() {
                *pn = name_str;
            }
        }
        let name = Arc::clone(&self.pre_name);
        // B-118 Phase 3 (③): engine 保持の Arc を共有（io が書き JUCE getter が読む / 世代跨ぎ継続）。
        let record_error_message = Arc::clone(&self.record_error_message);
        // A: enable 時点の license をスナップショット（immutable）。
        let license = self.license.clone();

        // B-118: io spawn を restart-closure に包む（初回 spawn も watchdog 再起動も同一経路）。
        // 継続性（最重要）: 共有状態 Arc（record_error_message / recording / record_acknowledged /
        // name / record_sm / measure_result / signal_state / session_summary / push_overflow）は
        // 同一実体を capture し、再起動後も同じ Arc を指す（closure 内での再生成・新規 Arc 化は禁止）。
        // io_shutdown のみ世代毎に新規生成する。
        let restart: RestartIoFn = {
            let record_sm = Arc::clone(&self.record_sm);
            let measure_result = Arc::clone(&self.measure_result);
            let signal_state = Arc::clone(&self.signal_state);
            let session_summary = Arc::clone(&self.session_summary);
            let record_trace_queue = Arc::clone(&self.record_trace_queue);
            let record_take_tracker = Arc::clone(&self.record_take_tracker);
            let push_overflow = Arc::clone(&self.push_overflow);
            let oversized_drop = Arc::clone(&self.oversized_drop); // B-125
            let sample_rate = self.sample_rate;
            Box::new(move || {
                let io_shutdown = Arc::new(AtomicBool::new(false));
                let handle = spawn_io_thread_pre(
                    Arc::clone(&instance_id),
                    project_hash.clone(),
                    daw_uuid.clone(), // PRE pre.json の document 境界として出力する復元値
                    sample_rate,
                    Arc::clone(&record_sm),
                    Arc::clone(&recording),
                    Arc::clone(&record_acknowledged),
                    license.clone(),
                    Arc::clone(&measure_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&io_shutdown),
                    Arc::clone(&name),
                    Arc::clone(&record_error_message),
                    Arc::clone(&session_summary),
                    Arc::clone(&record_trace_queue),
                    Arc::clone(&record_take_tracker),
                    Arc::clone(&push_overflow), // B-076: per-Record dropped_samples
                    Arc::clone(&oversized_drop), // B-125: per-Record oversized block drop
                );
                IoThreadHandle {
                    shutdown: io_shutdown,
                    handle,
                }
            })
        };

        // 初回 io spawn = closure 実行 → 共有 slot に置き watchdog が監視。restart を closure slot へ。
        *slot = Some(restart());
        if let Ok(mut rs) = self.io_restart_slot.lock() {
            *rs = Some(restart);
        }
    }

    /// POST の plugin_data 書込（post.json の Δ・select_target_pre 経由＝厳格選定）を
    /// 有効化する（B-060 3d-a）。`enable_pre_writes` と対。同一 engine では排他（片方のみ）。
    ///
    /// `kirin_measure::spawn_io_thread_post` を engine 既存の共有状態（record_sm /
    /// measure_result / signal_state / session_summary）+ POST 固有 Arc に繋いで起動する。
    /// io_thread の run_tick が `select_target_pre`（B-059 厳格）で PRE を選び post.json に
    /// Δ を書く。**Keep/ack（write_pending）は配線しない**（trigger closures は no-op = 3d-a）。
    ///
    /// 前提・割り切り（3d-a）:
    /// - `set_license` の後に呼ぶ（io_thread の license スナップショットは PRE と同様 / 但し
    ///   POST run_tick の Δ 表示は license gate なし）。
    /// - `set_identity` 済みなら復元値、未設定は生成（PRE と同経路 / 永続は 3c）。
    /// - **pair_pre_name（対 PRE 名）= identity.name** を使う（同名の PRE と対になる規約）。
    /// - 2 度目以降の呼出は no-op（冪等）。
    pub fn enable_post_writes(&self) {
        let mut slot = match self.io_thread.lock() {
            Ok(g) => g,
            Err(_) => return,
        };
        if slot.is_some() {
            return; // 冪等。
        }
        // B-067/F3: この engine の書込 role を POST に確定（add_annotation が使う / 単一 role）。
        if let Ok(mut r) = self.write_role.lock() {
            *r = Some(PluginDataRole::Post);
        }

        let (iid_str, name_str, project_hash, daw_uuid) = {
            let mut id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return,
            };
            if id.instance_id.is_empty() {
                id.instance_id = Uuid::new_v4().to_string();
            }
            // B-106/B-301/B-302: enable_pre_writes と同一規約。空/legacy session は runtime daw
            // を空にして host fallback、非空 session は saved-document group で解決する。
            let (resolved_project, resolved_daw) = resolve_role_identity(
                shared_post_project_hash_cell(),
                shared_post_daw_session_id_cell(),
                shared_post_identity_groups(),
                &id.project_uuid,
                &id.daw_session_uuid,
            );
            id.project_uuid = resolved_project.clone();
            id.project_hash = resolved_project.clone();
            id.daw_session_uuid = resolved_daw.clone();
            store_resolved_identity_cells(
                &self.project_hash_cell,
                &self.daw_session_id_cell,
                &resolved_project,
                &resolved_daw,
            );
            set_project_uuid(resolved_project);
            set_daw_session_id(resolved_daw);
            (
                id.instance_id.clone(),
                id.name.clone(),
                id.project_hash.clone(),
                id.daw_session_uuid.clone(),
            )
        };

        // B-102: broadcast 受信 closure 用に enable-resolved 値を clone しておく（以降の Arc
        // move より前に確保）。project_hash / instance_id / daw は enable 後不変。
        let cb_project_hash = project_hash.clone();
        let cb_post_iid = iid_str.clone();
        let cb_daw = daw_uuid.clone();

        // POST 固有の共有 Arc（hypha_post params と同型）。
        let instance_id = Arc::new(RwLock::new(iid_str));
        // Saved DAW documents with distinct non-empty daw_session_uuid values get distinct
        // engine cells. Empty/legacy sessions keep runtime daw empty and use host fallback.
        let project_hash_arc = Arc::clone(&self.project_hash_cell);
        // B-054: preset_available は engine と共有（PresetAvailable LED が poll）。
        let preset_available = Arc::clone(&self.preset_available);
        // paired_pre_target は engine と共有（keep() が set → POST Record の linkage に焼く）。
        let paired_pre_target = self.pair_binding.recording_pre();
        let pair_label = Arc::new(Mutex::new(String::new()));
        let daw_session_id = Arc::clone(&self.daw_session_id_cell);
        // pair_pre_name = self.pair_target（set_pair_target 優先 / 空なら identity.name で seed）。
        // io_thread と Arc 共有 → set_pair_target の live 反映 + keep() の select と同一値。
        self.pair_binding.seed_name_if_empty(name_str);
        let pair_pre_name = self.pair_binding.desired_name();
        // B-102: broadcast 受信 → 自身の keep/stop を発火する本物の closure（egui hypha_post と
        // 同一経路 / scope = 新↔新）。closure は Box 所有の engine を借用できないため、keep()/stop()
        // と同一の共有 free 関数 resolve_and_enter_keep / resolve_and_exit_stop を捕捉 Arc + enable
        // 値で呼ぶ。license は LiveLicense を live 読み（keep と同一 gate）。args (pre/post) は
        // 各 POST が自分の pair_target を再選定するため無視する。
        let trigger_pair_resolution: kirin_measure::TriggerPairResolutionFn = {
            let record_sm = Arc::clone(&self.record_sm);
            let pair_target = self.pair_binding.desired_name();
            let paired = self.pair_binding.recording_pre();
            let license = self.license.clone();
            let project_hash = cb_project_hash.clone();
            let post_iid = cb_post_iid.clone();
            let daw = cb_daw.clone();
            let latched = self.pair_binding.latched_pre();
            // B-127: broadcast 受信 keep も engine cap を通す。cap 到達通知の宛先 Arc を capture。
            let record_error_message = Arc::clone(&self.record_error_message);
            Arc::new(move |_pre: &str, _post: &str| {
                let lic = license.load();
                let _ = resolve_and_enter_keep(
                    lic,
                    &record_sm,
                    &pair_target,
                    &paired,
                    &project_hash,
                    &post_iid,
                    &daw,
                    &latched,
                    &record_error_message,
                    None,
                );
            })
        };
        let trigger_stop_resolution: kirin_measure::TriggerStopResolutionFn = {
            let record_sm = Arc::clone(&self.record_sm);
            let paired = self.pair_binding.recording_pre();
            let project_hash = cb_project_hash;
            let post_iid = cb_post_iid;
            Arc::new(move |_pre: &str, _post: &str| {
                resolve_and_exit_stop(
                    &record_sm,
                    &paired,
                    &project_hash,
                    &post_iid,
                    Some(ReleaseReason::AllStop),
                );
            })
        };
        let pair_binding_generation: kirin_measure::PairBindingGenerationFn = {
            let pair_binding = Arc::clone(&self.pair_binding);
            Arc::new(move || pair_binding.generation())
        };
        let release_pair_binding_if_current: kirin_measure::ReleasePairBindingIfCurrentFn = {
            let pair_binding = Arc::clone(&self.pair_binding);
            Arc::new(move |expected_name, expected_generation| {
                pair_binding.release_if_current(expected_name, expected_generation)
            })
        };
        // B-118 Phase 3 (③): engine 保持の Arc を共有（io が書き JUCE getter が読む / 世代跨ぎ継続）。
        let record_error_message = Arc::clone(&self.record_error_message);
        let pair_claimed_at = Arc::clone(&self.pair_claimed_at);
        let pair_release_notice = Arc::new(RwLock::new(None));
        let is_playing = Arc::new(AtomicBool::new(false));
        let live_license = self.license.clone();
        // B-118: io spawn を restart-closure に包む（初回 spawn も watchdog 再起動も同一経路）。
        // 継続性（最重要）: 共有状態 Arc（pair_label / pair_claimed_at / pair_release_notice /
        // record_error_message / paired_pre_target / pair_pre_name / trigger 群 / latched_pre / 各 self.*）
        // は同一実体を capture し再起動後も同じ Arc を指す（closure 内での再生成禁止）。io_shutdown のみ
        // 世代毎に新規生成。
        let restart: RestartIoFn = {
            let record_sm = Arc::clone(&self.record_sm);
            let measure_result = Arc::clone(&self.measure_result);
            let delta_result = Arc::clone(&self.delta_result);
            let signal_state = Arc::clone(&self.signal_state);
            let is_playing = Arc::clone(&is_playing);
            let session_summary = Arc::clone(&self.session_summary);
            let record_trace_queue = Arc::clone(&self.record_trace_queue);
            let record_take_tracker = Arc::clone(&self.record_take_tracker);
            let record_mark_queue = Arc::clone(&self.record_mark_queue);
            let push_overflow = Arc::clone(&self.push_overflow);
            let oversized_drop = Arc::clone(&self.oversized_drop); // B-125
            let latched_pre = self.pair_binding.latched_pre();
            let sample_rate = self.sample_rate;
            Box::new(move || {
                let io_shutdown = Arc::new(AtomicBool::new(false));
                let handle = spawn_io_thread_post(
                    Arc::clone(&instance_id),
                    Arc::clone(&project_hash_arc),
                    sample_rate,
                    Arc::clone(&record_sm),
                    Arc::clone(&measure_result),
                    Arc::clone(&delta_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&is_playing),
                    Arc::clone(&preset_available),
                    live_license.clone(),
                    Arc::clone(&paired_pre_target),
                    Arc::clone(&io_shutdown),
                    Arc::clone(&pair_label),
                    Arc::clone(&daw_session_id),
                    Arc::clone(&pair_pre_name),
                    Arc::clone(&trigger_pair_resolution),
                    Arc::clone(&trigger_stop_resolution),
                    Arc::clone(&pair_binding_generation),
                    Arc::clone(&release_pair_binding_if_current),
                    Arc::clone(&record_error_message),
                    Arc::clone(&pair_claimed_at),
                    Arc::clone(&pair_release_notice),
                    Arc::clone(&session_summary),
                    Arc::clone(&record_trace_queue),
                    Arc::clone(&record_take_tracker),
                    Arc::clone(&record_mark_queue),
                    Arc::clone(&push_overflow), // B-076: per-Record dropped_samples
                    Arc::clone(&oversized_drop), // B-125: per-Record oversized block drop
                    Arc::clone(&latched_pre),   // B-108: display/keep 共有ラッチ
                );
                IoThreadHandle {
                    shutdown: io_shutdown,
                    handle,
                }
            })
        };

        // 初回 io spawn = closure 実行 → 共有 slot に置き watchdog が監視。restart を closure slot へ。
        *slot = Some(restart());
        if let Ok(mut rs) = self.io_restart_slot.lock() {
            *rs = Some(restart);
        }
    }

    /// POST の Δ 結果を取得する（B-060 3d-a / GUI 表示用・read-only / try_lock 非ブロッキング）。
    /// `enable_post_writes` 後、POST io_thread が select_target_pre で選んだ PRE との差分。
    /// PRE engine / 未 enable / ロック競合・未計測時は `DeltaResult::default()`（mode=Active
    /// だが全 Δ None / B-059 で NoPre は last_active クリア）。`post.json` には Δ でなく POST
    /// 生メトリクスが入る（serialize_post_json）。Δ はこの in-memory 経路で公開する。
    pub fn poll_delta(&self) -> Option<DeltaResult> {
        match self.delta_result.try_lock() {
            Ok(g) => Some(g.clone()),
            Err(_) => None,
        }
    }

    fn begin_pair_reselection(&self) -> (String, String) {
        let (project_hash, post_iid) = match self.identity.lock() {
            Ok(id) => (id.project_hash.clone(), id.instance_id.clone()),
            Err(_) => (String::new(), String::new()),
        };
        if !project_hash.is_empty() && !post_iid.is_empty() {
            if let Ok(paths) = StoragePaths::default_platform() {
                let _ = mark_released_with_reason(
                    &paths.plugin_data_dir(),
                    &project_hash,
                    &post_iid,
                    ReleaseReason::ManualStop,
                );
            }
        }
        self.record_sm.exit_record();
        (project_hash, post_iid)
    }

    fn finish_pair_reselection(
        &self,
        transition: PairTargetTransition,
        project_hash: &str,
        post_iid: &str,
        pair_claimed_at: f64,
    ) {
        if !transition.changed {
            return;
        }
        if !project_hash.is_empty() && !post_iid.is_empty() {
            if let (Some(pre), Ok(paths)) = (
                transition.previous_pre_instance_id.as_deref(),
                StoragePaths::default_platform(),
            ) {
                reservation::release_pairing(&paths.plugin_data_dir(), project_hash, pre, post_iid);
            }
        }
        *self
            .pair_claimed_at
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = pair_claimed_at;
        self.preset_available.store(false, Ordering::Release);
        *self
            .delta_result
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = DeltaResult::default();
        if let Ok(mut error) = self.record_error_message.write() {
            *error = None;
        }
    }

    /// 対 PRE 名（pair target）を設定する（B-061 3d-b / identity.name 結合を解く）。
    /// io_thread と Arc 共有のため `enable_post_writes` 後でも live に反映される。
    pub fn set_pair_target(&self, name: String) {
        // B-071: sanitize（ASCII graphic + space / max 16）で PRE 名と同一語彙に正規化する
        // 単一情報源（kirin_measure::sanitize_name）。select_target_pre は sanitized な PRE 名と
        // 照合するため、pair target も同じ正規化を通す。
        let sanitized = sanitize_name(&name);
        let desired_name = self.pair_binding.desired_name();
        let current_name = desired_name
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .clone();
        if current_name == sanitized
            && (!sanitized.is_empty() || self.paired_pre_instance_id().is_none())
        {
            return;
        }

        // Publish Released before leaving Record so an ACK poller cannot re-enter the old session.
        let (project_hash, post_iid) = self.begin_pair_reselection();
        let transition = self.pair_binding.replace_name(sanitized);
        let claimed_at = if self
            .pair_binding
            .desired_name()
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .is_empty()
        {
            0.0
        } else {
            epoch_secs_now()
        };
        self.finish_pair_reselection(transition, &project_hash, &post_iid, claimed_at);
    }

    /// Bind one exact PRE selected from the dropdown. Human name remains the reconnect selector;
    /// the runtime instance latch is authoritative for this session.
    pub fn set_pair_candidate(&self, instance_id: &str) -> bool {
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        let Some((selected, name)) =
            select_live_pre_pair_choice_by_instance_for_post_project_in_session(
                &kirin_root,
                instance_id,
                &project_hash,
                &daw,
            )
        else {
            return false;
        };
        let name = sanitize_name(&name);
        let latch = latch_selected_pre(name.clone(), selected);
        let current_name = self
            .pair_binding
            .desired_name()
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .clone();
        if current_name == name
            && self.paired_pre_instance_id().as_deref() == Some(latch.instance_id.as_str())
        {
            return true;
        }
        let (project_hash, post_iid) = self.begin_pair_reselection();
        let transition = self.pair_binding.replace_exact(name, latch);
        self.finish_pair_reselection(transition, &project_hash, &post_iid, epoch_secs_now());
        true
    }

    pub fn pair_status(&self) -> PairStatus {
        let role = self.write_role.lock().ok().and_then(|role| *role);
        match role {
            Some(PluginDataRole::Post) => {
                let desired = self
                    .pair_binding
                    .desired_name()
                    .read()
                    .unwrap_or_else(|poisoned| poisoned.into_inner())
                    .clone();
                let latched = self.pair_binding.latched_pre();
                pair_status_for_post(&desired, &latched)
            }
            Some(PluginDataRole::Pre) => {
                let iid = self
                    .identity
                    .lock()
                    .map(|identity| identity.instance_id.clone())
                    .unwrap_or_default();
                let name = self
                    .pre_name
                    .read()
                    .unwrap_or_else(|poisoned| poisoned.into_inner())
                    .clone();
                pair_status_for_pre(&PlatformPaths::current_kirin_tmp_root(), &iid, &name)
            }
            None => PairStatus::Unpaired,
        }
    }

    pub fn paired_pre_instance_id(&self) -> Option<String> {
        paired_pre_instance_id(&self.pair_binding.latched_pre())
    }

    /// PRE の自名を設定する（B-054 / `set_pair_target` と完全対称）。
    /// io_thread_pre と Arc 共有のため `enable_pre_writes` 後でも live に反映される
    /// （pre.json の name に書かれ、POST 側 pair target と同一語彙で照合される）。
    /// 空文字はそのまま空 name として書かれる（instance_id 先頭8字 fallback は GUI 表示専用）。
    pub fn set_pre_name(&self, name: String) {
        // pair target と同一情報源（kirin_measure::sanitize_name / ASCII graphic + space / max 16）。
        let sanitized = sanitize_name(&name);
        if let Ok(mut pn) = self.pre_name.write() {
            *pn = sanitized;
        }
    }

    /// Measure Thread が生存しているか（B-054 LED Error 状態 / read-only poller）。
    /// B-118: T-8 watchdog が crash 検出で false / 復帰で true を書く `measure_alive` フラグを読む
    /// （FFI も watchdog を spawn するようになったため B-056 の「scope A 外」を撤回）。JUCE LED は
    /// `kirin_hypha_measure_alive` getter 経由でこれを読み Error 状態に落とす（既配線）。
    pub fn measure_alive(&self) -> bool {
        self.measure_alive.load(Ordering::Relaxed)
    }

    /// テスト専用: Measure Thread を強制終了させ watchdog の再起動経路を駆動する（B-118 test iii/iv）。
    /// `shutdown` をセットすると measure loop が抜けて exit → watchdog が is_finished を検出し
    /// （watchdog_shutdown は false のため）再 spawn して shutdown を false へ戻す。本番経路は使わない。
    #[doc(hidden)]
    pub fn __force_measure_restart_for_test(&self) {
        self.shutdown.store(true, Ordering::Relaxed);
    }

    /// B-118 Phase 3 (②): 現プロジェクトが Record 排他上限（MAX_ACTIVE_PER_PROJECT=12）に達しているか。
    /// これは表示用の advisory getter。Keep の正本判定は `resolve_and_enter_keep` の
    /// reserve→count>MAX で行う（同一 pairing の再Keepを 12 枠ちょうどで誤拒否しないため）。
    /// 未 enable（project_hash 空）/ StoragePaths 不能は false（保守側＝ブロックしない）。
    pub fn record_exclusion_conflict(&self) -> bool {
        let project_hash = match self.identity.lock() {
            Ok(id) => id.project_hash.clone(),
            Err(_) => return false,
        };
        if project_hash.is_empty() {
            return false;
        }
        let base = match StoragePaths::default_platform() {
            Ok(p) => p.plugin_data_dir(),
            Err(_) => return false,
        };
        let _ = reservation::sweep_stale_reservations(&base);
        matches!(
            check_record_exclusion(&base, &project_hash),
            ExclusionResult::Conflict { .. }
        )
    }

    /// B-118 Phase 3 (③): io_thread 連続失敗時の固定文言（RecordError::ui_message / G-115-29）。
    /// None=通常（R-26 沈黙）。JUCE 永続 status label が Some の間表示する。
    pub fn record_error_message(&self) -> Option<String> {
        self.record_error_message
            .read()
            .ok()
            .and_then(|g| g.clone())
    }

    /// B-118: heartbeat 鮮度（processBlock が呼ばれている事実 / read-only poller・非 RT）。
    /// 単一評価器 `is_live()`（G-115-245: 最終 heartbeat 変化から 3s window 以内）を返す。
    /// signal_state とは別軸（B-107 で無音再生中も state=Inactive になるため state を代用しない）。
    /// editor が POST pair 変更ロックの live 述語として読む（`playing かつ live` でロック）。
    pub fn heartbeat_live(&self) -> bool {
        self.liveness.is_live()
    }

    /// PRE が POST の record_signal を ack 済みか（B-054 LED poller）。
    /// enable_pre_writes 前 / POST engine は常に false。
    pub fn record_acknowledged(&self) -> bool {
        self.record_acknowledged.load(Ordering::Relaxed)
    }

    /// POST に pair 可能な PRE preset が居るか（B-054 LED poller）。
    /// enable_post_writes 前 / PRE engine は常に false。
    pub fn preset_available(&self) -> bool {
        self.preset_available.load(Ordering::Relaxed)
    }

    /// テスト専用: paired_pre_target（POST Record linkage）の現在値スナップショット。
    /// B-071 double-keep 検証で「2 回目 keep が linkage を None 化しない」ことを assert する。
    #[doc(hidden)]
    pub fn paired_pre_target_snapshot(&self) -> Option<String> {
        self.pair_binding
            .recording_pre()
            .lock()
            .ok()
            .and_then(|g| g.clone())
    }

    /// Kirin OS/JUCE runtime が Drop 後の WAV 正本 metadata を渡す入口。
    ///
    /// `current.json` を atomic 書込みした同じ呼出しで、既に閉じた今回 Record を
    /// WAV の `0..duration_samples` へ再照合する。Keep 前の前回世代は Record に結ばない。
    /// writer がまだ close 中なら IO Thread の定常 poll が同じ再照合を後続する。
    pub fn set_expected_wav_metadata(&self, input: ExpectedWavMetadataInput) -> bool {
        let project_hash = match self.identity.lock() {
            Ok(id) => id.project_hash.clone(),
            Err(_) => return false,
        };
        if project_hash.is_empty() {
            return false;
        }
        let base = match StoragePaths::default_platform() {
            Ok(p) => p.plugin_data_dir(),
            Err(_) => return false,
        };
        let metadata = ExpectedWavMetadata {
            expected_duration_samples: input.expected_duration_samples,
            expected_sample_rate: input.expected_sample_rate,
            wav_time_reference_samples: None,
            wav_path: input.wav_path,
            bounce_id: input.bounce_id,
            created_at_ms: kirin_measure::record_writer::now_epoch_ms(),
            wav_file_size: Some(input.wav_file_size),
            wav_mtime_ms: input.wav_mtime_ms,
            wav_hash: Some(input.wav_hash),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        };
        let Some(session_id) = self.record_sm.last_closed_session_id() else {
            if let Ok(mut g) = self.record_error_message.write() {
                *g = Some("WAV metadata has no closed Keep session".to_string());
            }
            return false;
        };
        match write_expected_metadata(&base, &project_hash, &metadata) {
            Ok(()) => {
                if !matches!(
                    mark_expected_metadata_consumed(
                        &base,
                        &project_hash,
                        Some(&metadata.bounce_id),
                        &session_id,
                    ),
                    Ok(true)
                ) {
                    if let Ok(mut g) = self.record_error_message.write() {
                        *g = Some("WAV metadata could not bind to closed Keep session".to_string());
                    }
                    return false;
                }
                kirin_measure::plugin_data::reconcile_late_expected_wav_project(
                    &base,
                    &project_hash,
                );
                true
            }
            Err(e) => {
                if let Ok(mut g) = self.record_error_message.write() {
                    *g = Some(format!("WAV metadata invalid: {e}"));
                }
                false
            }
        }
    }

    /// POST「Keep」: 厳格選定（select_target_pre）で対 PRE を一意決定し record_signal(pending)
    /// を書く（B-061 3d-b）。PRE 側 io_thread が autonomous に discover→ack する。
    /// `License::Os` かつ一意 PRE のとき `true`。選定 None（空名/不在/曖昧/Bypassed/古t）/
    /// 非 Os / AlreadyRecording は `false`（write_pending しない）。
    pub fn keep(&self) -> bool {
        let (project_hash, post_iid, daw) = {
            let id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return false,
            };
            if id.project_hash.is_empty() || id.instance_id.is_empty() {
                return false; // 未 enable_post_writes
            }
            (
                id.project_hash.clone(),
                id.instance_id.clone(),
                id.daw_session_uuid.clone(),
            )
        };
        // B-102: 解決本体は共有 free 関数 resolve_and_enter_keep（broadcast 受信 closure と同一
        // 経路）。選定→linkage→try_enter_record→write_pending の順序は従来 keep() と不変。
        let pair_target = self.pair_binding.desired_name();
        let paired_pre_target = self.pair_binding.recording_pre();
        let latched_pre = self.pair_binding.latched_pre();
        resolve_and_enter_keep(
            self.current_license(),
            &self.record_sm,
            &pair_target,
            &paired_pre_target,
            &project_hash,
            &post_iid,
            &daw,
            &latched_pre, // B-108: ラッチ済みならラッチ先を直接 target に使う
            &self.record_error_message, // B-127: cap 到達通知の宛先（両殻 B-118 表示）
            None,
        )
    }

    /// POST「All Keep」: all_keep broadcast を書いてから自身の keep を発火する（B-102 /
    /// egui ComboBox 先頭行と同一ライフサイクル: broadcast → self keep）。broadcast の棚パス
    /// は、厳格DAW scopeに加えて同一host内で明示的に見えるexact PREをclaimするPOST棚へ書く。
    /// これによりAU/VST3のidentity棚が分かれても届き、別host・不可視PRE claimは混ぜない。
    /// 自 keep の結果（有効ペアありなら true）を返す。broadcast 書込失敗は best-effort（無視）。
    pub fn keep_all(&self) -> bool {
        let post_iid = {
            let id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return false,
            };
            if id.project_hash.is_empty() || id.instance_id.is_empty() {
                return false; // 未 enable_post_writes
            }
            id.instance_id.clone()
        };
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        let host_process_id = current_host_process_id();
        if !project_hash.is_empty() {
            if let Ok(p) = StoragePaths::default_platform() {
                let kirin_root = PlatformPaths::current_kirin_tmp_root();
                let mut project_hashes = active_post_project_uuids_for_operation_group(
                    &kirin_root,
                    &project_hash,
                    &daw,
                    host_process_id,
                );
                if project_hashes.is_empty() {
                    project_hashes.push(project_hash.clone());
                }
                for ph in project_hashes {
                    let _ = write_broadcast(&p.plugin_data_dir(), &ph, &post_iid, daw.clone());
                }
            }
        }
        self.keep()
    }

    /// POST「Stop」: pair を解除（record_signal released）し Watch へ戻す（B-061 3d-b）。
    /// PRE 側は released を検出して自身も Record を抜ける（io_thread_pre）。
    pub fn stop(&self) {
        // 元 stop() と同一: exit_record + linkage クリアは常に行い、mark_released は enable 済
        // （identity 非空）のときだけ。共有 free 関数で broadcast 受信 closure と同一経路にする。
        let (project_hash, post_iid) = match self.identity.lock() {
            Ok(id) => (id.project_hash.clone(), id.instance_id.clone()),
            Err(_) => (String::new(), String::new()),
        };
        let paired_pre_target = self.pair_binding.recording_pre();
        resolve_and_exit_stop(
            &self.record_sm,
            &paired_pre_target,
            &project_hash,
            &post_iid,
            Some(ReleaseReason::ManualStop),
        );
    }

    /// POST「All Stop」: all_stop broadcast を書いてから自身の stop を発火する（B-102 /
    /// egui ComboBox 先頭行と同一ライフサイクル: broadcast → self stop）。
    pub fn stop_all(&self) {
        let post_iid = match self.identity.lock() {
            Ok(id) => id.instance_id.clone(),
            Err(_) => String::new(),
        };
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        let host_process_id = current_host_process_id();
        if !project_hash.is_empty() && !post_iid.is_empty() {
            if let Ok(p) = StoragePaths::default_platform() {
                let kirin_root = PlatformPaths::current_kirin_tmp_root();
                let mut project_hashes = live_post_project_uuids_for_operation_group(
                    &kirin_root,
                    &project_hash,
                    &daw,
                    host_process_id,
                );
                if project_hashes.is_empty() {
                    project_hashes.push(project_hash.clone());
                }
                for ph in project_hashes {
                    let _ = write_stop_broadcast(&p.plugin_data_dir(), &ph, &post_iid, daw.clone());
                }
            }
        }
        let (project_hash, post_iid) = match self.identity.lock() {
            Ok(id) => (id.project_hash.clone(), id.instance_id.clone()),
            Err(_) => (String::new(), String::new()),
        };
        let paired_pre_target = self.pair_binding.recording_pre();
        resolve_and_exit_stop(
            &self.record_sm,
            &paired_pre_target,
            &project_hash,
            &post_iid,
            Some(ReleaseReason::AllStop),
        );
    }

    /// pair 候補（Keep 可能な PRE）を列挙する（B-102 / GUI ドロップダウン用・read-only）。
    /// `$TMPDIR/kirin/` 配下から現在の project/session 境界で arm できる PRE を走査し
    /// (instance_id, name) で返す。
    pub fn enumerate_pre_candidates(&self) -> Vec<(String, Option<String>)> {
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        enumerate_live_pre_pair_choices_for_post_project_in_session(
            &kirin_root,
            &project_hash,
            &daw,
        )
        .into_iter()
        .map(|c| (c.instance_id, c.name))
        .collect()
    }

    /// All Keep の「N ready」= 同じexplicit-pair operation groupでpair設定済のActive POST数。
    /// AU/VST3 が project_hash / DAW ID の両方で分裂してもexact PRE可視性で集約する。
    pub fn count_keep_ready(&self) -> usize {
        if self.current_license() != License::Os {
            return 0;
        }
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        let candidates = enumerate_ready_post_pair_candidates_for_operation_group(
            &kirin_root,
            &project_hash,
            &daw,
            current_host_process_id(),
        );
        candidates.len()
    }

    /// POST 側の pair claim 一覧（GUI dropdown の keepability 表示用）。
    /// `count_keep_ready` と同じ explicit-pair operation group を使い、JUCE/egui の表示差を
    /// 生まないための read-only C ABI surface。
    pub fn enumerate_post_pair_claims(&self) -> Vec<(String, Option<String>, Option<String>)> {
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        let candidates = enumerate_owned_post_pair_candidates_for_operation_group(
            &kirin_root,
            &project_hash,
            &daw,
            current_host_process_id(),
        );
        candidates
            .into_iter()
            .map(|c| (c.instance_id, c.pair_pre_name, c.paired_pre_instance_id))
            .collect()
    }

    /// state chunk から復元した識別子を設定する（方式A / B-058 3c）。
    /// **`enable_pre_writes` の前**に呼ぶこと（復元順: create→set_license→set_identity→enable）。
    /// 空文字を渡したキーは `enable_pre_writes` で生成される（instance_id / project_uuid）。
    ///
    /// B-128 (G-115-371): restore 受領の**単一 materialize 点**。`self.identity` に格納する前に
    /// `materialize_restore_field` を通し、path-unsafe な値（絶対/`..`/区切り/制御文字/overlength/
    /// 予約 marker）を fresh new_v4 に差し替える（safe / empty は不変）。これにより keep / record /
    /// 永続化(get_identity) / enable→io_thread が**全て同一 materialize 済 self.identity** を読み、
    /// family 間分裂（raw 第二源）と uncounted-Record-bypass が構造的に消える。kirin_measure の
    /// path builder wall は DiD backstop として維持。invalid 時は invalid-identity event を surface。
    pub fn set_identity(
        &self,
        instance_id: String,
        project_uuid: String,
        daw_session_uuid: String,
        name: String,
    ) {
        if let Ok(mut id) = self.identity.lock() {
            // B-128 (G-115-373 / D3): instance_id を先に materialize し、その結果を tag に project_uuid /
            // daw の anomaly を per-instance routing する（当該 instance の editor が drain → UI へ）。
            // instance_id 自体の anomaly は確定前ゆえ tag なし（global wall 扱い・honest）。
            let iid = kirin_measure::materialize_restore_field(
                &instance_id,
                "ffi.set_identity.instance_id",
                None,
            );
            let tag = if iid.is_empty() {
                None
            } else {
                Some(iid.as_str())
            };
            id.project_uuid = kirin_measure::materialize_restore_field(
                &project_uuid,
                "ffi.set_identity.project_uuid",
                tag,
            );
            id.daw_session_uuid = kirin_measure::materialize_restore_field(
                &daw_session_uuid,
                "ffi.set_identity.daw_session_uuid",
                tag,
            );
            id.instance_id = iid;
            id.name = name; // name は path component でない（traversal 非該当・scope 外）。
        }
    }

    /// 現在の識別子スナップショット（JUCE が getStateInformation で chunk へ保存）。
    fn identity_snapshot(&self) -> IdentityState {
        self.identity.lock().map(|g| g.clone()).unwrap_or_default()
    }

    /// Record中の最新producer sample境界へ固定タグMARKを追加する。
    pub fn add_mark(&self, tag: String) -> bool {
        if !can_write_plugin_data(self.current_license())
            || self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post)
        {
            return false;
        }
        enqueue_record_mark(
            &self.record_sm,
            &self.record_take_tracker,
            &self.record_mark_queue,
            &tag,
        )
        .is_ok()
    }

    /// Record の最新 plugin_data .json に利用者メモ（Annotation）を追記する（Note / 方式A）。
    ///
    /// gate: `License::Os`（`can_write_plugin_data`）のみ通る。`enable_pre_writes` 後で
    /// 対象 .json が存在するとき `true`。それ以外（非 Os / 未 enable / .json 不在）は `false`。
    /// filesystem 操作は `kirin_measure::append_annotation_to_latest` に委譲（FFI は呼ぶだけ）。
    ///
    /// 注意（3c）: active record 中は io_thread の writer が 30s flush で .json を上書きする
    /// ため、確実なのは Record close 後の最新 .json への追記。
    pub fn add_annotation(&self, memo: String) -> bool {
        if !can_write_plugin_data(self.current_license()) {
            return false; // 二重 gate: Os 以外は不可（license.rs:88）。
        }
        // B-067/F3: 書込 role はこの engine の有効化時に確定（PRE/POST）。未 enable は no-op。
        // ハードコード ::Pre をやめ、保持 role に書く（POST engine は POST role に追記）。
        let role = match self.write_role.lock() {
            Ok(g) => match *g {
                Some(r) => r, // Role は Copy（plugin_data.rs:54）。
                None => return false,
            },
            Err(_) => return false,
        };
        let (project_hash, instance_id) = {
            let id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return false,
            };
            if id.project_hash.is_empty() || id.instance_id.is_empty() {
                return false; // 未 enable。
            }
            (id.project_hash.clone(), id.instance_id.clone())
        };
        let base = match StoragePaths::default_platform() {
            Ok(p) => p.plugin_data_dir(),
            Err(_) => return false,
        };
        append_annotation_to_latest(&base, &project_hash, &instance_id, role, memo).unwrap_or(false)
    }

    /// interleaved f32 サンプルを供給する（Audio Thread 単独・RT-safe）。
    ///
    /// 責務はこの 2 つのみ:
    /// 1. heartbeat を進める（B-118/G-115-245: ~3s stall override 回避）。
    /// 2. interleaved サンプルを rtrb に push（満杯時は drop。本番 process() の
    ///    `let _ = producer.push(*sample)` と同挙動 / hypha_pre.rs:410）。
    ///
    /// `num_channels` は create 時の channel count と一致している必要がある。
    /// 不一致ブロックは heartbeat だけ進め、測定 ring には入れない（防御ガード）。
    /// `interleaved.len()` は `num_frames * num_channels` を想定。
    pub fn push_samples(&self, interleaved: &[f32], num_channels: u32) {
        // (1) heartbeat は常に進める（空ブロック keepalive でも Active を維持できる）。
        self.heartbeat.fetch_add(1, Ordering::Relaxed);
        if self.watch_playback_pass_id.load(Ordering::Acquire) == 0 {
            self.watch_playback_pass_id.store(1, Ordering::Release);
            reset_watch_ring_cursor(
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                1,
            );
        }

        // B-118: watchdog が Measure Thread を再起動したとき pending_producer に新 Producer が来る。
        // 毎 call の先頭で try_lock（非ブロッキング）し、再起動後の Watch 欠落を1 callback以内に抑える。
        if let Ok(mut slot) = self.pending_producer.try_lock() {
            if let Some(new_producer) = slot.take() {
                self.record_take_tracker.reset_capture_clock();
                // SAFETY: push_samples は Audio Thread 単独（SPSC）。Producer 差し替えも同契約内。
                unsafe {
                    *self.ring_producer.get() = new_producer;
                }
                let pass_id = self.watch_playback_pass_id.load(Ordering::Acquire);
                reset_watch_ring_cursor(
                    &self.watch_ring_cursor_epoch,
                    &self.watch_ring_cursor_pass_id,
                    &self.watch_ring_cursor_samples,
                    pass_id,
                );
                self.watch_ring_replacing.store(false, Ordering::Release);
            }
        }

        // (2) create 時の layout と異なるブロックは測定に入れない。
        if num_channels as usize != self.num_channels {
            return;
        }

        // SAFETY: push_samples は Audio Thread 単独という FFI 契約。Producer への
        // 排他アクセスは単一スレッドに限定される（SPSC）。
        let producer = unsafe { &mut *self.ring_producer.get() };
        let mut pushed = 0_u64;
        for &s in interleaved {
            if producer.push(s).is_err() {
                self.push_overflow.fetch_add(1, Ordering::Relaxed);
            } else {
                pushed += 1;
            }
        }
        if pushed > 0 && !self.watch_ring_replacing.load(Ordering::Acquire) {
            let pass_id = self.watch_playback_pass_id.load(Ordering::Acquire);
            add_watch_ring_cursor_samples(
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                pass_id,
                pushed,
            );
        }
    }

    /// 最新の RT 計測結果を取得（UI Thread / try_lock 非ブロッキング）。
    /// ロック競合中・未計測時は `None`。
    pub fn poll_result(&self) -> Option<MeasureResult> {
        match self.measure_result.try_lock() {
            Ok(g) => Some(g.clone()),
            Err(_) => None,
        }
    }

    /// セッション集計の取得（UI Thread / try_lock 非ブロッキング）。
    /// Record 中に Measure Thread が finalize した値、または Record→Watch 後の直近値。
    /// 未 Record・ロック競合時は `None`。
    pub fn poll_session(&self) -> Option<SessionSummary> {
        match self.session_summary.try_lock() {
            Ok(g) => *g,
            Err(_) => None,
        }
    }

    /// ring 満杯で drop した push 数（§8 RT-safety 検証用）。
    pub fn overflow_count(&self) -> u64 {
        self.push_overflow.load(Ordering::Relaxed)
    }

    /// B-125: oversized block drop を計上する（Audio Thread から呼ばれる）。
    /// `kirin_hypha_note_oversized_drop` C-ABI の本体。`dropped_samples` は当該 block の
    /// interleaved sample 数（= num_frames * num_channels）。RT 安全のため `fetch_add` のみ
    /// （alloc/lock/syscall なし）。push_overflow とは別カウンタ（混ぜない）。
    pub fn note_oversized_drop(&self, dropped_samples: u64) {
        self.oversized_drop
            .fetch_add(dropped_samples, Ordering::Relaxed);
    }

    /// B-125: oversized block で drop した累積 interleaved sample 数（読み取り専用）。
    pub fn oversized_drop_count(&self) -> u64 {
        self.oversized_drop.load(Ordering::Relaxed)
    }

    /// B-129 reopen (G-115-380): **test-only** — Audio→Measure ring が全消費されたか。
    /// `producer.slots()`（書込可能空きスロット数）== ring 容量 ⟺ consumer が push 済み全サンプルを
    /// pop しきった状態。parity の session_finalize gate が「lufs_i 値プラトー」でなく ring 全消費を
    /// 直接確認するための read-only introspection。**計測数値サーフェスではない**（bool を返すのみ・
    /// engine.rs / 本番 finalize / FFI 計測数値は不変）。容量は `new()` の構築式
    /// （sample_rate * RING_BUFFER_SECONDS * num_channels）と同一に算出するため、watchdog の
    /// Producer 差し替え後も不変。
    ///
    /// SAFETY: `ring_producer`(UnsafeCell<rtrb::Producer>) は SPSC 契約で「Audio/test 単独スレッド」
    /// からのみ触れる（struct の `unsafe impl Sync` 根拠と同一）。本メソッドも push_samples と同一
    /// スレッドから順次呼ばれ時間的に重ならない（&mut と & の同時生成なし）。`Producer::slots()` は
    /// `&self` の read-only で head（consumer 位置）を Acquire load するのみで、consumer 側の pop と
    /// 並行しても rtrb SPSC 設計上健全。
    #[doc(hidden)]
    pub fn __ring_drained_for_test(&self) -> bool {
        let capacity = (self.sample_rate as usize) * RING_BUFFER_SECONDS * self.num_channels;
        // SAFETY: 上記参照（SPSC・push_samples と同一スレッド・read-only slots()）。
        let producer = unsafe { &*self.ring_producer.get() };
        producer.slots() == capacity
    }
}

impl Drop for KirinHyphaEngine {
    fn drop(&mut self) {
        let is_post = self.write_role.lock().ok().and_then(|g| *g) == Some(PluginDataRole::Post);
        if is_post {
            let (project_hash, post_iid) = match self.identity.lock() {
                Ok(id) => (id.project_hash.clone(), id.instance_id.clone()),
                Err(_) => (String::new(), String::new()),
            };
            let paired_pre_target = self.pair_binding.recording_pre();
            resolve_and_exit_stop(
                &self.record_sm,
                &paired_pre_target,
                &project_hash,
                &post_iid,
                None,
            );
        }
        // B-118: io/measure は watchdog（join_on_shutdown=true）が所有・join する。
        // 順序: ① watchdog_shutdown=true（再起動ガードを先に立て、shutdown 中の re-spawn を抑止）
        //       ② measure(self.shutdown) + 現世代 io の shutdown=true（各 thread を停止させる）
        //       ③ watchdog join（loop break → 内部で io→measure 順に join＝free 前に全 thread 停止）
        //       ④ identity refcount −1（B-110・全 thread join 後なので共有セル clear と非競合）
        // io_thread lock は watchdog の post-loop join も取るため、③ の前に必ず解放する（deadlock 回避）。
        self.watchdog_shutdown.store(true, Ordering::Relaxed);
        self.shutdown.store(true, Ordering::Relaxed);
        if let Ok(slot) = self.io_thread.lock() {
            if let Some(io) = slot.as_ref() {
                io.shutdown.store(true, Ordering::Relaxed);
            }
        }
        if let Ok(mut wh) = self.watchdog_handle.lock() {
            if let Some(h) = wh.take() {
                let _ = h.join(); // watchdog が io→measure を join してから終了
            }
        }
        // B-110: live インスタンス refcount −1。watchdog が全世代の io→measure を join した後なので、
        // refcount 0 到達時の共有セル clear が生存 thread の Arc live-read と競合しない。
        identity_instance_detach(clear_role_scoped_cells);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C ABI（§3 の契約。Option<f64> は NaN sentinel で表す）
// ─────────────────────────────────────────────────────────────────────────────

/// `KirinMeasureResult` — RT 計測結果（C struct）。Option は NaN で表す。
#[repr(C)]
pub struct KirinMeasureResult {
    pub lufs_m: f64,
    pub true_peak: f64, // tp_recent: 直近 400ms（B-074）
    pub crest: f64,
    pub psr: f64,
    pub n_prime_total: f64,
    pub sharpness: f64,
    pub psb_low: f64,
    pub psb_mid: f64,
    pub psb_high: f64,
    pub n_prime: [f64; 20],
    pub psb_bark: [f64; 20],
    // B-074: 末尾追加で既存フィールド offset を不変に保つ。
    // tp_session_max: init 以降の inter-sample running max [dBTP]（Record 正本と同定義）。
    pub tp_session_max: f64,
    // B-075: ring 満杯で測定 ring に push できなかった累積サンプル数（>0 = integrity 低下）。
    // 計測値は汚さない「欠落の露出」のみ。poll_result が engine の overflow_count() から注入する。
    pub dropped_samples: u64,
}

#[repr(C)]
pub struct KirinWatchDisplay {
    pub current: KirinMeasureResult,
    pub maximum: KirinMeasureResult,
}

/// `KirinSessionSummary` — セッション集計（C struct）。Option は NaN で表す。
#[repr(C)]
pub struct KirinSessionSummary {
    pub lufs_i: f64,
    pub lra: f64,
    pub max_true_peak: f64,
}

/// `KirinIdentity` — state chunk 往復する識別子（C struct / 方式A）。
/// 各フィールドは null 終端 C 文字列（最大 63 文字 + null）。`project_hash` は派生値の
/// ため含めない（JUCE は instance_id / project_uuid / daw_session_uuid / name の 4 キーを
/// chunk に保存する）。
#[repr(C)]
pub struct KirinIdentity {
    pub instance_id: [c_char; ID_BUF_LEN],
    pub project_uuid: [c_char; ID_BUF_LEN],
    pub daw_session_uuid: [c_char; ID_BUF_LEN],
    pub name: [c_char; ID_BUF_LEN],
}

/// `KirinPreCandidate` — pair 候補 1 件（C struct / B-102 ドロップダウン用）。
/// `instance_id` は null 終端 C 文字列（最大 63 文字）。`name` は PRE 表示名（旧 schema /
/// 未設定は `has_name=0` で `name` 内容は不定）。固定長 out-param で marshalling する。
#[repr(C)]
pub struct KirinPreCandidate {
    pub instance_id: [c_char; ID_BUF_LEN],
    pub name: [c_char; ID_BUF_LEN],
    pub has_name: u8,
}

/// `KirinPostPairClaim` — POST pair claim 1 件（C struct / dropdown keepability 表示用）。
#[repr(C)]
pub struct KirinPostPairClaim {
    pub instance_id: [c_char; ID_BUF_LEN],
    pub pair_pre_name: [c_char; ID_BUF_LEN],
    pub has_pair_pre_name: u8,
    pub paired_pre_instance_id: [c_char; ID_BUF_LEN],
    pub has_paired_pre_instance_id: u8,
}

/// `KirinDelta` — POST の Δ（C struct / B-061 3d-b）。各 double の「値なし」は NaN。
/// `mode`: 0=Active / 1=Stale / 2=NoPre / 3=Bypassed。
#[repr(C)]
pub struct KirinDelta {
    pub mode: u8,
    pub lufs: f64,
    pub true_peak: f64,
    pub crest: f64,
    pub psr: f64,
    pub n_prime_total: f64,
    pub sharpness: f64,
}

#[inline]
fn opt_f64(v: Option<f64>) -> f64 {
    v.unwrap_or(f64::NAN)
}

#[inline]
fn opt_arr20(v: Option<[f64; 20]>) -> [f64; 20] {
    v.unwrap_or([f64::NAN; 20])
}

fn to_c_result(r: &MeasureResult) -> KirinMeasureResult {
    let (low, mid, high) = match &r.psb_summary {
        Some(PsbSummary { low, mid, high }) => (*low, *mid, *high),
        None => (f64::NAN, f64::NAN, f64::NAN),
    };
    KirinMeasureResult {
        lufs_m: opt_f64(r.lufs_m),
        true_peak: opt_f64(r.true_peak),
        crest: opt_f64(r.crest),
        psr: opt_f64(r.psr),
        n_prime_total: opt_f64(r.n_prime_total),
        sharpness: opt_f64(r.sharpness),
        psb_low: low,
        psb_mid: mid,
        psb_high: high,
        n_prime: opt_arr20(r.n_prime),
        psb_bark: opt_arr20(r.psb_bark),
        tp_session_max: opt_f64(r.tp_session_max), // B-074: 末尾
        dropped_samples: 0, // B-075: poll_result wrapper が engine の overflow_count() で上書きする
    }
}

fn to_c_session(s: &SessionSummary) -> KirinSessionSummary {
    KirinSessionSummary {
        lufs_i: opt_f64(s.lufs_i),
        lra: opt_f64(s.lra),
        max_true_peak: opt_f64(s.max_true_peak),
    }
}

fn to_c_delta(d: &DeltaResult) -> KirinDelta {
    KirinDelta {
        mode: match d.mode {
            DeltaMode::Active => 0,
            DeltaMode::Stale => 1,
            DeltaMode::NoPre => 2,
            DeltaMode::Bypassed => 3,
        },
        lufs: opt_f64(d.lufs),
        true_peak: opt_f64(d.tp),
        crest: opt_f64(d.crest),
        psr: opt_f64(d.psr),
        n_prime_total: opt_f64(d.n_prime_total),
        sharpness: opt_f64(d.sharpness),
    }
}

/// ランタイムを生成して不透明ポインタを返す（失敗時 null は返さない）。
///
/// # Safety
/// 返り値は `kirin_hypha_destroy` でのみ解放すること。
#[no_mangle]
pub extern "C" fn kirin_hypha_create(sample_rate: u32, num_channels: u32) -> *mut KirinHyphaEngine {
    // panic を C ABI 境界で止める。panic 時は null を返す（UB 回避）。論理は変えない。
    catch_unwind(AssertUnwindSafe(|| {
        Box::into_raw(Box::new(KirinHyphaEngine::new(sample_rate, num_channels)))
    }))
    .unwrap_or(std::ptr::null_mut())
}

/// 信号状態を設定（0=Inactive 1=Active 2=Bypassed）。
///
/// # Safety
/// `handle` は `kirin_hypha_create` の戻り値（非 null・未解放）であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_signal_state(handle: *mut KirinHyphaEngine, state: u8) {
    // panic 捕捉時は no-op（音声経路に影響させない）。
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).set_signal_state(state) };
    }));
}

/// 現在の信号状態を読む（0=Inactive 1=Active 2=Bypassed）。LED poller 系（read-only）。
/// Measure Thread の heartbeat 停止検出で `Inactive` へ上書きされた値も反映する（B-113）。
/// 殻 editor はこの値で表示分岐し、processBlock 停止後に stale な Active を表示しない。
///
/// # Safety
/// `handle` は `kirin_hypha_create` の戻り値（非 null・未解放）であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_signal_state(handle: *mut KirinHyphaEngine) -> u8 {
    // panic / null は 0=Inactive（安全側＝表示は `---`）。
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return 0u8;
        }
        unsafe { (*handle).signal_state_abi() }
    }))
    .unwrap_or(0)
}

/// identity.json からライセンスコードを読む（0=Os 1=Sense 2=Unknown）。ハンドル不要。
/// `~/Library/Application Support/Kirin OS/identity.json` の `"license"` を loose 抽出する
/// `kirin_measure::load_license_safe` を包む。ファイル不在・parse 失敗・$HOME 不在は 2=Unknown
/// （安全側＝Record 不可）。殻はこの戻り値を `set_license` に渡す（出所一本化）。
#[no_mangle]
pub extern "C" fn kirin_hypha_load_license() -> u8 {
    catch_unwind(|| license_to_abi(load_license_safe())).unwrap_or(LICENSE_UNKNOWN)
}

/// ライセンスを設定（0=Os 1=Sense 2=Unknown / 未知値は安全側 Unknown）。
/// 次回 Keep の開始 gate にだけ反映し、開始済み Keep は停止しない。
///
/// # Safety
/// `handle` は `kirin_hypha_create` の戻り値（非 null・未解放）であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_license(handle: *mut KirinHyphaEngine, license: u8) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).set_license(license) };
    }));
}

/// Record を終了し Watch へ戻す（無条件・冪等）。SessionSummary は Measure Thread が
/// 直近 finalize 値を `session_summary` に保持済み（`poll_session` で取得）。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_exit_record(handle: *mut KirinHyphaEngine) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).exit_record() };
    }));
}

/// PRE の plugin_data 書込（Watch pre.json + Record frames/PSB）を有効化する（3b）。
/// **`set_license` の後に 1 度呼ぶ**こと（呼んだ時点の license をスナップショット）。
/// 2 度目以降は no-op。filesystem 書込は kirin_measure の io_thread_pre 内に閉じる。
///
/// # Safety
/// `handle` は `kirin_hypha_create` の戻り値（非 null・未解放）であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enable_pre_writes(handle: *mut KirinHyphaEngine) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).enable_pre_writes() };
    }));
}

/// POST のランタイム（post.json の Δ を select_target_pre 経由＝厳格選定で書く）を有効化する
/// （3d-a）。`enable_pre_writes` と対・同一 engine では排他（片方のみ・冪等）。
/// Keep/ack（write_pending）は配線しない（3d-b）。set_license / set_identity の後に呼ぶ。
///
/// # Safety
/// `handle` は `kirin_hypha_create` の戻り値（非 null・未解放）であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enable_post_writes(handle: *mut KirinHyphaEngine) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).enable_post_writes() };
    }));
}

/// POST の対 PRE 名（pair target）を設定する（3d-b / identity.name 結合を解く）。
/// io_thread と Arc 共有のため enable_post_writes 後でも live 反映。null は空文字扱い。
///
/// # Safety
/// `handle` は有効なハンドル。`name` は null か有効な null 終端 C 文字列であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_pair_target(
    handle: *mut KirinHyphaEngine,
    name: *const c_char,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        let nm = unsafe { read_c_str(name) };
        unsafe { (*handle).set_pair_target(nm) };
    }));
}

/// Select one exact PRE runtime from the dropdown. Returns false when that runtime is no longer a
/// live in-scope choice. UI thread only.
///
/// # Safety
/// A non-null `handle` must be a live pointer returned by `kirin_hypha_create`. `instance_id` may
/// be null; otherwise it must point to a readable null-terminated C string for this call.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_select_pair_candidate(
    handle: *mut KirinHyphaEngine,
    instance_id: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let instance_id = unsafe { read_c_str(instance_id) };
        unsafe { (*handle).set_pair_candidate(&instance_id) }
    }))
    .unwrap_or(false)
}

/// Factual pair status: 0=Unpaired, 1=Waiting, 2=Paired.
///
/// # Safety
/// A non-null `handle` must be a live pointer returned by `kirin_hypha_create`. A null handle is
/// accepted and returns `Unpaired`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_pair_status(handle: *mut KirinHyphaEngine) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return PairStatus::Unpaired as u8;
        }
        unsafe { (*handle).pair_status() as u8 }
    }))
    .unwrap_or(PairStatus::Unpaired as u8)
}

/// Return the exact PRE instance currently latched by a POST.
///
/// # Safety
/// A non-null `handle` must be a live pointer returned by `kirin_hypha_create`. When `out` is
/// non-null and `out_len > 0`, it must reference a writable buffer of at least `out_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_paired_pre_instance_id(
    handle: *mut KirinHyphaEngine,
    out: *mut c_char,
    out_len: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || out_len == 0 {
            return false;
        }
        let Some(instance_id) = (unsafe { (*handle).paired_pre_instance_id() }) else {
            return false;
        };
        let bytes = instance_id.as_bytes();
        let n = bytes.len().min(out_len - 1);
        let dst = unsafe { std::slice::from_raw_parts_mut(out as *mut u8, out_len) };
        dst[..n].copy_from_slice(&bytes[..n]);
        dst[n] = 0;
        true
    }))
    .unwrap_or(false)
}

/// PRE の自名（pre name）を設定する（B-054 / set_pair_target と完全対称）。
/// io_thread_pre と Arc 共有のため enable_pre_writes 後でも live 反映。null は空文字扱い。
/// 値は内部で sanitize される（ASCII graphic + space / 最大 16 文字）. POST 側 pair target と
/// 同一語彙. 空文字はそのまま空 name として書かれる（instance_id 先頭8字 fallback は GUI 表示専用）.
///
/// # Safety
/// `handle` は有効なハンドル。`name` は null か有効な null 終端 C 文字列であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_pre_name(
    handle: *mut KirinHyphaEngine,
    name: *const c_char,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        let nm = unsafe { read_c_str(name) };
        unsafe { (*handle).set_pre_name(nm) };
    }));
}

/// Measure Thread が生存しているか（B-054 LED Error 状態 poller / UI Thread）。null は false。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_measure_alive(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).measure_alive() }
    }))
    .unwrap_or(false)
}

/// B-115: heartbeat 鮮度を読む（processBlock が呼ばれている事実 / read-only poller / UI Thread）。
/// signal_state とは別軸。殻は `playing かつ live` で POST pair 変更をロックする。null / panic は
/// false（安全側＝ロックしない＝false-release より誤ロックを避ける）。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_heartbeat_live(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).heartbeat_live() }
    }))
    .unwrap_or(false)
}

/// B-118 Phase 3 (②): 現プロジェクトが Record 排他上限（12）に達しているか（advisory poller / UI Thread）。
/// keep 経路は `resolve_and_enter_keep` の reserve→count>MAX を正本にする。
/// null / panic は false。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_record_exclusion_conflict(
    handle: *mut KirinHyphaEngine,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).record_exclusion_conflict() }
    }))
    .unwrap_or(false)
}

/// B-118 Phase 3 (③): io_thread 連続失敗の固定文言を `out`（最大 `out_len-1` バイト + null 終端）へ書く。
/// 文言あり=true / 通常（None）・null・panic=false（out 不変）。read-only poller / UI Thread。
///
/// # Safety
/// `handle` は有効なハンドル。`out` は `out_len` バイト以上の有効バッファ。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_record_error_message(
    handle: *mut KirinHyphaEngine,
    out: *mut c_char,
    out_len: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || out_len == 0 {
            return false;
        }
        match unsafe { (*handle).record_error_message() } {
            Some(msg) => {
                let bytes = msg.as_bytes();
                let n = bytes.len().min(out_len - 1);
                let dst = unsafe { std::slice::from_raw_parts_mut(out as *mut u8, out_len) };
                dst[..n].copy_from_slice(&bytes[..n]);
                dst[n] = 0; // null 終端
                true
            }
            None => false,
        }
    }))
    .unwrap_or(false)
}

/// PRE が POST の record_signal を ack 済みか（B-054 LED / Keeping バナー poller / UI Thread）。
/// enable_pre_writes 前 / POST engine / null は false。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_record_acknowledged(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).record_acknowledged() }
    }))
    .unwrap_or(false)
}

/// POST に pair 可能な PRE preset が居るか（B-054 PresetAvailable LED poller / UI Thread）。
/// enable_post_writes 前 / PRE engine / null は false。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_preset_available(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).preset_available() }
    }))
    .unwrap_or(false)
}

/// POST「Keep」: 厳格選定で対 PRE を一意決定し record_signal(pending) を書く（3d-b）。
/// Os かつ一意 PRE のとき true / 選定 None・非 Os・AlreadyRecording は false。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_keep(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).keep() }
    }))
    .unwrap_or(false)
}

/// Drop 後の WAV expected metadata を登録し、閉じた今回 Record を即時再照合する。
/// Keep はこの事前登録を必要としない。
///
/// # Safety
/// `handle` は有効なハンドル。文字列ポインタは null または有効な null 終端 C 文字列。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_expected_wav_metadata(
    handle: *mut KirinHyphaEngine,
    bounce_id: *const c_char,
    expected_duration_samples: u64,
    expected_sample_rate: u32,
    wav_path: *const c_char,
    wav_file_size: u64,
    wav_mtime_ms: i64,
    wav_hash: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let bounce_id = unsafe { read_c_str(bounce_id) };
        let wav_path = unsafe { read_c_str(wav_path) };
        let wav_hash = unsafe { read_c_str(wav_hash) };
        unsafe {
            (*handle).set_expected_wav_metadata(ExpectedWavMetadataInput {
                bounce_id,
                expected_duration_samples,
                expected_sample_rate,
                wav_path,
                wav_file_size,
                wav_mtime_ms,
                wav_hash,
            })
        }
    }))
    .unwrap_or(false)
}

/// POST が Record 中か（true=Record / false=Watch）。pairing UI が Keep(Watch)/Stop(Record)
/// を出し分けるための読み取り（3d-b）。null は false。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_is_recording(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).is_recording() }
    }))
    .unwrap_or(false)
}

/// Record take の実レンダー長を通知する（Audio Thread 単独・RT-safe）。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_note_record_block(
    handle: *mut KirinHyphaEngine,
    recording: bool,
    rendered: bool,
    playing: bool,
    offline: bool,
    position_valid: bool,
    position_samples: i64,
    num_frames: u64,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe {
            (*handle).note_record_block(RecordTakeBlock {
                generation: 0,
                recording,
                rendered,
                playing,
                offline,
                position_valid,
                position_samples,
                num_frames,
                clock_start_samples: 0,
                clock_end_samples: None,
            })
        };
    }));
}

/// Record take のWAV/native clock windowを通知する（Audio Thread単独・RT-safe）。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_note_record_window(
    handle: *mut KirinHyphaEngine,
    recording: bool,
    rendered: bool,
    playing: bool,
    offline: bool,
    position_valid: bool,
    position_samples: i64,
    num_frames: u64,
    clock_start_samples: i64,
    clock_end_valid: bool,
    clock_end_samples: i64,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe {
            (*handle).note_record_block(RecordTakeBlock {
                generation: 0,
                recording,
                rendered,
                playing,
                offline,
                position_valid,
                position_samples,
                num_frames,
                clock_start_samples,
                clock_end_samples: clock_end_valid.then_some(clock_end_samples),
            })
        };
    }));
}

/// measurement ring へ投入する窓の host sample clock を通知する（Audio Thread単独・RT-safe）。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_note_capture_window(
    handle: *mut KirinHyphaEngine,
    position_valid: bool,
    position_samples: i64,
    num_frames: u64,
    clock_source: u8,
    presentation_source: u8,
    input_presentation_valid: bool,
    input_presentation_samples: u32,
    output_presentation_valid: bool,
    output_presentation_samples: u32,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe {
            (*handle).note_capture_window_with_presentation(
                position_valid,
                position_samples,
                num_frames,
                CaptureClockSource::from_abi(clock_source),
                PresentationLatencySamples {
                    source: PresentationLatencySource::from_abi(presentation_source),
                    input: input_presentation_valid.then_some(input_presentation_samples),
                    output: output_presentation_valid.then_some(output_presentation_samples),
                },
            );
        }
    }));
}

/// Host transport block notification used only to delimit Watch MAX passes.
/// Audio Thread safe: atomics + bounded seqlock writes, no allocation/IO/lock.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_note_transport_block(
    handle: *mut KirinHyphaEngine,
    playing: bool,
    position_valid: bool,
    position_samples: i64,
    num_frames: u64,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !handle.is_null() {
            unsafe {
                (*handle).note_transport_block(
                    playing,
                    position_valid,
                    position_samples,
                    num_frames,
                )
            };
        }
    }));
}

/// POST「Stop」: pair を解除（record_signal released）し Watch へ戻す（3d-b）。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_stop(handle: *mut KirinHyphaEngine) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).stop() };
    }));
}

/// POST「All Keep」: all_keep broadcast を書いてから自身の keep を発火する（B-102 / 新↔新）。
/// 同一 DAW セッションの他 POST が自分の pair を keep する。自 keep 成功（有効ペアあり）で true。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_keep_all(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).keep_all() }
    }))
    .unwrap_or(false)
}

/// POST「All Stop」: all_stop broadcast を書いてから自身の stop を発火する（B-102 / 新↔新）。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_stop_all(handle: *mut KirinHyphaEngine) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).stop_all() };
    }));
}

/// All Keep の「N ready」= pair 設定済の Active POST 数（B-102 / egui n_ready と同一・UI Thread）。
/// null は 0。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_count_keep_ready(handle: *mut KirinHyphaEngine) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return 0;
        }
        unsafe { (*handle).count_keep_ready() }
    }))
    .unwrap_or(0)
}

/// POST 側の pair claim を `out`（最大 `cap` 件）へ書き、書いた件数を返す（UI Thread）。
/// GUI は PRE 候補名と照合して "Can Keep" / "Keep ready" / "In use" を表示する。
///
/// # Safety
/// `handle` は有効なハンドル。`out` は `cap` 要素以上の書込可能 `KirinPostPairClaim` 配列。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enumerate_post_pair_claims(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinPostPairClaim,
    cap: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || cap == 0 {
            return 0;
        }
        let claims = unsafe { (*handle).enumerate_post_pair_claims() };
        let n = claims.len().min(cap);
        let slice = unsafe { std::slice::from_raw_parts_mut(out, n) };
        for (dst, (iid, pair_pre_name, paired_pre_instance_id)) in
            slice.iter_mut().zip(claims.into_iter().take(n))
        {
            write_c_buf(&mut dst.instance_id, &iid);
            match pair_pre_name {
                Some(name) => {
                    write_c_buf(&mut dst.pair_pre_name, &name);
                    dst.has_pair_pre_name = 1;
                }
                None => {
                    write_c_buf(&mut dst.pair_pre_name, "");
                    dst.has_pair_pre_name = 0;
                }
            }
            match paired_pre_instance_id {
                Some(instance_id) => {
                    write_c_buf(&mut dst.paired_pre_instance_id, &instance_id);
                    dst.has_paired_pre_instance_id = 1;
                }
                None => {
                    write_c_buf(&mut dst.paired_pre_instance_id, "");
                    dst.has_paired_pre_instance_id = 0;
                }
            }
        }
        n
    }))
    .unwrap_or(0)
}

/// pair 候補（Active な PRE）を `out`（最大 `cap` 件）へ書き、書いた件数を返す（B-102 / UI Thread）。
/// `out` は呼び出し側が確保した `KirinPreCandidate[cap]`。`cap` を超える候補は切り捨てる。
/// null / `cap==0` は 0。各 `instance_id` / `name` は null 終端（最大 63 文字 / `has_name` で名前有無）。
///
/// # Safety
/// `handle` は有効なハンドル。`out` は `cap` 要素以上の書込可能 `KirinPreCandidate` 配列であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enumerate_pre_candidates(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinPreCandidate,
    cap: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || cap == 0 {
            return 0;
        }
        let cands = unsafe { (*handle).enumerate_pre_candidates() };
        let n = cands.len().min(cap);
        let slice = unsafe { std::slice::from_raw_parts_mut(out, n) };
        for (dst, (iid, name)) in slice.iter_mut().zip(cands.into_iter().take(n)) {
            write_c_buf(&mut dst.instance_id, &iid);
            match name {
                Some(nm) => {
                    write_c_buf(&mut dst.name, &nm);
                    dst.has_name = 1;
                }
                None => {
                    write_c_buf(&mut dst.name, "");
                    dst.has_name = 0;
                }
            }
        }
        n
    }))
    .unwrap_or(0)
}

/// POST の Δ を `out` に書く（3d-b / GUI 表示用）。値があれば true、競合/未計測なら false。
/// `post.json` には Δ でなく POST 生メトリクスが入る。Δ はこの API で公開する。
///
/// # Safety
/// `handle`/`out` は有効。`out` は書込可能な `KirinDelta`。UI Thread から呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_delta(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinDelta,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        match unsafe { (*handle).poll_delta() } {
            Some(d) => {
                unsafe { *out = to_c_delta(&d) };
                true
            }
            None => false,
        }
    }))
    .unwrap_or(false)
}

/// state chunk から復元した識別子を設定する（方式A / 3c）。**`enable_pre_writes` の前**に呼ぶ。
/// 各引数は null 終端 C 文字列（null 可＝空文字扱い）。空のキーは enable 時に生成される。
///
/// # Safety
/// `handle` は有効なハンドル。各文字列ポインタは null か有効な null 終端 C 文字列であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_identity(
    handle: *mut KirinHyphaEngine,
    instance_id: *const c_char,
    project_uuid: *const c_char,
    daw_session_uuid: *const c_char,
    name: *const c_char,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        let iid = unsafe { read_c_str(instance_id) };
        let puid = unsafe { read_c_str(project_uuid) };
        let dsid = unsafe { read_c_str(daw_session_uuid) };
        let nm = unsafe { read_c_str(name) };
        unsafe { (*handle).set_identity(iid, puid, dsid, nm) };
    }));
}

/// 現在の識別子を `out` に書く（JUCE が getStateInformation で chunk へ保存）。
/// 各フィールドは null 終端 C 文字列（最大 63 文字）。
///
/// # Safety
/// `handle`/`out` は有効。`out` は書込可能な `KirinIdentity`。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_identity(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinIdentity,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return;
        }
        let id = unsafe { (*handle).identity_snapshot() };
        let out = unsafe { &mut *out };
        write_c_buf(&mut out.instance_id, &id.instance_id);
        write_c_buf(&mut out.project_uuid, &id.project_uuid);
        write_c_buf(&mut out.daw_session_uuid, &id.daw_session_uuid);
        write_c_buf(&mut out.name, &id.name);
    }));
}

/// B-128 (G-115-373 / D3): restore identity の anomaly を **当該 instance の分だけ** 1 件 `out` に drain
/// する（per-instance routing）。`handle` の instance_id に tag された materialize event か、instance
/// context のない wall event（global）を返す。他 instance の event は返さない（false attribution 防止）。
/// event があれば `true` + 文言、無ければ `false`。殻は毎描画 / timer で poll して surface する。
///
/// # Safety
/// `handle` は有効なハンドル（null 可＝global event のみ）。`out` は `len` バイト書込可能な領域であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_drain_path_event(
    handle: *mut KirinHyphaEngine,
    out: *mut c_char,
    len: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if out.is_null() || len == 0 {
            return false;
        }
        // 自 instance_id（materialize 済 self.identity）で per-instance filter。null handle は global のみ。
        let my_instance = if handle.is_null() {
            None
        } else {
            let iid = unsafe { (*handle).identity_snapshot() }.instance_id;
            if iid.is_empty() {
                None
            } else {
                Some(iid)
            }
        };
        match kirin_measure::take_path_event(my_instance.as_deref()) {
            Some(msg) => {
                let bytes = msg.as_bytes();
                let n = bytes.len().min(len - 1);
                let dst = unsafe { std::slice::from_raw_parts_mut(out as *mut u8, len) };
                dst[..n].copy_from_slice(&bytes[..n]);
                dst[n] = 0;
                true
            }
            None => false,
        }
    }))
    .unwrap_or(false)
}

/// Record の最新 plugin_data .json に利用者メモを追記する（Note / 方式A）。
/// `License::Os` かつ enable 済かつ対象 .json 存在のとき `true`、それ以外 `false`。
///
/// # Safety
/// `handle` は有効なハンドル。`memo` は null か有効な null 終端 C 文字列であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_add_annotation(
    handle: *mut KirinHyphaEngine,
    memo: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let memo = unsafe { read_c_str(memo) };
        unsafe { (*handle).add_annotation(memo) }
    }))
    .unwrap_or(false)
}

/// Record中の最新producer sample境界へ Good/Fix/Hold MARKを追加する。
///
/// # Safety
/// `handle` は有効なハンドル。`tag` は null か有効な null 終端 C 文字列であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_add_mark(
    handle: *mut KirinHyphaEngine,
    tag: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let tag = unsafe { read_c_str(tag) };
        unsafe { (*handle).add_mark(tag) }
    }))
    .unwrap_or(false)
}

/// interleaved f32 サンプルを供給（Audio Thread 単独・RT-safe）。
///
/// # Safety
/// `handle` は有効なハンドル。`interleaved` は `num_frames * num_channels` 個の f32 を指す
/// （`num_frames == 0` のときは null 可＝keepalive）。Audio Thread からのみ呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_push_samples(
    handle: *mut KirinHyphaEngine,
    interleaved: *const f32,
    num_frames: usize,
    num_channels: u32,
) {
    // panic 捕捉時は no-op（Audio Thread / 音声素通しに影響させない）。
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        let engine = unsafe { &*handle };
        let len = num_frames.saturating_mul(num_channels as usize);
        if len == 0 || interleaved.is_null() {
            // 0-frame keepalive（heartbeat のみ進める）。
            engine.push_samples(&[], num_channels);
            return;
        }
        let slice = unsafe { std::slice::from_raw_parts(interleaved, len) };
        engine.push_samples(slice, num_channels);
    }));
}

/// B-125: prealloc-max 超の病的 block を drop した interleaved sample 数を計上（Audio Thread 単独）。
/// JUCE 殻が oversized 分岐（scratch 容量超）で push_samples(null,0) keepalive と並べて呼ぶ。
/// 本体は `oversized_drop.fetch_add` のみ（alloc/lock/syscall なし＝R-12 / RT-safe）。
/// `dropped_samples` は当該 block の num_frames * num_channels。push_overflow とは別カウンタ。
///
/// # Safety
/// `handle` は有効なハンドル（非 null・未解放）。Audio Thread からのみ呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_note_oversized_drop(
    handle: *mut KirinHyphaEngine,
    dropped_samples: u64,
) {
    // panic 捕捉時は no-op（Audio Thread / 音声素通しに影響させない）。push_samples と同型。
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).note_oversized_drop(dropped_samples) };
    }));
}

/// 最新 RT 計測結果を `out` に書く。値があれば true、未計測/競合なら false。
///
/// # Safety
/// `handle` は有効なハンドル、`out` は書込可能な `KirinMeasureResult`。UI Thread から呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_result(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinMeasureResult,
) -> bool {
    // panic 捕捉時は false（out は書かない）。
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        match unsafe { (*handle).poll_result() } {
            Some(r) => {
                unsafe {
                    *out = to_c_result(&r);
                    // B-075 / B-125: 欠落サンプル累積数を engine から注入（MeasureResult には持たない）。
                    // ring 満杯 drop（push_overflow）と oversized block drop（oversized_drop）の合算。
                    // 2 カウンタは別計数だが live 露出は合算（無記録欠落の解消＝ZSA）。
                    (*out).dropped_samples = (*handle)
                        .overflow_count()
                        .saturating_add((*handle).oversized_drop_count());
                }
                true
            }
            None => false,
        }
    }))
    .unwrap_or(false)
}

/// Current Watch values and current-playback-pass maxima from one Rust
/// snapshot. UI thread only.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`].
/// `out` must be null or point to writable storage for one [`KirinWatchDisplay`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_watch_display(
    handle: *mut KirinHyphaEngine,
    playing: bool,
    out: *mut KirinWatchDisplay,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some((current, maximum)) = (unsafe { &*handle }).poll_watch_display(playing) else {
            return false;
        };
        unsafe {
            *out = KirinWatchDisplay {
                current: to_c_result(&current),
                maximum: to_c_result(&maximum),
            };
        }
        true
    }))
    .unwrap_or(false)
}

/// セッション集計を `out` に書く。Record 終了後（Measure Thread が finalize 済）に値が
/// あれば true、未 Record・未計測・競合なら false。ABI signature は Phase 1 と不変。
///
/// # Safety
/// `handle`/`out` は有効。UI Thread から呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_session(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinSessionSummary,
) -> bool {
    // panic 捕捉時も false（out は書かない）。
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        match unsafe { (*handle).poll_session() } {
            Some(s) => {
                unsafe { *out = to_c_session(&s) };
                true
            }
            None => false,
        }
    }))
    .unwrap_or(false)
}

/// ランタイムを破棄（shutdown → Measure Thread join）。
///
/// # Safety
/// `handle` は `kirin_hypha_create` の戻り値で、以後二重解放しないこと。null 可。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_destroy(handle: *mut KirinHyphaEngine) {
    // panic 捕捉時は no-op（二重解放はしない）。
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        drop(unsafe { Box::from_raw(handle) });
    }));
}

// `c_void` を未使用警告なく保持（将来 opaque alias 用の置き場）。
#[doc(hidden)]
pub type _KirinHyphaOpaque = c_void;

#[cfg(test)]
mod record_start_latch_tests {
    use super::{KirinHyphaEngine, RecordTakeBlock, LICENSE_OS};

    fn block(
        rendered: bool,
        position_samples: i64,
        clock_end_samples: Option<i64>,
    ) -> RecordTakeBlock {
        RecordTakeBlock {
            generation: 0,
            recording: true,
            rendered,
            playing: false,
            offline: false,
            position_valid: true,
            position_samples,
            num_frames: 512,
            clock_start_samples: clock_end_samples.map_or(0, |_| position_samples),
            clock_end_samples,
        }
    }

    #[test]
    fn ffi_record_start_latches_only_rendered_capture_window() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        engine.set_license(LICENSE_OS);
        assert!(engine.enter_record());

        engine.note_record_block(block(false, 185_880, None));
        assert_eq!(engine.record_sm.record_started_at_position_samples(), None);

        engine.note_record_block(block(true, 0, None));
        assert_eq!(
            engine.record_sm.record_started_at_position_samples(),
            Some(0)
        );
    }

    #[test]
    fn ffi_record_start_latches_explicit_clock_start_for_bounded_window() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        engine.set_license(LICENSE_OS);
        assert!(engine.enter_record());

        engine.note_record_block(block(true, 96_000, Some(97_000)));
        assert_eq!(
            engine.record_sm.record_started_at_position_samples(),
            Some(96_000)
        );
    }
}

#[cfg(test)]
mod b106_shared_id_tests {
    //! B-106/B-301: 空/legacy session の「role fallback・first-wins・live-read」と、
    //! 保存済み document の「非空 daw_session_uuid ごとの identity group」を確認する。
    //! `resolve_shared_id` / `resolve_role_identity` はセルを引数で受けるため、モジュール global
    //! static を触らずに 2 インスタンス相当の収束/分離を検証できる。
    use super::{read_shared_id, resolve_role_identity, resolve_shared_id, IdentityGroups};
    use std::collections::HashMap;
    use std::sync::{Arc, Mutex, RwLock};

    #[test]
    fn second_instance_adopts_shared_value_never_overwrites() {
        let cell = Arc::new(RwLock::new(String::new()));
        // instance A enable: 自分の chunk uuid で seed。
        let a = resolve_shared_id(&cell, "proj-A");
        // instance B enable: 別 chunk uuid を渡しても上書きせず A の値を採用。
        let b = resolve_shared_id(&cell, "proj-B");
        assert_eq!(a, "proj-A");
        assert_eq!(
            b, "proj-A",
            "2 つ目は共有値を採用（毎回生成・上書きの全廃）"
        );
        assert_eq!(read_shared_id(&cell), "proj-A");
    }

    #[test]
    fn write_shelf_equals_all_io_thread_scan_shelves_even_when_overwritten_midway() {
        let cell = Arc::new(RwLock::new(String::new()));
        // spawn 相当: 各 io_thread は共有 Arc の clone を持ち、毎 tick live-read する。
        let scan_clone_a = Arc::clone(&cell);
        let scan_clone_b = Arc::clone(&cell);
        // A enable → broadcast write 棚（keep_all が live-read する値と同一経路）。
        let write_shelf = resolve_shared_id(&cell, "proj-1");
        // B enable が「途中で値を変える」（2 つ目が別 candidate）。
        let _ = resolve_shared_id(&cell, "proj-2");
        // write 棚 == 全 io_thread scan 棚 が一致し続ける（同一実体 = 恒等）。
        assert_eq!(write_shelf, "proj-1");
        assert_eq!(read_shared_id(&scan_clone_a), write_shelf);
        assert_eq!(read_shared_id(&scan_clone_b), write_shelf);
        assert_eq!(read_shared_id(&cell), write_shelf);
    }

    #[test]
    fn empty_candidate_generates_once_then_all_share() {
        let cell = Arc::new(RwLock::new(String::new()));
        // 空ソング / 新規: chunk 空 → 生成して seed。
        let a = resolve_shared_id(&cell, "");
        assert!(!a.is_empty(), "空 candidate は生成して seed する");
        // 2 つ目も chunk 空だが、生成済み共有値を採用（毎回生成しない）。
        let b = resolve_shared_id(&cell, "");
        assert_eq!(a, b, "2 つ目は生成済み共有値を採用");
        // 別 io_thread の scan 棚とも一致。
        let scan = Arc::clone(&cell);
        assert_eq!(read_shared_id(&scan), a);
    }

    #[test]
    fn empty_cell_seeds_from_nonempty_chunk_candidate() {
        // chunk 復元値（非空）があれば、それで seed する（生成しない）。
        let cell = Arc::new(RwLock::new(String::new()));
        let resolved = resolve_shared_id(&cell, "restored-uuid");
        assert_eq!(resolved, "restored-uuid");
        assert_eq!(read_shared_id(&cell), "restored-uuid");
    }

    #[test]
    fn distinct_nonempty_daw_sessions_do_not_share_role_identity() {
        let fallback_project = Arc::new(RwLock::new(String::new()));
        let fallback_daw = Arc::new(RwLock::new(String::new()));
        let groups: IdentityGroups = Mutex::new(HashMap::new());

        let first = resolve_role_identity(
            &fallback_project,
            &fallback_daw,
            &groups,
            "project-mastering",
            "daw-mastering",
        );
        let second = resolve_role_identity(
            &fallback_project,
            &fallback_daw,
            &groups,
            "project-song",
            "daw-song",
        );

        assert_eq!(first, ("project-mastering".into(), "daw-mastering".into()));
        assert_eq!(second, ("project-song".into(), "daw-song".into()));
        assert!(
            read_shared_id(&fallback_project).is_empty(),
            "saved-document grouping must not seed the legacy fallback cell"
        );
    }

    #[test]
    fn empty_daw_session_remains_empty_for_runtime_legacy_bridge() {
        let fallback_project = Arc::new(RwLock::new(String::new()));
        let fallback_daw = Arc::new(RwLock::new(String::new()));
        let groups: IdentityGroups = Mutex::new(HashMap::new());

        let resolved = resolve_role_identity(
            &fallback_project,
            &fallback_daw,
            &groups,
            "project-legacy",
            "",
        );

        assert_eq!(resolved.0, "project-legacy");
        assert_eq!(
            resolved.1, "",
            "empty/legacy daw_session_uuid is not an explicit document identity"
        );
        assert!(
            read_shared_id(&fallback_daw).is_empty(),
            "legacy daw fallback cell must not fabricate an explicit DAW session"
        );
    }
}

#[cfg(test)]
mod b113_signal_state_tests {
    //! B-113: editor の表示状態源を Rust の signal_state 直読に統一する getter
    //! （`kirin_hypha_get_signal_state` → `signal_state_abi` → `signal_state_to_abi`）の
    //! 写像不変条件を決定的に検証する。`signal_state_to_abi` は純粋関数（Measure Thread の
    //! heartbeat 上書きと独立）なので、engine を構築せずに `set_signal_state` の逆写像である
    //! ことを確認できる（heartbeat 停止 → Inactive 上書きの反映そのものは DAW 実測 / kirin_measure
    //! 側 load_signal_state テストで担保）。
    use super::{signal_state_to_abi, SignalState};

    #[test]
    fn signal_state_to_abi_is_inverse_of_set_signal_state() {
        // set_signal_state の写像: 1→Active / 2→Bypassed / _→Inactive。その厳密な逆。
        assert_eq!(
            signal_state_to_abi(SignalState::Inactive),
            0,
            "Inactive → 0"
        );
        assert_eq!(signal_state_to_abi(SignalState::Active), 1, "Active → 1");
        assert_eq!(
            signal_state_to_abi(SignalState::Bypassed),
            2,
            "Bypassed → 2"
        );
    }
}

#[cfg(test)]
mod post_controls_parity_tests {
    use super::*;
    use kirin_measure::license::{show_note_button, show_save_button, show_stop_record_button};

    /// juce_shell/src/PostControls.cpp `PostControls::update` のボタン可視性を Rust に写した replica。
    /// 実 C++ ソースへの忠実性は xtask/src/shell_parity.rs::post_controls_update_visibility_formula_is_pinned
    /// が文字列ゲートで固定する。本 replica は os ゲートを Rust license ヘルパと値レベルで突合するためのもの。
    struct PostVis {
        keep: bool,
        sense: bool,
        stop: bool,
        mark: bool,
    }

    /// PostControls.cpp:73-91 のうち keep/sense/stop/mark の os/sense ゲートのみを Rust に写す
    /// （picker 4 ボタンと markPickerOpen reset は対象外＝string-gate
    /// post_controls_update_visibility_formula_is_pinned で固定）。os=(code==0) / sense=(code==1)。
    fn cpp_post_controls_update(
        recording: bool,
        license_code: u8,
        pair_non_empty: bool,
        mark_picker_open: bool,
    ) -> PostVis {
        let os = license_code == 0;
        let sense = license_code == 1;
        PostVis {
            keep: !recording && os && pair_non_empty,
            sense: !recording && sense,
            stop: recording && os && !mark_picker_open,
            mark: recording && os && !mark_picker_open,
        }
    }

    /// B-195 (Step3 監査ギャップ): 値レベル parity — C++ PostControls::update の os ゲートが
    /// Rust license ヘルパ (show_save_button / show_stop_record_button / show_note_button) と
    /// 全 (license × recording × pairNonEmpty × markPickerOpen) で一致する。実 License→abi
    /// マッピング (license_to_abi) を経由するので、int マッピングかヘルパのどちらが乖離しても捕捉する。
    #[test]
    fn post_controls_visibility_matches_rust_license_helpers() {
        for license in [License::Os, License::Sense, License::Unknown] {
            let code = license_to_abi(license);
            for &recording in &[false, true] {
                for &pair in &[false, true] {
                    for &picker_open in &[false, true] {
                        let v = cpp_post_controls_update(recording, code, pair, picker_open);
                        assert_eq!(
                            v.keep,
                            !recording && show_save_button(license) && pair,
                            "keep parity: {license:?} rec={recording} pair={pair}"
                        );
                        assert_eq!(
                            v.stop,
                            recording && show_stop_record_button(license) && !picker_open,
                            "stop parity: {license:?} rec={recording} picker={picker_open}"
                        );
                        assert_eq!(
                            v.mark,
                            recording && show_note_button(license) && !picker_open,
                            "mark parity: {license:?} rec={recording} picker={picker_open}"
                        );
                    }
                }
            }
        }
    }

    /// Sense ヒントは license==Sense かつ非 recording のときだけ表示され、Keep(Os) とは
    /// 相互排他（同時に出ない）であることを値レベルで固定する。
    #[test]
    fn sense_hint_visibility_is_sense_only_and_exclusive_with_keep() {
        for license in [License::Os, License::Sense, License::Unknown] {
            let code = license_to_abi(license);
            let v = cpp_post_controls_update(false, code, true, false);
            assert_eq!(
                v.sense,
                license == License::Sense,
                "sense-hint は Sense のときだけ: {license:?}"
            );
            assert!(
                !(v.keep && v.sense),
                "Keep と Sense ヒントは同時に出ない: {license:?}"
            );
        }
    }
}
