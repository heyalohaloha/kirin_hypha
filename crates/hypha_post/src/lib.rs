mod editor;

use kirin_measure::{
    daw_session_id, delete_broadcast, delete_signal, delete_stop_broadcast,
    ensure_legacy_cleanup_done, identity_instance_attach, identity_instance_detach, live_window,
    load_installation_id_safe, load_license_safe, new_record_trace_queue, peek_project_uuid,
    process_project_hash, reservation, sanitize_name, set_daw_session_id, set_project_uuid,
    spawn_io_thread_post, spawn_measure_thread, spawn_watchdog, store_signal_state, DeltaResult,
    LatchedPre, License, LivenessEvaluator, MeasureResult, RecordStateMachine, RecordTraceQueue,
    SessionSummary, SignalState, StoragePaths, TriggerPairResolutionFn, TriggerStopResolutionFn,
    WatchdogIo, WatchdogParams, N_CHANNELS, RING_BUFFER_SECONDS,
};
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU32, AtomicU64, AtomicU8, Ordering};
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
    /// B-108: display と keep/Arm が共有する単一ラッチ。`spawn_io_thread_post` に渡して io_thread が
    /// 毎 tick 維持し、editor の trigger_keep / broadcast 受信 closure が `resolve_arm_target` で読む。
    latched_pre: Arc<Mutex<Option<LatchedPre>>>,

    ring_producer: Option<rtrb::Producer<f32>>,
    measure_result: Arc<Mutex<MeasureResult>>,
    /// B-043: Record セッション集計値共有スロット (Measure → IO Thread)。
    session_summary: Arc<Mutex<Option<SessionSummary>>>,
    /// Offline bounce 用 TRACE queue（Measure → IO）。
    record_trace_queue: RecordTraceQueue,
    /// B-076: ring 満杯で測定 ring に push できなかった累積サンプル数。Audio Thread が
    /// 計数し、io_thread が per-Record dropped_samples を .kirin に焼き込む。
    overflow: Arc<AtomicU64>,
    delta_result: Arc<Mutex<DeltaResult>>,

    measure_shutdown: Arc<AtomicBool>,
    io_shutdown: Arc<AtomicBool>,

    // ── T-8: Watchdog ────────────────────────────────────────────────
    watchdog_shutdown: Arc<AtomicBool>,
    watchdog_handle: Option<JoinHandle<()>>,
    pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    measure_alive: Arc<AtomicBool>,
    process_counter: u32,
    /// B-207 #1: 直近の process_mode。Offline→Realtime 遷移（host の再 initialize）を
    /// 「オフラインレンダー終了」とみなし、KEEP 保持中なら自動 Stop する（AU の
    /// maybeAutoStopOnOfflineEnd と対）。message/main-thread の initialize() のみが触る。
    prev_process_mode: Option<ProcessMode>,

    // ── SignalState（SS-1）────────────────────────────────────────────
    signal_state: Arc<AtomicU8>,

    // ── Heartbeat（SS-3 代替: process() 停止検出）────────────────────
    heartbeat: Arc<AtomicU32>,
    /// B-118: 単一鮮度評価器。editor が POST pair 変更ロックの live 述語として読む
    /// （`playing かつ is_live()` でロック / signal_state とは別軸 / G-115-245: 3s window）。
    liveness: Arc<LivenessEvaluator>,

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

    installation_id: Arc<String>,
    playback_pos_samples: Arc<AtomicI64>,
    playback_sample_rate: Arc<AtomicU32>,

    /// W-280 / G-115-248: transport.playing 独立 AtomicBool (Audio Thread → GUI Thread)。
    /// `process()` で `transport.playing` を store / GUI が `update` 入口で load して
    /// 再生中の pair 変更 (draw_pair_pre_name_field / draw_pair_pre_combo PRE 候補行)
    /// を block する。SignalState::Active は `!silent` と論理積されるため再生中の
    /// 無音区間で Inactive 落ちする → 代用不可。本 atomic は silent / bypass と直交。
    is_playing: Arc<AtomicBool>,

    /// W-281 / G-115-249 / D-1: pair release toast 通知 channel (IO Thread → GUI Thread)。
    /// non-persist。`None` = 通常 / `Some(msg)` = 次 GUI 描画で `Toast::new(msg, now)` 化。
    /// 既存 `record_error_message` (上記 record_error_message field) と同パターン。
    /// IO Thread の `self_check_pair_claim` が後着優先で自身を release したとき
    /// "Released (paired elsewhere)" を書き込み、GUI 側 update closure 入口で
    /// `take()` で読出+クリアし `state.toast` に詰める。
    pair_release_notice: Arc<RwLock<Option<String>>>,

    /// B-025 Group B-2/B-3 / Gap-19/20: io_thread が連続失敗 (Directory missing /
    /// Write failed) で record exit したとき GUI に表示する英語 1 行ステータス
    /// (G-115-29 準拠)。`None` = 通常 (R-26 沈黙ゲート / 非表示) / `Some` = エラー
    /// 文言保持中。editor は毎フレーム `read()` で値を取り、TP メーター下に出す。
    /// io_thread (initial / restart_io 双方) と editor の間で Arc 共有。
    record_error_message: Arc<RwLock<Option<String>>>,
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

    /// W-281 / G-115-249 / B-1: pair claim 時刻 (Unix epoch sec / f64)。
    /// `pair_pre_name` 設定操作 (TextEdit Enter / ComboBox PRE 選択) のときのみ
    /// 値が `epoch_secs_now()` で更新される。空文字書換 (手動クリア / IO Thread release)
    /// 時は 0.0。post.json `pair_claimed_at` field に毎 tick snapshot 書込まれ、
    /// 後着優先 1対1 強制 (`self_check_pair_claim`) の判定軸となる。
    /// nih-plug `impl_persistent_arc!` 経由で `Arc<RwLock<f64>>` も chunk 永続化対応。
    #[persist = "pair_claimed_at"]
    pub pair_claimed_at: Arc<RwLock<f64>>,
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
            // W-281 / G-115-249: 初期値 0.0 (Default::default() / pair 未確立)。
            pair_claimed_at: Arc::new(RwLock::new(0.0)),
        }
    }
}

