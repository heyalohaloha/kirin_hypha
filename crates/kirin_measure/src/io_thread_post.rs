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

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant, SystemTime};

use crate::all_keep_signal::{self, ALL_KEEP_BROADCAST_STALE_SECS};
use crate::all_stop_signal::{self, ALL_STOP_BROADCAST_STALE_SECS};
use crate::cleanup::exit_record_preserve_pair;
use crate::delta::{DeltaMode, DeltaResult, DeltaSnapshot};
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
use crate::record_signal::{self, SignalStatus, SIGNALS_SUBDIR};
use crate::record_writer::{
    apply_record_take_snapshot, parse_iso8601_to_epoch_ms,
    run_record_tick_with_pair_names_require_session_and_marks, take_session_summary,
    writer_close_with_summary_and_marks, RecordingCtx,
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
use liveness::{
    poll_ack_timeout, poll_latched_pre_liveness, release_record_reservation,
    resolve_closed_drop_target,
};

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

#[path = "io_thread_post_policy.rs"]
mod policy;
use policy::{
    drop_commit_matches_observed_capture, generation_stop_authorizes_post, idle_autostop_due,
    keep_broadcast_blocked_by_stop, record_idle_timeout, remember_latest_started_at,
    SelfCheckReleaseGate,
};
#[cfg(test)]
use policy::{parse_idle_timeout, SELF_CHECK_RELEASE_CONFIRMATIONS};

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
    // B-027 段階 3-B α-7-1 / Step 6: 末尾 2 引数 (daw_session_id / pair_pre_name) 追加。
    // Step 11 で license 引数撤去 (closure 経由案 / Q-11-C 案 (i)) + trigger_pair_resolution
    // 引数追加 (closure 経由 / Q-11-D 案 (a))。引数 count は Step 6 以降 14 で不変。
    // §4-5 Step 1: `project_hash` / `daw_session_id` を `Arc<RwLock<String>>` 化
    // (B-022 段階 1 instance_id 同位相 / lib.rs:325-328 コメント参照)。editor() と
    // initialize() の snapshot timing 差で divergence していた構造異常を是正。
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
    // W-281 / G-115-249: pair_claimed_at (Unix epoch sec) Arc 共有 (HyphaPostParams /
    // editor / IO Thread 全 thread で同実体)。chunk 永続化済の値を IO Thread が
    // per-tick snapshot して serialize_post_json{,_minimal} に渡す。
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
        let mut last_self_check_at: Instant = Instant::now() - Duration::from_secs(2);
        let mut self_check_release_gate = SelfCheckReleaseGate::default();

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
        let mut next_reservation_lease_refresh = Instant::now();
        let mut next_closed_drop_poll = Instant::now();
        let mut completed_closed_drop_session: Option<String> = None;
        let mut discovery = PostDiscoveryState::new();
        let mut watch_lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        let mut owned_pair_claim: Option<crate::pair_claim_index::PairClaim> = None;
        let mut next_pair_claim_publish = Instant::now();
        // B-243: Record idle auto-stop は「10分以上無音」の正当停止理由。Active 信号 /
        // 非Record で基点更新し、Record 中に連続無Active がしきい値を超えたら graceful 停止。
        let mut idle_anchor = Instant::now();
        let idle_timeout = record_idle_timeout();
        match idle_timeout {
            Some(timeout) => log::info!("[IOThread POST] idle auto-stop timeout = {:?}", timeout),
            None => log::info!("[IOThread POST] idle auto-stop disabled"),
        }

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

            // Reservation liveness is one exact inode, refreshed by its POST owner. This replaces
            // startup/history sweeps and lets a later explicit Keep reclaim a crashed owner after
            // TTL without enumerating plugin_data. Failure is non-authoritative and never stops an
            // active Record.
            if record_sm.is_recording() && Instant::now() >= next_reservation_lease_refresh {
                if let (Some(pre_instance_id), Ok(paths)) = (
                    paired_pre_target
                        .lock()
                        .ok()
                        .and_then(|guard| guard.clone()),
                    StoragePaths::default_platform(),
                ) {
                    let _ = crate::reservation::refresh_pairing(
                        &paths.plugin_data_dir(),
                        project_hash_ref,
                        &pre_instance_id,
                        instance_id_ref,
                    );
                }
                next_reservation_lease_refresh = Instant::now()
                    + Duration::from_secs(crate::reservation::RESERVATION_LEASE_REFRESH_SECS);
            } else if !record_sm.is_recording() {
                next_reservation_lease_refresh = Instant::now();
            }
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
            let tick_now = Instant::now();
            let transport_playing = is_playing.load(Ordering::Relaxed);
            let self_check_allowed = !record_sm.is_recording()
                && !transport_playing
                && load_signal_state(&signal_state) != SignalState::Active;
            if paired_pre_instance_id_snapshot.is_none() || !self_check_allowed {
                self_check_release_gate.reset();
            } else if tick_now.duration_since(last_self_check_at) >= Duration::from_secs(1) {
                last_self_check_at = tick_now;
                let exact_pre = paired_pre_instance_id_snapshot
                    .as_deref()
                    .expect("checked exact PRE above");
                let conflict = crate::pair_claim_index::live_claim_owned_by_other(
                    &kirin_root,
                    exact_pre,
                    project_hash_ref,
                    instance_id_ref,
                    pair_owner.owner_id(),
                    pair_claimed_at_snapshot,
                );
                if !conflict {
                    self_check_release_gate.reset();
                } else if self_check_release_gate
                    .observe_conflict(exact_pre, pair_claimed_at_snapshot)
                {
                    // 判定後のrename/re-Keepを古い判定で破壊しない。所有層のtransition lock内で
                    // name+generationを再照合し、現世代だった場合だけ全bindingを解放する。
                    let released = !record_sm.is_recording()
                        && (release_pair_binding_if_current)(
                            &pair_pre_name_snapshot,
                            pair_binding_generation_snapshot,
                        );
                    if released {
                        log::info!(
                            "[POST self_check] reject pair: instance_id={} pair_pre_name={} paired_pre_instance_id={:?} (PRE owned by another POST)",
                            instance_id_ref,
                            pair_pre_name_snapshot,
                            paired_pre_instance_id_snapshot
                        );
                        if let Ok(mut c) = pair_claimed_at_for_thread.write() {
                            *c = 0.0;
                        }
                        if let Ok(mut n) = pair_release_notice_for_thread.write() {
                            *n = Some("PRE already in use".to_string());
                        }
                        // W-282 / G-115-250 / A-1: Δ 表示完全リセット。
                        // B-048 LKG (`last_active=Some(snap)`) を bypass し、解放された POST に
                        // 古い Δ 値が `draw_delta_grid_frozen` で凍結保持されるのを防ぐ。
                        *crate::sync_recovery::lock_recover(
                            &delta_result,
                            "POST self_check delta",
                        ) = DeltaResult::default();
                    }
                    self_check_release_gate.reset();
                }
            }

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

            let spectrum_target = latched_pre
                .lock()
                .ok()
                .and_then(|latched| latched.clone())
                .filter(|latched| latched.readiness == crate::LatchedPreReadiness::Confirmed)
                .and_then(|latched| {
                    crate::SpectrumTarget::from_pre_json(latched.instance_id, &latched.pre_json)
                });
            if let Some(spectrum) = spectrum.as_ref() {
                spectrum.service_post_endpoint(
                    instance_id_ref,
                    spectrum_target,
                    &pair_pre_name_snapshot,
                );
            }
            if let Some(meter_history) = meter_history.as_ref() {
                let meter_target = latched_pre
                    .lock()
                    .ok()
                    .and_then(|latched| latched.clone())
                    .filter(|latched| latched.readiness == crate::LatchedPreReadiness::Confirmed)
                    .and_then(|latched| {
                        crate::MeterHistoryTarget::from_pre_json(
                            latched.instance_id,
                            &latched.pre_json,
                        )
                    });
                meter_history.service_post_endpoint(meter_target);
            }

            // The exact PRE ownership index is published only after this POST's atomic snapshot
            // already contains the same binding generation. A PRE UI and a competing POST read
            // one fixed claim, then one fixed post.json; neither path enumerates a directory.
            let current_pre = crate::paired_pre_instance_id(&latched_pre);
            let current_claimed_at = pair_claimed_at_for_thread
                .read()
                .map(|value| *value)
                .unwrap_or(0.0);
            if let Some(owned) = owned_pair_claim.as_ref() {
                if Instant::now() >= next_pair_claim_publish {
                    let current = crate::pair_claim_index::read_pair_claim(
                        &kirin_root,
                        owned.host_process_id,
                        &owned.pre_instance_id,
                    );
                    if current.as_ref() != Some(owned)
                        || !crate::pair_claim_index::pair_claim_is_owned(&kirin_root, owned)
                    {
                        owned_pair_claim = None;
                    }
                    next_pair_claim_publish = Instant::now() + Duration::from_secs(1);
                }
            }
            let desired_matches_owned = owned_pair_claim.as_ref().is_some_and(|owned| {
                pair_claim_matches_desired_binding(
                    owned,
                    current_pre.as_deref(),
                    project_hash_ref,
                    instance_id_ref,
                    pair_owner.owner_id(),
                    current_claimed_at,
                )
            });
            if owned_pair_claim.is_some() && !desired_matches_owned {
                owned_pair_claim = None;
                next_pair_claim_publish = Instant::now();
            }
            if owned_pair_claim.is_none() && Instant::now() >= next_pair_claim_publish {
                let valid_binding = post_snapshot_written
                    && current_pre.is_some()
                    && current_claimed_at.is_finite()
                    && current_claimed_at > 0.0;
                let desired_pre = valid_binding.then(|| current_pre.clone()).flatten();
                let expected_pre = desired_pre.clone();
                let expected_claimed_at = if desired_pre.is_some() {
                    current_claimed_at
                } else {
                    0.0
                };
                let committed = pair_owner.commit_claimed_binding_if(
                    &kirin_root,
                    Some(&instance_dir),
                    desired_pre.as_deref(),
                    project_hash_ref,
                    instance_id_ref,
                    expected_claimed_at,
                    || {
                        crate::paired_pre_instance_id(&latched_pre) == expected_pre
                            && pair_claimed_at_for_thread
                                .read()
                                .map(|value| value.to_bits() == expected_claimed_at.to_bits())
                                .unwrap_or(false)
                    },
                    || Some(()),
                );
                match committed {
                    Ok(Some(())) => {
                        owned_pair_claim = desired_pre.as_deref().and_then(|pre_instance_id| {
                            crate::pair_claim_index::read_pair_claim(
                                &kirin_root,
                                crate::post_candidates::current_host_process_id(),
                                pre_instance_id,
                            )
                        });
                    }
                    Ok(None) => {}
                    Err(error) => log::debug!(
                        "[POST pair claim] atomic commit deferred: instance_id={} error={}",
                        instance_id_ref,
                        error
                    ),
                }
                next_pair_claim_publish = Instant::now() + Duration::from_secs(1);
            }

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
            // Drop は project broadcast ではなく、Kirin OS が Drop 開始時点で捕捉した
            // exact session_id の commit file だけを停止根拠にする。metadata を lifecycle
            // markerへ先に固定し、PREへ reason 付き Released を公開してから両 writerを閉じる。
            if record_sm.is_recording() {
                if let (Ok(paths), Some(session_id)) = (
                    StoragePaths::default_platform(),
                    record_sm.record_session_id(),
                ) {
                    let base = paths.plugin_data_dir();
                    match crate::record_drop_commit::inspect_drop_commit_for_open_session(
                        &base,
                        project_hash_ref,
                        &session_id,
                    ) {
                        Ok(Some(expected)) => {
                            let capture_matches_drop = drop_commit_matches_observed_capture(
                                &expected,
                                &record_take_tracker,
                                record_sm.generation(),
                            );
                            if capture_matches_drop {
                                if let Ok(Some(expected)) =
                                    crate::record_drop_commit::bind_drop_commit_for_open_session(
                                        &base,
                                        project_hash_ref,
                                        &session_id,
                                    )
                                {
                                    release_record_reservation(
                                        &base,
                                        project_hash_ref,
                                        instance_id_ref,
                                        &paired_pre_target,
                                        "drop_committed",
                                    );
                                    let _ = record_signal::mark_released_with_reason(
                                        &base,
                                        project_hash_ref,
                                        instance_id_ref,
                                        record_signal::ReleaseReason::DropCommitted,
                                    );
                                    log::info!(
                                        "[IOThread POST] Drop committed exact session: session={} bounce={} post_iid={}",
                                        session_id,
                                        expected.bounce_id,
                                        instance_id_ref
                                    );
                                    exit_record_preserve_pair(&record_sm);
                                }
                            }
                        }
                        Ok(None) => {}
                        // 内部 commit の一時的不在・破損は停止せず、次 tick へ委ねる。
                        // 利用者操作と結び付かないため R-28 に従い無言で skip する。
                        Err(_) => {}
                    }
                }
            }
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

            // A user may explicitly Stop before Drop. The producer still knows the exact closed
            // session and paired PRE, so poll one commit path and inspect only that pair's fixed
            // `.failed/.pair_pending` files. No project recursion or history convergence runs in
            // the steady-state IO loop.
            if !record_sm.is_recording() && Instant::now() >= next_closed_drop_poll {
                if let Ok(paths) = StoragePaths::default_platform() {
                    let base = paths.plugin_data_dir();
                    let memory_session = record_sm.last_closed_session_id();
                    let memory_pre = paired_pre_target
                        .lock()
                        .ok()
                        .and_then(|guard| guard.clone());
                    if let Some((session_id, pre_instance_id)) = resolve_closed_drop_target(
                        &base,
                        project_hash_ref,
                        instance_id_ref,
                        memory_session.as_deref(),
                        memory_pre.as_deref(),
                    ) {
                        if completed_closed_drop_session.as_deref() != Some(session_id.as_str()) {
                            let reconciled =
                                crate::plugin_data::reconcile_drop_committed_closed_session(
                                    &base,
                                    project_hash_ref,
                                    &session_id,
                                    &pre_instance_id,
                                    instance_id_ref,
                                );
                            if reconciled > 0
                                || crate::plugin_data::pair_record_session_manifest_exists(
                                    &base,
                                    project_hash_ref,
                                    &session_id,
                                )
                            {
                                completed_closed_drop_session = Some(session_id);
                            }
                        }
                    }
                }
                next_closed_drop_poll = Instant::now() + Duration::from_secs(1);
            }

            // ── B-243: Record idle auto-stop（10分以上無音）────────────────────────
            // KEEP は POST 側の操作なので、本機構は POST 主導。ただし通常運用では
            // Stop/All Stop と、この連続無Active timeout だけを Record 停止権限にする。
            // - 非Record: 基点を更新（次 Record で 0 から計時 / 過去の idle を持ち越さない）。
            // - Active: 基点リセット（録音中は決して発火しない）。
            // - 非Active 連続でしきい値超過: release reservation → mark_released → Record終了。
            //   writer は次 tick の run_record_tick (false,true) で graceful close（seal 待ち +
            //   session 集計注入 + trace drain）＝ degraded ではなく正常テイクとして保存。
            // 非Record または Active 信号あり → 基点リセット（次 Record で 0 から計時 /
            // 録音中は決して発火しない）。それ以外（Record 中の連続無Active）でしきい値超過 → 停止。
            if !record_sm.is_recording() || load_signal_state(&signal_state) == SignalState::Active
            {
                idle_anchor = Instant::now();
            } else if idle_autostop_due(true, false, idle_anchor.elapsed(), idle_timeout) {
                let idle_timeout =
                    idle_timeout.expect("idle_autostop_due is false when timeout is None");
                log::info!(
                    "[IOThread POST] idle auto-stop: no Active signal for {:?} (post_iid={})",
                    idle_timeout,
                    instance_id_ref
                );
                if let Ok(paths) = StoragePaths::default_platform() {
                    let base = paths.plugin_data_dir();
                    // reservation 解放（解放対象 PRE iid を読むため）。
                    release_record_reservation(
                        &base,
                        project_hash_ref,
                        instance_id_ref,
                        &paired_pre_target,
                        "idle_timeout",
                    );
                    // PRE は reason 付き released marker だけを Stop として扱う。
                    let _ = record_signal::mark_released_with_reason(
                        &base,
                        project_hash_ref,
                        instance_id_ref,
                        record_signal::ReleaseReason::IdleTimeout,
                    );
                }
                // B-207 #3: writer が存在した（=テイクが録れた）ときだけ "Take saved." を付す。
                // writer_start 失敗（record 開始時ディスクエラー）時は recording==None なので、
                // 実体のないテイクを「保存済み」と誤通知しない。
                let take_existed = recording.is_some();
                exit_record_preserve_pair(&record_sm);
                if let Ok(mut g) = record_error_message.write() {
                    // B-207 #3: しきい値を文言へ反映（env override 時も正確 / 既定 600s = "10 min"）。
                    // 分割り切れなければ秒表記（テスト用の短い override でも 0 min と出さない）。
                    let secs = idle_timeout.as_secs();
                    let dur = if secs.is_multiple_of(60) {
                        format!("{} min", secs / 60)
                    } else {
                        format!("{secs} sec")
                    };
                    *g = Some(if take_existed {
                        format!("Auto-stopped after {dur} idle. Take saved.")
                    } else {
                        format!("Auto-stopped after {dur} idle.")
                    });
                }
                idle_anchor = Instant::now();
            }

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

            // α-7' All Stop: Stop broadcast 受信 sub-tick (Keep より先に処理 / Stop 優先)。
            // 1 秒 throttle で `plugin_data/{ph}/all_stop_signal/current.json` だけを読み、
            // 新 broadcast を `processed_stop_broadcasts` cache に登録 + `trigger_stop_resolution`
            // closure 発火。Keep と並列の同型ロジック (cross-process filter / self skip /
            // 既処理 skip / stale fallback / GC)。
            let mut latest_fresh_stop_started_at: Option<String> = None;
            if Instant::now() >= next_all_keep_poll {
                if let Ok(paths) = StoragePaths::default_platform() {
                    let base_dir = paths.plugin_data_dir();
                    let now_chrono = chrono::Utc::now();
                    let daw_session_id_snapshot = read_daw_session_id_arc(&daw_session_id_arc);
                    let host_process_id_snapshot = crate::current_host_process_id();
                    let stop_broadcasts =
                        all_stop_signal::read_current_stop_broadcast(&base_dir, project_hash_ref);
                    for (originator_iid, broadcast) in stop_broadcasts.into_iter().take(1) {
                        let stop_key = if broadcast.has_generation() {
                            broadcast.capture_generation_id.clone()
                        } else {
                            format!("legacy:{}", broadcast.started_at)
                        };
                        if !broadcast_scope_or_same_project_host_matches(
                            &daw_session_id_snapshot,
                            host_process_id_snapshot,
                            &broadcast.daw_session_id,
                            broadcast.host_process_id,
                        ) {
                            continue;
                        }
                        if broadcast.has_generation()
                            && !generation_stop_authorizes_post(
                                &base_dir,
                                project_hash_ref,
                                instance_id_ref,
                                &broadcast,
                            )
                        {
                            continue;
                        }
                        if !broadcast.has_generation()
                            && all_stop_signal::is_stop_broadcast_stale(
                                &broadcast,
                                now_chrono,
                                ALL_STOP_BROADCAST_STALE_SECS,
                            )
                        {
                            processed_stop_broadcasts.remember(&originator_iid, &stop_key);
                            log::debug!(
                                "[all_stop] stale broadcast cached without fire: originator={}",
                                originator_iid
                            );
                            continue;
                        }
                        if !broadcast.has_generation() {
                            remember_latest_started_at(
                                &mut latest_fresh_stop_started_at,
                                &broadcast.started_at,
                            );
                        }
                        if originator_iid == instance_id_ref {
                            continue;
                        }
                        if processed_stop_broadcasts.contains(&originator_iid, &stop_key) {
                            continue;
                        }
                        processed_stop_broadcasts.remember(&originator_iid, &stop_key);
                        let scan_dir =
                            all_stop_signal::stop_signals_dir(&base_dir, project_hash_ref);
                        log::info!(
                            "[all_stop] new broadcast detected: originator={} started_at={} scan_dir={}",
                            originator_iid,
                            broadcast.started_at,
                            scan_dir.display()
                        );
                        (trigger_stop_resolution)(&originator_iid, &broadcast.started_at);
                    }
                }
                // 注: next_all_keep_poll は Keep sub-tick 末でresetされるため、StopはKeepと
                // 同じ100ms IO cadence・同frameで動く。
            }

            // B-027 段階 3-B α-7-4-C / Step 10: all_keep_signal broadcast 受信 sub-tick。
            // 100ms IO cadenceで`plugin_data/{ph}/all_keep_signal/current.json`だけを読み、
            // 新 broadcast を `processed_broadcasts` cache に登録する (検出 + cache + log
            // のみ / `trigger_keep_internal` 発火は Step 11 で本箇所に追加予定)。
            //
            //  1. cross-process 防壁: daw_session_id / host_process_id scope 外は skip
            //  2. self skip: `originator_iid == self_instance_id` skip (#16 (iii))
            //  3. 既処理 skip: cache 内の immutable generation id と一致 → 同 broadcast skip
            //  4. stale fallback: legacy または未commit generationだけ cache 登録
            //  5. commit済 generation: arm成功まで再試行し、成功後だけ cache 更新
            // Cache entries do not expire: a new Keep always has a new generation UUID/session and
            // therefore replaces the key explicitly. Elapsed time never recreates an edge.
            if Instant::now() >= next_all_keep_poll {
                if let Ok(paths) = StoragePaths::default_platform() {
                    let base_dir = paths.plugin_data_dir();
                    let now_chrono = chrono::Utc::now();
                    // §4-5 Step 1: cross-process 防壁用 daw_session_id を per-tick lazy-read。
                    // editor() snapshot との divergence を是正 (§4-4 R-9 主因 b)。
                    let daw_session_id_snapshot = read_daw_session_id_arc(&daw_session_id_arc);
                    let host_process_id_snapshot = crate::current_host_process_id();
                    let broadcasts =
                        all_keep_signal::read_current_broadcast(&base_dir, project_hash_ref);
                    for (originator_iid, broadcast) in broadcasts.into_iter().take(1) {
                        let broadcast_key = if broadcast.capture_generation_id.trim().is_empty() {
                            format!("legacy:{}", broadcast.started_at)
                        } else {
                            broadcast.capture_generation_id.clone()
                        };
                        // 1. cross-process 防壁
                        if !broadcast_scope_or_same_project_host_matches(
                            &daw_session_id_snapshot,
                            host_process_id_snapshot,
                            &broadcast.daw_session_id,
                            broadcast.host_process_id,
                        ) {
                            continue;
                        }
                        // 2. self skip
                        if originator_iid == instance_id_ref {
                            processed_broadcasts.remember(&originator_iid, &broadcast_key);
                            continue;
                        }
                        // 3. 既処理 skip
                        if processed_broadcasts.contains(&originator_iid, &broadcast_key) {
                            continue;
                        }
                        let broadcast_is_stale = all_keep_signal::is_broadcast_stale(
                            &broadcast,
                            now_chrono,
                            ALL_KEEP_BROADCAST_STALE_SECS,
                        );
                        // Legacy messages have no immutable transaction owner, so time remains
                        // their only compatibility bound. A generation message is handled below:
                        // committed generations remain retryable for their full lifetime.
                        if broadcast_is_stale && broadcast.capture_generation_id.trim().is_empty() {
                            processed_broadcasts.remember(&originator_iid, &broadcast_key);
                            log::debug!(
                                "[all_keep] stale broadcast cached without fire: originator={}, started_at={}",
                                originator_iid,
                                broadcast.started_at
                            );
                            continue;
                        }
                        if keep_broadcast_blocked_by_stop(
                            &broadcast.started_at,
                            latest_fresh_stop_started_at.as_deref(),
                        ) {
                            processed_broadcasts.remember(&originator_iid, &broadcast_key);
                            log::info!(
                                "[all_keep] keep broadcast suppressed by newer/equal all_stop: originator={} keep_started_at={} stop_started_at={}",
                                originator_iid,
                                broadcast.started_at,
                                latest_fresh_stop_started_at.as_deref().unwrap_or("")
                            );
                            continue;
                        }
                        // v1 broadcasts have no generation and cannot safely arm a new
                        // transaction: selecting them would re-introduce time-based grouping.
                        if broadcast.capture_generation_id.trim().is_empty()
                            || broadcast.generation_started_at_ms <= 0
                        {
                            processed_broadcasts.remember(&originator_iid, &broadcast_key);
                            log::debug!(
                                "[all_keep] legacy broadcast skipped without generation: originator={}",
                                originator_iid
                            );
                            continue;
                        }
                        // Workers may arm from the producer-only preparation barrier. Kirin OS
                        // still sees only the active pointer, which is promoted after every exact
                        // PRE/POST writer claim exists. A mismatch is transient and is not cached.
                        let generation =
                            match crate::capture_generation::read_producer_authorized_generation(
                                &base_dir,
                                project_hash_ref,
                                &broadcast.capture_generation_id,
                                broadcast.generation_started_at_ms,
                            ) {
                                Ok(Some(project_generation))
                                    if project_generation
                                        .member(project_hash_ref, instance_id_ref)
                                        .is_some() =>
                                {
                                    project_generation
                                }
                                _ if broadcast_is_stale => {
                                    // A stale staged/aborted generation that never became active is
                                    // permanently non-authoritative and may now be cached.
                                    processed_broadcasts.remember(&originator_iid, &broadcast_key);
                                    continue;
                                }
                                _ => continue,
                            };
                        let entered = (trigger_pair_resolution)(
                            &originator_iid,
                            &broadcast.started_at,
                            &generation,
                        );
                        // Cache only a successful arm (or an IO restart that observes the same
                        // instance already recording). Transient selection/license/IO failures
                        // remain retryable instead of permanently dropping one All Keep member.
                        if entered || record_sm.is_recording() {
                            if entered {
                                log::info!(
                                    "[all_keep] generation member armed: originator={} generation={}",
                                    originator_iid,
                                    generation.capture_generation_id
                                );
                            }
                            processed_broadcasts.remember(&originator_iid, &broadcast_key);
                        }
                    }
                } else {
                    log::warn!("[all_keep] StoragePaths::default_platform() failed; skipping tick");
                }
                next_all_keep_poll = Instant::now() + ALL_KEEP_POLL_INTERVAL;
            }

            thread::sleep(LOOP_SLEEP);
        }

        // Do not release the exact claim here. The IO worker is restartable and does not own the
        // relationship. The engine-held pair lease makes this claim invalid automatically when the
        // POST instance is actually destroyed; a restarted worker adopts the same fixed claim.
        // 終了処理: 直近 tick の instance_id でクリーンアップ。
        // `set_state` 復元後に instance_id が切り替わった場合は旧 instance dir
        // (Default UUID) の post.json が残骸として残るが、次回起動時の同関数で
        // 同じ Default UUID を踏むことは無いため自然消失する (R-28 機能的沈黙)。
        // B-043: Record 中に thread shutdown された場合も Measure Thread の最新
        // session_summary を取り出して JSON に焼き込む (Daisuke 抜去シナリオ救済)。
        if let Some(mut ctx) = recording.take() {
            // B-132 (G-115-382): shutdown-during-record（抜去 / アンロード）。Measure Thread は自身の
            // teardown 中（shutdown フラグで loop 冒頭 break）で seal を進めないため instant check
            // （wait なし = bounded 0 / teardown deadlock 無縁）。seal 未前進 = graceful な
            // Record→Watch tight-drain を経ていない＝ tail 不確定 → 不完全と記録（共通B / R-28）。
            let sealed = record_sm.seal() > ctx.seal_at_start;
            let summary = take_session_summary(&session_summary);
            ctx.writer.add_integrity_reason("lifecycle_shutdown");
            if !sealed {
                ctx.writer.mark_integrity_degraded();
            }
            apply_record_take_snapshot(&mut ctx, Some(&record_take_tracker));
            writer_close_with_summary_and_marks(ctx, summary, &record_mark_queue);
        }

        // §4-5 Step 1: 終了処理時も project_hash を lazy-read で確定。
        let final_iid = read_instance_id_arc(&instance_id);
        let final_project_hash = read_project_hash_arc(&project_hash);
        // Do not delete post.json, temp siblings, or the instance directory here.
        // pluginval and some DAWs can tear down and recreate the same restored
        // instance_id in quick succession; an old IO thread deleting this path
        // can remove the next IO thread's live write and create transient missing
        // POST/PRE pairing. Dropping this thread's unique WatchSnapshotLease
        // makes the old snapshot immediately invisible to new readers, while
        // legacy readers/snapshots keep the mtime expiry path.

        // B-244: IO Thread terminate 終端でも record_signal は削除せず Released にする。
        // PRE は missing では止めないため、shutdown/watchdog restart/drop の lifecycle 終了も
        // 明示 Stop として伝播させる。
        // 失敗時 warn のみ (設計判断 #8): IO Thread terminate 内 panic は thread
        // crash の連鎖のため避ける。NotFound は no signal として扱う。
        match StoragePaths::default_platform() {
            Ok(paths) => {
                release_record_reservation(
                    &paths.plugin_data_dir(),
                    &final_project_hash,
                    &final_iid,
                    &paired_pre_target,
                    "cleanup #4",
                );
                let base = paths.plugin_data_dir();
                let owned_session = record_sm
                    .record_session_id()
                    .or_else(|| record_sm.last_closed_session_id());
                let release_result =
                    record_signal::read_signal(&base, &final_project_hash, &final_iid)
                        .filter(|signal| {
                            owned_session.as_deref() == Some(signal.session_id.as_str())
                        })
                        .map_or(Ok(false), |expected| {
                            record_signal::mark_released_if_current(
                                &base,
                                &final_project_hash,
                                &final_iid,
                                &expected,
                            )
                        });
                match release_result {
                    Ok(true) => log::info!("[POST cleanup #4] mark_released ok"),
                    Ok(false) => log::info!("[POST cleanup #4] no signal to release"),
                    Err(e) => log::warn!("[POST cleanup #4] mark_released failed: {:?}", e),
                }

                // Step 12-C 統合点 #4 broadcast: originator として配置した
                // all_keep_signal/{POST_iid}.json を削除。delete_broadcast は冪等
                // (NotFound→Ok)。統合点 #2/#3 と重複呼出されても安全。失敗時 warn のみ。
                match all_keep_signal::delete_broadcast(
                    &paths.plugin_data_dir(),
                    &final_project_hash,
                    &final_iid,
                ) {
                    Ok(()) => log::info!(
                        "[POST shutdown #4 broadcast] delete_broadcast succeeded: instance={}",
                        final_iid
                    ),
                    Err(e) => log::warn!(
                        "[POST shutdown #4 broadcast] delete_broadcast failed: {:?}",
                        e
                    ),
                }

                // α-7' All Stop: own all_stop_signal/{POST_iid}.json も並列削除。
                match all_stop_signal::delete_stop_broadcast(
                    &paths.plugin_data_dir(),
                    &final_project_hash,
                    &final_iid,
                ) {
                    Ok(()) => log::info!(
                        "[POST shutdown #4 stop_broadcast] delete_stop_broadcast succeeded: instance={}",
                        final_iid
                    ),
                    Err(e) => log::warn!(
                        "[POST shutdown #4 stop_broadcast] delete_stop_broadcast failed: {:?}",
                        e
                    ),
                }
            }
            Err(e) => log::warn!("[POST cleanup #4] StoragePaths error: {:?}", e),
        }

        log::info!("[IOThread POST] terminated");
    })
}

