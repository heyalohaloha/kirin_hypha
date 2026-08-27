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

use std::collections::HashMap;
use std::ffi::CStr;
use std::os::raw::{c_char, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU32, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, OnceLock, RwLock};
use std::thread::JoinHandle;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde_json::Value;
use uuid::Uuid;

use kirin_measure::engine::SessionSummary;
use kirin_measure::reservation; // B-127 (G-115-364): per-pairing O_EXCL reservation
use kirin_measure::{
    add_watch_ring_cursor_samples, publish_watch_playback_pass_boundary, reset_watch_ring_cursor,
};
use kirin_measure::{
    append_annotation_to_latest, can_write_plugin_data, count_distinct_pairings,
    current_host_process_id, enqueue_record_mark,
    enumerate_live_pre_pair_choices_for_post_project_in_session,
    enumerate_owned_post_pair_candidates_for_operation_group,
    enumerate_ready_post_pair_candidates_for_operation_group, identity_instance_attach,
    identity_instance_detach, latch_selected_pre, live_window, load_license_safe,
    load_signal_state, mark_generation_terminal, mark_released_if_current,
    mark_released_with_reason, mark_released_with_reason_if_current, new_record_mark_queue,
    new_record_take_tracker, new_record_trace_queue, pair_owner_instance_dir, pair_status_for_pre,
    pair_status_from_owned_binding_with_intent, pair_status_or_last_known, paired_pre_instance_id,
    read_signal, record_ring_capacity_samples, resolve_arm_target_for_post_project_in_session,
    sanitize_name, select_live_pre_pair_choice_by_instance_for_post_project_in_session,
    set_daw_session_id, set_project_uuid, spawn_io_thread_post, spawn_io_thread_pre,
    spawn_measure_thread, spawn_watchdog, store_signal_state, watch_ring_capacity_samples,
    write_broadcast_for_generation, write_pending_claiming_expected_and_clock_for_generation,
    write_stop_broadcast, write_stop_broadcast_for_generation, CaptureClockSource,
    CaptureGeneration, CaptureGenerationMember, CaptureGenerationTransaction, DeltaMode,
    DeltaResult, GenerationTerminalReason, IoThreadHandle, LatchedPre, License, LiveLicense,
    LivenessEvaluator, MeasureResult, PairOwnershipBinding, PairOwnershipLease, PairStatus,
    PlatformPaths, PluginDataRole, PrePairStatusObserver, PresentationLatencySamples,
    PresentationLatencySource, PsbSummary, RecordDisplaySnapshot, RecordDisplayStatus,
    RecordIngress, RecordMarkQueue, RecordStateMachine, RecordTakeBlock, RecordTakeTracker,
    RecordTraceQueue, ReleaseReason, RestartIoFn, SignalError, SignalState, SpectrumCoordinator,
    SpectrumRuntime, SpectrumRuntimeStats, SpectrumViewSnapshot, SpectrumViewStatus, StoragePaths,
    WatchMaxTracker, WatchProducerHandoff, WatchdogIo, WatchdogParams,
    CAPTURE_PRODUCER_READY_TIMEOUT, MAX_ACTIVE_PER_PROJECT, MAX_AUDIO_BLOCK_FRAMES,
    MAX_CAPTURE_GENERATION_MEMBERS, N_CHANNELS, SPECTRUM_BAND_COUNT,
};

mod pair_binding;

use pair_binding::{PairBinding, PairTargetTransition};

pub const KIRIN_KEEP_PHASE_IDLE: u8 = 0;
pub const KIRIN_KEEP_PHASE_PREPARING: u8 = 1;
pub const KIRIN_KEEP_PHASE_ARMED: u8 = 2;

#[inline]
fn keep_phase_is_closed(
    phase: u8,
    expected_record_generation: u64,
    observed_record_generation: u64,
    recording: bool,
) -> bool {
    phase == KIRIN_KEEP_PHASE_ARMED
        && expected_record_generation != 0
        && observed_record_generation >= expected_record_generation
        && !recording
}

#[cfg(test)]
mod keep_phase_contract_tests {
    use super::*;

    #[test]
    fn armed_survives_ui_poll_before_first_record_ack() {
        assert!(!keep_phase_is_closed(KIRIN_KEEP_PHASE_ARMED, 4, 3, false));
    }

    #[test]
    fn armed_survives_while_its_exact_record_generation_is_open() {
        assert!(!keep_phase_is_closed(KIRIN_KEEP_PHASE_ARMED, 4, 4, true));
    }

    #[test]
    fn armed_retires_only_after_its_exact_record_generation_closed() {
        assert!(keep_phase_is_closed(KIRIN_KEEP_PHASE_ARMED, 4, 4, false));
        assert!(keep_phase_is_closed(KIRIN_KEEP_PHASE_ARMED, 4, 5, false));
        assert!(!keep_phase_is_closed(
            KIRIN_KEEP_PHASE_PREPARING,
            4,
            4,
            false
        ));
    }

    #[test]
    fn stop_closes_record_but_preserves_exact_pre_for_post_stop_drop() {
        let record_sm = RecordStateMachine::new();
        record_sm.try_enter_record(License::Os).unwrap();
        let paired_pre_target = Mutex::new(Some("pre-exact".to_string()));

        resolve_and_exit_stop(
            &record_sm,
            &paired_pre_target,
            "",
            "",
            Some(ReleaseReason::ManualStop),
        );

        assert!(!record_sm.is_recording());
        assert_eq!(
            paired_pre_target.lock().unwrap().as_deref(),
            Some("pre-exact"),
            "closed-session Drop polling must retain its deterministic PRE address"
        );
    }
}

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

/// 旧 nih-plug VST3 state を JUCE shell へ移すための固定長 DTO。
/// state restore の message thread でのみ使い、Audio Thread には到達しない。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinLegacyNihState {
    pub instance_id: [c_char; ID_BUF_LEN],
    pub project_uuid: [c_char; ID_BUF_LEN],
    pub daw_session_uuid: [c_char; ID_BUF_LEN],
    pub name: [c_char; ID_BUF_LEN],
    pub pair_pre_name: [c_char; ID_BUF_LEN],
}

