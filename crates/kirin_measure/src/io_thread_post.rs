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

use std::collections::HashMap;
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
use crate::record_signal::{self, SignalStatus, ACK_TIMEOUT_SECONDS, SIGNALS_SUBDIR};
use crate::record_writer::{
    apply_record_take_snapshot, parse_iso8601_to_epoch_ms,
    run_record_tick_with_pair_names_require_session_and_marks, take_session_summary,
    writer_close_with_summary_and_marks, RecordingCtx,
};
use crate::storage::{PlatformPaths, StoragePaths};
use crate::{load_signal_state, MeasureResult, RecordTakeTracker, RecordTraceQueue, SignalState};

const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// B-206/B-225/B-243: Record idle auto-stop の既定しきい値（秒）。
/// Record 中に 10 分以上 Active が無ければ、利用者の Stop 漏れ相当として graceful 停止する。
const RECORD_IDLE_TIMEOUT_DEFAULT_SECS: u64 = 600; // 10 min
const SELF_CHECK_RELEASE_CONFIRMATIONS: u8 = 3;

#[derive(Debug, Default)]
struct SelfCheckReleaseGate {
    candidate: Option<SelfCheckReleaseCandidate>,
}

#[derive(Debug)]
struct SelfCheckReleaseCandidate {
    pair_key: String,
    pair_claimed_at: f64,
    confirmations: u8,
}

impl SelfCheckReleaseGate {
    fn reset(&mut self) {
        self.candidate = None;
    }

    fn observe_conflict(&mut self, pair_key: &str, pair_claimed_at: f64) -> bool {
        if pair_key.is_empty() {
            self.reset();
            return false;
        }

        match self.candidate.as_mut() {
            Some(candidate)
                if candidate.pair_key == pair_key
                    && candidate.pair_claimed_at == pair_claimed_at =>
            {
                candidate.confirmations = candidate.confirmations.saturating_add(1);
                candidate.confirmations >= SELF_CHECK_RELEASE_CONFIRMATIONS
            }
            _ => {
                self.candidate = Some(SelfCheckReleaseCandidate {
                    pair_key: pair_key.to_string(),
                    pair_claimed_at,
                    confirmations: 1,
                });
                false
            }
        }
    }
}

/// B-206: idle timeout を解決する（env override 対応 / pure・テスト用に分離）。
/// `KIRIN_RECORD_IDLE_TIMEOUT_SECS` が有効な整数（>= 5）なら採用、それ以外は既定 600s。
/// 下限 5s は暴発防止。テスト/チューニング用途（DAW では `launchctl setenv ...` 後に起動）。
fn parse_idle_timeout(raw: Option<String>) -> Duration {
    let secs = raw
        .and_then(|s| s.trim().parse::<u64>().ok())
        .filter(|&s| s >= 5)
        .unwrap_or(RECORD_IDLE_TIMEOUT_DEFAULT_SECS);
    Duration::from_secs(secs)
}

/// B-243: io thread spawn 時に一度だけ idle timeout を確定する。
fn record_idle_timeout() -> Option<Duration> {
    Some(parse_idle_timeout(
        std::env::var("KIRIN_RECORD_IDLE_TIMEOUT_SECS").ok(),
    ))
}

/// B-206: idle auto-stop すべきか（pure 判定 / テスト容易性のため分離）。
/// Record 中 かつ 非Active かつ idle 経過がしきい値以上 → true。Active 信号が来ている間は
/// 呼出側が経過をリセットするため、録音継続中は決して true にならない。
#[inline]
fn idle_autostop_due(
    is_recording: bool,
    is_active: bool,
    idle_elapsed: Duration,
    timeout: Option<Duration>,
) -> bool {
    timeout.is_some_and(|timeout| is_recording && !is_active && idle_elapsed >= timeout)
}

fn drop_commit_matches_observed_capture(
    expected: &crate::record_expected::ExpectedWavMetadata,
    tracker: &RecordTakeTracker,
    generation: u64,
) -> bool {
    let bwf_matches = expected
        .wav_time_reference_samples
        .and_then(|start| {
            let start = i64::try_from(start).ok()?;
            let duration = i64::try_from(expected.expected_duration_samples).ok()?;
            Some((start, start.checked_add(duration)?))
        })
        .is_some_and(|(start, end)| tracker.observed_content_range(start, end));
    if bwf_matches {
        return true;
    }
    // Non-BWF WAVs have no absolute timeline origin. The host's bounded offline-render epoch is
    // still an exact producer fact: only the Keep generation that rendered this exact native
    // sample count may consume the Drop transaction. Unrelated or stale Keeps remain armed.
    expected.wav_time_reference_samples.is_none()
        && tracker.snapshot(generation).is_some_and(|snapshot| {
            snapshot.generation == generation
                && snapshot.duration_samples == expected.expected_duration_samples
                && snapshot
                    .host_start_position_samples
                    .zip(snapshot.host_end_position_samples)
                    .and_then(|(start, end)| end.checked_sub(start))
                    == i64::try_from(expected.expected_duration_samples).ok()
        })
}

/// All Stop is a filesystem-level barrier for older All Keep broadcasts.
///
/// Studio One can re-initialize plugins during offline bounce. That restarts the
/// IO thread and clears the in-memory "processed keep broadcast" cache, while
/// the old all_keep_signal file can still be fresh. A fresh all_stop_signal with
/// a later/equal `started_at` must therefore suppress that older Keep, otherwise
/// POST instances can re-enter Record after Stop.
#[inline]
fn keep_broadcast_blocked_by_stop(
    keep_started_at: &str,
    latest_stop_started_at: Option<&str>,
) -> bool {
    latest_stop_started_at.is_some_and(|stop_started_at| keep_started_at <= stop_started_at)
}

