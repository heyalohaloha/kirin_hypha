//! IO Thread — POST 側（A-3 修正後）。
//!
//! 100ms ループで:
//! 1. 確定済みPREの1本の `pre.json` を直接読む（未確定時だけ名前解決をbounded実行）
//! 2. Δ = POST − PRE を算出、鮮度判定
//! 3. `$TMPDIR/kirin/{project_hash}/{self.instance_id}/post.json` にアトミック書込
//! 4. PRE固有の1本のpair claimを公開
//! 5. `Arc<Mutex<DeltaResult>>` を更新
//! 6. Record mode 時: `plugin_data/{project_hash}/{instance_id}/post/*.json` に
//!    Frame (10 fps) / PSB (2 fps) を追記、30 秒毎に flush
//!
//! - このスレッドが panic / 権限エラーで止まっても Audio Thread / Measure Thread は継続
//! - Drop 時に post.json 自体は削除しない。実行体固有 lease を解放すると新 reader から
//!   即座に不可視になる。起動時の履歴sweepは行わない。
//! - Record 中に終了した場合、保留中の writer は status=closed で flush してから閉じる

#[cfg(test)]
use std::fs;
#[cfg(test)]
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::{self, JoinHandle};
#[cfg(test)]
use std::time::SystemTime;
use std::time::{Duration, Instant};

#[cfg(test)]
use crate::delta::DeltaSnapshot;
use crate::delta::{DeltaMode, DeltaResult};
use crate::engine::SessionSummary;
use crate::pairing_scope::{
    read_pre_at, select_target_pre_for_arm_for_post_project_in_session, LatchedPre,
};
use crate::plugin_data::Role as PluginDataRole;
#[cfg(test)]
use crate::post_candidates::{
    discover_active_post_dirs, enumerate_active_post_pair_candidates, scan_post_candidates_in,
    self_check_pair_claim, self_check_pair_claim_exact, PostTmpJson,
};
use crate::pre_discovery::PostDiscoveryState;
#[cfg(test)]
use crate::pre_discovery::DISCOVERY_STALE_SECS;
use crate::record::RecordStateMachine;
#[cfg(test)]
use crate::record_signal;
#[cfg(test)]
use crate::record_signal::{SignalStatus, SIGNALS_SUBDIR};
#[cfg(test)]
use crate::record_writer::parse_iso8601_to_epoch_ms;
use crate::record_writer::{
    run_record_tick_with_pair_names_require_session_and_marks, RecordingCtx,
};
use crate::storage::{PlatformPaths, StoragePaths};
use crate::{load_signal_state, MeasureResult, RecordTakeTracker, RecordTraceQueue, SignalState};

#[path = "io_thread_post_record_ack.rs"]
mod record_ack;
#[cfg(test)]
use record_ack::{
    current_preset_exists, poll_record_signal_ack_with_base, post_ack_generation_is_authorized,
};
use record_ack::{poll_preset_availability, poll_record_signal_ack};

#[path = "io_thread_post_liveness.rs"]
mod liveness;
pub use liveness::format_pair_label;
#[cfg(test)]
use liveness::{
    find_pre_json_mtime, poll_ack_timeout_with_base, poll_pre_liveness, poll_pre_liveness_at,
};
use liveness::{poll_ack_timeout, poll_latched_pre_liveness};

#[path = "io_thread_post_delta.rs"]
mod delta_resolution;
#[cfg(test)]
use delta_resolution::compute_delta_with_state;
pub(crate) use delta_resolution::resolve_delta_for_store;
pub use delta_resolution::{compute_delta, merge_last_active};
use delta_resolution::{compute_delta_for_pre_file, resolve_delta_for_non_active_post};

#[path = "io_thread_post_json.rs"]
mod post_json;
pub use post_json::serialize_post_json;
#[cfg(test)]
use post_json::{
    serialize_post_json_minimal, serialize_post_json_minimal_with_daw, serialize_post_json_with_daw,
};
use post_json::{
    serialize_post_json_minimal_with_daw_owner_and_pair_instance,
    serialize_post_json_with_daw_owner_and_pair_instance,
};

#[path = "io_thread_post_identity.rs"]
mod identity;
pub(crate) use identity::read_instance_id_arc;

#[path = "io_thread_post_pair_claim.rs"]
mod pair_claim;
use pair_claim::service_pair_claim;

#[path = "io_thread_post_analysis.rs"]
mod analysis;
use analysis::service_post_analysis_endpoints;

#[path = "io_thread_post_reservation.rs"]
mod reservation;
use reservation::ReservationLeaseRefresh;

#[path = "io_thread_post_idle.rs"]
mod idle;
use idle::IdleRecordStop;

#[path = "io_thread_post_drop.rs"]
mod drop_commit;
use drop_commit::service_open_drop_commit;

