mod editor;

use kirin_measure::{
    daw_session_id, delete_broadcast, delete_signal, delete_stop_broadcast,
    ensure_legacy_cleanup_done, load_installation_id_safe, load_license_safe, peek_project_uuid,
    process_project_hash, sanitize_name, set_daw_session_id, set_project_uuid,
    spawn_io_thread_post, spawn_measure_thread, spawn_watchdog, store_signal_state, DeltaResult,
    License, MeasureResult, RecordStateMachine, SignalState, StoragePaths,
    TriggerPairResolutionFn, TriggerStopResolutionFn, WatchdogParams, N_CHANNELS,
    RING_BUFFER_SECONDS,
};
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::JoinHandle;
use uuid::Uuid;

/// Kirin Hypha POST — マスタリングチェイン後段計測プラグイン。
///
/// # 4層隔離
/// ```text
/// Audio Thread    — process(): バッファコピーのみ（R-12）。絶対に止まらない
/// Measure Thread  — 4項目計測（ebur128 + スライディングウィンドウ）
/// IO Thread       — PRE ファイル読み + Δ算出 + post.json アトミック書き込み
/// Watchdog Thread — Measure / IO の is_finished() 監視・自動再起動（T-8）
/// ```
///
/// # B-020 / γ-3 後の識別子モデル
/// - `params.instance_id`     : 永続 UUID（`#[persist]`）。plugin instance ごと
/// - `params.project_uuid`    : 永続 UUID（`#[persist]`）。プロジェクト chunk 共有
/// - `params.daw_session_uuid`: 永続 UUID（`#[persist]`）。プロジェクト chunk 共有
/// - `project_hash` field     : `process_project_hash()` 経由で cell から読む chunk-persistent 値
/// - `daw_session_id` field   : `daw_session_id()` 経由で cell から読む chunk-persistent 値
///
/// # PRE/POST sync
/// POST の `initialize()` は [`sync_project_uuid_from_pre`] でセルから PRE 側の
/// project_uuid を取り込み、自身の `params.project_uuid` を上書きする。これに
/// より「同一プロジェクトに新規 POST を後から挿入」したケースでも path が
/// PRE と一致する。順序依存（PRE が先に initialize 済の前提）は §S2 の既知制約。
pub struct HyphaPost {
    params: Arc<HyphaPostParams>,
    editor_state: Arc<EguiState>,

    /// プロセス単位 `project_hash`（plugin_data path のルートセグメント）。
    /// §4-5 Step 1: B-022 段階 1 instance_id 同位相で `Arc<RwLock<String>>` 化。
    /// editor() / initialize() の snapshot timing 差で生じていた divergence
    /// (§4-4 R-9) を構造的に解消する。各 use site では `read_project_hash_arc`
    /// で lazy-read snapshot を取って `&str` 引数として下流に渡す。
    project_hash: Arc<RwLock<String>>,
    /// プロセス単位 `daw_session_id`（record_signal content の cross-process 防壁）。
    /// §4-5 Step 1: 同位相で `Arc<RwLock<String>>` 化 (`read_daw_session_id_arc`)。
    daw_session_id: Arc<RwLock<String>>,

    ring_producer: Option<rtrb::Producer<f32>>,
    measure_result: Arc<Mutex<MeasureResult>>,
    delta_result: Arc<Mutex<DeltaResult>>,

    measure_shutdown: Arc<AtomicBool>,
    io_shutdown: Arc<AtomicBool>,

    // ── T-8: Watchdog ────────────────────────────────────────────────
    watchdog_shutdown: Arc<AtomicBool>,
    watchdog_handle: Option<JoinHandle<()>>,
    pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    measure_alive: Arc<AtomicBool>,
    process_counter: u32,

    // ── SignalState（SS-1）────────────────────────────────────────────
    signal_state: Arc<AtomicU8>,

    // ── Heartbeat（SS-3 代替: process() 停止検出）────────────────────
    heartbeat: Arc<AtomicU32>,

    // ── Record モード ─────────────────────────────────────────────────
    record_sm: Arc<RecordStateMachine>,
    record_acknowledged: Arc<AtomicBool>,
    /// pair_label（POST GUI に表示。Record 中は "pair: PRE_xxxxxxxx"、Watch 中は空文字）
    pair_label: Arc<Mutex<String>>,
    /// trigger_keep が選定した PRE instance_id（v1.2 (a) cross-instance pair 復元キー）。
    /// Watch 中は None、Keep 成功直後に Some、Stop / 失敗で None。POST IO Thread が
    /// Record 開始時に読み出して plugin_data の `paired_pre_instance_id` に書き込む。
    paired_pre_target: Arc<Mutex<Option<String>>>,
    license: Arc<License>,

    /// preset/*.json が 1 件以上存在するか。
    preset_available: Arc<AtomicBool>,

    // ── T-E/T-F 追加（guardian_77 v3 §9 / §10）────────────────────────
    installation_id: Arc<String>,
    playback_pos_samples: Arc<AtomicI64>,
    playback_sample_rate: Arc<AtomicU32>,
}

