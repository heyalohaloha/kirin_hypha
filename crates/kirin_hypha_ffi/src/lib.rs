//! kirin_hypha_ffi — Kirin Hypha JUCE 移植の C ABI ラッパ。
//!
//! 方式 B2: 検証済み Rust ランタイム(`kirin_measure`)を **無変更** で C ABI に包む。
//! C++/JUCE 側に DSP・計測ロジックを一切移さない（計測器は精度が製品そのもの）。
//!
//! # スコープ
//! - Phase 1: `create` / `set_signal_state` / `push_samples` / `poll_result`（RT メトリクス）/ `destroy`。
//! - Phase 3a（本コミット）: `set_license` / `enter_record` / `exit_record` を追加し、
//!   `poll_session`(LUFS-I/LRA/max_true_peak) を実体化。SessionSummary は `engine.finalize()`
//!   由来で Record 中にのみ成立する量で、Measure Thread が **自律的に** finalize して
//!   `session_summary` を充填する（measure_thread.rs:290-295）。FFI は RecordStateMachine を
//!   flip するだけ（exit で finalize を呼ばない＝finalize は Measure Thread のみ / engine.rs:161）。
//!   `poll_session` の ABI signature は Phase 1 と不変（Record 前は false のまま）。
//! - まだ触れない（3b 以降）: plugin_data/preset export / state chunk / PRE-POST ペアリング /
//!   IO(pre|post.json) / Note。これらに依存する関数を足さない。
//!
//! # スレッドモデル（本番 hypha_pre/post と同一の入口を使う）
//! `create` は本番の実運用入口 `kirin_measure::spawn_measure_thread`(measure_thread.rs:59) で
//! Measure Thread を起動する。IO Thread / Watchdog は立てない（RT 計測に不要）。
//! - `push_samples`: **Audio Thread 単独**。rtrb Producer への lock-free push + heartbeat++。
//!   アロケーション/lock/syscall なし（RT-safe）。Record 中も読むだけ（R-12）。
//! - `poll_result` / `poll_session` : **UI Thread**。`try_lock`（非ブロッキング）。
//!
//! ## heartbeat（必須配線）
//! Measure Thread は heartbeat が ~200ms 変化しないと signal_state を Inactive に上書きし結果を
//! clear する(measure_thread.rs:160-169)。本番は host の `process()` が毎回 `heartbeat.fetch_add(1)`
//! していた(hypha_pre.rs:390)。本 FFI では **`push_samples` が heartbeat を進める**。

use std::cell::UnsafeCell;
use std::ffi::CStr;
use std::os::raw::{c_char, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, AtomicU64, AtomicU8, AtomicBool, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::JoinHandle;

use uuid::Uuid;

use kirin_measure::engine::SessionSummary;
use kirin_measure::{
    append_annotation_to_latest, can_write_plugin_data, mark_released, process_project_hash,
    select_target_pre, set_daw_session_id, set_project_uuid, spawn_io_thread_post,
    spawn_io_thread_pre, spawn_measure_thread, store_signal_state, write_pending, DeltaMode,
    DeltaResult, License, MeasureResult, PluginDataRole, PsbSummary, RecordStateMachine,
    SignalState, StoragePaths, N_CHANNELS, RING_BUFFER_SECONDS,
};

/// state chunk 往復する識別子（方式A: JUCE が chunk bytes を所有・FFI は文字列 get/set のみ）。
/// `project_hash` は派生値（`process_project_hash` = project_uuid セル値）で永続対象外。
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

/// PRE/POST の io_thread ハンドル＋停止フラグ。`enable_pre_writes`（PRE）/
/// `enable_post_writes`（POST）が spawn し、Drop で shutdown→join する。
/// PRE/POST は同一 engine では排他（片方のみ enable する想定・冪等）。
struct IoThreadHandle {
    shutdown: Arc<AtomicBool>,
    handle: JoinHandle<()>,
}

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
    /// Record 状態機械。`enter_record`/`exit_record` で flip し、Measure Thread が
    /// `is_recording()` を見て自律 finalize する（Phase 3a で実配線）。
    record_sm: Arc<RecordStateMachine>,
    /// 現ライセンス（C ABI コード: 0=Os 1=Sense 2=Unknown）。`enter_record` の
    /// 二重 gate（E-21）に使う。既定は Unknown（Record 不可）。
    license: AtomicU8,
    /// `spawn_io_thread_pre` に渡す入力サンプルレート（create 時に保持）。
    sample_rate: u32,
    /// PRE/POST io_thread（B-057 3b / B-060 3d-a）。`enable_pre_writes` or
    /// `enable_post_writes` で 1 度だけ起動。内部可変のため `Mutex`（C ABI は `&self` 経由）。
    io_thread: Mutex<Option<IoThreadHandle>>,
    /// state chunk 往復する識別子（B-058 3c / 方式A）。`set_identity` で復元値を入れ、
    /// 未設定なら `enable_pre_writes` が生成する。`get_identity` で JUCE が読み戻す。
    identity: Mutex<IdentityState>,
    /// POST の対 PRE 名（B-061 3d-b）。`set_pair_target` で設定（identity.name 結合を解く）。
    /// `enable_post_writes` 時に空なら identity.name で seed し、io_thread と Arc 共有する
    /// （run_tick の select / keep() の write_pending target 解決に使う・live 反映）。
    pair_target: Arc<RwLock<String>>,
    /// Measure Thread の JoinHandle（drop で join）。
    measure_handle: Option<JoinHandle<()>>,
    /// ring 満杯で push できなかった回数（§8 RT-safety 検証用 / FFI 側のみ）。
    push_overflow: AtomicU64,
}