#[path = "io_thread_post_closed_drop.rs"]
mod closed_drop;
use closed_drop::ClosedDropRecovery;

#[path = "io_thread_post_broadcast.rs"]
mod broadcast;
use broadcast::poll_post_broadcasts;

#[path = "io_thread_post_shutdown.rs"]
mod shutdown;
use shutdown::shutdown_post_io;

#[path = "io_thread_post_self_check.rs"]
mod self_check;
use self_check::PairSelfCheckState;

#[path = "io_thread_post_tick.rs"]
mod tick;
#[cfg(test)]
use identity::broadcast_scope_or_same_project_host_matches;
#[cfg(test)]
use identity::pair_claim_matches_desired_binding;
use identity::{read_daw_session_id_arc, read_project_hash_arc, snapshot_pair_pre_name};
#[cfg(test)]
use tick::compute_latched_display;
use tick::run_tick;

#[path = "io_thread_post_policy.rs"]
mod policy;
use policy::record_idle_timeout;
#[cfg(test)]
use policy::{
    drop_commit_matches_observed_capture, idle_autostop_due, keep_broadcast_blocked_by_stop,
    parse_idle_timeout, remember_latest_started_at, SelfCheckReleaseGate,
    SELF_CHECK_RELEASE_CONFIRMATIONS,
};

const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// PRE ファイルが Active とみなされる最大経過時間（秒）
const STALE_SECS: i64 = 5; // B-046: 2→5 (fs I/O backpressure 吸収 / G-115-246)

/// PRE ファイルが NoPre とみなされる最大経過時間（秒）
/// B-059: `pairing_scope::select_target_pre` の freshness gate でも単一ソースとして参照。
pub(crate) const NO_PRE_SECS: i64 = 10;

/// producer-owned preset/current.json ポーリング間隔（固定 1 path / 1 秒）。
const PRESET_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// record_signal.json ACK タイムアウト監視間隔（G-60-02: 1 秒）。
const ACK_TIMEOUT_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// B-023 段階 4: ACK/start barrier polling 間隔。
/// PRE 側 ack 後 `paired_pre_name` 取得と POST Record start barrier を同じ tick で扱う。
/// 1 秒 throttle では `started_at` を過ぎた後に POST だけ遅れて Record へ入るため、IO tick と同じ
/// 100ms cadence にする。
const PAIR_LABEL_POLL_INTERVAL: Duration = LOOP_SLEEP;

/// B-027 段階 3-B α-7-4-C / Step 10: all_keep_signal broadcast polling 間隔。
/// Producer preparation is a user-triggered control handshake。既存IO tickと同じ100msで
/// project-local `current.json` 1件だけを読み、履歴やoriginator数に比例する走査はしない。
/// これによりAll Keepはbusy scanなしで全exact writerのreadyを確認してから成功を返せる。
const ALL_KEEP_POLL_INTERVAL: Duration = LOOP_SLEEP;

/// B-024 Group A / Gap-2: PRE 死活確認 sub-tick の間隔 (1 秒 / 既存 PRESET / ACK /
/// PAIR_LABEL / ALL_KEEP と同位相)。`record_sm.is_recording()` 中のみ動作し disk I/O は
/// 軽量 (`fs::metadata` 1 件 / project)。
const PRE_LIVENESS_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// 60 秒以上 mtime 更新が無い (or pre.json 不在) 場合の診断しきい値。
/// B-243 以降、stale/missing 自体は Stop 権限を持たず Record は維持する。
const PRE_LIVENESS_STALE_SECS: u64 = 60;

/// B-027 段階 3-B α-7-4-D / Step 11: IO Thread broadcast 受信時に発火する trigger
/// closure 型。引数 `(originator_iid, started_at)`。crate 構造制約 (kirin_measure →
/// hypha_post 逆依存不可) を回避するため、closure 構築は呼出側 (hypha_post::lib.rs)
/// で完結し本 crate は Arc<dyn Fn> として受領するのみ。`clippy::type_complexity`
/// (rust-clippy 1.94) を type alias で抑制。
pub type TriggerPairResolutionFn =
    Arc<dyn Fn(&str, &str, &crate::capture_generation::CaptureGeneration) -> bool + Send + Sync>;

/// α-7' All Stop: broadcast 受信時に発火する Stop trigger closure 型。
/// `TriggerPairResolutionFn` と同シグネチャ (`(originator_iid, started_at)`) で
/// hypha_post::editor::trigger_stop_internal を toast=None で呼出す。
pub type TriggerStopResolutionFn = Arc<dyn Fn(&str, &str) + Send + Sync>;

/// PairBinding の世代snapshot/releaseを所有層へ戻す。IO Threadは古いself-check判定を
/// generationなしで現在のbindingへ適用してはならない。
pub type PairBindingGenerationFn = Arc<dyn Fn() -> u64 + Send + Sync>;
pub type ReleasePairBindingIfCurrentFn = Arc<dyn Fn(&str, u64) -> bool + Send + Sync>;