impl Default for HyphaPost {
    fn default() -> Self {
        // B-110: live インスタンス refcount +1（破棄は Drop で −1）。下の project_hash /
        // daw_session_id seed より前に置き、seed 時点で refcount>0 を保証する。
        identity_instance_attach();
        // 起動時 1 回限りの旧構造 cleanup（OnceLock 内側で flag-guarded）。
        ensure_legacy_cleanup_done();

        // B-118: heartbeat を先に作り、同一 Arc を観測する単一鮮度評価器を構築する。
        let heartbeat = Arc::new(AtomicU32::new(0));
        let liveness = Arc::new(LivenessEvaluator::new(
            Arc::clone(&heartbeat),
            live_window(),
        ));

        Self {
            params: Arc::new(HyphaPostParams::default()),
            editor_state: EguiState::from_size(300, 200),
            project_hash: Arc::new(RwLock::new(process_project_hash())),
            daw_session_id: Arc::new(RwLock::new(daw_session_id())),
            // B-108: 未ラッチで起動。editor()/initialize() で io_thread と Arc 共有する。
            latched_pre: Arc::new(Mutex::new(None)),
            ring_producer: None,
            measure_result: Arc::new(Mutex::new(MeasureResult::default())),
            session_summary: Arc::new(Mutex::new(None)),
            record_trace_queue: new_record_trace_queue(),
            overflow: Arc::new(AtomicU64::new(0)),
            delta_result: Arc::new(Mutex::new(DeltaResult::default())),
            measure_shutdown: Arc::new(AtomicBool::new(false)),
            io_shutdown: Arc::new(AtomicBool::new(false)),
            watchdog_shutdown: Arc::new(AtomicBool::new(false)),
            watchdog_handle: None,
            pending_producer: Arc::new(Mutex::new(None)),
            measure_alive: Arc::new(AtomicBool::new(true)),
            process_counter: 0,
            prev_process_mode: None,
            signal_state: Arc::new(AtomicU8::new(SignalState::Inactive as u8)),
            heartbeat,
            liveness,
            record_sm: Arc::new(RecordStateMachine::new()),
            record_acknowledged: Arc::new(AtomicBool::new(false)),
            pair_label: Arc::new(Mutex::new(String::new())),
            paired_pre_target: Arc::new(Mutex::new(None)),
            license: Arc::new(load_license_safe()),
            preset_available: Arc::new(AtomicBool::new(false)),
            installation_id: Arc::new(load_installation_id_or_empty()),
            playback_pos_samples: Arc::new(AtomicI64::new(i64::MIN)),
            playback_sample_rate: Arc::new(AtomicU32::new(0)),
            is_playing: Arc::new(AtomicBool::new(false)),
            record_error_message: Arc::new(RwLock::new(None)),
            // W-281 / G-115-249 / D-1: pair release toast channel (non-persist)。
            pair_release_notice: Arc::new(RwLock::new(None)),
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

/// B-207 #1: オフラインレンダー終了の検出（pure / テスト用に分離）。nih-plug は process_mode が
/// 変わると plugin を再 initialize するため、`Offline → Realtime` 遷移を「オフラインバウンス完了」
/// とみなす。`Buffered`（VST3 の先読み）は Offline と別扱いなので誤検知しない。
fn offline_render_ended(prev: Option<ProcessMode>, current: ProcessMode) -> bool {
    prev == Some(ProcessMode::Offline) && current == ProcessMode::Realtime
}

#[cfg(test)]
mod b207_offline_render_tests {
    use super::offline_render_ended;
    use nih_plug::prelude::ProcessMode;

    /// B-207 #1: Offline→Realtime のみを「終了」とみなす。開始/継続/churn/Buffered は対象外。
    #[test]
    fn offline_render_ended_edge() {
        assert!(
            offline_render_ended(Some(ProcessMode::Offline), ProcessMode::Realtime),
            "Offline->Realtime = ended"
        );
        assert!(
            !offline_render_ended(Some(ProcessMode::Realtime), ProcessMode::Offline),
            "Realtime->Offline (start) is not end"
        );
        assert!(
            !offline_render_ended(None, ProcessMode::Realtime),
            "no prior mode = not end"
        );
        assert!(
            !offline_render_ended(Some(ProcessMode::Offline), ProcessMode::Offline),
            "still offline = not end"
        );
        assert!(
            !offline_render_ended(Some(ProcessMode::Realtime), ProcessMode::Realtime),
            "realtime churn = not end"
        );
        assert!(
            !offline_render_ended(Some(ProcessMode::Buffered), ProcessMode::Realtime),
            "Buffered (VST3 lookahead) is not offline end"
        );
    }
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
        let released_pre = self.paired_pre_target.lock().ok().and_then(|g| g.clone());
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
        match StoragePaths::default_platform() {
            Ok(paths) => {
                if let Some(pre) = released_pre.as_deref() {
                    reservation::release_pairing(
                        &paths.plugin_data_dir(),
                        &project_hash_owned,
                        pre,
                        &instance_id_owned,
                    );
                }
                match delete_signal(
                    &paths.plugin_data_dir(),
                    &project_hash_owned,
                    &instance_id_owned,
                ) {
                    Ok(()) => log::info!(
                        "[POST cleanup #3] record_signal deleted: {}",
                        instance_id_owned
                    ),
                    Err(e) => log::warn!("[POST cleanup #3] delete_signal failed: {:?}", e),
                }

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
                    Err(e) => {
                        log::warn!("[POST drop #3 broadcast] delete_broadcast failed: {:?}", e)
                    }
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
            Err(e) => log::warn!("[POST cleanup #3] StoragePaths error: {:?}", e),
        }

        // B-110: live インスタンス refcount −1。0 到達で共有 identity セル（project_uuid /
        // daw_session_id）を clear し、次プロジェクトの再 seed を許す。egui 殻は role-scoped 追加
        // セルを持たないので clear_extra は no-op。POST io_thread はインスタンス固有の
        // `Arc<RwLock<String>>` field を読む（global 非参照）ため、未 join でも clear と非競合（A-3）。
        identity_instance_detach(|| {});
    }
}

impl Plugin for HyphaPost {
    const NAME: &'static str = "POST Kirin Hypha";
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
            // W-280 / G-115-248: transport.playing 共有 (再生中 pair 変更 block)。
            is_playing: Arc::clone(&self.is_playing),
            // B-118: 単一鮮度評価器共有。editor は `playing かつ is_live()` で pair 変更をロックする
            // （playing 凍結値の false-release 防止 / processing 停止中は解除 / G-115-245: 3s）。
            liveness: Arc::clone(&self.liveness),
            // B-027 段階 2: POST GUI に pair_pre_name 入力欄を追加。trigger_keep
            // で `filter_candidates_by_name` の引数として読み出す。
            pair_pre_name: Arc::clone(&self.params.pair_pre_name),
            // W-281 / G-115-249: pair_claimed_at 共有 (editor write / IO Thread read)。
            pair_claimed_at: Arc::clone(&self.params.pair_claimed_at),
            // W-281 / G-115-249 / D-2: pair release toast 通知 channel 共有。
            pair_release_notice: Arc::clone(&self.pair_release_notice),
            // B-025 Group B-2/B-3 / Gap-19/20: io_thread → GUI ステータス行通知 Arc。
            record_error_message: Arc::clone(&self.record_error_message),
            // B-108: display/keep 共有ラッチ。trigger_keep が resolve_arm_target で読む。
            latched_pre: Arc::clone(&self.latched_pre),
        })
    }