#[inline]
fn remember_latest_started_at(latest: &mut Option<String>, candidate: &str) {
    if latest
        .as_deref()
        .map(|existing| candidate > existing)
        .unwrap_or(true)
    {
        *latest = Some(candidate.to_string());
    }
}

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
/// project-local `current.json` 1 件を 1 秒間隔で読む。履歴や originator 数に比例しない。
const ALL_KEEP_POLL_INTERVAL: Duration = Duration::from_secs(1);

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
    // GUI Thread → POST IO Thread. Only the writer consumes queued MARKs.
    record_mark_queue: crate::record_mark::RecordMarkQueue,
    // B-076: 累積 push_overflow（Audio Thread が ring 満杯時に積む）。run_record_tick が
    // Record 開始で snapshot し close 時に差分を per-Record dropped_samples として焼き込む。
    overflow: Arc<std::sync::atomic::AtomicU64>,
    // B-125: 累積 oversized_drop（JUCE 殻のみ計上 / egui は常に 0）。overflow とは別カウンタ。
    // run_record_tick が同位相で snapshot/差分し、合算を dropped_samples へ焼く。
    oversized_drop: Arc<std::sync::atomic::AtomicU64>,
    // B-108: display と keep/Arm が共有する単一ラッチ。io_thread が毎 tick 維持し、shell 側の
    // keep/keep_all/broadcast 受信が `resolve_arm_target` で読む（egui/JUCE 両殻が同実体を渡す）。
    latched_pre: Arc<Mutex<Option<LatchedPre>>>,
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
        // B-027 段階 3-B α-7-4-C / Step 10: all_keep_signal broadcast 受信側 cache。
        // key = `originator_post_instance_id`、value = `(started_at, last_seen)`。
        //
        // - `started_at` 値比較で「同 originator + 同 broadcast」の既処理 skip (clock-skew
        //   完全耐性 / Q-A8-6)。
        // - `last_seen: Instant` で GC (`ACK_TIMEOUT_SECONDS` = 30 秒経過 entry を retain で
        //   削除 / 引数 #24 (ii) 採用 / 先例 io_thread_pre.rs:378-403 partner.last_seen_status
        //   cache パターンと同位相 / chrono 新規依存導入なし)。
        let mut processed_broadcasts: HashMap<String, (String, Instant)> = HashMap::new();
        // α-7' All Stop: Stop broadcast 受信側 cache (Keep と並列 / 同型 HashMap)。
        let mut processed_stop_broadcasts: HashMap<String, (String, Instant)> = HashMap::new();
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

            // W-281 / C-3: 1 sec interval で後着優先 self check を発火。
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
            // 動いている間は保持し、後着優先 release は停止中のみ許容する。
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
                            "[POST self_check] release pair: instance_id={} pair_pre_name={} paired_pre_instance_id={:?} (newer claim detected)",
                            instance_id_ref,
                            pair_pre_name_snapshot,
                            paired_pre_instance_id_snapshot
                        );
                        if let Ok(mut c) = pair_claimed_at_for_thread.write() {
                            *c = 0.0;
                        }
                        if let Ok(mut n) = pair_release_notice_for_thread.write() {
                            *n = Some("Released (paired elsewhere)".to_string());
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
                record_sm.is_recording(),
                &latched_pre,
            ) {
                Ok(()) => true,
                Err(e) => {
                    log::warn!("[IOThread POST] tick error: {}", e);
                    false
                }
            };

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
                        || !crate::pair_claim_index::pair_claim_is_live(&kirin_root, owned)
                    {
                        owned_pair_claim = None;
                    }
                    next_pair_claim_publish = Instant::now() + Duration::from_secs(1);
                }
            }
            let desired_matches_owned = owned_pair_claim.as_ref().is_some_and(|owned| {
                current_pre.as_deref() == Some(owned.pre_instance_id.as_str())
                    && owned.project_hash == project_hash_ref
                    && owned.post_instance_id == instance_id_ref
                    && owned.post_watch_owner_id == watch_lease.owner_id()
                    && owned.pair_claimed_at_bits == current_claimed_at.to_bits()
            });
            if !desired_matches_owned {
                if let Some(previous) = owned_pair_claim.take() {
                    let _ = crate::pair_claim_index::release_pair_claim(&kirin_root, &previous);
                }
                next_pair_claim_publish = Instant::now();
            }
            if post_snapshot_written
                && owned_pair_claim.is_none()
                && current_claimed_at.is_finite()
                && current_claimed_at > 0.0
                && Instant::now() >= next_pair_claim_publish
            {
                if let Some(pre_instance_id) = current_pre.as_deref() {
                    match crate::pair_claim_index::publish_pair_claim(
                        &kirin_root,
                        pre_instance_id,
                        project_hash_ref,
                        instance_id_ref,
                        watch_lease.owner_id(),
                        crate::post_candidates::current_host_process_id(),
                        current_claimed_at,
                    ) {
                        Ok(
                            crate::pair_claim_index::PublishPairClaimOutcome::Published
                            | crate::pair_claim_index::PublishPairClaimOutcome::AlreadyOwner,
                        ) => {
                            owned_pair_claim = crate::pair_claim_index::read_pair_claim(
                                &kirin_root,
                                crate::post_candidates::current_host_process_id(),
                                pre_instance_id,
                            );
                        }
                        Ok(crate::pair_claim_index::PublishPairClaimOutcome::OwnedByOther) => {}
                        Err(error) => log::debug!(
                            "[POST pair claim] exact publish deferred: instance_id={} error={}",
                            instance_id_ref,
                            error
                        ),
                    }
                    next_pair_claim_publish = Instant::now() + Duration::from_secs(1);
                }
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
                if let (Some(session_id), Some(pre_instance_id), Ok(paths)) = (
                    record_sm.last_closed_session_id(),
                    paired_pre_target
                        .lock()
                        .ok()
                        .and_then(|guard| guard.clone()),
                    StoragePaths::default_platform(),
                ) {
                    if completed_closed_drop_session.as_deref() != Some(session_id.as_str()) {
                        let base = paths.plugin_data_dir();
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
                        if !broadcast_scope_or_same_project_host_matches(
                            &daw_session_id_snapshot,
                            host_process_id_snapshot,
                            &broadcast.daw_session_id,
                            broadcast.host_process_id,
                        ) {
                            continue;
                        }
                        if all_stop_signal::is_stop_broadcast_stale(
                            &broadcast,
                            now_chrono,
                            ALL_STOP_BROADCAST_STALE_SECS,
                        ) {
                            processed_stop_broadcasts.insert(
                                originator_iid.clone(),
                                (broadcast.started_at.clone(), Instant::now()),
                            );
                            log::debug!(
                                "[all_stop] stale broadcast cached without fire: originator={}",
                                originator_iid
                            );
                            continue;
                        }
                        remember_latest_started_at(
                            &mut latest_fresh_stop_started_at,
                            &broadcast.started_at,
                        );
                        if originator_iid == instance_id_ref {
                            continue;
                        }
                        if let Some((cached_started_at, _)) =
                            processed_stop_broadcasts.get(&originator_iid)
                        {
                            if cached_started_at == &broadcast.started_at {
                                continue;
                            }
                        }
                        processed_stop_broadcasts.insert(
                            originator_iid.clone(),
                            (broadcast.started_at.clone(), Instant::now()),
                        );
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
                    let timeout = Duration::from_secs(ACK_TIMEOUT_SECONDS as u64);
                    processed_stop_broadcasts
                        .retain(|_, (_, last_seen)| last_seen.elapsed() < timeout);
                }
                // 注: next_all_keep_poll は Keep sub-tick 末で reset されるため
                // Stop は Keep と同 throttle (1 秒) で同 frame に動く。
            }

            // B-027 段階 3-B α-7-4-C / Step 10: all_keep_signal broadcast 受信 sub-tick。
            // 1 秒 throttle で `plugin_data/{ph}/all_keep_signal/current.json` だけを読み、
            // 新 broadcast を `processed_broadcasts` cache に登録する (検出 + cache + log
            // のみ / `trigger_keep_internal` 発火は Step 11 で本箇所に追加予定)。
            //
            //  1. cross-process 防壁: daw_session_id / host_process_id scope 外は skip
            //  2. self skip: `originator_iid == self_instance_id` skip (#16 (iii))
            //  3. 既処理 skip: cache 内の immutable generation id と一致 → 同 broadcast skip
            //  4. stale fallback: legacy または未commit generationだけ cache 登録
            //  5. commit済 generation: arm成功まで再試行し、成功後だけ cache 更新
            //  6. GC: `last_seen.elapsed() >= ACK_TIMEOUT_SECONDS` の entry を retain で削除
            //     (先例 io_thread_pre.rs:378-403 partner.last_seen_status cache 同位相 /
            //     chrono 新規依存なし / 申し送り #24 (ii))
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
                            processed_broadcasts.insert(
                                originator_iid.clone(),
                                (broadcast_key.clone(), Instant::now()),
                            );
                            continue;
                        }
                        // 3. 既処理 skip
                        if let Some((cached_broadcast_key, _)) =
                            processed_broadcasts.get(&originator_iid)
                        {
                            if cached_broadcast_key == &broadcast_key {
                                continue;
                            }
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
                            processed_broadcasts.insert(
                                originator_iid.clone(),
                                (broadcast_key.clone(), Instant::now()),
                            );
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
                            processed_broadcasts.insert(
                                originator_iid.clone(),
                                (broadcast_key.clone(), Instant::now()),
                            );
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
                            processed_broadcasts.insert(
                                originator_iid.clone(),
                                (broadcast_key.clone(), Instant::now()),
                            );
                            log::debug!(
                                "[all_keep] legacy broadcast skipped without generation: originator={}",
                                originator_iid
                            );
                            continue;
                        }
                        // Project pointers may exist while the producer is still staging
                        // broadcasts. Only the installation-wide pointer is the commit barrier.
                        // A mismatch is transient and must not be cached: the next 1s tick retries.
                        let generation = match (
                            crate::capture_generation::read_current_generation(
                                &base_dir,
                                project_hash_ref,
                            ),
                            crate::capture_generation::read_active_generation(&base_dir),
                        ) {
                            (Ok(Some(project_generation)), Ok(Some(active_generation)))
                                if project_generation.capture_generation_id
                                    == broadcast.capture_generation_id
                                    && project_generation.started_at_ms
                                        == broadcast.generation_started_at_ms
                                    && active_generation.capture_generation_id
                                        == project_generation.capture_generation_id
                                    && active_generation.started_at_ms
                                        == project_generation.started_at_ms
                                    && project_generation
                                        .member(project_hash_ref, instance_id_ref)
                                        .is_some() =>
                            {
                                project_generation
                            }
                            _ if broadcast_is_stale => {
                                // A stale staged/aborted generation that never became active is
                                // permanently non-authoritative and may now be cached.
                                processed_broadcasts.insert(
                                    originator_iid.clone(),
                                    (broadcast_key.clone(), Instant::now()),
                                );
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
                                    "[all_keep] committed member armed: originator={} generation={}",
                                    originator_iid,
                                    generation.capture_generation_id
                                );
                            }
                            processed_broadcasts
                                .insert(originator_iid.clone(), (broadcast_key, Instant::now()));
                        }
                    }
                    // 6. GC: ACK_TIMEOUT_SECONDS 経過 cache 削除
                    let timeout = Duration::from_secs(ACK_TIMEOUT_SECONDS as u64);
                    processed_broadcasts.retain(|_, (_, last_seen)| last_seen.elapsed() < timeout);
                } else {
                    log::warn!("[all_keep] StoragePaths::default_platform() failed; skipping tick");
                }
                next_all_keep_poll = Instant::now() + ALL_KEEP_POLL_INTERVAL;
            }

            thread::sleep(LOOP_SLEEP);
        }

        // Conditional release is serialized by the same per-PRE OS lock. If a newer POST already
        // replaced this claim, the old teardown cannot remove the new owner's pointer.
        if let Some(claim) = owned_pair_claim.take() {
            let _ = crate::pair_claim_index::release_pair_claim(&kirin_root, &claim);
        }

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
                match record_signal::mark_released(
                    &paths.plugin_data_dir(),
                    &final_project_hash,
                    &final_iid,
                ) {
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

/// B-108: ラッチ意味論で表示Δを決める単一実装（`run_tick` の POST=Active 表示経路が呼ぶ）。
///
/// 戻り `(delta, store_directly, pre_signal_state)`:
/// - `store_directly = true` は **latched-idle**（Stale + 全Δ None + `last_active = None`）。
///   `run_tick` は `resolve_delta_for_store` を経由せずそのまま格納し凍結値の復活を防ぐ（--- のみ
///   / B-048・B-049 維持）。
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

    // (2) ラッチ維持中。現行leaseの明示終了だけは完全切断し、同じnameの再作成PREを
    // 同tickで再解決できるようにする。Record中は上のfreeze分岐が先に返るため不変。
    if keep {
        let l = current.expect("keep implies current is Some");
        if crate::watch_snapshot_lease::snapshot_file_has_released_current_owner(&l.pre_json) {
            let mut binding = latched
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            if binding.as_ref().is_some_and(|current| {
                current.instance_id == l.instance_id && current.pre_json == l.pre_json
            }) {
                binding.take();
                log::info!(
                    "[POST pairing] released PRE runtime detached: instance_id={}",
                    l.instance_id
                );
            }
        } else {
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
                // stale / idle / silent / missing / rename → ラッチ維持のまま muted Δ/---。
                _ => return Ok(delta_latched_idle()),
            }
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
    // - store_directly（latched-idle）→ Stale + last_active=None をそのまま格納（凍結値復活禁止
    //   / B-048・B-049 維持）。merge_last_active を経由しない。
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
mod watch_producer_recovery_tests {
    use super::*;
    use std::sync::atomic::AtomicU64;

    fn isolated_root() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let root = std::env::temp_dir().join(format!(
            "kirin_post_watch_recovery_{}_{}",
            std::process::id(),
            n
        ));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root
    }

    #[test]
    fn active_watch_tick_recovers_poisoned_result_locks() {
        let root = isolated_root();
        let project_dir = root.join("project");
        let instance_dir = project_dir.join("post-instance");
        let post_file = instance_dir.join("post.json");
        let post_result = Arc::new(Mutex::new(MeasureResult {
            lufs_m: Some(-11.0),
            ..Default::default()
        }));
        let delta_result = Arc::new(Mutex::new(DeltaResult::default()));

        let post_poison_target = Arc::clone(&post_result);
        let _ = std::thread::spawn(move || {
            let _guard = post_poison_target.lock().unwrap();
            panic!("poison fixture");
        })
        .join();
        let delta_poison_target = Arc::clone(&delta_result);
        let _ = std::thread::spawn(move || {
            let _guard = delta_poison_target.lock().unwrap();
            panic!("poison fixture");
        })
        .join();
        assert!(post_result.is_poisoned());
        assert!(delta_result.is_poisoned());

        let state = Arc::new(AtomicU8::new(SignalState::Active as u8));
        let latched = Mutex::new(None);
        run_tick(
            &project_dir,
            &root,
            &mut PostDiscoveryState::new(),
            &instance_dir,
            &post_file,
            "post-instance",
            "owner",
            &post_result,
            &delta_result,
            &state,
            "",
            0.0,
            "project",
            "daw",
            false,
            &latched,
        )
        .expect("poisoned result locks must not stop Watch JSON");

        assert!(!post_result.is_poisoned());
        assert!(!delta_result.is_poisoned());

        let parsed: PostTmpJson = serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
        assert_eq!(parsed.instance_id, "post-instance");
        assert_eq!(parsed.lufs_m, Some(-11.0));
        assert!(!parsed.t.is_empty());
    }
}

/// PRE ファイルをスキャンして Δ を算出する（後方互換ラッパー / 既存 integration test 用）。
///
/// W-280: pair filter なし版。本ラッパーは `pair_pre_name = None` 相当で呼ぶ。
/// 新規プロダクションコードは `compute_delta_with_state(..., pair_pre_name)` を直接呼ぶこと。
#[doc(hidden)]
pub fn compute_delta(project_dir: &Path, post: &MeasureResult) -> Result<DeltaResult, String> {
    compute_delta_with_state(project_dir, post, None).map(|(delta, _)| delta)
}

/// B-048 / G-115-245 Last Known Good: 新 `DeltaResult` と前回 `last_active` を
/// マージする pure 関数。`run_tick` 内で Mutex lock 取得後に呼ばれる。
///
/// セマンティクス:
/// - `new_delta.mode == Active` → 新 6 field 値で `DeltaSnapshot` 作成 →
///   `last_active = Some(snapshot)`。`prev_last_active` は破棄。
/// - `new_delta.mode == Stale | NoPre` → `prev_last_active` をそのまま保持。
///   新値を `last_active` に書き込まない (PRE pre.json 不在/古い区間で「直近の
///   Active 時の凍結値」を GUI に保持させ続けるため)。
/// - `new_delta.mode == Bypassed` → 明示 OFF なので凍結値を保持しない。
///
/// 純関数のため Mutex / fs 副作用なし → unit test 容易。
/// `run_tick` (Mutex / fs / spawn 副作用あり) は本関数を呼ぶだけのアダプタ。
///
/// 可視性: `pub` は `tests/io_thread_post_test.rs` (kirin_measure crate 外扱いの
/// integration test) から呼ぶための公開。外部 crate consumer (hypha_post 等) は
/// 本関数を直接使う想定無し (`DeltaResult::last_active` を読むのは GUI 側のみ)。
pub fn merge_last_active(
    prev_last_active: Option<DeltaSnapshot>,
    new_delta: DeltaResult,
) -> DeltaResult {
    match new_delta.mode {
        DeltaMode::Active => {
            let snapshot = DeltaSnapshot {
                lufs: new_delta.lufs,
                psr: new_delta.psr,
                tp: new_delta.tp,
                n_prime_total: new_delta.n_prime_total,
                crest: new_delta.crest,
                sharpness: new_delta.sharpness,
            };
            DeltaResult {
                last_active: Some(snapshot),
                ..new_delta
            }
        }
        DeltaMode::Stale | DeltaMode::NoPre => DeltaResult {
            last_active: prev_last_active,
            ..new_delta
        },
        DeltaMode::Bypassed => new_delta,
    }
}

fn snapshot_from_delta(d: &DeltaResult) -> Option<DeltaSnapshot> {
    if d.lufs.is_none()
        && d.psr.is_none()
        && d.tp.is_none()
        && d.n_prime_total.is_none()
        && d.crest.is_none()
        && d.sharpness.is_none()
    {
        None
    } else {
        Some(DeltaSnapshot {
            lufs: d.lufs,
            psr: d.psr,
            tp: d.tp,
            n_prime_total: d.n_prime_total,
            crest: d.crest,
            sharpness: d.sharpness,
        })
    }
}

fn resolve_delta_for_non_active_post(
    state: SignalState,
    pair_pre_name: &str,
    previous: &DeltaResult,
) -> DeltaResult {
    if state != SignalState::Inactive || pair_pre_name.trim().is_empty() {
        return DeltaResult::default();
    }

    DeltaResult {
        mode: DeltaMode::Stale,
        last_active: previous
            .last_active
            .clone()
            .or_else(|| snapshot_from_delta(previous)),
        ..DeltaResult::default()
    }
}

/// B-059 / G-115-245 置換: `run_tick` が delta_result に書く値を決める pure 関数。
///
/// - `mode == NoPre | Bypassed`（= 有効ペアなし / PRE 明示OFF）→ **`new_delta` をそのまま**
///   （`DeltaResult::default()` 由来で `last_active = None` ＝ クリア）。表示=commit 一本化で
///   「選定 None なのに直近 Δ が凍結表示される」のを防ぐ（B-048 の NoPre 保持を廃止）。
/// - `mode == Active | Stale`（= 一意有効 PRE を選定）→ `merge_last_active`（Active 保存 /
///   Stale 5-10s は同一有効 pair の凍結値保持＝B-048 維持）。
///
/// 純関数（Mutex/fs 副作用なし）→ unit test 容易。
pub(crate) fn resolve_delta_for_store(
    new_delta: DeltaResult,
    prev_last_active: Option<DeltaSnapshot>,
) -> DeltaResult {
    if matches!(new_delta.mode, DeltaMode::NoPre | DeltaMode::Bypassed) {
        new_delta
    } else {
        merge_last_active(prev_last_active, new_delta)
    }
}

/// `$TMPDIR/kirin/{project_hash}/` 配下の全 instance_id サブディレクトリを走査して
/// `pre.json` を集め、Δ を算出する (W-280: pair filter 対応)。
///
/// # pair_pre_name セマンティクス (W-280 / G-115-248)
/// - `Some(target)` (非空文字): 各 pre.json を read → `parsed["name"].as_str()` が
///   `target` に一致するものだけ候補に push する。pair 確立後の Δ 計算経路で
///   2 セット環境の交互 pick 症状を解消する根本対処。
/// - `None` または `Some("")` (filter なし):
///   - project_dir 配下 instance 数が **1 件**なら従来通り pass-through (ZSA: single-PRE
///     後方互換 / pair_pre_name 未設定時の挙動維持)。
///   - **2 件以上**は曖昧として `DeltaMode::NoPre` (R-7 / R-26 沈黙ゲート / 推測でない)。
///
/// # 戻り値
/// - 0 個 → `DeltaMode::NoPre`, `None`
/// - 複数 → 最新 `t` を選択（ISO 8601 は文字列比較で最新判定可 / A-2 上流 filter 済前提）
/// - 鮮度判定 → `Active` / `Stale` / `NoPre`
fn compute_delta_with_state(
    project_dir: &Path,
    post: &MeasureResult,
    pair_pre_name: Option<&str>,
) -> Result<(DeltaResult, Option<SignalState>), String> {
    if !project_dir.exists() {
        return Ok((
            DeltaResult {
                mode: DeltaMode::NoPre,
                ..Default::default()
            },
            None,
        ));
    }

    // W-280: 空文字は filter 適用なし扱い (B-027 段階 1 pass-through セマンティクス継承)。
    let pair_target = pair_pre_name.filter(|s| !s.is_empty());

    // W-280: 1 回目の walk で「project_dir 配下の instance 候補 (pre.json 持ち)」を
    // 列挙し、その後 pair filter / pass-through 判定を行う。
    let mut candidates: Vec<PathBuf> = Vec::new();
    let project_entries = fs::read_dir(project_dir).map_err(|e| format!("read_dir: {e}"))?;
    for entry in project_entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        // record_signal/ 等の予約名は instance_id ではない
        if path.file_name().and_then(|n| n.to_str()) == Some(SIGNALS_SUBDIR) {
            continue;
        }
        let candidate = path.join("pre.json");
        if candidate.is_file() {
            candidates.push(candidate);
        }
    }

    // W-280: pair filter — Some(target) なら name 一致のみ / None なら 1 件 pass-through。
    let mut pre_files: Vec<PathBuf> = match pair_target {
        Some(target) => {
            let mut matched = Vec::new();
            for cand in candidates {
                let content = match fs::read_to_string(&cand) {
                    Ok(s) => s,
                    Err(_) => continue,
                };
                let parsed: serde_json::Value = match serde_json::from_str(&content) {
                    Ok(v) => v,
                    Err(_) => continue,
                };
                if parsed.get("name").and_then(|v| v.as_str()) == Some(target) {
                    matched.push(cand);
                }
            }
            matched
        }
        None => {
            // pair 未指定: 1 件のみ pass-through / 2 件以上は曖昧として 0 件 (NoPre 落ち)。
            if candidates.len() == 1 {
                candidates
            } else {
                Vec::new()
            }
        }
    };

    if pre_files.is_empty() {
        return Ok((
            DeltaResult {
                mode: DeltaMode::NoPre,
                ..Default::default()
            },
            None,
        ));
    }

    let best = select_best_pre(&mut pre_files)?;
    compute_delta_for_pre_file(&best, post)
}