impl Default for KirinLegacyNihState {
    fn default() -> Self {
        Self {
            instance_id: [0; ID_BUF_LEN],
            project_uuid: [0; ID_BUF_LEN],
            daw_session_uuid: [0; ID_BUF_LEN],
            name: [0; ID_BUF_LEN],
            pair_pre_name: [0; ID_BUF_LEN],
        }
    }
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

fn legacy_nih_field(fields: &serde_json::Map<String, Value>, key: &str) -> Option<String> {
    let encoded = fields.get(key)?.as_str()?;
    serde_json::from_str::<String>(encoded).ok()
}

fn decode_legacy_nih_state_bytes(data: &[u8]) -> Option<KirinLegacyNihState> {
    // Old shipped nih-plug did not enable its optional zstd feature. Keep this decoder deliberately
    // narrow: accepting arbitrary compressed/enveloped data would turn state restore into a second
    // format-discovery system. JUCE XML remains the sole current writer.
    const MAX_LEGACY_STATE_BYTES: usize = 1024 * 1024;
    if data.is_empty() || data.len() > MAX_LEGACY_STATE_BYTES {
        return None;
    }
    let root: Value = serde_json::from_slice(data).ok()?;
    let fields = root.get("fields")?.as_object()?;
    let instance_id = legacy_nih_field(fields, "instance_id");
    let project_uuid = legacy_nih_field(fields, "project_uuid");
    let daw_session_uuid = legacy_nih_field(fields, "daw_session_uuid");
    let name = legacy_nih_field(fields, "name");
    let pair_pre_name = legacy_nih_field(fields, "pair_pre_name");
    if instance_id.is_none()
        && project_uuid.is_none()
        && daw_session_uuid.is_none()
        && name.is_none()
        && pair_pre_name.is_none()
    {
        return None;
    }

    let mut out = KirinLegacyNihState::default();
    write_c_buf(
        &mut out.instance_id,
        instance_id.as_deref().unwrap_or_default(),
    );
    write_c_buf(
        &mut out.project_uuid,
        project_uuid.as_deref().unwrap_or_default(),
    );
    write_c_buf(
        &mut out.daw_session_uuid,
        daw_session_uuid.as_deref().unwrap_or_default(),
    );
    write_c_buf(&mut out.name, name.as_deref().unwrap_or_default());
    write_c_buf(
        &mut out.pair_pre_name,
        pair_pre_name.as_deref().unwrap_or_default(),
    );
    Some(out)
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

#[derive(Debug, Clone, Copy)]
struct PendingCaptureWindow {
    position_valid: bool,
    position_samples: i64,
    num_frames: u64,
    clock_source: CaptureClockSource,
    presentation_latency: PresentationLatencySamples,
    force_new_epoch: bool,
}

#[derive(Debug, Clone, Copy)]
struct PendingRecordBlock {
    recording: bool,
    rendered: bool,
    playing: bool,
    offline: bool,
    position_valid: bool,
    position_samples: i64,
    num_frames: u64,
    clock_start_samples: i64,
    clock_end_samples: Option<i64>,
}

#[inline]
fn spectrum_presentation_start(clock: PendingCaptureWindow) -> Option<i64> {
    if !clock.position_valid
        || !matches!(
            clock.presentation_latency.source,
            PresentationLatencySource::Vst3 | PresentationLatencySource::AudioUnitV2
        )
    {
        return None;
    }
    clock
        .presentation_latency
        .output
        .and_then(|latency| clock.position_samples.checked_add(i64::from(latency)))
}

/// RT 計測ランタイムのハンドル。C ABI からは不透明ポインタ。
pub struct KirinHyphaEngine {
    /// Audio Thread → Measure Thread の rtrb Producer 所有権。
    /// Audio Thread は atomic pointer の読み/差し替えだけを行い、旧 Producer の
    /// deallocation は Watchdog Thread へ返す（R-12: lock / alloc / free なし）。
    producer_handoff: Arc<WatchProducerHandoff>,
    /// Dedicated bounded Record producer, preallocated by the IO/control plane before entry.
    record_ingress: Arc<RecordIngress>,
    /// Measure Thread が 100ms cadence で更新、UI Thread が読む。
    measure_result: Arc<Mutex<MeasureResult>>,
    /// POST の Δ 結果（B-060 3d-a）。POST io_thread の run_tick が select_target_pre で
    /// 選んだ PRE との差分を書き、`poll_delta` が読む（GUI 表示用）。PRE では未更新。
    delta_result: Arc<Mutex<DeltaResult>>,
    /// Optional POST-requested Spectrum path. The bounded SPSC producer is always allocated at
    /// prepare time, but its worker remains absent and its audio ingress returns after one atomic
    /// read until the POST Spectrum page is visible (or an exact PRE is serving that request).
    spectrum_runtime: Arc<SpectrumRuntime>,
    /// Exact-pair request/snapshot coordination. All filesystem work runs on the existing IO
    /// worker; the UI only changes visibility and polls the latest immutable view.
    spectrum: Arc<SpectrumCoordinator>,
    /// Record 中、Measure Thread が毎ループ `engine.finalize()` を書き込む
    /// （measure_thread.rs:290-295）。Watch では未更新（Record→Watch で直近値を保持）。
    session_summary: Arc<Mutex<Option<SessionSummary>>>,
    /// Offline bounce 用 TRACE queue（Measure → IO）。
    record_trace_queue: RecordTraceQueue,
    /// Audio Thread が積む実レンダー長。Record close 時に bounce_take の正本になる。
    record_take_tracker: Arc<RecordTakeTracker>,
    /// One Audio-callback-local clock descriptor staged by
    /// `kirin_hypha_note_capture_window` and consumed by the immediately following
    /// `push_samples`. The descriptor is committed to `RecordTakeTracker` only after the
    /// destination SPSC proves it has room for the complete block. This keeps the immutable
    /// sample clock and accepted audio cardinality inseparable when a lane is full.
    pending_capture_version: AtomicU64,
    pending_capture_valid: AtomicBool,
    pending_position_valid: AtomicBool,
    pending_position_samples: AtomicI64,
    pending_num_frames: AtomicU64,
    pending_clock_source: AtomicU8,
    pending_presentation_source: AtomicU8,
    pending_input_presentation_samples: AtomicU64,
    pending_output_presentation_samples: AtomicU64,
    pending_force_new_epoch: AtomicBool,
    /// Record-take facts are staged by the JUCE callback beside the capture descriptor. A
    /// rendered block becomes visible to the immutable take selector only after `push_samples`
    /// admits the complete clock+audio transaction.
    pending_record_valid: AtomicBool,
    pending_recording: AtomicBool,
    pending_record_rendered: AtomicBool,
    pending_record_playing: AtomicBool,
    pending_record_offline: AtomicBool,
    pending_record_position_valid: AtomicBool,
    pending_record_position_samples: AtomicI64,
    pending_record_num_frames: AtomicU64,
    pending_record_clock_start_samples: AtomicI64,
    pending_record_clock_end_valid: AtomicBool,
    pending_record_clock_end_samples: AtomicI64,
    /// Last fully admitted callback's host render mode. The FFI, rather than the JUCE wrapper,
    /// owns this edge so a Watch callback can establish the offline epoch before Keep ACK.
    capture_last_offline: AtomicBool,
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
    /// User-facing Keep control state. Record capture may enter before all 1–12 members are ready;
    /// only `Armed` authorizes the user to start an offline bounce.
    keep_phase: Arc<AtomicU8>,
    keep_phase_generation_started_at_ms: Arc<AtomicI64>,
    /// State-machine generation reserved by the current Keep. ARMED may precede the first ACK,
    /// therefore a UI read can retire it only after this exact generation entered and closed.
    keep_record_generation: Arc<AtomicU64>,
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
    /// B-118: Measure Thread 生存フラグ（watchdog が crash で false / 復帰で true）。measure_alive() が読む。
    measure_alive: Arc<AtomicBool>,
    /// Durable worker generation for restart observability; the initial Measure worker is one.
    measure_worker_generation: Arc<AtomicU64>,
    /// B-118: watchdog 自身の停止フラグ（Drop でセット）。
    watchdog_shutdown: Arc<AtomicBool>,
    /// B-118: watchdog Thread の JoinHandle（Drop で shutdown→join / 内部で io→measure を join）。
    watchdog_handle: Mutex<Option<JoinHandle<()>>>,
    /// B-118 Phase 3 (③): io_thread 連続失敗時の固定文言（RecordError::ui_message / G-115-29）。
    /// enable_*_writes で io と Arc 共有し、`record_error_message()` getter（JUCE status label）が読む。
    record_error_message: Arc<RwLock<Option<String>>>,
    /// Direct user-action feedback is a consumable edge, never persistent producer state.
    /// Keep conflicts, capacity limits, and pair-readiness failures are drained once by the shell
    /// and rendered as a bounded toast. Only real IO/producer faults use `record_error_message`.
    keep_action_notice: Arc<RwLock<Option<String>>>,
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
    /// POST engine-lifetime pair authority. Worker restarts receive clones of this same lease.
    pair_owner: Arc<PairOwnershipLease>,
    /// POST GUI/ABI status only; unknown observations retain the last coherent state.
    last_post_pair_status: Mutex<Option<PairStatus>>,
    /// PRE 表示専用。所有 marker が生きている間、claim index の置換競合を表示へ漏らさない。
    /// Audio/Measure/Record/TRACE 経路からは参照しない。
    pre_pair_status: PrePairStatusObserver,
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

// SAFETY: `producer_handoff` の active Producer mutable access は push_samples からのみ行い、
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

fn restored_pair_latch(
    kirin_root: &Path,
    project_hash: &str,
    daw_session_id: &str,
    pair_name: &str,
    pre_instance_id: &str,
    host_process_id: u32,
) -> Option<LatchedPre> {
    if !kirin_measure::is_path_safe_component(project_hash)
        || !kirin_measure::is_path_safe_component(pre_instance_id)
    {
        return None;
    }
    let project_dir = kirin_root.join(project_hash);
    Some(LatchedPre {
        name: pair_name.to_string(),
        instance_id: pre_instance_id.to_string(),
        pre_json: project_dir.join(pre_instance_id).join("pre.json"),
        project_dir,
        daw_session_id: (!daw_session_id.is_empty()).then(|| daw_session_id.to_string()),
        host_process_id: (host_process_id != 0).then_some(host_process_id),
        readiness: kirin_measure::LatchedPreReadiness::RestoredWaiting,
    })
}

#[cfg(test)]
mod restored_pair_latch_tests {
    use super::restored_pair_latch;
    use std::path::Path;

    #[test]
    fn saved_exact_pre_reconstructs_one_fixed_waiting_path_without_discovery() {
        let latch = restored_pair_latch(
            Path::new("/tmp/kirin"),
            "project-a",
            "daw-a",
            "2Mix",
            "pre-a",
            42,
        )
        .expect("safe saved pair");
        assert_eq!(latch.name, "2Mix");
        assert_eq!(latch.instance_id, "pre-a");
        assert_eq!(latch.project_dir, Path::new("/tmp/kirin/project-a"));
        assert_eq!(
            latch.pre_json,
            Path::new("/tmp/kirin/project-a/pre-a/pre.json")
        );
        assert_eq!(latch.daw_session_id.as_deref(), Some("daw-a"));
        assert_eq!(latch.host_process_id, Some(42));
        assert_eq!(
            latch.readiness,
            kirin_measure::LatchedPreReadiness::RestoredWaiting
        );
    }

    #[test]
    fn saved_exact_pre_accepts_unnamed_pair_but_rejects_unsafe_path_components() {
        assert!(restored_pair_latch(
            Path::new("/tmp/kirin"),
            "project-a",
            "daw-a",
            "",
            "pre-a",
            42,
        )
        .is_some());
        assert!(restored_pair_latch(
            Path::new("/tmp/kirin"),
            "../escape",
            "daw-a",
            "2Mix",
            "pre-a",
            42,
        )
        .is_none());
        assert!(restored_pair_latch(
            Path::new("/tmp/kirin"),
            "project-a",
            "daw-a",
            "2Mix",
            "../../pre",
            42,
        )
        .is_none());
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
/// Drop は generation archive の immutable rosterだけを受理して今回WAVを結び付ける。
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
    // Persistent IO/producer faults only. Direct Keep conflicts and capacity/readiness feedback
    // use the one-shot action channel below (R-28 without stale UI state).
    record_error_message: &Arc<RwLock<Option<String>>>,
    keep_action_notice: &Arc<RwLock<Option<String>>>,
    keep_phase: &Arc<AtomicU8>,
    keep_phase_generation_started_at_ms: &Arc<AtomicI64>,
    keep_record_generation: &Arc<AtomicU64>,
    started_at_position_samples: Option<i64>,
    capture_generation: Option<&CaptureGeneration>,
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
            Ok(reservation::ReserveOutcome::PreInUse) => {
                if let Ok(mut g) = keep_action_notice.write() {
                    *g = Some("PRE already in use".to_string());
                }
                return false;
            }
            // G-115-365 (3): 枠が取れない（write_all 失敗等の Err / 不完全枠は内部で unlink 済）= reject。
            // 枠なしで keep に入らない。
            Err(_) => {
                if let Ok(mut g) = keep_action_notice.write() {
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
        if let Ok(mut g) = keep_action_notice.write() {
            *g = Some("Maximum 12 pairs reached".to_string());
        }
        return false;
    }
    if let Some(generation) = capture_generation {
        let Some(generation_member) = generation.member(project_hash, post_iid) else {
            if reservation_created {
                reservation::release_pairing(&base, project_hash, &target, post_iid);
            }
            return false;
        };
        if !generation_member.pre_instance_id.is_empty()
            && generation_member.pre_instance_id != target
        {
            if reservation_created {
                reservation::release_pairing(&base, project_hash, &target, post_iid);
            }
            return false;
        }
    }
    // A single Keep owns the same two-phase generation contract as All Keep. External
    // generations are already staged by the All Keep originator and are committed only after all
    // members report exact PRE+POST writer ownership.
    let owned_generation = capture_generation.is_none().then(|| {
        CaptureGeneration::new_single_named(
            project_hash.to_string(),
            post_iid.to_string(),
            target.clone(),
            daw.to_string(),
            current_host_process_id(),
            Some(pair.clone()),
        )
    });
    let generation = match capture_generation.or(owned_generation.as_ref()) {
        Some(generation) => generation,
        None => {
            if reservation_created {
                reservation::release_pairing(&base, project_hash, &target, post_iid);
            }
            if let Ok(mut message) = record_error_message.write() {
                *message = Some("Failed to start record".to_string());
            }
            return false;
        }
    };
    let mut owned_transaction = if owned_generation.is_some() {
        let mut transaction = match CaptureGenerationTransaction::begin(&base, generation) {
            Ok(transaction) => transaction,
            Err(error) => {
                if reservation_created {
                    reservation::release_pairing(&base, project_hash, &target, post_iid);
                }
                if matches!(
                    error,
                    kirin_measure::CaptureGenerationError::Io(ref error)
                        if error.kind() == std::io::ErrorKind::WouldBlock
                ) {
                    if let Ok(mut message) = keep_action_notice.write() {
                        *message = Some("Another Keep is active".to_string());
                    }
                } else if let Ok(mut message) = record_error_message.write() {
                    *message = Some("Failed to start record".to_string());
                }
                return false;
            }
        };
        if transaction.stage().is_err() {
            if reservation_created {
                reservation::release_pairing(&base, project_hash, &target, post_iid);
            }
            if let Ok(mut message) = record_error_message.write() {
                *message = Some("Failed to start record".to_string());
            }
            return false;
        }
        Some(transaction)
    } else {
        None
    };
    keep_phase_generation_started_at_ms.store(generation.started_at_ms, Ordering::Release);
    keep_record_generation.store(record_sm.generation().saturating_add(1), Ordering::Release);
    keep_phase.store(KIRIN_KEEP_PHASE_PREPARING, Ordering::Release);
    // target_pre_instance_id = 選定 PRE。PRE が自宛て signal を発見し ack する。
    let write_result = write_pending_claiming_expected_and_clock_for_generation(
        &base,
        project_hash,
        post_iid,
        target.clone(),
        daw.to_string(),
        started_at_position_samples,
        generation,
    );
    if write_result.is_ok() {
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
                instance_id: target.clone(),
                project_dir: sel.project_dir,
                pre_json: sel.pre_json,
                daw_session_id: sel.daw_session_id,
                host_process_id: sel.host_process_id,
                readiness: kirin_measure::LatchedPreReadiness::Confirmed,
            });
        }
        if let Some(transaction) = owned_transaction.take() {
            let action_notice = Arc::clone(keep_action_notice);
            let phase = Arc::clone(keep_phase);
            let phase_generation = Arc::clone(keep_phase_generation_started_at_ms);
            let generation_started_at_ms = generation.started_at_ms;
            if transaction
                .commit_when_ready_async(CAPTURE_PRODUCER_READY_TIMEOUT, move |result| {
                    if phase_generation.load(Ordering::Acquire) != generation_started_at_ms {
                        return;
                    }
                    if result.is_ok() {
                        phase.store(KIRIN_KEEP_PHASE_ARMED, Ordering::Release);
                    } else {
                        phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
                        if let Ok(mut message) = action_notice.write() {
                            *message = Some("Failed to arm PRE and POST".to_string());
                        }
                    }
                })
                .is_err()
            {
                keep_phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
                if let Ok(mut message) = keep_action_notice.write() {
                    *message = Some("Failed to arm PRE and POST".to_string());
                }
                return false;
            }
        } else if !spawn_keep_phase_observer(
            base.clone(),
            generation.capture_generation_id.clone(),
            generation.started_at_ms,
            Arc::clone(keep_phase),
            Arc::clone(keep_phase_generation_started_at_ms),
        ) {
            keep_phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
            if let Ok(mut message) = keep_action_notice.write() {
                *message = Some("Failed to arm PRE and POST".to_string());
            }
            return false;
        }
        true
    } else {
        if matches!(
            write_result,
            Err(SignalError::Io(ref error))
                if error.kind() == std::io::ErrorKind::WouldBlock
        ) {
            if let Ok(mut g) = keep_action_notice.write() {
                *g = Some("Another Keep is active".to_string());
            }
        } else if let Ok(mut g) = record_error_message.write() {
            *g = Some("Failed to start record".to_string());
        }
        // write_pending 失敗時も予約枠を戻す（自分が作った場合のみ）。
        if reservation_created {
            reservation::release_pairing(&base, project_hash, &target, post_iid);
        }
        if let Ok(mut g) = paired_pre_target.lock() {
            *g = None;
        }
        keep_phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
        keep_record_generation.store(0, Ordering::Release);
        false
    }
}

fn spawn_keep_phase_observer(
    base: std::path::PathBuf,
    capture_generation_id: String,
    generation_started_at_ms: i64,
    phase: Arc<AtomicU8>,
    phase_generation: Arc<AtomicI64>,
) -> bool {
    std::thread::Builder::new()
        .name("hypha-arm-observer".to_string())
        .spawn(move || {
            let deadline = std::time::Instant::now() + CAPTURE_PRODUCER_READY_TIMEOUT;
            loop {
                if phase_generation.load(Ordering::Acquire) != generation_started_at_ms {
                    return;
                }
                if kirin_measure::read_active_generation(&base)
                    .ok()
                    .flatten()
                    .is_some_and(|active| {
                        active.capture_generation_id == capture_generation_id
                            && active.started_at_ms == generation_started_at_ms
                    })
                {
                    phase.store(KIRIN_KEEP_PHASE_ARMED, Ordering::Release);
                    return;
                }
                if kirin_measure::read_generation_terminal(
                    &base,
                    &capture_generation_id,
                    generation_started_at_ms,
                )
                .ok()
                .flatten()
                .is_some()
                    || std::time::Instant::now() >= deadline
                {
                    phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
                    return;
                }
                std::thread::sleep(Duration::from_millis(10));
            }
        })
        .is_ok()
}

/// Stop resolution shared by the direct control and All Stop receiver.
///
/// Record lifecycle, reservation ownership and pair selection are deliberately separate. Stop
/// closes the current session and releases its reservation, but the exact PRE selection remains
/// available to the stopped-session Drop reconciler. Only an explicit selector transition/unpair
/// clears `paired_pre_target` through `PairBinding`.
fn resolve_and_exit_stop(
    record_sm: &RecordStateMachine,
    paired_pre_target: &Mutex<Option<String>>,
    project_hash: &str,
    post_iid: &str,
    release_reason: Option<ReleaseReason>,
) {
    // Capture the selected PRE without consuming it. The reservation is Record-scoped, while this
    // identity is also the deterministic address for a WAV dropped after Stop.
    let released_pre = paired_pre_target.lock().ok().and_then(|g| g.clone());
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
        let owned_session = record_sm
            .record_session_id()
            .or_else(|| record_sm.last_closed_session_id());
        if let Some(current) = read_signal(&base, project_hash, post_iid) {
            // Explicit user/broadcast Stop owns the currently addressed member, including the
            // Pending-before-ACK interval. Teardown without a Stop reason owns only the exact
            // session this engine entered or closed; a recreated engine may already have replaced
            // the canonical path with its next Keep. An ownership mismatch must be a no-op, never
            // an unconditional fallback release of the newer incarnation.
            let _ = match release_reason {
                Some(reason) => mark_released_with_reason_if_current(
                    &base,
                    project_hash,
                    post_iid,
                    &current,
                    reason,
                ),
                None if owned_session.as_deref() == Some(current.session_id.as_str()) => {
                    mark_released_if_current(&base, project_hash, post_iid, &current)
                }
                None => Ok(false),
            };
        }
    }
    record_sm.exit_record();
}

fn clear_keep_action_notice(keep_action_notice: &RwLock<Option<String>>) {
    if let Ok(mut message) = keep_action_notice.write() {
        *message = None;
    }
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
        let capacity = watch_ring_capacity_samples(num_channels);
        let (producer, consumer) = rtrb::RingBuffer::new(capacity);
        let producer_handoff = Arc::new(WatchProducerHandoff::new(producer));
        let record_ingress = Arc::new(RecordIngress::new(record_ring_capacity_samples(
            num_channels,
        )));

        let measure_result = Arc::new(Mutex::new(MeasureResult::default()));
        let delta_result = Arc::new(Mutex::new(DeltaResult::default()));
        let spectrum_runtime = SpectrumRuntime::new(sample_rate, num_channels);
        let spectrum = SpectrumCoordinator::new(sample_rate, Arc::clone(&spectrum_runtime));
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
        let keep_phase = Arc::new(AtomicU8::new(KIRIN_KEEP_PHASE_IDLE));
        let keep_phase_generation_started_at_ms = Arc::new(AtomicI64::new(0));
        let keep_record_generation = Arc::new(AtomicU64::new(0));

        // B-118: watchdog（Lazy）と共有する slot / フラグ群。
        let io_thread: Arc<Mutex<Option<IoThreadHandle>>> = Arc::new(Mutex::new(None));
        let io_restart_slot: Arc<Mutex<Option<RestartIoFn>>> = Arc::new(Mutex::new(None));
        let measure_alive = Arc::new(AtomicBool::new(true));
        let measure_worker_generation = Arc::new(AtomicU64::new(1));
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
            Arc::clone(&record_ingress),
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
            measure_worker_generation: Arc::clone(&measure_worker_generation),
            producer_handoff: Arc::clone(&producer_handoff),
            record_ingress: Arc::clone(&record_ingress),
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
            producer_handoff,
            record_ingress,
            measure_result,
            delta_result,
            spectrum_runtime,
            spectrum,
            session_summary,
            record_trace_queue,
            record_take_tracker,
            pending_capture_version: AtomicU64::new(0),
            pending_capture_valid: AtomicBool::new(false),
            pending_position_valid: AtomicBool::new(false),
            pending_position_samples: AtomicI64::new(i64::MIN),
            pending_num_frames: AtomicU64::new(0),
            pending_clock_source: AtomicU8::new(CaptureClockSource::Unknown as u8),
            pending_presentation_source: AtomicU8::new(PresentationLatencySource::Unknown as u8),
            pending_input_presentation_samples: AtomicU64::new(u64::MAX),
            pending_output_presentation_samples: AtomicU64::new(u64::MAX),
            pending_force_new_epoch: AtomicBool::new(false),
            pending_record_valid: AtomicBool::new(false),
            pending_recording: AtomicBool::new(false),
            pending_record_rendered: AtomicBool::new(false),
            pending_record_playing: AtomicBool::new(false),
            pending_record_offline: AtomicBool::new(false),
            pending_record_position_valid: AtomicBool::new(false),
            pending_record_position_samples: AtomicI64::new(i64::MIN),
            pending_record_num_frames: AtomicU64::new(0),
            pending_record_clock_start_samples: AtomicI64::new(0),
            pending_record_clock_end_valid: AtomicBool::new(false),
            pending_record_clock_end_samples: AtomicI64::new(0),
            capture_last_offline: AtomicBool::new(false),
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
            keep_phase,
            keep_phase_generation_started_at_ms,
            keep_record_generation,
            latest_position_valid: Arc::new(AtomicBool::new(false)),
            latest_position_samples: Arc::new(AtomicI64::new(i64::MIN)),
            // 既定 Unknown（set_license(Os) されるまで Record 不可・安全側）。
            license: LiveLicense::new(License::Unknown),
            sample_rate,
            num_channels,
            io_thread,
            io_restart_slot,
            measure_alive,
            measure_worker_generation,
            watchdog_shutdown,
            watchdog_handle: Mutex::new(Some(watchdog_handle)),
            record_error_message: Arc::new(RwLock::new(None)),
            keep_action_notice: Arc::new(RwLock::new(None)),
            identity: Mutex::new(IdentityState::default()),
            project_hash_cell: Arc::new(RwLock::new(String::new())),
            daw_session_id_cell: Arc::new(RwLock::new(String::new())),
            pair_binding: Arc::new(PairBinding::new()),
            pair_owner: Arc::new(PairOwnershipLease::new()),
            last_post_pair_status: Mutex::new(None),
            pre_pair_status: PrePairStatusObserver::new(),
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
        let next_generation = self.record_sm.generation().saturating_add(1);
        if !self.record_ingress.prepare_for_generation(next_generation) {
            return false;
        }
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

    pub fn keep_phase(&self) -> u8 {
        let phase = self.keep_phase.load(Ordering::Acquire);
        let expected_record_generation = self.keep_record_generation.load(Ordering::Acquire);
        if keep_phase_is_closed(
            phase,
            expected_record_generation,
            self.record_sm.generation(),
            self.record_sm.is_recording(),
        ) {
            self.keep_phase_generation_started_at_ms
                .store(0, Ordering::Release);
            self.keep_record_generation.store(0, Ordering::Release);
            self.keep_phase
                .store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
            KIRIN_KEEP_PHASE_IDLE
        } else {
            phase
        }
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

    /// Stage one JUCE callback's take facts. The single Audio Thread consumes this immediately in
    /// `push_samples`; atomics keep `KirinHyphaEngine: Sync` truthful without introducing a lock.
    fn stage_record_block(&self, block: RecordTakeBlock) {
        self.pending_record_valid.store(false, Ordering::Relaxed);
        self.pending_recording
            .store(block.recording, Ordering::Relaxed);
        self.pending_record_rendered
            .store(block.rendered, Ordering::Relaxed);
        self.pending_record_playing
            .store(block.playing, Ordering::Relaxed);
        self.pending_record_offline
            .store(block.offline, Ordering::Relaxed);
        self.pending_record_position_valid
            .store(block.position_valid, Ordering::Relaxed);
        self.pending_record_position_samples
            .store(block.position_samples, Ordering::Relaxed);
        self.pending_record_num_frames
            .store(block.num_frames, Ordering::Relaxed);
        self.pending_record_clock_start_samples
            .store(block.clock_start_samples, Ordering::Relaxed);
        self.pending_record_clock_end_valid
            .store(block.clock_end_samples.is_some(), Ordering::Relaxed);
        self.pending_record_clock_end_samples.store(
            block.clock_end_samples.unwrap_or_default(),
            Ordering::Relaxed,
        );
        self.pending_record_valid.store(true, Ordering::Release);
    }

    #[inline]
    fn take_pending_record_block(&self, expected_frames: u64) -> Option<PendingRecordBlock> {
        if !self.pending_record_valid.swap(false, Ordering::AcqRel) {
            return None;
        }
        let block = PendingRecordBlock {
            recording: self.pending_recording.load(Ordering::Relaxed),
            rendered: self.pending_record_rendered.load(Ordering::Relaxed),
            playing: self.pending_record_playing.load(Ordering::Relaxed),
            offline: self.pending_record_offline.load(Ordering::Relaxed),
            position_valid: self.pending_record_position_valid.load(Ordering::Relaxed),
            position_samples: self.pending_record_position_samples.load(Ordering::Relaxed),
            num_frames: self.pending_record_num_frames.load(Ordering::Relaxed),
            clock_start_samples: self
                .pending_record_clock_start_samples
                .load(Ordering::Relaxed),
            clock_end_samples: self
                .pending_record_clock_end_valid
                .load(Ordering::Relaxed)
                .then(|| {
                    self.pending_record_clock_end_samples
                        .load(Ordering::Relaxed)
                }),
        };
        (block.num_frames == expected_frames || (!block.rendered && expected_frames == 0))
            .then_some(block)
    }

    #[inline]
    fn commit_pending_record_block(&self, block: PendingRecordBlock) {
        self.note_record_block(RecordTakeBlock {
            generation: 0,
            recording: block.recording,
            rendered: block.rendered,
            playing: block.playing,
            offline: block.offline,
            position_valid: block.position_valid,
            position_samples: block.position_samples,
            num_frames: block.num_frames,
            clock_start_samples: block.clock_start_samples,
            clock_end_samples: block.clock_end_samples,
        });
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
            false,
        );
    }

    pub fn note_capture_window_with_presentation(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        clock_source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
        force_new_epoch: bool,
    ) {
        // This call deliberately stages facts only. `push_samples` first proves whole-block SPSC
        // capacity, then commits this descriptor and all samples as one producer transaction.
        // Advancing the clock here would let a partially accepted callback permanently shift every
        // later TRACE sample.
        self.pending_capture_version.fetch_add(1, Ordering::AcqRel);
        self.pending_capture_valid.store(false, Ordering::Relaxed);
        self.pending_position_valid
            .store(position_valid, Ordering::Relaxed);
        self.pending_position_samples
            .store(position_samples, Ordering::Relaxed);
        self.pending_num_frames.store(num_frames, Ordering::Relaxed);
        self.pending_clock_source
            .store(clock_source as u8, Ordering::Relaxed);
        self.pending_presentation_source
            .store(presentation_latency.source as u8, Ordering::Relaxed);
        self.pending_input_presentation_samples.store(
            presentation_latency.input.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.pending_output_presentation_samples.store(
            presentation_latency.output.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.pending_force_new_epoch
            .store(force_new_epoch, Ordering::Relaxed);
        self.pending_capture_valid.store(true, Ordering::Relaxed);
        self.pending_capture_version.fetch_add(1, Ordering::Release);
    }

    #[inline]
    fn take_pending_capture_window(&self, expected_frames: u64) -> Option<PendingCaptureWindow> {
        for _ in 0..4 {
            let before = self.pending_capture_version.load(Ordering::Acquire);
            if before & 1 != 0 || !self.pending_capture_valid.load(Ordering::Relaxed) {
                continue;
            }
            let pending = PendingCaptureWindow {
                position_valid: self.pending_position_valid.load(Ordering::Relaxed),
                position_samples: self.pending_position_samples.load(Ordering::Relaxed),
                num_frames: self.pending_num_frames.load(Ordering::Relaxed),
                clock_source: CaptureClockSource::from_abi(
                    self.pending_clock_source.load(Ordering::Relaxed),
                ),
                presentation_latency: PresentationLatencySamples {
                    source: PresentationLatencySource::from_abi(
                        self.pending_presentation_source.load(Ordering::Relaxed),
                    ),
                    input: u32::try_from(
                        self.pending_input_presentation_samples
                            .load(Ordering::Relaxed),
                    )
                    .ok(),
                    output: u32::try_from(
                        self.pending_output_presentation_samples
                            .load(Ordering::Relaxed),
                    )
                    .ok(),
                },
                force_new_epoch: self.pending_force_new_epoch.load(Ordering::Relaxed),
            };
            let after = self.pending_capture_version.load(Ordering::Acquire);
            if before == after && after & 1 == 0 {
                self.pending_capture_valid.store(false, Ordering::Release);
                return (pending.num_frames == expected_frames).then_some(pending);
            }
        }
        self.pending_capture_valid.store(false, Ordering::Release);
        None
    }

    /// Publish one host transport block. Audio Thread only; atomics and the
    /// existing ring-cursor seqlock make this RT-safe.
    pub fn note_transport_block(
        &self,
        playing: bool,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        force_new_pass: bool,
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
        if watch_transport_starts_new_pass(playing, previous_playing, discontinuity, force_new_pass)
        {
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
            let record_ingress = Arc::clone(&self.record_ingress);
            let push_overflow = Arc::clone(&self.push_overflow);
            let oversized_drop = Arc::clone(&self.oversized_drop); // B-125
            let spectrum = Arc::clone(&self.spectrum);
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
                    Arc::clone(&record_ingress),
                    Arc::clone(&push_overflow), // B-076: per-Record dropped_samples
                    Arc::clone(&oversized_drop), // B-125: per-Record oversized block drop
                    Arc::clone(&spectrum),
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
        // Exact pair ownership belongs to the plugin engine, not a restartable IO generation.
        // The restart closure below retains this Arc until the POST engine itself is destroyed.
        let pair_owner = Arc::clone(&self.pair_owner);

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
            let keep_action_notice = Arc::clone(&self.keep_action_notice);
            let keep_phase = Arc::clone(&self.keep_phase);
            let keep_phase_generation_started_at_ms =
                Arc::clone(&self.keep_phase_generation_started_at_ms);
            let keep_record_generation = Arc::clone(&self.keep_record_generation);
            Arc::new(
                move |_originator: &str, _started_at: &str, generation: &CaptureGeneration| {
                    let lic = license.refresh_for_user_action();
                    resolve_and_enter_keep(
                        lic,
                        &record_sm,
                        &pair_target,
                        &paired,
                        &project_hash,
                        &post_iid,
                        &daw,
                        &latched,
                        &record_error_message,
                        &keep_action_notice,
                        &keep_phase,
                        &keep_phase_generation_started_at_ms,
                        &keep_record_generation,
                        None,
                        Some(generation),
                    )
                },
            )
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
            let record_ingress = Arc::clone(&self.record_ingress);
            let record_mark_queue = Arc::clone(&self.record_mark_queue);
            let push_overflow = Arc::clone(&self.push_overflow);
            let oversized_drop = Arc::clone(&self.oversized_drop); // B-125
            let pair_owner = Arc::clone(&pair_owner);
            let latched_pre = self.pair_binding.latched_pre();
            let spectrum = Arc::clone(&self.spectrum);
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
                    Arc::clone(&record_ingress),
                    Arc::clone(&record_mark_queue),
                    Arc::clone(&push_overflow), // B-076: per-Record dropped_samples
                    Arc::clone(&oversized_drop), // B-125: per-Record oversized block drop
                    Arc::clone(&pair_owner),    // exact pair survives IO worker restart
                    Arc::clone(&latched_pre),   // B-108: display/keep 共有ラッチ
                    Arc::clone(&spectrum),
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

    /// POST editor visibility edge. PRE has no public Spectrum page and therefore rejects it.
    /// The call is control-plane only; request-file work is deferred to the next POST IO tick.
    pub fn set_spectrum_visible(&self, visible: bool) -> bool {
        let is_post =
            self.write_role.lock().ok().and_then(|role| *role) == Some(PluginDataRole::Post);
        if !is_post {
            return false;
        }
        self.spectrum.set_post_visible(visible);
        true
    }

    /// Latest POST-minus-PRE Spectrum display snapshot. Lock contention is a silent skipped
    /// presentation tick; it never reaches the audio or measurement paths.
    pub fn poll_spectrum(&self) -> Option<SpectrumViewSnapshot> {
        self.spectrum.try_view()
    }

    /// Read-only performance counters used by regression tests and validation builds.
    pub fn spectrum_stats(&self) -> SpectrumRuntimeStats {
        self.spectrum_runtime.stats()
    }

    /// Keep/Record専用の世代付き表示スナップショット。
    /// 計測・writer・pairingの正本とは独立し、UI Threadから非ブロッキングで読む。
    pub fn poll_record_display(&self) -> Option<RecordDisplaySnapshot> {
        self.record_sm.try_record_display_snapshot()
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
        if self.pair_binding.matches_exact(&name, &latch) {
            return true;
        }
        let post_instance_id = self
            .identity
            .lock()
            .map(|identity| identity.instance_id.clone())
            .unwrap_or_default();
        if kirin_measure::pair_claim_owned_by_other_post(
            &kirin_root,
            instance_id,
            &project_hash,
            &post_instance_id,
        ) {
            if let Ok(mut notice) = self.keep_action_notice.write() {
                *notice = Some("PRE already in use".to_string());
            }
            return false;
        }
        let (project_hash, post_iid) = self.begin_pair_reselection();
        let transition = self.pair_binding.replace_exact(name, latch);
        self.finish_pair_reselection(transition, &project_hash, &post_iid, epoch_secs_now());
        true
    }

    /// Restore one exact PRE selected in a saved DAW document without scanning the live registry.
    /// The fixed path may not exist yet because hosts restore plugin instances in arbitrary order;
    /// the latch remains Waiting and becomes Paired as soon as that PRE publishes at the same path.
    pub fn restore_pair_candidate(&self, pre_project_hash: &str, instance_id: &str) -> bool {
        let desired_name = self
            .pair_binding
            .desired_name()
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .clone();
        let daw_session_id = self
            .identity
            .lock()
            .map(|identity| identity.daw_session_uuid.clone())
            .unwrap_or_default();
        let Some(latch) = restored_pair_latch(
            &PlatformPaths::current_kirin_tmp_root(),
            pre_project_hash,
            &daw_session_id,
            &desired_name,
            instance_id,
            current_host_process_id(),
        ) else {
            return false;
        };
        if self.pair_binding.matches_exact(&desired_name, &latch) {
            return true;
        }

        let (project_hash, post_iid) = self.begin_pair_reselection();
        let transition = self.pair_binding.replace_exact(desired_name, latch);
        self.finish_pair_reselection(transition, &project_hash, &post_iid, epoch_secs_now());
        true
    }

    pub fn pair_status(&self) -> PairStatus {
        let role = self.write_role.lock().ok().and_then(|role| *role);
        match role {
            Some(PluginDataRole::Post) => {
                let (project_hash, post_instance_id) = self
                    .identity
                    .lock()
                    .map(|identity| (identity.project_hash.clone(), identity.instance_id.clone()))
                    .unwrap_or_default();
                let pair_claimed_at = self
                    .pair_claimed_at
                    .read()
                    .map(|value| *value)
                    .unwrap_or(0.0);
                let kirin_root = PlatformPaths::current_kirin_tmp_root();
                let observed = self.pair_owner.observe_binding_if_stable(|| {
                    let (selection_intent, pre_instance_id) = self.pair_binding.status_snapshot();
                    let has_exact_binding = pre_instance_id.is_some();
                    let binding = pre_instance_id.and_then(|pre_instance_id| {
                        pair_owner_instance_dir(&kirin_root, &project_hash, &post_instance_id).map(
                            |instance_dir| {
                                PairOwnershipBinding::new(
                                    instance_dir,
                                    pre_instance_id,
                                    pair_claimed_at,
                                )
                            },
                        )
                    });
                    ((selection_intent, has_exact_binding), binding)
                });
                let observed = observed.map(
                    |((selection_intent, has_exact_binding), owns_exact_marker)| {
                        pair_status_from_owned_binding_with_intent(
                            selection_intent,
                            has_exact_binding,
                            owns_exact_marker,
                        )
                    },
                );
                let mut last_known = self
                    .last_post_pair_status
                    .lock()
                    .unwrap_or_else(|poisoned| poisoned.into_inner());
                let status = pair_status_or_last_known(observed, *last_known);
                *last_known = Some(status);
                status
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
                pair_status_for_pre(
                    &self.pre_pair_status,
                    &PlatformPaths::current_kirin_tmp_root(),
                    &iid,
                    &name,
                )
            }
            None => PairStatus::Unpaired,
        }
    }

    pub fn paired_pre_instance_id(&self) -> Option<String> {
        paired_pre_instance_id(&self.pair_binding.latched_pre())
    }

    pub fn paired_pre_locator(&self) -> Option<(String, String)> {
        let binding = self.pair_binding.latched_pre();
        let binding = binding
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let pre = binding.as_ref()?;
        let project_hash = pre.project_dir.file_name()?.to_str()?;
        if !kirin_measure::is_path_safe_component(project_hash)
            || !kirin_measure::is_path_safe_component(&pre.instance_id)
        {
            return None;
        }
        Some((project_hash.to_string(), pre.instance_id.clone()))
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

    /// Durable Measure worker generation used by restart acceptance tests and diagnostics.
    #[doc(hidden)]
    pub fn __measure_worker_generation_for_test(&self) -> u64 {
        self.measure_worker_generation.load(Ordering::Acquire)
    }

    /// テスト専用: Measure Thread を強制終了させ watchdog の再起動経路を駆動する（B-118 test iii/iv）。
    /// `shutdown` をセットすると measure loop が抜けて exit → watchdog が is_finished を検出し
    /// （watchdog_shutdown は false のため）再 spawn して shutdown を false へ戻す。本番経路は使わない。
    #[doc(hidden)]
    pub fn __force_measure_restart_for_test(&self) {
        self.shutdown.store(true, Ordering::Relaxed);
    }

    /// Compatibility advisory for older shells. This getter is deliberately memory-only: UI
    /// polling must never enumerate reservation files. The authoritative Keep attempt publishes
    /// a one-shot action result when it cannot obtain the 13th slot.
    pub fn record_exclusion_conflict(&self) -> bool {
        self.keep_action_notice
            .read()
            .ok()
            .and_then(|message| message.clone())
            .as_deref()
            == Some("Maximum 12 pairs reached")
    }

    /// B-118 Phase 3 (③): io_thread 連続失敗時の固定文言（RecordError::ui_message / G-115-29）。
    /// None=通常（R-26 沈黙）。JUCE 永続 status label が Some の間表示する。
    pub fn record_error_message(&self) -> Option<String> {
        self.record_error_message
            .read()
            .ok()
            .and_then(|g| g.clone())
    }

    /// Drain exactly one direct user-action result. This channel has edge semantics: polling it
    /// cannot recreate Keep authority and a consumed notice never becomes persistent UI state.
    pub fn drain_keep_action_notice(&self) -> Option<String> {
        self.keep_action_notice
            .write()
            .ok()
            .and_then(|mut notice| notice.take())
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

    /// POST「Keep」: 厳格選定（select_target_pre）で対 PRE を一意決定し record_signal(pending)
    /// を書く（B-061 3d-b）。PRE 側 io_thread が autonomous に discover→ack する。
    /// `License::Os` かつ一意 PRE のとき `true`。選定 None（空名/不在/曖昧/Bypassed/古t）/
    /// 非 Os / AlreadyRecording は `false`（write_pending しない）。
    pub fn keep(&self) -> bool {
        self.keep_with_optional_generation(None)
    }

    fn keep_with_generation(&self, generation: &CaptureGeneration) -> bool {
        self.keep_with_optional_generation(Some(generation))
    }

    fn keep_with_optional_generation(&self, generation: Option<&CaptureGeneration>) -> bool {
        if generation.is_none() {
            clear_keep_action_notice(&self.keep_action_notice);
        }
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
        let license = self.license.refresh_for_user_action();
        resolve_and_enter_keep(
            license,
            &self.record_sm,
            &pair_target,
            &paired_pre_target,
            &project_hash,
            &post_iid,
            &daw,
            &latched_pre, // B-108: ラッチ済みならラッチ先を直接 target に使う
            &self.record_error_message, // persistent producer/I/O fault channel
            &self.keep_action_notice,
            &self.keep_phase,
            &self.keep_phase_generation_started_at_ms,
            &self.keep_record_generation,
            None,
            generation,
        )
    }

    /// POST「All Keep」: all_keep broadcast を書いてから自身の keep を発火する（B-102 /
    /// egui ComboBox 先頭行と同一ライフサイクル: broadcast → self keep）。broadcast の棚パス
    /// は、厳格DAW scopeに加えて同一host内で明示的に見えるexact PREをclaimするPOST棚へ書く。
    /// これによりAU/VST3のidentity棚が分かれても届き、別host・不可視PRE claimは混ぜない。
    /// 自 keep の結果（有効ペアありなら true）を返す。broadcast 書込失敗は best-effort（無視）。
    pub fn keep_all(&self) -> bool {
        clear_keep_action_notice(&self.keep_action_notice);
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
        if project_hash.is_empty() {
            return false;
        }
        let p = match StoragePaths::default_platform() {
            Ok(paths) => paths,
            Err(_) => return false,
        };
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let ready = enumerate_ready_post_pair_candidates_for_operation_group(
            &kirin_root,
            &project_hash,
            &daw,
            host_process_id,
        );
        let members: Vec<_> = ready
            .iter()
            .filter_map(|candidate| {
                Some((
                    CaptureGenerationMember {
                        project_hash: candidate.project_uuid.clone(),
                        post_instance_id: candidate.instance_id.clone(),
                        pre_instance_id: candidate.paired_pre_instance_id.clone()?,
                        record_session_id: String::new(),
                    },
                    candidate.pair_pre_name.clone(),
                ))
            })
            .collect();
        if members.len() > MAX_CAPTURE_GENERATION_MEMBERS {
            if let Ok(mut error) = self.keep_action_notice.write() {
                *error = Some("Maximum 12 pairs reached".to_string());
            }
            return false;
        }
        let generation = CaptureGeneration::new_for_named_members(
            post_iid.clone(),
            daw.clone(),
            host_process_id,
            members,
        );
        if !generation.is_valid() {
            return false;
        }
        let plugin_data_dir = p.plugin_data_dir();
        let mut transaction =
            match CaptureGenerationTransaction::begin(&plugin_data_dir, &generation) {
                Ok(transaction) => transaction,
                Err(error) => {
                    let is_active_generation = matches!(
                        &error,
                        kirin_measure::CaptureGenerationError::Io(error)
                            if error.kind() == std::io::ErrorKind::WouldBlock
                    );
                    let message = match &error {
                        kirin_measure::CaptureGenerationError::Io(error)
                            if error.kind() == std::io::ErrorKind::WouldBlock =>
                        {
                            "Another Keep is active"
                        }
                        _ => "All Keep failed (file write error)",
                    };
                    if is_active_generation {
                        if let Ok(mut notice) = self.keep_action_notice.write() {
                            *notice = Some(message.to_string());
                        }
                    } else if let Ok(mut persistent) = self.record_error_message.write() {
                        *persistent = Some(message.to_string());
                    }
                    return false;
                }
            };
        if transaction.stage().is_err() {
            if let Ok(mut error) = self.record_error_message.write() {
                *error = Some("All Keep failed (file write error)".to_string());
            }
            return false;
        }
        let project_hashes = generation
            .members
            .iter()
            .map(|member| member.project_hash.clone())
            .collect::<std::collections::BTreeSet<_>>();
        for ph in project_hashes {
            if write_broadcast_for_generation(
                &plugin_data_dir,
                &ph,
                &post_iid,
                daw.clone(),
                host_process_id,
                &generation,
            )
            .is_err()
            {
                if let Ok(mut error) = self.record_error_message.write() {
                    *error = Some("All Keep failed (file write error)".to_string());
                }
                return false;
            }
        }
        if !self.keep_with_generation(&generation) {
            return false;
        }
        let action_notice = Arc::clone(&self.keep_action_notice);
        let phase = Arc::clone(&self.keep_phase);
        let phase_generation = Arc::clone(&self.keep_phase_generation_started_at_ms);
        let generation_started_at_ms = generation.started_at_ms;
        if transaction
            .commit_when_ready_async(CAPTURE_PRODUCER_READY_TIMEOUT, move |result| {
                if phase_generation.load(Ordering::Acquire) != generation_started_at_ms {
                    return;
                }
                if result.is_ok() {
                    phase.store(KIRIN_KEEP_PHASE_ARMED, Ordering::Release);
                } else {
                    phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
                    if let Ok(mut error) = action_notice.write() {
                        *error = Some("All Keep: pair not ready".to_string());
                    }
                }
            })
            .is_err()
        {
            self.keep_phase
                .store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
            if let Ok(mut error) = self.keep_action_notice.write() {
                *error = Some("All Keep: pair not ready".to_string());
            }
            return false;
        }
        true
    }

    /// POST「Stop」: pair を解除（record_signal released）し Watch へ戻す（B-061 3d-b）。
    /// PRE 側は released を検出して自身も Record を抜ける（io_thread_pre）。
    pub fn stop(&self) {
        clear_keep_action_notice(&self.keep_action_notice);
        self.keep_phase_generation_started_at_ms
            .store(0, Ordering::Release);
        self.keep_record_generation.store(0, Ordering::Release);
        self.keep_phase
            .store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
        // Stop preserves the exact pair identity for a later WAV Drop. Reservation release and
        // record_signal teardown still share the same resolver with broadcast Stop.
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
        clear_keep_action_notice(&self.keep_action_notice);
        let post_iid = match self.identity.lock() {
            Ok(id) => id.instance_id.clone(),
            Err(_) => String::new(),
        };
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        if !project_hash.is_empty() && !post_iid.is_empty() {
            if let Ok(p) = StoragePaths::default_platform() {
                let base = p.plugin_data_dir();
                let active =
                    kirin_measure::read_stop_target_generation(&base, &project_hash, &post_iid)
                        .ok()
                        .flatten();
                let mut wrote_generation_stop = false;
                if let Some(generation) = active {
                    if mark_generation_terminal(
                        &base,
                        &generation,
                        GenerationTerminalReason::AllStop,
                    )
                    .is_ok()
                    {
                        let projects = generation
                            .members
                            .iter()
                            .map(|member| member.project_hash.as_str())
                            .collect::<std::collections::BTreeSet<_>>();
                        wrote_generation_stop = true;
                        for ph in projects {
                            if write_stop_broadcast_for_generation(
                                &base,
                                ph,
                                &post_iid,
                                generation.daw_session_id.clone(),
                                generation.host_process_id,
                                &generation,
                            )
                            .is_err()
                            {
                                wrote_generation_stop = false;
                            }
                        }
                    }
                }
                // Legacy/no-active fallback is deliberately one known shelf. New producers never
                // rediscover an All Stop group by scanning live/history directories.
                if !wrote_generation_stop {
                    let _ = write_stop_broadcast(&base, &project_hash, &post_iid, daw.clone());
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
        // Rust-safe compatibility surface used by the historical direct-engine tests. It still
        // publishes an explicit Unknown span, so audio and clock cardinality remain inseparable;
        // the shipping C ABI bypasses this shelf and rejects a missing host descriptor below.
        if !interleaved.is_empty()
            && num_channels as usize == self.num_channels
            && interleaved.len().is_multiple_of(self.num_channels)
            && !self.pending_capture_valid.load(Ordering::Acquire)
        {
            self.note_capture_window(
                false,
                i64::MIN,
                (interleaved.len() / self.num_channels) as u64,
                CaptureClockSource::Unknown,
            );
        }
        let _ = self.push_samples_transaction(interleaved, num_channels);
    }

    /// Shipping Audio-Thread transaction. A non-empty callback must already have an exact host
    /// descriptor staged by the wrapper; the direct Rust shelf above is never used by JUCE.
    fn push_samples_transaction(&self, interleaved: &[f32], num_channels: u32) -> bool {
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

        // Watchdog 再起動後の Producer 所有権を atomic pointer だけで取り込む。
        // 旧 Producer は retired slot へ移し、Watchdog Thread が破棄するため、この
        // Audio Thread 経路に lock / allocation / deallocation はない。
        // SAFETY: push_samples は FFI 契約上、単一 Audio Thread からのみ呼ばれる。
        if unsafe { self.producer_handoff.swap_pending_from_audio() } {
            self.record_take_tracker.reset_capture_clock();
            self.capture_last_offline.store(false, Ordering::Release);
            let pass_id = self.watch_playback_pass_id.load(Ordering::Acquire);
            reset_watch_ring_cursor(
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                pass_id,
            );
            self.watch_ring_replacing.store(false, Ordering::Release);
        }
        // SAFETY: same single Audio Thread. Record adoption only moves atomic pointers.
        unsafe { self.record_ingress.adopt_from_audio() };

        // (2) create 時の layout と異なるブロックは測定に入れない。
        if num_channels as usize != self.num_channels {
            self.pending_capture_valid.store(false, Ordering::Release);
            self.pending_record_valid.store(false, Ordering::Release);
            return false;
        }

        let recording = self.record_sm.is_recording();
        if !interleaved.len().is_multiple_of(self.num_channels) {
            self.pending_capture_valid.store(false, Ordering::Release);
            self.pending_record_valid.store(false, Ordering::Release);
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        let frames = (interleaved.len() / self.num_channels) as u64;
        if frames > MAX_AUDIO_BLOCK_FRAMES as u64 {
            self.pending_capture_valid.store(false, Ordering::Release);
            self.pending_record_valid.store(false, Ordering::Release);
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        let had_pending_clock = self.pending_capture_valid.load(Ordering::Acquire);
        let pending_clock = self.take_pending_capture_window(frames);
        let had_pending_record = self.pending_record_valid.load(Ordering::Acquire);
        let pending_record = self.take_pending_record_block(frames);
        if had_pending_clock && pending_clock.is_none() {
            // A descriptor/sample cardinality mismatch is an invalid callback transaction. Drop
            // it whole; accepting unclocked audio would shift every later producer coordinate.
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        if had_pending_record && pending_record.is_none() {
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        if interleaved.is_empty() {
            if let Some(block) = pending_record {
                self.commit_pending_record_block(block);
            }
            return false;
        }
        if pending_clock.is_none() {
            // A sample without an immutable producer coordinate can never enter raw pre-roll or
            // Record truthfully. The JUCE ABI guarantees note_capture_window immediately before
            // every non-empty push, so this is a malformed transaction.
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        // Optional Spectrum ingress is independent from the established Watch/Record ring. Its
        // first operation is an atomic enabled check; hidden PRE/POST instances do no copy, FFT,
        // allocation, lock, I/O, wake, or repaint. Alignment is fail-closed unless the format
        // wrapper supplied an exact producer coordinate plus output presentation latency.
        let spectrum_presentation_start = pending_clock.and_then(spectrum_presentation_start);
        let _ = self.spectrum_runtime.push_block_from_audio(
            interleaved,
            self.num_channels,
            spectrum_presentation_start,
        );
        let accepted_offline_mode = pending_record.map(|block| block.offline);
        let offline_capture_boundary = accepted_offline_mode.is_some_and(|offline| offline)
            && !self.capture_last_offline.load(Ordering::Acquire);
        let pushed = if recording {
            let generation = self.record_sm.generation();
            // SAFETY: producer access is closure-bounded to this one Audio callback.
            let accepted = unsafe {
                self.record_ingress
                    .with_producer_from_audio(generation, |producer| {
                        if producer.slots() < interleaved.len() {
                            self.push_overflow
                                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
                            return 0;
                        }
                        let capture_origin = self.record_take_tracker.captured_frames_total();
                        self.record_ingress
                            .begin_generation_from_audio(generation, capture_origin);
                        if let Some(block) = pending_record {
                            self.commit_pending_record_block(block);
                        }
                        if let Some(clock) = pending_clock {
                            self.record_take_tracker
                                .note_capture_window_with_presentation_boundary(
                                    clock.position_valid,
                                    clock.position_samples,
                                    clock.num_frames,
                                    clock.clock_source,
                                    clock.presentation_latency,
                                    clock.force_new_epoch || offline_capture_boundary,
                                );
                        }
                        for &sample in interleaved {
                            // The single producer checked the complete cardinality above. A
                            // concurrent consumer can only increase, never consume, free slots.
                            let _ = producer.push(sample);
                        }
                        interleaved.len() as u64
                    })
            };
            if accepted.is_none() {
                self.push_overflow
                    .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            }
            accepted.unwrap_or(0)
        } else {
            // SAFETY: push_samples is the sole Audio Thread owner. Producer access cannot cross
            // the callback or a handoff swap.
            unsafe {
                self.producer_handoff
                    .with_active_producer_from_audio(|producer| {
                        if producer.slots() < interleaved.len() {
                            self.push_overflow
                                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
                            return 0;
                        }
                        if let Some(block) = pending_record {
                            self.commit_pending_record_block(block);
                        }
                        if let Some(clock) = pending_clock {
                            self.record_take_tracker
                                .note_capture_window_with_presentation_boundary(
                                    clock.position_valid,
                                    clock.position_samples,
                                    clock.num_frames,
                                    clock.clock_source,
                                    clock.presentation_latency,
                                    clock.force_new_epoch || offline_capture_boundary,
                                );
                        }
                        for &sample in interleaved {
                            let _ = producer.push(sample);
                        }
                        interleaved.len() as u64
                    })
            }
        };
        if pushed > 0 {
            if let Some(offline) = accepted_offline_mode {
                self.capture_last_offline.store(offline, Ordering::Release);
            }
        }
        if !recording && pushed > 0 && !self.watch_ring_replacing.load(Ordering::Acquire) {
            let pass_id = self.watch_playback_pass_id.load(Ordering::Acquire);
            add_watch_ring_cursor_samples(
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                pass_id,
                pushed,
            );
        }
        pushed == interleaved.len() as u64
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

    /// B-129 reopen (G-115-380): **test-only** — Audio→spool ring が空で、spool に publish 済みの
    /// 全サンプルを Measure reader が消費したか。parity の session_finalize gate が `lufs_i` の値
    /// プラトーでなく取り込み全体の実 drain を確認するための read-only introspection。
    /// **計測数値サーフェスではない**（bool を返すのみ・本番計測値は不変）。
    ///
    /// Test harness から `push_samples` と直列に呼ぶ。Record 経路では control mutex を読むため、
    /// shipping Audio callback から呼んではならない。
    #[doc(hidden)]
    pub fn __ring_drained_for_test(&self) -> bool {
        if self.record_sm.is_recording() {
            return self.record_ingress.drained_for_test();
        }
        let capacity = watch_ring_capacity_samples(self.num_channels);
        // SAFETY: 上記参照（SPSC・push_samples と同一スレッド・read-only slots()）。
        unsafe { self.producer_handoff.active_slots_from_audio() == capacity }
    }
}

#[inline]
fn watch_transport_starts_new_pass(
    playing: bool,
    previous_playing: bool,
    discontinuity: bool,
    force_new_pass: bool,
) -> bool {
    playing && (force_new_pass || !previous_playing || discontinuity)
}

impl Drop for KirinHyphaEngine {
    fn drop(&mut self) {
        // Stop renewing the exact PRE request before either IO generation is signalled. Runtime
        // join happens after the watchdog has joined IO/measure, so no worker can observe freed
        // engine state and no request survives a closed POST editor/instance.
        self.spectrum.shutdown();
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
        self.spectrum_runtime.shutdown_and_join();
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
    // LUFS-S は既存 ABI offset を変えないよう末尾追加。
    pub lufs_s: f64,
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
/// `mode`: 0=Active / 1=Stale / 2=NoPre / 3=Bypassed / 4=PreInactive。
#[repr(C)]
pub struct KirinDelta {
    pub mode: u8,
    pub lufs: f64,
    pub true_peak: f64,
    pub crest: f64,
    pub psr: f64,
    pub n_prime_total: f64,
    pub sharpness: f64,
    // Δ LUFS-S は既存 ABI offset を変えないよう末尾追加。
    pub lufs_s: f64,
}

pub const KIRIN_SPECTRUM_HIDDEN: u8 = 0;
pub const KIRIN_SPECTRUM_NO_PAIR: u8 = 1;
pub const KIRIN_SPECTRUM_WARMING_UP: u8 = 2;
pub const KIRIN_SPECTRUM_ACTIVE: u8 = 3;
pub const KIRIN_SPECTRUM_UNAVAILABLE: u8 = 4;

/// POST-only Spectrum view. `display_db` is signed POST - PRE and bounded only by the renderer;
/// the Rust exchange retains its unclipped raw difference separately.
#[repr(C)]
pub struct KirinSpectrumView {
    pub status: u8,
    pub has_data: u8,
    pub reserved: [u8; 2],
    pub sample_rate: u32,
    pub min_hz: f32,
    pub max_hz: f32,
    pub display_db: [f32; SPECTRUM_BAND_COUNT],
}

/// Read-only validation counters. No counter is used to make display or DSP decisions.
#[repr(C)]
pub struct KirinSpectrumStats {
    pub enabled: u8,
    pub worker_running: u8,
    pub reserved: [u8; 6],
    pub pushed_blocks: u64,
    pub dropped_blocks: u64,
    pub analyzed_frames: u64,
}

/// `KirinRecordDisplay` — Keep/Record専用の世代付き表示スナップショット。
#[repr(C)]
pub struct KirinRecordDisplay {
    pub phase: u8,
    pub has_measure: u8,
    pub has_session: u8,
    pub has_delta: u8,
    pub generation: u64,
    pub measure: KirinMeasureResult,
    pub session: KirinSessionSummary,
    pub delta: KirinDelta,
    /// 保持差分のPREと現在選択中のPREが同一なら1。表示分岐専用。
    pub pair_matches_current: u8,
}

pub const KIRIN_RECORD_DISPLAY_WATCH: u8 = 0;
pub const KIRIN_RECORD_DISPLAY_LIVE: u8 = 1;
pub const KIRIN_RECORD_DISPLAY_FINALIZING: u8 = 2;
pub const KIRIN_RECORD_DISPLAY_RESULT_HOLD: u8 = 3;
pub const KIRIN_RECORD_DISPLAY_UNAVAILABLE: u8 = 4;

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
        lufs_s: opt_f64(r.lufs_s),
    }
}

fn to_c_session(s: &SessionSummary) -> KirinSessionSummary {
    KirinSessionSummary {
        lufs_i: opt_f64(s.lufs_i),
        lra: opt_f64(s.lra),
        max_true_peak: opt_f64(s.max_true_peak),
    }
}

/// Shipped nih-plug VST3 state -> JUCE common-shell one-time migration.
/// This is invoked only from the host's state restore callback, never from `processBlock`.
///
/// # Safety
/// `data` must reference `len` readable bytes and `out` must reference writable storage.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_decode_legacy_nih_state(
    data: *const u8,
    len: usize,
    out: *mut KirinLegacyNihState,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if data.is_null() || out.is_null() {
            return false;
        }
        let bytes = unsafe { std::slice::from_raw_parts(data, len) };
        let Some(decoded) = decode_legacy_nih_state_bytes(bytes) else {
            return false;
        };
        unsafe { *out = decoded };
        true
    }))
    .unwrap_or(false)
}

fn delta_mode_to_abi(mode: &DeltaMode) -> u8 {
    match mode {
        DeltaMode::Active => 0,
        DeltaMode::Stale => 1,
        DeltaMode::NoPre => 2,
        DeltaMode::Bypassed => 3,
        DeltaMode::PreInactive => 4,
    }
}

fn to_c_delta(d: &DeltaResult) -> KirinDelta {
    KirinDelta {
        mode: delta_mode_to_abi(&d.mode),
        lufs: opt_f64(d.lufs),
        true_peak: opt_f64(d.tp),
        crest: opt_f64(d.crest),
        psr: opt_f64(d.psr),
        n_prime_total: opt_f64(d.n_prime_total),
        sharpness: opt_f64(d.sharpness),
        lufs_s: opt_f64(d.lufs_s),
    }
}

fn spectrum_status_to_abi(status: SpectrumViewStatus) -> u8 {
    match status {
        SpectrumViewStatus::Hidden => KIRIN_SPECTRUM_HIDDEN,
        SpectrumViewStatus::NoPair => KIRIN_SPECTRUM_NO_PAIR,
        SpectrumViewStatus::WarmingUp => KIRIN_SPECTRUM_WARMING_UP,
        SpectrumViewStatus::Active => KIRIN_SPECTRUM_ACTIVE,
        SpectrumViewStatus::Unavailable => KIRIN_SPECTRUM_UNAVAILABLE,
    }
}

fn to_c_spectrum(snapshot: SpectrumViewSnapshot) -> KirinSpectrumView {
    let has_data = snapshot.difference.is_some() as u8;
    let (sample_rate, min_hz, max_hz, display_db) =
        snapshot
            .difference
            .map_or((0, 0.0, 0.0, [0.0; SPECTRUM_BAND_COUNT]), |difference| {
                (
                    difference.sample_rate,
                    difference.min_hz,
                    difference.max_hz,
                    difference.display_db,
                )
            });
    KirinSpectrumView {
        status: spectrum_status_to_abi(snapshot.status),
        has_data,
        reserved: [0; 2],
        sample_rate,
        min_hz,
        max_hz,
        display_db,
    }
}

fn to_c_spectrum_stats(stats: SpectrumRuntimeStats) -> KirinSpectrumStats {
    KirinSpectrumStats {
        enabled: stats.enabled as u8,
        worker_running: stats.worker_running as u8,
        reserved: [0; 6],
        pushed_blocks: stats.pushed_blocks,
        dropped_blocks: stats.dropped_blocks,
        analyzed_frames: stats.analyzed_frames,
    }
}

fn record_display_phase_to_abi(status: RecordDisplayStatus) -> u8 {
    match status {
        RecordDisplayStatus::Empty | RecordDisplayStatus::Dismissed => KIRIN_RECORD_DISPLAY_WATCH,
        RecordDisplayStatus::Live => KIRIN_RECORD_DISPLAY_LIVE,
        RecordDisplayStatus::Finalizing => KIRIN_RECORD_DISPLAY_FINALIZING,
        RecordDisplayStatus::Finalized => KIRIN_RECORD_DISPLAY_RESULT_HOLD,
        RecordDisplayStatus::Unavailable => KIRIN_RECORD_DISPLAY_UNAVAILABLE,
    }
}

fn to_c_record_display(
    snapshot: RecordDisplaySnapshot,
    current_pair_pre_instance_id: Option<&str>,
) -> KirinRecordDisplay {
    let has_measure = snapshot.measure.is_some() as u8;
    let has_session = snapshot.summary.is_some() as u8;
    let has_delta = snapshot.delta.is_some() as u8;
    let pair_matches_current = matches!(
        (
            snapshot.pair_pre_instance_id.as_deref(),
            current_pair_pre_instance_id
        ),
        (Some(recorded), Some(current)) if recorded == current
    ) as u8;
    KirinRecordDisplay {
        phase: record_display_phase_to_abi(snapshot.status),
        has_measure,
        has_session,
        has_delta,
        generation: snapshot.generation,
        measure: snapshot
            .measure
            .as_ref()
            .map_or_else(|| to_c_result(&MeasureResult::default()), to_c_result),
        session: snapshot
            .summary
            .as_ref()
            .map_or_else(|| to_c_session(&SessionSummary::default()), to_c_session),
        delta: snapshot
            .delta
            .as_ref()
            .map_or_else(|| to_c_delta(&DeltaResult::default()), to_c_delta),
        pair_matches_current,
    }
}

#[cfg(test)]
mod delta_mode_abi_tests {
    use super::{delta_mode_to_abi, DeltaMode};

    #[test]
    fn pre_inactive_has_distinct_abi_mode_for_post_absolute_display() {
        assert_eq!(delta_mode_to_abi(&DeltaMode::Active), 0);
        assert_eq!(delta_mode_to_abi(&DeltaMode::Stale), 1);
        assert_eq!(delta_mode_to_abi(&DeltaMode::NoPre), 2);
        assert_eq!(delta_mode_to_abi(&DeltaMode::Bypassed), 3);
        assert_eq!(delta_mode_to_abi(&DeltaMode::PreInactive), 4);
    }
}

#[cfg(test)]
mod spectrum_abi_tests {
    use super::*;
    use kirin_measure::spectrum::SpectrumDifference;

    #[test]
    fn spectrum_status_and_signed_display_values_have_stable_c_mapping() {
        assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::Hidden), 0);
        assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::NoPair), 1);
        assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::WarmingUp), 2);
        assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::Active), 3);
        assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::Unavailable), 4);

        let snapshot = SpectrumViewSnapshot {
            status: SpectrumViewStatus::Active,
            difference: Some(SpectrumDifference {
                presentation_end_samples: 48_000,
                sample_rate: 48_000,
                min_hz: 10.0,
                max_hz: 22_000.0,
                raw_db: [15.0; SPECTRUM_BAND_COUNT],
                display_db: [-3.5; SPECTRUM_BAND_COUNT],
            }),
        };
        let out = to_c_spectrum(snapshot);
        assert_eq!(out.status, KIRIN_SPECTRUM_ACTIVE);
        assert_eq!(out.has_data, 1);
        assert_eq!(out.sample_rate, 48_000);
        assert_eq!(out.display_db[0], -3.5);
        assert_eq!(out.display_db[SPECTRUM_BAND_COUNT - 1], -3.5);
    }

    #[test]
    fn presentation_alignment_requires_known_wrapper_output_latency() {
        let exact = PendingCaptureWindow {
            position_valid: true,
            position_samples: 9_600,
            num_frames: 480,
            clock_source: CaptureClockSource::ProjectTimeline,
            presentation_latency: PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(2_048),
            },
            force_new_epoch: false,
        };
        assert_eq!(spectrum_presentation_start(exact), Some(11_648));
        assert_eq!(
            spectrum_presentation_start(PendingCaptureWindow {
                presentation_latency: PresentationLatencySamples::default(),
                ..exact
            }),
            None
        );
        assert_eq!(
            spectrum_presentation_start(PendingCaptureWindow {
                position_valid: false,
                ..exact
            }),
            None
        );
    }

    #[test]
    fn pre_role_cannot_expose_the_post_spectrum_page() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        assert!(!engine.set_spectrum_visible(true));
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Pre);
        assert!(!engine.set_spectrum_visible(true));
        assert!(!engine.spectrum_stats().enabled);
    }
}