/// POST 用 IO Thread を起動して JoinHandle を返す。
///
/// # 引数
/// - `instance_id`        : POST の永続 instance UUID（`Arc<RwLock<String>>` で
///   plugin params と共有。B-022 段階 1 で String snapshot から lazy-read 化。
///   `set_state` 経由の chunk-restore 後でも次 tick から最新値を拾う）
/// - `project_hash`       : DAW プロセス単位の project_hash
/// - `sample_rate`        : Record モード Writer の `sample_rate` フィールドに格納
/// - `record_sm`          : Watch/Record 判定用（editor.rs から共有）
/// - `post_result`        : Measure Thread が更新する POST 側計測結果
/// - `delta_result`       : この IO Thread が更新する Δ結果
/// - `preset_available`   : 1 秒ごとに producer-owned `preset/current.json` だけを確認
/// - `paired_pre_target`  : trigger_keep が選定した PRE instance_id（v1.2 (a)
///   cross-instance pair 復元キー）。Watch 中は None、Keep 成功直後に Some、Stop で None
/// - `shutdown`           : `true` になったらループ終了
/// - `pair_label`         : POST GUI 表示用 pair ラベル（B-023 段階 4）。
///   PRE 側 ack 後の `paired_pre_name` を 1 秒 throttle で読出 → 形式
///   `pair: <name>` または `pair: <UUID8>` （[`format_pair_label`]）で書込。
///   `record_sm.is_recording()` でガードし Stop 直後の復活を防ぐ。
/// - `daw_session_id`     : cross-process 防壁。Step 10 all_keep sub-tick で
///   `broadcast.daw_session_id` 比較に使用。
/// - `pair_pre_name`      : POST GUI で編集された pair PRE Name の `Arc<RwLock<String>>`。
///   100ms tick で snapshot 取得 → `serialize_post_json{,_minimal}` の `pair_pre_name`
///   field に書込。
/// - `trigger_pair_resolution`: B-027 段階 3-B α-7-4-D / Step 11 / closure 経由案。
///   呼出側 (hypha_post::lib.rs) で `Arc::clone` 9 件 + `editor::trigger_keep_internal`
///   ラップで構築した closure を受領。引数は `(originator_iid, started_at)`。
///   sub-tick で broadcast 新規検出時のみ発火 (toast=None / now=0.0)。crate 構造制約
///   (kirin_measure → hypha_post 逆依存不可) 回避と `trigger_keep_internal` シグネチャ
#[allow(clippy::too_many_arguments)]
pub fn spawn_io_thread_post(
    instance_id: Arc<RwLock<String>>,
    project_hash: Arc<RwLock<String>>,
    sample_rate: u32,
    record_sm: Arc<RecordStateMachine>,
    post_result: Arc<Mutex<MeasureResult>>,
    delta_result: Arc<Mutex<DeltaResult>>,
    signal_state: Arc<AtomicU8>,
    is_playing: Arc<AtomicBool>,
    preset_available: Arc<AtomicBool>,
    _license: impl Into<crate::LiveLicense>,
    paired_pre_target: Arc<Mutex<Option<String>>>,
    shutdown: Arc<AtomicBool>,
    pair_label: Arc<Mutex<String>>,
    daw_session_id: Arc<RwLock<String>>,
    pair_pre_name: Arc<RwLock<String>>,
    trigger_pair_resolution: TriggerPairResolutionFn,
    // α-7' All Stop: Stop broadcast 受信時 closure (Keep と完全対称)。
    trigger_stop_resolution: TriggerStopResolutionFn,
    pair_binding_generation: PairBindingGenerationFn,
    release_pair_binding_if_current: ReleasePairBindingIfCurrentFn,
    // io_thread → GUI ステータス行への通知 channel。
    // B-245 以降、writer flush failure は Record を止めない。
    // 現在は idle timeout など、Record を正当に閉じた経路の説明だけを書き込む。
    record_error_message: Arc<RwLock<Option<String>>>,
    pair_claimed_at: Arc<RwLock<f64>>,
    // W-281 / G-115-249 / D-1: pair release toast 通知 channel (IO Thread → GUI)。
    // None = 通常 / Some(msg) = GUI 側 update closure 入口で take() → Toast 化。
    pair_release_notice: Arc<RwLock<Option<String>>>,
    // B-043: Record セッション集計値共有スロット (Measure Thread → IO Thread)。
    // Record→Watch 遷移時に take して PluginDataWriter::set_session_aggregates に渡す。
    session_summary: Arc<Mutex<Option<SessionSummary>>>,
    // Offline bounce 用 TRACE queue (Measure Thread → IO Thread)。
    record_trace_queue: RecordTraceQueue,
    // Audio Thread が積んだ実レンダー長。Record close 時の clean bounce_take 正本。
    record_take_tracker: Arc<RecordTakeTracker>,
    // Control-plane-preallocated Record-only Audio -> Measure lane.
    record_ingress: Arc<crate::RecordIngress>,
    // GUI Thread → POST IO Thread. Only the writer consumes queued MARKs.
    record_mark_queue: crate::record_mark::RecordMarkQueue,
    // B-076: 累積 push_overflow（Audio Thread が ring 満杯時に積む）。run_record_tick が
    // Record 開始で snapshot し close 時に差分を per-Record dropped_samples として焼き込む。
    overflow: Arc<std::sync::atomic::AtomicU64>,
    // B-125: 累積 oversized_drop（JUCE 殻のみ計上 / egui は常に 0）。overflow とは別カウンタ。
    // run_record_tick が同位相で snapshot/差分し、合算を dropped_samples へ焼く。
    oversized_drop: Arc<std::sync::atomic::AtomicU64>,
    // Engine-lifetime exact-pair owner. Unlike the Watch lease below, this Arc is created outside
    // the watchdog restart closure and remains locked while IO worker generations are replaced.
    pair_owner: Arc<crate::PairOwnershipLease>,
    // B-108: display と keep/Arm が共有する単一ラッチ。io_thread が毎 tick 維持し、shell 側の
    // keep/keep_all/broadcast 受信が `resolve_arm_target` で読む（egui/JUCE 両殻が同実体を渡す）。
    latched_pre: Arc<Mutex<Option<LatchedPre>>>,
    // JUCE measurement shell supplies the optional on-demand Spectrum coordinator. The legacy
    // egui shell passes None so its pair/IO behavior remains unchanged.
    spectrum: Option<Arc<crate::SpectrumCoordinator>>,
    // JUCE Observatory exact TIME join. Legacy shells pass None.
    meter_history: Option<Arc<crate::MeterDeltaHistoryExchange>>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        // B-128 (G-115-370): 観測 family（io_thread）入口の identity materialize（唯一の検証点）。
        // restore 由来の path-unsafe な project_hash / instance_id セルを正規化し、path-unsafe なら
        // fresh new_v4 へ差し替える（§7② 観測継続）。daw_session_id は空が legacy bridge の明示契約
        // なので observation 扱いで UUID 化せず、非空 unsafe だけ restore 経路で畳む。
        // path-safe な値は無改変＝parity の literal id path テスト不変。下流 builder wall が DiD backstop。
        crate::path_identity::normalize_observation_cell(
            &instance_id,
            "io_thread_post.instance_id",
        );
        crate::path_identity::normalize_observation_cell(
            &project_hash,
            "io_thread_post.project_hash",
        );
        crate::path_identity::normalize_restore_cell(
            &daw_session_id,
            "io_thread_post.daw_session_id",
            None,
        );
        // B-021 Phase 1A: PRE scan の起点は `kirin_root` (= $TMPDIR/kirin/) で、
        // POST IO Thread が動的に discover する。`project_dir_hint` は POST 自身の
        // project_uuid から構築した fallback (PRE が見つからない場合のみ使う)。
        // POST 自身の post.json 書込先は instance_dir 固定 (POST 自分の project_uuid)。
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let initial_project_hash = read_project_hash_arc(&project_hash);
        let initial_instance_id = read_instance_id_arc(&instance_id);
        let plugin_data_dir_str = match StoragePaths::default_platform() {
            Ok(paths) => paths.plugin_data_dir().display().to_string(),
            Err(_) => "<unresolved>".to_string(),
        };

        log::info!(
            "[IOThread POST] started: instance_id={} project_hash={} plugin_data_dir={} (lazy-read instance_id/project_hash/daw_session_id, initial project_dir_hint={}, kirin_root={})",
            initial_instance_id,
            initial_project_hash,
            plugin_data_dir_str,
            kirin_root.join(&initial_project_hash).display(),
            kirin_root.display()
        );

        // Exact capture generations and direct lifecycle paths make historical artifacts
        // non-authoritative. A POST startup performs no plugin_data or /tmp history sweep;
        // recovery belongs to the exact transaction that is explicitly opened by Keep/Drop.

        // B-027 段階 3-B α-7-1 / Step 6: 引数を closure scope に capture。
        // Step 11 で `license_for_thread` は撤去 (closure 経由案 / 呼出側 lib.rs で
        // `trigger_pair_resolution` closure に直接 capture / 申し送り #31 遅延約束追跡完了)。
        // - `daw_session_id_arc` (§4-5 Step 1 Arc 化): Step 10 で all_keep sub-tick
        //   (cross-process 防壁 = daw_session_id / host_process_id scope filter) で実 use 中。
        //   per-tick lazy-read で chunk-restore 後の最新 cell 値を反映 (snapshot timing
        //   divergence 是正 / §4-4 R-9)。
        // - `pair_pre_name_for_thread`: Step 6 で実 use (run_tick 内 100ms tick で snapshot
        //   取得 → serialize_post_json{,_minimal} に渡す / Q-A7 採用案 A)。
        let daw_session_id_arc = daw_session_id;
        let pair_pre_name_for_thread = pair_pre_name;
        // W-281: pair_claimed_at + pair_release_notice の Arc を thread scope に capture。
        let pair_claimed_at_for_thread = pair_claimed_at;
        let pair_release_notice_for_thread = pair_release_notice;
        // W-281 / C-3: self check 周期 (1 sec interval / Daisuke 確定 判断 3)。
        // 毎 tick 100ms はコスト過大のため tick state 局所変数で last 時刻を保持。
        let mut pair_self_check = PairSelfCheckState::new(Instant::now());

        let mut recording: Option<RecordingCtx> = None;
        let mut last_preset_available: Option<bool> = None;
        let mut next_preset_poll = Instant::now();
        let mut next_ack_timeout_poll = Instant::now();
        let mut next_pair_label_poll = Instant::now();
        // All Keep/Stop are edge notifications, not renewable execution leases. Each current file
        // has one owner and is replaced by that owner's next immutable generation, so retaining
        // the last processed key per originator is naturally bounded by the live POST roster. A
        // wall-clock TTL here would let an unchanged completed generation become a new edge after
        // 30 seconds and is therefore forbidden.
        let mut processed_broadcasts = crate::broadcast_edge::BroadcastEdgeMemory::default();
        let mut processed_stop_broadcasts = crate::broadcast_edge::BroadcastEdgeMemory::default();
        let mut next_all_keep_poll = Instant::now();
        // B-024 Group A / Gap-2: PRE 死活監視 sub-tick の next-fire 時刻。
        let mut next_pre_liveness_poll = Instant::now();
        let mut reservation_lease_refresh = ReservationLeaseRefresh::new(Instant::now());
        let mut closed_drop_recovery = ClosedDropRecovery::new(Instant::now());
        let mut discovery = PostDiscoveryState::new();
        let mut watch_lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        let mut owned_pair_claim: Option<crate::pair_claim_index::PairClaim> = None;
        let mut next_pair_claim_publish = Instant::now();
        // B-243: Record idle auto-stop は「10分以上無音」の正当停止理由。Active 信号 /
        // 非Record で基点更新し、Record 中に連続無Active がしきい値を超えたら graceful 停止。
        let idle_timeout = record_idle_timeout();
        match idle_timeout {
            Some(timeout) => log::info!("[IOThread POST] idle auto-stop timeout = {:?}", timeout),
            None => log::info!("[IOThread POST] idle auto-stop disabled"),
        }
        let mut idle_record_stop = IdleRecordStop::new(Instant::now(), idle_timeout);

        loop {
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            // B-022 段階 1: tick 開始時に instance_id を lazy-read。
            // `Arc<RwLock<String>>` は plugin params と同実体を共有するため、
            // `set_state_inner` 経由で chunk-restored 値が書かれた直後でも
            // 次 tick からは新値を拾う。
            // §4-5 Step 1: 同位相で project_hash も毎 tick lazy-read。editor() と
            // initialize() の snapshot timing 差で生じていた divergence を構造的に解消。
            let instance_id_owned = read_instance_id_arc(&instance_id);
            let instance_id_ref = instance_id_owned.as_str();
            let project_hash_owned = read_project_hash_arc(&project_hash);
            let project_hash_ref = project_hash_owned.as_str();
            let daw_session_id_owned = read_daw_session_id_arc(&daw_session_id_arc);
            let daw_session_id_ref = daw_session_id_owned.as_str();

            // Reservation liveness is one exact inode, refreshed by its POST owner. Failure is
            // non-authoritative and never stops an active Record.
            reservation_lease_refresh.service(
                Instant::now(),
                record_sm.is_recording(),
                &paired_pre_target,
                project_hash_ref,
                instance_id_ref,
            );
            // B-128 (G-115-370): within-base wall。POST post.json は inline writer ゆえ builder 関数を
            // 通らない。spawn 時 normalize_observation_cell に加え、ここでも guard して PRE(io_dir 毎回
            // guard)との DiD parity を取る（cell が spawn 後に path-unsafe 化しても base 内に留める）。
            let ph_guard = crate::path_identity::guard_path_component(
                project_hash_ref,
                "io_thread_post.post_json.project_hash",
            );
            let iid_guard = crate::path_identity::guard_path_component(
                instance_id_ref,
                "io_thread_post.post_json.instance_id",
            );
            let project_dir_hint = kirin_root.join(&*ph_guard);
            let instance_dir = project_dir_hint.join(&*iid_guard);
            let post_file = instance_dir.join("post.json");
            if let Err(e) = watch_lease.bind(&instance_dir) {
                log::warn!("[IOThread POST] watch lease bind error: {}", e);
            }

            // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name snapshot per tick。
            // RwLock read guard 寿命を tick 内に閉じる (closure スコープから外で
            // guard を保持しない)。poison error 時は空文字 fallback (旧 schema 互換)。
            let pair_pre_name_snapshot = snapshot_pair_pre_name(&pair_pre_name_for_thread);
            let paired_pre_instance_id_snapshot = crate::paired_pre_instance_id(&latched_pre);
            let pair_binding_generation_snapshot = (pair_binding_generation)();
            // W-281: pair_claimed_at snapshot per tick (同位相 / poison は 0.0 fallback)。
            let pair_claimed_at_snapshot =
                pair_claimed_at_for_thread.read().map(|g| *g).unwrap_or(0.0);

            // W-281 / C-3: 1 sec interval で固定所有権の競合 self check を発火。
            // PRE 固有の1本の claim と、その所有 POST lease だけを直接確認する。project 配下の
            // POST 列挙は行わない。exact PRE が未確定なら競合を断定できないため release しない。
            // 解放対象 → C-4 経路で pair_pre_name="" / pair_claimed_at=0.0 + Toast 通知。
            // W-284 / G-115-252: Record 中は self_check を skip。Record 中に self_check
            // が release を発火すると pair_pre_name="" + delta_result clear (W-282)
            // + pair_label 切替で Record 継続が破綻する (Daisuke 2026-05-17 報告)。
            // Record 開始時点で確定した pair は Stop まで保持する仕様。
            // B-253: 再生/バウンス中は pair を外さない。
            // SignalState::Active は無音 gap で Inactive になり得るため、transport.playing も
            // self-check release の gate に含める。名前で結ばれた pair は transport が
            // 動いている間は保持し、競合による選択解除は停止中のみ許容する。
            pair_self_check.service(
                Instant::now(),
                &kirin_root,
                &record_sm,
                &is_playing,
                &signal_state,
                paired_pre_instance_id_snapshot.as_deref(),
                &pair_pre_name_snapshot,
                pair_binding_generation_snapshot,
                project_hash_ref,
                instance_id_ref,
                pair_owner.owner_id(),
                pair_claimed_at_snapshot,
                &release_pair_binding_if_current,
                &pair_claimed_at_for_thread,
                &pair_release_notice_for_thread,
                &delta_result,
            );

            // C-4 直後 / 通常 tick: 解放後の最新値を再 snapshot して run_tick へ渡す
            // (1 tick 内に解放 → 書込まで完結 / 次 tick 待ちでの一過性矛盾を排除)。
            let pair_pre_name_snapshot = snapshot_pair_pre_name(&pair_pre_name_for_thread);
            let pair_claimed_at_snapshot =
                pair_claimed_at_for_thread.read().map(|g| *g).unwrap_or(0.0);
            let record_generation_before_tick = record_sm.generation();
            let recording_for_tick = record_sm.is_recording();
            let record_generation_after_tick = record_sm.generation();
            let stable_record_generation = (recording_for_tick
                && record_generation_before_tick == record_generation_after_tick)
                .then_some(record_generation_after_tick);

            let post_snapshot_written = match run_tick(
                &project_dir_hint,
                &kirin_root,
                &mut discovery,
                &instance_dir,
                &post_file,
                instance_id_ref,
                watch_lease.owner_id(),
                &post_result,
                &delta_result,
                &signal_state,
                &pair_pre_name_snapshot,
                pair_claimed_at_snapshot,
                project_hash_ref,
                daw_session_id_ref,
                // B-108: Record 中はラッチ凍結（W-284 self_check-skip と同型）。latched は共有実体。
                recording_for_tick,
                &latched_pre,
            ) {
                Ok(()) => true,
                Err(e) => {
                    log::warn!("[IOThread POST] tick error: {}", e);
                    false
                }
            };
            if post_snapshot_written {
                if let Some(generation) = stable_record_generation {
                    let delta = crate::sync_recovery::lock_recover(
                        &delta_result,
                        "POST Record display delta",
                    )
                    .clone();
                    record_sm.publish_record_display_delta(
                        generation,
                        delta,
                        crate::paired_pre_instance_id(&latched_pre),
                    );
                }
            }

            service_post_analysis_endpoints(
                spectrum.as_ref(),
                meter_history.as_ref(),
                &latched_pre,
                instance_id_ref,
                &pair_pre_name_snapshot,
            );

            // The exact PRE ownership index is published only after this POST's atomic snapshot
            // already contains the same binding generation. A PRE UI and a competing POST read
            // one fixed claim, then one fixed post.json; neither path enumerates a directory.
            let current_pre = crate::paired_pre_instance_id(&latched_pre);
            let current_claimed_at = pair_claimed_at_for_thread
                .read()
                .map(|value| *value)
                .unwrap_or(0.0);
            service_pair_claim(
                &kirin_root,
                &instance_dir,
                post_snapshot_written,
                current_pre.as_deref(),
                project_hash_ref,
                instance_id_ref,
                current_claimed_at,
                &pair_owner,
                &mut owned_pair_claim,
                &mut next_pair_claim_publish,
                |expected_pre, expected_claimed_at| {
                    crate::paired_pre_instance_id(&latched_pre).as_deref() == expected_pre
                        && pair_claimed_at_for_thread
                            .read()
                            .map(|value| value.to_bits() == expected_claimed_at.to_bits())
                            .unwrap_or(false)
                },
            );

            // plugin_data/.../post/*.json ライフサイクル
            // POST は自身の signal_path から started_at を resolve
            // §4-5 Step 1: project_hash_ref は tick 開始時の lazy-read snapshot を流用。
            let resolver = || match StoragePaths::default_platform() {
                Ok(paths) => crate::record_writer::resolve_started_at_ms(
                    &paths.plugin_data_dir(),
                    project_hash_ref,
                    instance_id_ref,
                ),
                Err(_) => crate::record_writer::now_epoch_ms(),
            };
            // v1.2 (a): POST 側は paired_pre_instance_id に trigger_keep が保存した
            // target_id を渡す。paired_post は常に None（POST 自身が POST なので相手 POST は無い）。
            let paired_pre_arc = Arc::clone(&paired_pre_target);
            let paired_pre_resolver = move || paired_pre_arc.lock().ok().and_then(|g| g.clone());
            let paired_post_resolver = || None::<String>;
            let pair_name_for_writer = pair_pre_name_snapshot.clone();
            let pair_pre_name_for_writer = pair_pre_name_snapshot.clone();
            let pair_name_resolver = move || Some(pair_name_for_writer);
            let pair_pre_name_resolver = move || Some(pair_pre_name_for_writer);
            // Drop はKirin OSが開始時に捕捉したexact session commitだけを受理する。
            // 一時的不在・破損・capture不一致はR-28に従い無言で次tickへ委ねる。
            service_open_drop_commit(
                &record_sm,
                &record_take_tracker,
                &paired_pre_target,
                project_hash_ref,
                instance_id_ref,
            );
            let record_session_id = record_sm.record_session_id();
            if let Err(e) = run_record_tick_with_pair_names_require_session_and_marks(
                &record_sm,
                PluginDataRole::Post,
                sample_rate,
                project_hash_ref,
                instance_id_ref,
                resolver,
                paired_pre_resolver,
                paired_post_resolver,
                pair_name_resolver,
                pair_pre_name_resolver,
                move || record_session_id,
                &post_result,
                &mut recording,
                Some(&session_summary),
                &overflow,       // B-076: per-Record dropped_samples 算出用
                &oversized_drop, // B-125: per-Record oversized block drop 算出用
                Some(&record_trace_queue),
                Some(&record_take_tracker),
                &record_mark_queue,
            ) {
                log::warn!("[writer] tick error: {}", e);
            }

            closed_drop_recovery.service(
                Instant::now(),
                &record_sm,
                &paired_pre_target,
                project_hash_ref,
                instance_id_ref,
            );

            idle_record_stop.service(
                Instant::now(),
                &record_sm,
                &signal_state,
                recording.is_some(),
                &record_error_message,
                &paired_pre_target,
                project_hash_ref,
                instance_id_ref,
            );

            if Instant::now() >= next_preset_poll {
                poll_preset_availability(
                    project_hash_ref,
                    &preset_available,
                    &mut last_preset_available,
                );
                next_preset_poll = Instant::now() + PRESET_POLL_INTERVAL;
            }

            if Instant::now() >= next_ack_timeout_poll {
                poll_ack_timeout(
                    project_hash_ref,
                    instance_id_ref,
                    &record_sm,
                    &pair_label,
                    &paired_pre_target,
                );
                next_ack_timeout_poll = Instant::now() + ACK_TIMEOUT_POLL_INTERVAL;
            }

            // B-023 段階 4: PRE 側 ack 後の paired_pre_name を読み出して pair_label
            // を更新（1 秒 throttle / record_sm.is_recording() ガードで Stop 直後の
            // 復活窓を構造的に防止）。
            if Instant::now() >= next_pair_label_poll {
                poll_record_signal_ack(
                    project_hash_ref,
                    instance_id_ref,
                    sample_rate,
                    &record_sm,
                    &pair_label,
                    &record_ingress,
                );
                next_pair_label_poll = Instant::now() + PAIR_LABEL_POLL_INTERVAL;
            }

            // B-024 Group A / Gap-2: PRE 死活確認 sub-tick (1 秒 throttle)。
            // Record 中でも stem/offline export 後は PRE pre.json の mtime が止まるため、
            // ここで Record を自動解除しない。Keep は利用者の Stop 操作まで保持する。
            if Instant::now() >= next_pre_liveness_poll {
                poll_latched_pre_liveness(instance_id_ref, &record_sm, &latched_pre);
                next_pre_liveness_poll = Instant::now() + PRE_LIVENESS_POLL_INTERVAL;
            }

            if Instant::now() >= next_all_keep_poll {
                if let Ok(paths) = StoragePaths::default_platform() {
                    let base_dir = paths.plugin_data_dir();
                    poll_post_broadcasts(
                        &base_dir,
                        project_hash_ref,
                        instance_id_ref,
                        &daw_session_id_arc,
                        &record_sm,
                        &mut processed_broadcasts,
                        &mut processed_stop_broadcasts,
                        &trigger_pair_resolution,
                        &trigger_stop_resolution,
                    );
                } else {
                    log::warn!("[all_keep] StoragePaths::default_platform() failed; skipping tick");
                }
                next_all_keep_poll = Instant::now() + ALL_KEEP_POLL_INTERVAL;
            }

            thread::sleep(LOOP_SLEEP);
        }

        // The restartable worker does not release the engine-owned exact claim. It only closes its
        // own Record and broadcast lifecycle before this closure drops the unique watch lease.
        shutdown_post_io(
            recording,
            &record_sm,
            &session_summary,
            &record_take_tracker,
            &record_mark_queue,
            &instance_id,
            &project_hash,
            &paired_pre_target,
        );
    })
}