#[derive(Params)]
struct HyphaPostParams {
    /// DAW バイパスパラメータ（SS-3: nih-plug `kIsBypass` フラグ）。
    #[id = "bypass"]
    pub bypass: BoolParam,

    /// プロジェクト保存時に永続化される instance UUID（A-3 修正後）。
    /// 初回挿入時に Default::default() で生成、project 再オープン時には
    /// nih-plug の persist 機構が同じ値を復元する。Watch / Record / plugin_data
    /// の path 構築と record_signal の target_pre_instance_id 識別に使う。
    ///
    /// B-022 段階 1: chunk-restore タイミングで host が `set_state` を発行した
    /// 直後に editor / io_thread の snapshot が陳腐化する問題への対処として
    /// `Arc<RwLock<String>>` 型で `editor::PostEditorArgs` / `spawn_io_thread_post`
    /// に共有する。`#[persist]` は `Arc<RwLock<String>>` も等価に扱う
    /// （nih-plug `params/persist.rs` の `impl_persistent_arc!` 経由で
    /// chunk JSON 形式は変わらない / 既存プロジェクト互換）。
    #[persist = "instance_id"]
    pub instance_id: Arc<RwLock<String>>,

    /// プロジェクト chunk に永続化される project UUID（B-020 / γ-3）。
    /// PRE 側の値を `sync_project_uuid_from_pre()` でセルから取り込んだ後、
    /// 次回 project save で同じ値が chunk に書かれる。
    #[persist = "project_uuid"]
    pub project_uuid: RwLock<String>,

    /// プロジェクト chunk に永続化される daw session UUID（B-020 / γ-3）。
    /// `record_signal.json` の content に同梱され、別 DAW プロセス起源の
    /// signal を PRE が誤って ack することを防ぐ cross-process 防壁。
    #[persist = "daw_session_uuid"]
    pub daw_session_uuid: RwLock<String>,

    /// B-027 段階 2: pair PRE Name (G-115-43 論点 2 (b))。
    /// 同 project_uuid 配下に複数 PRE が存在する場合の explicit pair 指定。
    /// ASCII 0x20-0x7E のみ / 16 文字以内 / 空文字許容 (空 = filter 適用なし →
    /// 従来 `pick_closest_pre` 単独経路 = B-027 段階 1 受入維持)。
    /// PRE 側 `name` (B-023 段階 1 / `params.name`) と完全対称構造。
    /// POST 自身の identity ではなく、どの PRE と pair したいかを示す filter 入力
    /// (B-023 論点 4 (c) POST 独自命名禁止と整合)。
    #[persist = "pair_pre_name"]
    pub pair_pre_name: Arc<RwLock<String>>,
}

impl Default for HyphaPostParams {
    fn default() -> Self {
        Self {
            bypass: BoolParam::new("Bypass", false)
                .make_bypass()
                .with_value_to_string(formatters::v2s_bool_bypass())
                .with_string_to_value(formatters::s2v_bool_bypass()),
            instance_id: Arc::new(RwLock::new(Uuid::new_v4().to_string())),
            project_uuid: RwLock::new(Uuid::new_v4().to_string()),
            daw_session_uuid: RwLock::new(Uuid::new_v4().to_string()),
            pair_pre_name: Arc::new(RwLock::new(String::new())),
        }
    }
}

impl Default for HyphaPost {
    fn default() -> Self {
        // 起動時 1 回限りの旧構造 cleanup（OnceLock 内側で flag-guarded）。
        ensure_legacy_cleanup_done();

        Self {
            params: Arc::new(HyphaPostParams::default()),
            editor_state: EguiState::from_size(300, 200),
            project_hash: Arc::new(RwLock::new(process_project_hash())),
            daw_session_id: Arc::new(RwLock::new(daw_session_id())),
            ring_producer: None,
            measure_result: Arc::new(Mutex::new(MeasureResult::default())),
            delta_result: Arc::new(Mutex::new(DeltaResult::default())),
            measure_shutdown: Arc::new(AtomicBool::new(false)),
            io_shutdown: Arc::new(AtomicBool::new(false)),
            watchdog_shutdown: Arc::new(AtomicBool::new(false)),
            watchdog_handle: None,
            pending_producer: Arc::new(Mutex::new(None)),
            measure_alive: Arc::new(AtomicBool::new(true)),
            process_counter: 0,
            signal_state: Arc::new(AtomicU8::new(SignalState::Inactive as u8)),
            heartbeat: Arc::new(AtomicU32::new(0)),
            record_sm: Arc::new(RecordStateMachine::new()),
            record_acknowledged: Arc::new(AtomicBool::new(false)),
            pair_label: Arc::new(Mutex::new(String::new())),
            paired_pre_target: Arc::new(Mutex::new(None)),
            license: Arc::new(load_license_safe()),
            preset_available: Arc::new(AtomicBool::new(false)),
            installation_id: Arc::new(load_installation_id_or_empty()),
            playback_pos_samples: Arc::new(AtomicI64::new(i64::MIN)),
            playback_sample_rate: Arc::new(AtomicU32::new(0)),
        }
    }
}