#[cfg(test)]
mod record_display_abi_tests {
    use super::*;

    #[test]
    fn every_record_display_phase_has_a_stable_c_code() {
        assert_eq!(
            record_display_phase_to_abi(RecordDisplayStatus::Empty),
            KIRIN_RECORD_DISPLAY_WATCH
        );
        assert_eq!(
            record_display_phase_to_abi(RecordDisplayStatus::Dismissed),
            KIRIN_RECORD_DISPLAY_WATCH
        );
        assert_eq!(
            record_display_phase_to_abi(RecordDisplayStatus::Live),
            KIRIN_RECORD_DISPLAY_LIVE
        );
        assert_eq!(
            record_display_phase_to_abi(RecordDisplayStatus::Finalizing),
            KIRIN_RECORD_DISPLAY_FINALIZING
        );
        assert_eq!(
            record_display_phase_to_abi(RecordDisplayStatus::Finalized),
            KIRIN_RECORD_DISPLAY_RESULT_HOLD
        );
        assert_eq!(
            record_display_phase_to_abi(RecordDisplayStatus::Unavailable),
            KIRIN_RECORD_DISPLAY_UNAVAILABLE
        );
    }

    #[test]
    fn record_display_marshalling_keeps_presence_flags_and_short_term_values() {
        let snapshot = RecordDisplaySnapshot {
            generation: 19,
            status: RecordDisplayStatus::Finalized,
            measure: Some(MeasureResult {
                lufs_m: Some(-14.0),
                lufs_s: Some(-13.5),
                ..MeasureResult::default()
            }),
            summary: Some(SessionSummary {
                lufs_i: Some(-14.2),
                lra: Some(3.0),
                max_true_peak: Some(-0.7),
            }),
            delta: Some(DeltaResult {
                lufs: Some(0.2),
                lufs_s: Some(0.4),
                ..DeltaResult::default()
            }),
            pair_pre_instance_id: Some("pre-19".to_string()),
            measure_started: true,
        };
        let out = to_c_record_display(snapshot, Some("pre-19"));
        assert_eq!(out.phase, KIRIN_RECORD_DISPLAY_RESULT_HOLD);
        assert_eq!(out.generation, 19);
        assert_eq!((out.has_measure, out.has_session, out.has_delta), (1, 1, 1));
        assert_eq!(out.pair_matches_current, 1);
        assert_eq!(out.measure.lufs_s, -13.5);
        assert_eq!(out.session.lufs_i, -14.2);
        assert_eq!(out.delta.lufs_s, 0.4);
    }