/// 選定済みの PRE `pre.json` だけを読んで Δ を算出する。
///
/// Pairing/Arm 側が決めた `LatchedPre::pre_json` を再スキャンで失わないための境界。
/// `compute_delta_with_state` は棚から候補を選ぶ互換入口として残し、Record/ラッチ表示は
/// 本関数へ直接入る。
fn compute_delta_for_pre_file(
    pre_json: &Path,
    post: &MeasureResult,
) -> Result<(DeltaResult, Option<SignalState>), String> {
    let content = fs::read_to_string(pre_json).map_err(|e| format!("read PRE file: {e}"))?;
    let parsed: serde_json::Value =
        serde_json::from_str(&content).map_err(|e| format!("parse PRE JSON: {e}"))?;

    let pre_signal_state = parsed["signal_state"].as_str().map(|s| match s {
        "active" => SignalState::Active,
        "bypassed" => SignalState::Bypassed,
        _ => SignalState::Inactive,
    });

    if pre_signal_state != Some(SignalState::Active) {
        return Ok((
            DeltaResult {
                lufs: None,
                psr: None,
                tp: None,
                n_prime_total: None,
                crest: None,
                sharpness: None,
                mode: if pre_signal_state == Some(SignalState::Bypassed) {
                    DeltaMode::Bypassed
                } else {
                    DeltaMode::NoPre
                },
                last_active: None, // B-048 §4-2: run_tick で merge する責務分業
            },
            pre_signal_state,
        ));
    }

    let mode = freshness_mode(&parsed)?;
    if mode == DeltaMode::NoPre {
        return Ok((
            DeltaResult {
                mode: DeltaMode::NoPre,
                ..Default::default()
            },
            pre_signal_state,
        ));
    }

    let pre_lufs = parsed["lufs_m"].as_f64();
    let pre_psr = parsed["psr"].as_f64();
    let pre_tp = parsed["true_peak"].as_f64();
    // (io_thread_pre.rs:856-862 / opt_f64 ではなく conditional concat)、欠落時の
    // `serde_json::Value::Null` も `as_f64() == None` で素直に Δ=None になる。
    let pre_n_prime_total = parsed["n_prime_total"].as_f64();
    let pre_crest = parsed["crest"].as_f64();
    let pre_sharpness = parsed["sharpness"].as_f64();

    let delta_lufs = post.lufs_m.zip(pre_lufs).map(|(p, r)| p - r);
    let delta_psr = post.psr.zip(pre_psr).map(|(p, r)| p - r);
    let delta_tp = post.true_peak.zip(pre_tp).map(|(p, r)| p - r);
    let delta_n = post
        .n_prime_total
        .zip(pre_n_prime_total)
        .map(|(p, r)| p - r);
    let delta_crest = post.crest.zip(pre_crest).map(|(p, r)| p - r);
    let delta_sharpness = post.sharpness.zip(pre_sharpness).map(|(p, r)| p - r);

    Ok((
        DeltaResult {
            lufs: delta_lufs,
            psr: delta_psr,
            tp: delta_tp,
            n_prime_total: delta_n,
            crest: delta_crest,
            sharpness: delta_sharpness,
            mode,
            last_active: None, // B-048 §4-2: run_tick で merge する責務分業
        },
        pre_signal_state,
    ))
}

/// pre.json リストから最新 `t` フィールドを持つファイルを返す。
fn select_best_pre(files: &mut Vec<PathBuf>) -> Result<PathBuf, String> {
    if files.len() == 1 {
        return Ok(files.remove(0));
    }

    let mut best_path: Option<PathBuf> = None;
    let mut best_t = String::new();

    for path in files.iter() {
        let content = match fs::read_to_string(path) {
            Ok(c) => c,
            Err(_) => continue,
        };
        let parsed: serde_json::Value = match serde_json::from_str(&content) {
            Ok(v) => v,
            Err(_) => continue,
        };
        let t = parsed["t"].as_str().unwrap_or("").to_string();
        if t > best_t {
            best_t = t;
            best_path = Some(path.clone());
        }
    }

    best_path.ok_or_else(|| "no valid PRE file found".to_string())
}

fn freshness_mode(parsed: &serde_json::Value) -> Result<DeltaMode, String> {
    let t_str = parsed["t"]
        .as_str()
        .ok_or_else(|| "PRE JSON missing 't' field".to_string())?;

    let pre_time = chrono::DateTime::parse_from_rfc3339(t_str)
        .map_err(|e| format!("parse PRE timestamp: {e}"))?;

    let now = chrono::Utc::now();
    let age_secs = (now - pre_time.with_timezone(&chrono::Utc)).num_seconds();

    let mode = if age_secs >= NO_PRE_SECS {
        DeltaMode::NoPre
    } else if age_secs >= STALE_SECS {
        DeltaMode::Stale
    } else {
        DeltaMode::Active
    };

    // B-047 / G-115-247: NoPre / Stale 観測ログ (UI 異常診断用)。
    // instance_id は PRE 側 serialize_pre_json が書き込む "instance_id" field を引く。
    // freshness_mode は io_thread_post tick (100ms) ごとに呼ばれるため、
    // Stale/NoPre 継続中はログが洪水になる。Phase 1 観察フェーズ後は edge-triggered
    // (前回 mode との比較) または log level 引き下げを別 commit で検討する。
    if mode != DeltaMode::Active {
        let iid = parsed
            .get("instance_id")
            .and_then(|v| v.as_str())
            .unwrap_or("?");
        log::warn!(
            "[freshness] PRE pre.json {:?}: age_secs={} instance_id={}",
            mode,
            age_secs,
            iid
        );
    }

    Ok(mode)
}

/// POST JSON v2 フォーマット（Active 時。SS-5 + SS-6）。bus フィールドは削除済（A-3 修正後）。
///
/// # B-027 段階 3-B α-7-1: `pair_pre_name` field 追加
/// 同 project_hash 内の他 POST から read される (cross-instance 公開機構 / Q-A7 採用案 A)。
/// 旧 schema (本変更前 plugin) との互換は read 側 `PostTmpJson` の `#[serde(default)]`
/// で保証される (record_signal::RecordSignal.paired_pre_name と同位相)。
pub fn serialize_post_json(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
    pair_pre_name: &str,
    pair_claimed_at: f64,
) -> String {
    serialize_post_json_with_daw(
        instance_id,
        state,
        pre_signal_state,
        result,
        pair_pre_name,
        pair_claimed_at,
        "",
    )
}

fn serialize_post_json_with_daw(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
) -> String {
    serialize_post_json_with_daw_and_owner(
        instance_id,
        state,
        pre_signal_state,
        result,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        "",
    )
}

#[allow(clippy::too_many_arguments)]
fn serialize_post_json_with_daw_and_owner(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
    watch_owner_id: &str,
) -> String {
    serialize_post_json_with_daw_owner_and_pair_instance(
        instance_id,
        state,
        pre_signal_state,
        result,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        watch_owner_id,
        "",
    )
}

#[allow(clippy::too_many_arguments)]
fn serialize_post_json_with_daw_owner_and_pair_instance(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
    watch_owner_id: &str,
    paired_pre_instance_id: &str,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    let pre_state_str = pre_signal_state
        .map(|s| format!(r#""{}""#, s.as_str()))
        .unwrap_or_else(|| "null".to_string());
    // B-131 (G-115-380): hand-built JSON に生補間される外部由来の文字列 field を serde で escape する。
    // 旧: 生補間で `"` `\` を含む値が不正 JSON を生成 → 他 POST の scan_post_candidates_in が parse
    // 失敗で無言 skip → pairing 消失していた R-28 欠陥。PRE serialize_pre_json と対称。正常 ASCII /
    // UUID では byte 不変（既存 wire / parity literal-id 不変）。
    //   - pair_pre_name: 利用者 GUI 入力（set_pair_target / 対 PRE の Name）。
    //   - instance_id  : restore で host 由来になりうる。gate の is_path_safe_component
    //     (path_identity.rs) は `/` `\` 制御文字は拒否するが **`"` を拒否しない** ため、`"` 入りの
    //     restore instance_id が materialize wall を素通って同一 R-28 を起こす（census で検出）。
    //     根本封止（wall 側で `"` を quarantine）は B-128 領域につき番人へ別途上申。本 commit は
    //     JSON 出力層で同種一括 escape する。
    let instance_id_json =
        serde_json::to_string(instance_id).unwrap_or_else(|_| "\"\"".to_string());
    let daw_session_id_json =
        serde_json::to_string(daw_session_id).unwrap_or_else(|_| "\"\"".to_string());
    let watch_owner_id_json =
        serde_json::to_string(watch_owner_id).unwrap_or_else(|_| "\"\"".to_string());
    let host_process_id = crate::current_host_process_id();
    let pair_pre_name_json =
        serde_json::to_string(pair_pre_name).unwrap_or_else(|_| "\"\"".to_string());
    let paired_pre_instance_id_json =
        serde_json::to_string(paired_pre_instance_id).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        r#"{{"v":2,"role":"POST","instance_id":{instance_id_json},"daw_session_id":{daw_session_id},"host_process_id":{host_process_id},"watch_owner_id":{watch_owner_id},"signal_state":"{signal_state}","pre_signal_state":{pre_signal_state},"t":"{t}","pair_pre_name":{pair_pre_name},"paired_pre_instance_id":{paired_pre_instance_id},"pair_claimed_at":{pair_claimed_at},"lufs_m":{lufs_m},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id_json = instance_id_json,
        daw_session_id = daw_session_id_json,
        host_process_id = host_process_id,
        watch_owner_id = watch_owner_id_json,
        signal_state = state.as_str(),
        pre_signal_state = pre_state_str,
        t = t,
        pair_pre_name = pair_pre_name_json,
        paired_pre_instance_id = paired_pre_instance_id_json,
        pair_claimed_at = pair_claimed_at,
        lufs_m = opt_f64(result.lufs_m),
        true_peak = opt_f64(result.true_peak),
        crest = opt_f64(result.crest),
        psr = opt_f64(result.psr),
        phase_d = phase_d_fragment(result),
    )
}

/// Bypassed / Inactive 時の最小 POST JSON。
///
/// B-027 段階 3-B α-7-1: `pair_pre_name` field を追加 (Bypassed/Inactive でも候補化
/// される / All Keep N 計算で参照されるため filter 照合に必要)。
/// W-281 / G-115-249: `pair_claimed_at` field 追加 (後着優先 self check 判定軸)。
#[cfg(test)]
fn serialize_post_json_minimal(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
) -> String {
    serialize_post_json_minimal_with_daw(instance_id, state, pair_pre_name, pair_claimed_at, "")
}

#[cfg(test)]
fn serialize_post_json_minimal_with_daw(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
) -> String {
    serialize_post_json_minimal_with_daw_and_owner(
        instance_id,
        state,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        "",
    )
}

#[cfg(test)]
fn serialize_post_json_minimal_with_daw_and_owner(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
    watch_owner_id: &str,
) -> String {
    serialize_post_json_minimal_with_daw_owner_and_pair_instance(
        instance_id,
        state,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        watch_owner_id,
        "",
    )
}

fn serialize_post_json_minimal_with_daw_owner_and_pair_instance(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
    watch_owner_id: &str,
    paired_pre_instance_id: &str,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    // B-131 (G-115-380): instance_id / pair_pre_name を serde で JSON escape（serialize_post_json と同一契約）。
    let instance_id_json =
        serde_json::to_string(instance_id).unwrap_or_else(|_| "\"\"".to_string());
    let daw_session_id_json =
        serde_json::to_string(daw_session_id).unwrap_or_else(|_| "\"\"".to_string());
    let watch_owner_id_json =
        serde_json::to_string(watch_owner_id).unwrap_or_else(|_| "\"\"".to_string());
    let host_process_id = crate::current_host_process_id();
    let pair_pre_name_json =
        serde_json::to_string(pair_pre_name).unwrap_or_else(|_| "\"\"".to_string());
    let paired_pre_instance_id_json =
        serde_json::to_string(paired_pre_instance_id).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        r#"{{"v":2,"role":"POST","instance_id":{instance_id_json},"daw_session_id":{daw_session_id},"host_process_id":{host_process_id},"watch_owner_id":{watch_owner_id},"signal_state":"{signal_state}","t":"{t}","pair_pre_name":{pair_pre_name},"paired_pre_instance_id":{paired_pre_instance_id},"pair_claimed_at":{pair_claimed_at}}}"#,
        instance_id_json = instance_id_json,
        daw_session_id = daw_session_id_json,
        host_process_id = host_process_id,
        watch_owner_id = watch_owner_id_json,
        signal_state = state.as_str(),
        t = t,
        pair_pre_name = pair_pre_name_json,
        paired_pre_instance_id = paired_pre_instance_id_json,
        pair_claimed_at = pair_claimed_at,
    )
}

fn opt_f64(v: Option<f64>) -> String {
    match v {
        Some(x) => format!("{:.3}", x),
        None => "null".to_string(),
    }
}