/// Best-effort read of the installation_id for T-E proposals filtering.
fn load_installation_id_or_empty() -> String {
    load_installation_id_safe().unwrap_or_default()
}

/// `Arc<RwLock<String>>` から現在値を lazy-read（panic-safe）。
///
/// B-022 段階 1: editor / io_thread 側で snapshot を取らず、毎 use site で
/// 最新値を取得する目的で使う。`params.instance_id` が `Arc<RwLock<String>>` 化
/// されたことで、editor() 時点 / initialize() 時点に String snapshot を取らず
/// 共有 Arc を渡し、各 use site で `read_instance_id_arc(&arc)` を呼ぶ。
pub(crate) fn read_instance_id_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// `Arc<RwLock<String>>` から `project_hash` を lazy-read（panic-safe）。
///
/// §4-5 Step 1: `read_instance_id_arc` と同位相。`HyphaPost.project_hash` を Arc 化した
/// ことで、editor() snapshot と initialize() snapshot の divergence
/// (§4-4 R-9 主因 a) を構造的に解消する。各 use site で本関数を呼んで snapshot を
/// 取り、`&str` 引数として下流関数 (`trigger_keep_internal` 等) に渡す。
pub(crate) fn read_project_hash_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// `daw_session_id` の現在値を取得（panic-safe）。
///
/// §4-5 Step 5 (instance scope divergence 是正):
/// 引数 `_arc` は構造維持のため受け取るが内部では使わず、`kirin_measure::daw_session_id()`
/// 経由で **process scope cell** を直読みする。`HyphaPost.daw_session_id` Arc field
/// は initialize() 時点の cell 値を凍結するため、複数 plugin instance 環境では
/// 後発 instance の `set_daw_session_id` 上書きを反映できず、6 POST で daw_session_id
/// が divergence していた (Hpha0504 / sub-tick cross-process filter で全件 skip)。
///
/// `daw_session_id()` cell は process scope (lib.rs:145-148 daw_session_id_cell の
/// static OnceLock) のため全 plugin instance で同一値を返し、broadcast filter
/// (`broadcast.daw_session_id != snapshot`) が POST 同士で正しくマッチする。
///
/// callsite 引数の Arc 構造は維持 (editor / io_thread_post / lib.rs spawn closure 不変 /
/// Pass 15 最小スコープ)。`§4-5 Step 1` の Arc 化はそのまま残し、本関数のみ
/// 「Arc 凍結値を見ない」semantics に切替える 1 関数 body 修正。
pub(crate) fn read_daw_session_id_arc(_arc: &Arc<RwLock<String>>) -> String {
    daw_session_id()
}