#[cfg(test)]
#[path = "io_thread_post_pair_claim_tests.rs"]
mod pair_claim_binding_tests;

#[cfg(test)]
#[path = "io_thread_post_watch_recovery_tests.rs"]
mod watch_producer_recovery_tests;

#[cfg(test)]
#[path = "io_thread_post_idle_autostop_tests.rs"]
mod b206_idle_autostop_tests;

#[cfg(test)]
#[path = "io_thread_post_self_check_tests.rs"]
mod self_check_release_gate_tests;

#[cfg(test)]
#[path = "io_thread_post_self_check_service_tests.rs"]
mod self_check_service_tests;

#[cfg(test)]
#[path = "io_thread_post_stop_keep_barrier_tests.rs"]
mod b222_all_stop_keep_barrier_tests;

#[cfg(test)]
#[path = "io_thread_post_broadcast_tests.rs"]
mod broadcast_receiver_tests;

#[cfg(test)]
#[path = "io_thread_post_compute_delta_tests.rs"]
mod compute_delta_selection_tests;
#[cfg(test)]
#[path = "io_thread_post_latch_state_tests.rs"]
mod latched_delta_state_tests;
#[cfg(test)]
#[path = "io_thread_post_latch_identity_tests.rs"]
mod latched_delta_tests;
#[cfg(test)]
#[path = "io_thread_post_preset_tests.rs"]
mod preset_poll_tests;