fn phase_d_fragment(result: &MeasureResult) -> String {
    let mut s = String::new();
    if let Some(n) = result.n_prime_total {
        s.push_str(&format!(r#","n_prime_total":{:.3}"#, n));
    }
    if let Some(sh) = result.sharpness {
        s.push_str(&format!(r#","sharpness":{:.3}"#, sh));
    }
    if let Some(ref psb) = result.psb_summary {
        s.push_str(&format!(
            r#","psb_summary":{{"low":{:.3},"mid":{:.3},"high":{:.3}}}"#,
            psb.low, psb.mid, psb.high
        ));
    }
    s
}

// ── ACK タイムアウト監視（G-60-02 / B-7）──────────────────────────────────

fn poll_ack_timeout(
    project_hash: &str,
    instance_id: &str,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
) {
    let base = match StoragePaths::default_platform() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_ack_timeout_with_base(
        &base,
        project_hash,
        instance_id,
        record_sm,
        pair_label,
        paired_pre_target,
        chrono::Utc::now(),
    );
}

/// Legacy test helper. Runtime liveness uses the exact `LatchedPre::pre_json` path and never scans
/// project directories.
/// B-024 Group A / Gap-2: kirin_root 配下の全 project_hash を横断 scan して
/// `*/{pre_iid}/pre.json` の中で最新 mtime を返す。
///
/// cdylib 隔離下で PRE/POST の `project_hash` が乖離するため、POST 自身の
/// `project_hash` だけを見ても PRE pre.json は見つからない。`pre_discovery::
/// discover_active_pre_dir` と同じ走査方針を採用 (project_dir 全件 + 当該
/// instance_id 直結 read)。
///
/// R-28 機能的沈黙: 各エラー (read_dir 不能 / metadata 不能 / modified 不能) は
/// 当該 dir/file のみ skip。全件失敗 / 不在なら None。
#[cfg(test)]
fn find_pre_json_mtime(kirin_root: &Path, pre_iid: &str) -> Option<SystemTime> {
    if pre_iid.is_empty() {
        return None;
    }
    // B-128 reopen / G-115-376: `pre_iid` は他 instance pre.json の content instance_id
    // (record_signal.rs:569 → pairing latch → `paired_pre_target` / 本 file:104 doc) 由来で、
    // content 由来 component を `.join()` する **唯一** の production path builder。within-base
    // DiD invariant (G-115-368) の例外であり、read-only でも path-unsafe 値 (`..` / 絶対 / 区切り /
    // 制御文字 / overlength / `_q_`) を join すると base 外の存在・mtime を観測する **mtime オラクル**
    // になる。よって `.join()` 前に within-base wall (`is_path_safe_component` = guard_path_component
    // が内部で使う同一述語) を通し、unsafe は **stat せず** pairing no-match (None) で返す。
    // 書込 builder の `guard_path_component` と違い quarantine 名で stat し続けず・event も出さない:
    // 本経路は利用者意図操作と非紐づきの read probe ゆえ R-28 surface 不要 (toast/event なし)。
    if !crate::path_identity::is_path_safe_component(pre_iid) {
        return None;
    }
    let project_entries = fs::read_dir(kirin_root).ok()?;
    let mut latest: Option<SystemTime> = None;
    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        let pre_json = project_dir.join(pre_iid).join("pre.json");
        let meta = match fs::metadata(&pre_json) {
            Ok(m) => m,
            Err(_) => continue,
        };
        if !meta.is_file() {
            continue;
        }
        let mtime = match meta.modified() {
            Ok(t) => t,
            Err(_) => continue,
        };
        latest = Some(match latest {
            Some(prev) if prev > mtime => prev,
            _ => mtime,
        });
    }
    latest
}

/// B-024 Group A / Gap-2: POST 側 PRE 死活監視 sub-tick の本体。
///
/// `record_sm` が Record 中で、かつ `paired_pre_target` が `Some(pre_iid)` のとき:
///   1. `find_pre_json_mtime(kirin_root, pre_iid)` で PRE pre.json の最新 mtime を取得
///   2. `now - mtime > PRE_LIVENESS_STALE_SECS` (60 秒 / G-50-33) または mtime 不在を検出
///   3. 検出時も Record は維持する。stem/offline export 後は DAW が process 更新を止めるため、
///      ここで `exit_record_full` すると Keep が利用者の Stop 前に消える。
#[cfg(test)]
fn poll_pre_liveness(
    kirin_root: &Path,
    project_hash: &str,
    self_post_iid: &str,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
) {
    if !record_sm.is_recording() {
        return;
    }
    let Some(pre_iid) = paired_pre_target.lock().ok().and_then(|g| g.clone()) else {
        return;
    };
    let plugin_data_root = match StoragePaths::default_platform() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_pre_liveness_at(
        kirin_root,
        &plugin_data_root,
        project_hash,
        self_post_iid,
        &pre_iid,
        record_sm,
        pair_label,
        paired_pre_target,
        SystemTime::now(),
    );
}

/// `poll_pre_liveness` の純粋ロジック版 (テスト容易性のため `now` と `plugin_data_root`
/// を注入)。Production は `poll_pre_liveness` を経由する。
#[allow(clippy::too_many_arguments)]
#[cfg(test)]
fn poll_pre_liveness_at(
    kirin_root: &Path,
    _plugin_data_root: &Path,
    _project_hash: &str,
    self_post_iid: &str,
    pre_iid: &str,
    _record_sm: &Arc<RecordStateMachine>,
    _pair_label: &Arc<Mutex<String>>,
    _paired_pre_target: &Arc<Mutex<Option<String>>>,
    now: SystemTime,
) {
    let stale = match find_pre_json_mtime(kirin_root, pre_iid) {
        Some(mtime) => match now.duration_since(mtime) {
            Ok(d) => d.as_secs() > PRE_LIVENESS_STALE_SECS,
            Err(_) => false, // future mtime (clock skew): fresh 扱い
        },
        None => true, // pre.json 不在 = PRE 既に消失
    };
    if !stale {
        return;
    }
    log::warn!(
        "[POST liveness] PRE pre.json stale > {}s — keeping record armed (partner_pre_iid={}, post_iid={})",
        PRE_LIVENESS_STALE_SECS,
        pre_iid,
        self_post_iid
    );
}

/// Record liveness diagnostic on one producer-selected PRE path. This never changes Record state:
/// offline bounce can legitimately stop Watch updates, and only Stop/Drop/idle timeout own the
/// Record lifecycle.
fn poll_latched_pre_liveness(
    self_post_iid: &str,
    record_sm: &Arc<RecordStateMachine>,
    latched: &Mutex<Option<LatchedPre>>,
) {
    if !record_sm.is_recording() {
        return;
    }
    let Some(pre) = latched.lock().ok().and_then(|binding| binding.clone()) else {
        return;
    };
    let stale = match fs::metadata(&pre.pre_json)
        .ok()
        .filter(|metadata| metadata.is_file())
        .and_then(|metadata| metadata.modified().ok())
    {
        Some(mtime) => SystemTime::now()
            .duration_since(mtime)
            .map(|age| age.as_secs() > PRE_LIVENESS_STALE_SECS)
            .unwrap_or(false),
        None => true,
    };
    if stale {
        log::warn!(
            "[POST liveness] exact PRE pre.json stale > {}s — keeping record armed (partner_pre_iid={}, post_iid={})",
            PRE_LIVENESS_STALE_SECS,
            pre.instance_id,
            self_post_iid
        );
    }
}

fn poll_ack_timeout_with_base(
    base: &Path,
    project_hash: &str,
    instance_id: &str,
    _record_sm: &Arc<RecordStateMachine>,
    _pair_label: &Arc<Mutex<String>>,
    _paired_pre_target: &Arc<Mutex<Option<String>>>,
    now: chrono::DateTime<chrono::Utc>,
) {
    let Some(signal) = record_signal::read_signal(base, project_hash, instance_id) else {
        return;
    };
    if signal.status != SignalStatus::Pending {
        return;
    }
    if !record_signal::is_timed_out(&signal, now, ACK_TIMEOUT_SECONDS) {
        return;
    }
    log::warn!(
        "[IOThread POST] ACK timeout ({}s) — keeping Record armed",
        ACK_TIMEOUT_SECONDS
    );
}

fn release_record_reservation(
    base: &Path,
    project_hash: &str,
    post_iid: &str,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    reason: &str,
) {
    let released_pre = paired_pre_target.lock().ok().and_then(|g| g.clone());
    if let Some(pre) = released_pre.as_deref() {
        crate::reservation::release_pairing(base, project_hash, pre, post_iid);
        log::info!(
            "[IOThread POST] reservation released: reason={} pre={} post={}",
            reason,
            pre,
            post_iid
        );
    }
}

/// B-023 段階 4: pair_label 表示文字列を組み立てる（POST GUI / PRE Name 反映）。
///
/// 単一情報源（`kirin_measure::format_pair_label` 経由で hypha_post から再利用）。
/// drift 防止のため Keep 時 / poll 時 / Stop 時 の全パスから本関数を経由して
/// 同一フォーマットを生成する。
///
/// - `paired_pre_name` 非空 → `pair: <name>`
/// - `paired_pre_name` 空   → `pair: <target_id 先頭 8 文字>`（PRE_ プレフィックス無し）
pub fn format_pair_label(paired_pre_name: &str, target_id: &str) -> String {
    if !paired_pre_name.is_empty() {
        format!("pair: {}", paired_pre_name)
    } else {
        let short: String = target_id.chars().take(8).collect();
        format!("pair: {}", short)
    }
}

/// B-023 段階 4: record_signal の Acknowledged を検知して pair_label を更新。
///
/// `record_sm.is_recording()` でガードし、Stop 後の poll で削除前の Acknowledged
/// signal を読んで pair_label が復活する race を構造的に防ぐ。
/// 値変化時のみ書込（無音 idempotent / R-28 機能的沈黙）。
fn poll_record_signal_ack(
    project_hash: &str,
    instance_id: &str,
    sample_rate: u32,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
) {
    let base = match StoragePaths::default_platform() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_record_signal_ack_with_base(
        &base,
        project_hash,
        instance_id,
        sample_rate,
        record_sm,
        pair_label,
    );
}

fn poll_record_signal_ack_with_base(
    base: &Path,
    project_hash: &str,
    instance_id: &str,
    sample_rate: u32,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
) {
    let Some(signal) = record_signal::read_signal(base, project_hash, instance_id) else {
        return;
    };
    if signal.status != SignalStatus::Acknowledged {
        return;
    }
    if signal.expected_wav.is_some()
        && !signal
            .expected_wav
            .as_ref()
            .is_some_and(crate::record_expected::ExpectedWavMetadata::is_usable)
    {
        log::warn!(
            "[IOThread POST] ACK has invalid expected WAV metadata; Record may publish with \
             degraded integrity \
             (post_iid={})",
            instance_id
        );
    }
    if !record_sm.is_recording() {
        let Some(started_at_ms) = parse_iso8601_to_epoch_ms(&signal.started_at) else {
            log::warn!(
                "[IOThread POST] ACK ignored: started_at invalid (post_iid={}, started_at={:?})",
                instance_id,
                signal.started_at
            );
            return;
        };
        let now_ms = crate::record_writer::now_epoch_ms();
        if now_ms < started_at_ms {
            return;
        }
        if crate::record_writer::record_session_closed_for_role_instance(
            base,
            project_hash,
            instance_id,
            PluginDataRole::Post,
            &signal.session_id,
        ) {
            log::warn!(
                "[IOThread POST] ACK ignored: session already closed on disk \
                 (session={}, post_iid={})",
                signal.session_id,
                instance_id
            );
            return;
        }
        match crate::record_entry_lock::claim_record_entry(
            base,
            project_hash,
            &signal.session_id,
            PluginDataRole::Post,
            instance_id,
        ) {
            Ok(()) => {}
            Err(crate::record_entry_lock::RecordEntryLockError::AlreadyActive { .. }) => {
                log::warn!(
                    "[IOThread POST] ACK ignored: record entry already owned \
                     (session={}, post_iid={})",
                    signal.session_id,
                    instance_id
                );
                return;
            }
            Err(e) => {
                log::warn!(
                    "[IOThread POST] ACK ignored: record entry claim failed \
                     (session={}, post_iid={}): {}",
                    signal.session_id,
                    instance_id,
                    e
                );
                return;
            }
        }
        if crate::record_writer_claim::writer_claim_active(
            base,
            project_hash,
            &signal.session_id,
            PluginDataRole::Post,
            instance_id,
        )
        .unwrap_or(false)
        {
            log::warn!(
                "[IOThread POST] ACK ignored: writer already active \
                 (session={}, post_iid={})",
                signal.session_id,
                instance_id
            );
            return;
        }
        match record_sm.try_enter_record_started_at_clock_window_transaction(
            crate::License::Os,
            started_at_ms,
            signal.started_at_position_samples,
            signal.expected_end_position_samples_for_sample_rate(sample_rate),
            signal.session_id.clone(),
        ) {
            Ok(()) => log::info!(
                "[IOThread POST] ACK received; POST entered Record (session={}, post_iid={})",
                signal.session_id,
                instance_id
            ),
            Err(crate::record::TransitionError::AlreadyRecording) => {}
            Err(e) => {
                log::warn!("[IOThread POST] ACK Record enter rejected: {:?}", e);
                return;
            }
        }
    }
    let new_label = format_pair_label(&signal.paired_pre_name, &signal.target_pre_instance_id);
    if let Ok(mut g) = pair_label.lock() {
        if *g != new_label {
            log::info!(
                "[IOThread POST] pair_label updated: {} (paired_pre_name={:?})",
                new_label,
                signal.paired_pre_name
            );
            *g = new_label;
        }
    }
}

// ── preset/ poller ──────────────────────────────────────────────────────────

fn current_preset_exists(preset_dir: &Path) -> bool {
    fs::metadata(preset_dir.join("current.json"))
        .map(|metadata| metadata.is_file())
        .unwrap_or(false)
}

fn poll_preset_availability(
    project_hash: &str,
    preset_available: &Arc<AtomicBool>,
    last_seen: &mut Option<bool>,
) {
    let preset_dir = match StoragePaths::default_platform() {
        // B-128 (G-115-370): within-base wall（preset availability read。preset_dir 同等の inline 構築）。
        Ok(paths) => paths
            .plugin_data_dir()
            .join(&*crate::path_identity::guard_path_component(
                project_hash,
                "io_thread_post.poll_preset.project_hash",
            ))
            .join(crate::preset::PRESET_SUBDIR),
        Err(_) => {
            if *last_seen != Some(false) {
                log::info!("[preset] unavailable");
                *last_seen = Some(false);
            }
            preset_available.store(false, Ordering::Relaxed);
            return;
        }
    };
    let available = current_preset_exists(&preset_dir);
    preset_available.store(available, Ordering::Relaxed);

    if *last_seen != Some(available) {
        if available {
            log::info!("[preset] available");
        } else {
            log::info!("[preset] unavailable");
        }
        *last_seen = Some(available);
    }
}

// ── Tests ─────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod b206_idle_autostop_tests {
    use super::{
        drop_commit_matches_observed_capture, idle_autostop_due, parse_idle_timeout,
        record_idle_timeout,
    };
    use crate::record_expected::ExpectedWavMetadata;
    use crate::record_take::{RecordTakeBlock, RecordTakeTracker};
    use std::time::Duration;

    #[test]
    fn record_idle_timeout_is_enabled_by_default() {
        assert_eq!(record_idle_timeout(), Some(Duration::from_secs(600)));
    }

    /// B-206: timeout override パース。無効/欠落/下限未満は既定 600s、有効値は採用。
    #[test]
    fn parse_idle_timeout_override() {
        assert_eq!(
            parse_idle_timeout(None),
            Duration::from_secs(600),
            "欠落 → 既定600s"
        );
        assert_eq!(
            parse_idle_timeout(Some("60".into())),
            Duration::from_secs(60),
            "有効値採用"
        );
        assert_eq!(
            parse_idle_timeout(Some(" 30 ".into())),
            Duration::from_secs(30),
            "trim して採用"
        );
        assert_eq!(
            parse_idle_timeout(Some("abc".into())),
            Duration::from_secs(600),
            "非数 → 既定"
        );
        assert_eq!(
            parse_idle_timeout(Some("0".into())),
            Duration::from_secs(600),
            "下限未満(0) → 既定"
        );
        assert_eq!(
            parse_idle_timeout(Some("4".into())),
            Duration::from_secs(600),
            "下限未満(4) → 既定"
        );
        assert_eq!(
            parse_idle_timeout(Some("5".into())),
            Duration::from_secs(5),
            "下限ちょうど → 採用"
        );
    }

    /// B-206: idle auto-stop 判定の境界。録音中×非Active×しきい値到達でのみ true。
    #[test]
    fn idle_autostop_due_boundary() {
        let t = Duration::from_secs(600);
        // 録音中・非Active・10分到達/超過 → 停止
        assert!(
            idle_autostop_due(true, false, Duration::from_secs(600), Some(t)),
            "ちょうど10分で停止"
        );
        assert!(
            idle_autostop_due(true, false, Duration::from_secs(601), Some(t)),
            "10分超で停止"
        );
        // 10分未満 → 停止しない
        assert!(
            !idle_autostop_due(true, false, Duration::from_secs(599), Some(t)),
            "10分未満は停止しない"
        );
        // Active 中は経過に関わらず絶対に停止しない（録音継続中）
        assert!(
            !idle_autostop_due(true, true, Duration::from_secs(99_999), Some(t)),
            "Active 中は停止しない"
        );
        // 非録音は停止対象外
        assert!(
            !idle_autostop_due(false, false, Duration::from_secs(99_999), Some(t)),
            "非録音は対象外"
        );
        assert!(
            !idle_autostop_due(true, false, Duration::from_secs(99_999), None),
            "timeout disabledなら停止権限を持たない"
        );
    }

    #[test]
    fn drop_commit_requires_bwf_range_observed_by_this_post() {
        let tracker = RecordTakeTracker::new();
        tracker.note_capture_window(true, 96_000, 48_000);
        let mut expected = ExpectedWavMetadata {
            expected_duration_samples: 48_000,
            expected_sample_rate: 48_000,
            wav_time_reference_samples: Some(96_000),
            wav_path: "/tmp/drop.wav".to_string(),
            bounce_id: "bounce".to_string(),
            created_at_ms: chrono::Utc::now().timestamp_millis(),
            wav_file_size: Some(1),
            wav_mtime_ms: chrono::Utc::now().timestamp_millis(),
            wav_hash: Some("hash".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        };
        assert!(drop_commit_matches_observed_capture(&expected, &tracker, 1));

        expected.wav_time_reference_samples = Some(192_000);
        assert!(!drop_commit_matches_observed_capture(
            &expected, &tracker, 1
        ));
        expected.wav_time_reference_samples = None;
        assert!(!drop_commit_matches_observed_capture(
            &expected, &tracker, 1
        ));
    }

    #[test]
    fn non_bwf_drop_commit_requires_exact_current_render_generation_and_duration() {
        let tracker = RecordTakeTracker::new();
        tracker.note_block(RecordTakeBlock {
            generation: 7,
            recording: true,
            rendered: true,
            playing: true,
            offline: true,
            position_valid: true,
            position_samples: 96_000,
            num_frames: 48_000,
            clock_start_samples: 96_000,
            clock_end_samples: Some(144_000),
        });
        let mut expected = ExpectedWavMetadata {
            expected_duration_samples: 48_000,
            expected_sample_rate: 48_000,
            wav_time_reference_samples: None,
            wav_path: "/tmp/drop-no-bwf.wav".to_string(),
            bounce_id: "bounce-no-bwf".to_string(),
            created_at_ms: chrono::Utc::now().timestamp_millis(),
            wav_file_size: Some(1),
            wav_mtime_ms: chrono::Utc::now().timestamp_millis(),
            wav_hash: Some("hash-no-bwf".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        };
        assert!(drop_commit_matches_observed_capture(&expected, &tracker, 7));
        assert!(!drop_commit_matches_observed_capture(
            &expected, &tracker, 8
        ));
        expected.expected_duration_samples += 1;
        assert!(!drop_commit_matches_observed_capture(
            &expected, &tracker, 7
        ));
    }
}

#[cfg(test)]
mod self_check_release_gate_tests {
    use super::{SelfCheckReleaseGate, SELF_CHECK_RELEASE_CONFIRMATIONS};

    #[test]
    fn conflict_must_repeat_before_release_is_confirmed() {
        let mut gate = SelfCheckReleaseGate::default();

        for i in 1..SELF_CHECK_RELEASE_CONFIRMATIONS {
            assert!(
                !gate.observe_conflict("PRE-A", 100.0),
                "confirmation {i} must not release yet"
            );
        }

        assert!(
            gate.observe_conflict("PRE-A", 100.0),
            "third consecutive same conflict confirms release"
        );
    }

    #[test]
    fn reset_discards_partial_confirmations() {
        let mut gate = SelfCheckReleaseGate::default();

        assert!(!gate.observe_conflict("PRE-A", 100.0));
        assert!(!gate.observe_conflict("PRE-A", 100.0));
        gate.reset();

        assert!(
            !gate.observe_conflict("PRE-A", 100.0),
            "playback/Record/Active gate reset must force a fresh confirmation run"
        );
    }

    #[test]
    fn changed_candidate_restarts_confirmation_count() {
        let mut gate = SelfCheckReleaseGate::default();

        assert!(!gate.observe_conflict("PRE-A", 100.0));
        assert!(!gate.observe_conflict("PRE-A", 100.0));
        assert!(
            !gate.observe_conflict("PRE-B", 100.0),
            "different pair name starts a new candidate"
        );
        assert!(
            !gate.observe_conflict("PRE-B", 100.0),
            "new candidate still needs repeated confirmations"
        );
    }
}

#[cfg(test)]
mod b222_all_stop_keep_barrier_tests {
    use super::{keep_broadcast_blocked_by_stop, remember_latest_started_at};

    #[test]
    fn stop_barrier_blocks_older_or_equal_keep_broadcast() {
        let stop = Some("2026-07-04T07:57:31Z");

        assert!(
            keep_broadcast_blocked_by_stop("2026-07-04T07:56:58Z", stop),
            "old all_keep_signal must not re-arm after all_stop_signal"
        );
        assert!(
            keep_broadcast_blocked_by_stop("2026-07-04T07:57:31Z", stop),
            "same-timestamp keep must lose to stop"
        );
    }

    #[test]
    fn stop_barrier_allows_new_keep_after_stop() {
        assert!(
            !keep_broadcast_blocked_by_stop("2026-07-04T07:57:32Z", Some("2026-07-04T07:57:31Z")),
            "a deliberate new Keep after Stop must remain possible"
        );
        assert!(
            !keep_broadcast_blocked_by_stop("2026-07-04T07:57:32Z", None),
            "no stop barrier means normal keep handling"
        );
    }

    #[test]
    fn remember_latest_started_at_keeps_newest_iso_timestamp() {
        let mut latest = None;

        remember_latest_started_at(&mut latest, "2026-07-04T07:57:20Z");
        remember_latest_started_at(&mut latest, "2026-07-04T07:57:19Z");
        remember_latest_started_at(&mut latest, "2026-07-04T07:57:31Z");

        assert_eq!(latest.as_deref(), Some("2026-07-04T07:57:31Z"));
    }
}

#[cfg(test)]
mod preset_poll_tests {
    use super::*;
    use std::sync::atomic::AtomicU64;

    fn isolated_dir(tag: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_preset_poll_test_{pid}_{n}_{tag}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn empty_dir_has_no_current_pointer() {
        let dir = isolated_dir("empty");
        assert!(!current_preset_exists(&dir));
    }

    #[test]
    fn missing_dir_has_no_current_pointer() {
        let dir = isolated_dir("missing");
        let child = dir.join("no_such");
        assert!(!current_preset_exists(&child));
    }

    #[test]
    fn current_pointer_is_available() {
        let dir = isolated_dir("one");
        fs::write(dir.join("current.json"), b"x").unwrap();
        assert!(current_preset_exists(&dir));
    }

    #[test]
    fn history_tmp_and_non_json_never_make_preset_available() {
        let dir = isolated_dir("ignore");
        fs::write(dir.join("history.json"), b"x").unwrap();
        fs::write(dir.join("notes.txt"), b"x").unwrap();
        fs::write(dir.join("current.json.tmp"), b"x").unwrap();
        assert!(!current_preset_exists(&dir));
    }

    #[test]
    fn current_pointer_must_be_a_file() {
        let dir = isolated_dir("current_dir");
        fs::create_dir_all(dir.join("current.json")).unwrap();
        assert!(!current_preset_exists(&dir));
    }
}

// ── Tests (compute_delta with new structure) ─────────────────────────────────
#[cfg(test)]
mod compute_delta_tests {
    use super::*;
    use std::sync::atomic::AtomicU64;

    fn isolated_project_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir()
            .join(format!("kirin_compute_delta_test_{pid}_{n}"))
            .join("ph");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn write_pre(project_dir: &Path, instance_id: &str, t: &str, lufs: f64) {
        let dir = project_dir.join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let json = format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","t":"{t}","lufs_m":{lufs},"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
        );
        fs::write(dir.join("pre.json"), json).unwrap();
    }

    /// W-280: pair filter テスト用 — `name` field 付き pre.json を書き出す。
    fn write_pre_named(project_dir: &Path, instance_id: &str, name: &str, t: &str, lufs: f64) {
        let dir = project_dir.join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let json = format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","name":"{name}","signal_state":"active","t":"{t}","lufs_m":{lufs},"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
        );
        fs::write(dir.join("pre.json"), json).unwrap();
    }

    #[test]
    fn no_pre_dir_returns_no_pre_mode() {
        let pd = isolated_project_dir();
        let r = compute_delta_with_state(
            &pd,
            &MeasureResult {
                lufs_m: Some(-10.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
                ..Default::default()
            },
            None,
        )
        .unwrap();
        assert_eq!(r.0.mode, DeltaMode::NoPre);
    }

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

    /// T1: ラッチ後、PRE が silence/idle（signal_state=inactive・fresh）でも Stale（NoPre でない）+
    /// 全Δ None + last_active クリア。ラッチは保持される。
    #[test]
    fn latch_idle_stays_stale_not_nopre() {
        let root = isolated_dir("latch_idle");
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let latched = std::sync::Mutex::new(None);
        let (d0, sd0, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(d0.mode, DeltaMode::Active, "active で初回 Δ");
        assert!(!sd0);
        assert!(latched.lock().unwrap().is_some(), "active で初回ラッチ成立");
        // PRE が idle（fresh のまま signal_state=inactive）。
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "inactive", &latch_now());
        let (d1, sd1, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(
            d1.mode,
            DeltaMode::Stale,
            "idle はラッチ維持で Stale（NoPre でない）"
        );
        assert!(sd1, "latched-idle は store_directly（last_active クリア）");
        assert!(
            d1.lufs.is_none() && d1.last_active.is_none(),
            "全Δ None + 凍結なし"
        );
        assert!(latched.lock().unwrap().is_some(), "idle でラッチは外れない");
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

    /// 現行PRE leaseの終了はexact latchを完全に外し、同名の再作成へ即再接続する。
    #[test]
    fn released_owner_detaches_and_recreated_name_relatches() {
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
            latched.lock().unwrap().is_none(),
            "released owner must detach"
        );
        assert_eq!(d.mode, DeltaMode::NoPre);

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
        assert_eq!(d2.mode, DeltaMode::Active, "recreated PRE must reconnect");
        assert_eq!(
            latched
                .lock()
                .unwrap()
                .as_ref()
                .map(|pre| pre.instance_id.as_str()),
            Some("iid-B")
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

    /// T5c: PRE が明示 Bypassed のときは、pair は維持したまま POST 単独表示へ戻す。
    #[test]
    fn latch_pre_bypassed_keeps_pair_but_marks_bypassed() {
        let root = isolated_dir("latch_pre_bypassed");
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

        write_pre_latch(&root, "puid-1", "iid-A", "snare", "bypassed", &latch_now());
        let (d, sd, pre_state) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(d.mode, DeltaMode::Bypassed);
        assert!(sd);
        assert_eq!(pre_state, Some(SignalState::Bypassed));
        assert!(
            latched.lock().unwrap().is_some(),
            "PRE bypass must not release the explicit pair"
        );
    }

    /// T6: 停止中(inactive)の PRE を Arm でラッチでき、再生再開(active)で live Δ が出る。
    /// Step2「stopped Inactive PRE を Keep でき、再生後 Delta が出る」(5c) のシナリオ。
    /// realtime end-to-end の `b140_inactive_keep_latches_pre_for_delta_after_audio`
    /// (parity.rs, #[ignore], 遅い) が既に同経路を被覆しているが、本テストは
    /// `compute_latched_display` 直叩きで決定的・高速な等価カバレッジを通常ゲートに足す。
    #[test]
    fn latch_inactive_then_active_yields_live_delta() {
        let root = isolated_dir("latch_inactive_to_active");
        // 停止中: PRE は fresh だが signal_state=inactive。
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "inactive", &latch_now());
        let latched = std::sync::Mutex::new(None);
        let (d0, sd0, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        // Arm は inactive-fresh をラッチする（Keep 可）が、表示は active を要求するため
        // latched-idle = Stale（NoPre には落とさない）。
        assert!(
            latched.lock().unwrap().is_some(),
            "inactive-fresh でも Arm でラッチ成立（Keep 可）"
        );
        assert_eq!(d0.mode, DeltaMode::Stale, "停止中は latched-idle（Stale）");
        assert!(sd0, "latched-idle は store_directly（last_active クリア）");
        assert!(d0.lufs.is_none(), "停止中は Δ 非表示");

        // 再生再開: 同 instance が active+fresh に遷移。
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let (d1, sd1, _) = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert_eq!(d1.mode, DeltaMode::Active, "再生後は live Δ（Active）");
        assert!(
            !sd1,
            "Active は store_directly でない（last_active 保存経路）"
        );
        assert_eq!(
            d1.lufs,
            Some(4.0),
            "Δlufs = post(-10.0) − pre(-14.0) = 4.0（ラッチ先 pre.json 直読）"
        );
        assert!(
            latched.lock().unwrap().is_some(),
            "active 遷移後もラッチ維持"
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

    #[test]
    fn single_instance_pass_through_when_no_pair() {
        // W-280: pair=None かつ instance 1 件のみ → 後方互換で pass-through (Active 算出可)。
        let pd = isolated_project_dir();
        let now = chrono::Utc::now()
            .format("%Y-%m-%dT%H:%M:%S%.3fZ")
            .to_string();
        write_pre(&pd, "iid-A", &now, -14.0);

        let r = compute_delta_with_state(
            &pd,
            &MeasureResult {
                lufs_m: Some(-10.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
                ..Default::default()
            },
            None,
        )
        .unwrap();
        assert_eq!(r.0.mode, DeltaMode::Active);
        assert!(r.0.lufs.is_some());
    }

    #[test]
    fn record_signal_subdir_is_skipped() {
        let pd = isolated_project_dir();
        // record_signal/ ディレクトリを作るが pre.json は無い
        let signal_dir = pd.join(SIGNALS_SUBDIR);
        fs::create_dir_all(&signal_dir).unwrap();
        fs::write(signal_dir.join("post-1.json"), b"{}").unwrap();
        let r = compute_delta_with_state(
            &pd,
            &MeasureResult {
                lufs_m: Some(-10.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
                ..Default::default()
            },
            None,
        )
        .unwrap();
        // record_signal/ 以外に pre が無いので NoPre
        assert_eq!(r.0.mode, DeltaMode::NoPre);
    }

    // ── W-280 / G-115-248: pair filter 経路テスト (A-6) ─────────────────────

    /// (i) pair filter で name 不一致の pre.json は無視される。
    #[test]
    fn pair_filter_skips_non_matching_name() {
        let pd = isolated_project_dir();
        let now = chrono::Utc::now()
            .format("%Y-%m-%dT%H:%M:%S%.3fZ")
            .to_string();
        write_pre_named(&pd, "iid-A", "snare", &now, -14.0);
        write_pre_named(&pd, "iid-B", "kick", &now, -15.0);

        // pair_pre_name = "snare" を指定 → iid-A のみ採用 → Active で iid-A の Δ
        let r = compute_delta_with_state(
            &pd,
            &MeasureResult {
                lufs_m: Some(-10.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
                ..Default::default()
            },
            Some("snare"),
        )
        .unwrap();
        assert_eq!(r.0.mode, DeltaMode::Active);
        // Δ = POST(-10) - PRE(-14) = +4.0 (iid-A 採用確認 / iid-B の -15 ではない)
        let delta_lufs = r.0.lufs.expect("delta.lufs should be Some");
        assert!(
            (delta_lufs - 4.0).abs() < 0.01,
            "expected Δ from iid-A (snare) ~+4.0, got {}",
            delta_lufs
        );
    }

    /// (ii) 2 セット環境で select_best_pre が pair 内 max t を選ぶ。
    #[test]
    fn pair_filter_picks_max_t_within_pair() {
        let pd = isolated_project_dir();
        // 同じ name "snare" を持つ pre.json 2 件 + 別 name "kick" 1 件。
        // t は ISO 8601 文字列比較で kick(最新) > snare-NEW > snare-OLD の順序を保ち、
        // 「name filter 後に pair 内 max t = snare-new」を検証する。
        // 固定日付 (旧: 2026-05-17) は freshness_mode の NO_PRE_SECS(10s) 窓を実行日に
        // 抜けて NoPre 化する time-bomb のため、now() 基準の相対オフセットで生成する
        // (選択される snare-new を STALE_SECS(5s) 内に収め Active 判定にする / B-054)。
        let base = chrono::Utc::now();
        let fmt_t = |secs_ago: i64| {
            (base - chrono::Duration::seconds(secs_ago))
                .format("%Y-%m-%dT%H:%M:%S%.3fZ")
                .to_string()
        };
        write_pre_named(&pd, "iid-snare-old", "snare", &fmt_t(2), -14.0);
        write_pre_named(&pd, "iid-snare-new", "snare", &fmt_t(1), -16.0);
        write_pre_named(&pd, "iid-kick", "kick", &fmt_t(0), -12.0);

        let r = compute_delta_with_state(
            &pd,
            &MeasureResult {
                lufs_m: Some(-10.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
                ..Default::default()
            },
            Some("snare"),
        )
        .unwrap();
        assert_eq!(r.0.mode, DeltaMode::Active);
        // snare 内 max t = iid-snare-new (lufs=-16) / Δ = -10 - (-16) = +6.0
        let delta_lufs = r.0.lufs.expect("delta.lufs should be Some");
        assert!(
            (delta_lufs - 6.0).abs() < 0.01,
            "expected Δ from snare-new ~+6.0, got {} (kick (latest t) must not be picked)",
            delta_lufs
        );
    }

    /// (iii) pair filter 後 0 件で DeltaMode::NoPre に落ちる。
    #[test]
    fn pair_filter_zero_match_falls_to_no_pre() {
        let pd = isolated_project_dir();
        let now = chrono::Utc::now()
            .format("%Y-%m-%dT%H:%M:%S%.3fZ")
            .to_string();
        write_pre_named(&pd, "iid-A", "snare", &now, -14.0);
        write_pre_named(&pd, "iid-B", "kick", &now, -15.0);

        // pair_pre_name = "vocal" を指定 → どれも一致せず → 0 件 → NoPre
        let r = compute_delta_with_state(
            &pd,
            &MeasureResult {
                lufs_m: Some(-10.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
                ..Default::default()
            },
            Some("vocal"),
        )
        .unwrap();
        assert_eq!(r.0.mode, DeltaMode::NoPre);
        assert!(r.0.lufs.is_none());
    }

    /// W-280 補強: pair=None かつ 2 件以上 → 曖昧として NoPre (R-7/R-26 沈黙ゲート)。
    /// 元の `scans_across_instance_ids` テストが期待していた挙動の反転を明示する。
    #[test]
    fn no_pair_with_multiple_instances_falls_to_no_pre() {
        let pd = isolated_project_dir();
        let now = chrono::Utc::now()
            .format("%Y-%m-%dT%H:%M:%S%.3fZ")
            .to_string();
        write_pre(&pd, "iid-A", &now, -14.0);
        write_pre(&pd, "iid-B", &now, -15.0);

        let r = compute_delta_with_state(
            &pd,
            &MeasureResult {
                lufs_m: Some(-10.0),
                true_peak: Some(-1.0),
                crest: Some(12.0),
                ..Default::default()
            },
            None,
        )
        .unwrap();
        assert_eq!(r.0.mode, DeltaMode::NoPre);
        assert!(r.0.lufs.is_none());
    }
}

#[cfg(test)]
mod record_signal_ack_barrier_tests {
    use super::*;
    use crate::record::RecordState;
    use crate::record_expected::ExpectedWavMetadata;
    use crate::record_signal::{RecordSignal, SignalStatus};
    use std::sync::atomic::{AtomicU64, Ordering};

    const TEST_PH: &str = "ph";
    const TEST_POST_IID: &str = "post-iid";

    fn isolated_base(tag: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_ack_barrier_{pid}_{n}_{tag}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn expected_wav() -> ExpectedWavMetadata {
        ExpectedWavMetadata {
            expected_duration_samples: 48_000,
            expected_sample_rate: 48_000,
            wav_time_reference_samples: None,
            wav_path: "/tmp/kirin-post-ack-test.wav".to_string(),
            bounce_id: "test-bounce".to_string(),
            created_at_ms: chrono::Utc::now().timestamp_millis(),
            wav_file_size: Some(1),
            wav_mtime_ms: chrono::Utc::now().timestamp_millis(),
            wav_hash: Some("test-wav-hash".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    fn write_ack(base: &Path, started_at: &str) {
        write_ack_with_expected(base, started_at, Some(expected_wav()));
    }

    fn write_ack_with_expected(
        base: &Path,
        started_at: &str,
        expected_wav: Option<ExpectedWavMetadata>,
    ) {
        let signal = RecordSignal {
            status: SignalStatus::Acknowledged,
            requested_by: TEST_POST_IID.to_string(),
            target_pre_instance_id: "pre-iid".to_string(),
            daw_session_id: "daw-1".to_string(),
            session_id: "session-post-ack".to_string(),
            capture_generation_id: String::new(),
            generation_started_at_ms: 0,
            t: "2026-07-05T00:00:00Z".to_string(),
            started_at: started_at.to_string(),
            started_at_position_samples: None,
            paired_pre_name: "PRE".to_string(),
            release_reason: None,
            expected_wav,
        };
        crate::record_signal::write_signal(base, TEST_PH, TEST_POST_IID, &signal).unwrap();
    }

    #[test]
    fn acknowledged_signal_waits_until_started_at_barrier() {
        let base = isolated_base("future");
        write_ack(&base, "2099-01-01T00:00:00Z");
        let sm = Arc::new(RecordStateMachine::new());
        let pair_label = Arc::new(Mutex::new(String::new()));

        poll_record_signal_ack_with_base(&base, TEST_PH, TEST_POST_IID, 48_000, &sm, &pair_label);
        assert_eq!(sm.current(), RecordState::Watch);

        write_ack(&base, "2026-07-05T00:00:00Z");
        poll_record_signal_ack_with_base(&base, TEST_PH, TEST_POST_IID, 48_000, &sm, &pair_label);
        assert_eq!(sm.current(), RecordState::Record);
        assert_eq!(
            sm.record_started_at_ms(),
            parse_iso8601_to_epoch_ms("2026-07-05T00:00:00Z").unwrap()
        );
    }

    #[test]
    fn acknowledged_without_expected_metadata_enters_degraded_record_path() {
        let base = isolated_base("degraded");
        write_ack_with_expected(&base, "2026-07-05T00:00:00Z", None);
        let sm = Arc::new(RecordStateMachine::new());
        let pair_label = Arc::new(Mutex::new(String::new()));

        poll_record_signal_ack_with_base(&base, TEST_PH, TEST_POST_IID, 48_000, &sm, &pair_label);

        assert_eq!(sm.current(), RecordState::Record);
        assert_eq!(
            sm.record_started_at_ms(),
            parse_iso8601_to_epoch_ms("2026-07-05T00:00:00Z").unwrap()
        );
    }

    #[test]
    fn duplicate_post_state_machines_same_ack_only_one_enters_record() {
        let base = isolated_base("duplicate-post-entry");
        write_ack(&base, "2026-07-05T00:00:00Z");
        let first = Arc::new(RecordStateMachine::new());
        let second = Arc::new(RecordStateMachine::new());
        let pair_label = Arc::new(Mutex::new(String::new()));

        poll_record_signal_ack_with_base(
            &base,
            TEST_PH,
            TEST_POST_IID,
            48_000,
            &first,
            &pair_label,
        );
        poll_record_signal_ack_with_base(
            &base,
            TEST_PH,
            TEST_POST_IID,
            48_000,
            &second,
            &pair_label,
        );

        assert_eq!(first.current(), RecordState::Record);
        assert_eq!(
            second.current(),
            RecordState::Watch,
            "same session/POST instance must have only one cross-process Record entrant"
        );
    }
}

// ── Tests (ACK timeout / G-60-02) ────────────────────────────────────────
#[cfg(test)]
mod ack_timeout_tests {
    use super::*;
    use crate::record::RecordState;
    use crate::record_signal::{mark_acknowledged, write_pending};
    use std::sync::atomic::AtomicU64;

    const TEST_PH: &str = "ph";
    const TEST_POST_IID: &str = "post-iid";

    fn isolated_base(tag: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_ack_timeout_{pid}_{n}_{tag}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// ACK timeout is diagnostic only. PRE may still become ready after bounce starts, so timeout
    /// does not own Stop authority.
    #[test]
    fn pending_over_30s_keeps_record_armed() {
        let base = isolated_base("stale");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();
        let pair_label = Arc::new(Mutex::new("pair: deadbeef".to_string()));
        let paired_pre_target = Arc::new(Mutex::new(Some("pre-1".to_string())));

        write_pending(
            &base,
            TEST_PH,
            TEST_POST_IID,
            "pre-1".into(),
            "daw-1".into(),
        )
        .unwrap();
        crate::reservation::reserve_pairing(&base, TEST_PH, "pre-1", TEST_POST_IID).unwrap();
        assert_eq!(crate::reservation::count_frames(&base, TEST_PH), 1);
        let future_now = chrono::Utc::now() + chrono::Duration::seconds(31);

        poll_ack_timeout_with_base(
            &base,
            TEST_PH,
            TEST_POST_IID,
            &sm,
            &pair_label,
            &paired_pre_target,
            future_now,
        );

        let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
        assert_eq!(after.status, SignalStatus::Pending);
        assert_eq!(sm.current(), RecordState::Record);
        assert_eq!(
            pair_label.lock().unwrap().as_str(),
            "pair: deadbeef",
            "ACK timeout must not clear pair_label"
        );
        assert_eq!(
            paired_pre_target.lock().unwrap().as_deref(),
            Some("pre-1"),
            "ACK timeout must not reset paired_pre_target"
        );
        assert_eq!(
            crate::reservation::count_frames(&base, TEST_PH),
            1,
            "ACK timeout must not release the O_EXCL reservation frame"
        );
    }

    #[test]
    fn pending_within_30s_is_noop() {
        let base = isolated_base("fresh");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();
        let pair_label = Arc::new(Mutex::new("pair: deadbeef".to_string()));
        let paired_pre_target = Arc::new(Mutex::new(Some("pre-1".to_string())));

        write_pending(
            &base,
            TEST_PH,
            TEST_POST_IID,
            "pre-1".into(),
            "daw-1".into(),
        )
        .unwrap();
        poll_ack_timeout_with_base(
            &base,
            TEST_PH,
            TEST_POST_IID,
            &sm,
            &pair_label,
            &paired_pre_target,
            chrono::Utc::now(),
        );

        let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
        assert_eq!(after.status, SignalStatus::Pending);
        assert_eq!(sm.current(), RecordState::Record);
        assert_eq!(
            pair_label.lock().unwrap().as_str(),
            "pair: deadbeef",
            "G-115-64: within-window must NOT clear pair_label"
        );
        assert_eq!(
            paired_pre_target.lock().unwrap().as_deref(),
            Some("pre-1"),
            "G-115-64: within-window must NOT clear paired_pre_target"
        );
    }

    #[test]
    fn acknowledged_is_noop_even_over_30s() {
        let base = isolated_base("acked");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();
        let pair_label = Arc::new(Mutex::new("pair: deadbeef".to_string()));
        let paired_pre_target = Arc::new(Mutex::new(Some("pre-1".to_string())));

        write_pending(
            &base,
            TEST_PH,
            TEST_POST_IID,
            "pre-1".into(),
            "daw-1".into(),
        )
        .unwrap();
        mark_acknowledged(&base, TEST_PH, TEST_POST_IID).unwrap();

        let future_now = chrono::Utc::now() + chrono::Duration::seconds(300);
        poll_ack_timeout_with_base(
            &base,
            TEST_PH,
            TEST_POST_IID,
            &sm,
            &pair_label,
            &paired_pre_target,
            future_now,
        );

        let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
        assert_eq!(after.status, SignalStatus::Acknowledged);
        assert_eq!(sm.current(), RecordState::Record);
        assert_eq!(
            pair_label.lock().unwrap().as_str(),
            "pair: deadbeef",
            "G-115-64: Acknowledged must NOT clear pair_label"
        );
        assert_eq!(
            paired_pre_target.lock().unwrap().as_deref(),
            Some("pre-1"),
            "G-115-64: Acknowledged must NOT clear paired_pre_target"
        );
    }

    #[test]
    fn missing_signal_is_noop() {
        let base = isolated_base("missing");
        let sm = Arc::new(RecordStateMachine::new());
        let pair_label = Arc::new(Mutex::new(String::new()));
        let paired_pre_target = Arc::new(Mutex::new(None));

        poll_ack_timeout_with_base(
            &base,
            TEST_PH,
            TEST_POST_IID,
            &sm,
            &pair_label,
            &paired_pre_target,
            chrono::Utc::now(),
        );

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(pair_label.lock().unwrap().is_empty());
        assert!(paired_pre_target.lock().unwrap().is_none());
    }
}

// ── PostTmpJson deserialize テスト (B-027 段階 3-B α-7-1 / Step 4) ─────────────
#[cfg(test)]
mod post_tmp_json_tests {
    use super::*;

    /// Active 完全形 (`serialize_post_json` 出力) → PostTmpJson roundtrip。
    #[test]
    fn deserialize_active_full_roundtrip() {
        let result = MeasureResult {
            lufs_m: Some(-12.0),
            true_peak: Some(-0.5),
            crest: Some(10.0),
            psr: Some(7.0),
            ..Default::default()
        };
        let json = serialize_post_json(
            "post-iid-A",
            SignalState::Active,
            Some(SignalState::Active),
            &result,
            "PRE-Master",
            123.456,
        );
        let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

        assert_eq!(parsed.v, 2);
        assert_eq!(parsed.role, "POST");
        assert_eq!(parsed.instance_id, "post-iid-A");
        assert_eq!(parsed.signal_state, "active");
        assert_eq!(parsed.pre_signal_state.as_deref(), Some("active"));
        assert!(
            parsed.t.starts_with("20") && parsed.t.ends_with('Z'),
            "ISO 8601 t: {}",
            parsed.t
        );
        assert_eq!(parsed.pair_pre_name, "PRE-Master");
        assert!((parsed.pair_claimed_at - 123.456).abs() < 1e-6);
        assert_eq!(parsed.lufs_m, Some(-12.0));
        assert_eq!(parsed.true_peak, Some(-0.5));
        assert_eq!(parsed.crest, Some(10.0));
        assert_eq!(parsed.psr, Some(7.0));
        assert!(parsed.n_prime_total.is_none());
        assert!(parsed.sharpness.is_none());
        assert!(parsed.psb_summary.is_none());
    }

    /// B-131 (G-115-380): `"` / `\` を含む pair_pre_name が serde escape され valid JSON に
    /// なり値が往復する（旧手組み生補間では不正 JSON を生成し、他 POST の
    /// `scan_post_candidates_in` が parse 失敗で無言 skip → pairing 消失していた回帰）。
    /// PRE 側 `serialize_pre_json_escapes_quotes_and_backslash` (B-077) と対称。
    #[test]
    fn serialize_post_json_escapes_quotes_and_backslash() {
        let result = MeasureResult::default();
        let name = "PRE\"x\\y";
        let json = serialize_post_json(
            "post-iid",
            SignalState::Active,
            Some(SignalState::Active),
            &result,
            name,
            1.0,
        );
        let parsed: serde_json::Value = serde_json::from_str(&json)
            .expect("B-131: special-char pair_pre_name must produce valid JSON");
        assert_eq!(parsed["pair_pre_name"].as_str(), Some(name));
    }

    /// B-131 (G-115-380): minimal でも `"` / `\` を含む pair_pre_name を serde escape する
    /// (`serialize_post_json` と対称 / Bypassed・Inactive でも候補化されるため必須)。
    #[test]
    fn serialize_post_json_minimal_escapes_quotes_and_backslash() {
        let name = "PRE\"x\\y";
        let json = serialize_post_json_minimal("post-iid", SignalState::Bypassed, name, 0.0);
        let parsed: serde_json::Value = serde_json::from_str(&json)
            .expect("B-131: special-char pair_pre_name must produce valid JSON (minimal)");
        assert_eq!(parsed["pair_pre_name"].as_str(), Some(name));
    }

    /// B-131 (G-115-380): 日本語 pair_pre_name が JSON で保持され読み戻せる
    /// (PRE 側 `serialize_pre_json_keeps_japanese_name` と対称 / serde が UTF-8 を維持)。
    #[test]
    fn serialize_post_json_keeps_japanese_pair_pre_name() {
        let result = MeasureResult::default();
        let name = "日本語PRE";
        let json = serialize_post_json(
            "post-iid",
            SignalState::Active,
            Some(SignalState::Active),
            &result,
            name,
            0.0,
        );
        let parsed: serde_json::Value = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed["pair_pre_name"].as_str(), Some(name));
    }

    /// B-131 (G-115-380) census-twin: instance_id も serde escape される。restore で `"` を含む
    /// instance_id が materialize wall（is_path_safe_component は `"` を拒否しない）を素通っても
    /// valid JSON になり、他 POST の scan が parse 失敗 → pairing 消失する同種 R-28 を防ぐ。
    #[test]
    fn serialize_post_json_escapes_instance_id_quote() {
        let result = MeasureResult::default();
        let iid = "post\"evil\\id";
        let json = serialize_post_json(
            iid,
            SignalState::Active,
            Some(SignalState::Active),
            &result,
            "PRE-Master",
            0.0,
        );
        let parsed: serde_json::Value = serde_json::from_str(&json)
            .expect("B-131: special-char instance_id must produce valid JSON");
        assert_eq!(parsed["instance_id"].as_str(), Some(iid));
    }

    /// B-131 (G-115-380) census-twin: minimal でも instance_id を serde escape する。
    #[test]
    fn serialize_post_json_minimal_escapes_instance_id_quote() {
        let iid = "post\"evil\\id";
        let json = serialize_post_json_minimal(iid, SignalState::Bypassed, "PRE-Mix", 0.0);
        let parsed: serde_json::Value = serde_json::from_str(&json)
            .expect("B-131: special-char instance_id must produce valid JSON (minimal)");
        assert_eq!(parsed["instance_id"].as_str(), Some(iid));
    }

    /// Minimal (`serialize_post_json_minimal` 出力 / Bypassed) → PostTmpJson roundtrip。
    /// pre_signal_state / 計測値系は不在 → Option::None で defaulted。
    #[test]
    fn deserialize_minimal_roundtrip() {
        let json =
            serialize_post_json_minimal("post-iid-B", SignalState::Bypassed, "PRE-Mix", 99.5);
        let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

        assert_eq!(parsed.instance_id, "post-iid-B");
        assert_eq!(parsed.signal_state, "bypassed");
        assert_eq!(parsed.pair_pre_name, "PRE-Mix");
        assert!(parsed.pre_signal_state.is_none());
        assert!(parsed.lufs_m.is_none());
        assert!(parsed.true_peak.is_none());
        assert!(parsed.crest.is_none());
        assert!(parsed.psr.is_none());
    }

    /// 旧 schema 互換: `pair_pre_name` field 不在 → 空文字 fallback (#[serde(default)])。
    #[test]
    fn deserialize_legacy_without_pair_pre_name_defaults_empty() {
        let legacy = r#"{"v":2,"role":"POST","instance_id":"old-iid","signal_state":"active","pre_signal_state":"active","t":"2026-05-04T10:00:00.000Z","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
        let parsed: PostTmpJson = serde_json::from_str(legacy).expect("legacy deserialize ok");

        assert_eq!(parsed.instance_id, "old-iid");
        assert_eq!(parsed.signal_state, "active");
        assert_eq!(
            parsed.pair_pre_name, "",
            "pair_pre_name must default to empty for legacy schema"
        );
        assert_eq!(parsed.lufs_m, Some(-14.0));
    }

    /// pair_pre_name が空文字で書込まれた場合の roundtrip (Active の POST が PRE 未選択)。
    #[test]
    fn deserialize_active_with_empty_pair_pre_name() {
        let result = MeasureResult::default();
        let json = serialize_post_json(
            "post-iid-C",
            SignalState::Active,
            Some(SignalState::Active),
            &result,
            "",
            0.0,
        );
        let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

        assert_eq!(parsed.pair_pre_name, "");
        assert_eq!(parsed.pair_claimed_at, 0.0);
        assert_eq!(parsed.signal_state, "active");
        assert!(parsed.lufs_m.is_none());
    }

    #[test]
    fn deserialize_active_with_phase_d_fields() {
        let result = MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-0.3),
            crest: Some(11.0),
            psr: Some(7.5),
            n_prime_total: Some(0.42),
            sharpness: Some(1.85),
            psb_summary: Some(crate::PsbSummary {
                low: 0.10,
                mid: 0.20,
                high: 0.30,
            }),
            ..Default::default()
        };
        let json = serialize_post_json(
            "post-iid-D",
            SignalState::Active,
            Some(SignalState::Active),
            &result,
            "PRE-D",
            555.0,
        );
        let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

        assert_eq!(parsed.pair_pre_name, "PRE-D");
        assert_eq!(parsed.n_prime_total, Some(0.42));
        assert_eq!(parsed.sharpness, Some(1.85));
        let psb = parsed.psb_summary.expect("psb_summary present");
        assert!((psb.low - 0.10).abs() < 1e-6);
        assert!((psb.mid - 0.20).abs() < 1e-6);
        assert!((psb.high - 0.30).abs() < 1e-6);
    }

    /// signal_state や instance_id 等 必須 field 不在 → deserialize エラー。
    #[test]
    fn deserialize_missing_required_field_errors() {
        let bad = r#"{"v":2,"role":"POST","signal_state":"active","t":"2026-05-04T10:00:00.000Z"}"#;
        let res: Result<PostTmpJson, _> = serde_json::from_str(bad);
        assert!(res.is_err(), "instance_id 不在は err");
    }

    // ── W-281 / G-115-249 / A-5: pair_claimed_at schema 拡張 テスト ─────────

    /// (A-5 i) serialize 出力 (full + minimal) に "pair_claimed_at" リテラル含有。
    #[test]
    fn post_json_serialize_includes_pair_claimed_at() {
        let json_full = serialize_post_json(
            "post-iid",
            SignalState::Active,
            Some(SignalState::Active),
            &MeasureResult::default(),
            "PRE-X",
            42.0,
        );
        assert!(
            json_full.contains(r#""pair_claimed_at":42"#),
            "full: {}",
            json_full
        );

        let json_min = serialize_post_json_minimal("post-iid", SignalState::Bypassed, "PRE-X", 0.0);
        assert!(
            json_min.contains(r#""pair_claimed_at":0"#),
            "min: {}",
            json_min
        );
    }

    #[test]
    fn post_json_serialize_includes_daw_session_id() {
        let json_full = serialize_post_json_with_daw(
            "post-iid",
            SignalState::Active,
            Some(SignalState::Active),
            &MeasureResult::default(),
            "PRE-X",
            42.0,
            "daw-A",
        );
        assert!(json_full.contains(r#""daw_session_id":"daw-A""#));
        assert!(json_full.contains(&format!(
            r#""host_process_id":{}"#,
            crate::current_host_process_id()
        )));

        let json_min = serialize_post_json_minimal_with_daw(
            "post-iid",
            SignalState::Bypassed,
            "PRE-X",
            0.0,
            "daw-A",
        );
        assert!(json_min.contains(r#""daw_session_id":"daw-A""#));
        assert!(json_min.contains(&format!(
            r#""host_process_id":{}"#,
            crate::current_host_process_id()
        )));
    }

    /// (A-5 ii) 旧 schema (pair_claimed_at field 不在) deserialize → default=0.0。
    #[test]
    fn post_json_deserialize_legacy_without_pair_claimed_at() {
        let legacy = r#"{"v":2,"role":"POST","instance_id":"legacy-iid","signal_state":"active","pre_signal_state":"active","t":"2026-05-04T10:00:00.000Z","pair_pre_name":"PRE-Legacy","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
        let parsed: PostTmpJson = serde_json::from_str(legacy).expect("legacy deserialize ok");

        assert_eq!(parsed.pair_pre_name, "PRE-Legacy");
        assert_eq!(
            parsed.pair_claimed_at, 0.0,
            "pair_claimed_at must default to 0.0 for legacy schema"
        );
        assert_eq!(
            parsed.daw_session_id, "",
            "daw_session_id must default to empty for legacy schema"
        );
        assert_eq!(
            parsed.host_process_id, 0,
            "host_process_id must default to 0 for legacy schema"
        );
    }
}

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
mod snapshot_pair_pre_name_tests {
    use super::*;

    /// 通常ケース: 設定値が snapshot として返る。
    #[test]
    fn normal_value_returned() {
        let arc = Arc::new(RwLock::new(String::from("PRE-Master")));
        let snap = snapshot_pair_pre_name(&arc);
        assert_eq!(snap, "PRE-Master");
    }

    /// 空文字 (default 状態) → 空文字 snapshot。
    #[test]
    fn empty_string_returned_as_empty() {
        let arc = Arc::new(RwLock::new(String::new()));
        let snap = snapshot_pair_pre_name(&arc);
        assert_eq!(snap, "");
    }

    /// poison error → 空文字 fallback (R-28 機能的沈黙 / 旧 schema 互換)。
    #[test]
    fn poisoned_lock_returns_empty_fallback() {
        let arc = Arc::new(RwLock::new(String::from("PRE-Should-Not-Be-Returned")));
        let arc_clone = Arc::clone(&arc);
        // 別 thread で write guard 保持中に panic → poison 状態化。
        let _ = std::thread::spawn(move || {
            let _guard = arc_clone.write().unwrap();
            panic!("intentional poison for test");
        })
        .join();
        // 上記 thread は join() で error を返すが poison 化は完了している。
        assert!(arc.is_poisoned(), "lock should be poisoned");
        let snap = snapshot_pair_pre_name(&arc);
        assert_eq!(snap, "", "poisoned lock must fall back to empty string");
    }
}

// ── Tests (B-024 Group A / Gap-2 PRE liveness / Gap-1/7/18 構造解消) ─────────
#[cfg(test)]
mod pre_liveness_tests {
    use super::*;
    use crate::record_signal::write_pending;
    use std::sync::atomic::AtomicU64;

    const TEST_PH: &str = "ph";
    const TEST_POST_IID: &str = "post-iid-liveness";
    const TEST_PRE_IID: &str = "pre-iid-liveness";
    const TEST_DAW_SESSION: &str = "daw-session-1";

    fn isolated_root(tag: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_pre_liveness_test_{pid}_{n}_{tag}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn write_dummy_pre_json(kirin_root: &Path, project_hash: &str, pre_iid: &str) -> PathBuf {
        let dir = kirin_root.join(project_hash).join(pre_iid);
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("pre.json");
        // 中身は空の JSON で OK (本テストは mtime のみ評価)。
        fs::write(&path, b"{}").unwrap();
        path
    }

    /// stem/offline export 後は PRE pre.json の mtime が止まり得る。
    /// stale 検出時も Keep は利用者の Stop まで保持する。
    #[test]
    fn poll_pre_liveness_at_stale_pre_keeps_recording_and_signal() {
        let kirin_root = isolated_root("stale_pre");
        let plugin_data_root = isolated_root("stale_pre_pdr");

        // PRE pre.json を書き込み (mtime = "現在時刻")。
        write_dummy_pre_json(&kirin_root, TEST_PH, TEST_PRE_IID);
        // POST 自身の record_signal を書き込み (status=Pending で OK)。
        write_pending(
            &plugin_data_root,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID.to_string(),
            TEST_DAW_SESSION.to_string(),
        )
        .unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();
        assert!(
            sm.is_recording(),
            "precondition: record_sm must be Recording"
        );
        // G-115-64: cleanup 後に空文字 / None になることを assert する前提として
        // 「設定済み」状態を入力にする (editor の Keep 経由 = trigger_keep_internal
        // が set_pair_label + paired_pre_target=Some を実施した後の状態を再現)。
        let pair_label = Arc::new(Mutex::new(format!("pair: {}", TEST_PRE_IID)));
        let paired_pre_target = Arc::new(Mutex::new(Some(TEST_PRE_IID.to_string())));

        // mtime + 100 秒先を `now` として注入 → 60 秒 threshold を超える。
        let stale_now = SystemTime::now() + Duration::from_secs(100);
        poll_pre_liveness_at(
            &kirin_root,
            &plugin_data_root,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID,
            &sm,
            &pair_label,
            &paired_pre_target,
            stale_now,
        );

        assert!(sm.is_recording(), "stale pre.json must keep Record armed");
        assert_eq!(
            pair_label.lock().unwrap().as_str(),
            format!("pair: {}", TEST_PRE_IID).as_str(),
            "stale pre.json must keep pair_label for manual Stop"
        );
        assert_eq!(
            paired_pre_target.lock().unwrap().as_deref(),
            Some(TEST_PRE_IID),
            "stale pre.json must keep paired_pre_target for manual Stop"
        );
        let signal_after =
            crate::record_signal::read_signal(&plugin_data_root, TEST_PH, TEST_POST_IID);
        assert!(
            signal_after.is_some(),
            "stale pre.json must not delete record_signal"
        );
    }

    /// pre.json 不在でも Keep は利用者の Stop まで保持する。
    #[test]
    fn poll_pre_liveness_at_missing_pre_json_keeps_recording() {
        let kirin_root = isolated_root("missing_pre");
        let plugin_data_root = isolated_root("missing_pre_pdr");

        // pre.json を一切作らない (PRE drop 後を再現)。
        write_pending(
            &plugin_data_root,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID.to_string(),
            TEST_DAW_SESSION.to_string(),
        )
        .unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();
        let pair_label = Arc::new(Mutex::new(format!("pair: {}", TEST_PRE_IID)));
        let paired_pre_target = Arc::new(Mutex::new(Some(TEST_PRE_IID.to_string())));

        poll_pre_liveness_at(
            &kirin_root,
            &plugin_data_root,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID,
            &sm,
            &pair_label,
            &paired_pre_target,
            SystemTime::now(),
        );

        assert!(sm.is_recording(), "missing pre.json must keep Record armed");
        assert_eq!(
            pair_label.lock().unwrap().as_str(),
            format!("pair: {}", TEST_PRE_IID).as_str(),
            "missing pre.json must keep pair_label for manual Stop"
        );
        assert_eq!(
            paired_pre_target.lock().unwrap().as_deref(),
            Some(TEST_PRE_IID),
            "missing pre.json must keep paired_pre_target for manual Stop"
        );
        let signal_after =
            crate::record_signal::read_signal(&plugin_data_root, TEST_PH, TEST_POST_IID);
        assert!(
            signal_after.is_some(),
            "missing pre.json must not delete record_signal"
        );
    }

    /// Gap-2 + G-115-64: pre.json mtime fresh (< 60s) → exit_record せず Record 維持.
    /// pair_label / paired_pre_target も保持される (cleanup は走らない).
    #[test]
    fn poll_pre_liveness_at_fresh_pre_keeps_recording() {
        let kirin_root = isolated_root("fresh_pre");
        let plugin_data_root = isolated_root("fresh_pre_pdr");

        write_dummy_pre_json(&kirin_root, TEST_PH, TEST_PRE_IID);
        write_pending(
            &plugin_data_root,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID.to_string(),
            TEST_DAW_SESSION.to_string(),
        )
        .unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();
        let pair_label = Arc::new(Mutex::new(format!("pair: {}", TEST_PRE_IID)));
        let paired_pre_target = Arc::new(Mutex::new(Some(TEST_PRE_IID.to_string())));

        // `now` をリアルな現在時刻にする → mtime ≈ now → 経過 ≈ 0 秒。
        poll_pre_liveness_at(
            &kirin_root,
            &plugin_data_root,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID,
            &sm,
            &pair_label,
            &paired_pre_target,
            SystemTime::now(),
        );

        assert!(
            sm.is_recording(),
            "fresh pre.json must NOT trigger exit_record()"
        );
        assert_eq!(
            pair_label.lock().unwrap().as_str(),
            format!("pair: {}", TEST_PRE_IID).as_str(),
            "G-115-64: fresh pre.json must NOT clear pair_label"
        );
        assert_eq!(
            paired_pre_target.lock().unwrap().as_deref(),
            Some(TEST_PRE_IID),
            "G-115-64: fresh pre.json must NOT clear paired_pre_target"
        );
        let signal_after =
            crate::record_signal::read_signal(&plugin_data_root, TEST_PH, TEST_POST_IID);
        assert!(
            signal_after.is_some(),
            "fresh pre.json must NOT delete signal"
        );
    }

    /// Gap-2: `record_sm` が Watch 状態のとき sub-tick は no-op。
    #[test]
    fn poll_pre_liveness_at_watch_state_is_noop() {
        let kirin_root = isolated_root("watch_state");
        let plugin_data_root = isolated_root("watch_state_pdr");

        write_pending(
            &plugin_data_root,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID.to_string(),
            TEST_DAW_SESSION.to_string(),
        )
        .unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        // try_enter_record せず Watch のまま。
        assert!(!sm.is_recording());

        let pair_label = Arc::new(Mutex::new(String::new()));
        let paired = Arc::new(Mutex::new(Some(TEST_PRE_IID.to_string())));

        // poll_pre_liveness (top-level) は record_sm guard 内で早期 return。
        poll_pre_liveness(
            &kirin_root,
            TEST_PH,
            TEST_POST_IID,
            &sm,
            &pair_label,
            &paired,
        );

        // Watch のまま signal も削除されない。
        let signal_after =
            crate::record_signal::read_signal(&plugin_data_root, TEST_PH, TEST_POST_IID);
        // Watch ガードで delete_signal を呼ばないため signal は残る (本 test は
        // production の StoragePaths を経由するので、本機ホームの plugin_data
        // を触らないことが重要だが、Watch ガードの早期 return でその経路にすら
        // 入らないことを is_recording=false で間接確証する)。
        assert!(!sm.is_recording(), "Watch state must remain unchanged");
        let _ = signal_after; // 環境依存ホームを避けるため値は assert しない
    }
}

#[cfg(test)]
mod pre_mtime_path_guard_tests {
    use super::*;

    /// B-128 reopen / G-115-376 gate ①: `find_pre_json_mtime` は他 instance content 由来の
    /// `pre_iid` (peer pre.json instance_id) を `.join()` する **唯一** の path builder。
    /// path-unsafe な peer instance_id (`..` traversal / 絶対 / 区切り / 制御文字 / overlength /
    /// `_q_` 詐称) では within-base wall が reject し、`.join()`→stat に到達せず base 外の
    /// pre.json の存在・mtime を **観測しない** (mtime オラクル封鎖)。同時に valid な peer
    /// instance_id は従来どおり `Some(mtime)` を返す (over-reject なし = 正常系の pairing 不変)。
    ///
    /// guard を外すと "../../SECRET" 経路が base/SECRET/pre.json を stat して `Some` を返すため
    /// 本 test は fail する (= guard 感度の確証)。
    #[test]
    fn find_pre_json_mtime_rejects_path_unsafe_pre_iid_no_external_stat() {
        let base =
            std::env::temp_dir().join(format!("kirin_b128_mtime_oracle_{}", std::process::id()));
        let _ = fs::remove_dir_all(&base);
        fs::create_dir_all(&base).unwrap();
        let kirin_root = base.join("kirin");
        // read_dir(kirin_root) が 1 件以上返すよう project subdir を作る (loop body 到達条件)。
        let project_dir = kirin_root.join("proj-uuid");
        fs::create_dir_all(&project_dir).unwrap();

        // (control / over-reject 封じ) valid peer instance_id 配下の正規 pre.json → Some(mtime)。
        let legit_iid = "pre-iid-legit";
        let legit_dir = project_dir.join(legit_iid);
        fs::create_dir_all(&legit_dir).unwrap();
        fs::write(legit_dir.join("pre.json"), b"{}").unwrap();
        assert!(
            find_pre_json_mtime(&kirin_root, legit_iid).is_some(),
            "valid peer instance_id は従来どおり pre.json mtime を返す (over-reject 不可)"
        );

        // base 外 (kirin_root の 2 つ上 = base) に SECRET/pre.json を置く。無 guard なら
        // pre_iid="../../SECRET" で project_dir/../../SECRET/pre.json = base/SECRET/pre.json を
        // stat し mtime を観測できてしまう (mtime オラクル)。
        let secret_dir = base.join("SECRET");
        fs::create_dir_all(&secret_dir).unwrap();
        fs::write(secret_dir.join("pre.json"), b"{}").unwrap();
        // traversal target が実在し project_dir から到達可能であることを test 自身で確認。
        assert!(
            project_dir
                .join("..")
                .join("..")
                .join("SECRET")
                .join("pre.json")
                .exists(),
            "precondition: traversal target base/SECRET/pre.json は project_dir から到達可能"
        );

        // 各攻撃ベクタ: guard reject → None (`.join()`→stat に到達せず base 外を観測しない)。
        let attacks = [
            "../../SECRET",   // traversal (base 外の実在 pre.json を狙う = mtime オラクル本体)
            "..",             // 親
            ".",              // 自身
            "/etc",           // 絶対パス
            "..\\..\\SECRET", // backslash 区切り
            "_q_deadbeef",    // quarantine prefix 詐称 (cap-bypass 封止と同基準)
            "evil\u{0}null",  // null byte / 制御文字
            "tab\tname",      // 制御文字 (tab)
            "",               // empty (既存 early-return None だが網羅)
        ];
        for a in attacks {
            assert_eq!(
                find_pre_json_mtime(&kirin_root, a),
                None,
                "path-unsafe pre_iid {a:?} は reject され base 外 stat に到達しない (mtime オラクル封鎖)"
            );
        }
        // overlength (MAX_COMPONENT_LEN 超) も reject (D4 と同基準)。
        let overlong = "a".repeat(crate::path_identity::MAX_COMPONENT_LEN + 1);
        assert_eq!(
            find_pre_json_mtime(&kirin_root, &overlong),
            None,
            "overlength pre_iid は reject (D4 と同基準)"
        );
    }
}

// ── W-282 / G-115-250: pair 解放時 Δ 表示完全リセット テスト (C-1 / C-2) ─────
#[cfg(test)]
mod release_delta_reset_tests {
    use super::*;

    /// (C-1) IO Thread C-4 release block の `*delta_result.lock() = DeltaResult::default()`
    /// パターンが last_active を None に / mode を NoPre に / Δ 6 field を None に
    /// する事を verify。spawn_io_thread_post 経由のフル統合は既存 timing flake test
    /// (test_pair_pre_name_arc_roundtrip_to_post_json) と同経路のため、本 unit test
    /// では A-1 の mutation pattern 単体を直接検証する (gui_wiring C-3 invariant 検査と
    /// 組合せで release 経路全体カバー)。
    #[test]
    fn release_clears_delta_result_when_self_check_releases() {
        // 解放前: last_active=Some / mode=Active / 6 field=Some (W-281 release 直前状態)。
        let delta = Arc::new(Mutex::new(DeltaResult {
            lufs: Some(-2.0),
            psr: Some(1.5),
            tp: Some(-0.5),
            n_prime_total: Some(0.1),
            crest: Some(3.0),
            sharpness: Some(0.8),
            mode: DeltaMode::Active,
            last_active: Some(DeltaSnapshot {
                lufs: Some(-2.0),
                psr: Some(1.5),
                tp: Some(-0.5),
                n_prime_total: Some(0.1),
                crest: Some(3.0),
                sharpness: Some(0.8),
            }),
        }));

        // A-1 release mutation (W-282 io_thread_post.rs C-4 release block と同一 pattern)。
        if let Ok(mut d) = delta.lock() {
            *d = DeltaResult::default();
        }

        // 解放後: last_active=None / mode=NoPre / 6 field=None。
        let r = delta.lock().unwrap();
        assert!(
            r.last_active.is_none(),
            "release must clear last_active (B-048 LKG bypass)"
        );
        assert_eq!(
            r.mode,
            DeltaMode::NoPre,
            "release must reset mode to NoPre (Default)"
        );
        assert!(r.lufs.is_none());
        assert!(r.psr.is_none());
        assert!(r.tp.is_none());
        assert!(r.n_prime_total.is_none());
        assert!(r.crest.is_none());
        assert!(r.sharpness.is_none());
    }

    /// (C-2) R-9 補足 1 検証: release 直後の同 tick run_tick 再走で
    /// `compute_delta_with_state` が NoPre を返した場合、`merge_last_active(prev=None,
    /// new=NoPre)` で last_active=None が維持される (= 復活しない)。
    #[test]
    fn merge_last_active_after_release_keeps_none() {
        // release 直後の状態: prev_last_active = None (A-1 で reset 済)。
        let prev_last_active: Option<DeltaSnapshot> = None;
        // 同 tick 再走の compute_delta_with_state が返す new_delta (instance 2+ 環境 /
        // pair filter で 0 件 → NoPre)。
        let new_delta = DeltaResult {
            mode: DeltaMode::NoPre,
            ..Default::default()
        };

        let merged = merge_last_active(prev_last_active, new_delta);

        // last_active=None 維持 / Active 経路を通っていないため復活しない。
        assert!(
            merged.last_active.is_none(),
            "merge with prev=None + NoPre must keep last_active=None"
        );
        assert_eq!(merged.mode, DeltaMode::NoPre);
    }
}

// ── B-059: resolve_delta_for_store（last_active 置換 / G-115-245 廃止）─────────
#[cfg(test)]
mod resolve_delta_for_store_tests {
    use super::*;

    fn snap(lufs: f64) -> DeltaSnapshot {
        DeltaSnapshot {
            lufs: Some(lufs),
            psr: None,
            tp: None,
            n_prime_total: None,
            crest: None,
            sharpness: None,
        }
    }

    /// B-059: NoPre（select_target_pre が None）→ **last_active クリア**（B-048 の保持を廃止）。
    /// 「見えないのに直近 Δ が凍結表示される」のを防ぐ＝表示=commit 一本化の核。
    #[test]
    fn nopre_clears_last_active() {
        let prev = Some(snap(1.0));
        let nopre = DeltaResult {
            mode: DeltaMode::NoPre,
            ..Default::default()
        };
        let r = resolve_delta_for_store(nopre, prev);
        assert_eq!(r.mode, DeltaMode::NoPre);
        assert!(
            r.last_active.is_none(),
            "NoPre は last_active をクリア（凍結 Δ を残さない）"
        );
    }

    /// Stale（一意有効 pair の 5-10s）→ 前回 last_active 保持（B-048 維持）。
    #[test]
    fn stale_keeps_last_active() {
        let prev = Some(snap(2.0));
        let stale = DeltaResult {
            mode: DeltaMode::Stale,
            ..Default::default()
        };
        let r = resolve_delta_for_store(stale, prev);
        assert!(
            r.last_active.is_some(),
            "Stale は同一有効 pair の last_active を保持"
        );
        assert_eq!(r.last_active.unwrap().lufs, Some(2.0));
    }

    /// Active → 新 snapshot 保存。
    #[test]
    fn active_stores_snapshot() {
        let active = DeltaResult {
            mode: DeltaMode::Active,
            lufs: Some(3.0),
            ..Default::default()
        };
        let r = resolve_delta_for_store(active, None);
        assert!(r.last_active.is_some(), "Active は新 snapshot を保存");
        assert_eq!(r.last_active.unwrap().lufs, Some(3.0));
    }
}

#[cfg(test)]
mod non_active_delta_store_tests {
    use super::*;

    #[test]
    fn inactive_with_pair_keeps_last_active_as_stale() {
        let previous = DeltaResult {
            mode: DeltaMode::Active,
            lufs: Some(1.0),
            tp: Some(2.0),
            crest: Some(3.0),
            last_active: Some(DeltaSnapshot {
                lufs: Some(1.0),
                psr: Some(4.0),
                tp: Some(2.0),
                n_prime_total: Some(5.0),
                crest: Some(3.0),
                sharpness: Some(6.0),
            }),
            ..Default::default()
        };

        let r = resolve_delta_for_non_active_post(SignalState::Inactive, "Drum", &previous);

        assert_eq!(r.mode, DeltaMode::Stale);
        let snap = r.last_active.expect("inactive pair must keep frozen delta");
        assert_eq!(snap.lufs, Some(1.0));
        assert_eq!(snap.tp, Some(2.0));
        assert_eq!(snap.crest, Some(3.0));
    }

    #[test]
    fn inactive_with_pair_snapshots_previous_core_values_if_needed() {
        let previous = DeltaResult {
            mode: DeltaMode::Active,
            lufs: Some(-0.5),
            tp: Some(0.2),
            crest: Some(1.5),
            last_active: None,
            ..Default::default()
        };

        let r = resolve_delta_for_non_active_post(SignalState::Inactive, "Music", &previous);

        assert_eq!(r.mode, DeltaMode::Stale);
        let snap = r.last_active.expect("core delta should be recoverable");
        assert_eq!(snap.lufs, Some(-0.5));
        assert_eq!(snap.tp, Some(0.2));
        assert_eq!(snap.crest, Some(1.5));
    }

    #[test]
    fn inactive_without_pair_clears_delta() {
        let previous = DeltaResult {
            mode: DeltaMode::Active,
            lufs: Some(1.0),
            last_active: Some(DeltaSnapshot {
                lufs: Some(1.0),
                ..Default::default()
            }),
            ..Default::default()
        };

        let r = resolve_delta_for_non_active_post(SignalState::Inactive, "", &previous);

        assert_eq!(r.mode, DeltaMode::NoPre);
        assert!(r.last_active.is_none());
    }

    #[test]
    fn bypassed_clears_even_when_pair_is_selected() {
        let previous = DeltaResult {
            mode: DeltaMode::Active,
            lufs: Some(1.0),
            last_active: Some(DeltaSnapshot {
                lufs: Some(1.0),
                ..Default::default()
            }),
            ..Default::default()
        };

        let r = resolve_delta_for_non_active_post(SignalState::Bypassed, "Drum", &previous);

        assert_eq!(r.mode, DeltaMode::NoPre);
        assert!(r.last_active.is_none());
    }
}