/// `RwLock<String>` 永続フィールドから現在値を読む（panic-safe）。
fn read_persisted_string(field: &RwLock<String>) -> String {
    field.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// POST 側の `params.project_uuid` を PRE が cell に書いた値に合わせる。
///
/// 呼び出しタイミング: POST `initialize()` の冒頭。PRE が先に initialize 済
/// であれば cell に PRE の chunk-persistent UUID が入っている。POST 側の
/// `params.project_uuid`（chunk-restored または Default 生成値）と異なれば
/// cell 値を採用し、`params.project_uuid` を上書きする（次回 project save で
/// PRE と同じ値が chunk に書かれる）。
///
/// 順序依存 (POST が先に initialize されると cell が空 → POST が cell をセット
/// → PRE が後で上書き) は §S2 既知制約として受容。
///
/// `peek_project_uuid()` を使い lazy fallback を避ける。POST 側で cell を
/// 自動初期化すると、PRE 後発時に cell が POST 値に「占拠」されてしまい、
/// PRE の `set_project_uuid()` で上書きされても POST はもう adopt しない。
fn sync_project_uuid_from_pre(params: &HyphaPostParams) {
    let cell_value = peek_project_uuid();
    if cell_value.is_empty() {
        return;
    }
    let own_value = read_persisted_string(&params.project_uuid);
    if own_value != cell_value {
        if let Ok(mut g) = params.project_uuid.write() {
            *g = cell_value;
        }
    }
}

impl Drop for HyphaPost {
    fn drop(&mut self) {
        self.record_sm.exit_record();

        self.watchdog_shutdown.store(true, Ordering::Relaxed);
        self.measure_shutdown.store(true, Ordering::Relaxed);
        self.io_shutdown.store(true, Ordering::Relaxed);

        if let Some(h) = self.watchdog_handle.take() {
            let _ = h.join();
        }

        // B-027 段階 3-B α-7 / Group 2 統合点 #3 (Gap-6 局所対処):
        // POST 消失時 (DAW 終了 / instance unload / Undo) に self_post_iid の
        // record_signal/{POST_iid}.json を削除する。POST 自身が writer =
        // cleanup 責任を持つ (cdylib 越境通信媒体の lifecycle 管理原則 /
        // 設計判断 #5)。
        //
        // 順序: record_sm.exit_record() → shutdown flags → watchdog join →
        //       delete_signal の順。watchdog join 内で IO Thread terminate が
        //       走り、統合点 #4 でも delete_signal が呼ばれるため file は既に
        //       消えている可能性が高いが、delete_signal は冪等 (NotFound→Ok /
        //       record_signal.rs:289-301) で安全。両方ある方が watchdog 経路
        //       から外れた直接 Drop ケースをカバーできる。
        //
        // 失敗時 warn のみ (設計判断 #8): Drop 内 panic は abort のため避ける。
        let instance_id_owned = read_instance_id_arc(&self.params.instance_id);
        // §4-5 Step 1: project_hash も lazy-read (Drop 時点の最新 cell 値を反映)。
        let project_hash_owned = read_project_hash_arc(&self.project_hash);
        match StoragePaths::default_macos() {
            Ok(paths) => {
                match delete_signal(
                    &paths.plugin_data_dir(),
                    &project_hash_owned,
                    &instance_id_owned,
                ) {
                    Ok(()) => log::info!(
                        "[POST cleanup #3] record_signal deleted: {}",
                        instance_id_owned
                    ),
                    Err(e) => log::warn!(
                        "[POST cleanup #3] delete_signal failed: {:?}",
                        e
                    ),
                }

                // B-027 段階 3-B α-7-4-D / Step 12-B 統合点 #3 (DEV INBOX §9-3 / S117 判断 2 (P)):
                // originator として配置した all_keep_signal/{POST_iid}.json broadcast を削除。
                // delete_broadcast は冪等 (NotFound→Ok)。統合点 #2 (trigger_stop) / #4 (IO Thread
                // terminate) と重複呼出されても安全。失敗時 warn のみ (Drop 内 panic は abort /
                // 設計判断 #8 / 既存 delete_signal と同規範)。
                match delete_broadcast(
                    &paths.plugin_data_dir(),
                    &project_hash_owned,
                    &instance_id_owned,
                ) {
                    Ok(()) => log::info!(
                        "[POST drop #3 broadcast] delete_broadcast succeeded: instance={}",
                        instance_id_owned
                    ),
                    Err(e) => log::warn!(
                        "[POST drop #3 broadcast] delete_broadcast failed: {:?}",
                        e
                    ),
                }

                // α-7' All Stop: own all_stop_signal/{POST_iid}.json も並列削除。
                match delete_stop_broadcast(
                    &paths.plugin_data_dir(),
                    &project_hash_owned,
                    &instance_id_owned,
                ) {
                    Ok(()) => log::info!(
                        "[POST drop #3 stop_broadcast] delete_stop_broadcast succeeded: instance={}",
                        instance_id_owned
                    ),
                    Err(e) => log::warn!(
                        "[POST drop #3 stop_broadcast] delete_stop_broadcast failed: {:?}",
                        e
                    ),
                }
            }
            Err(e) => log::warn!(
                "[POST cleanup #3] StoragePaths error: {:?}",
                e
            ),
        }
    }
}

impl Plugin for HyphaPost {
    const NAME: &'static str = "Kirin Hypha POST";
    const VENDOR: &'static str = "Kirin";
    const URL: &'static str = "";
    const EMAIL: &'static str = "";
    const VERSION: &'static str = env!("CARGO_PKG_VERSION");

    const AUDIO_IO_LAYOUTS: &'static [AudioIOLayout] = &[AudioIOLayout {
        main_input_channels: NonZeroU32::new(2),
        main_output_channels: NonZeroU32::new(2),
        ..AudioIOLayout::const_default()
    }];

    const MIDI_INPUT: MidiConfig = MidiConfig::None;
    const MIDI_OUTPUT: MidiConfig = MidiConfig::None;
    const SAMPLE_ACCURATE_AUTOMATION: bool = false;

    type SysExMessage = ();
    type BackgroundTask = ();

    fn params(&self) -> Arc<dyn Params> {
        self.params.clone()
    }

    fn editor(&mut self, _async_executor: AsyncExecutor<Self>) -> Option<Box<dyn Editor>> {
        editor::create_post_editor(editor::PostEditorArgs {
            egui_state: Arc::clone(&self.editor_state),
            // B-022 段階 1: editor 側は use site で lazy-read するため Arc を共有する
            // （旧: `read_instance_id(&self.params)` で String snapshot を渡していたが
            // editor() 自体が WrapperInner::new() で 1 度だけ呼ばれ、Default UUID で
            // 凍結されてしまう問題があった）。
            instance_id: Arc::clone(&self.params.instance_id),
            // §4-5 Step 1: B-022 段階 1 同位相で `Arc::clone` を渡す。editor() / 各
            // use site で `read_project_hash_arc` / `read_daw_session_id_arc` を呼んで
            // chunk-restore + cell update 後の最新値を lazy-read する。
            project_hash: Arc::clone(&self.project_hash),
            daw_session_id: Arc::clone(&self.daw_session_id),
            measure: Arc::clone(&self.measure_result),
            delta: Arc::clone(&self.delta_result),
            measure_alive: Arc::clone(&self.measure_alive),
            signal_state: Arc::clone(&self.signal_state),
            record_sm: Arc::clone(&self.record_sm),
            record_acknowledged: Arc::clone(&self.record_acknowledged),
            pair_label: Arc::clone(&self.pair_label),
            paired_pre_target: Arc::clone(&self.paired_pre_target),
            license: Arc::clone(&self.license),
            preset_available: Arc::clone(&self.preset_available),
            installation_id: Arc::clone(&self.installation_id),
            playback_pos_samples: Arc::clone(&self.playback_pos_samples),
            playback_sample_rate: Arc::clone(&self.playback_sample_rate),
            // B-027 段階 2: POST GUI に pair_pre_name 入力欄を追加。trigger_keep
            // で `filter_candidates_by_name` の引数として読み出す。
            pair_pre_name: Arc::clone(&self.params.pair_pre_name),
        })
    }

    fn initialize(
        &mut self,
        _audio_io_layout: &AudioIOLayout,
        buffer_config: &BufferConfig,
        context: &mut impl InitContext<Self>,
    ) -> bool {
        context.set_latency_samples(0);

        // chunk-persist 値（params.project_uuid / params.daw_session_uuid）を
        // プロセス cell に反映。POST は PRE の cell 値を優先する authority 順序。
        // 1. PRE が既に initialize 済なら cell に PRE_uuid → POST adopt して params 上書き
        // 2. PRE 未 initialize なら cell 空 → 自身の値で cell をセット（PRE 後発時に
        //    PRE が overwrite するが POST はもう adopt しない既知制約）
        sync_project_uuid_from_pre(&self.params);
        let persisted_project_uuid = read_persisted_string(&self.params.project_uuid);
        let persisted_session_uuid = read_persisted_string(&self.params.daw_session_uuid);
        if !persisted_project_uuid.is_empty() {
            set_project_uuid(persisted_project_uuid);
        }
        if !persisted_session_uuid.is_empty() {
            set_daw_session_id(persisted_session_uuid);
        }
        // §4-5 Step 1: Arc<RwLock<String>> 経由で cell 値を反映。Arc は editor()
        // 経由で PostEditorState / IO Thread closure / Watchdog restart_io 全部
        // と共有されており、全 use site の lazy-read で同 instance 内の divergence
        // が消える (§4-4 R-9 主因 a / b の構造的解消)。
        if let Ok(mut g) = self.project_hash.write() {
            *g = process_project_hash();
        }
        if let Ok(mut g) = self.daw_session_id.write() {
            *g = daw_session_id();
        }

        // B-027 段階 2: chunk-restored pair_pre_name の正規化補助 (経路 1)。
        // 主の正規化は editor 入力時 / GUI sanitize で都度かかるが、
        // 永続化されている値自体を正規化済に保つため initialize() 後に 1 度だけ
        // 書き戻す (PRE 側 lib.rs の B-023 段階 1 同パターン)。
        // R-28 機能的沈黙: 違反値の検出で UI エラーを出さない。
        let restored_pair_name = self
            .params
            .pair_pre_name
            .read()
            .ok()
            .map(|g| g.clone())
            .unwrap_or_default();
        let sanitized_pair_name = sanitize_name(&restored_pair_name);
        if sanitized_pair_name != restored_pair_name {
            if let Ok(mut g) = self.params.pair_pre_name.write() {
                *g = sanitized_pair_name;
            }
        }

        // ── 既存 Watchdog を停止 ─────────────────────────────────────
        self.watchdog_shutdown.store(true, Ordering::Relaxed);
        self.measure_shutdown.store(true, Ordering::Relaxed);
        self.io_shutdown.store(true, Ordering::Relaxed);
        if let Some(h) = self.watchdog_handle.take() {
            let _ = h.join();
        }

        // ── フラグリセット ───────────────────────────────────────────
        self.watchdog_shutdown.store(false, Ordering::Relaxed);
        self.measure_shutdown.store(false, Ordering::Relaxed);
        self.io_shutdown.store(false, Ordering::Relaxed);
        self.measure_alive.store(true, Ordering::Relaxed);
        if let Ok(mut slot) = self.pending_producer.lock() {
            *slot = None;
        }
        self.process_counter = 0;

        // ── リングバッファ再生成 ─────────────────────────────────────
        let capacity =
            (buffer_config.sample_rate as usize) * RING_BUFFER_SECONDS * N_CHANNELS;
        let (producer, consumer) = rtrb::RingBuffer::new(capacity);
        self.ring_producer = Some(producer);

        // ── Heartbeat リセット ────────────────────────────────────────
        self.heartbeat.store(0, Ordering::Relaxed);

        // ── T-F: sample_rate キャッシュ ──────────────────────────────
        self.playback_sample_rate
            .store(buffer_config.sample_rate as u32, Ordering::Relaxed);
        self.playback_pos_samples.store(i64::MIN, Ordering::Relaxed);

        // ── Measure Thread 起動 ──────────────────────────────────────
        let measure_handle = spawn_measure_thread(
            consumer,
            buffer_config.sample_rate as u32,
            Arc::clone(&self.measure_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.measure_shutdown),
            Arc::clone(&self.heartbeat),
        );

        // ── IO Thread 起動 ───────────────────────────────────────────
        // B-022 段階 1: io_thread には Arc<RwLock<String>> 共有 → tick ごと lazy-read。
        // 旧 String snapshot は initialize() 時点で凍結されるため、setState 経由の
        // 後続復元・再 initialize で値が変わったケースで instance_id が陳腐化していた。
        let sample_rate = buffer_config.sample_rate as u32;
        let instance_id_arc = Arc::clone(&self.params.instance_id);
        // §4-5 Step 1: `project_hash` は `Arc<RwLock<String>>` 経由で IO Thread と共有。
        // `Arc::clone` で Arc 自体を複製し、内部 String への参照を共有する (use site
        // で `read_project_hash_arc` lazy-read)。
        let project_hash_arc = Arc::clone(&self.project_hash);
        let daw_session_id_arc = Arc::clone(&self.daw_session_id);
        // B-023 段階 4: POST IO Thread に pair_label Arc を共有。PRE 側 ack 後の
        // paired_pre_name を 1 秒間隔で読出して GUI 表示を更新する。
        let pair_label_arc = Arc::clone(&self.pair_label);

        // B-027 段階 3-B α-7-4-D / Step 11: trigger_pair_resolution closure 構築。
        // IO Thread sub-tick (io_thread_post.rs / Step 10 sub-tick L289 area) で broadcast
        // 新規検出時のみ呼出され、editor::trigger_keep_internal を toast=None / now=0.0 で
        // 実行する。crate 構造制約 (kirin_measure → hypha_post 逆依存不可) の回避経路:
        //   - kirin_measure 側 spawn_io_thread_post は Arc<dyn Fn> を引数受領のみ
        //   - closure 構築 (Arc capture 9 件) は本 lib.rs 側で完結
        //   - editor::trigger_keep_internal は Step 11 で pub(crate) 昇格 (Q-11-D 案 (a))
        // 申し送り #31 遅延約束追跡: license は Step 6 で IO Thread 引数追加 (実 use 待ち)
        // → Step 11 で closure capture に直接移行 + spawn_io_thread_post 引数から撤去。
        let trigger_pair_resolution: TriggerPairResolutionFn = {
            let license_for_closure = Arc::clone(&self.license);
            let record_sm_for_closure = Arc::clone(&self.record_sm);
            let instance_id_for_closure = Arc::clone(&self.params.instance_id);
            // §4-5 Step 1: project_hash / daw_session_id を Arc capture (closure body
            // 内で lazy-read snapshot を取る / chunk-restore 後の最新 cell 値が
            // broadcast 受信側 trigger_keep_internal に渡る経路を構造保証する)。
            let project_hash_for_closure = Arc::clone(&self.project_hash);
            let daw_session_id_for_closure = Arc::clone(&self.daw_session_id);
            let pair_label_for_closure = Arc::clone(&self.pair_label);
            let paired_pre_target_for_closure = Arc::clone(&self.paired_pre_target);
            let pair_pre_name_for_closure = Arc::clone(&self.params.pair_pre_name);
            let measure_result_for_closure = Arc::clone(&self.measure_result);
            Arc::new(move |originator_iid: &str, started_at: &str| {
                let iid_snapshot = read_instance_id_arc(&instance_id_for_closure);
                let project_hash_snapshot = read_project_hash_arc(&project_hash_for_closure);
                let daw_session_id_snapshot =
                    read_daw_session_id_arc(&daw_session_id_for_closure);
                let pair_pre_name_snapshot = pair_pre_name_for_closure
                    .read()
                    .ok()
                    .map(|g| g.clone())
                    .unwrap_or_default();
                let m_snapshot = match measure_result_for_closure.lock() {
                    Ok(g) => g.clone(),
                    Err(poisoned) => poisoned.into_inner().clone(),
                };
                editor::trigger_keep_internal(
                    *license_for_closure,
                    &record_sm_for_closure,
                    &iid_snapshot,
                    &project_hash_snapshot,
                    &daw_session_id_snapshot,
                    &pair_label_for_closure,
                    &paired_pre_target_for_closure,
                    &m_snapshot,
                    None,
                    0.0,
                    &pair_pre_name_snapshot,
                );
                log::info!(
                    "[all_keep] trigger_keep_internal invoked: originator={} started_at={}",
                    originator_iid,
                    started_at
                );
            })
        };

        // α-7' All Stop: Stop broadcast 受信 closure。Keep と完全対称形 / toast=None /
        // editor::trigger_stop_internal を呼出して record_sm.exit_record + cleanup 一式実行。
        let trigger_stop_resolution: TriggerStopResolutionFn = {
            let record_sm_for_closure = Arc::clone(&self.record_sm);
            let instance_id_for_closure = Arc::clone(&self.params.instance_id);
            let project_hash_for_closure = Arc::clone(&self.project_hash);
            let pair_label_for_closure = Arc::clone(&self.pair_label);
            let paired_pre_target_for_closure = Arc::clone(&self.paired_pre_target);
            Arc::new(move |originator_iid: &str, started_at: &str| {
                let iid_snapshot = read_instance_id_arc(&instance_id_for_closure);
                let project_hash_snapshot = read_project_hash_arc(&project_hash_for_closure);
                editor::trigger_stop_internal(
                    &record_sm_for_closure,
                    &project_hash_snapshot,
                    &iid_snapshot,
                    &pair_label_for_closure,
                    &paired_pre_target_for_closure,
                    None,
                    0.0,
                );
                log::info!(
                    "[all_stop] trigger_stop_internal invoked: originator={} started_at={}",
                    originator_iid,
                    started_at
                );
            })
        };

        let io_handle = spawn_io_thread_post(
            Arc::clone(&instance_id_arc),
            Arc::clone(&project_hash_arc),
            sample_rate,
            Arc::clone(&self.record_sm),
            Arc::clone(&self.measure_result),
            Arc::clone(&self.delta_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.preset_available),
            Arc::clone(&self.paired_pre_target),
            Arc::clone(&self.io_shutdown),
            Arc::clone(&pair_label_arc),
            // B-027 段階 3-B α-7-4-D / Step 11: license 引数撤去 (Q-11-C 案 (i)) /
            // 末尾 trigger_pair_resolution 追加 (Q-11-D 案 (a)) / 引数 count = 14 不変。
            // §4-5 Step 1: project_hash / daw_session_id 引数を Arc<RwLock<String>>
            // 化 (B-022 段階 1 instance_id 同位相 / per-tick lazy-read で divergence
            // 是正)。
            Arc::clone(&daw_session_id_arc),
            Arc::clone(&self.params.pair_pre_name),
            Arc::clone(&trigger_pair_resolution),
            // α-7' All Stop: 末尾に trigger_stop_resolution 引数追加 (count 15)。
            Arc::clone(&trigger_stop_resolution),
        );

        // ── Watchdog Thread 起動 ──────────────────────────────────────
        let restart_io = {
            let instance_id_arc = Arc::clone(&instance_id_arc);
            // §4-5 Step 1: Watchdog restart 経路でも Arc を capture / 共有。initial
            // 経路と完全対称 (snapshot timing divergence 是正範囲を全 spawn 経路で維持)。
            let project_hash_arc = Arc::clone(&project_hash_arc);
            let record_sm = Arc::clone(&self.record_sm);
            let measure_result = Arc::clone(&self.measure_result);
            let delta_result = Arc::clone(&self.delta_result);
            let signal_state = Arc::clone(&self.signal_state);
            let preset_available = Arc::clone(&self.preset_available);
            let paired_pre_target = Arc::clone(&self.paired_pre_target);
            let pair_label_arc = Arc::clone(&pair_label_arc);
            // Step 11: license capture 撤去 (closure 経由 / Q-11-C 案 (i)) /
            // trigger_pair_resolution Arc capture 追加 (initial 経路と完全対称)。
            let daw_session_id_arc = Arc::clone(&daw_session_id_arc);
            let pair_pre_name_arc = Arc::clone(&self.params.pair_pre_name);
            let trigger_pair_resolution = Arc::clone(&trigger_pair_resolution);
            // α-7' All Stop: Watchdog restart 経路でも trigger_stop_resolution capture。
            let trigger_stop_resolution = Arc::clone(&trigger_stop_resolution);
            move |new_shutdown: Arc<AtomicBool>| {
                spawn_io_thread_post(
                    Arc::clone(&instance_id_arc),
                    Arc::clone(&project_hash_arc),
                    sample_rate,
                    Arc::clone(&record_sm),
                    Arc::clone(&measure_result),
                    Arc::clone(&delta_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&preset_available),
                    Arc::clone(&paired_pre_target),
                    new_shutdown,
                    Arc::clone(&pair_label_arc),
                    Arc::clone(&daw_session_id_arc),
                    Arc::clone(&pair_pre_name_arc),
                    Arc::clone(&trigger_pair_resolution),
                    Arc::clone(&trigger_stop_resolution),
                )
            }
        };

        self.watchdog_handle = Some(spawn_watchdog(WatchdogParams {
            sample_rate: buffer_config.sample_rate as u32,
            ring_capacity: capacity,
            measure_result: Arc::clone(&self.measure_result),
            signal_state: Arc::clone(&self.signal_state),
            heartbeat: Arc::clone(&self.heartbeat),
            measure_shutdown: Arc::clone(&self.measure_shutdown),
            measure_alive: Arc::clone(&self.measure_alive),
            pending_producer: Arc::clone(&self.pending_producer),
            measure_handle,
            io_shutdown: Arc::clone(&self.io_shutdown),
            io_handle,
            restart_io: Box::new(restart_io),
            watchdog_shutdown: Arc::clone(&self.watchdog_shutdown),
        }));

        true
    }

    fn reset(&mut self) {}

    fn process(
        &mut self,
        buffer: &mut Buffer,
        _aux: &mut AuxiliaryBuffers,
        context: &mut impl ProcessContext<Self>,
    ) -> ProcessStatus {
        self.heartbeat.fetch_add(1, Ordering::Relaxed);

        let bypass_val = self.params.bypass.value();
        let transport = context.transport();
        let playing = transport.playing;
        let silent = buffer_is_silent(buffer);

        let pos = transport.pos_samples().unwrap_or(i64::MIN);
        self.playback_pos_samples.store(pos, Ordering::Relaxed);

        let state = if bypass_val {
            SignalState::Bypassed
        } else if !playing || silent {
            SignalState::Inactive
        } else {
            SignalState::Active
        };
        store_signal_state(&self.signal_state, state);

        if state == SignalState::Active {
            if let Some(producer) = &mut self.ring_producer {
                for channel_samples in buffer.iter_samples() {
                    for sample in channel_samples {
                        let _ = producer.push(*sample);
                    }
                }
            }
        }

        self.process_counter = self.process_counter.wrapping_add(1);
        if self.process_counter & 0xFF == 0 {
            log::info!(
                "[POST diag] state={:?} bypass={} playing={} silent={} buf_len={}",
                state, bypass_val, playing, silent,
                buffer.samples()
            );

            if let Ok(mut slot) = self.pending_producer.try_lock() {
                if let Some(new_producer) = slot.take() {
                    self.ring_producer = Some(new_producer);
                    log::info!("[POST process] ring producer swapped after Measure restart");
                }
            }
        }

        ProcessStatus::Normal
    }
}