    fn initialize(
        &mut self,
        _audio_io_layout: &AudioIOLayout,
        buffer_config: &BufferConfig,
        context: &mut impl InitContext<Self>,
    ) -> bool {
        context.set_latency_samples(0);

        // B-207 #1: egui (VST3) オフラインレンダー終了の自動 Stop。host が process_mode を
        // Offline→Realtime に戻して再 initialize したら（=オフラインバウンス完了）、KEEP 保持中の
        // POST を手動 Stop と同経路（trigger_stop_internal: exit_record_full + reservation 解放 +
        // mark_released → PRE 追従）で停止する。AU の maybeAutoStopOnOfflineEnd と対（バウンス毎に
        // 1 テイク）。record_sm は Arc 永続なので reinit を跨いで Record 状態を保持している。
        // host が reinit しない/エッジを出さない場合も B-206 idle-net が backstop（additive-safe）。
        if offline_render_ended(self.prev_process_mode, buffer_config.process_mode)
            && self.record_sm.is_recording()
        {
            let project_hash = read_project_hash_arc(&self.project_hash);
            let instance_id = read_instance_id_arc(&self.params.instance_id);
            nih_log!("[hypha_post] offline-end auto-stop (process_mode Offline->Realtime)");
            crate::editor::trigger_stop_internal(
                &self.record_sm,
                &project_hash,
                &instance_id,
                &self.pair_label,
                &self.paired_pre_target,
                None,
                0.0,
            );
        }
        self.prev_process_mode = Some(buffer_config.process_mode);

        // chunk-persist 値（params.project_uuid / params.daw_session_uuid）を
        // プロセス cell に反映。POST は PRE の cell 値を優先する authority 順序。
        // 1. PRE が既に initialize 済なら cell に PRE_uuid → POST adopt して params 上書き
        // 2. PRE 未 initialize なら cell 空 → 自身の値で cell をセット（PRE 後発時に
        //    PRE が overwrite するが POST はもう adopt しない既知制約）
        sync_project_uuid_from_pre(&self.params);
        // B-128 (G-115-371 / D2): restore 受領点（egui）の同期 materialize（FFI set_identity と対称）。
        // io_thread spawn / GUI keep より前に params の path-unsafe identity を new_v4 へ畳み、async な
        // io_thread normalize が走る前の窓で uncounted-quarantine Record に入る経路を閉じる（両殻 D2 統一）。
        kirin_measure::normalize_restore_cell(
            &self.params.instance_id,
            "egui_post.initialize.instance_id",
            None,
        );
        // D3: materialize 済 instance_id を tag に project_uuid / daw の anomaly を per-instance routing。
        let iid_tag = self
            .params
            .instance_id
            .read()
            .ok()
            .map(|g| g.clone())
            .unwrap_or_default();
        let tag = if iid_tag.is_empty() {
            None
        } else {
            Some(iid_tag.as_str())
        };
        kirin_measure::normalize_restore_cell(
            &self.params.project_uuid,
            "egui_post.initialize.project_uuid",
            tag,
        );
        kirin_measure::normalize_restore_cell(
            &self.params.daw_session_uuid,
            "egui_post.initialize.daw_session_uuid",
            tag,
        );
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
        let capacity = (buffer_config.sample_rate as usize) * RING_BUFFER_SECONDS * N_CHANNELS;
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
            N_CHANNELS,
            Arc::clone(&self.measure_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.measure_shutdown),
            Arc::clone(&self.liveness),
            Arc::clone(&self.record_sm),
            Arc::clone(&self.session_summary),
            Arc::clone(&self.record_trace_queue),
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
            let latched_for_closure = Arc::clone(&self.latched_pre);
            Arc::new(move |originator_iid: &str, started_at: &str| {
                let iid_snapshot = read_instance_id_arc(&instance_id_for_closure);
                let project_hash_snapshot = read_project_hash_arc(&project_hash_for_closure);
                let daw_session_id_snapshot = read_daw_session_id_arc(&daw_session_id_for_closure);
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
                    &latched_for_closure, // B-108: ラッチ済みならラッチ先を直接 target に使う
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

        // B-125: egui POST は oversized 経路を持たない（全 frame を per-sample で overflow に計上済）。
        // spawn 契約を満たすため常に 0 の専用ゼロカウンタを渡す（io 再起動跨ぎで同実体を共有）。
        let oversized_drop = Arc::new(std::sync::atomic::AtomicU64::new(0));
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
            // B-025 Group B-2/B-3 / Gap-19/20: GUI ステータス行通知 Arc。
            Arc::clone(&self.record_error_message),
            // W-281 / G-115-249: pair_claimed_at + pair_release_notice 共有。
            Arc::clone(&self.params.pair_claimed_at),
            Arc::clone(&self.pair_release_notice),
            // B-043: session_summary 共有 (Measure → IO Thread / Record→Watch 注入)。
            Arc::clone(&self.session_summary),
            Arc::clone(&self.record_trace_queue),
            Arc::clone(&self.overflow), // B-076: per-Record dropped_samples
            Arc::clone(&oversized_drop), // B-125: egui は常に 0（per-sample で overflow に計上済）
            Arc::clone(&self.latched_pre), // B-108: display/keep 共有ラッチ
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
            // B-025 Group B-2/B-3 / Gap-19/20: restart 経路も GUI 通知 Arc を共有
            // (initial 経路と完全対称 / restart 後も連続失敗→exit 通知が GUI に届く)。
            let record_error_message = Arc::clone(&self.record_error_message);
            // W-281 / G-115-249: restart 経路でも pair_claimed_at + pair_release_notice
            // を共有 (initial と完全対称 / restart 後も後着 self check 継続)。
            let pair_claimed_at_arc = Arc::clone(&self.params.pair_claimed_at);
            let pair_release_notice_arc = Arc::clone(&self.pair_release_notice);
            // B-043: restart 経路でも session_summary を共有 (initial と完全対称)。
            let session_summary = Arc::clone(&self.session_summary);
            let record_trace_queue = Arc::clone(&self.record_trace_queue);
            let overflow = Arc::clone(&self.overflow); // B-076
            let oversized_drop = Arc::clone(&oversized_drop); // B-125: egui ゼロカウンタを再起動跨ぎ共有
            let latched_pre = Arc::clone(&self.latched_pre); // B-108
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
                    Arc::clone(&record_error_message),
                    Arc::clone(&pair_claimed_at_arc),
                    Arc::clone(&pair_release_notice_arc),
                    Arc::clone(&session_summary),
                    Arc::clone(&record_trace_queue),
                    Arc::clone(&overflow), // B-076: per-Record dropped_samples
                    Arc::clone(&oversized_drop), // B-125: egui は常に 0
                    Arc::clone(&latched_pre), // B-108: display/keep 共有ラッチ
                )
            }
        };