    #[test]
    fn absent_record_values_are_flagged_and_marshaled_as_nan() {
        let out = to_c_record_display(RecordDisplaySnapshot::default(), None);
        assert_eq!(out.phase, KIRIN_RECORD_DISPLAY_WATCH);
        assert_eq!((out.has_measure, out.has_session, out.has_delta), (0, 0, 0));
        assert_eq!(out.pair_matches_current, 0);
        assert!(out.measure.lufs_m.is_nan());
        assert!(out.measure.lufs_s.is_nan());
        assert!(out.session.lufs_i.is_nan());
        assert!(out.delta.lufs_s.is_nan());
    }

    #[test]
    fn changed_pair_cannot_claim_a_held_delta() {
        let snapshot = RecordDisplaySnapshot {
            pair_pre_instance_id: Some("pre-original".to_string()),
            ..RecordDisplaySnapshot::default()
        };
        assert_eq!(
            to_c_record_display(snapshot.clone(), Some("pre-other")).pair_matches_current,
            0
        );
        assert_eq!(
            to_c_record_display(snapshot, Some("pre-original")).pair_matches_current,
            1
        );
    }

    #[test]
    fn additive_lufs_s_fields_follow_the_preexisting_abi_tail() {
        assert!(
            std::mem::offset_of!(KirinMeasureResult, lufs_s)
                > std::mem::offset_of!(KirinMeasureResult, dropped_samples)
        );
        assert!(
            std::mem::offset_of!(KirinDelta, lufs_s) > std::mem::offset_of!(KirinDelta, sharpness)
        );
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
/// A non-null `handle` must be a live pointer returned by `kirin_hypha_create`.
/// `pre_project_hash` and `instance_id` may be null; otherwise each must point to a readable
/// null-terminated C string for this call.
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

/// Restore one exact PRE from the DAW state chunk. Unlike user selection this does not enumerate
/// live PREs: it reconstructs the saved fixed path and waits for that runtime to publish.
///
/// # Safety
/// A non-null `handle` must be a live pointer returned by `kirin_hypha_create`.
/// `pre_project_hash` and `instance_id` may be null; otherwise each must point to a readable
/// null-terminated C string for this call.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_restore_pair_candidate(
    handle: *mut KirinHyphaEngine,
    pre_project_hash: *const c_char,
    instance_id: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let pre_project_hash = unsafe { read_c_str(pre_project_hash) };
        let instance_id = unsafe { read_c_str(instance_id) };
        unsafe { (*handle).restore_pair_candidate(&pre_project_hash, &instance_id) }
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

/// Return the project shelf and instance ID of the exact PRE from one binding snapshot.
///
/// # Safety
/// A non-null `handle` must be a live pointer returned by `kirin_hypha_create`. Each non-null
/// output pointer must reference a writable buffer at least as large as its corresponding length.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_paired_pre_locator(
    handle: *mut KirinHyphaEngine,
    project_out: *mut c_char,
    project_out_len: usize,
    instance_out: *mut c_char,
    instance_out_len: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || project_out.is_null()
            || project_out_len == 0
            || instance_out.is_null()
            || instance_out_len == 0
        {
            return false;
        }
        let Some((project_hash, instance_id)) = (unsafe { (*handle).paired_pre_locator() }) else {
            return false;
        };
        let project_bytes = project_hash.as_bytes();
        let project_n = project_bytes.len().min(project_out_len - 1);
        let project_dst =
            unsafe { std::slice::from_raw_parts_mut(project_out as *mut u8, project_out_len) };
        project_dst[..project_n].copy_from_slice(&project_bytes[..project_n]);
        project_dst[project_n] = 0;

        let instance_bytes = instance_id.as_bytes();
        let instance_n = instance_bytes.len().min(instance_out_len - 1);
        let instance_dst =
            unsafe { std::slice::from_raw_parts_mut(instance_out as *mut u8, instance_out_len) };
        instance_dst[..instance_n].copy_from_slice(&instance_bytes[..instance_n]);
        instance_dst[instance_n] = 0;
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

/// Drain one direct Keep/All Keep user-action notice. Unlike `record_error_message`, this is an
/// edge and therefore cannot remain visible after the shell has acknowledged it.
///
/// # Safety
/// `handle` は有効なハンドル。`out` は `out_len` バイト以上の有効バッファ。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_drain_keep_action_notice(
    handle: *mut KirinHyphaEngine,
    out: *mut c_char,
    out_len: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || out_len == 0 {
            return false;
        }
        match unsafe { (*handle).drain_keep_action_notice() } {
            Some(message) => {
                let bytes = message.as_bytes();
                let n = bytes.len().min(out_len - 1);
                let dst = unsafe { std::slice::from_raw_parts_mut(out as *mut u8, out_len) };
                dst[..n].copy_from_slice(&bytes[..n]);
                dst[n] = 0;
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

/// Keep control phase. `PREPARING` is not yet safe to bounce; `ARMED` means every exact 1–12
/// member crossed the same writer/measure barrier.
///
/// # Safety
/// `handle` は有効なハンドル。null は `KIRIN_KEEP_PHASE_IDLE` として扱う。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_keep_phase(handle: *mut KirinHyphaEngine) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return KIRIN_KEEP_PHASE_IDLE;
        }
        unsafe { (*handle).keep_phase() }
    }))
    .unwrap_or(KIRIN_KEEP_PHASE_IDLE)
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
            (*handle).stage_record_block(RecordTakeBlock {
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
            (*handle).stage_record_block(RecordTakeBlock {
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
    force_new_epoch: bool,
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
                force_new_epoch,
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
    force_new_pass: bool,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !handle.is_null() {
            unsafe {
                (*handle).note_transport_block(
                    playing,
                    position_valid,
                    position_samples,
                    num_frames,
                    force_new_pass,
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

/// Enable or disable the POST-only Spectrum page. The visibility edge itself performs no file
/// access; the existing POST IO worker owns request renewal and exact-PRE snapshot reads.
/// PRE and not-yet-enabled engines return false.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_spectrum_visible(
    handle: *mut KirinHyphaEngine,
    visible: bool,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).set_spectrum_visible(visible) }
    }))
    .unwrap_or(false)
}

/// Poll the latest POST-minus-PRE Spectrum view. Status-only snapshots are successful reads and
/// carry `has_data=0`; lock contention or a null argument returns false without changing `out`.
///
/// # Safety
/// `handle` and `out` must be live writable pointers. UI Thread only.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_spectrum(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinSpectrumView,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(snapshot) = (unsafe { &*handle }).poll_spectrum() else {
            return false;
        };
        unsafe { *out = to_c_spectrum(snapshot) };
        true
    }))
    .unwrap_or(false)
}