impl Vst3Plugin for HyphaPost {
    const VST3_CLASS_ID: [u8; 16] = *b"KirinHyphaPOSTv1";
    const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] =
        &[Vst3SubCategory::Fx, Vst3SubCategory::Analyzer];
}

fn buffer_is_silent(buffer: &mut Buffer) -> bool {
    for channel in buffer.as_slice() {
        for &sample in channel.iter() {
            if sample != 0.0 {
                return false;
            }
        }
    }
    true
}

nih_export_vst3!(HyphaPost);

// ── B-027 段階 2: pair_pre_name chunk persist + 正規化テスト ───────────────
//
// hypha_post crate は cdylib のみのため外部 tests/ から直接 import 不可。
// hypha_pre `name_persist_tests` (lib.rs:454-515 / B-023 段階 1) と完全同パターン
// で 5 件の必須テストを揃える。chunk roundtrip は nih-plug `params/persist.rs`
// の pub re-export (`serialize_field` / `deserialize_field`) 経由で固定する。
#[cfg(test)]
mod pair_pre_name_persist_tests {
    use kirin_measure::sanitize_name;
    use nih_plug::params::persist::{deserialize_field, serialize_field};
    use std::sync::RwLock;

    /// Test 1: 空文字保存・復元。pair_pre_name 未設定の Default::default() 状態
    /// (空文字) が chunk persist と同経路を経ても空文字のまま戻ることを構造的に固定する。
    #[test]
    fn pair_pre_name_roundtrip_empty() {
        let rw: RwLock<String> = RwLock::new(String::new());
        let json = serialize_field(&*rw.read().unwrap()).expect("serialize empty");
        assert_eq!(json, r#""""#);
        let restored: String = deserialize_field(&json).expect("deserialize empty");
        let rw2: RwLock<String> = RwLock::new(restored);
        assert_eq!(*rw2.read().unwrap(), "");
    }

    /// Test 2: ASCII 16 文字保存・復元。境界値が完全一致で戻ることを固定。
    #[test]
    fn pair_pre_name_roundtrip_ascii_16() {
        let original = "AbcDefGhi1234567"; // 16 文字
        assert_eq!(original.len(), 16);
        let rw: RwLock<String> = RwLock::new(original.to_string());
        let json =
            serialize_field(&*rw.read().unwrap()).expect("serialize ascii_16");
        let restored: String =
            deserialize_field(&json).expect("deserialize ascii_16");
        let rw2: RwLock<String> = RwLock::new(restored);
        assert_eq!(*rw2.read().unwrap(), original);
    }

    /// Test 3: sanitize_name が 17 文字目以降を truncate する。
    #[test]
    fn pair_pre_name_sanitize_truncates_over_16() {
        let raw = "a".repeat(20);
        let sanitized = sanitize_name(&raw);
        assert_eq!(sanitized.len(), 16);
        assert_eq!(sanitized, "a".repeat(16));
    }

    /// Test 4: sanitize_name が非 ASCII 文字を strip する。
    #[test]
    fn pair_pre_name_sanitize_strips_non_ascii() {
        let raw = "日本語Snare";
        let sanitized = sanitize_name(raw);
        assert_eq!(sanitized, "Snare");
    }

    /// Test 5: sanitize_name が制御文字を strip する。
    #[test]
    fn pair_pre_name_sanitize_strips_control_chars() {
        let raw = "\x01Snare\x7f";
        let sanitized = sanitize_name(raw);
        assert_eq!(sanitized, "Snare");
    }
}