/// `Arc<RwLock<String>>` から現在値を lazy-read（panic-safe）。
///
/// B-022 段階 1: chunk-restore 後の最新 instance_id を毎 tick / 各 use site で
/// 取得するための kirin_measure 内部ヘルパ。`hypha_post::read_instance_id_arc`
/// と同等の実装だが、kirin_measure crate からは hypha_* を参照できないため
/// 重複定義する。public は不要 (本ファイル + io_thread_pre.rs から使うのみ)。
pub(crate) fn read_instance_id_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// `Arc<RwLock<String>>` から `project_hash` を lazy-read（panic-safe）。
///
/// §4-5 Step 1: `read_instance_id_arc` と同位相。chunk-restore + cell update 後の
/// 最新 `project_hash` を毎 tick / 各 use site で取得し、editor() snapshot と
/// initialize() snapshot の divergence (§4-4 R-9 主因 a) を構造的に解消する。
pub(crate) fn read_project_hash_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// `Arc<RwLock<String>>` から `daw_session_id` を lazy-read（panic-safe）。
///
/// FFI/JUCE path ではこの Arc が engine 単位の session identity を保持する。
/// `crate::daw_session_id()` の process scope cell をここで読み直すと、Studio One の
/// 複数 Song/Project 同時オープン時に後発 document の identity へ吸われ、別棚の
/// PRE/POST と誤って同居する。IO Thread の各 use site は渡された Arc を正とする。
pub(crate) fn read_daw_session_id_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