/// Read optional Spectrum worker counters for performance/regression validation.
///
/// # Safety
/// `handle` and `out` must be live writable pointers. Control/UI Thread only.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_spectrum_stats(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinSpectrumStats,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        unsafe { *out = to_c_spectrum_stats((*handle).spectrum_stats()) };
        true
    }))
    .unwrap_or(false)
}

/// Keep/Record表示スナップショットを`out`へ書く。UI Thread専用。
///
/// # Safety
/// `handle`/`out`は有効。`out`は書込可能な`KirinRecordDisplay`。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_record_display(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinRecordDisplay,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(snapshot) = (unsafe { &*handle }).poll_record_display() else {
            return false;
        };
        let current_pair = (unsafe { &*handle }).paired_pre_instance_id();
        unsafe { *out = to_c_record_display(snapshot, current_pair.as_deref()) };
        true
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
) -> bool {
    // panic 捕捉時は no-op（Audio Thread / 音声素通しに影響させない）。
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let engine = unsafe { &*handle };
        let len = num_frames.saturating_mul(num_channels as usize);
        if len == 0 || interleaved.is_null() {
            // 0-frame keepalive（heartbeat のみ進める）。
            return engine.push_samples_transaction(&[], num_channels);
        }
        let slice = unsafe { std::slice::from_raw_parts(interleaved, len) };
        engine.push_samples_transaction(slice, num_channels)
    }))
    .unwrap_or(false)
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
mod admission_contract_tests {
    use super::{KirinHyphaEngine, RecordTakeBlock, MAX_AUDIO_BLOCK_FRAMES};