#[cfg(test)]
#[path = "io_thread_post_ack_timeout_tests.rs"]
mod ack_timeout_tests;
#[cfg(test)]
#[path = "io_thread_post_json_tests.rs"]
mod post_tmp_json_tests;
#[cfg(test)]
#[path = "io_thread_post_record_ack_tests.rs"]
mod record_signal_ack_barrier_tests;

// ── PostCandidate / scan / discover / enumerate テスト (Step 5) ───────────────
#[cfg(test)]
#[path = "io_thread_post_candidate_scan_tests.rs"]
mod post_candidate_tests;

#[cfg(test)]
#[path = "io_thread_post_candidate_enumeration_tests.rs"]
mod post_candidate_enumeration_tests;

#[cfg(test)]
#[path = "io_thread_post_self_check_claim_tests.rs"]
mod self_check_claim_tests;

// ── snapshot_pair_pre_name テスト (B-027 段階 3-B α-7-1 / Step 6) ─────────────
#[cfg(test)]
#[path = "io_thread_post_pre_liveness_tests.rs"]
mod pre_liveness_tests;
#[cfg(test)]
#[path = "io_thread_post_pair_name_snapshot_tests.rs"]
mod snapshot_pair_pre_name_tests;

#[cfg(test)]
#[path = "io_thread_post_pre_mtime_tests.rs"]
mod pre_mtime_path_guard_tests;
#[cfg(test)]
#[path = "io_thread_post_release_delta_tests.rs"]
mod release_delta_reset_tests;
#[cfg(test)]
#[path = "io_thread_post_delta_store_tests.rs"]
mod resolve_delta_for_store_tests;

#[cfg(test)]
#[path = "io_thread_post_non_active_tests.rs"]
mod non_active_delta_store_tests;