        self.watchdog_handle = Some(spawn_watchdog(WatchdogParams {
            sample_rate: buffer_config.sample_rate as u32,
            n_channels: N_CHANNELS,
            ring_capacity: capacity,
            measure_result: Arc::clone(&self.measure_result),
            signal_state: Arc::clone(&self.signal_state),
            evaluator: Arc::clone(&self.liveness),
            measure_shutdown: Arc::clone(&self.measure_shutdown),
            measure_alive: Arc::clone(&self.measure_alive),
            pending_producer: Arc::clone(&self.pending_producer),
            measure_handle,
            // B-118: egui は io を eager spawn 済み handle で渡す（既存挙動）。
            io: WatchdogIo::Eager {
                io_shutdown: Arc::clone(&self.io_shutdown),
                io_handle,
                restart_io: Box::new(restart_io),
            },
            watchdog_shutdown: Arc::clone(&self.watchdog_shutdown),
            join_on_shutdown: false, // egui=detach（既存挙動・default 不変）
            record_sm: Arc::clone(&self.record_sm),
            session_summary: Arc::clone(&self.session_summary),
            record_trace_queue: Arc::clone(&self.record_trace_queue),
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
        // W-280 / G-115-248: GUI Thread に純粋な transport.playing を公開する
        // (silent / bypass と直交)。Relaxed store ~1ns / lock-free / R-12 違反なし。
        self.is_playing.store(playing, Ordering::Relaxed);
        let silent = buffer_is_silent(buffer);
        let recording = self.record_sm.is_recording();

        let pos = transport.pos_samples().unwrap_or(i64::MIN);
        self.playback_pos_samples.store(pos, Ordering::Relaxed);

        let state = resolve_process_signal_state(bypass_val, playing, silent, recording);
        store_signal_state(&self.signal_state, state);

        if state == SignalState::Active {
            if let Some(producer) = &mut self.ring_producer {
                for channel_samples in buffer.iter_samples() {
                    for sample in channel_samples {
                        // B-076: ring 満杯時は無言で捨てず累積計数（per-Record で .kirin に露出）。
                        if producer.push(*sample).is_err() {
                            self.overflow.fetch_add(1, Ordering::Relaxed);
                        }
                    }
                }
            }
        }

        self.process_counter = self.process_counter.wrapping_add(1);
        if self.process_counter & 0xFF == 0 {
            log::info!(
                "[POST diag] state={:?} bypass={} playing={} silent={} recording={} buf_len={}",
                state,
                bypass_val,
                playing,
                silent,
                recording,
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

/// B-107: 無音床のピーク線形しきい値 = -140 dBFS = 10^(-140/20) = 1e-7。
/// Audio Thread で log10 を避けるため線形で比較する（RT 安全 / R-12 精緻化と整合）。
const SILENCE_PEAK_LINEAR: f32 = 1e-7;

/// B-107: 1 サンプルが無音床（|s| < -140 dBFS）か（旧: 厳密 0 比較）。純粋・境界テスト可能。
#[inline]
fn sample_is_silent(sample: f32) -> bool {
    sample.abs() < SILENCE_PEAK_LINEAR
}

fn buffer_is_silent(buffer: &mut Buffer) -> bool {
    for channel in buffer.as_slice() {
        for &sample in channel.iter() {
            if !sample_is_silent(sample) {
                return false;
            }
        }
    }
    true
}

#[inline]
fn resolve_process_signal_state(
    bypassed: bool,
    playing: bool,
    silent: bool,
    recording: bool,
) -> SignalState {
    // B-205: Active 条件は「非無音」を必須にする。recording だけでは Active にしない。
    // - transport 停止の Record 保持（playing=false, silent=true）→ Inactive（"---"）。
    //   従来は recording 短絡で Active になり、残留ほぼ0状態を -400 LUFS/TP と誤表示していた。
    // - 非無音のオフラインバウンス（playing=false, silent=false, recording=true）→ Active 維持
    //   （B-147 の bounce 計測継続意図を保つ）。
    if bypassed {
        SignalState::Bypassed
    } else if !silent && (recording || playing) {
        SignalState::Active
    } else {
        SignalState::Inactive
    }
}

nih_export_vst3!(HyphaPost);

// ── B-027 段階 2: pair_pre_name chunk persist + 正規化テスト ───────────────
//
// hypha_post crate は cdylib のみのため外部 tests/ から直接 import 不可。
// hypha_pre `name_persist_tests` (lib.rs:454-515 / B-023 段階 1) と完全同パターン
// で 5 件の必須テストを揃える。chunk roundtrip は nih-plug `params/persist.rs`
// の pub re-export (`serialize_field` / `deserialize_field`) 経由で固定する。
#[cfg(test)]
mod b107_silence_tests {
    use super::{sample_is_silent, SILENCE_PEAK_LINEAR};

    /// B-107: 無音判定の境界（厳密0 / -141dBFS / -139dBFS）。silent iff peak < -140 dBFS。
    #[test]
    fn silence_threshold_boundary() {
        let lin = |dbfs: f32| 10f32.powf(dbfs / 20.0);
        assert!(sample_is_silent(0.0), "厳密0 は無音");
        assert!(sample_is_silent(lin(-141.0)), "-141 dBFS は無音 (< -140)");
        assert!(
            sample_is_silent(-lin(-141.0)),
            "-141 dBFS 負符号も無音 (abs)"
        );
        assert!(
            !sample_is_silent(lin(-139.0)),
            "-139 dBFS は非無音 (> -140)"
        );
        assert!(
            !sample_is_silent(-lin(-139.0)),
            "-139 dBFS 負符号も非無音 (abs)"
        );
        // ちょうど -140 dBFS = SILENCE_PEAK_LINEAR は >= しきい値 → 非無音（peak < -140 のみ無音）。
        assert!(
            !sample_is_silent(SILENCE_PEAK_LINEAR),
            "ちょうど -140 dBFS は非無音（境界）"
        );
    }
}

#[cfg(test)]
mod b147_record_state_tests {
    use super::resolve_process_signal_state;
    use kirin_measure::SignalState;

    #[test]
    fn watch_mode_keeps_previous_playing_and_silence_gate() {
        assert_eq!(
            resolve_process_signal_state(false, true, false, false),
            SignalState::Active
        );
        assert_eq!(
            resolve_process_signal_state(false, true, true, false),
            SignalState::Inactive
        );
        assert_eq!(
            resolve_process_signal_state(false, false, false, false),
            SignalState::Inactive
        );
    }

    /// B-205: 非無音のオフラインバウンス（playing=false, silent=false, recording=true）は
    /// Active を維持する（offline render の実音計測継続 / B-147 意図）。
    #[test]
    fn record_mode_captures_offline_bounce() {
        assert_eq!(
            resolve_process_signal_state(false, false, false, true),
            SignalState::Active
        );
    }

    /// B-205: Record 保持中に transport 停止（playing=false, silent=true）したら Inactive。
    /// 停止＝無信号なので `---` を出す。残留ほぼ0状態を計測して -400 LUFS/TP を表示する
    /// バグ（Record 中に停止 → -400）の回帰防止。
    #[test]
    fn record_mode_silent_transport_is_inactive() {
        assert_eq!(
            resolve_process_signal_state(false, false, true, true),
            SignalState::Inactive
        );
    }

    /// B-205: 再生中の無音ギャップも Record 中は Inactive（無音は計測せず -400 を出さない）。
    /// engine.reset 抑制は recording 軸で別管理（B-043）のため信号復帰時の継続性は保たれる。
    #[test]
    fn record_mode_playing_silent_gap_is_inactive() {
        assert_eq!(
            resolve_process_signal_state(false, true, true, true),
            SignalState::Inactive
        );
    }

    #[test]
    fn bypass_overrides_record_mode() {
        assert_eq!(
            resolve_process_signal_state(true, false, false, true),
            SignalState::Bypassed
        );
    }
}

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
        let json = serialize_field(&*rw.read().unwrap()).expect("serialize ascii_16");
        let restored: String = deserialize_field(&json).expect("deserialize ascii_16");
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

    /// B-077 Test 4: sanitize_name が非 ASCII（日本語）を **保持**する（旧: strip）。
    #[test]
    fn pair_pre_name_sanitize_keeps_non_ascii() {
        let raw = "日本語Snare";
        let sanitized = sanitize_name(raw);
        assert_eq!(sanitized, "日本語Snare");
    }

    /// Test 5: sanitize_name が制御文字を strip する。
    #[test]
    fn pair_pre_name_sanitize_strips_control_chars() {
        let raw = "\x01Snare\x7f";
        let sanitized = sanitize_name(raw);
        assert_eq!(sanitized, "Snare");
    }
}