    #[test]
    fn shipping_transaction_rejects_channel_remainder_without_advancing_clock() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        assert!(!engine.push_samples_transaction(&[0.0, 1.0, 2.0], 2));
        assert_eq!(engine.overflow_count(), 3);
        assert_eq!(engine.record_take_tracker.captured_frames_total(), 0);
    }

    #[test]
    fn shipping_transaction_reports_success_only_after_audio_and_clock_commit() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        engine.note_capture_window(
            true,
            42,
            1,
            kirin_measure::CaptureClockSource::ProjectTimeline,
        );
        assert!(engine.push_samples_transaction(&[0.25, -0.25], 2));
        assert_eq!(engine.record_take_tracker.captured_frames_total(), 1);

        assert!(!engine.push_samples_transaction(&[0.0, 0.0], 2));
        assert_eq!(engine.record_take_tracker.captured_frames_total(), 1);
    }

    #[test]
    fn shipping_transaction_rejects_nonempty_unclocked_watch_and_record() {
        let watch = KirinHyphaEngine::new(48_000, 2);
        assert!(!watch.push_samples_transaction(&[0.0, 0.0], 2));
        assert_eq!(watch.overflow_count(), 2);
        assert_eq!(watch.record_take_tracker.captured_frames_total(), 0);

        let record = KirinHyphaEngine::new(48_000, 2);
        record.set_license(super::LICENSE_OS);
        assert!(record.enter_record());
        record.stage_record_block(RecordTakeBlock {
            generation: 0,
            recording: true,
            rendered: true,
            playing: true,
            offline: true,
            position_valid: true,
            position_samples: 0,
            num_frames: 1,
            clock_start_samples: 0,
            clock_end_samples: None,
        });
        assert!(!record.push_samples_transaction(&[0.0, 0.0], 2));
        assert_eq!(record.overflow_count(), 2);
        assert_eq!(record.record_take_tracker.captured_frames_total(), 0);
        assert_eq!(record.record_sm.record_started_at_position_samples(), None);
    }

    #[test]
    fn shipping_transaction_rejects_frames_above_declared_host_maximum() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        let frames = MAX_AUDIO_BLOCK_FRAMES + 1;
        let block = vec![0.0; frames * 2];
        engine.note_capture_window(
            true,
            0,
            frames as u64,
            kirin_measure::CaptureClockSource::ProjectTimeline,
        );
        assert!(!engine.push_samples_transaction(&block, 2));
        assert_eq!(engine.overflow_count(), block.len() as u64);
        assert_eq!(engine.record_take_tracker.captured_frames_total(), 0);
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
mod b474_watch_restart_tests {
    use super::watch_transport_starts_new_pass;