/// `Arc<RwLock<String>>` から `pair_pre_name` を毎 tick snapshot で取得する
/// (B-027 段階 3-B α-7-1 / Step 6)。
///
/// `params.pair_pre_name` (`hypha_post::HyphaPostParams::pair_pre_name` /
/// `Arc<RwLock<String>>` / `#[persist]`) は GUI から書込される値。POST IO Thread の
/// 100ms tick で snapshot を取得し `serialize_post_json{,_minimal}` に渡すことで、
/// Q-A7 採用案 A (post.json schema 拡張による cross-instance 公開機構) を成立させる。
///
/// # poison fallback
/// `RwLock::read()` が `Err(PoisonError)` を返した場合は空文字 fallback。
/// 旧 schema (本 stage 前 plugin) 互換 (`PostTmpJson::pair_pre_name` は
/// `#[serde(default)]` で空文字 → `None` 同等) と一貫させ、IO Thread 経路を
/// pair_pre_name 取得失敗で停止させない (R-28 機能的沈黙)。
pub(crate) fn snapshot_pair_pre_name(arc: &Arc<RwLock<String>>) -> String {
    arc.read().map(|g| g.clone()).unwrap_or_default()
}

fn pair_claim_matches_desired_binding(
    claim: &crate::PairClaim,
    pre_instance_id: Option<&str>,
    project_hash: &str,
    post_instance_id: &str,
    pair_owner_id: &str,
    pair_claimed_at: f64,
) -> bool {
    pre_instance_id == Some(claim.pre_instance_id.as_str())
        && claim.project_hash == project_hash
        && claim.post_instance_id == post_instance_id
        && claim.pair_owner_id == pair_owner_id
        && claim.pair_claimed_at_bits == pair_claimed_at.to_bits()
}

