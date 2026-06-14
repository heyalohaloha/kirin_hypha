//! kirin_hypha_ffi — Kirin Hypha JUCE 移植の C ABI ラッパ。
//!
//! 方式 B2: 検証済み Rust ランタイム(`kirin_measure`)を **無変更** で C ABI に包む。
//! C++/JUCE 側に DSP・計測ロジックを一切移さない（計測器は精度が製品そのもの）。
//!
//! # C ABI surface（すべて実装済み）
//! - RT 計測: `create` / `set_signal_state` / `push_samples` / `poll_result` / `destroy`。
//! - Record: `set_license` / `enter_record` / `exit_record` と `poll_session`
//!   (LUFS-I/LRA/max_true_peak)。SessionSummary は `engine.finalize()` 由来で Record 中にのみ
//!   成立する量で、Measure Thread が **自律的に** finalize して `session_summary` を充填する
//!   （measure_thread.rs:290-295）。FFI は RecordStateMachine を flip するだけ（exit で finalize
//!   を呼ばない＝finalize は Measure Thread のみ / engine.rs:161）。`poll_session` は Record
//!   finalize 後に値を返す（Record 前は false）。
//! - state chunk 識別子: `set_identity` / `get_identity`（方式A）。
//! - plugin_data IO: `enable_pre_writes`（PRE: Watch pre.json + Record frames/PSB）/
//!   `enable_post_writes`（POST: post.json の生メトリクス + Δ を select_target_pre 経由で算出）。
//!   filesystem 書込は kirin_measure の io_thread 内に閉じる（FFI は spawn と識別子注入のみ）。
//! - PRE-POST ペアリング: `set_pair_target` / `keep` / `stop` / `poll_delta`
//!   （POST Keep → PRE が record_signal を ack して自律的に Record に入る）。
//! - Note: `add_annotation`（Record の最新 plugin_data .json に利用者メモを追記）。
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
use std::ffi::CStr;
use std::os::raw::{c_char, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, AtomicU64, AtomicU8, AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock, RwLock};
use std::thread::JoinHandle;

use uuid::Uuid;

use kirin_measure::engine::SessionSummary;
use kirin_measure::reservation; // B-127 (G-115-364): per-pairing O_EXCL reservation
use kirin_measure::{
    append_annotation_to_latest, can_write_plugin_data, check_record_exclusion,
    count_distinct_pairings, identity_instance_attach, identity_instance_detach,
    MAX_ACTIVE_PER_PROJECT,
    enumerate_active_post_pair_candidates, enumerate_active_pre_pair_candidates, load_license_safe,
    live_window, load_signal_state, mark_released,
    resolve_arm_target, sanitize_name, set_daw_session_id, set_project_uuid,
    spawn_io_thread_post,
    spawn_io_thread_pre, spawn_measure_thread, spawn_watchdog, store_signal_state, write_broadcast,
    write_pending, write_stop_broadcast, DeltaMode, DeltaResult, ExclusionResult, IoThreadHandle,
    LatchedPre, License,
    LivenessEvaluator, MeasureResult, PluginDataRole, PsbSummary, RecordStateMachine, RestartIoFn,
    SignalState, StoragePaths, WatchdogIo, WatchdogParams, N_CHANNELS, RING_BUFFER_SECONDS,
};

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
    /// Audio Thread が宣言する信号状態（Measure Thread が読む）。
    signal_state: Arc<AtomicU8>,
    /// Measure Thread 停止フラグ（destroy でセット → join）。
    shutdown: Arc<AtomicBool>,
    /// process() 相当の heartbeat（push_samples が進める）。
    heartbeat: Arc<AtomicU32>,
    /// B-118: 単一鮮度評価器。editor が POST pair lock の live 述語として読み（`kirin_hypha_heartbeat_live`
    /// getter 経由）、Measure Thread / watchdog も同一評価器を読む（signal_state とは別軸）。
    liveness: Arc<LivenessEvaluator>,
    /// Record 状態機械。`enter_record`/`exit_record` で flip し、Measure Thread が
    /// `is_recording()` を見て自律 finalize する（Phase 3a で実配線）。
    record_sm: Arc<RecordStateMachine>,
    /// 現ライセンス（C ABI コード: 0=Os 1=Sense 2=Unknown）。`enter_record` の
    /// 二重 gate（E-21）に使う。既定は Unknown（Record 不可）。
    /// B-102: `Arc<AtomicU8>` 化（broadcast 受信 closure が live に読むため / keep と同一 gate）。
    license: Arc<AtomicU8>,
    /// `spawn_io_thread_pre` に渡す入力サンプルレート（create 時に保持）。
    sample_rate: u32,
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
    /// POST の対 PRE 名（B-061 3d-b）。`set_pair_target` で設定（identity.name 結合を解く）。
    /// `enable_post_writes` 時に空なら identity.name で seed し、io_thread と Arc 共有する
    /// （run_tick の select / keep() の write_pending target 解決に使う・live 反映）。
    pair_target: Arc<RwLock<String>>,
    /// keep() が選定した対 PRE instance_id（B-062）。io_thread と Arc 共有し、POST Record の
    /// `paired_pre_instance_id`（plugin_data linkage）に焼かれる。stop() で None に戻す。
    paired_pre_target: Arc<Mutex<Option<String>>>,
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
    /// B-108: display と keep/Arm が共有する単一ラッチ。`enable_post_writes` で io_thread_post と
    /// Arc 共有し、keep/keep_all/broadcast 受信が `resolve_arm_target` で読む。一度成立した PRE 結合を
    /// 保持し、解除は pair 名変更/クリアと PRE 実消滅のみ（B-108）。
    latched_pre: Arc<Mutex<Option<LatchedPre>>>,
}

