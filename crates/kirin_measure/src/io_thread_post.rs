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
use crate::record_writer::RecordingCtx;
use crate::{load_signal_state, MeasureResult, RecordTakeTracker, RecordTraceQueue, SignalState};

#[path = "io_thread_post_record_ack.rs"]
mod record_ack;
#[cfg(test)]
use record_ack::{
    current_preset_exists, poll_record_signal_ack_with_base, post_ack_generation_is_authorized,
};

#[path = "io_thread_post_liveness.rs"]
mod liveness;
pub use liveness::format_pair_label;
#[cfg(test)]
use liveness::{
    find_pre_json_mtime, poll_ack_timeout_with_base, poll_pre_liveness, poll_pre_liveness_at,
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

#[path = "io_thread_post_identity.rs"]
mod identity;
pub(crate) use identity::read_instance_id_arc;

#[path = "io_thread_post_pair_claim.rs"]
mod pair_claim;
#[cfg(test)]
use pair_claim::service_pair_claim;

#[path = "io_thread_post_analysis.rs"]
mod analysis;
use analysis::PostAnalysisEndpoints;

#[path = "io_thread_post_reservation.rs"]
mod reservation;

#[path = "io_thread_post_idle.rs"]
mod idle;
use idle::IdleRecordStop;

#[path = "io_thread_post_drop.rs"]
mod drop_commit;
use drop_commit::service_open_drop_commit;

#[path = "io_thread_post_closed_drop.rs"]
mod closed_drop;
use closed_drop::ClosedDropRecovery;

#[path = "io_thread_post_writer.rs"]
mod writer;
use writer::service_post_record_writer;

#[path = "io_thread_post_polls.rs"]
mod polls;
use polls::PostControlPolls;

#[path = "io_thread_post_observation.rs"]
mod observation;
use observation::{PostObservation, PostObservationRuntime, PostPairObservationDeps};

#[path = "io_thread_post_broadcast.rs"]
mod broadcast;
#[cfg(test)]
use broadcast::poll_post_broadcasts;

#[path = "io_thread_post_shutdown.rs"]
mod shutdown;
use shutdown::shutdown_post_io;

#[path = "io_thread_post_self_check.rs"]
mod self_check;
#[cfg(test)]
use self_check::PairSelfCheckState;

#[path = "io_thread_post_tick.rs"]
mod tick;
#[cfg(test)]
use identity::broadcast_scope_or_same_project_host_matches;
#[cfg(test)]
use identity::pair_claim_matches_desired_binding;
#[cfg(test)]
use identity::snapshot_pair_pre_name;
#[cfg(test)]
use tick::compute_latched_display;
#[cfg(test)]
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
        let daw_session_id_arc = daw_session_id;
        let mut observation = PostObservation::new(
            PostObservationRuntime {
                instance_id: Arc::clone(&instance_id),
                project_hash: Arc::clone(&project_hash),
                daw_session_id: Arc::clone(&daw_session_id_arc),
                record_sm: Arc::clone(&record_sm),
                post_result: Arc::clone(&post_result),
                delta_result,
                signal_state: Arc::clone(&signal_state),
                is_playing,
            },
            PostPairObservationDeps {
                paired_pre_target: Arc::clone(&paired_pre_target),
                pair_pre_name,
                pair_binding_generation,
                release_pair_binding_if_current,
                pair_claimed_at,
                pair_release_notice,
                pair_owner,
                latched_pre: Arc::clone(&latched_pre),
            },
            PostAnalysisEndpoints::new(spectrum, meter_history),
            Instant::now(),
        );

        let mut recording: Option<RecordingCtx> = None;
        let mut control_polls = PostControlPolls::new(Instant::now());
        let mut closed_drop_recovery = ClosedDropRecovery::new(Instant::now());
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

            let observation_tick = observation.service();
            let project_hash_ref = observation_tick.project_hash.as_str();
            let instance_id_ref = observation_tick.instance_id.as_str();

            // Drop はKirin OSが開始時に捕捉したexact session commitだけを受理する。
            // 一時的不在・破損・capture不一致はR-28に従い無言で次tickへ委ねる。
            service_open_drop_commit(
                &record_sm,
                &record_take_tracker,
                &paired_pre_target,
                project_hash_ref,
                instance_id_ref,
            );
            service_post_record_writer(
                &record_sm,
                sample_rate,
                project_hash_ref,
                instance_id_ref,
                &observation_tick.pair_pre_name,
                &paired_pre_target,
                &post_result,
                &mut recording,
                &session_summary,
                &overflow,
                &oversized_drop,
                &record_trace_queue,
                &record_take_tracker,
                &record_mark_queue,
            );

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

            control_polls.service(
                project_hash_ref,
                instance_id_ref,
                sample_rate,
                &preset_available,
                &record_sm,
                &pair_label,
                &paired_pre_target,
                &record_ingress,
                &latched_pre,
                &daw_session_id_arc,
                &trigger_pair_resolution,
                &trigger_stop_resolution,
            );

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