#[cfg(test)]
#[path = "io_thread_post_pair_claim_tests.rs"]
mod pair_claim_binding_tests;

fn same_project_host_broadcast_matches(
    local_host_process_id: u32,
    remote_host_process_id: u32,
) -> bool {
    local_host_process_id != 0
        && remote_host_process_id != 0
        && local_host_process_id == remote_host_process_id
}

fn broadcast_scope_or_same_project_host_matches(
    local_daw_session_id: &str,
    local_host_process_id: u32,
    remote_daw_session_id: &str,
    remote_host_process_id: u32,
) -> bool {
    // Callers scan one project shelf at a time. The same-host branch only bridges
    // instance-scoped DAW IDs inside that already-selected shelf.
    crate::broadcast_scope_ids_match(
        local_daw_session_id,
        local_host_process_id,
        remote_daw_session_id,
        remote_host_process_id,
    ) || same_project_host_broadcast_matches(local_host_process_id, remote_host_process_id)
}

/// 1 ループの処理本体。
///
/// # B-021 Phase 1A: filesystem-discovery の優先順位
///
/// Pair未確定時だけ `discovery` が名前候補を1秒間隔で解決する。いったん exact PRE を
/// latchした後は、その1本の `pre.json` だけを読み、再走査しない。
///
/// `instance_dir` (POST 自身の post.json 書込先) は変更しない。POST 自身の
/// `project_uuid` で構築された path のままで、検出された PRE dir とは独立。
#[allow(clippy::too_many_arguments)]
/// B-108: latched-idle 表示値（Stale + 全Δ None + `last_active` クリア / 凍結値なし）。
fn delta_latched_idle() -> (DeltaResult, bool, Option<SignalState>) {
    (
        DeltaResult {
            mode: DeltaMode::Stale,
            ..Default::default()
        },
        true,
        None,
    )
}

/// B-108: 未ラッチ・ペアなし表示値（NoPre / `last_active` は resolve_delta_for_store がクリア）。
fn delta_no_pre() -> (DeltaResult, bool, Option<SignalState>) {
    (
        DeltaResult {
            mode: DeltaMode::NoPre,
            ..Default::default()
        },
        false,
        None,
    )
}

/// B-231: ラッチ先 PRE が明示 Bypassed。pair は維持し、表示は POST 単独へ戻す。
fn delta_pre_bypassed() -> (DeltaResult, bool, Option<SignalState>) {
    (
        DeltaResult {
            mode: DeltaMode::Bypassed,
            ..Default::default()
        },
        true,
        Some(SignalState::Bypassed),
    )
}

/// Pair binding remains authoritative while the paired PRE process is inactive. POST renders its
/// own absolute metrics until the same PRE instance resumes, then Δ resumes without re-pairing.
fn delta_pre_inactive() -> (DeltaResult, bool, Option<SignalState>) {
    (
        DeltaResult {
            mode: DeltaMode::PreInactive,
            ..Default::default()
        },
        true,
        Some(SignalState::Inactive),
    )
}

/// B-108: ラッチ意味論で表示Δを決める単一実装（`run_tick` の POST=Active 表示経路が呼ぶ）。
///
/// 戻り `(delta, store_directly, pre_signal_state)`:
/// - `store_directly = true` は **PREから差分を作れないがpairを維持する状態**。Stale は全Δ None、
///   PreInactive / Bypassed は POST 単独表示へ切り替える。いずれも `run_tick` は
///   `resolve_delta_for_store` を経由せずそのまま格納し、古い凍結Δの復活を防ぐ。
/// - `false` は従来どおり `resolve_delta_for_store`（Active は last_active 保存、active-pair の
///   fs-lag Stale は B-048 凍結保持、NoPre は last_active クリア）。
///
/// ラッチ規律（B-108）:
/// - Record 中はラッチ凍結（アンラッチ/再選定しない / W-284 self_check-skip と同型）。
///   PRE pre.json の stale/missing は表示上だけ latched-idle にし、Record は Stop / idle timeout
///   まで保持する。
/// - Watch 中: pair 名変更/クリアで即アンラッチ。ラッチ先 pre.json を直読するが、実消滅
///   （不在/stale>TTL/rename）は解除理由にしない。明示 pair 名が残る限り、ラッチ済み instance を
///   権威にして muted Δ/--- を返す。未ラッチ時だけ Arm ゲート（B-104）で初回解決する。
///   同名2台目が現れてもラッチ済みなら再選定しない。
#[cfg(test)]
fn compute_latched_display(
    kirin_root: &Path,
    pair_pre_name: &str,
    post: &MeasureResult,
    pair_opt: Option<&str>,
    recording: bool,
    latched: &Mutex<Option<LatchedPre>>,
) -> Result<(DeltaResult, bool, Option<SignalState>), String> {
    compute_latched_display_for_post_project(
        kirin_root,
        pair_pre_name,
        "",
        "",
        post,
        pair_opt,
        recording,
        true,
        latched,
    )
}

#[allow(clippy::too_many_arguments)]
fn compute_latched_display_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    post_daw_session_id: &str,
    post: &MeasureResult,
    _pair_opt: Option<&str>,
    recording: bool,
    allow_unlatched_resolution: bool,
    latched: &Mutex<Option<LatchedPre>>,
) -> Result<(DeltaResult, bool, Option<SignalState>), String> {
    let current = latched.lock().ok().and_then(|g| g.clone());

    if recording {
        // Record 中: ラッチ凍結（再解決/アンラッチしない）。
        let Some(l) = current else {
            return Ok(delta_no_pre()); // ラッチ無しで Record（理論上稀）→ 従来 NoPre。
        };
        return match read_pre_at(&l.pre_json) {
            Some(st) if st.fresh && st.active => {
                let (d, ss) = compute_delta_for_pre_file(&l.pre_json, post)?;
                Ok((d, false, ss))
            }
            Some(st) if st.signal_state == Some(SignalState::Bypassed) => Ok(delta_pre_bypassed()),
            Some(st) if st.signal_state == Some(SignalState::Inactive) => Ok(delta_pre_inactive()),
            // 一時 idle / stale / missing → latched-idle 表示。missing 単独では Record を閉じない。
            _ => Ok(delta_latched_idle()),
        };
    }

    // Watch 中。
    // (1) 名前変更/クリア → 即アンラッチ。
    let keep = current
        .as_ref()
        .is_some_and(|l| l.name == pair_pre_name && !l.instance_id.is_empty());
    if current.is_some() && !keep {
        if let Ok(mut g) = latched.lock() {
            *g = None;
        }
    }

    // (2) ラッチ維持中。Watch lease・snapshot freshnessは計測可否だけを決め、ユーザーが
    // 確定した exact binding は変更しない。Record中は上のfreeze分岐が先に返るため不変。
    if keep {
        let l = current.expect("keep implies current is Some");
        // A saved exact locator first waits for a current-process owner at that same path. The
        // previous DAW process deliberately leaves its JSON behind with a released lease; treating
        // that residue as a new deletion would discard the saved pair and re-enter name discovery.
        if l.readiness == crate::LatchedPreReadiness::RestoredWaiting
            && !crate::pairing_scope::confirm_restored_latch_runtime(latched)
        {
            return Ok(delta_latched_idle());
        }
        match read_pre_at(&l.pre_json) {
            // fresh + active → 通常 Δ。名前の一時不一致では解除しない。
            Some(st) if st.fresh && st.active => {
                let (d, ss) = compute_delta_for_pre_file(&l.pre_json, post)?;
                return Ok((d, false, ss));
            }
            // 明示 OFF は pair 維持のまま POST 単独表示に戻す。
            Some(st) if st.signal_state == Some(SignalState::Bypassed) => {
                return Ok(delta_pre_bypassed());
            }
            // PRE process inactive: keep exact binding, show POST absolute until it resumes.
            Some(st) if st.signal_state == Some(SignalState::Inactive) => {
                return Ok(delta_pre_inactive());
            }
            // stopped writer / stale / missing / rename → ラッチ維持のまま muted Δ/---。
            _ => return Ok(delta_latched_idle()),
        }
    }

    // (3) 未ラッチ（含む直前アンラッチ）→ pair 名があれば Arm ゲートで初回/再ラッチ。
    if pair_pre_name.is_empty() {
        return Ok(delta_no_pre());
    }
    if !allow_unlatched_resolution {
        return Ok(delta_no_pre());
    }
    match select_target_pre_for_arm_for_post_project_in_session(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        post_daw_session_id,
    ) {
        Some(sel) => {
            let pre_json = sel.pre_json.clone();
            let project_dir = sel.project_dir.clone();
            let daw_session_id = sel.daw_session_id.clone();
            if let Ok(mut g) = latched.lock() {
                *g = Some(LatchedPre {
                    name: pair_pre_name.to_string(),
                    instance_id: sel.instance_id,
                    project_dir: project_dir.clone(),
                    pre_json: pre_json.clone(),
                    daw_session_id,
                    host_process_id: sel.host_process_id,
                    readiness: crate::LatchedPreReadiness::Confirmed,
                });
            }
            // 初回ラッチ直後の同 tick 表示。
            match read_pre_at(&pre_json) {
                Some(st) if st.fresh && st.active => {
                    let (d, ss) = compute_delta_for_pre_file(&pre_json, post)?;
                    Ok((d, false, ss))
                }
                Some(st) if st.signal_state == Some(SignalState::Bypassed) => {
                    Ok(delta_pre_bypassed())
                }
                Some(st) if st.signal_state == Some(SignalState::Inactive) => {
                    Ok(delta_pre_inactive())
                }
                _ => Ok(delta_latched_idle()),
            }
        }
        // 0 件 or 曖昧(2+) → 沈黙（NoPre）。
        None => Ok(delta_no_pre()),
    }
}

