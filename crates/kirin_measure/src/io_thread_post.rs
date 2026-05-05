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
//! 3層隔離（guardian_53）:
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

use serde::Deserialize;

use crate::all_keep_signal::{self, ALL_KEEP_BROADCAST_STALE_SECS};
use crate::all_stop_signal::{self, ALL_STOP_BROADCAST_STALE_SECS};
use crate::delta::{DeltaMode, DeltaResult};
use crate::plugin_data::Role as PluginDataRole;
use crate::pre_discovery::{discover_active_pre_dir, PostDiscoveryState, DISCOVERY_STALE_SECS};
use crate::record::RecordStateMachine;
use crate::record_signal::{self, SignalStatus, ACK_TIMEOUT_SECONDS, SIGNALS_SUBDIR};
use crate::record_writer::{run_record_tick, writer_close, RecordingCtx};
use crate::storage::StoragePaths;
use crate::{load_signal_state, MeasureResult, SignalState};

/// IO Thread ループ間隔（guardian_53: 100ms = 10fps）
const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// PRE ファイルが Active とみなされる最大経過時間（秒）
const STALE_SECS: i64 = 2;

/// PRE ファイルが NoPre とみなされる最大経過時間（秒）
const NO_PRE_SECS: i64 = 10;

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
///   完全不変 (S114-S115 §2-3) を両立する設計判断 (Q-11-D 案 (a))。
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
) -> JoinHandle<()> {
    thread::spawn(move || {
        // B-021 Phase 1A: PRE scan の起点は `kirin_root` (= $TMPDIR/kirin/) で、
        // POST IO Thread が動的に discover する。`project_dir_hint` は POST 自身の
        // project_uuid から構築した fallback (PRE が見つからない場合のみ使う)。
        // POST 自身の post.json 書込先は instance_dir 固定 (POST 自分の project_uuid)。
        let kirin_root = std::env::temp_dir().join("kirin");
        let initial_project_hash = read_project_hash_arc(&project_hash);
        let initial_instance_id = read_instance_id_arc(&instance_id);
        let plugin_data_dir_str = match StoragePaths::default_macos() {
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
            let project_dir_hint = kirin_root.join(project_hash_ref);
            let instance_dir = project_dir_hint.join(instance_id_ref);
            let post_file = instance_dir.join("post.json");
            let post_tmp = instance_dir.join("post.json.tmp");

            // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name snapshot per tick。
            // RwLock read guard 寿命を tick 内に閉じる (closure スコープから外で
            // guard を保持しない)。poison error 時は空文字 fallback (旧 schema 互換)。
            let pair_pre_name_snapshot = snapshot_pair_pre_name(&pair_pre_name_for_thread);

            match run_tick(
                &project_dir_hint,
                &kirin_root,
                &mut discovery,
                &instance_dir,
                &post_tmp,
                &post_file,
                instance_id_ref,
                &post_result,
                &delta_result,
                &signal_state,
                &pair_pre_name_snapshot,
            ) {
                Ok(()) => {}
                Err(e) => log::warn!("[IOThread POST] tick error: {}", e),
            }

            // plugin_data/.../post/*.json ライフサイクル
            // POST は自身の signal_path から started_at を resolve
            // §4-5 Step 1: project_hash_ref は tick 開始時の lazy-read snapshot を流用。
            let resolver = || match StoragePaths::default_macos() {
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
            let paired_pre_resolver =
                move || paired_pre_arc.lock().ok().and_then(|g| g.clone());
            let paired_post_resolver = || None::<String>;
            if let Err(e) = run_record_tick(
                &record_sm,
                PluginDataRole::Post,
                sample_rate,
                project_hash_ref,
                instance_id_ref,
                resolver,
                paired_pre_resolver,
                paired_post_resolver,
                &post_result,
                &mut recording,
            ) {
                log::warn!("[writer] tick error: {}", e);
            }

            if Instant::now() >= next_preset_poll {
                poll_preset_availability(project_hash_ref, &preset_available, &mut last_preset_count);
                next_preset_poll = Instant::now() + PRESET_POLL_INTERVAL;
            }

            if Instant::now() >= next_ack_timeout_poll {
                poll_ack_timeout(project_hash_ref, instance_id_ref, &record_sm);
                next_ack_timeout_poll = Instant::now() + ACK_TIMEOUT_POLL_INTERVAL;
            }

            // B-023 段階 4: PRE 側 ack 後の paired_pre_name を読み出して pair_label
            // を更新（1 秒 throttle / record_sm.is_recording() ガードで Stop 直後の
            // 復活窓を構造的に防止）。
            if Instant::now() >= next_pair_label_poll {
                poll_record_signal_ack(
                    project_hash_ref,
                    instance_id_ref,
                    &record_sm,
                    &pair_label,
                );
                next_pair_label_poll = Instant::now() + PAIR_LABEL_POLL_INTERVAL;
            }

            // α-7' All Stop: Stop broadcast 受信 sub-tick (Keep より先に処理 / Stop 優先)。
            // 1 秒 throttle で `plugin_data/{ph}/all_stop_signal/*.json` を全件 scan し、
            // 新 broadcast を `processed_stop_broadcasts` cache に登録 + `trigger_stop_resolution`
            // closure 発火。Keep と並列の同型ロジック (cross-process filter / self skip /
            // 既処理 skip / stale fallback / GC)。
            if Instant::now() >= next_all_keep_poll {
                if let Ok(paths) = StoragePaths::default_macos() {
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
                    processed_stop_broadcasts.retain(|_, (_, last_seen)| last_seen.elapsed() < timeout);
                }
                // 注: next_all_keep_poll は Keep sub-tick 末で reset されるため
                // Stop は Keep と同 throttle (1 秒) で同 frame に動く。
            }

            // B-027 段階 3-B α-7-4-C / Step 10: all_keep_signal broadcast 受信 sub-tick。
            // 1 秒 throttle で `plugin_data/{ph}/all_keep_signal/*.json` を全件 scan し、
            // 新 broadcast を `processed_broadcasts` cache に登録する (検出 + cache + log
            // のみ / `trigger_keep_internal` 発火は Step 11 で本箇所に追加予定)。
            //
            // 受信側ロジック (S117 判断 / DEV INBOX §6 / 設計判断 #16-#24):
            //  1. cross-process 防壁: `broadcast.daw_session_id != self_daw_session_id` skip
            //  2. self skip: `originator_iid == self_instance_id` skip (#16 (iii))
            //  3. 既処理 skip: cache 内の `started_at` と一致 → 同 broadcast 既処理 skip
            //     (clock-skew 完全耐性 / Q-A8-6)
            //  4. stale fallback: `is_broadcast_stale` (≥30 秒経過) → cache 登録のみ +
            //     非発火 (S117 判断 2 (P) / orphan broadcast の構造的無視)
            //  5. 新 broadcast: cache 更新 + log::info! (Step 11 で trigger_keep_internal 発火)
            //  6. GC: `last_seen.elapsed() >= ACK_TIMEOUT_SECONDS` の entry を retain で削除
            //     (先例 io_thread_pre.rs:378-403 partner.last_seen_status cache 同位相 /
            //     chrono 新規依存なし / 申し送り #24 (ii))
            if Instant::now() >= next_all_keep_poll {
                if let Ok(paths) = StoragePaths::default_macos() {
                    let base_dir = paths.plugin_data_dir();
                    let now_chrono = chrono::Utc::now();
                    // §4-5 Step 1: cross-process 防壁用 daw_session_id を per-tick lazy-read。
                    // editor() snapshot との divergence を是正 (§4-4 R-9 主因 b)。
                    let daw_session_id_snapshot = read_daw_session_id_arc(&daw_session_id_arc);
                    let broadcasts = all_keep_signal::scan_broadcasts_dir(&base_dir, project_hash_ref);
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
                    log::warn!("[all_keep] StoragePaths::default_macos() failed; skipping tick");
                }
                next_all_keep_poll = Instant::now() + ALL_KEEP_POLL_INTERVAL;
            }

            thread::sleep(LOOP_SLEEP);
        }

        // 終了処理: 直近 tick の instance_id でクリーンアップ。
        // `set_state` 復元後に instance_id が切り替わった場合は旧 instance dir
        // (Default UUID) の post.json が残骸として残るが、次回起動時の同関数で
        // 同じ Default UUID を踏むことは無いため自然消失する (R-28 機能的沈黙)。
        if let Some(ctx) = recording.take() {
            writer_close(ctx);
        }

        // §4-5 Step 1: 終了処理時も project_hash を lazy-read で確定。
        let final_iid = read_instance_id_arc(&instance_id);
        let final_project_hash = read_project_hash_arc(&project_hash);
        let final_project_dir_hint = kirin_root.join(&final_project_hash);
        let final_instance_dir = final_project_dir_hint.join(&final_iid);
        let final_post_file = final_instance_dir.join("post.json");
        let final_post_tmp = final_instance_dir.join("post.json.tmp");
        if let Err(e) = fs::remove_file(&final_post_file) {
            log::debug!("[IOThread POST] cleanup post file: {}", e);
        }
        if let Err(e) = fs::remove_file(&final_post_tmp) {
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
        // (統合点 #4 / DEV INBOX §9-3 / S117 判断 2 (P))。`paths` は同 scope 内で 2 度
        // 利用 (二重 resolve 回避)。機能的差異なし / panic 回避規範 (warn のみ) は両者で同等。
        match StoragePaths::default_macos() {
            Ok(paths) => {
                match record_signal::delete_signal(
                    &paths.plugin_data_dir(),
                    &final_project_hash,
                    &final_iid,
                ) {
                    Ok(()) => log::info!(
                        "[POST cleanup #4] record_signal deleted: {}",
                        final_iid
                    ),
                    Err(e) => log::warn!(
                        "[POST cleanup #4] delete_signal failed: {:?}",
                        e
                    ),
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
            Err(e) => log::warn!(
                "[POST cleanup #4] StoragePaths error: {:?}",
                e
            ),
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
fn run_tick(
    project_dir_hint: &Path,
    kirin_root: &Path,
    discovery: &mut PostDiscoveryState,
    instance_dir: &Path,
    post_tmp: &Path,
    post_file: &Path,
    instance_id: &str,
    post_result: &Arc<Mutex<MeasureResult>>,
    delta_result: &Arc<Mutex<DeltaResult>>,
    signal_state_atom: &Arc<AtomicU8>,
    pair_pre_name: &str,
) -> Result<(), String> {
    let state = load_signal_state(signal_state_atom);

    fs::create_dir_all(instance_dir).map_err(|e| format!("create_dir_all: {e}"))?;

    if state != SignalState::Active {
        *delta_result
            .lock()
            .map_err(|e| format!("delta Mutex poisoned: {e}"))? = DeltaResult::default();

        // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name は閉路 1 tick の snapshot。
        // Q-A7 採用案 A (post.json schema 拡張による cross-instance 公開)。
        let json = serialize_post_json_minimal(instance_id, state, pair_pre_name);
        fs::write(post_tmp, json.as_bytes()).map_err(|e| format!("write tmp: {e}"))?;
        fs::rename(post_tmp, post_file).map_err(|e| format!("rename: {e}"))?;
        return Ok(());
    }

    // B-021 Phase 1A: PRE discovery (1 秒 throttle)。
    let now = Instant::now();
    if discovery.should_rescan(now) {
        let found = discover_active_pre_dir(kirin_root);
        discovery.record_scan(now, found);
    }
    let project_dir: &Path = discovery
        .cached_pre_dir()
        .unwrap_or(project_dir_hint);

    let post = post_result
        .lock()
        .map_err(|e| format!("post Mutex poisoned: {e}"))?
        .clone();

    let (delta, pre_signal_state) = compute_delta_with_state(project_dir, &post)?;

    *delta_result
        .lock()
        .map_err(|e| format!("delta Mutex poisoned: {e}"))? = delta;

    // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name は閉路 1 tick の snapshot
    // (Q-A7 採用案 A 完成 / cross-instance 公開機構)。
    let json = serialize_post_json(instance_id, state, pre_signal_state, &post, pair_pre_name);
    fs::write(post_tmp, json.as_bytes()).map_err(|e| format!("write tmp: {e}"))?;
    fs::rename(post_tmp, post_file).map_err(|e| format!("rename: {e}"))?;

    Ok(())
}

/// PRE ファイルをスキャンして Δ を算出する（後方互換ラッパー）。
#[doc(hidden)]
pub fn compute_delta(project_dir: &Path, post: &MeasureResult) -> Result<DeltaResult, String> {
    compute_delta_with_state(project_dir, post).map(|(delta, _)| delta)
}

/// `$TMPDIR/kirin/{project_hash}/` 配下の全 instance_id サブディレクトリを走査して
/// `pre.json` を集め、Δ を算出する。
///
/// - 0 個 → `DeltaMode::NoPre`, `None`
/// - 複数 → 最新 `t` を選択（ISO 8601 は文字列比較で最新判定可）
/// - 鮮度判定 → `Active` / `Stale` / `NoPre`
fn compute_delta_with_state(
    project_dir: &Path,
    post: &MeasureResult,
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

    let mut pre_files: Vec<PathBuf> = Vec::new();
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
            pre_files.push(candidate);
        }
    }

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
    let content = fs::read_to_string(&best).map_err(|e| format!("read PRE file: {e}"))?;
    let parsed: serde_json::Value =
        serde_json::from_str(&content).map_err(|e| format!("parse PRE JSON: {e}"))?;

    let pre_signal_state = parsed["signal_state"]
        .as_str()
        .map(|s| match s {
            "active" => SignalState::Active,
            "bypassed" => SignalState::Bypassed,
            _ => SignalState::Inactive,
        });

    if pre_signal_state != Some(SignalState::Active) {
        return Ok((
            DeltaResult {
                lufs: None,
                tp: None,
                crest: None,
                mode: DeltaMode::NoPre,
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
    let pre_tp = parsed["true_peak"].as_f64();
    let pre_crest = parsed["crest"].as_f64();

    let delta_lufs = post.lufs_m.zip(pre_lufs).map(|(p, r)| p - r);
    let delta_tp = post.true_peak.zip(pre_tp).map(|(p, r)| p - r);
    let delta_crest = post.crest.zip(pre_crest).map(|(p, r)| p - r);

    Ok((
        DeltaResult {
            lufs: delta_lufs,
            tp: delta_tp,
            crest: delta_crest,
            mode,
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

    Ok(if age_secs >= NO_PRE_SECS {
        DeltaMode::NoPre
    } else if age_secs >= STALE_SECS {
        DeltaMode::Stale
    } else {
        DeltaMode::Active
    })
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
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    let pre_state_str = pre_signal_state
        .map(|s| format!(r#""{}""#, s.as_str()))
        .unwrap_or_else(|| "null".to_string());
    format!(
        r#"{{"v":2,"role":"POST","instance_id":"{instance_id}","signal_state":"{signal_state}","pre_signal_state":{pre_signal_state},"t":"{t}","pair_pre_name":"{pair_pre_name}","lufs_m":{lufs_m},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id = instance_id,
        signal_state = state.as_str(),
        pre_signal_state = pre_state_str,
        t = t,
        pair_pre_name = pair_pre_name,
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
fn serialize_post_json_minimal(instance_id: &str, state: SignalState, pair_pre_name: &str) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    format!(
        r#"{{"v":2,"role":"POST","instance_id":"{instance_id}","signal_state":"{signal_state}","t":"{t}","pair_pre_name":"{pair_pre_name}"}}"#,
        instance_id = instance_id,
        signal_state = state.as_str(),
        t = t,
        pair_pre_name = pair_pre_name,
    )
}

/// `post.json` の deserialize 用 wire format struct (B-027 段階 3-B α-7-1 / Step 4)。
///
/// `serialize_post_json` / `serialize_post_json_minimal` (本 file 上方) で書き出される
/// post.json を、同 project_hash 内の他 POST instance から read するために定義する
/// (cross-instance 公開機構 / Q-A7 採用案 A)。Step 5 (`scan_post_candidates_in` 等)
/// から `pub(crate)` で参照される。
///
/// # field 構成
/// Active 時 (full) と Bypassed・Inactive 時 (minimal) で書込 field 数が異なる。
/// 共通 field (instance_id / signal_state / t / pair_pre_name) は最低限の必須型。
/// schema metadata (`v` / `role`) は `#[serde(default)]` で旧 schema 互換確保。
/// Active のみの値 field (pre_signal_state / lufs_m / true_peak / crest / psr /
/// phase_d 系) は `Option<T>` + `#[serde(default)]` で minimal 形式 / 旧 schema
/// での不在を許容。
///
/// # 旧 schema 互換 (申し送り #19)
/// `pair_pre_name` は本 stage で追加された field。本変更前 plugin が書いた post.json
/// (`pair_pre_name` field 不在) との互換は `#[serde(default)]` で空文字 fallback
/// で保証する (`record_signal::PreTmpJson::name` field と同位相)。
/// serde 1.0.228 / serde_json 1.0.149 公式仕様 ([serde docs](https://serde.rs/field-attrs.html#default))。
///
/// Step 5 (`scan_post_candidates_in` 等) から実利用されるまで lib build で
/// dead_code lint が立つ (cfg(test) でのみ deserialize 経由 construct されるため)。
/// allow を付与して struct 完全性 (schema 定義) を保つ。
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize)]
pub(crate) struct PostTmpJson {
    /// schema version (常に 2)。read 側では値を参照しないが完全 schema 定義として保持。
    #[serde(default)]
    pub v: u32,
    /// 役割識別子 (常に "POST")。
    #[serde(default)]
    pub role: String,
    /// instance_id (POST 自身の UUID)。
    pub instance_id: String,
    /// signal_state ("active" / "bypassed" / "inactive")。
    pub signal_state: String,
    /// pre_signal_state は Active 時のみ書込 (Bypassed / Inactive で不在 → None)。
    #[serde(default)]
    pub pre_signal_state: Option<String>,
    /// 書込時刻 (ISO 8601 / RFC 3339)。
    pub t: String,
    /// B-027 段階 3-B α-7-1: pair_pre_name (POST GUI 選択 PRE 表示用 Name / 公開機構)。
    /// 旧 schema (本変更前 plugin) には不在 → 空文字 fallback。
    #[serde(default)]
    pub pair_pre_name: String,
    /// Active 時の計測値 (null 許容で `Option<f64>`)。Minimal で field 不在 → None。
    #[serde(default)]
    pub lufs_m: Option<f64>,
    #[serde(default)]
    pub true_peak: Option<f64>,
    #[serde(default)]
    pub crest: Option<f64>,
    #[serde(default)]
    pub psr: Option<f64>,
    /// Phase D 拡張 (Active 時 + 値が Some(_) の場合のみ書込)。
    #[serde(default)]
    pub n_prime_total: Option<f64>,
    #[serde(default)]
    pub sharpness: Option<f64>,
    #[serde(default)]
    pub psb_summary: Option<PostPsbSummary>,
}

/// `post.json` の `psb_summary` object (Phase D)。
///
/// 本 struct は serde deserialize 経由でのみ構築されるため、Step 5 で
/// `scan_post_candidates_in` 等の read 経路から実利用されるまで dead_code lint
/// が立つ。allow を付与して struct 完全性 (schema 定義) を保つ。
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize)]
pub(crate) struct PostPsbSummary {
    pub low: f64,
    pub mid: f64,
    pub high: f64,
}

// ── PostCandidate / scan / discover / enumerate (B-027 段階 3-B α-7-1 / Step 5) ─
//
// PRE 版 (`record_signal::PreCandidate` / `scan_pre_candidates_in` /
// `pre_discovery::discover_active_pre_dirs` / `record_signal::enumerate_active_pre_pair_candidates`)
// と完全対称形。POST 側 `pair_pre_name` cross-instance 公開機構 (Q-A7 採用案 A) と
// α-7 All Keep broadcast (Q-A8 採用案 ii / iii) の peer enumerate 経路を提供する。
//
// 配置: io_thread_post.rs (post.json schema 定義 / serialize / deserialize と
// 同 file)。PRE 版は record_signal.rs / pre_discovery.rs に分散しているが、POST
// 側は schema 集中 (#20 / file 一貫性) を優先して同 file 内集約。
//
// Step 5 では struct + 関数定義のみ。lib.rs 呼出側改修は Step 6 以降のため
// 各 item に `#[allow(dead_code)]` を付与する (Step 6 で削除予定)。

/// `/tmp/kirin/{project_uuid}/{instance_id}/post.json` 1 件分のパース結果。
///
/// PRE 版 [`crate::record_signal::PreCandidate`] と対称形。
/// PRE 版が距離計算用の計測値 (`lufs_m` / `true_peak` / `crest`) を持つのに対し、
/// POST 版は cross-project enumerate 用に `project_uuid` を持つ (cdylib 隔離環境で
/// 各 candidate が所属する project_uuid を保持する目的 / G-115-49)。
///
/// `pair_pre_name` は `Option<String>` (PRE 版 `name` と同位相)。post.json の
/// `pair_pre_name` field が空文字 → `None` / 非空 → `Some(...)` に変換する。
///
/// B-027 段階 3-B α-7-4-D Step 3 (S119 設計判断 #23 (A)): visibility を `pub(crate)` →
/// `pub` に昇格 (Phase B Step 5 H 報告の「Step 6 で削除予定」遅延履行)。
/// hypha_post crate (editor.rs) からの呼出経路 (`enumerate_active_post_pair_candidates`)
/// で利用される。
#[derive(Debug, Clone, PartialEq)]
pub struct PostCandidate {
    pub instance_id: String,
    pub project_uuid: String,
    pub pair_pre_name: Option<String>,
    pub path: PathBuf,
}

/// 指定された `{project_uuid}/` dir 配下の `post.json` を走査する。
///
/// PRE 版 [`crate::record_signal::scan_pre_candidates_in`] と対称形。
/// `record_signal/` 予約 dir は除外。`post.json` deserialize 失敗・ファイル不在は
/// silently skip。`signal_state == "bypassed"` の POST は除外する (PRE 側 Bypass
/// 防御と対称 / 二重防御の片側)。
///
/// `pair_pre_name`: `String` → `Option<String>` 変換 (空文字 → `None` / 非空 →
/// `Some(...)`)。`#[serde(default)]` により旧 schema (本 stage 前 plugin) の post.json
/// では空文字 → `None` で fallback する。
///
/// 戻り順: instance_id 辞書順 (PRE 版と同じ / 再現性確保)。
#[allow(dead_code)]
pub(crate) fn scan_post_candidates_in(project_dir: &Path) -> Vec<PostCandidate> {
    let project_uuid = project_dir
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("")
        .to_string();
    let entries = match fs::read_dir(project_dir) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };
    let mut out = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        // record_signal/ 予約 dir 除外 (PRE 版対称)
        if path.file_name().and_then(|n| n.to_str()) == Some(SIGNALS_SUBDIR) {
            continue;
        }
        let post_file = path.join("post.json");
        let Ok(bytes) = fs::read(&post_file) else { continue };
        let Ok(parsed): Result<PostTmpJson, _> = serde_json::from_slice(&bytes) else {
            continue;
        };
        // Bypassed の POST は候補から除外 (PRE 版 Bypass 防御対称 / 二重防御の片側)。
        // Active / Inactive / 旧 schema は候補化する。
        if parsed.signal_state == "bypassed" {
            continue;
        }
        let pair_pre_name = if parsed.pair_pre_name.is_empty() {
            None
        } else {
            Some(parsed.pair_pre_name)
        };
        out.push(PostCandidate {
            instance_id: parsed.instance_id,
            project_uuid: project_uuid.clone(),
            pair_pre_name,
            path: post_file,
        });
    }
    out.sort_by(|a, b| a.instance_id.cmp(&b.instance_id));
    out
}

/// `kirin_root` (= `$TMPDIR/kirin/`) 配下を scan して **mtime fresh の全 active POST
/// dir** を返す。
///
/// PRE 版 [`crate::pre_discovery::discover_active_pre_dirs`] と対称形。各
/// `{project_uuid}/` 配下に `*/post.json` が 1 件以上存在し、mtime が
/// `DISCOVERY_STALE_SECS` (= 10s) 以内に更新されている dir のみ候補化する。
///
/// 用途: α-7 All Keep broadcast の peer POST 列挙。POST originator は本関数で
/// 全 fresh project_uuid dir を取得し、各 dir で `scan_post_candidates_in` →
/// flatten で peer POST candidates 列挙する。
///
/// 戻り順: project_uuid (= file_name) 辞書順固定 (PRE 版と同じ / G-115-53 と同一
/// 根拠 = 100ms tick 書込 / 10 Hz draw race で順序反転回避)。
///
/// 空入力 / 全 stale なら空 Vec。
#[allow(dead_code)]
pub(crate) fn discover_active_post_dirs(kirin_root: &Path) -> Vec<PathBuf> {
    let now = SystemTime::now();
    let stale_threshold = Duration::from_secs(DISCOVERY_STALE_SECS);

    let project_entries = match fs::read_dir(kirin_root) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };

    let mut candidates: Vec<(PathBuf, SystemTime)> = Vec::new();

    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }

        let instance_entries = match fs::read_dir(&project_dir) {
            Ok(e) => e,
            Err(_) => continue,
        };

        let mut latest_in_project: Option<SystemTime> = None;
        for instance_entry in instance_entries.flatten() {
            let instance_dir = instance_entry.path();
            if !instance_dir.is_dir() {
                continue;
            }

            let post_json = instance_dir.join("post.json");
            let meta = match fs::metadata(&post_json) {
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

            // stale 判定: now - mtime > threshold なら除外。
            // future mtime (clock skew) は Err → 「fresh」扱い (PRE 版対称)。
            if let Ok(age) = now.duration_since(mtime) {
                if age > stale_threshold {
                    continue;
                }
            }

            latest_in_project = Some(match latest_in_project {
                Some(prev) if prev > mtime => prev,
                _ => mtime,
            });
        }

        if let Some(t) = latest_in_project {
            candidates.push((project_dir, t));
        }
    }

    // project_uuid (= file_name) 辞書順固定 (PRE 版 G-115-53 対称)。
    candidates.sort_by(|a, b| a.0.file_name().cmp(&b.0.file_name()));
    candidates.into_iter().map(|(p, _)| p).collect()
}

/// 全 active POST dir を flatten 列挙して `Vec<PostCandidate>` を返す
/// (α-7 All Keep broadcast peer enumerate 用)。
///
/// PRE 版 [`crate::record_signal::enumerate_active_pre_pair_candidates`] と対称形。
/// `discover_active_post_dirs` で fresh 全 project_uuid dir を取得し、各 dir の
/// post.json を `scan_post_candidates_in` で候補化して flatten する。
///
/// # 戻り順
/// project_uuid (= file_name) 辞書順 (`discover_active_post_dirs` の順序) → 各 dir
/// 内 instance_id 辞書順 (`scan_post_candidates_in` の順序)。
///
/// # cdylib 越境通信 (申し送り #22)
/// 含まない (filesystem enumerate のみ)。`project_uuid_cell()` /
/// `peek_project_uuid` / `set_project_uuid` のいずれも本関数からは触れない。
///
/// # B-027 段階 3-B α-7-4-D Step 3 (S119 設計判断 #23 (A))
/// visibility を `pub(crate)` → `pub` に昇格 (Phase B Step 5 H 報告の「Step 6 で削除予定」
/// 遅延履行)。hypha_post crate (editor.rs / `draw_pair_pre_combo` の N 集計) から
/// 直接呼出される。`scan_post_candidates_in` / `discover_active_post_dirs` は本関数
/// 経由で内部利用されるため `pub(crate)` 維持。
pub fn enumerate_active_post_pair_candidates(kirin_root: &Path) -> Vec<PostCandidate> {
    discover_active_post_dirs(kirin_root)
        .into_iter()
        .flat_map(|d| scan_post_candidates_in(&d))
        .collect()
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
) {
    let base = match StoragePaths::default_macos() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_ack_timeout_with_base(&base, project_hash, instance_id, record_sm, chrono::Utc::now());
}

fn poll_ack_timeout_with_base(
    base: &Path,
    project_hash: &str,
    instance_id: &str,
    record_sm: &Arc<RecordStateMachine>,
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
    record_sm.exit_record();
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
    let base = match StoragePaths::default_macos() {
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
    let preset_dir = match StoragePaths::default_macos() {
        Ok(paths) => paths
            .plugin_data_dir()
            .join(project_hash)
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
        )
        .unwrap();
        assert_eq!(r.0.mode, DeltaMode::NoPre);
    }

    #[test]
    fn scans_across_instance_ids() {
        let pd = isolated_project_dir();
        let now = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ").to_string();
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
        )
        .unwrap();
        // Δ が算出される（mode が Active）
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
        )
        .unwrap();
        // record_signal/ 以外に pre が無いので NoPre
        assert_eq!(r.0.mode, DeltaMode::NoPre);
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

    #[test]
    fn pending_over_30s_is_auto_released() {
        let base = isolated_base("stale");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();

        write_pending(
            &base,
            TEST_PH,
            TEST_POST_IID,
            "pre-1".into(),
            "daw-1".into(),
        )
        .unwrap();
        let future_now = chrono::Utc::now() + chrono::Duration::seconds(31);

        poll_ack_timeout_with_base(&base, TEST_PH, TEST_POST_IID, &sm, future_now);

        let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
        assert_eq!(after.status, SignalStatus::Released);
        assert_eq!(sm.current(), RecordState::Watch);
    }

    #[test]
    fn pending_within_30s_is_noop() {
        let base = isolated_base("fresh");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();

        write_pending(
            &base,
            TEST_PH,
            TEST_POST_IID,
            "pre-1".into(),
            "daw-1".into(),
        )
        .unwrap();
        poll_ack_timeout_with_base(&base, TEST_PH, TEST_POST_IID, &sm, chrono::Utc::now());

        let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
        assert_eq!(after.status, SignalStatus::Pending);
        assert_eq!(sm.current(), RecordState::Record);
    }

    #[test]
    fn acknowledged_is_noop_even_over_30s() {
        let base = isolated_base("acked");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();

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
        poll_ack_timeout_with_base(&base, TEST_PH, TEST_POST_IID, &sm, future_now);

        let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
        assert_eq!(after.status, SignalStatus::Acknowledged);
        assert_eq!(sm.current(), RecordState::Record);
    }

    #[test]
    fn missing_signal_is_noop() {
        let base = isolated_base("missing");
        let sm = Arc::new(RecordStateMachine::new());

        poll_ack_timeout_with_base(&base, TEST_PH, TEST_POST_IID, &sm, chrono::Utc::now());

        assert_eq!(sm.current(), RecordState::Watch);
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
        assert_eq!(parsed.lufs_m, Some(-12.0));
        assert_eq!(parsed.true_peak, Some(-0.5));
        assert_eq!(parsed.crest, Some(10.0));
        assert_eq!(parsed.psr, Some(7.0));
        assert!(parsed.n_prime_total.is_none());
        assert!(parsed.sharpness.is_none());
        assert!(parsed.psb_summary.is_none());
    }

    /// Minimal (`serialize_post_json_minimal` 出力 / Bypassed) → PostTmpJson roundtrip。
    /// pre_signal_state / 計測値系は不在 → Option::None で defaulted。
    #[test]
    fn deserialize_minimal_roundtrip() {
        let json = serialize_post_json_minimal("post-iid-B", SignalState::Bypassed, "PRE-Mix");
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
        );
        let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

        assert_eq!(parsed.pair_pre_name, "");
        assert_eq!(parsed.signal_state, "active");
        assert!(parsed.lufs_m.is_none());
    }

    /// Phase D 拡張 field (n_prime_total / sharpness / psb_summary) を含む JSON の deserialize。
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
            ),
            _ => serialize_post_json_minimal(instance_id, signal_state, pair_pre_name),
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