// SAFETY: `ring_producer`(UnsafeCell<rtrb::Producer>) は push_samples からのみ触れ、
// その push_samples は「Audio Thread 単独」という FFI 契約で単一スレッドアクセスに限定される。
// 他の全フィールドは Arc<Mutex>/Arc<Atomic>/AtomicU64/AtomicU8 で Sync。よって
// `&KirinHyphaEngine` を Audio/UI 2 スレッドで共有しても（契約を守る限り）健全。
unsafe impl Sync for KirinHyphaEngine {}
// SAFETY: 内部状態はスレッド間移動可能（Producer/Arc は Send）。
unsafe impl Send for KirinHyphaEngine {}

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
        // 実 RecordStateMachine（既定 Watch）。FFI が enter/exit で flip する。
        let record_sm = Arc::new(RecordStateMachine::new());

        let measure_handle = spawn_measure_thread(
            consumer,
            sample_rate,
            Arc::clone(&measure_result),
            Arc::clone(&signal_state),
            Arc::clone(&shutdown),
            Arc::clone(&heartbeat),
            Arc::clone(&record_sm),
            Arc::clone(&session_summary),
        );

        Self {
            ring_producer: UnsafeCell::new(producer),
            measure_result,
            delta_result,
            session_summary,
            signal_state,
            shutdown,
            heartbeat,
            record_sm,
            // 既定 Unknown（set_license(Os) されるまで Record 不可・安全側）。
            license: AtomicU8::new(LICENSE_UNKNOWN),
            sample_rate,
            io_thread: Mutex::new(None),
            identity: Mutex::new(IdentityState::default()),
            pair_target: Arc::new(RwLock::new(String::new())),
            measure_handle: Some(measure_handle),
            push_overflow: AtomicU64::new(0),
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
    /// - `instance_id` / `project_uuid` は `Uuid::new_v4` 生成（永続は 3c）。`project_uuid`
    ///   は `set_project_uuid` で **プロセスグローバル** セルに反映され `process_project_hash`
    ///   で path のルートになる（単一 PRE/プロセス前提・C / multi-instance は番人案件）。
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

        // 識別子: set_identity 済みなら復元値を使い、未設定はここで生成（3b フォールバック）。
        // project_uuid → プロセスグローバルセル → project_hash（= project_uuid）を確定。
        // 確定値を identity に書き戻し、get_identity が JUCE chunk へ返せるようにする。
        let (iid_str, name_str, project_hash, daw_uuid) = {
            let mut id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return,
            };
            if id.instance_id.is_empty() {
                id.instance_id = Uuid::new_v4().to_string();
            }
            if id.project_uuid.is_empty() {
                id.project_uuid = Uuid::new_v4().to_string();
            }
            set_project_uuid(id.project_uuid.clone());
            id.project_hash = process_project_hash(); // = project_uuid（cell 値）
            if !id.daw_session_uuid.is_empty() {
                set_daw_session_id(id.daw_session_uuid.clone());
            }
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
        let record_acknowledged = Arc::new(AtomicBool::new(false));
        let name = Arc::new(RwLock::new(name_str)); // 空 → instance_id 先頭8字 fallback
        let record_error_message = Arc::new(RwLock::new(None));
        // A: enable 時点の license をスナップショット（immutable）。
        let license = Arc::new(self.current_license());
        let io_shutdown = Arc::new(AtomicBool::new(false));

        let handle = spawn_io_thread_pre(
            instance_id,
            project_hash,
            daw_uuid, // _daw_session_id（io_thread_pre では未使用 / 念のため復元値を渡す）
            self.sample_rate,
            Arc::clone(&self.record_sm),
            recording,
            record_acknowledged,
            license,
            Arc::clone(&self.measure_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&io_shutdown),
            name,
            record_error_message,
            Arc::clone(&self.session_summary),
        );

        *slot = Some(IoThreadHandle { shutdown: io_shutdown, handle });
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

        let (iid_str, name_str, project_hash, daw_uuid) = {
            let mut id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return,
            };
            if id.instance_id.is_empty() {
                id.instance_id = Uuid::new_v4().to_string();
            }
            if id.project_uuid.is_empty() {
                id.project_uuid = Uuid::new_v4().to_string();
            }
            set_project_uuid(id.project_uuid.clone());
            id.project_hash = process_project_hash();
            if !id.daw_session_uuid.is_empty() {
                set_daw_session_id(id.daw_session_uuid.clone());
            }
            (
                id.instance_id.clone(),
                id.name.clone(),
                id.project_hash.clone(),
                id.daw_session_uuid.clone(),
            )
        };

        // POST 固有の共有 Arc（hypha_post params と同型）。
        let instance_id = Arc::new(RwLock::new(iid_str));
        let project_hash_arc = Arc::new(RwLock::new(project_hash)); // POST は Arc<RwLock<String>>
        let preset_available = Arc::new(AtomicBool::new(false));
        let paired_pre_target = Arc::new(Mutex::new(None));
        let pair_label = Arc::new(Mutex::new(String::new()));
        let daw_session_id = Arc::new(RwLock::new(daw_uuid));
        // pair_pre_name = self.pair_target（set_pair_target 優先 / 空なら identity.name で seed）。
        // io_thread と Arc 共有 → set_pair_target の live 反映 + keep() の select と同一値。
        if let Ok(mut pt) = self.pair_target.write() {
            if pt.is_empty() {
                *pt = name_str;
            }
        }
        let pair_pre_name = Arc::clone(&self.pair_target);
        // 3d-a: Keep/Stop 解決は配線しない（broadcast 受信時 no-op）。write_pending は 3d-b。
        let trigger_pair_resolution: kirin_measure::TriggerPairResolutionFn =
            Arc::new(|_pre: &str, _post: &str| {});
        let trigger_stop_resolution: kirin_measure::TriggerStopResolutionFn =
            Arc::new(|_pre: &str, _post: &str| {});
        let record_error_message = Arc::new(RwLock::new(None));
        let pair_claimed_at = Arc::new(RwLock::new(0.0));
        let pair_release_notice = Arc::new(RwLock::new(None));
        let io_shutdown = Arc::new(AtomicBool::new(false));

        let handle = spawn_io_thread_post(
            instance_id,
            project_hash_arc,
            self.sample_rate,
            Arc::clone(&self.record_sm),
            Arc::clone(&self.measure_result),
            Arc::clone(&self.delta_result),
            Arc::clone(&self.signal_state),
            preset_available,
            paired_pre_target,
            Arc::clone(&io_shutdown),
            pair_label,
            daw_session_id,
            pair_pre_name,
            trigger_pair_resolution,
            trigger_stop_resolution,
            record_error_message,
            pair_claimed_at,
            pair_release_notice,
            Arc::clone(&self.session_summary),
        );

        *slot = Some(IoThreadHandle { shutdown: io_shutdown, handle });
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
        if let Ok(mut pt) = self.pair_target.write() {
            *pt = name;
        }
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
        let kirin_root = std::env::temp_dir().join("kirin");
        let pair = self.pair_target.read().map(|g| g.clone()).unwrap_or_default();
        let Some(sel) = select_target_pre(&kirin_root, &pair) else {
            return false; // No PRE Paired（厳格: 空名/不在/曖昧/Inactive/古t）
        };
        // license 二重 gate（record.rs try_enter_record / E-21）。
        if self.record_sm.try_enter_record(self.current_license()).is_err() {
            return false; // 非 Os / AlreadyRecording
        }
        let base = match StoragePaths::default_macos() {
            Ok(p) => p.plugin_data_dir(),
            Err(_) => return false,
        };
        // target_pre_instance_id = 選定 PRE の instance_id。PRE が自宛て signal を発見し ack する。
        write_pending(&base, &project_hash, &post_iid, sel.instance_id, daw).is_ok()
    }

    /// POST「Stop」: pair を解除（record_signal released）し Watch へ戻す（B-061 3d-b）。
    /// PRE 側は released を検出して自身も Record を抜ける（io_thread_pre）。
    pub fn stop(&self) {
        self.record_sm.exit_record();
        let (project_hash, post_iid) = {
            let id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return,
            };
            if id.project_hash.is_empty() || id.instance_id.is_empty() {
                return;
            }
            (id.project_hash.clone(), id.instance_id.clone())
        };
        if let Ok(p) = StoragePaths::default_macos() {
            let _ = mark_released(&p.plugin_data_dir(), &project_hash, &post_iid);
        }
    }

    /// state chunk から復元した識別子を設定する（方式A / B-058 3c）。
    /// **`enable_pre_writes` の前**に呼ぶこと（復元順: create→set_license→set_identity→enable）。
    /// 空文字を渡したキーは `enable_pre_writes` で生成される（instance_id / project_uuid）。
    pub fn set_identity(
        &self,
        instance_id: String,
        project_uuid: String,
        daw_session_uuid: String,
        name: String,
    ) {
        if let Ok(mut id) = self.identity.lock() {
            id.instance_id = instance_id;
            id.project_uuid = project_uuid;
            id.daw_session_uuid = daw_session_uuid;
            id.name = name;
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
        append_annotation_to_latest(&base, &project_hash, &instance_id, PluginDataRole::Pre, memo)
            .unwrap_or(false)
    }

    /// interleaved f32 サンプルを供給する（Audio Thread 単独・RT-safe）。
    ///
    /// 責務はこの 2 つのみ:
    /// 1. heartbeat を進める（200ms stall override 回避）。
    /// 2. interleaved サンプルを rtrb に push（満杯時は drop。本番 process() の
    ///    `let _ = producer.push(*sample)` と同挙動 / hypha_pre.rs:410）。
    ///
    /// stereo 前提（`num_channels` ≠ 2 のブロックは push しない＝R-28 機能的沈黙）。
    /// `interleaved.len()` は `num_frames * num_channels` を想定。
    pub fn push_samples(&self, interleaved: &[f32], num_channels: u32) {
        // (1) heartbeat は常に進める（空ブロック keepalive でも Active を維持できる）。
        self.heartbeat.fetch_add(1, Ordering::Relaxed);

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
}

impl Drop for KirinHyphaEngine {
    fn drop(&mut self) {
        // PRE/POST io_thread を先に止める（PRE: status=closed flush + pre.json/instance dir
        // 後始末 / POST: post.json/instance dir 後始末・record_signal 削除を自前で行う）。
        // 共有 Arc を読むため Measure Thread より先に join する。
        if let Ok(mut slot) = self.io_thread.lock() {
            if let Some(io) = slot.take() {
                io.shutdown.store(true, Ordering::Relaxed);
                let _ = io.handle.join();
            }
        }
        self.shutdown.store(true, Ordering::Relaxed);
        if let Some(h) = self.measure_handle.take() {
            let _ = h.join();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C ABI（§3 の契約。Option<f64> は NaN sentinel で表す）
// ─────────────────────────────────────────────────────────────────────────────

/// `KirinMeasureResult` — RT 計測結果（C struct）。Option は NaN で表す。
#[repr(C)]
pub struct KirinMeasureResult {
    pub lufs_m: f64,
    pub true_peak: f64,
    pub crest: f64,
    pub psr: f64,
    pub n_prime_total: f64,
    pub sharpness: f64,
    pub psb_low: f64,
    pub psb_mid: f64,
    pub psb_high: f64,
    pub n_prime: [f64; 20],
    pub psb_bark: [f64; 20],
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
                unsafe { *out = to_c_result(&r) };
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