    #[test]
    fn shared_unavailable_to_active_boundary_starts_a_new_watch_pass() {
        assert!(watch_transport_starts_new_pass(true, true, false, true));
    }

    #[test]
    fn continuous_playback_without_a_shared_boundary_keeps_the_current_pass() {
        assert!(!watch_transport_starts_new_pass(true, true, false, false));
    }

    #[test]
    fn unavailable_boundary_cannot_start_a_pass_while_transport_is_stopped() {
        assert!(!watch_transport_starts_new_pass(false, true, false, true));
    }
}

#[cfg(test)]
mod legacy_nih_state_tests {
    use super::{decode_legacy_nih_state_bytes, KirinLegacyNihState};
    use std::ffi::CStr;

    fn field(state: &KirinLegacyNihState, which: &str) -> String {
        let ptr = match which {
            "instance_id" => state.instance_id.as_ptr(),
            "project_uuid" => state.project_uuid.as_ptr(),
            "daw_session_uuid" => state.daw_session_uuid.as_ptr(),
            "name" => state.name.as_ptr(),
            "pair_pre_name" => state.pair_pre_name.as_ptr(),
            _ => unreachable!(),
        };
        unsafe { CStr::from_ptr(ptr) }
            .to_string_lossy()
            .into_owned()
    }

