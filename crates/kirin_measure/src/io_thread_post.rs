//! IO Thread — POST 側（A-3 修正後）。
//!
//! 100ms ループで:
//! 1. `$TMPDIR/kirin/{project_hash}/*/pre.json` を全 instance_id 横断で走査
//! 2. 最新 `t` を持つ PRE を選択
//! 3. Δ = POST − PRE を算出、鮮度判定
//! 4. `$TMPDIR/kirin/{project_hash}/{self.instance_id}/post.json` にアトミック書込
//! 5. `Arc<Mutex<DeltaResult>>` を更新
//! 6. Record mode 時: `plugin_data/{project_hash}/{instance_id}/post/*.json` に
//!    Frame (10 fps) / PSB (2 fps) を追記、30 秒毎に flush
//!
//! - このスレッドが panic / 権限エラーで止まっても Audio Thread / Measure Thread は継続
//! - Drop 時に自分の post.json と instance ディレクトリを削除する
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
use crate::cleanup::exit_record_full;
use crate::delta::{DeltaMode, DeltaResult, DeltaSnapshot};
use crate::engine::SessionSummary;
use crate::pairing_scope::{read_pre_at, select_target_pre_for_arm_for_post_project, LatchedPre};
use crate::plugin_data::Role as PluginDataRole;
use crate::post_candidates::self_check_pair_claim;
#[cfg(test)]
use crate::post_candidates::{
    discover_active_post_dirs, enumerate_active_post_pair_candidates, scan_post_candidates_in,
    PostTmpJson,
};
use crate::pre_discovery::PostDiscoveryState;
#[cfg(test)]
use crate::pre_discovery::DISCOVERY_STALE_SECS;
use crate::record::RecordStateMachine;
use crate::record_signal::{self, SignalStatus, ACK_TIMEOUT_SECONDS, SIGNALS_SUBDIR};
use crate::record_writer::{
    run_record_tick_with_pair_names, take_session_summary, writer_close_degraded,
    writer_close_with_summary, RecordError, RecordingCtx,
};
use crate::storage::{PlatformPaths, StoragePaths};
use crate::{load_signal_state, MeasureResult, RecordTraceQueue, SignalState};

const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// PRE ファイルが Active とみなされる最大経過時間（秒）
const STALE_SECS: i64 = 5; // B-046: 2→5 (fs I/O backpressure 吸収 / G-115-246)

/// PRE ファイルが NoPre とみなされる最大経過時間（秒）
/// B-059: `pairing_scope::select_target_pre` の freshness gate でも単一ソースとして参照。
pub(crate) const NO_PRE_SECS: i64 = 10;

/// preset/ ポーリング間隔（サブ3-C-2: 1 秒）。
const PRESET_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// record_signal.json ACK タイムアウト監視間隔（G-60-02: 1 秒）。
const ACK_TIMEOUT_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// B-023 段階 4: pair_label 更新 polling 間隔。
/// PRE 側 ack 後 `paired_pre_name` 取得 → POST GUI へ反映する間隔。
/// 100ms tick で毎回 disk read は heavy なので 1 秒間隔（ack 検出遅延の体感差は無視可能）。
const PAIR_LABEL_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// B-027 段階 3-B α-7-4-C / Step 10: all_keep_signal broadcast polling 間隔。
/// 100ms tick で毎回 `scan_broadcasts_dir` (= read_dir + per-file read + parse) を回すと
/// disk I/O が嵩むため 1 秒 throttle (ack/preset/pair_label と同位相 / 体感差は無視可能)。
const ALL_KEEP_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// B-024 Group A / Gap-2: PRE 死活確認 sub-tick の間隔 (1 秒 / 既存 PRESET / ACK /
/// PAIR_LABEL / ALL_KEEP と同位相)。`record_sm.is_recording()` 中のみ動作し disk I/O は
/// 軽量 (`fs::metadata` 1 件 / project)。
const PRE_LIVENESS_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// 60 秒以上 mtime 更新が無い (or pre.json 不在) の場合、PRE crash / unload とみなし
/// POST が `delete_signal` + `exit_record` で構造的同期する (Daisuke 判断 α 採用 /
/// Gap-1 / Gap-7 / Gap-18 統合解)。
const PRE_LIVENESS_STALE_SECS: u64 = 60;

/// B-027 段階 3-B α-7-4-D / Step 11: IO Thread broadcast 受信時に発火する trigger
/// closure 型。引数 `(originator_iid, started_at)`。crate 構造制約 (kirin_measure →
/// hypha_post 逆依存不可) を回避するため、closure 構築は呼出側 (hypha_post::lib.rs)
/// で完結し本 crate は Arc<dyn Fn> として受領するのみ。`clippy::type_complexity`
/// (rust-clippy 1.94) を type alias で抑制。
pub type TriggerPairResolutionFn = Arc<dyn Fn(&str, &str) + Send + Sync>;