// SAFETY: `ring_producer`(UnsafeCell<rtrb::Producer>) は push_samples からのみ触れ、
// その push_samples は「Audio Thread 単独」という FFI 契約で単一スレッドアクセスに限定される。
// 他の全フィールドは Arc<Mutex>/Arc<Atomic>/AtomicU64/AtomicU8 で Sync。よって
// `&KirinHyphaEngine` を Audio/UI 2 スレッドで共有しても（契約を守る限り）健全。
unsafe impl Sync for KirinHyphaEngine {}
// SAFETY: 内部状態はスレッド間移動可能（Producer/Arc は Send）。
unsafe impl Send for KirinHyphaEngine {}

// ── B-106: FFI dylib 内で全 engine instance が共有する project_hash / daw_session_id ──
//
// egui の不変条件「project_hash / daw_session_id は per-instance コピーではなく、dylib に
// 1 つの共有値を全員が使用時に live-read」を FFI でも成立させるためのセル。kirin_measure の
// `project_uuid_cell` / `daw_session_id_cell` は pub な Arc を公開しない（kirin_measure 無変更
// 制約）ため、`spawn_io_thread_post`（`Arc<RwLock<String>>` を受領し io_thread が毎 tick
// `read_project_hash_arc` で deref する）に渡す clonable Arc を FFI 側で保持する。これにより
// enable 順・上書きに依らず全 POST instance が同一の broadcast 棚に収束する（B-105 で判明した
// project_uuid 棚分裂 = io_thread_post.rs:526 の自棚限定 scan の解消）。
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
/// 順序は従来 `keep()` と不変: double-keep guard → select_target_pre → linkage set →
/// try_enter_record（license 二重 gate）→ write_pending。失敗時は遷移/linkage を巻き戻す。
/// `None` 選定は `try_enter_record` 前に抜けるため、broadcast 起因の Record は有効ペア時のみ。
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
) -> bool {
    // B-071 double-keep guard: 既に Record 中なら no-op（既存 linkage 温存）。
    if record_sm.is_recording() {
        return false;
    }
    let kirin_root = std::env::temp_dir().join("kirin");
    let pair = pair_target.read().map(|g| g.clone()).unwrap_or_default();
    // B-108: ラッチ済み（名前一致+fresh）はラッチ先を直接使用、未ラッチは B-104 Arm ゲート
    // （非Bypassed + fresh + 一意 / Active 要求なし）。v1.0.0 の「アーム→再生」を維持する。
    let Some(sel) = resolve_arm_target(&kirin_root, &pair, latched) else {
        return false; // 未ラッチ時の厳格選定 None: 空名/不在/曖昧/Bypassed/古t（Inactive は許容）。
    };
    let target = sel.instance_id;
    // linkage（B-062）: record_sm を Record に flip する前に set。
    if let Ok(mut g) = paired_pre_target.lock() {
        *g = Some(target.clone());
    }
    // license 二重 gate（record.rs try_enter_record / E-21）。非 Os は投機的 linkage 巻き戻し。
    if record_sm.try_enter_record(license).is_err() {
        if let Ok(mut g) = paired_pre_target.lock() {
            *g = None;
        }
        return false;
    }
    let base = match StoragePaths::default_macos() {
        Ok(p) => p.plugin_data_dir(),
        Err(_) => {
            revert_after_enter(record_sm, paired_pre_target);
            return false;
        }
    };
    // B-127 (G-115-365): per-pairing O_EXCL reservation で cross-process atomic に枠を確保する。
    // pairing key = (target=PRE iid, post_iid=POST iid)。cap の真実源は枠ファイルの**物理存在のみ**
    // （count_distinct_pairings = reservation::count_frames）。reservation を先に atomic-create
    // してから枠数を数えることで、active marker 出現前の TOCTOU 窓を cross-process で閉じる。cap は
    // 12 pairs。keep() / keep_all() / broadcast 受信 closure は全て本関数を通る（JUCE 殻 parity）。
    // reserve は 1 回だけ呼ぶ。Created = 本呼び出しが枠を作った（reject 時に解放する責務）。
    let reservation_created = match reservation::reserve_pairing(&base, project_hash, &target, post_iid) {
        Ok(reservation::ReserveOutcome::Created) => true,
        Ok(reservation::ReserveOutcome::AlreadyReserved) => false,
        // G-115-365 (3): 枠が取れない（write_all 失敗等の Err / 不完全枠は内部で unlink 済）= reject。
        // 枠なしで keep に入らない。
        Err(_) => {
            if let Ok(mut g) = record_error_message.write() {
                *g = Some("Maximum 12 pairs reached".to_string());
            }
            revert_after_enter(record_sm, paired_pre_target);
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
        revert_after_enter(record_sm, paired_pre_target);
        return false;
    }
    // target_pre_instance_id = 選定 PRE。PRE が自宛て signal を発見し ack する。
    if write_pending(&base, project_hash, post_iid, target.clone(), daw.to_string()).is_ok() {
        // B-127: 正常 enter で stale な cap/io-fail 通知を消す（新しい健全な Record が開始した）。
        if let Ok(mut g) = record_error_message.write() {
            *g = None;
        }
        true
    } else {
        // write_pending 失敗時も予約枠を戻す（自分が作った場合のみ）。
        if reservation_created {
            reservation::release_pairing(&base, project_hash, &target, post_iid);
        }
        revert_after_enter(record_sm, paired_pre_target);
        false
    }
}

/// `resolve_and_enter_keep` が try_enter_record 成功**後**に失敗した時の巻き戻し（B-066 / F2）。
/// 本 keep が行った Watch→Record 遷移と linkage 代入を戻す（副作用なし exit_record）。
fn revert_after_enter(record_sm: &RecordStateMachine, paired_pre_target: &Mutex<Option<String>>) {
    record_sm.exit_record();
    if let Ok(mut g) = paired_pre_target.lock() {
        *g = None;
    }
}

/// B-102: stop の解決本体（`stop()` と broadcast 受信 closure が共有）。exit_record + linkage
/// クリアは常に行い、`mark_released` は identity 非空のときだけ（元 `stop()` と同一）。
fn resolve_and_exit_stop(
    record_sm: &RecordStateMachine,
    paired_pre_target: &Mutex<Option<String>>,
    project_hash: &str,
    post_iid: &str,
) {
    record_sm.exit_record();
    // B-127: linkage クリア前に対 PRE iid を捕捉し、本 pairing の O_EXCL reservation 枠を解放する
    // （両 marker が Closed/stale になる前でも stop で明示解放。孤児は sweep が age-based で回収）。
    let released_pre = paired_pre_target.lock().ok().and_then(|g| g.clone());
    if let Ok(mut g) = paired_pre_target.lock() {
        *g = None; // linkage クリア（次 Keep まで）。
    }
    if project_hash.is_empty() || post_iid.is_empty() {
        return; // 未 enable → released marker は書けない。
    }
    if let Ok(p) = StoragePaths::default_macos() {
        let base = p.plugin_data_dir();
        if let Some(pre) = released_pre.as_deref() {
            reservation::release_pairing(&base, project_hash, pre, post_iid);
        }
        let _ = mark_released(&base, project_hash, post_iid);
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
    /// `num_channels` は stereo 前提（N_CHANNELS=2）。
    pub fn new(sample_rate: u32, _num_channels: u32) -> Self {
        // 本番 hypha_pre.rs:282-284 と同一の容量計算。
        let capacity = (sample_rate as usize) * RING_BUFFER_SECONDS * N_CHANNELS;
        let (producer, consumer) = rtrb::RingBuffer::new(capacity);

        let measure_result = Arc::new(Mutex::new(MeasureResult::default()));
        let delta_result = Arc::new(Mutex::new(DeltaResult::default()));
        let session_summary: Arc<Mutex<Option<SessionSummary>>> = Arc::new(Mutex::new(None));
        let signal_state = Arc::new(AtomicU8::new(SignalState::Inactive as u8));
        let shutdown = Arc::new(AtomicBool::new(false));
        let heartbeat = Arc::new(AtomicU32::new(0));
        // B-118: 単一鮮度評価器。heartbeat を内部観測し is_live()（G-115-245: 3s window）を返す。
        // Measure Thread / editor pair lock / FFI getter / watchdog が同一評価器を読む。
        let liveness = Arc::new(LivenessEvaluator::new(Arc::clone(&heartbeat), live_window()));
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
            Arc::clone(&measure_result),
            Arc::clone(&signal_state),
            Arc::clone(&shutdown),
            Arc::clone(&liveness),
            Arc::clone(&record_sm),
            Arc::clone(&session_summary),
        );

        // B-118: T-8 watchdog 再採用（B-056 opt-out 撤回）。Measure Thread crash を再起動し、io は
        // Lazy（enable で後発 spawn → 共有 slot）で監視・再起動する。FFI は free 前に全 thread を
        // join するため join_on_shutdown=true（io→measure 順 / 共有 Arc UAF 回避）。
        let watchdog_handle = spawn_watchdog(WatchdogParams {
            sample_rate,
            ring_capacity: capacity,
            measure_result: Arc::clone(&measure_result),
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
        });

        // B-110: live インスタンス refcount +1（破棄は Drop で −1）。enable ではなく create に置く
        // （enable は冪等 early-return のため）。
        identity_instance_attach();

        Self {
            ring_producer: UnsafeCell::new(producer),
            measure_result,
            delta_result,
            session_summary,
            signal_state,
            shutdown,
            heartbeat,
            liveness,
            record_sm,
            // 既定 Unknown（set_license(Os) されるまで Record 不可・安全側）。
            license: Arc::new(AtomicU8::new(LICENSE_UNKNOWN)),
            sample_rate,
            io_thread,
            io_restart_slot,
            pending_producer,
            measure_alive,
            watchdog_shutdown,
            watchdog_handle: Mutex::new(Some(watchdog_handle)),
            record_error_message: Arc::new(RwLock::new(None)),
            identity: Mutex::new(IdentityState::default()),
            pair_target: Arc::new(RwLock::new(String::new())),
            paired_pre_target: Arc::new(Mutex::new(None)),
            write_role: Mutex::new(None),
            push_overflow: Arc::new(AtomicU64::new(0)),
            oversized_drop: Arc::new(AtomicU64::new(0)), // B-125: JUCE oversized block drop 専用
            // B-054: 既定空 / 既定 false。enable_*_writes が io_thread と Arc 共有する。
            pre_name: Arc::new(RwLock::new(String::new())),
            record_acknowledged: Arc::new(AtomicBool::new(false)),
            preset_available: Arc::new(AtomicBool::new(false)),
            // B-108: 未ラッチで起動。enable_post_writes で io_thread_post と Arc 共有する。
            latched_pre: Arc::new(Mutex::new(None)),
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
    /// 降格（Os 以外）かつ Record 中なら強制 Watch（E-21 保険 / record.rs:141）。
    pub fn set_license(&self, abi: u8) {
        let code = match abi {
            LICENSE_OS => LICENSE_OS,
            LICENSE_SENSE => LICENSE_SENSE,
            _ => LICENSE_UNKNOWN,
        };
        self.license.store(code, Ordering::Relaxed);
        self.record_sm.enforce_license(license_from_abi(code));
    }

    /// 現ライセンスを取得。
    fn current_license(&self) -> License {
        license_from_abi(self.license.load(Ordering::Relaxed))
    }

    /// Record へ遷移を試みる。`License::Os` かつ Watch のとき `true`、それ以外 `false`。
    /// license 二重 gate（E-21）: `try_enter_record` が内部で `License::Os` を再判定する
    /// （record.rs:109-123）。`AlreadyRecording` / `LicenseDenied` は `false`。
    pub fn enter_record(&self) -> bool {
        self.record_sm.try_enter_record(self.current_license()).is_ok()
    }

    /// Record を終了し Watch へ戻す（無条件・冪等 / record.rs:132）。
    /// finalize は Measure Thread が自律実行・直近値を `session_summary` に保持するため
    /// ここでは呼ばない（B2 / finalize は Measure Thread のみ / engine.rs:161）。
    pub fn exit_record(&self) {
        self.record_sm.exit_record();
    }

    /// Record 中かどうか（read-only オブザーバ）。C ABI には公開しない（3a surface 厳守）。
    pub fn is_recording(&self) -> bool {
        self.record_sm.is_recording()
    }

    /// PRE の plugin_data 書込（Watch pre.json + Record frames/PSB）を有効化する（B-057 3b）。
    ///
    /// `kirin_measure::spawn_io_thread_pre`（io_thread_pre.rs:179）を engine 既存の共有
    /// 状態（record_sm / measure_result / signal_state / session_summary）に繋いで起動する。
    /// io_thread ロジック自体は kirin_measure のまま（呼ぶだけ）。filesystem 書込は全て
    /// その Rust スレッド内に閉じる（FFI は spawn と識別子注入のみ・B2 分離原則）。
    ///
    /// 前提・割り切り（3b）:
    /// - **`set_license` の後に呼ぶこと**。呼んだ時点の license を `Arc<License>` に
    ///   スナップショットする（A）。enable 後の license 変更は反映されない（3c）。
    /// - `instance_id` は `Uuid::new_v4` 生成（永続は 3c）。`project_uuid` / `daw_session_id`
    ///   は B-106 で FFI dylib の **role-scoped** 共有セル（PRE は [`shared_pre_project_hash_cell`] /
    ///   [`shared_pre_daw_session_id_cell`]）で **first-wins** 解決し（[`resolve_shared_id`]）、
    ///   `set_project_uuid` で kirin_measure セルへも反映、path のルートになる。同一 dylib の全
    ///   instance が同一棚に収束する（旧「単一 PRE/プロセス前提」を解消 / B-105 で判明した棚分裂の修正）。
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
        // project_uuid / daw_session_id は下記 B-106 共有セル解決（first-wins）で確定し、
        // identity に書き戻して get_identity が JUCE chunk へ返せるようにする。
        let (iid_str, name_str, project_hash, daw_uuid) = {
            let mut id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return,
            };
            if id.instance_id.is_empty() {
                id.instance_id = Uuid::new_v4().to_string();
            }
            // B-106: project_uuid / daw_session_id を FFI dylib 共有セルで first-wins 解決する。
            // chunk 復元値（id.*）を candidate に渡し、共有セルが空のときだけ seed（毎回生成・
            // 上書きを廃止）。同一 dylib の全 instance が同一棚に収束し、broadcast write 棚 ==
            // 全 io_thread scan 棚 が一致する。解決値を identity に書き戻し get_identity 経由で
            // chunk 永続。kirin_measure cell への反映（set_*）は現状維持（受信 filter / 既存読出と整合）。
            // PRE role セル（本番 KirinHyphaPRE.dylib スコープ相当）。
            let resolved_project = resolve_shared_id(shared_pre_project_hash_cell(), &id.project_uuid);
            let resolved_daw = resolve_shared_id(shared_pre_daw_session_id_cell(), &id.daw_session_uuid);
            id.project_uuid = resolved_project.clone();
            id.project_hash = resolved_project.clone();
            id.daw_session_uuid = resolved_daw.clone();
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
        let license = Arc::new(self.current_license());

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
            let push_overflow = Arc::clone(&self.push_overflow);
            let oversized_drop = Arc::clone(&self.oversized_drop); // B-125
            let sample_rate = self.sample_rate;
            Box::new(move || {
                let io_shutdown = Arc::new(AtomicBool::new(false));
                let handle = spawn_io_thread_pre(
                    Arc::clone(&instance_id),
                    project_hash.clone(),
                    daw_uuid.clone(), // _daw_session_id（io_thread_pre では未使用 / 復元値）
                    sample_rate,
                    Arc::clone(&record_sm),
                    Arc::clone(&recording),
                    Arc::clone(&record_acknowledged),
                    Arc::clone(&license),
                    Arc::clone(&measure_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&io_shutdown),
                    Arc::clone(&name),
                    Arc::clone(&record_error_message),
                    Arc::clone(&session_summary),
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
            // B-106: project_uuid / daw_session_id を FFI dylib 共有セルで first-wins 解決する
            // （enable_pre_writes と同一規約）。毎回生成・上書きを廃止し全 instance を同一棚に収束。
            let resolved_project = resolve_shared_id(shared_post_project_hash_cell(), &id.project_uuid);
            let resolved_daw = resolve_shared_id(shared_post_daw_session_id_cell(), &id.daw_session_uuid);
            id.project_uuid = resolved_project.clone();
            id.project_hash = resolved_project.clone();
            id.daw_session_uuid = resolved_daw.clone();
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
        // B-106: per-instance Arc::new を廃し、dylib 共有セルの clone を渡す。io_thread が
        // 毎 tick read_project_hash_arc で deref する scan 棚が全 POST で同一実体に収束する。
        let project_hash_arc = Arc::clone(shared_post_project_hash_cell());
        // B-054: preset_available は engine と共有（PresetAvailable LED が poll）。
        let preset_available = Arc::clone(&self.preset_available);
        // paired_pre_target は engine と共有（keep() が set → POST Record の linkage に焼く）。
        let paired_pre_target = Arc::clone(&self.paired_pre_target);
        let pair_label = Arc::new(Mutex::new(String::new()));
        // B-106: daw も共有セルの clone を渡す（per-instance Arc::new 全廃）。受信 filter は
        // read_daw_session_id_arc が kirin_measure cell を直読するが、enable で共有値を反映済。
        let daw_session_id = Arc::clone(shared_post_daw_session_id_cell());
        // pair_pre_name = self.pair_target（set_pair_target 優先 / 空なら identity.name で seed）。
        // io_thread と Arc 共有 → set_pair_target の live 反映 + keep() の select と同一値。
        if let Ok(mut pt) = self.pair_target.write() {
            if pt.is_empty() {
                *pt = name_str;
            }
        }
        let pair_pre_name = Arc::clone(&self.pair_target);
        // B-102: broadcast 受信 → 自身の keep/stop を発火する本物の closure（egui hypha_post と
        // 同一経路 / scope = 新↔新）。closure は Box 所有の engine を借用できないため、keep()/stop()
        // と同一の共有 free 関数 resolve_and_enter_keep / resolve_and_exit_stop を捕捉 Arc + enable
        // 値で呼ぶ。license は Arc<AtomicU8> を live 読み（keep と同一 gate）。args (pre/post) は
        // 各 POST が自分の pair_target を再選定するため無視する。
        let trigger_pair_resolution: kirin_measure::TriggerPairResolutionFn = {
            let record_sm = Arc::clone(&self.record_sm);
            let pair_target = Arc::clone(&self.pair_target);
            let paired = Arc::clone(&self.paired_pre_target);
            let license = Arc::clone(&self.license);
            let project_hash = cb_project_hash.clone();
            let post_iid = cb_post_iid.clone();
            let daw = cb_daw.clone();
            let latched = Arc::clone(&self.latched_pre);
            // B-127: broadcast 受信 keep も engine cap を通す。cap 到達通知の宛先 Arc を capture。
            let record_error_message = Arc::clone(&self.record_error_message);
            Arc::new(move |_pre: &str, _post: &str| {
                let lic = license_from_abi(license.load(Ordering::Relaxed));
                let _ = resolve_and_enter_keep(
                    lic, &record_sm, &pair_target, &paired, &project_hash, &post_iid, &daw,
                    &latched, &record_error_message,
                );
            })
        };
        let trigger_stop_resolution: kirin_measure::TriggerStopResolutionFn = {
            let record_sm = Arc::clone(&self.record_sm);
            let paired = Arc::clone(&self.paired_pre_target);
            let project_hash = cb_project_hash;
            let post_iid = cb_post_iid;
            Arc::new(move |_pre: &str, _post: &str| {
                resolve_and_exit_stop(&record_sm, &paired, &project_hash, &post_iid);
            })
        };
        // B-118 Phase 3 (③): engine 保持の Arc を共有（io が書き JUCE getter が読む / 世代跨ぎ継続）。
        let record_error_message = Arc::clone(&self.record_error_message);
        let pair_claimed_at = Arc::new(RwLock::new(0.0));
        let pair_release_notice = Arc::new(RwLock::new(None));
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
            let session_summary = Arc::clone(&self.session_summary);
            let push_overflow = Arc::clone(&self.push_overflow);
            let oversized_drop = Arc::clone(&self.oversized_drop); // B-125
            let latched_pre = Arc::clone(&self.latched_pre);
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
                    Arc::clone(&preset_available),
                    Arc::clone(&paired_pre_target),
                    Arc::clone(&io_shutdown),
                    Arc::clone(&pair_label),
                    Arc::clone(&daw_session_id),
                    Arc::clone(&pair_pre_name),
                    Arc::clone(&trigger_pair_resolution),
                    Arc::clone(&trigger_stop_resolution),
                    Arc::clone(&record_error_message),
                    Arc::clone(&pair_claimed_at),
                    Arc::clone(&pair_release_notice),
                    Arc::clone(&session_summary),
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

    /// 対 PRE 名（pair target）を設定する（B-061 3d-b / identity.name 結合を解く）。
    /// io_thread と Arc 共有のため `enable_post_writes` 後でも live に反映される。
    pub fn set_pair_target(&self, name: String) {
        // B-071: sanitize（ASCII graphic + space / max 16）で PRE 名と同一語彙に正規化する
        // 単一情報源（kirin_measure::sanitize_name）。select_target_pre は sanitized な PRE 名と
        // 照合するため、pair target も同じ正規化を通す。
        let sanitized = sanitize_name(&name);
        if let Ok(mut pt) = self.pair_target.write() {
            *pt = sanitized;
        }
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
    /// engine keep() は 12-limit を強制しない（egui UI 専管）ため、JUCE が keep 前に本 read-only getter で
    /// pre-check し "Maximum 12 pairs reached" を出すための露出。未 enable（project_hash 空）/ StoragePaths
    /// 不能は false（保守側＝ブロックしない）。
    pub fn record_exclusion_conflict(&self) -> bool {
        let project_hash = match self.identity.lock() {
            Ok(id) => id.project_hash.clone(),
            Err(_) => return false,
        };
        if project_hash.is_empty() {
            return false;
        }
        let base = match StoragePaths::default_macos() {
            Ok(p) => p.plugin_data_dir(),
            Err(_) => return false,
        };
        matches!(
            check_record_exclusion(&base, &project_hash),
            ExclusionResult::Conflict { .. }
        )
    }

    /// B-118 Phase 3 (③): io_thread 連続失敗時の固定文言（RecordError::ui_message / G-115-29）。
    /// None=通常（R-26 沈黙）。JUCE 永続 status label が Some の間表示する。
    pub fn record_error_message(&self) -> Option<String> {
        self.record_error_message.read().ok().and_then(|g| g.clone())
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
        self.paired_pre_target.lock().ok().and_then(|g| g.clone())
    }

    /// POST「Keep」: 厳格選定（select_target_pre）で対 PRE を一意決定し record_signal(pending)
    /// を書く（B-061 3d-b）。PRE 側 io_thread が autonomous に discover→ack する。
    /// `License::Os` かつ一意 PRE のとき `true`。選定 None（空名/不在/曖昧/Inactive/古t）/
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
        resolve_and_enter_keep(
            self.current_license(),
            &self.record_sm,
            &self.pair_target,
            &self.paired_pre_target,
            &project_hash,
            &post_iid,
            &daw,
            &self.latched_pre, // B-108: ラッチ済みならラッチ先を直接 target に使う
            &self.record_error_message, // B-127: cap 到達通知の宛先（両殻 B-118 表示）
        )
    }

    /// POST「All Keep」: all_keep broadcast を書いてから自身の keep を発火する（B-102 /
    /// egui ComboBox 先頭行と同一ライフサイクル: broadcast → self keep）。broadcast の棚パス
    /// （`project_hash`）と `daw_session_id` は B-106 で **FFI dylib 共有セルを呼び出し時に
    /// live-read** する（全 io_thread の scan 棚 = 共有 Arc と同一実体なので恒等的に一致）。
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
        // B-106: 棚パス・daw は共有セルを live-read（id.* と一致するが共有セルが単一正本）。
        let project_hash = read_shared_id(shared_post_project_hash_cell());
        let daw = read_shared_id(shared_post_daw_session_id_cell());
        if !project_hash.is_empty() {
            if let Ok(p) = StoragePaths::default_macos() {
                let _ = write_broadcast(&p.plugin_data_dir(), &project_hash, &post_iid, daw);
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
        resolve_and_exit_stop(&self.record_sm, &self.paired_pre_target, &project_hash, &post_iid);
    }

    /// POST「All Stop」: all_stop broadcast を書いてから自身の stop を発火する（B-102 /
    /// egui ComboBox 先頭行と同一ライフサイクル: broadcast → self stop）。
    pub fn stop_all(&self) {
        let post_iid = match self.identity.lock() {
            Ok(id) => id.instance_id.clone(),
            Err(_) => String::new(),
        };
        // B-106: 棚パス・daw は共有セルを live-read（keep_all と対称）。
        let project_hash = read_shared_id(shared_post_project_hash_cell());
        let daw = read_shared_id(shared_post_daw_session_id_cell());
        if !project_hash.is_empty() && !post_iid.is_empty() {
            if let Ok(p) = StoragePaths::default_macos() {
                let _ = write_stop_broadcast(
                    &p.plugin_data_dir(),
                    &project_hash,
                    &post_iid,
                    daw,
                );
            }
        }
        self.stop();
    }

    /// pair 候補（Active な PRE）を列挙する（B-102 / GUI ドロップダウン用・read-only）。
    /// `$TMPDIR/kirin/` 配下の Active PRE を走査し (instance_id, name) で返す。
    pub fn enumerate_pre_candidates(&self) -> Vec<(String, Option<String>)> {
        let kirin_root = std::env::temp_dir().join("kirin");
        enumerate_active_pre_pair_candidates(&kirin_root)
            .into_iter()
            .map(|c| (c.instance_id, c.name))
            .collect()
    }

    /// All Keep の「N ready」= pair 設定済の Active POST 数（B-102 / egui n_ready と同一・
    /// hypha_post editor.rs:938-944）。`enumerate_active_post_pair_candidates`（解決済み機構）を
    /// 再利用し `pair_pre_name.is_some()` を数える read-only カウント（GUI ラベル表示用）。
    pub fn count_keep_ready(&self) -> usize {
        let kirin_root = std::env::temp_dir().join("kirin");
        enumerate_active_post_pair_candidates(&kirin_root)
            .into_iter()
            .filter(|c| c.pair_pre_name.is_some())
            .count()
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
            let tag = if iid.is_empty() { None } else { Some(iid.as_str()) };
            id.project_uuid =
                kirin_measure::materialize_restore_field(&project_uuid, "ffi.set_identity.project_uuid", tag);
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
        let base = match StoragePaths::default_macos() {
            Ok(p) => p.plugin_data_dir(),
            Err(_) => return false,
        };
        append_annotation_to_latest(&base, &project_hash, &instance_id, role, memo)
            .unwrap_or(false)
    }

    /// interleaved f32 サンプルを供給する（Audio Thread 単独・RT-safe）。
    ///
    /// 責務はこの 2 つのみ:
    /// 1. heartbeat を進める（B-118/G-115-245: ~3s stall override 回避）。
    /// 2. interleaved サンプルを rtrb に push（満杯時は drop。本番 process() の
    ///    `let _ = producer.push(*sample)` と同挙動 / hypha_pre.rs:410）。
    ///
    /// stereo 前提（`num_channels` ≠ 2 のブロックは push しない＝防御ガード）。
    /// 殻が stereo bus のみ受理する（PluginProcessor::isBusesLayoutSupported）ため
    /// 非 stereo は到達しない。FFI 契約の防御として残す（到達時は無音 skip）。
    /// `interleaved.len()` は `num_frames * num_channels` を想定。
    pub fn push_samples(&self, interleaved: &[f32], num_channels: u32) {
        // (1) heartbeat は常に進める（空ブロック keepalive でも Active を維持できる）。
        let hb = self.heartbeat.fetch_add(1, Ordering::Relaxed);

        // B-118: watchdog が Measure Thread を再起動したとき pending_producer に新 Producer が来る。
        // 256 ブロック毎に try_lock（非ブロッキング）で取り出して ring_producer を差し替える
        // （hypha_pre:450 同型 / alloc・lock 待ちなし＝RT 安全）。
        if hb & 0xFF == 0 {
            if let Ok(mut slot) = self.pending_producer.try_lock() {
                if let Some(new_producer) = slot.take() {
                    // SAFETY: push_samples は Audio Thread 単独（SPSC）。Producer 差し替えも同契約内。
                    unsafe {
                        *self.ring_producer.get() = new_producer;
                    }
                }
            }
        }

        // (2) stereo 以外は既存 Rust の前提（2ch interleaved）に合わないため push しない。
        if num_channels != N_CHANNELS as u32 {
            return;
        }

        // SAFETY: push_samples は Audio Thread 単独という FFI 契約。Producer への
        // 排他アクセスは単一スレッドに限定される（SPSC）。
        let producer = unsafe { &mut *self.ring_producer.get() };
        for &s in interleaved {
            if producer.push(s).is_err() {
                self.push_overflow.fetch_add(1, Ordering::Relaxed);
            }
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
        self.oversized_drop.fetch_add(dropped_samples, Ordering::Relaxed);
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
    /// （sample_rate * RING_BUFFER_SECONDS * N_CHANNELS）と同一に算出するため、watchdog の
    /// Producer 差し替え後も不変。
    ///
    /// SAFETY: `ring_producer`(UnsafeCell<rtrb::Producer>) は SPSC 契約で「Audio/test 単独スレッド」
    /// からのみ触れる（struct の `unsafe impl Sync` 根拠と同一）。本メソッドも push_samples と同一
    /// スレッドから順次呼ばれ時間的に重ならない（&mut と & の同時生成なし）。`Producer::slots()` は
    /// `&self` の read-only で head（consumer 位置）を Acquire load するのみで、consumer 側の pop と
    /// 並行しても rtrb SPSC 設計上健全。
    #[doc(hidden)]
    pub fn __ring_drained_for_test(&self) -> bool {
        let capacity = (self.sample_rate as usize) * RING_BUFFER_SECONDS * N_CHANNELS;
        // SAFETY: 上記参照（SPSC・push_samples と同一スレッド・read-only slots()）。
        let producer = unsafe { &*self.ring_producer.get() };
        producer.slots() == capacity
    }
}

impl Drop for KirinHyphaEngine {
    fn drop(&mut self) {
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
    pub true_peak: f64,       // tp_recent: 直近 400ms（B-074）
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

/// `KirinDelta` — POST の Δ（C struct / B-061 3d-b）。各 double の「値なし」は NaN。
/// `mode`: 0=Active / 1=Stale / 2=NoPre。
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
/// Os 以外へ降格すると Record 中なら強制 Watch（E-21 保険）。
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

/// Record 遷移を試みる。`License::Os` かつ Watch のとき `true`、それ以外 `false`
/// （二重 gate / 冪等。AlreadyRecording / LicenseDenied は false）。
///
/// # Safety
/// `handle` は有効なハンドル。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enter_record(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).enter_record() }
    }))
    .unwrap_or(false)
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

/// B-118 Phase 3 (②): 現プロジェクトが Record 排他上限（12）に達しているか（read-only poller / UI Thread）。
/// 殻は keep 前に本 getter で pre-check し "Maximum 12 pairs reached" を出す（engine keep は非強制）。
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
                    (*out).dropped_samples =
                        (*handle).overflow_count().saturating_add((*handle).oversized_drop_count());
                }
                true
            }
            None => false,
        }
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
mod b106_shared_id_tests {
    //! B-106: project_hash / daw_session_id の「dylib 共有・first-wins・live-read」不変条件の
    //! 単体テスト。`resolve_shared_id` は共有セルを引数で受けるため（モジュール global static
    //! を触らない）、ローカル `Arc<RwLock<String>>` で「2 インスタンス相当（enable 2 回・2 つ目が
    //! 別値）」を模し、broadcast write 棚（resolve 戻り値）== 全 io_thread の scan 棚（共有 Arc
    //! clone の read）が一致し続けることを確認する。
    use super::{read_shared_id, resolve_shared_id};
    use std::sync::{Arc, RwLock};

    #[test]
    fn second_instance_adopts_shared_value_never_overwrites() {
        let cell = Arc::new(RwLock::new(String::new()));
        // instance A enable: 自分の chunk uuid で seed。
        let a = resolve_shared_id(&cell, "proj-A");
        // instance B enable: 別 chunk uuid を渡しても上書きせず A の値を採用。
        let b = resolve_shared_id(&cell, "proj-B");
        assert_eq!(a, "proj-A");
        assert_eq!(b, "proj-A", "2 つ目は共有値を採用（毎回生成・上書きの全廃）");
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
        assert_eq!(signal_state_to_abi(SignalState::Inactive), 0, "Inactive → 0");
        assert_eq!(signal_state_to_abi(SignalState::Active), 1, "Active → 1");
        assert_eq!(signal_state_to_abi(SignalState::Bypassed), 2, "Bypassed → 2");
    }
}