    #[test]
    fn decodes_pre_identity_from_exact_nih_fields_contract() {
        let bytes = br#"{"version":"1.1.26","params":{},"fields":{"instance_id":"\"iid-pre\"","project_uuid":"\"project-a\"","daw_session_uuid":"\"session-a\"","name":"\"Drum\""}}"#;
        let state = decode_legacy_nih_state_bytes(bytes).expect("legacy PRE state");
        assert_eq!(field(&state, "instance_id"), "iid-pre");
        assert_eq!(field(&state, "project_uuid"), "project-a");
        assert_eq!(field(&state, "daw_session_uuid"), "session-a");
        assert_eq!(field(&state, "name"), "Drum");
        assert_eq!(field(&state, "pair_pre_name"), "");
    }

    #[test]
    fn decodes_post_pair_and_rejects_unrelated_or_malformed_state() {
        let bytes = br#"{"version":"1.1.26","params":{"bypass":{"Bool":false}},"fields":{"instance_id":"\"iid-post\"","project_uuid":"\"project-a\"","daw_session_uuid":"\"session-a\"","pair_pre_name":"\"2Mix\"","pair_claimed_at":"12.0"}}"#;
        let state = decode_legacy_nih_state_bytes(bytes).expect("legacy POST state");
        assert_eq!(field(&state, "instance_id"), "iid-post");
        assert_eq!(field(&state, "pair_pre_name"), "2Mix");
        assert!(decode_legacy_nih_state_bytes(br#"{"fields":{"other":"\"x\""}}"#).is_none());
        assert!(decode_legacy_nih_state_bytes(b"not-json").is_none());
        assert!(decode_legacy_nih_state_bytes(&vec![b' '; 1024 * 1024 + 1]).is_none());
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

#[cfg(test)]
mod keep_action_notice_tests {
    use super::*;

    #[test]
    fn user_action_notice_is_one_shot_and_never_pollutes_persistent_io_error() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        *engine
            .keep_action_notice
            .write()
            .expect("keep action notice lock") = Some("Another Keep is active".to_string());

        assert_eq!(engine.record_error_message(), None);
        assert_eq!(
            engine.drain_keep_action_notice().as_deref(),
            Some("Another Keep is active")
        );
        assert_eq!(engine.drain_keep_action_notice(), None);
        assert_eq!(engine.record_error_message(), None);
    }
}