/// α-7' All Stop: broadcast 受信時に発火する Stop trigger closure 型。
/// `TriggerPairResolutionFn` と同シグネチャ (`(originator_iid, started_at)`) で
/// hypha_post::editor::trigger_stop_internal を toast=None で呼出す。
pub type TriggerStopResolutionFn = Arc<dyn Fn(&str, &str) + Send + Sync>;

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
/// - `preset_available`   : 1 秒ごとに preset/ を ls して更新
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
    preset_available: Arc<AtomicBool>,
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
    // B-025 Group B-2/B-3 / Gap-19/20: io_thread → GUI ステータス行への通知 channel。
    // None = 通常 / Some = "Record stopped: ..." 文言。`run_record_tick` が
    // `RecordingCtx::exit_requested` に sentinel をセットしたら本 io_thread が
    // `record_sm.exit_record()` 後に書き込む。editor 側は次描画で表示する。
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
        // restore 由来の path-unsafe な project_hash / instance_id / daw_session_id セルを正規化し、
        // path-unsafe なら fresh new_v4 へ差し替える（§7② 観測継続 / daw は drift 防止で同経路）。
        // path-safe な値は無改変＝parity の literal id path テスト不変。下流 builder wall が DiD backstop。
        crate::path_identity::normalize_observation_cell(
            &instance_id,
            "io_thread_post.instance_id",
        );
        crate::path_identity::normalize_observation_cell(
            &project_hash,
            "io_thread_post.project_hash",
        );
        crate::path_identity::normalize_observation_cell(
            &daw_session_id,
            "io_thread_post.daw_session_id",
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

        // B-025 Group B-1 / Gap-8: 30 秒 flush 周期中の DAW crash で残った
        // `*.json.tmp` を整合 verify (HMAC-SHA256) → `.json` に atomic rename で救出。
        // 不整合 .tmp は warn ログのみで残置 (削除しない / 約束 5 原則)。loop 前 1 回。
        if let Ok(paths) = StoragePaths::default_platform() {
            let _ = crate::record_writer::recover_orphan_tmps(&paths.plugin_data_dir());
        }

        // B-103: dead Pending record_signal を起動時掃除（PRE 経路と冪等・age ベース保守条件）。
        record_signal::sweep_stale_pending_at_startup();

        // B-127 (G-115-364): 孤児 reservation 枠（keep が active marker 生成前にクラッシュ等 /
        // age > RESERVATION_TTL_SECS）を起動時掃除（B-103 と合流・age ベース・冪等）。
        if let Ok(paths) = StoragePaths::default_platform() {
            let _ = crate::reservation::sweep_stale_reservations_in(
                &paths.plugin_data_dir(),
                chrono::Utc::now(),
            );
        }

        // B-026 / Gap-9: crash 残骸 `pre/{compact}.json` / `post/{compact}.json`
        // のうち status=Active かつ mtime > 60s のファイルを status=Closed に
        // 書換 (Lens 側「進行中 Record」誤認の構造的解消)。loop 前 1 回。
        if let Ok(paths) = StoragePaths::default_platform() {
            let _ = crate::record_writer::sweep_stale_active_at_startup(&paths.plugin_data_dir());
        }

        // B-027 段階 3-B α-7-1 / Step 6: 引数を closure scope に capture。
        // Step 11 で `license_for_thread` は撤去 (closure 経由案 / 呼出側 lib.rs で
        // `trigger_pair_resolution` closure に直接 capture / 申し送り #31 遅延約束追跡完了)。
        // - `daw_session_id_arc` (§4-5 Step 1 Arc 化): Step 10 で all_keep sub-tick
        //   (cross-process 防壁 = `broadcast.daw_session_id != snapshot` skip) で実 use 中。
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

        let mut recording: Option<RecordingCtx> = None;
        let mut last_preset_count: Option<usize> = None;
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
        let mut discovery = PostDiscoveryState::new();

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

            // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name snapshot per tick。
            // RwLock read guard 寿命を tick 内に閉じる (closure スコープから外で
            // guard を保持しない)。poison error 時は空文字 fallback (旧 schema 互換)。
            let pair_pre_name_snapshot = snapshot_pair_pre_name(&pair_pre_name_for_thread);
            // W-281: pair_claimed_at snapshot per tick (同位相 / poison は 0.0 fallback)。
            let pair_claimed_at_snapshot =
                pair_claimed_at_for_thread.read().map(|g| *g).unwrap_or(0.0);

            // W-281 / C-3: 1 sec interval で後着優先 self check を発火。
            // pair_pre_name が空 or 自身が唯一の claim 者 → release 不要。
            // 解放対象 → C-4 経路で pair_pre_name="" / pair_claimed_at=0.0 + Toast 通知。
            // W-284 / G-115-252: Record 中は self_check を skip。Record 中に self_check
            // が release を発火すると pair_pre_name="" + delta_result clear (W-282)
            // + pair_label 切替で Record 継続が破綻する (Daisuke 2026-05-17 報告)。
            // Record 開始時点で確定した pair は Stop まで保持する仕様。Watch 中のみ
            // 後着優先 release を許容する。
            let tick_now = Instant::now();
            if !pair_pre_name_snapshot.is_empty()
                && !record_sm.is_recording()
                && tick_now.duration_since(last_self_check_at) >= Duration::from_secs(1)
            {
                last_self_check_at = tick_now;
                if self_check_pair_claim(
                    &project_dir_hint,
                    instance_id_ref,
                    &pair_pre_name_snapshot,
                    pair_claimed_at_snapshot,
                ) {
                    // C-4: 自身の claim を解放。
                    log::info!(
                        "[POST self_check] release pair: instance_id={} pair_pre_name={} (newer claim detected)",
                        instance_id_ref,
                        pair_pre_name_snapshot
                    );
                    if let Ok(mut g) = pair_pre_name_for_thread.write() {
                        g.clear();
                    }
                    if let Ok(mut c) = pair_claimed_at_for_thread.write() {
                        *c = 0.0;
                    }
                    if let Ok(mut n) = pair_release_notice_for_thread.write() {
                        *n = Some("Released (paired elsewhere)".to_string());
                    }
                    // W-282 / G-115-250 / A-1: Δ 表示完全リセット。
                    // B-048 LKG (`last_active=Some(snap)`) を bypass し、解放された POST に
                    // 古い Δ 値が `draw_delta_grid_frozen` で凍結保持されるのを防ぐ。
                    // 次 GUI frame で `draw_watch_absolute_grid` (絶対値 fallback) に切替。
                    // 同 tick 内 run_tick 再走時は instance 2+ 環境のみで release 発火するため
                    // compute_delta_with_state は NoPre を返し、merge_last_active(prev=None,
                    // new=NoPre) で last_active=None が維持される。
                    if let Ok(mut d) = delta_result.lock() {
                        *d = DeltaResult::default();
                    }
                }
            }

            // C-4 直後 / 通常 tick: 解放後の最新値を再 snapshot して run_tick へ渡す
            // (1 tick 内に解放 → 書込まで完結 / 次 tick 待ちでの一過性矛盾を排除)。
            let pair_pre_name_snapshot = snapshot_pair_pre_name(&pair_pre_name_for_thread);
            let pair_claimed_at_snapshot =
                pair_claimed_at_for_thread.read().map(|g| *g).unwrap_or(0.0);

            match run_tick(
                &project_dir_hint,
                &kirin_root,
                &mut discovery,
                &instance_dir,
                &post_file,
                instance_id_ref,
                &post_result,
                &delta_result,
                &signal_state,
                &pair_pre_name_snapshot,
                pair_claimed_at_snapshot,
                project_hash_ref,
                // B-108: Record 中はラッチ凍結（W-284 self_check-skip と同型）。latched は共有実体。
                record_sm.is_recording(),
                &latched_pre,
            ) {
                Ok(()) => {}
                Err(e) => log::warn!("[IOThread POST] tick error: {}", e),
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
            if let Err(e) = run_record_tick_with_pair_names(
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
                &post_result,
                &mut recording,
                Some(&session_summary),
                &overflow,       // B-076: per-Record dropped_samples 算出用
                &oversized_drop, // B-125: per-Record oversized block drop 算出用
                Some(&record_trace_queue),
            ) {
                log::warn!("[writer] tick error: {}", e);
            }

            // run_record_tick が連続失敗閾値を検知して `exit_requested` に sentinel
            // をセットしたら、本 tick で `handle_exit_reason` 経由で
            // writer_close + exit_record_full + UI 通知文字列書込を 1 単位で行う。
            // 1 record session 内で 1 回のみ発火 (`Option::take` で消費)。
            let exit_reason = recording.as_mut().and_then(|ctx| ctx.exit_requested.take());
            if let Some(reason) = exit_reason {
                let plugin_data_root = StoragePaths::default_platform()
                    .ok()
                    .map(|paths| paths.plugin_data_dir());
                handle_exit_reason(
                    reason,
                    &mut recording,
                    plugin_data_root.as_deref(),
                    project_hash_ref,
                    &record_sm,
                    &pair_label,
                    &paired_pre_target,
                    &record_error_message,
                    instance_id_ref,
                );
            }

            if Instant::now() >= next_preset_poll {
                poll_preset_availability(
                    project_hash_ref,
                    &preset_available,
                    &mut last_preset_count,
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
                poll_record_signal_ack(project_hash_ref, instance_id_ref, &record_sm, &pair_label);
                next_pair_label_poll = Instant::now() + PAIR_LABEL_POLL_INTERVAL;
            }

            // B-024 Group A / Gap-2: PRE 死活確認 sub-tick (1 秒 throttle)。
            // Record 中でも stem/offline export 後は PRE pre.json の mtime が止まるため、
            // ここで Record を自動解除しない。Keep は利用者の Stop 操作まで保持する。
            if Instant::now() >= next_pre_liveness_poll {
                poll_pre_liveness(
                    &kirin_root,
                    project_hash_ref,
                    instance_id_ref,
                    &record_sm,
                    &pair_label,
                    &paired_pre_target,
                );
                next_pre_liveness_poll = Instant::now() + PRE_LIVENESS_POLL_INTERVAL;
            }

            // α-7' All Stop: Stop broadcast 受信 sub-tick (Keep より先に処理 / Stop 優先)。
            // 1 秒 throttle で `plugin_data/{ph}/all_stop_signal/*.json` を全件 scan し、
            // 新 broadcast を `processed_stop_broadcasts` cache に登録 + `trigger_stop_resolution`
            // closure 発火。Keep と並列の同型ロジック (cross-process filter / self skip /
            // 既処理 skip / stale fallback / GC)。
            if Instant::now() >= next_all_keep_poll {
                if let Ok(paths) = StoragePaths::default_platform() {
                    let base_dir = paths.plugin_data_dir();
                    let now_chrono = chrono::Utc::now();
                    let daw_session_id_snapshot = read_daw_session_id_arc(&daw_session_id_arc);
                    let stop_broadcasts =
                        all_stop_signal::scan_stop_broadcasts_dir(&base_dir, project_hash_ref);
                    for (originator_iid, broadcast) in stop_broadcasts {
                        if broadcast.daw_session_id != daw_session_id_snapshot {
                            continue;
                        }
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
            // 1 秒 throttle で `plugin_data/{ph}/all_keep_signal/*.json` を全件 scan し、
            // 新 broadcast を `processed_broadcasts` cache に登録する (検出 + cache + log
            // のみ / `trigger_keep_internal` 発火は Step 11 で本箇所に追加予定)。
            //
            //  1. cross-process 防壁: `broadcast.daw_session_id != self_daw_session_id` skip
            //  2. self skip: `originator_iid == self_instance_id` skip (#16 (iii))
            //  3. 既処理 skip: cache 内の `started_at` と一致 → 同 broadcast 既処理 skip
            //     (clock-skew 完全耐性 / Q-A8-6)
            //  4. stale fallback: `is_broadcast_stale` (≥30 秒経過) → cache 登録のみ +
            //  5. 新 broadcast: cache 更新 + log::info! (Step 11 で trigger_keep_internal 発火)
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
                    let broadcasts =
                        all_keep_signal::scan_broadcasts_dir(&base_dir, project_hash_ref);
                    for (originator_iid, broadcast) in broadcasts {
                        // 1. cross-process 防壁
                        if broadcast.daw_session_id != daw_session_id_snapshot {
                            continue;
                        }
                        // 2. self skip
                        if originator_iid == instance_id_ref {
                            continue;
                        }
                        // 3. 既処理 skip
                        if let Some((cached_started_at, _)) =
                            processed_broadcasts.get(&originator_iid)
                        {
                            if cached_started_at == &broadcast.started_at {
                                continue;
                            }
                        }
                        // 4. stale fallback
                        if all_keep_signal::is_broadcast_stale(
                            &broadcast,
                            now_chrono,
                            ALL_KEEP_BROADCAST_STALE_SECS,
                        ) {
                            processed_broadcasts.insert(
                                originator_iid.clone(),
                                (broadcast.started_at.clone(), Instant::now()),
                            );
                            log::debug!(
                                "[all_keep] stale broadcast cached without fire: originator={}, started_at={}",
                                originator_iid,
                                broadcast.started_at
                            );
                            continue;
                        }
                        // 5. 新 broadcast 検出 → cache 更新 + closure 経由 trigger_keep_internal
                        //    発火 (Step 11 / closure 経由案 / 呼出側 lib.rs で構築済 / 引数 2 件
                        //    = (originator_iid, started_at) / closure 内部で toast=None / now=0.0 /
                        //    Arc 共有資源 9 件は呼出側で move-capture 済 / Q-11-D 案 (a))。
                        processed_broadcasts.insert(
                            originator_iid.clone(),
                            (broadcast.started_at.clone(), Instant::now()),
                        );
                        let scan_dir = all_keep_signal::signals_dir(&base_dir, project_hash_ref);
                        log::info!(
                            "[all_keep] new broadcast detected: originator={} started_at={} scan_dir={}",
                            originator_iid,
                            broadcast.started_at,
                            scan_dir.display()
                        );
                        (trigger_pair_resolution)(&originator_iid, &broadcast.started_at);
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
            if !sealed {
                ctx.writer.mark_integrity_degraded();
            }
            writer_close_with_summary(ctx, summary);
        }

        // §4-5 Step 1: 終了処理時も project_hash を lazy-read で確定。
        let final_iid = read_instance_id_arc(&instance_id);
        let final_project_hash = read_project_hash_arc(&project_hash);
        // B-128 (G-115-370): within-base wall（POST post.json cleanup builder / DiD parity）。
        let final_ph_guard = crate::path_identity::guard_path_component(
            &final_project_hash,
            "io_thread_post.cleanup.project_hash",
        );
        let final_iid_guard = crate::path_identity::guard_path_component(
            &final_iid,
            "io_thread_post.cleanup.instance_id",
        );
        let final_project_dir_hint = kirin_root.join(&*final_ph_guard);
        let final_instance_dir = final_project_dir_hint.join(&*final_iid_guard);
        let final_post_file = final_instance_dir.join("post.json");
        if let Err(e) = fs::remove_file(&final_post_file) {
            log::debug!("[IOThread POST] cleanup post file: {}", e);
        }
        if let Err(e) = crate::atomic_file::remove_temp_siblings(&final_post_file) {
            log::debug!("[IOThread POST] cleanup post tmp: {}", e);
        }
        let _ = fs::remove_dir(&final_instance_dir);

        // B-027 段階 3-B α-7 / Group 2 統合点 #4 (Gap-6 局所対処):
        // IO Thread terminate 終端で self_post_iid の record_signal/{POST_iid}.json
        // を削除する。POST 自身が writer = cleanup 責任を持つ (cdylib 越境通信
        // 媒体の lifecycle 管理原則 / 設計判断 #5)。
        //
        // 経路: shutdown=true で loop 抜けた直後 / watchdog restart 時にも発火。
        // Watchdog restart シナリオでは新 IO Thread spawn 直前にこの delete が
        // 走るため、新 thread は新たな record_signal 書込が発生するまで対象 file
        // は不在 (= clean state)。Watchdog による自動回復は IO Thread panic 後
        // の T-8 経路のため、通常運用では発生しない。
        //
        // 失敗時 warn のみ (設計判断 #8): IO Thread terminate 内 panic は thread
        // crash の連鎖のため避ける。delete_signal は冪等 (NotFound→Ok /
        // record_signal.rs:289-301) で重複呼出 (統合点 #2/#3) と同居安全。
        // B-027 段階 3-B α-7-4-D / Step 12-C: Ok(paths) arm を block 化 (single match arm
        // → sequential block) し、delete_signal の直後に delete_broadcast 呼出を追加する
        // 利用 (二重 resolve 回避)。機能的差異なし / panic 回避規範 (warn のみ) は両者で同等。
        match StoragePaths::default_platform() {
            Ok(paths) => {
                release_record_reservation(
                    &paths.plugin_data_dir(),
                    &final_project_hash,
                    &final_iid,
                    &paired_pre_target,
                    "cleanup #4",
                );
                match record_signal::delete_signal(
                    &paths.plugin_data_dir(),
                    &final_project_hash,
                    &final_iid,
                ) {
                    Ok(()) => log::info!("[POST cleanup #4] record_signal deleted: {}", final_iid),
                    Err(e) => log::warn!("[POST cleanup #4] delete_signal failed: {:?}", e),
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

/// `daw_session_id` の現在値を取得（panic-safe）。
///
/// §4-5 Step 5 (instance scope divergence 是正):
/// 引数 `_arc` は構造維持のため受け取るが内部では使わず、`crate::daw_session_id()`
/// 経由で **process scope cell** を直読みする。`HyphaPost.daw_session_id` Arc field
/// は initialize() 時点の cell 値を凍結するため、複数 plugin instance 環境では
/// 後発 instance の `set_daw_session_id` 上書きを反映できず、6 POST で daw_session_id
/// が divergence していた (Hpha0504 / sub-tick cross-process filter で全件 skip)。
///
/// `daw_session_id()` cell は process scope (`lib.rs:145-148 daw_session_id_cell` の
/// static OnceLock) のため全 plugin instance で同一値を返し、broadcast filter
/// (`broadcast.daw_session_id != snapshot`) が POST 同士で正しくマッチする。
///
/// callsite 引数の Arc 構造は維持 (sub-tick `daw_session_id_arc` 不変 /
/// Pass 15 最小スコープ)。`§4-5 Step 1` の Arc 化はそのまま残し、本関数のみ
/// 「Arc 凍結値を見ない」semantics に切替える 1 関数 body 修正。
pub(crate) fn read_daw_session_id_arc(_arc: &Arc<RwLock<String>>) -> String {
    crate::daw_session_id()
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

/// 1 ループの処理本体。
///
/// # B-021 Phase 1A: filesystem-discovery の優先順位
///
/// `kirin_root` (= `$TMPDIR/kirin/`) を `discovery` 経由で 1 秒に 1 回 scan し、
/// active な PRE が居る `{project_uuid}/` dir を採用する。検出できない場合のみ
/// `project_dir_hint` (POST 自身の project_uuid 由来) にフォールバック。
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
/// - Record 中はラッチ凍結（アンラッチ/再選定しない / W-284 self_check-skip と同型）。真の PRE 実消滅は
///   `poll_pre_liveness`(60s) が `exit_record_full` で処理（G-115-56/64 不変）。
/// - Watch 中: pair 名変更/クリアで即アンラッチ。ラッチ先 pre.json を直読し、実消滅（不在/stale>TTL/
///   rename）でアンラッチ → 同 tick で名前が残れば Arm ゲート（B-104）で再ラッチ（DAW 再開で自動復元）。
///   未ラッチ時は Arm ゲートで初回解決（成立でラッチ）。同名2台目が現れてもラッチ済みなら再選定しない。
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
        post,
        pair_opt,
        recording,
        latched,
    )
}

#[allow(clippy::too_many_arguments)]
fn compute_latched_display_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    post: &MeasureResult,
    _pair_opt: Option<&str>,
    recording: bool,
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
            // 一時 idle / 一時消失(<60s) → latched-idle 表示（真消滅は poll_pre_liveness 管轄）。
            _ => Ok(delta_latched_idle()),
        };
    }

    // Watch 中。
    // (1) 名前変更/クリア → 即アンラッチ。
    let keep = current
        .as_ref()
        .is_some_and(|l| !pair_pre_name.is_empty() && l.name == pair_pre_name);
    if current.is_some() && !keep {
        if let Ok(mut g) = latched.lock() {
            *g = None;
        }
    }

    // (2) ラッチ維持中 → ラッチ先 pre.json 直読で維持/アンラッチ判定。
    if keep {
        let l = current.expect("keep implies current is Some");
        match read_pre_at(&l.pre_json) {
            // 実消滅（不在/読込不可）→ アンラッチし (3) の再ラッチへ落とす。
            None => {
                if let Ok(mut g) = latched.lock() {
                    *g = None;
                }
            }
            // stale>TTL or rename → アンラッチ。
            Some(st) if !st.fresh || st.name.as_deref() != Some(pair_pre_name) => {
                if let Ok(mut g) = latched.lock() {
                    *g = None;
                }
            }
            // fresh + active → 通常 Δ（現行どおり）。
            Some(st) if st.active => {
                let (d, ss) = compute_delta_for_pre_file(&l.pre_json, post)?;
                return Ok((d, false, ss));
            }
            // fresh + idle/silent → latched-idle（Stale + NaN / 凍結なし）。NoPre には落とさない。
            Some(_) => return Ok(delta_latched_idle()),
        }
    }

    // (3) 未ラッチ（含む直前アンラッチ）→ pair 名があれば Arm ゲートで初回/再ラッチ。
    if pair_pre_name.is_empty() {
        return Ok(delta_no_pre());
    }
    match select_target_pre_for_arm_for_post_project(kirin_root, pair_pre_name, post_project_hash) {
        Some(sel) => {
            let pre_json = sel.pre_json.clone();
            let project_dir = sel.project_dir.clone();
            if let Ok(mut g) = latched.lock() {
                *g = Some(LatchedPre {
                    name: pair_pre_name.to_string(),
                    instance_id: sel.instance_id,
                    project_dir: project_dir.clone(),
                    pre_json: pre_json.clone(),
                });
            }
            // 初回ラッチ直後の同 tick 表示。
            match read_pre_at(&pre_json) {
                Some(st) if st.active => {
                    let (d, ss) = compute_delta_for_pre_file(&pre_json, post)?;
                    Ok((d, false, ss))
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
    _discovery: &mut PostDiscoveryState,
    instance_dir: &Path,
    post_file: &Path,
    instance_id: &str,
    post_result: &Arc<Mutex<MeasureResult>>,
    delta_result: &Arc<Mutex<DeltaResult>>,
    signal_state_atom: &Arc<AtomicU8>,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    post_project_hash: &str,
    // B-108: recording=Record 中はラッチ凍結（アンラッチ/再選定しない）。latched=display と
    // keep/Arm が共有する単一ラッチ（io_thread が毎 tick 維持、keep が resolve_arm_target で読む）。
    recording: bool,
    latched: &Mutex<Option<LatchedPre>>,
) -> Result<(), String> {
    let state = load_signal_state(signal_state_atom);

    fs::create_dir_all(instance_dir).map_err(|e| format!("create_dir_all: {e}"))?;

    if state != SignalState::Active {
        *delta_result
            .lock()
            .map_err(|e| format!("delta Mutex poisoned: {e}"))? = DeltaResult::default();

        // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name は閉路 1 tick の snapshot。
        // Q-A7 採用案 A (post.json schema 拡張による cross-instance 公開)。
        // W-281: pair_claimed_at も同 tick snapshot を書き出す (後着優先 self check 軸)。
        let json = serialize_post_json_minimal(instance_id, state, pair_pre_name, pair_claimed_at);
        crate::atomic_file::write_bytes_atomic(post_file, json.as_bytes())
            .map_err(|e| format!("atomic write: {e}"))?;
        return Ok(());
    }

    // B-059: 表示=commit 一本化。commit (trigger_keep_internal) と同一の
    // `select_target_pre` で PRE を選定する。pair_pre_name 空 / 同名複数 / 不在 /
    // Inactive / 古t は None (= 表示 NoPre 沈黙 = commit 拒否)。
    let pair_opt = Some(pair_pre_name).filter(|s| !s.is_empty());

    let post = post_result
        .lock()
        .map_err(|e| format!("post Mutex poisoned: {e}"))?
        .clone();

    // B-108: ラッチ意味論で表示Δを決める（select_target_pre 直呼びを廃止）。一度成立した結合は
    // 無音/停止/一時鮮度揺らぎ/同名2台目では NoPre に落とさず、解除は名前変更/クリアと PRE 実消滅のみ。
    let (new_delta, store_directly, pre_signal_state) = compute_latched_display_for_post_project(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        &post,
        pair_opt,
        recording,
        latched,
    )?;

    // last_active 規律:
    // - store_directly（latched-idle）→ Stale + last_active=None をそのまま格納（凍結値復活禁止
    //   / B-048・B-049 維持）。merge_last_active を経由しない。
    // - それ以外 → resolve_delta_for_store（Active 保存 / active-pair fs-lag Stale は B-048 凍結保持
    //   / NoPre は last_active クリア）。
    {
        let mut delta_locked = delta_result
            .lock()
            .map_err(|e| format!("delta Mutex poisoned: {e}"))?;
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
    let json = serialize_post_json(
        instance_id,
        state,
        pre_signal_state,
        &post,
        pair_pre_name,
        pair_claimed_at,
    );
    crate::atomic_file::write_bytes_atomic(post_file, json.as_bytes())
        .map_err(|e| format!("atomic write: {e}"))?;

    Ok(())
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
    }
}

/// B-059 / G-115-245 置換: `run_tick` が delta_result に書く値を決める pure 関数。
///
/// - `mode == NoPre`（= `select_target_pre` が有効ペアなし）→ **`new_delta` をそのまま**
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
    if new_delta.mode == DeltaMode::NoPre {
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
                mode: DeltaMode::NoPre,
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
    let pair_pre_name_json =
        serde_json::to_string(pair_pre_name).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        r#"{{"v":2,"role":"POST","instance_id":{instance_id_json},"signal_state":"{signal_state}","pre_signal_state":{pre_signal_state},"t":"{t}","pair_pre_name":{pair_pre_name},"pair_claimed_at":{pair_claimed_at},"lufs_m":{lufs_m},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id_json = instance_id_json,
        signal_state = state.as_str(),
        pre_signal_state = pre_state_str,
        t = t,
        pair_pre_name = pair_pre_name_json,
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
fn serialize_post_json_minimal(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    // B-131 (G-115-380): instance_id / pair_pre_name を serde で JSON escape（serialize_post_json と同一契約）。
    let instance_id_json =
        serde_json::to_string(instance_id).unwrap_or_else(|_| "\"\"".to_string());
    let pair_pre_name_json =
        serde_json::to_string(pair_pre_name).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        r#"{{"v":2,"role":"POST","instance_id":{instance_id_json},"signal_state":"{signal_state}","t":"{t}","pair_pre_name":{pair_pre_name},"pair_claimed_at":{pair_claimed_at}}}"#,
        instance_id_json = instance_id_json,
        signal_state = state.as_str(),
        t = t,
        pair_pre_name = pair_pre_name_json,
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

// ── B-025 Group B-2/B-3 + G-115-64: exit_reason cleanup helper ────────────────

/// `RecordingCtx::exit_requested` sentinel が立ったときの cleanup を 1 単位で実行する
///
/// `run_record_tick` が連続失敗閾値 (`CONSECUTIVE_FAILURE_THRESHOLD` = 3) を検知して
/// `exit_requested` に sentinel をセットしたら、IO Thread main loop は本関数を呼び出し:
///   1. `writer_close_degraded(ctx)` — B-134: best-effort で integrity_degraded を立ててから
///      status=closed 確定（書ければ file flag / storage-loss は下記 3 の record_error_message が UI backstop）
///   2. `exit_record_full(...)` — record_sm + pair_label + paired_pre_target を一括 cleanup
///   3. `record_error_message` に GUI 通知文言を書込
///   4. `log::warn!` で診断ログ
///
/// editor 側 `trigger_stop_internal` と完全対称な構造的契約を成立させる
/// (Watch 状態 ⟹ pair_label 空 / paired_pre_target = None).
///
/// unit test 容易性のため main loop から切り出した。
#[allow(clippy::too_many_arguments)]
fn handle_exit_reason(
    reason: RecordError,
    recording: &mut Option<RecordingCtx>,
    plugin_data_root: Option<&Path>,
    project_hash: &str,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    record_error_message: &Arc<RwLock<Option<String>>>,
    instance_id: &str,
) {
    if let Some(ctx) = recording.take() {
        // B-134 (G-115-391): auto-stop（data-loss 異常終了）は best-effort で integrity_degraded を
        // 立ててから close（書ければ file flag / 書けねば下の record_error_message が UI backstop）。
        writer_close_degraded(ctx);
    }
    if let Some(base) = plugin_data_root {
        release_record_reservation(
            base,
            project_hash,
            instance_id,
            paired_pre_target,
            "exit_reason",
        );
    }
    exit_record_full(record_sm, pair_label, paired_pre_target);
    if let Ok(mut g) = record_error_message.write() {
        *g = Some(reason.ui_message().to_string());
    }
    log::warn!(
        "[IOThread POST] record exit: {} (post_iid={})",
        reason,
        instance_id
    );
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

fn poll_ack_timeout_with_base(
    base: &Path,
    project_hash: &str,
    instance_id: &str,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
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
        "[IOThread POST] ACK timeout ({}s) — auto-releasing record signal",
        ACK_TIMEOUT_SECONDS
    );
    match record_signal::mark_released(base, project_hash, instance_id) {
        Ok(true) => log::info!("[IOThread POST] mark_released ok"),
        Ok(false) => log::debug!("[IOThread POST] signal already gone"),
        Err(e) => log::warn!("[IOThread POST] mark_released failed: {}", e),
    }
    release_record_reservation(
        base,
        project_hash,
        instance_id,
        paired_pre_target,
        "ack_timeout",
    );
    // を成立させるため、editor 側 trigger_stop_internal と同じ 3 ステップを通過させる。
    exit_record_full(record_sm, pair_label, paired_pre_target);
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
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
) {
    if !record_sm.is_recording() {
        return;
    }
    let base = match StoragePaths::default_platform() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_record_signal_ack_with_base(&base, project_hash, instance_id, pair_label);
}

fn poll_record_signal_ack_with_base(
    base: &Path,
    project_hash: &str,
    instance_id: &str,
    pair_label: &Arc<Mutex<String>>,
) {
    let Some(signal) = record_signal::read_signal(base, project_hash, instance_id) else {
        return;
    };
    if signal.status != SignalStatus::Acknowledged {
        return;
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

fn count_preset_files(preset_dir: &Path) -> usize {
    let Ok(entries) = fs::read_dir(preset_dir) else {
        return 0;
    };
    entries
        .flatten()
        .filter(|e| {
            e.path()
                .file_name()
                .and_then(|n| n.to_str())
                .map(|n| n.ends_with(".json") && !n.ends_with(".tmp"))
                .unwrap_or(false)
        })
        .count()
}

fn poll_preset_availability(
    project_hash: &str,
    preset_available: &Arc<AtomicBool>,
    last_seen: &mut Option<usize>,
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
            if *last_seen != Some(0) {
                log::info!("[preset] unavailable");
                *last_seen = Some(0);
            }
            preset_available.store(false, Ordering::Relaxed);
            return;
        }
    };
    let count = count_preset_files(&preset_dir);
    preset_available.store(count > 0, Ordering::Relaxed);

    if *last_seen != Some(count) {
        if count > 0 {
            log::info!("[preset] available: {} files", count);
        } else {
            log::info!("[preset] unavailable");
        }
        *last_seen = Some(count);
    }
}

// ── Tests ─────────────────────────────────────────────────────────────────────

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
    fn count_empty_dir_returns_zero() {
        let dir = isolated_dir("empty");
        assert_eq!(count_preset_files(&dir), 0);
    }

    #[test]
    fn count_missing_dir_returns_zero() {
        let dir = isolated_dir("missing");
        let child = dir.join("no_such");
        assert_eq!(count_preset_files(&child), 0);
    }

    #[test]
    fn count_one_json_returns_one() {
        let dir = isolated_dir("one");
        fs::write(dir.join("a.json"), b"x").unwrap();
        assert_eq!(count_preset_files(&dir), 1);
    }

    #[test]
    fn count_ignores_tmp_and_non_json() {
        let dir = isolated_dir("ignore");
        fs::write(dir.join("ok.json"), b"x").unwrap();
        fs::write(dir.join("notes.txt"), b"x").unwrap();
        fs::write(dir.join("in_progress.json.tmp"), b"x").unwrap();
        assert_eq!(count_preset_files(&dir), 1);
    }

    #[test]
    fn count_multiple_json_files() {
        let dir = isolated_dir("multi");
        for name in ["a.json", "b.json", "c.json"] {
            fs::write(dir.join(name), b"x").unwrap();
        }
        assert_eq!(count_preset_files(&dir), 3);
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
        let json = format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{iid}","name":"{name}","signal_state":"{signal_state}","t":"{t}","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
        );
        let p = dir.join("pre.json");
        fs::write(&p, json).unwrap();
        p
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

    /// T4: pre.json 削除でアンラッチ → 同名 fresh PRE 再出現で自動再ラッチ（DAW 再開復元）。
    #[test]
    fn latch_delete_then_relatch() {
        let root = isolated_dir("latch_delete");
        let p = write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
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
        // 削除 → アンラッチ（再ラッチ先も無く NoPre）。
        fs::remove_file(&p).unwrap();
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
            "pre.json 削除でアンラッチ"
        );
        assert_eq!(d.mode, DeltaMode::NoPre);
        // 同名 fresh PRE 再出現 → 自動再ラッチ。
        write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
        let _ = compute_latched_display(
            &root,
            "snare",
            &latch_post(),
            Some("snare"),
            false,
            &latched,
        )
        .unwrap();
        assert!(latched.lock().unwrap().is_some(), "再出現で自動再ラッチ");
    }

    /// T5: ラッチ先 pre.json が stale > NO_PRE_SECS(10s) でアンラッチ（実消滅扱い）。
    #[test]
    fn latch_stale_beyond_ttl_unlatches() {
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
        // t を 20s 古く（> NO_PRE_SECS=10）→ アンラッチ + 再ラッチも stale 不可 → NoPre。
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
        assert!(latched.lock().unwrap().is_none(), "stale>TTL でアンラッチ");
        assert_eq!(d.mode, DeltaMode::NoPre);
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

    /// G-115-64: ACK timeout 経路でも `exit_record_full` で 3 ステップ cleanup される.
    #[test]
    fn pending_over_30s_is_auto_released_with_full_cleanup() {
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
        assert_eq!(after.status, SignalStatus::Released);
        assert_eq!(sm.current(), RecordState::Watch);
        assert!(
            pair_label.lock().unwrap().is_empty(),
            "G-115-64: ACK timeout must clear pair_label"
        );
        assert!(
            paired_pre_target.lock().unwrap().is_none(),
            "G-115-64: ACK timeout must reset paired_pre_target to None"
        );
        assert_eq!(
            crate::reservation::count_frames(&base, TEST_PH),
            0,
            "ACK timeout must release the O_EXCL reservation frame"
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
    }
}

// ── PostCandidate / scan / discover / enumerate テスト (Step 5) ───────────────
#[cfg(test)]
mod post_candidate_tests {
    use super::*;
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
        assert_eq!(c.pair_pre_name.as_deref(), Some("PRE-Master"));
        assert!(c.path.ends_with("post.json"));
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
mod handle_exit_reason_tests {
    use super::*;
    use crate::record::{RecordState, RecordStateMachine};

    const TEST_PH: &str = "ph";

    /// `RecordingCtx` 無し (= writer 未確立 / Watch 直前で連続失敗閾値到達は通常起こらないが
    /// 防御的に検証) でも 3 ステップ cleanup が動く. `exit_record_full` 経由で
    /// pair_label / paired_pre_target が完全 cleanup される.
    #[test]
    fn handle_exit_reason_runs_full_cleanup_without_recording() {
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();
        let pair_label = Arc::new(Mutex::new("pair: deadbeef".to_string()));
        let paired_pre_target = Arc::new(Mutex::new(Some("pre-iid-x".to_string())));
        let record_error_message: Arc<RwLock<Option<String>>> = Arc::new(RwLock::new(None));
        let mut recording: Option<RecordingCtx> = None;
        let base = std::env::temp_dir().join(format!(
            "kirin_handle_exit_reason_release_{}_dir",
            std::process::id()
        ));
        let _ = fs::remove_dir_all(&base);
        fs::create_dir_all(&base).unwrap();
        crate::reservation::reserve_pairing(&base, TEST_PH, "pre-iid-x", "post-iid-test").unwrap();

        handle_exit_reason(
            RecordError::DirectoryMissing,
            &mut recording,
            Some(&base),
            TEST_PH,
            &sm,
            &pair_label,
            &paired_pre_target,
            &record_error_message,
            "post-iid-test",
        );

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(
            pair_label.lock().unwrap().is_empty(),
            "G-115-64: pair_label must be cleared by handle_exit_reason"
        );
        assert!(
            paired_pre_target.lock().unwrap().is_none(),
            "G-115-64: paired_pre_target must be reset by handle_exit_reason"
        );
        let msg = record_error_message.read().unwrap().clone();
        assert_eq!(msg.as_deref(), Some("Record stopped: storage missing"));
        assert_eq!(
            crate::reservation::count_frames(&base, TEST_PH),
            0,
            "auto-stop must release the O_EXCL reservation frame"
        );
        let _ = fs::remove_dir_all(&base);
    }

    /// `WriteFailureExceeded` 経路でも完全 cleanup + 文言が固定 (G-115-29).
    #[test]
    fn handle_exit_reason_write_failure_message_is_fixed() {
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();
        let pair_label = Arc::new(Mutex::new("pair: cafef00d".to_string()));
        let paired_pre_target = Arc::new(Mutex::new(Some("pre-iid-y".to_string())));
        let record_error_message: Arc<RwLock<Option<String>>> = Arc::new(RwLock::new(None));
        let mut recording: Option<RecordingCtx> = None;

        handle_exit_reason(
            RecordError::WriteFailureExceeded,
            &mut recording,
            None,
            TEST_PH,
            &sm,
            &pair_label,
            &paired_pre_target,
            &record_error_message,
            "post-iid-test",
        );

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(pair_label.lock().unwrap().is_empty());
        assert!(paired_pre_target.lock().unwrap().is_none());
        let msg = record_error_message.read().unwrap().clone();
        assert_eq!(msg.as_deref(), Some("Record stopped: write failed"));
    }

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