#[allow(clippy::too_many_arguments)]
fn run_tick(
    // B-059: PRE 選定は select_target_pre(kirin_root) に一本化。project_dir_hint / discovery
    // (単一最新 dir の throttle/cache) は不要化（caller は据え置きで `_` 受け）。
    _project_dir_hint: &Path,
    kirin_root: &Path,
    discovery: &mut PostDiscoveryState,
    instance_dir: &Path,
    post_file: &Path,
    instance_id: &str,
    watch_owner_id: &str,
    post_result: &Arc<Mutex<MeasureResult>>,
    delta_result: &Arc<Mutex<DeltaResult>>,
    signal_state_atom: &Arc<AtomicU8>,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    post_project_hash: &str,
    daw_session_id: &str,
    // B-108: recording=Record 中はラッチ凍結（アンラッチ/再選定しない）。latched=display と
    // keep/Arm が共有する単一ラッチ（io_thread が毎 tick 維持、keep が resolve_arm_target で読む）。
    recording: bool,
    latched: &Mutex<Option<LatchedPre>>,
) -> Result<(), String> {
    let state = load_signal_state(signal_state_atom);

    fs::create_dir_all(instance_dir).map_err(|e| format!("create_dir_all: {e}"))?;

    if state != SignalState::Active {
        let mut delta_locked =
            crate::sync_recovery::lock_recover(delta_result, "POST inactive delta");
        let previous_delta = delta_locked.clone();
        *delta_locked = resolve_delta_for_non_active_post(state, pair_pre_name, &previous_delta);

        // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name は閉路 1 tick の snapshot。
        // Q-A7 採用案 A (post.json schema 拡張による cross-instance 公開)。
        // W-281: pair_claimed_at も同 tick snapshot を書き出す (後着優先 self check 軸)。
        let paired_pre_instance_id = crate::paired_pre_instance_id(latched).unwrap_or_default();
        let json = serialize_post_json_minimal_with_daw_owner_and_pair_instance(
            instance_id,
            state,
            pair_pre_name,
            pair_claimed_at,
            daw_session_id,
            watch_owner_id,
            &paired_pre_instance_id,
        );
        crate::atomic_file::write_bytes_atomic(post_file, json.as_bytes())
            .map_err(|e| format!("atomic write: {e}"))?;
        return Ok(());
    }

    // B-059: 表示=commit 一本化。commit (trigger_keep_internal) と同一の
    // `select_target_pre` で PRE を選定する。pair_pre_name 空 / 同名複数 / 不在 /
    // Inactive / 古t は None (= 表示 NoPre 沈黙 = commit 拒否)。
    let pair_opt = Some(pair_pre_name).filter(|s| !s.is_empty());

    let post = crate::sync_recovery::lock_recover(post_result, "POST Watch result").clone();

    // B-108: ラッチ意味論で表示Δを決める（select_target_pre 直呼びを廃止）。一度成立した結合は
    // 無音/停止/一時鮮度揺らぎ/同名2台目では NoPre に落とさず、解除は名前変更/クリアと PRE 実消滅のみ。
    let needs_resolution = !pair_pre_name.is_empty()
        && latched
            .lock()
            .map(|binding| binding.is_none())
            .unwrap_or(true);
    let resolution_now = Instant::now();
    let allow_unlatched_resolution = !needs_resolution || discovery.should_rescan(resolution_now);
    if needs_resolution && allow_unlatched_resolution {
        // Record the bounded discovery attempt even when no PRE exists. Otherwise an unresolved
        // selector would walk the live registry on every 100 ms IO tick.
        discovery.record_scan(resolution_now, None);
    }
    let (new_delta, store_directly, pre_signal_state) = compute_latched_display_for_post_project(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        daw_session_id,
        &post,
        pair_opt,
        recording,
        allow_unlatched_resolution,
        latched,
    )?;

    // last_active 規律:
    // - store_directly（latched-idle / PRE bypassed / PRE inactive）→ そのまま格納し、
    //   古い凍結Δを復活させない。pair binding 自体は別管理なので維持される。
    // - それ以外 → resolve_delta_for_store（Active 保存 / active-pair fs-lag Stale は B-048 凍結保持
    //   / NoPre は last_active クリア）。
    {
        let mut delta_locked =
            crate::sync_recovery::lock_recover(delta_result, "POST active delta");
        let prev_last_active = delta_locked.last_active.clone();
        *delta_locked = if store_directly {
            new_delta
        } else {
            resolve_delta_for_store(new_delta, prev_last_active)
        };
    }

    // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name は閉路 1 tick の snapshot
    // (Q-A7 採用案 A 完成 / cross-instance 公開機構)。
    // W-281: pair_claimed_at も同 tick snapshot (後着優先 self check 判定軸)。
    let paired_pre_instance_id = crate::paired_pre_instance_id(latched).unwrap_or_default();
    let json = serialize_post_json_with_daw_owner_and_pair_instance(
        instance_id,
        state,
        pre_signal_state,
        &post,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        watch_owner_id,
        &paired_pre_instance_id,
    );
    crate::atomic_file::write_bytes_atomic(post_file, json.as_bytes())
        .map_err(|e| format!("atomic write: {e}"))?;

    Ok(())
}

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
#[path = "io_thread_post_stop_keep_barrier_tests.rs"]
mod b222_all_stop_keep_barrier_tests;

#[cfg(test)]
#[path = "io_thread_post_compute_delta_tests.rs"]
mod compute_delta_selection_tests;
#[cfg(test)]
#[path = "io_thread_post_latch_state_tests.rs"]
mod latched_delta_state_tests;
#[cfg(test)]
#[path = "io_thread_post_preset_tests.rs"]
mod preset_poll_tests;
#[cfg(test)]
mod latched_delta_tests {
    use super::*;
    use std::sync::atomic::AtomicU64;

    // ── B-108: pairing latch（compute_latched_display）─────────────────────────

    /// kirin_root（`{puid}` の親）を一意な temp に作る。
    fn isolated_dir(tag: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_{tag}_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// kirin_root に `{puid}/{iid}/pre.json`（signal_state/t 可変）を書き、pre.json パスを返す。
    fn write_pre_latch(
        kirin_root: &Path,
        puid: &str,
        iid: &str,
        name: &str,
        signal_state: &str,
        t: &str,
    ) -> PathBuf {
        let dir = kirin_root.join(puid).join(iid);
        fs::create_dir_all(&dir).unwrap();
        let host_process_id = crate::post_candidates::current_host_process_id();
        let json = format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{iid}","name":"{name}","host_process_id":{host_process_id},"signal_state":"{signal_state}","t":"{t}","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
        );
        let p = dir.join("pre.json");
        fs::write(&p, json).unwrap();
        p
    }

    fn attach_pre_owner(pre_json: &Path) -> crate::watch_snapshot_lease::WatchSnapshotLease {
        let instance_dir = pre_json.parent().unwrap();
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(instance_dir).unwrap();
        let mut json: serde_json::Value =
            serde_json::from_slice(&fs::read(pre_json).unwrap()).unwrap();
        json["watch_owner_id"] = serde_json::json!(lease.owner_id());
        fs::write(pre_json, json.to_string()).unwrap();
        lease
    }
    fn latch_now() -> String {
        chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
    }
    fn latch_old(secs: i64) -> String {
        (chrono::Utc::now() - chrono::Duration::seconds(secs))
            .format("%Y-%m-%dT%H:%M:%SZ")
            .to_string()
    }
    fn latch_post() -> MeasureResult {
        MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
            ..Default::default()
        }
    }

    /// T2: ラッチ後に同名 2 台目 PRE が現れてもラッチ先 instance_id 不変（再選定しない）。
    #[test]
    fn latch_invariant_to_second_same_name() {
        let root = isolated_dir("latch_2nd");
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let latched = std::sync::Mutex::new(None);
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(
            latched.lock().unwrap().as_ref().unwrap().instance_id,
            "iid-A"
        );
        // 同名 2 台目出現（曖昧 → 素の Arm 選定なら None）。
        write_pre_latch(&root, "puid-2", "iid-B", "snare", "active", &latch_now());
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(
            latched.lock().unwrap().as_ref().unwrap().instance_id,
            "iid-A",
            "同名2台目でもラッチ先不変（再選定しない）"
        );
    }

    /// T3: pair 名変更/クリアで即アンラッチ（Watch 中）。
    #[test]
    fn latch_name_change_unlatches() {
        let root = isolated_dir("latch_rename");
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let latched = std::sync::Mutex::new(None);
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert!(latched.lock().unwrap().is_some());
        // 名前変更（"kick" 不在）→ アンラッチ + NoPre。
        let (d, _, _) =
            compute_latched_display(&root, "kick", &latch_post(), Some("kick"), false, &latched)
                .unwrap();
        assert!(latched.lock().unwrap().is_none(), "名前変更で即アンラッチ");
        assert_eq!(d.mode, DeltaMode::NoPre);
        // クリア（空）→ アンラッチ + NoPre。
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert!(latched.lock().unwrap().is_some());
        let (d2, _, _) =
            compute_latched_display(&root, "", &latch_post(), None, false, &latched).unwrap();
        assert!(latched.lock().unwrap().is_none(), "クリアで即アンラッチ");
        assert_eq!(d2.mode, DeltaMode::NoPre);
    }

    #[test]
    fn unnamed_exact_latch_remains_valid_until_selection_layer_clears_it() {
        let root = isolated_dir("latch_unnamed_exact");
        let pre_json = write_pre_latch(&root, "puid-1", "iid-A", "", "active", &latch_now());
        let latched = std::sync::Mutex::new(Some(LatchedPre {
            name: String::new(),
            instance_id: "iid-A".to_string(),
            project_dir: root.join("puid-1"),
            pre_json,
            daw_session_id: None,
            host_process_id: Some(crate::post_candidates::current_host_process_id()),
            readiness: crate::LatchedPreReadiness::Confirmed,
        }));

        let (delta, _, _) =
            compute_latched_display(&root, "", &latch_post(), None, false, &latched).unwrap();
        assert_eq!(delta.mode, DeltaMode::Active);
        assert_eq!(
            latched
                .lock()
                .unwrap()
                .as_ref()
                .map(|pre| pre.instance_id.as_str()),
            Some("iid-A")
        );
    }

    #[test]
    fn restored_exact_latch_waits_for_pre_loaded_later_without_name_rescan() {
        let root = isolated_dir("restored_exact_wait");
        let pre_json = write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let old_owner = attach_pre_owner(&pre_json);
        drop(old_owner); // normal previous-DAW residue: JSON remains, kernel lease is released

        let competing = write_pre_latch(&root, "puid-2", "iid-B", "snare", "active", &latch_now());
        let _competing_owner = attach_pre_owner(&competing);
        let latched = std::sync::Mutex::new(Some(LatchedPre {
            name: "snare".to_string(),
            instance_id: "iid-A".to_string(),
            project_dir: root.join("puid-1"),
            pre_json: pre_json.clone(),
            daw_session_id: Some("daw-1".to_string()),
            host_process_id: Some(crate::post_candidates::current_host_process_id()),
            readiness: crate::LatchedPreReadiness::RestoredWaiting,
        }));

        let (waiting, _, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(waiting.mode, DeltaMode::Stale);
        assert_eq!(
            latched
                .lock()
                .unwrap()
                .as_ref()
                .map(|pre| pre.instance_id.as_str()),
            Some("iid-A"),
            "released previous-process residue must not release the saved exact binding or select iid-B by name"
        );

        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let _new_owner = attach_pre_owner(&pre_json);
        let (active, _, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(active.mode, DeltaMode::Active);
        assert_eq!(
            latched
                .lock()
                .unwrap()
                .as_ref()
                .map(|pre| pre.instance_id.as_str()),
            Some("iid-A"),
            "late PRE publication must resume the saved exact pair, not select by name"
        );
        assert_eq!(
            latched.lock().unwrap().as_ref().map(|pre| pre.readiness),
            Some(crate::LatchedPreReadiness::Confirmed),
            "the fixed restore latch becomes a normal live latch only after current owner proof"
        );
    }

    /// PRE Watch lease終了は計測をstaleにするだけで、exact latchを別instanceへ移さない。
    #[test]
    fn released_watch_owner_keeps_exact_pair_and_rejects_name_retargeting() {
        let root = isolated_dir("latch_recreate");
        let old = write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let lease = attach_pre_owner(&old);
        let latched = std::sync::Mutex::new(None);
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert!(latched.lock().unwrap().is_some());

        drop(lease);
        // Watch owner release and metric freshness are separate signals. The just-written snapshot
        // remains usable until its timestamp TTL expires; age that exact snapshot to exercise the
        // unavailable-measurement path without changing the pair identity.
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_old(20));
        let (d, _, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert!(
            latched.lock().unwrap().is_some(),
            "released Watch owner must not detach the exact pair"
        );
        assert_eq!(d.mode, DeltaMode::Stale);

        write_pre_latch(&root, "puid-2", "iid-B", "snare", "active", &latch_now());
        let (d2, _, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(
            d2.mode,
            DeltaMode::Stale,
            "same-name PRE must not steal the pair"
        );
        assert_eq!(
            latched
                .lock()
                .unwrap()
                .as_ref()
                .map(|pre| pre.instance_id.as_str()),
            Some("iid-A")
        );
    }

    /// T5: ラッチ先 pre.json が stale > NO_PRE_SECS(10s) でもアンラッチしない。
    #[test]
    fn latch_stale_beyond_ttl_keeps_pair_latched() {
        let root = isolated_dir("latch_stale");
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let latched = std::sync::Mutex::new(None);
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert!(latched.lock().unwrap().is_some());
        // t を 20s 古く（> NO_PRE_SECS=10）→ muted Δ/---。ラッチは外さない。
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_old(20));
        let (d, _, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert!(
            latched.lock().unwrap().is_some(),
            "stale>TTL でも明示 pair は維持"
        );
        assert_eq!(d.mode, DeltaMode::Stale);
    }

    /// T5b: ラッチ先 PRE の name field が一時的に違っても、instance ラッチを優先する。
    #[test]
    fn latch_pre_name_mismatch_keeps_instance_authority() {
        let root = isolated_dir("latch_name_mismatch");
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let latched = std::sync::Mutex::new(None);
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();

        write_pre_latch(
            &root,
            "puid-1",
            "iid-A",
            "snare_tmp",
            "active",
            &latch_now(),
        );
        let (d, sd, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(d.mode, DeltaMode::Active);
        assert!(!sd);
        assert_eq!(
            latched.lock().unwrap().as_ref().unwrap().instance_id,
            "iid-A"
        );
    }

    /// T7: Record 中（recording=true）はラッチ凍結 — 名前変更でもアンラッチしない（W-284 同型）。
    #[test]
    fn latch_frozen_during_record() {
        let root = isolated_dir("latch_record");
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let latched = std::sync::Mutex::new(None);
        // Watch で初回ラッチ。
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert!(latched.lock().unwrap().is_some());
        // Record 中に名前変更（別名）→ アンラッチしない（凍結）。
        let _ = compute_latched_display(&root, "kick", &latch_post(), Some("kick"), true, &latched)
            .unwrap();
        assert_eq!(
            latched.lock().unwrap().as_ref().unwrap().instance_id,
            "iid-A",
            "Record 中は名前変更でもラッチ凍結"
        );
    }
}

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
mod post_candidate_tests {
    use super::*;
    use crate::{
        active_post_project_uuids_for_broadcast_scope, active_post_project_uuids_for_daw_session,
        enumerate_active_post_pair_candidates_for_broadcast_scope,
        enumerate_active_post_pair_candidates_for_daw_session,
        host_scope_has_other_active_post_project,
    };
    use std::sync::atomic::AtomicU64;

    fn unique_root(label: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("kirin_post_cand_{label}_{pid}_{n}_{now}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn write_post_json(
        kirin_root: &Path,
        project_uuid: &str,
        instance_id: &str,
        signal_state: SignalState,
        pre_signal_state: Option<SignalState>,
        pair_pre_name: &str,
    ) -> PathBuf {
        let dir = kirin_root.join(project_uuid).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let post_file = dir.join("post.json");
        let json = match signal_state {
            SignalState::Active => serialize_post_json(
                instance_id,
                signal_state,
                pre_signal_state,
                &MeasureResult::default(),
                pair_pre_name,
                0.0,
            ),
            _ => serialize_post_json_minimal(instance_id, signal_state, pair_pre_name, 0.0),
        };
        fs::write(&post_file, json.as_bytes()).unwrap();
        post_file
    }

    fn write_post_json_with_daw_and_host(
        kirin_root: &Path,
        project_uuid: &str,
        instance_id: &str,
        pair_pre_name: &str,
        daw_session_id: &str,
        host_process_id: u32,
    ) -> PathBuf {
        let post_file = write_post_json_with_daw(
            kirin_root,
            project_uuid,
            instance_id,
            pair_pre_name,
            daw_session_id,
        );
        let mut json: serde_json::Value =
            serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
        json["host_process_id"] = serde_json::json!(host_process_id);
        fs::write(&post_file, serde_json::to_vec(&json).unwrap()).unwrap();
        post_file
    }

    fn write_legacy_post_json_with_host(
        kirin_root: &Path,
        project_uuid: &str,
        instance_id: &str,
        pair_pre_name: &str,
        host_process_id: u32,
    ) -> PathBuf {
        let post_file = write_post_json(
            kirin_root,
            project_uuid,
            instance_id,
            SignalState::Active,
            Some(SignalState::Active),
            pair_pre_name,
        );
        let mut json: serde_json::Value =
            serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
        json.as_object_mut().unwrap().remove("daw_session_id");
        json["host_process_id"] = serde_json::json!(host_process_id);
        fs::write(&post_file, serde_json::to_vec(&json).unwrap()).unwrap();
        post_file
    }

    fn write_post_json_with_daw(
        kirin_root: &Path,
        project_uuid: &str,
        instance_id: &str,
        pair_pre_name: &str,
        daw_session_id: &str,
    ) -> PathBuf {
        let dir = kirin_root.join(project_uuid).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let post_file = dir.join("post.json");
        let json = serialize_post_json_with_daw(
            instance_id,
            SignalState::Active,
            Some(SignalState::Active),
            &MeasureResult::default(),
            pair_pre_name,
            0.0,
            daw_session_id,
        );
        fs::write(&post_file, json.as_bytes()).unwrap();
        post_file
    }

    /// scan_post_candidates_in: 通常 case (Active 1 件) → instance_id / project_uuid /
    /// pair_pre_name (空文字 → None / 非空 → Some) / path が正しく構築される。
    #[test]
    fn scan_in_active_with_pair_pre_name() {
        let root = unique_root("scan_active");
        let project_uuid = "pj-AAA";
        let _ = write_post_json(
            &root,
            project_uuid,
            "post-iid-1",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-Master",
        );
        let project_dir = root.join(project_uuid);
        let cands = scan_post_candidates_in(&project_dir);
        assert_eq!(cands.len(), 1);
        let c = &cands[0];
        assert_eq!(c.instance_id, "post-iid-1");
        assert_eq!(c.project_uuid, project_uuid);
        assert!(c.daw_session_id.is_none());
        assert_eq!(c.host_process_id, Some(crate::current_host_process_id()));
        assert_eq!(c.pair_pre_name.as_deref(), Some("PRE-Master"));
        assert!(c.path.ends_with("post.json"));
    }

    #[test]
    fn released_post_runtime_cannot_block_pair_scope_for_thirty_seconds() {
        let root = unique_root("released_post_scope");
        let project_uuid = "pj-OLD";
        let post_file = write_post_json(
            &root,
            project_uuid,
            "post-old",
            SignalState::Active,
            Some(SignalState::Active),
            "2Mix",
        );
        let instance_dir = post_file.parent().unwrap();
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(instance_dir).unwrap();
        let mut json: serde_json::Value =
            serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
        json["watch_owner_id"] = serde_json::json!(lease.owner_id());
        fs::write(&post_file, serde_json::to_vec(&json).unwrap()).unwrap();
        let host_process_id = crate::current_host_process_id();
        assert!(host_scope_has_other_active_post_project(
            &root,
            "pj-CURRENT",
            host_process_id
        ));

        drop(lease);
        assert!(
            !host_scope_has_other_active_post_project(&root, "pj-CURRENT", host_process_id),
            "a normally removed POST must stop excluding every PRE immediately"
        );
    }

    /// B-131 (G-115-380) 感度確証: `"` / `\` を含む pair_pre_name の POST が
    /// serialize → scan の往復で pairing 候補から **消えない**。
    /// 旧: 生補間 → 不正 JSON → `scan_post_candidates_in` が無言 skip → pairing 消失
    /// （PRE 選択済でも対 POST が候補から欠落し All Keep / pair が成立しない R-28 欠陥）。
    #[test]
    fn scan_in_survives_special_char_pair_pre_name() {
        let root = unique_root("scan_special");
        let project_uuid = "pj-SPECIAL";
        let name = "PRE\"x\\y";
        let _ = write_post_json(
            &root,
            project_uuid,
            "post-iid-1",
            SignalState::Active,
            Some(SignalState::Active),
            name,
        );
        let project_dir = root.join(project_uuid);
        let cands = scan_post_candidates_in(&project_dir);
        assert_eq!(
            cands.len(),
            1,
            "special-char pair_pre_name POST must survive scan (not silently skipped)"
        );
        assert_eq!(cands[0].pair_pre_name.as_deref(), Some(name));
    }

    /// B-131 (G-115-380): 真に壊れた post.json は無言 skip されず log surface され、かつ
    /// 同 dir の valid POST 候補は返る（不正 1 件が sibling を巻き込まない / R-28 sweep 継続）。
    /// PRE 側 pre_candidates scan (B-077) の log::warn surface と対称。
    #[test]
    fn scan_in_skips_corrupt_but_keeps_valid_sibling() {
        let root = unique_root("scan_corrupt");
        let project_uuid = "pj-CORRUPT";
        let _ = write_post_json(
            &root,
            project_uuid,
            "post-good",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-Good",
        );
        // 故意に壊した post.json（不正 JSON）。
        let bad_dir = root.join(project_uuid).join("post-bad");
        fs::create_dir_all(&bad_dir).unwrap();
        fs::write(bad_dir.join("post.json"), b"{ not valid json").unwrap();

        let project_dir = root.join(project_uuid);
        let cands = scan_post_candidates_in(&project_dir);
        assert_eq!(cands.len(), 1, "corrupt skipped, valid sibling kept");
        assert_eq!(cands[0].instance_id, "post-good");
    }

    /// pair_pre_name が空文字 → PostCandidate.pair_pre_name == None (PRE 版 name None
    /// 対称)。
    #[test]
    fn scan_in_empty_pair_pre_name_to_none() {
        let root = unique_root("scan_empty");
        let project_uuid = "pj-BBB";
        let _ = write_post_json(
            &root,
            project_uuid,
            "post-iid-2",
            SignalState::Active,
            Some(SignalState::Active),
            "",
        );
        let cands = scan_post_candidates_in(&root.join(project_uuid));
        assert_eq!(cands.len(), 1);
        assert!(cands[0].pair_pre_name.is_none());
    }

    /// signal_state == "bypassed" の POST は候補から除外 (PRE 版 Bypass 防御対称)。
    #[test]
    fn scan_in_bypassed_excluded() {
        let root = unique_root("scan_bypass");
        let project_uuid = "pj-CCC";
        let _ = write_post_json(
            &root,
            project_uuid,
            "post-iid-active",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-X",
        );
        let _ = write_post_json(
            &root,
            project_uuid,
            "post-iid-bypassed",
            SignalState::Bypassed,
            None,
            "PRE-Y",
        );
        let cands = scan_post_candidates_in(&root.join(project_uuid));
        assert_eq!(cands.len(), 1, "only Active POST remains: {:?}", cands);
        assert_eq!(cands[0].instance_id, "post-iid-active");
    }

    /// signal_state == "inactive" の POST は候補化される (PRE 版対称)。
    #[test]
    fn scan_in_inactive_included() {
        let root = unique_root("scan_inactive");
        let project_uuid = "pj-DDD";
        let _ = write_post_json(
            &root,
            project_uuid,
            "post-iid-inactive",
            SignalState::Inactive,
            None,
            "PRE-Z",
        );
        let cands = scan_post_candidates_in(&root.join(project_uuid));
        assert_eq!(cands.len(), 1);
        assert_eq!(cands[0].instance_id, "post-iid-inactive");
    }

    /// 旧 schema (pair_pre_name field 不在) の post.json → pair_pre_name == None。
    #[test]
    fn scan_in_legacy_schema_no_pair_pre_name_field() {
        let root = unique_root("scan_legacy");
        let project_uuid = "pj-EEE";
        let dir = root.join(project_uuid).join("post-iid-legacy");
        fs::create_dir_all(&dir).unwrap();
        let legacy = r#"{"v":2,"role":"POST","instance_id":"post-iid-legacy","signal_state":"active","pre_signal_state":"active","t":"2026-05-04T10:00:00.000Z","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
        fs::write(dir.join("post.json"), legacy).unwrap();
        let cands = scan_post_candidates_in(&root.join(project_uuid));
        assert_eq!(cands.len(), 1);
        assert!(cands[0].pair_pre_name.is_none());
    }

    /// scan_post_candidates_in: SIGNALS_SUBDIR (record_signal/) は除外。
    #[test]
    fn scan_in_excludes_signals_subdir() {
        let root = unique_root("scan_signals");
        let project_uuid = "pj-FFF";
        let project_dir = root.join(project_uuid);
        fs::create_dir_all(project_dir.join(SIGNALS_SUBDIR)).unwrap();
        // SIGNALS_SUBDIR 内に post.json があっても候補化されないこと。
        fs::write(
            project_dir.join(SIGNALS_SUBDIR).join("post.json"),
            r#"{"instance_id":"x","signal_state":"active","t":"x"}"#,
        )
        .unwrap();
        let _ = write_post_json(
            &root,
            project_uuid,
            "post-iid-real",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-Q",
        );
        let cands = scan_post_candidates_in(&project_dir);
        assert_eq!(cands.len(), 1);
        assert_eq!(cands[0].instance_id, "post-iid-real");
    }

    /// scan_post_candidates_in: 戻り順は instance_id 辞書順 (PRE 版対称 / 再現性)。
    #[test]
    fn scan_in_sorted_by_instance_id() {
        let root = unique_root("scan_sort");
        let project_uuid = "pj-GGG";
        for iid in &["post-c", "post-a", "post-b"] {
            let _ = write_post_json(
                &root,
                project_uuid,
                iid,
                SignalState::Active,
                Some(SignalState::Active),
                "PRE-S",
            );
        }
        let cands = scan_post_candidates_in(&root.join(project_uuid));
        let ids: Vec<&str> = cands.iter().map(|c| c.instance_id.as_str()).collect();
        assert_eq!(ids, vec!["post-a", "post-b", "post-c"]);
    }

    /// scan_post_candidates_in: 不在 / 非 dir 入力 → 空 Vec (silently skip)。
    #[test]
    fn scan_in_missing_dir_returns_empty() {
        let nonexistent = std::env::temp_dir().join("kirin_post_cand_does_not_exist");
        let _ = fs::remove_dir_all(&nonexistent);
        let cands = scan_post_candidates_in(&nonexistent);
        assert!(cands.is_empty());
    }

    /// discover_active_post_dirs: fresh post.json を持つ project_uuid dir のみ列挙。
    #[test]
    fn discover_returns_fresh_dirs() {
        let root = unique_root("discover_fresh");
        // 2 project_uuid dir / それぞれ Active POST 1 件
        let _ = write_post_json(
            &root,
            "pj-AA",
            "post-1",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-1",
        );
        let _ = write_post_json(
            &root,
            "pj-BB",
            "post-2",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-2",
        );
        let dirs = discover_active_post_dirs(&root);
        let names: Vec<String> = dirs
            .iter()
            .map(|d| d.file_name().unwrap().to_string_lossy().to_string())
            .collect();
        assert_eq!(names, vec!["pj-AA".to_string(), "pj-BB".to_string()]);
    }

    /// discover_active_post_dirs: 戻り順は file_name 辞書順固定 (G-115-53 対称)。
    #[test]
    fn discover_sorted_by_file_name() {
        let root = unique_root("discover_sort");
        for pj in &["pj-CC", "pj-AA", "pj-BB"] {
            let _ = write_post_json(
                &root,
                pj,
                "post-x",
                SignalState::Active,
                Some(SignalState::Active),
                "PRE-X",
            );
        }
        let dirs = discover_active_post_dirs(&root);
        let names: Vec<String> = dirs
            .iter()
            .map(|d| d.file_name().unwrap().to_string_lossy().to_string())
            .collect();
        assert_eq!(names, vec!["pj-AA", "pj-BB", "pj-CC"]);
    }

    /// discover_active_post_dirs: 空 root / 不在 → 空 Vec。
    #[test]
    fn discover_empty_root_returns_empty() {
        let nonexistent = std::env::temp_dir().join("kirin_post_cand_discover_does_not_exist");
        let _ = fs::remove_dir_all(&nonexistent);
        let dirs = discover_active_post_dirs(&nonexistent);
        assert!(dirs.is_empty());
        let empty_root = unique_root("discover_empty");
        let dirs2 = discover_active_post_dirs(&empty_root);
        assert!(dirs2.is_empty());
    }

    /// discover_active_post_dirs: stale (mtime > DISCOVERY_STALE_SECS) は除外。
    /// post.json mtime を過去に書き戻してチェック。
    #[test]
    fn discover_excludes_stale_dirs() {
        use std::fs::{File, FileTimes};
        let root = unique_root("discover_stale");
        let fresh = write_post_json(
            &root,
            "pj-FRESH",
            "post-fresh",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-F",
        );
        let stale = write_post_json(
            &root,
            "pj-STALE",
            "post-stale",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-S",
        );
        // stale 側 mtime を threshold より古く設定。
        let old = SystemTime::now() - Duration::from_secs(DISCOVERY_STALE_SECS + 5);
        let times = FileTimes::new().set_modified(old).set_accessed(old);
        File::options()
            .write(true)
            .open(&stale)
            .unwrap()
            .set_times(times)
            .unwrap();
        // 不変監視: fresh 側は手を入れない。
        let _ = fresh;

        let dirs = discover_active_post_dirs(&root);
        let names: Vec<String> = dirs
            .iter()
            .map(|d| d.file_name().unwrap().to_string_lossy().to_string())
            .collect();
        assert_eq!(names, vec!["pj-FRESH".to_string()]);
    }

    /// enumerate_active_post_pair_candidates: 多 project_uuid dir flatten + 順序。
    #[test]
    fn enumerate_flattens_multiple_projects() {
        let root = unique_root("enum_flatten");
        // pj-AA: 2 candidates (post-a / post-b)
        let _ = write_post_json(
            &root,
            "pj-AA",
            "post-b",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-AA",
        );
        let _ = write_post_json(
            &root,
            "pj-AA",
            "post-a",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-AA",
        );
        // pj-BB: 1 candidate
        let _ = write_post_json(
            &root,
            "pj-BB",
            "post-c",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-BB",
        );

        let cands = enumerate_active_post_pair_candidates(&root);
        assert_eq!(cands.len(), 3);
        // 順序: pj-AA dir (file_name 辞書順) → 内 instance_id 辞書順 → pj-BB dir
        let order: Vec<(&str, &str)> = cands
            .iter()
            .map(|c| (c.project_uuid.as_str(), c.instance_id.as_str()))
            .collect();
        assert_eq!(
            order,
            vec![
                ("pj-AA", "post-a"),
                ("pj-AA", "post-b"),
                ("pj-BB", "post-c"),
            ]
        );
    }

    /// enumerate_active_post_pair_candidates: pair_pre_name の None / Some 混在。
    #[test]
    fn enumerate_preserves_pair_pre_name_option() {
        let root = unique_root("enum_pair");
        let _ = write_post_json(
            &root,
            "pj-X",
            "post-with-name",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-Hello",
        );
        let _ = write_post_json(
            &root,
            "pj-X",
            "post-no-name",
            SignalState::Active,
            Some(SignalState::Active),
            "",
        );
        let cands = enumerate_active_post_pair_candidates(&root);
        assert_eq!(cands.len(), 2);
        let with_name = cands
            .iter()
            .find(|c| c.instance_id == "post-with-name")
            .unwrap();
        let no_name = cands
            .iter()
            .find(|c| c.instance_id == "post-no-name")
            .unwrap();
        assert_eq!(with_name.pair_pre_name.as_deref(), Some("PRE-Hello"));
        assert!(no_name.pair_pre_name.is_none());
    }

    #[test]
    fn enumerate_for_daw_session_spans_projects_and_filters_other_daw() {
        let root = unique_root("enum_daw");
        let _ = write_post_json_with_daw(&root, "pj-AU", "post-2mix", "2Mix", "daw-main");
        let _ = write_post_json_with_daw(&root, "pj-VST3", "post-drum", "Drum", "daw-main");
        let _ = write_post_json_with_daw(&root, "pj-VST3", "post-music", "Music", "daw-main");
        let _ = write_post_json_with_daw(&root, "pj-OTHER", "post-other", "Vocal", "daw-other");

        let cands = enumerate_active_post_pair_candidates_for_daw_session(&root, "daw-main");
        let names: Vec<_> = cands
            .iter()
            .filter_map(|c| c.pair_pre_name.as_deref())
            .collect();
        assert_eq!(names, vec!["2Mix", "Drum", "Music"]);

        let projects = active_post_project_uuids_for_daw_session(&root, "daw-main");
        assert_eq!(projects, vec!["pj-AU".to_string(), "pj-VST3".to_string()]);
    }

    #[test]
    fn enumerate_for_broadcast_scope_does_not_span_distinct_nonempty_daw_same_process() {
        let root = unique_root("enum_broadcast_scope");
        let host_pid = 42_4242;
        let other_pid = 77_7777;
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-AU",
            "post-2mix",
            "2Mix",
            "daw-au",
            host_pid,
        );
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-VST3",
            "post-drum",
            "Drum",
            "daw-vst3",
            host_pid,
        );
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-OTHER",
            "post-other",
            "Vocal",
            "daw-other",
            other_pid,
        );

        let cands =
            enumerate_active_post_pair_candidates_for_broadcast_scope(&root, "daw-au", host_pid);
        let names: Vec<_> = cands
            .iter()
            .filter_map(|c| c.pair_pre_name.as_deref())
            .collect();
        assert_eq!(names, vec!["2Mix"]);

        let projects = active_post_project_uuids_for_broadcast_scope(&root, "daw-au", host_pid);
        assert_eq!(projects, vec!["pj-AU".to_string()]);

        let daw_only = enumerate_active_post_pair_candidates_for_daw_session(&root, "daw-au");
        assert_eq!(daw_only.len(), 1);
        assert_eq!(daw_only[0].pair_pre_name.as_deref(), Some("2Mix"));
    }

    #[test]
    fn enumerate_for_broadcast_scope_keeps_single_post_project_when_daw_ids_are_instance_scoped() {
        let root = unique_root("enum_broadcast_scope_single_project_instance_daw");
        let host_pid = 42_4242;
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-current",
            "post-2mix",
            "2Mix",
            "daw-post-2mix",
            host_pid,
        );
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-current",
            "post-drum",
            "Drum",
            "daw-post-drum",
            host_pid,
        );
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-current",
            "post-music",
            "Music",
            "daw-post-music",
            host_pid,
        );

        let cands = enumerate_active_post_pair_candidates_for_broadcast_scope(
            &root,
            "daw-post-2mix",
            host_pid,
        );
        let names: Vec<_> = cands
            .iter()
            .filter_map(|c| c.pair_pre_name.as_deref())
            .collect();
        assert_eq!(names, vec!["2Mix", "Drum", "Music"]);

        let projects =
            active_post_project_uuids_for_broadcast_scope(&root, "daw-post-2mix", host_pid);
        assert_eq!(projects, vec!["pj-current".to_string()]);
    }

    #[test]
    fn broadcast_receive_gate_bridges_instance_scoped_daw_inside_same_project_host() {
        assert!(broadcast_scope_or_same_project_host_matches(
            "daw-local",
            42,
            "daw-remote",
            42
        ));
        assert!(!broadcast_scope_or_same_project_host_matches(
            "daw-local",
            42,
            "daw-remote",
            77
        ));
    }

    #[test]
    fn enumerate_for_broadcast_scope_rejects_legacy_no_daw_when_local_has_explicit_daw() {
        let root = unique_root("enum_broadcast_scope_legacy");
        let host_pid = 42_4242;
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-current",
            "post-2mix",
            "2Mix",
            "daw-current",
            host_pid,
        );
        let _ =
            write_legacy_post_json_with_host(&root, "pj-legacy", "post-legacy", "Drum", host_pid);

        let cands = enumerate_active_post_pair_candidates_for_broadcast_scope(
            &root,
            "daw-current",
            host_pid,
        );
        let names: Vec<_> = cands
            .iter()
            .filter_map(|c| c.pair_pre_name.as_deref())
            .collect();
        assert_eq!(
            names,
            vec!["2Mix"],
            "explicit local daw_session_id must not bridge to a legacy no-daw POST by host alone"
        );
    }

    #[test]
    fn enumerate_for_broadcast_scope_keeps_same_host_legacy_when_both_daw_absent() {
        let root = unique_root("enum_broadcast_scope_legacy_empty");
        let host_pid = 42_4242;
        let _ =
            write_legacy_post_json_with_host(&root, "pj-legacy-a", "post-2mix", "2Mix", host_pid);
        let _ =
            write_legacy_post_json_with_host(&root, "pj-legacy-b", "post-drum", "Drum", host_pid);
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-current",
            "post-current",
            "Music",
            "daw-current",
            host_pid,
        );

        let cands = enumerate_active_post_pair_candidates_for_broadcast_scope(&root, "", host_pid);
        let names: Vec<_> = cands
            .iter()
            .filter_map(|c| c.pair_pre_name.as_deref())
            .collect();
        assert_eq!(
            names,
            vec!["2Mix", "Drum"],
            "legacy host fallback is only valid when both sides lack explicit daw_session_id"
        );
    }

    #[test]
    fn host_scope_has_other_active_post_project_detects_same_host_foreign_project() {
        let root = unique_root("host_scope_other_project");
        let host_pid = 42_4242;
        let other_pid = 77_7777;
        let unused_pid = 88_8888;
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-current",
            "post-current",
            "2Mix",
            "daw-current",
            host_pid,
        );
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-other-song",
            "post-other-song",
            "Drum",
            "daw-other",
            host_pid,
        );
        let _ = write_post_json_with_daw_and_host(
            &root,
            "pj-other-host",
            "post-other-host",
            "Vocal",
            "daw-foreign",
            other_pid,
        );

        assert!(host_scope_has_other_active_post_project(
            &root,
            "pj-current",
            host_pid
        ));
        assert!(!host_scope_has_other_active_post_project(
            &root,
            "pj-current",
            unused_pid
        ));
    }

    // ── W-281 / G-115-249 / C-5: self_check_pair_claim テスト 5 件 ─────────

    /// write_post_json の拡張版: pair_claimed_at 値を指定して post.json を書く。
    fn write_post_json_with_claim(
        kirin_root: &Path,
        project_uuid: &str,
        instance_id: &str,
        pair_pre_name: &str,
        pair_claimed_at: f64,
    ) -> PathBuf {
        let dir = kirin_root.join(project_uuid).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let post_file = dir.join("post.json");
        let json = serialize_post_json(
            instance_id,
            SignalState::Active,
            Some(SignalState::Active),
            &MeasureResult::default(),
            pair_pre_name,
            pair_claimed_at,
        );
        fs::write(&post_file, json.as_bytes()).unwrap();
        post_file
    }

    fn write_post_json_with_exact_claim(
        kirin_root: &Path,
        project_uuid: &str,
        instance_id: &str,
        pair_pre_name: &str,
        paired_pre_instance_id: &str,
        pair_claimed_at: f64,
    ) -> PathBuf {
        let dir = kirin_root.join(project_uuid).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let post_file = dir.join("post.json");
        let json = serialize_post_json_with_daw_owner_and_pair_instance(
            instance_id,
            SignalState::Active,
            Some(SignalState::Active),
            &MeasureResult::default(),
            pair_pre_name,
            pair_claimed_at,
            "",
            "",
            paired_pre_instance_id,
        );
        fs::write(&post_file, json.as_bytes()).unwrap();
        post_file
    }

    /// (C-5 i) 他 POST が自分より新しい claim → release 必要 (true)。
    #[test]
    fn self_check_returns_true_when_other_post_has_newer_claim() {
        let root = unique_root("self_check_newer");
        let project_uuid = "pj-X";
        write_post_json_with_claim(&root, project_uuid, "post-self", "PRE-A", 100.0);
        write_post_json_with_claim(&root, project_uuid, "post-other", "PRE-A", 200.0);
        let project_dir = root.join(project_uuid);
        assert!(self_check_pair_claim(
            &project_dir,
            "post-self",
            "PRE-A",
            100.0
        ));
    }

    /// (C-5 ii) 他 POST が別 PRE / 該当なし → release 不要 (false)。
    #[test]
    fn self_check_returns_false_when_no_overlap() {
        let root = unique_root("self_check_no_overlap");
        let project_uuid = "pj-X";
        write_post_json_with_claim(&root, project_uuid, "post-self", "PRE-A", 100.0);
        write_post_json_with_claim(&root, project_uuid, "post-other", "PRE-B", 200.0);
        let project_dir = root.join(project_uuid);
        assert!(!self_check_pair_claim(
            &project_dir,
            "post-self",
            "PRE-A",
            100.0
        ));
    }

    /// (C-5 iii) tie-break: pair_claimed_at 同値 + 自 id 大 → release 必要 (true)。
    #[test]
    fn self_check_returns_true_on_tiebreak_when_self_id_is_larger() {
        let root = unique_root("self_check_tie_larger");
        let project_uuid = "pj-X";
        // 自 id = "post-Z" (lex 大) / other id = "post-A" (lex 小)
        write_post_json_with_claim(&root, project_uuid, "post-Z", "PRE-A", 100.0);
        write_post_json_with_claim(&root, project_uuid, "post-A", "PRE-A", 100.0);
        let project_dir = root.join(project_uuid);
        assert!(self_check_pair_claim(
            &project_dir,
            "post-Z",
            "PRE-A",
            100.0
        ));
    }

    /// (C-5 iv) tie-break: pair_claimed_at 同値 + 自 id 小 → release 不要 (false)。
    #[test]
    fn self_check_returns_false_on_tiebreak_when_self_id_is_smaller() {
        let root = unique_root("self_check_tie_smaller");
        let project_uuid = "pj-X";
        write_post_json_with_claim(&root, project_uuid, "post-A", "PRE-A", 100.0);
        write_post_json_with_claim(&root, project_uuid, "post-Z", "PRE-A", 100.0);
        let project_dir = root.join(project_uuid);
        assert!(!self_check_pair_claim(
            &project_dir,
            "post-A",
            "PRE-A",
            100.0
        ));
    }

    /// (C-5 v) 自 pair_pre_name 空 → release 不要 (false / 即 return)。
    #[test]
    fn self_check_returns_false_when_self_pair_pre_name_is_empty() {
        let root = unique_root("self_check_empty");
        let project_uuid = "pj-X";
        // 他 POST が pair claim 中でも自身が pair 未設定なら不要。
        write_post_json_with_claim(&root, project_uuid, "post-other", "PRE-A", 200.0);
        let project_dir = root.join(project_uuid);
        assert!(!self_check_pair_claim(&project_dir, "post-self", "", 0.0));
    }

    #[test]
    fn self_check_distinguishes_exact_instances_with_the_same_name() {
        let root = unique_root("self_check_exact_same_name");
        let project_uuid = "pj-X";
        write_post_json_with_exact_claim(
            &root,
            project_uuid,
            "post-self",
            "PRE-A",
            "pre-instance-a",
            100.0,
        );
        write_post_json_with_exact_claim(
            &root,
            project_uuid,
            "post-other",
            "PRE-A",
            "pre-instance-b",
            200.0,
        );
        assert!(!self_check_pair_claim_exact(
            &root.join(project_uuid),
            "post-self",
            "PRE-A",
            "pre-instance-a",
            100.0,
        ));
    }

    #[test]
    fn self_check_matches_exact_instance_even_if_display_name_changed() {
        let root = unique_root("self_check_exact_renamed");
        let project_uuid = "pj-X";
        write_post_json_with_exact_claim(
            &root,
            project_uuid,
            "post-other",
            "RENAMED",
            "pre-instance-a",
            200.0,
        );
        assert!(self_check_pair_claim_exact(
            &root.join(project_uuid),
            "post-self",
            "PRE-A",
            "pre-instance-a",
            100.0,
        ));
    }

    #[test]
    fn self_check_keeps_bypassed_post_claim_exclusive() {
        let root = unique_root("self_check_exact_bypassed");
        let project_uuid = "pj-X";
        let post_file = write_post_json_with_exact_claim(
            &root,
            project_uuid,
            "post-other",
            "PRE-A",
            "pre-instance-a",
            200.0,
        );
        let mut json: serde_json::Value =
            serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
        json["signal_state"] = serde_json::json!("bypassed");
        fs::write(post_file, json.to_string()).unwrap();

        assert!(self_check_pair_claim_exact(
            &root.join(project_uuid),
            "post-self",
            "PRE-A",
            "pre-instance-a",
            100.0,
        ));
    }
}

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
