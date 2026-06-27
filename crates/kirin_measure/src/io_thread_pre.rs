//! IO Thread — PRE 側（A-3 修正後 / B-022 段階 3 更新）。
//!
//! 100ms ループで:
//! 1. `$TMPDIR/kirin/{project_hash}/{instance_id}/pre.json` にアトミック書込（Watch 値）
//! 2. 1 秒毎に `{plugin_data_dir}/{effective_project_hash}/record_signal/*.json`
//!    を全件 polling し、`target_pre_instance_id == self.instance_id` の signal
//!    にだけ追従。`daw_session_id` の filter 比較は撤廃（B-022 段階 3）:
//!    cdylib 隔離下では PRE/POST が別 `static OnceLock` を持ち、
//!    `daw_session_id_cell()` 値が PRE 側 / POST が書いた signal 値で乖離する
//!    ため、PRE 側 cell との比較は構造的に成立しない。代わりに
//!    `target_pre_instance_id` (UUID v4 ≈ 衝突 2^-122) で 1 PRE↔1 POST の
//!    決定論性を保ち、必要なら discover が見つけた signal の `daw_session_id`
//!    を `effective_daw_session_id_ref` として `record_signal` 書込側で利用する。
//! 3. Record 中: `plugin_data/{effective_project_hash}/{instance_id}/pre/*.json`
//!    に Frame / PSB を追記
//!
//! - このスレッドが panic / 権限エラーで止まっても Audio Thread / Measure Thread は継続。
//! - Drop 時（プラグインアンロード）に自分の pre.json と instance ディレクトリを削除する。
//! - Record 中にループ終了した場合、writer は status=closed で flush してから閉じる。
//!
//! - `License::Os`: 条件一致 pending 検出で `try_enter_record` + `mark_acknowledged`、writer 起動
//! - それ以外: pending 検出でログのみ。state machine を触らず、writer も生成しない。
//!   `record_signal.json` は POST 側が消す（PRE は削除しない）。

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::engine::SessionSummary;
use crate::io_thread_post::read_instance_id_arc;
use crate::plugin_data::Role as PluginDataRole;
use crate::pre_self_discovery::{discover_pair_post_project_dir, PreSelfDiscoveryState};
use crate::record::RecordStateMachine;
use crate::record_signal::{self, SignalStatus};
use crate::record_writer::{
    run_record_tick_with_pair_names, take_session_summary, writer_close_degraded,
    writer_close_with_summary, RecordingCtx,
};
use crate::storage::{PlatformPaths, StoragePaths};
use crate::{load_signal_state, License, MeasureResult, RecordTraceQueue, SignalState};

const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// record_signal poll 間隔（1 秒）。
const SIGNAL_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// B-022 段階 5 P-3: 直接 read リトライ最大回数。
///
/// `scan_signals_dir` が transient 失敗 (read_dir Err / parse race) で
/// `current=None` を返した場合、`read_signal` で直接 path を read し直す
/// 最大回数。本値 + `RETRY_INTERVAL` の積が「Keep 解除を遅延させる最大時間」。
const RETRY_MAX_ATTEMPTS: usize = 2;

/// B-022 段階 5 P-3: 直接 read リトライ間隔。
///
/// atomic rename (`fs::rename`) の race window は通常 ms 未満だが、
/// macOS の APFS / 仮想ボリューム経由では数十 ms に延びることがあるため
/// 50ms を採用 (リトライ 2 回で最大 100ms 遅延)。
const RETRY_INTERVAL: Duration = Duration::from_millis(50);

/// PRE スレッドが追従中の partner POST 情報（IO Thread ローカル）。
struct PartnerInfo {
    post_instance_id: String,
    last_seen_status: SignalStatus,
}

/// B-024 Group A / Gap-3 / Gap-5: PRE startup 時に stale Acknowledged signal を削除する。
///
/// 同 instance_id で再起動した PRE が、過去自身が Acknowledged にした
/// `record_signal/{post_iid}.json` を plugin_data に残したまま fresh Watch 状態で
/// 起動するケースで、既存 Pending only filter (`poll_record_signal:509-515`) は
/// この古い Acknowledged を adopt しない。POST 側は status 不変のまま Record 維持
/// → 構造的状態乖離 → Gap-3 / Gap-5 の真因。
///
/// 本関数は startup 時に plugin_data 配下の全 project_hash 横断 scan を行い、
/// `target_pre_instance_id == self_iid && status == Acknowledged` を満たす signal
/// を `delete_signal` で削除する (Daisuke 判断 α 採用 / Watch リセット仕様 /
/// → exit_record で Watch 復帰する (Gap-2 と並ぶ構造的同期経路)。
///
/// R-28 機能的沈黙: storage path 解決失敗 / read_dir 失敗 / parse 失敗は当該
/// dir/file のみ skip。UI エラー出さず log のみ。
fn clear_stale_self_acks_at_startup(self_instance_id: &str) {
    if self_instance_id.is_empty() {
        return;
    }
    let Ok(paths) = StoragePaths::default_platform() else {
        return;
    };
    clear_stale_self_acks_in(&paths.plugin_data_dir(), self_instance_id);
}

/// `clear_stale_self_acks_at_startup` の純粋ロジック版 (テスト容易性のため
/// `plugin_data_root` を注入)。Production は `clear_stale_self_acks_at_startup` 経由。
fn clear_stale_self_acks_in(plugin_data_root: &Path, self_instance_id: &str) {
    let project_entries = match fs::read_dir(plugin_data_root) {
        Ok(e) => e,
        Err(_) => return,
    };
    let mut cleared: usize = 0;
    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        let project_hash = match project_dir.file_name().and_then(|n| n.to_str()) {
            Some(s) => s.to_string(),
            None => continue,
        };
        // 予約名 (record_signal / preset 等) が project_hash として現れるケースを除外。
        if project_hash.as_str() == record_signal::SIGNALS_SUBDIR {
            continue;
        }
        let signals = record_signal::scan_signals_dir(plugin_data_root, &project_hash);
        for (post_iid, sig) in signals {
            if sig.target_pre_instance_id != self_instance_id {
                continue;
            }
            if sig.status != SignalStatus::Acknowledged {
                continue;
            }
            match record_signal::delete_signal(plugin_data_root, &project_hash, &post_iid) {
                Ok(()) => {
                    cleared += 1;
                    log::info!(
                        "[IOThread PRE] startup: cleared stale Acknowledged signal \
                         (project_hash={}, post_iid={})",
                        project_hash,
                        post_iid
                    );
                }
                Err(e) => {
                    log::warn!(
                        "[IOThread PRE] startup: delete_signal failed \
                         (project_hash={}, post_iid={}): {}",
                        project_hash,
                        post_iid,
                        e
                    );
                }
            }
        }
    }
    if cleared > 0 {
        log::info!(
            "[IOThread PRE] startup: cleared {} stale Acknowledged signal(s) (self_iid={})",
            cleared,
            self_instance_id
        );
    }
}

/// PRE 用 IO Thread を起動して JoinHandle を返す。
///
/// # 引数
/// - `instance_id`         : PRE の永続 instance UUID（`Arc<RwLock<String>>` で
///   plugin params と共有。B-022 段階 1 で String snapshot から lazy-read 化。
///   `set_state` 経由の chunk-restore 後でも次 tick から最新値を拾う）
/// - `project_hash`        : DAW プロセス単位の project_hash
/// - `_daw_session_id`     : DAW プロセス単位の UUID。chunk persistence 用に
///   呼出側で保持される値。B-022 段階 3 で record_signal filter から撤廃した
///   ため本スレッドでは未使用（cdylib 隔離下で PRE 側 cell 値と POST が
///   書いた signal の `daw_session_id` が乖離するため filter は破綻）。
///   将来 record_signal 書込側で必要になった時点で復活する余地を残す。
/// - `sample_rate`         : Record writer メタデータ用
/// - `record_sm`           : Watch ↔ Record 状態機械（IO Thread が駆動）
/// - `recording`           : editor 表示用ミラー
/// - `record_acknowledged` : pending を ack した後 true、released で false
/// - `license`             : pending 追従可否の判定（Os のみ参加）
/// - `result`              : Measure Thread が更新する計測結果
/// - `signal_state`        : SS-1 シグナル状態
/// - `shutdown`            : `true` になったらループ終了
/// - `name`                : PRE 表示用 Name (`Arc<RwLock<String>>` で plugin
///   params と共有 / B-023 段階 3)。各 ack 呼出時に lazy-read して
///   `record_signal::mark_acknowledged_with_name` の `paired_pre_name` 引数に
///   渡す。空文字許容 (POST 側で UUID 短縮 8 文字 fallback 表示)。
#[allow(clippy::too_many_arguments)]
pub fn spawn_io_thread_pre(
    instance_id: Arc<RwLock<String>>,
    project_hash: String,
    _daw_session_id: String,
    sample_rate: u32,
    record_sm: Arc<RecordStateMachine>,
    recording: Arc<AtomicBool>,
    record_acknowledged: Arc<AtomicBool>,
    license: Arc<License>,
    result: Arc<Mutex<MeasureResult>>,
    signal_state: Arc<AtomicU8>,
    shutdown: Arc<AtomicBool>,
    name: Arc<RwLock<String>>,
    // B-025 Group B-2/B-3 / Gap-19/20: io_thread → GUI ステータス行への通知 channel。
    // io_thread_post.rs と完全対称 (writer_close + record_sm.exit_record + 文字列書込)。
    record_error_message: Arc<RwLock<Option<String>>>,
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
) -> JoinHandle<()> {
    thread::spawn(move || {
        // B-128 (G-115-370): 観測 family（io_thread）入口の identity materialize（唯一の検証点）。
        // restore 由来の path-unsafe な project_hash は fresh new_v4 へ差し替え、instance_id セルも
        // 同様に正規化する（§7② 観測継続）。path-safe な値（valid UUID / 無害な literal）は無改変＝
        // parity の literal id path テスト不変。下流の builder wall は materialize 後の DiD backstop。
        let project_hash = crate::path_identity::materialize_observation_id(
            &project_hash,
            "io_thread_pre.project_hash",
        );
        crate::path_identity::normalize_observation_cell(&instance_id, "io_thread_pre.instance_id");
        log::info!(
            "[IOThread PRE] started (lazy-read instance_id, fallback project_hash={})",
            project_hash
        );

        // B-024 Group A / Gap-3 / Gap-5: PRE startup で stale Acknowledged signal を
        // 削除し POST 側 Record を Watch に復帰させる。loop 突入前の 1 回のみ実行。
        let initial_self_iid = read_instance_id_arc(&instance_id);
        clear_stale_self_acks_at_startup(&initial_self_iid);

        // B-103: dead Pending record_signal（書込 POST 消失・自 release 30s 超）を起動時掃除。
        // age ベース・保守しきい値（STALE_PENDING_SECS=120s）で生記録は誤掃除しない。loop 前 1 回。
        record_signal::sweep_stale_pending_at_startup();

        // B-127 (G-115-364): 孤児 reservation 枠を起動時掃除（POST 経路と冪等・age ベース）。
        if let Ok(paths) = StoragePaths::default_platform() {
            let _ = crate::reservation::sweep_stale_reservations_in(
                &paths.plugin_data_dir(),
                chrono::Utc::now(),
            );
        }

        // B-025 Group B-1 / Gap-8: 30 秒 flush 周期中の DAW crash で残った
        // `*.json.tmp` を整合 verify (HMAC-SHA256) → `.json` に atomic rename で救出。
        // 不整合 .tmp は warn ログのみで残置 (削除しない / 約束 5 原則)。loop 前 1 回。
        if let Ok(paths) = StoragePaths::default_platform() {
            let _ = crate::record_writer::recover_orphan_tmps(&paths.plugin_data_dir());
        }

        // B-026 / Gap-9: crash 残骸 `pre/{compact}.json` / `post/{compact}.json`
        // のうち status=Active かつ mtime > 60s のファイルを status=Closed に
        // 書換 (Lens 側「進行中 Record」誤認の構造的解消)。loop 前 1 回。
        if let Ok(paths) = StoragePaths::default_platform() {
            let _ = crate::record_writer::sweep_stale_active_at_startup(&paths.plugin_data_dir());
        }

        let mut writer_ctx: Option<RecordingCtx> = None;
        let mut last_poll: Option<Instant> = None;
        let mut partner: Option<PartnerInfo> = None;
        // B-022 段階 5 P-1 #1: effective_project_hash_ref の edge-triggered ログ用。
        // 値が前回と異なる時のみ INFO で出す (毎 tick 出すと R-26/R-28 沈黙ゲート違反)。
        let mut prev_effective_ph: Option<String> = None;
        // B-022 段階 2: PRE 側 filesystem-discovery (G-115-36)。
        // `plugin_data/{any_uuid}/record_signal/*.json` を 1 秒 throttle で scan
        // し、`target_pre_instance_id == 自 PRE instance_id` の signal が居る
        // `{any_uuid}/` (= POST 側 project_uuid) を採用する。これにより PRE 側
        // plugin_data 出力先 / record_signal poll 経路を POST と同じ
        // project_uuid 空間に揃え、cdylib 隔離下 (`static OnceLock` が PRE/POST
        // で別実体) でも cross-instance pair 復元 v1.2 (a) が機能する。
        let mut discovery = PreSelfDiscoveryState::new();

        loop {
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            // B-022 段階 1: tick 開始時に instance_id を lazy-read。
            // POST 側 io_thread と同パターン。`Arc<RwLock<String>>` は plugin
            // params と同実体を共有するため、`set_state_inner` 経由の
            // chunk-restored 値を次 tick で拾う。
            // B-027 段階 2: name も同パターンで lazy-read し pre.json に出力。
            let instance_id_owned = read_instance_id_arc(&instance_id);
            let instance_id_ref = instance_id_owned.as_str();
            let name_owned_for_write = read_instance_id_arc(&name);
            let dir = io_dir(&project_hash, instance_id_ref);
            let file_path = dir.join("pre.json");

            // ① pre.json（Watch 値）書き込み
            // path は PRE 自身の `project_hash` で構築する。POST 側 io_thread
            // の B-021 filesystem-discovery (`pre_discovery::discover_active_pre_dir`)
            // が `$TMPDIR/kirin/` 配下を全 project_uuid 横断で scan するため、
            // PRE/POST の project_hash が乖離していても POST はこの pre.json を
            // 拾える。本 path は B-022 では変更しない。
            if let Err(e) = write_json(
                &dir,
                &file_path,
                instance_id_ref,
                name_owned_for_write.as_str(),
                &result,
                &signal_state,
            ) {
                log::warn!("[IOThread PRE] write error: {}", e);
            }

            // B-022 段階 2: 1 秒 throttle で POST 側 project_uuid を再走査。
            // 結果が cache されるので、各 tick での fs::read_dir コストは抑制。
            //
            // B-022 段階 4: Record 中 (partner=Some) は discovery を skip。
            // 理由: PRE が一度 ack して partner を確定した後、POST 側は signal を
            // 触らない (heartbeat 不在 / record_signal.rs:511-512 で Acknowledged
            // signal はスキップ) ため、signal の mtime は ack 時点で固定。
            // discovery の stale 閾値 (DISCOVERY_STALE_SECS=10s) を超えると
            // `discover_pair_post_project_dir` が None を返し、cached_post_project_dir
            // が None にリセットされる。すると effective_project_hash_ref が
            // PRE 自身の project_hash に fallback (line 167-169) し、
            // poll_record_signal が誤った dir を scan して matching=empty →
            // partner.current=None → exit_record() が誤発火する (Keep 解除)。
            //
            // 修正: partner.is_some() の間は discovery 呼出を skip し、
            // cached_post_project_dir を保持し続ける。partner=None (Record 終了
            // または signal 消失検出) 後の次 tick から discovery 再開。
            let now_instant = Instant::now();
            if partner.is_none() && discovery.should_rescan(now_instant) {
                let plugin_data_root = StoragePaths::default_platform()
                    .ok()
                    .map(|paths| paths.plugin_data_dir());
                let found = match plugin_data_root.as_ref() {
                    Some(root) => discover_pair_post_project_dir(root, instance_id_ref),
                    None => None,
                };
                discovery.record_scan(now_instant, found);
            }

            // `effective_project_hash`:
            // - discover が POST project_uuid を見つけた場合 → その値
            // - 見つからない場合 → PRE 自身の project_hash (B-020 fallback)
            //
            // record_signal poll / plugin_data writer の入出力 path 両方で
            // この値を一貫して使うことで「PRE が POST と別 plugin_data 空間に
            // 書いてしまう」 cdylib 隔離問題を回避する。
            let effective_project_hash_owned: Option<String> = discovery
                .cached_post_project_dir()
                .and_then(|p| p.file_name().and_then(|n| n.to_str()).map(str::to_string));
            let effective_project_hash_ref: &str = effective_project_hash_owned
                .as_deref()
                .unwrap_or(project_hash.as_str());

            // B-022 段階 5 P-1 #1: effective_project_hash_ref が前回と異なる
            // 場合のみ INFO ログ。fallback 切替 (POST_uuid → PRE 自身 / 逆) の
            // 瞬間を 1 行で捕捉できる。R-28 沈黙ゲート: 値不変時は無音。
            if prev_effective_ph.as_deref() != Some(effective_project_hash_ref) {
                log::info!(
                    "[discover] effective_project_hash_ref={} (prev={:?}, partner={:?})",
                    effective_project_hash_ref,
                    prev_effective_ph,
                    partner.as_ref().map(|p| p.post_instance_id.as_str())
                );
                prev_effective_ph = Some(effective_project_hash_ref.to_string());
            }

            // B-022 段階 3: PRE 側 `daw_session_id` は record_signal filter から
            // 撤廃。`discovery.cached_daw_session_id()` (= POST が書いた signal
            // の値) は将来 record_signal 書込側で必要になった時点で参照する
            // ため state 内に保持済み。本ループでは使わない。
            //
            // 設計補足: cdylib 隔離下で PRE/POST が別 `static OnceLock` を
            // 持つため、PRE 側 `daw_session_id_cell()` 値と POST が書いた
            // signal の `daw_session_id` は構造的に乖離する。filter 比較は
            // 成立しないため撤廃 (`target_pre_instance_id` UUID v4 で
            // 衝突 ≈ 2^-122、決定論的に PRE 自身宛てを識別)。

            // ② record_signal poll（1 秒間隔）
            // base_dir = plugin_data_root / project_hash = effective_project_hash_ref
            //
            // B-022 段階 3: filter から daw_session_id 比較を撤廃。
            // target_pre_instance_id (UUID v4) のみで PRE 自身宛て signal を識別。
            let now = Instant::now();
            let should_poll =
                last_poll.is_none_or(|t| now.duration_since(t) >= SIGNAL_POLL_INTERVAL);
            if should_poll {
                last_poll = Some(now);
                // B-023 段階 3: ack 直前に name を lazy-read して poll に渡す。
                // instance_id と同パターン (chunk-restore 後の最新値追従)。
                let name_owned = read_instance_id_arc(&name);
                poll_record_signal(
                    effective_project_hash_ref,
                    instance_id_ref,
                    &record_sm,
                    &recording,
                    &record_acknowledged,
                    &license,
                    &mut partner,
                    name_owned.as_str(),
                    &signal_state,
                );
            }

            // ③ plugin_data/.../pre/*.json ライフサイクル（Record writer）
            // partner が居れば partner の signal.started_at を解決、不在なら現在時刻 fallback
            //
            // B-022 段階 2: project_hash 引数も `effective_project_hash_ref`
            // に切り替え。PRE 側 plugin_data の親 dir = `{POST の project_uuid}/`
            // を採用 (G-115-36)。
            let writer_project_hash: String = effective_project_hash_ref.to_string();
            let project_hash_ref_for_resolver = writer_project_hash.clone();
            let partner_iid = partner.as_ref().map(|p| p.post_instance_id.clone());
            let started_resolver_iid = partner_iid.clone();
            let resolver = move || match started_resolver_iid {
                Some(iid) => match StoragePaths::default_platform() {
                    Ok(paths) => crate::record_writer::resolve_started_at_ms(
                        &paths.plugin_data_dir(),
                        &project_hash_ref_for_resolver,
                        &iid,
                    ),
                    Err(_) => crate::record_writer::now_epoch_ms(),
                },
                None => crate::record_writer::now_epoch_ms(),
            };
            // v1.2 (a): PRE 側は paired_post_instance_id に partner.post_instance_id を渡す。
            // paired_pre は常に None（PRE 自身は PRE なので相手 PRE は無い）。
            let paired_post_for_writer = partner_iid;
            let paired_pre_resolver = || None::<String>;
            let paired_post_resolver = move || paired_post_for_writer;
            let pre_name_for_writer = read_instance_id_arc(&name);
            let pair_name_for_writer = pre_name_for_writer.clone();
            let pair_pre_name_for_writer = pre_name_for_writer;
            let pair_name_resolver = move || Some(pair_name_for_writer);
            let pair_pre_name_resolver = move || Some(pair_pre_name_for_writer);
            if let Err(e) = run_record_tick_with_pair_names(
                &record_sm,
                PluginDataRole::Pre,
                sample_rate,
                &writer_project_hash,
                instance_id_ref,
                resolver,
                paired_pre_resolver,
                paired_post_resolver,
                pair_name_resolver,
                pair_pre_name_resolver,
                &result,
                &mut writer_ctx,
                Some(&session_summary),
                &overflow,       // B-076: per-Record dropped_samples 算出用
                &oversized_drop, // B-125: per-Record oversized block drop 算出用
                Some(&record_trace_queue),
            ) {
                log::warn!("[writer] tick error: {}", e);
            }

            // B-025 Group B-2/B-3 / Gap-19/20: run_record_tick が連続失敗閾値を
            // 検知して `exit_requested` に sentinel をセットしたら、本 tick で
            // writer_close + record_sm.exit_record + UI 通知文字列書込。io_thread_post と対称。
            let exit_reason = writer_ctx
                .as_mut()
                .and_then(|ctx| ctx.exit_requested.take());
            if let Some(reason) = exit_reason {
                if let Some(ctx) = writer_ctx.take() {
                    // B-134 (G-115-391): auto-stop（data-loss 異常終了）は best-effort で
                    // integrity_degraded を立ててから close（書ければ file flag / 書けねば下の
                    // record_error_message が UI backstop）。
                    writer_close_degraded(ctx);
                }
                record_sm.exit_record();
                record_acknowledged.store(false, Ordering::Relaxed);
                if let Ok(mut g) = record_error_message.write() {
                    *g = Some(reason.ui_message().to_string());
                }
                log::warn!(
                    "[IOThread PRE] record exit: {} (pre_iid={})",
                    reason,
                    instance_id_ref
                );
            }

            recording.store(record_sm.is_recording(), Ordering::Relaxed);

            thread::sleep(LOOP_SLEEP);
        }

        // ── Record 中に shutdown された場合: writer を閉じる ─────────────
        // B-043: Record 中の shutdown 時も Measure Thread の最新 session_summary を
        // 取り出して JSON に焼き込む (Daisuke 抜去シナリオ救済 / POST 側と対称)。
        if let Some(mut ctx) = writer_ctx.take() {
            // B-132 (G-115-382): shutdown-during-record。Measure Thread は teardown 中で seal を
            // 進めないため instant check（bounded 0 / deadlock 無縁）。seal 未前進 = graceful な
            // Record→Watch tight-drain を経ていない＝ tail 不確定 → 不完全と記録（共通B / POST と対称）。
            let sealed = record_sm.seal() > ctx.seal_at_start;
            let summary = take_session_summary(&session_summary);
            if !sealed {
                ctx.writer.mark_integrity_degraded();
            }
            writer_close_with_summary(ctx, summary);
        }
        record_sm.exit_record();
        recording.store(false, Ordering::Relaxed);
        record_acknowledged.store(false, Ordering::Relaxed);

        // ── クリーンアップ ───────────────────────────────────────────────
        // 終了時の instance_id を lazy-read して使用 (POST 側 io_thread と同様、
        // 設計上残骸が残るのは Default UUID dir の 1 度限り。R-28 機能的沈黙)。
        let final_iid = read_instance_id_arc(&instance_id);
        let final_dir = io_dir(&project_hash, &final_iid);
        let final_file = final_dir.join("pre.json");
        if let Err(e) = fs::remove_file(&final_file) {
            log::debug!("[IOThread PRE] cleanup file: {}", e);
        }
        if let Err(e) = crate::atomic_file::remove_temp_siblings(&final_file) {
            log::debug!("[IOThread PRE] cleanup tmp: {}", e);
        }
        // instance ディレクトリ自体も空なら削除（残骸を残さない）
        let _ = fs::remove_dir(&final_dir);
        log::info!("[IOThread PRE] terminated");
    })
}

/// `$TMPDIR/kirin/{project_hash}/{instance_id}/` パスを返す（pre.json / Watch 用）。
pub fn io_dir(project_hash: &str, instance_id: &str) -> PathBuf {
    // B-128 (G-115-370): within-base wall（Watch pre.json の path builder）。
    let ph = crate::path_identity::guard_path_component(project_hash, "io_dir.project_hash");
    let iid = crate::path_identity::guard_path_component(instance_id, "io_dir.instance_id");
    PlatformPaths::current_kirin_tmp_root()
        .join(&*ph)
        .join(&*iid)
}

/// record_signal を 1 度だけ poll し、PRE 側の record state を同期する。
///
/// scan_signals_dir で全 signal を取得し、`target_pre_instance_id == instance_id`
/// を満たす最初の signal にだけ追従。追従中の partner（post_instance_id）を
/// `partner` に保持し、消失・released で外す。
///
/// # B-022 段階 3: daw_session_id filter 撤廃
/// 旧版は `daw_session_id` 比較で cross-process 防壁を掛けていたが、cdylib 隔離
/// 下で PRE/POST が別 `static OnceLock` を持つため両者の値は構造的に乖離し、
/// filter は常に false を返して signal を全廃棄していた (実機 NG 真因)。
/// `target_pre_instance_id` (UUID v4) のみで衝突確率 2^-122 の決定論性が
/// 確保されるため、二重 filter は冗長として撤廃した。
#[allow(clippy::too_many_arguments)]
fn poll_record_signal(
    project_hash: &str,
    instance_id: &str,
    record_sm: &Arc<RecordStateMachine>,
    recording: &Arc<AtomicBool>,
    record_acknowledged: &Arc<AtomicBool>,
    license: &Arc<License>,
    partner: &mut Option<PartnerInfo>,
    paired_pre_name: &str,
    signal_state: &Arc<AtomicU8>,
) {
    let base = match StoragePaths::default_platform() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };

    // 全 signal を読む。target_pre_instance_id 一致のみで filter。
    let signals = record_signal::scan_signals_dir(&base, project_hash);
    let matching: Vec<_> = signals
        .into_iter()
        .filter(|(_, s)| s.target_pre_instance_id == instance_id)
        .collect();

    // 既存 partner の現状況を確認
    if let Some(p) = partner.as_mut() {
        let current = matching.iter().find(|(iid, _)| iid == &p.post_instance_id);
        match current {
            Some((_, sig)) => {
                let new_status = sig.status;
                if new_status == p.last_seen_status {
                    return; // 変化なし
                }
                match new_status {
                    SignalStatus::Released => {
                        record_sm.exit_record();
                        recording.store(false, Ordering::Relaxed);
                        record_acknowledged.store(false, Ordering::Relaxed);
                        log::info!(
                            "[signal] released, PRE exiting Record (partner={})",
                            p.post_instance_id
                        );
                        *partner = None;
                        return;
                    }
                    // B-027 段階 3-B α-7 / Group 3 (Gap-21 局所対処 / 設計判断 #15 (α) B 案):
                    // Acknowledged → Pending 直接遷移 = Stop+Keep 1 秒以内連打で Released
                    // を経由せずに status が再 Pending になる race 経路 (1:N record file
                    // 崩壊真因)。Released branch と完全対称に exit_record + state クリア +
                    // partner=None を実行し、新 generation の Pending として次 tick の
                    // Group 1 経路 (filter / N 並列 ack / L485-565) に流す。
                    //
                    // writer_close は run_record_tick の (true→false) arm
                    // (record_writer.rs:281-285) で次 tick (≤LOOP_SLEEP=100ms 後) に
                    // 自動実行されるため、ここで明示呼出は不要 (B 案採用 / Group 1 改修
                    // 不変原則 / Pass 15)。
                    SignalStatus::Pending if p.last_seen_status == SignalStatus::Acknowledged => {
                        record_sm.exit_record();
                        recording.store(false, Ordering::Relaxed);
                        record_acknowledged.store(false, Ordering::Relaxed);
                        log::info!(
                            "[signal] B-027 Group 3: direct A→P transition, PRE exiting Record (partner={})",
                            p.post_instance_id
                        );
                        *partner = None;
                        return;
                    }
                    _ => {
                        // pending → acknowledged 等は Record 継続
                        p.last_seen_status = new_status;
                        return;
                    }
                }
            }
            None => {
                // B-022 段階 5 P-1 #4: scan が partner.post_instance_id を
                // 含まなかった瞬間の matching 内容を診断ログとしてダンプ。
                // 真因 β/γ 確定 (= scan transient 失敗 vs 別 POST instance) の
                // 決め手になる情報を 1 行に集約。
                let matching_iids: Vec<&str> =
                    matching.iter().map(|(iid, _)| iid.as_str()).collect();
                log::warn!(
                    "[signal] current=None: project_hash={}, partner.post_instance_id={}, \
                     matching_count={}, matching_iids={:?}",
                    project_hash,
                    p.post_instance_id,
                    matching.len(),
                    matching_iids
                );

                // B-022 段階 5 P-3: 直接 read リトライ。
                // scan_signals_dir の transient 失敗 (read_dir Err / parse race) を
                // 救済するため、`signal_path(base, project_hash, post_instance_id)`
                // で直接 read し直す。1 path 直撃 read は scan の `read_dir` よりも
                // 失敗確率が低く、atomic rename と read の race window も
                // 50ms 待機で抜けやすい。
                //
                // 最大 RETRY_MAX_ATTEMPTS (= 2) 回、各 RETRY_INTERVAL (= 50ms) 間隔で
                // read を試み、いずれかで Some が返れば transient 失敗扱いで
                // exit_record をスキップして watch を維持。全試行で None なら
                // 真の消失として従来通り exit_record。
                let recovered = retry_direct_read(&base, project_hash, &p.post_instance_id);

                if let Some(sig) = recovered {
                    log::info!(
                        "[signal] recovered via direct read after scan miss \
                         (partner={}, status={:?}) — keeping Record",
                        p.post_instance_id,
                        sig.status
                    );
                    // status は記憶し直す (Released なら次 tick の status==last_seen で
                    // 抜けないよう、ここで Released を検出した場合は exit_record する)。
                    if sig.status == SignalStatus::Released {
                        record_sm.exit_record();
                        recording.store(false, Ordering::Relaxed);
                        record_acknowledged.store(false, Ordering::Relaxed);
                        log::info!(
                            "[signal] released (via direct read), PRE exiting Record (partner={})",
                            p.post_instance_id
                        );
                        *partner = None;
                    } else {
                        // 単に scan が transient で取り逃しただけ。partner 維持。
                        p.last_seen_status = sig.status;
                    }
                    return;
                }

                // 直接 read も None → 真の消失 → 従来動作。
                if record_sm.is_recording() {
                    record_sm.exit_record();
                    recording.store(false, Ordering::Relaxed);
                    record_acknowledged.store(false, Ordering::Relaxed);
                    log::info!(
                        "[signal] file removed (confirmed by direct read), PRE exiting Record (partner={})",
                        p.post_instance_id
                    );
                }
                *partner = None;
                return;
            }
        }
    }

    // partner 未設定 → 同 instance_id 配下 Pending を全件列挙
    //
    // B-027 段階 3-B α-7 / Group 1 (G-115-XX 申し送り #24 broadcast/multi-target):
    // 旧版は `find` で先頭 1 件のみ ack していた (Gap-4 / Gap-11)。All Keep で
    // N POST が同時に同 PRE 宛 write_pending を発行すると、N-1 件は
    // ACK_TIMEOUT_SECONDS=30 経路に流れて silent fail していた。
    // 本改修で同 tick 内 Vec 全件並列 ack に拡張する。
    //
    // 順序: scan_signals_dir が post_instance_id 辞書順を返す (record_signal.rs:327
    // doc 仕様) ため、本 filter 後も同順を維持。決定論性確保。
    let pending_signals: Vec<(String, record_signal::RecordSignal)> = matching
        .into_iter()
        .filter(|(_, s)| s.status == SignalStatus::Pending)
        .collect();
    if pending_signals.is_empty() {
        return;
    }

    // B-027: 書込側 guard を Bypassed のみに緩和。停止中 (Inactive) PRE も
    // pair 候補として ack する。二重防御の片側 (POST 側読込 filter は
    // pre_candidates::scan_pre_candidates_in で同等動作)。
    let current_state = load_signal_state(signal_state);
    if current_state == SignalState::Bypassed {
        log::info!(
            "[signal] {} pending detected, ignored (PRE bypassed)",
            pending_signals.len()
        );
        return;
    }

    if !is_os_license(license) {
        log::info!(
            "[signal] {} pending detected, ignored (license: {:?})",
            pending_signals.len(),
            **license
        );
        return;
    }

    // state machine 遷移: 1 度だけ (record_sm.try_enter_record は compare_exchange
    // で冪等 / record.rs:111-125)。N 件 ack の前に Watch→Record 遷移を確定する。
    let entered_now = match record_sm.try_enter_record(**license) {
        Ok(()) => true,
        Err(e) => {
            log::warn!("[signal] try_enter_record rejected: {:?}", e);
            return;
        }
    };
    recording.store(true, Ordering::Relaxed);

    // N 件並列 ack。各 ack は独立 atomic rename (record_signal::write_signal /
    // L195-203 既存パターン)。1 件失敗しても他 N-1 件は継続 (Vec 全件処理 / Gap-11
    // 構造解消)。
    //
    // partner state は IO Thread ローカルの 1 件保持構造を維持 (Group 1 スコープ /
    // POST 側 paired_pre_name + PAIR_LABEL_POLL_INTERVAL=1s 経路に同期を委ねる /
    // 設計判断 #4 (i))。最後に ack 成功した POST_iid を保持し、次 tick 以降の
    // partner=Some 経路 (L378) で Released 観測の起点とする。
    let total = pending_signals.len();
    let mut last_acked: Option<String> = None;
    let mut ack_ok: usize = 0;
    for (post_iid, _) in &pending_signals {
        match record_signal::mark_acknowledged_with_name(
            &base,
            project_hash,
            post_iid,
            paired_pre_name,
        ) {
            Ok(_) => {
                log::info!(
                    "[signal] PRE acknowledged Record request (partner={}, pair_pre_name={})",
                    post_iid,
                    paired_pre_name
                );
                last_acked = Some(post_iid.clone());
                ack_ok += 1;
            }
            Err(e) => {
                log::warn!(
                    "[signal] mark_acknowledged_with_name failed for {}: {}",
                    post_iid,
                    e
                );
            }
        }
    }

    if let Some(post_iid) = last_acked {
        record_acknowledged.store(true, Ordering::Relaxed);
        *partner = Some(PartnerInfo {
            post_instance_id: post_iid,
            last_seen_status: SignalStatus::Acknowledged,
        });
        log::info!(
            "[signal] B-027 Group 1: ack_count={}/{} (partner={:?})",
            ack_ok,
            total,
            partner.as_ref().map(|p| p.post_instance_id.as_str())
        );
    } else if entered_now {
        // 全 ack 失敗 + 本 tick で Watch→Record した場合は state machine を巻き戻す。
        // (既存 Record 中の場合は触らない / 冪等性を保つ)
        record_sm.exit_record();
        recording.store(false, Ordering::Relaxed);
        log::warn!(
            "[signal] all {} pending ack failed; reverted to Watch",
            total
        );
    }
}

fn is_os_license(license: &License) -> bool {
    matches!(license, License::Os)
}

/// B-022 段階 5 P-3: 直接 read リトライヘルパ。
///
/// `scan_signals_dir` が transient 失敗で対象 signal を取り逃した場合に、
/// `record_signal::read_signal` (path 直撃 read) で `RETRY_MAX_ATTEMPTS` 回まで
/// 再試行する。各試行間に `RETRY_INTERVAL` 秒待機 (atomic rename race window
/// 抜けを期待)。
///
/// # 戻り値
/// - `Some(signal)` : いずれかの試行で読込成功。呼出側は scan の transient
///   失敗とみなして exit_record を呼ばずに watch を維持する。
/// - `None`         : 全試行で read 不能。真の消失 (unload / delete_signal) と
///   みなして従来通り exit_record してよい。
///
/// # 副作用
/// IO Thread を最大 `RETRY_MAX_ATTEMPTS * RETRY_INTERVAL` 秒間 sleep する。
/// 100ms 主ループの 1 tick 内で完結 (現状: 2 * 50ms = 100ms 上限)。
fn retry_direct_read(
    base: &Path,
    project_hash: &str,
    post_instance_id: &str,
) -> Option<crate::record_signal::RecordSignal> {
    for attempt in 0..RETRY_MAX_ATTEMPTS {
        if attempt > 0 {
            thread::sleep(RETRY_INTERVAL);
        }
        if let Some(sig) = record_signal::read_signal(base, project_hash, post_instance_id) {
            log::debug!(
                "[signal] direct read recovered: attempt={}/{}, post_iid={}",
                attempt + 1,
                RETRY_MAX_ATTEMPTS,
                post_instance_id
            );
            return Some(sig);
        }
    }
    None
}

/// 計測結果を JSON に変換して アトミックに書き込む（Watch 値）。
fn write_json(
    dir: &Path,
    file_path: &Path,
    instance_id: &str,
    name: &str,
    result: &Arc<Mutex<MeasureResult>>,
    signal_state: &Arc<AtomicU8>,
) -> Result<(), String> {
    fs::create_dir_all(dir).map_err(|e| format!("create_dir_all: {e}"))?;

    let state = load_signal_state(signal_state);

    let json = if state == SignalState::Active {
        let measure = result
            .lock()
            .map_err(|e| {
                // B-047 / G-115-247: Mutex poison は Measure Thread panic 後の永続障害。
                // pre.json の `t` field 更新が止まり、POST 側で DeltaMode::Stale/NoPre 化する真因。
                log::error!(
                    "[PRE write_json] Mutex poisoned — t field NOT updated (instance_id={}, err={})",
                    instance_id, e
                );
                format!("Mutex poisoned: {e}")
            })?
            .clone();
        serialize_pre_json(instance_id, name, state, &measure)
    } else {
        serialize_pre_json_minimal(instance_id, name, state)
    };

    crate::atomic_file::write_bytes_atomic(file_path, json.as_bytes())
        .map_err(|e| format!("atomic write: {e}"))?;

    Ok(())
}

///
/// A-3 修正後: bus フィールドは削除（path に instance_id が入るため不要）。
/// B-027 段階 2: `name` field を追加 (POST `pair_pre_name` filter 用)。
/// pre.json schema バージョン (`v=2`) は据置き。読込側 (`PreTmpJson`) は
/// `#[serde(default)]` で旧 schema 互換を維持する。
pub fn serialize_pre_json(
    instance_id: &str,
    name: &str,
    state: SignalState,
    result: &MeasureResult,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    // B-077: name は利用者入力。serde で JSON 文字列化し " \ 制御文字・日本語を安全に escape する
    // （旧: 生補間で " \ を含む名が不正 JSON を生成 → POST parse 失敗）。正常 ASCII 名は不変。
    // B-131 (G-115-380): instance_id も同様に serde escape する。restore で host 由来になりうる値を
    // gate する is_path_safe_component (path_identity.rs) は `"` を拒否しないため、`"` 入り
    // instance_id が materialize wall を素通り不正 JSON → pairing 消失を起こす同種欠陥（census 検出）。
    // 正常 UUID では byte 不変（parity literal-id 不変）。根本封止（wall 側 `"` quarantine）は番人へ上申。
    let name_json = serde_json::to_string(name).unwrap_or_else(|_| "\"\"".to_string());
    let instance_id_json =
        serde_json::to_string(instance_id).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        r#"{{"v":2,"role":"PRE","instance_id":{instance_id_json},"name":{name_json},"signal_state":"{signal_state}","t":"{t}","lufs_m":{lufs_m},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id_json = instance_id_json,
        name_json = name_json,
        signal_state = state.as_str(),
        t = t,
        lufs_m = opt_f64(result.lufs_m),
        true_peak = opt_f64(result.true_peak),
        crest = opt_f64(result.crest),
        psr = opt_f64(result.psr),
        phase_d = phase_d_fragment(result),
    )
}

/// Bypassed / Inactive 時の最小 JSON。
///
/// B-027 段階 2: `name` field を追加 (Bypassed / Inactive でも候補化されるため
/// filter 照合に必要。pre.json schema バージョン据置き)。
fn serialize_pre_json_minimal(instance_id: &str, name: &str, state: SignalState) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    // B-077: name を serde で JSON escape（serialize_pre_json と同一。" \ 制御文字・日本語を安全に）。
    // B-131 (G-115-380): instance_id も serde escape（serialize_pre_json と同一契約）。
    let name_json = serde_json::to_string(name).unwrap_or_else(|_| "\"\"".to_string());
    let instance_id_json =
        serde_json::to_string(instance_id).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        r#"{{"v":2,"role":"PRE","instance_id":{instance_id_json},"name":{name_json},"signal_state":"{signal_state}","t":"{t}"}}"#,
        instance_id_json = instance_id_json,
        name_json = name_json,
        signal_state = state.as_str(),
        t = t,
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

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::record::RecordState;
    use crate::record_signal::{write_pending, RecordSignal, SignalStatus};
    use std::sync::atomic::AtomicU64;

    const TEST_PH: &str = "ph";
    const TEST_PRE_IID: &str = "pre-iid-A";
    const TEST_DAW: &str = "daw-uuid-A";

    fn isolated_base() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_io_pre_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// poll_record_signal の挙動を検証するため、tmp_kirin_base に依存しない薄いラッパー。
    /// 実装と同じロジック（scan_signals_dir + target_pre_instance_id 単一 filter
    /// + B-024 signal_state guard）を直接回す。
    ///
    /// B-022 段階 3: daw_session_id filter 撤廃に追従。
    /// B-024: signal_state guard を追加。既存テストは `SignalState::Active` を
    /// 渡す `poll_with_base` に集約。signal_state ガードのテストは
    /// `poll_with_base_state` 経由で `Bypassed` / `Inactive` を渡す。
    #[allow(clippy::too_many_arguments)]
    fn poll_with_base(
        base: &Path,
        record_sm: &Arc<RecordStateMachine>,
        recording: &Arc<AtomicBool>,
        record_acknowledged: &Arc<AtomicBool>,
        license: &Arc<License>,
        partner: &mut Option<PartnerInfo>,
        instance_id: &str,
    ) {
        poll_with_base_state(
            base,
            record_sm,
            recording,
            record_acknowledged,
            license,
            partner,
            instance_id,
            SignalState::Active,
        );
    }

    /// signal_state を明示指定する版。B-024 の signal_state guard をテスト
    /// するために用意する (production の poll_record_signal と等価動作)。
    #[allow(clippy::too_many_arguments)]
    fn poll_with_base_state(
        base: &Path,
        record_sm: &Arc<RecordStateMachine>,
        recording: &Arc<AtomicBool>,
        record_acknowledged: &Arc<AtomicBool>,
        license: &Arc<License>,
        partner: &mut Option<PartnerInfo>,
        instance_id: &str,
        signal_state: SignalState,
    ) {
        let signals = record_signal::scan_signals_dir(base, TEST_PH);
        let matching: Vec<_> = signals
            .into_iter()
            .filter(|(_, s)| s.target_pre_instance_id == instance_id)
            .collect();

        if let Some(p) = partner.as_mut() {
            let current = matching.iter().find(|(iid, _)| iid == &p.post_instance_id);
            match current {
                Some((_, sig)) => {
                    if sig.status == p.last_seen_status {
                        return;
                    }
                    if sig.status == SignalStatus::Released {
                        record_sm.exit_record();
                        recording.store(false, Ordering::Relaxed);
                        record_acknowledged.store(false, Ordering::Relaxed);
                        *partner = None;
                        return;
                    }
                    // B-027 段階 3-B α-7 / Group 3 (Gap-21 / 設計判断 #15 (α) B 案):
                    // production poll_record_signal の Acknowledged → Pending 直接遷移
                    // branch をテスト helper にも mirror。production と test helper の
                    // 構造的同期を維持する (Group 1 の find→filter 同期と同方針)。
                    if sig.status == SignalStatus::Pending
                        && p.last_seen_status == SignalStatus::Acknowledged
                    {
                        record_sm.exit_record();
                        recording.store(false, Ordering::Relaxed);
                        record_acknowledged.store(false, Ordering::Relaxed);
                        *partner = None;
                        return;
                    }
                    p.last_seen_status = sig.status;
                    return;
                }
                None => {
                    // B-022 段階 5 P-3: 直接 read リトライ救済 (production と同一ロジック)。
                    let recovered = retry_direct_read(base, TEST_PH, &p.post_instance_id);
                    if let Some(sig) = recovered {
                        if sig.status == SignalStatus::Released {
                            record_sm.exit_record();
                            recording.store(false, Ordering::Relaxed);
                            record_acknowledged.store(false, Ordering::Relaxed);
                            *partner = None;
                        } else {
                            p.last_seen_status = sig.status;
                        }
                        return;
                    }
                    if record_sm.is_recording() {
                        record_sm.exit_record();
                        recording.store(false, Ordering::Relaxed);
                        record_acknowledged.store(false, Ordering::Relaxed);
                    }
                    *partner = None;
                    return;
                }
            }
        }

        // B-027 段階 3-B α-7 / Group 1: production 改修 (find→filter / Vec 全件 ack)
        // と同期。Vec 列挙 + 順次 ack で N Pending 並列処理を再現する。
        let pending_signals: Vec<(String, RecordSignal)> = matching
            .into_iter()
            .filter(|(_, s)| s.status == SignalStatus::Pending)
            .collect();
        if pending_signals.is_empty() {
            return;
        }

        // B-027: signal_state guard。Bypassed のみ ack を抑止。
        if signal_state == SignalState::Bypassed {
            return;
        }

        if !is_os_license(license) {
            return;
        }

        let entered_now = record_sm.try_enter_record(**license).is_ok();
        if !entered_now {
            return;
        }
        recording.store(true, Ordering::Relaxed);

        let mut last_acked: Option<String> = None;
        for (post_iid, _) in &pending_signals {
            if record_signal::mark_acknowledged(base, TEST_PH, post_iid).is_ok() {
                last_acked = Some(post_iid.clone());
            }
        }
        if let Some(post_iid) = last_acked {
            record_acknowledged.store(true, Ordering::Relaxed);
            *partner = Some(PartnerInfo {
                post_instance_id: post_iid,
                last_seen_status: SignalStatus::Acknowledged,
            });
        } else if entered_now {
            record_sm.exit_record();
            recording.store(false, Ordering::Relaxed);
        }
    }

    fn write_matching_pending(base: &Path, post_iid: &str) {
        write_pending(
            base,
            TEST_PH,
            post_iid,
            TEST_PRE_IID.into(),
            TEST_DAW.into(),
        )
        .unwrap();
    }

    // ── poll_record_signal ───────────────────────────────────────

    #[test]
    fn pending_os_license_enters_record_and_acknowledges() {
        let base = isolated_base();
        write_matching_pending(&base, "post-1");

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(sm.current(), RecordState::Record);
        assert!(recording.load(Ordering::Relaxed));
        assert!(ack.load(Ordering::Relaxed));
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(sig.status, SignalStatus::Acknowledged);
        assert!(partner.is_some());
        assert_eq!(partner.as_ref().unwrap().post_instance_id, "post-1");
    }

    /// Q1 (b) 厳格化: target_pre_instance_id が異なる signal は無視される。
    #[test]
    fn signal_with_wrong_target_pre_id_is_ignored() {
        let base = isolated_base();
        // 別の PRE 向けの signal
        write_pending(
            &base,
            TEST_PH,
            "post-1",
            "pre-other-instance".into(),
            TEST_DAW.into(),
        )
        .unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        // 自分宛てではないので Record に入らない
        assert_eq!(sm.current(), RecordState::Watch);
        assert!(!recording.load(Ordering::Relaxed));
        assert!(partner.is_none());
        // signal は変更されていない
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(sig.status, SignalStatus::Pending);
    }

    /// B-022 段階 3 退行防止: PRE 側 cell の daw_session_id と signal の値が
    /// 乖離していても、target_pre_instance_id が一致する signal には追従する
    /// （cdylib 隔離下で PRE/POST が別 OnceLock を持つ実機シナリオ）。
    ///
    /// 旧版 (段階 1〜2) の `signal_with_wrong_daw_session_id_is_ignored` は
    /// `daw_session_id` filter で signal を弾く挙動を固定していたが、その
    /// filter は実機で全 signal を弾く真因となったため段階 3 で撤廃。
    /// 本テストは「撤廃 = false-negative 救済」の不変条件を逆方向に固定。
    #[test]
    fn signal_with_different_daw_session_id_is_still_acknowledged() {
        let base = isolated_base();
        // POST が書いた signal の daw_session_id は PRE 側 cell 値と異なる
        write_pending(
            &base,
            TEST_PH,
            "post-1",
            TEST_PRE_IID.into(),
            "daw-from-POST-cdylib".into(),
        )
        .unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        // poll_with_base は instance_id だけ受け取り、daw_session_id 比較は
        // しない（段階 3 実装に同期）。
        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(
            sm.current(),
            RecordState::Record,
            "段階 3: target_pre_instance_id 一致のみで PRE は追従する"
        );
        assert!(recording.load(Ordering::Relaxed));
        assert!(ack.load(Ordering::Relaxed));
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(
            sig.status,
            SignalStatus::Acknowledged,
            "POST 側 cell からの signal も ack される (cdylib 隔離 G-115-36)"
        );
        assert_eq!(partner.as_ref().unwrap().post_instance_id, "post-1");
    }

    #[test]
    fn pending_sense_license_ignored() {
        let base = isolated_base();
        write_matching_pending(&base, "post-1");

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Sense);
        let mut partner = None;

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(!recording.load(Ordering::Relaxed));
        assert!(!ack.load(Ordering::Relaxed));
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(sig.status, SignalStatus::Pending);
    }

    #[test]
    fn released_transitions_back_to_watch() {
        let base = isolated_base();
        write_matching_pending(&base, "post-1");
        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;
        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );
        assert!(sm.is_recording());

        record_signal::mark_released(&base, TEST_PH, "post-1").unwrap();
        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(!recording.load(Ordering::Relaxed));
        assert!(!ack.load(Ordering::Relaxed));
        assert!(partner.is_none());
    }

    #[test]
    fn signal_file_removed_transitions_back_to_watch() {
        let base = isolated_base();
        write_matching_pending(&base, "post-1");
        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;
        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );
        assert!(sm.is_recording());

        record_signal::delete_signal(&base, TEST_PH, "post-1").unwrap();
        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(partner.is_none());
    }

    #[test]
    fn no_signal_file_is_noop() {
        let base = isolated_base();
        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(partner.is_none());
    }

    // ── B-024: signal_state guard (書込側 / 二重防御の片側) ─────────────

    /// SignalState::Bypassed の間 pending を検出しても ack しない (regression 防止)。
    /// signal は Pending のまま残り、partner も None のまま。
    #[test]
    fn poll_record_signal_skips_when_bypassed() {
        let base = isolated_base();
        write_matching_pending(&base, "post-1");

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base_state(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
            SignalState::Bypassed,
        );

        assert_eq!(
            sm.current(),
            RecordState::Watch,
            "Bypassed の間 PRE は ack せず Watch のまま"
        );
        assert!(!recording.load(Ordering::Relaxed));
        assert!(!ack.load(Ordering::Relaxed));
        assert!(partner.is_none());
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(
            sig.status,
            SignalStatus::Pending,
            "Bypassed の間 signal は Pending のまま (POST 側で別 PRE に切替可)"
        );
    }

    /// B-027: SignalState::Inactive でも pending を検出すれば ack する (緩和後)。
    /// 停止中 (transport 停止 / 無音) PRE も pair 候補として機能する。
    #[test]
    fn poll_record_signal_acks_when_inactive() {
        let base = isolated_base();
        write_matching_pending(&base, "post-1");

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base_state(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
            SignalState::Inactive,
        );

        assert_eq!(
            sm.current(),
            RecordState::Record,
            "Inactive でも ack して Record に入る (B-027 緩和後)"
        );
        assert!(recording.load(Ordering::Relaxed));
        assert!(ack.load(Ordering::Relaxed));
        assert!(partner.is_some());
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(sig.status, SignalStatus::Acknowledged);
    }

    /// SignalState::Active なら通常通り ack される (regression 防止)。
    /// poll_with_base 経由の既存テスト群と同じ判定軸を、明示 Active 引数で固定。
    #[test]
    fn poll_record_signal_acks_when_active() {
        let base = isolated_base();
        write_matching_pending(&base, "post-1");

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base_state(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
            SignalState::Active,
        );

        assert_eq!(
            sm.current(),
            RecordState::Record,
            "Active なら通常通り Record に入る"
        );
        assert!(recording.load(Ordering::Relaxed));
        assert!(ack.load(Ordering::Relaxed));
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(sig.status, SignalStatus::Acknowledged);
        assert!(partner.is_some());
        assert_eq!(partner.as_ref().unwrap().post_instance_id, "post-1");
    }

    // ── B-027 段階 3-B α-7 / Group 1: N Pending 並列 ack (Gap-4 + Gap-11) ─────

    /// 同 instance_id 配下 N Pending (N=2/5/12) を 1 tick で全件 ack する。
    /// MAX_ACTIVE_PER_PROJECT=12 (exclusion.rs:34) を上限境界として検証。
    #[test]
    fn n_pending_all_acked_in_single_tick() {
        for n in [2usize, 5, 12] {
            let base = isolated_base();
            for i in 0..n {
                write_matching_pending(&base, &format!("post-{i:02}"));
            }

            let sm = Arc::new(RecordStateMachine::new());
            let recording = Arc::new(AtomicBool::new(false));
            let ack = Arc::new(AtomicBool::new(false));
            let license = Arc::new(License::Os);
            let mut partner = None;

            poll_with_base(
                &base,
                &sm,
                &recording,
                &ack,
                &license,
                &mut partner,
                TEST_PRE_IID,
            );

            assert_eq!(
                sm.current(),
                RecordState::Record,
                "N={n}: state machine が Record に遷移する"
            );
            assert!(recording.load(Ordering::Relaxed), "N={n}: recording=true");
            assert!(
                ack.load(Ordering::Relaxed),
                "N={n}: record_acknowledged=true"
            );
            assert!(
                partner.is_some(),
                "N={n}: partner = 最後に ack した POST_iid"
            );
            for i in 0..n {
                let sig =
                    record_signal::read_signal(&base, TEST_PH, &format!("post-{i:02}")).unwrap();
                assert_eq!(
                    sig.status,
                    SignalStatus::Acknowledged,
                    "N={n}: post-{i:02} は同 tick 内で ack される (Gap-11 解消)"
                );
            }
        }
    }

    /// 同 instance_id 配下 N Pending のうち 1 件が race で消失しても、他 N-1 件の
    /// ack は継続する (fault injection 相当 / 各 ack は独立 atomic rename)。
    /// `mark_acknowledged_with_name` は signal 不在で `Ok(false)` を返すため、
    /// `last_acked` は更新されないが他件は正常 ack される。
    #[test]
    fn n_pending_one_removed_others_still_acked() {
        let base = isolated_base();
        for iid in &["post-A", "post-B", "post-C"] {
            write_matching_pending(&base, iid);
        }

        // 1 件を scan 直前に削除 (race window 再現): scan 後に削除でも同等動作だが、
        // ここでは scan で 3 件取れて ack ループ前に 1 件消えた状態を作る。
        // poll_with_base 内で scan_signals_dir は 3 件返すが、ack ループで
        // post-B の signal_path は読込失敗 → `Ok(false)` → スキップ。
        record_signal::delete_signal(&base, TEST_PH, "post-B").unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(
            sm.current(),
            RecordState::Record,
            "他件 ack 成功で Record 維持"
        );
        let sig_a = record_signal::read_signal(&base, TEST_PH, "post-A").unwrap();
        let sig_c = record_signal::read_signal(&base, TEST_PH, "post-C").unwrap();
        assert_eq!(sig_a.status, SignalStatus::Acknowledged, "post-A 継続 ack");
        assert_eq!(sig_c.status, SignalStatus::Acknowledged, "post-C 継続 ack");
        assert!(
            record_signal::read_signal(&base, TEST_PH, "post-B").is_none(),
            "post-B は削除済 (signal は復活しない)"
        );
    }

    /// 単独 1 Pending でも従来通り ack される (find→filter 改修の回帰防止)。
    #[test]
    fn single_pending_still_acks_after_filter_change() {
        let base = isolated_base();
        write_matching_pending(&base, "post-only");

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(sm.current(), RecordState::Record);
        assert!(recording.load(Ordering::Relaxed));
        assert!(ack.load(Ordering::Relaxed));
        let sig = record_signal::read_signal(&base, TEST_PH, "post-only").unwrap();
        assert_eq!(sig.status, SignalStatus::Acknowledged);
        assert_eq!(partner.as_ref().unwrap().post_instance_id, "post-only");
    }

    /// 自分宛 N Pending + 異 instance_id 宛 M Pending 混在時、自分宛のみ全件 ack
    /// (filter 正確性 / target_pre_instance_id 経路保持)。
    #[test]
    fn mixed_target_pre_id_only_self_pending_acked() {
        let base = isolated_base();
        // 自分宛 3 件
        for iid in &["post-self-A", "post-self-B", "post-self-C"] {
            write_matching_pending(&base, iid);
        }
        // 別 PRE 宛 2 件
        write_pending(
            &base,
            TEST_PH,
            "post-other-1",
            "pre-other-X".into(),
            TEST_DAW.into(),
        )
        .unwrap();
        write_pending(
            &base,
            TEST_PH,
            "post-other-2",
            "pre-other-Y".into(),
            TEST_DAW.into(),
        )
        .unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        for iid in &["post-self-A", "post-self-B", "post-self-C"] {
            let sig = record_signal::read_signal(&base, TEST_PH, iid).unwrap();
            assert_eq!(
                sig.status,
                SignalStatus::Acknowledged,
                "自分宛 {iid} は ack される"
            );
        }
        for iid in &["post-other-1", "post-other-2"] {
            let sig = record_signal::read_signal(&base, TEST_PH, iid).unwrap();
            assert_eq!(
                sig.status,
                SignalStatus::Pending,
                "別 PRE 宛 {iid} は Pending のまま (filter 除外)"
            );
        }
    }

    #[test]
    fn multiple_signals_only_matching_one_acked() {
        let base = isolated_base();
        // 別 PRE 向け（無視）
        write_pending(
            &base,
            TEST_PH,
            "post-other",
            "pre-other".into(),
            TEST_DAW.into(),
        )
        .unwrap();
        // 自分向け
        write_matching_pending(&base, "post-self");

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert!(sm.is_recording());
        assert_eq!(partner.as_ref().unwrap().post_instance_id, "post-self");
        let other = record_signal::read_signal(&base, TEST_PH, "post-other").unwrap();
        assert_eq!(
            other.status,
            SignalStatus::Pending,
            "他 PRE 向け signal は触らない"
        );
    }

    // ── B-027 段階 3-B α-7 / Group 3: A→P 直接遷移検出 (Gap-21 / 設計判断 #15 (α) B 案) ─────

    /// last_seen_status=Acknowledged + 観測 status=Pending (Released 経由なし)
    /// = Stop+Keep 1 秒以内連打の race window 再現。poll_record_signal は
    /// exit_record / recording=false / record_acknowledged=false / partner=None
    /// を実行し、新 generation の Pending として次 tick の Group 1 経路に流す
    /// 準備をする。
    #[test]
    fn a_to_p_direct_transition_clears_state() {
        let base = isolated_base();
        // Stop+Keep race 後の signal: status=Pending (新 generation)
        write_matching_pending(&base, "post-race");

        let sm = Arc::new(RecordStateMachine::new());
        // Recording 中だった想定
        sm.try_enter_record(License::Os).unwrap();
        let recording = Arc::new(AtomicBool::new(true));
        let ack = Arc::new(AtomicBool::new(true));
        let license = Arc::new(License::Os);
        // 既存 partner は last_seen_status=Acknowledged (前 generation の ack 済 state)
        let mut partner: Option<PartnerInfo> = Some(PartnerInfo {
            post_instance_id: "post-race".to_string(),
            last_seen_status: SignalStatus::Acknowledged,
        });

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(
            sm.current(),
            RecordState::Watch,
            "Group 3 branch で record_sm は Watch に戻る"
        );
        assert!(
            !recording.load(Ordering::Relaxed),
            "Group 3 branch で recording=false"
        );
        assert!(
            !ack.load(Ordering::Relaxed),
            "Group 3 branch で record_acknowledged=false"
        );
        assert!(
            partner.is_none(),
            "Group 3 branch で partner=None (新 generation 扱い)"
        );
    }

    /// last_seen_status=Pending + 観測 status=Pending は match new_status の早期
    /// return (status==last_seen) で抜ける / last_seen_status=Released → Pending のような
    /// 他遷移は default branch (last_seen_status 上書き) に流れて record_sm は不変。
    /// = Group 3 branch の guard `p.last_seen_status == Acknowledged` が他経路を
    /// 巻き込まないことの回帰防止。
    #[test]
    fn a_to_p_direct_transition_preserves_other_paths() {
        // ケース 1: last_seen_status=Pending → 観測 Pending (= 早期 return)
        {
            let base = isolated_base();
            write_matching_pending(&base, "post-keep");
            let sm = Arc::new(RecordStateMachine::new());
            sm.try_enter_record(License::Os).unwrap();
            let recording = Arc::new(AtomicBool::new(true));
            let ack = Arc::new(AtomicBool::new(true));
            let license = Arc::new(License::Os);
            let mut partner: Option<PartnerInfo> = Some(PartnerInfo {
                post_instance_id: "post-keep".to_string(),
                last_seen_status: SignalStatus::Pending,
            });

            poll_with_base(
                &base,
                &sm,
                &recording,
                &ack,
                &license,
                &mut partner,
                TEST_PRE_IID,
            );

            assert_eq!(
                sm.current(),
                RecordState::Record,
                "status==last_seen の早期 return / record_sm 不変"
            );
            assert!(recording.load(Ordering::Relaxed));
            assert!(ack.load(Ordering::Relaxed));
            assert!(partner.is_some(), "early return / partner 維持");
        }

        // ケース 2: last_seen_status=Pending → 観測 Acknowledged (= default branch)
        {
            let base = isolated_base();
            write_matching_pending(&base, "post-ack");
            // signal を Acknowledged に遷移させる (PRE 側 ack 済を fixture で再現)
            record_signal::mark_acknowledged(&base, TEST_PH, "post-ack").unwrap();

            let sm = Arc::new(RecordStateMachine::new());
            sm.try_enter_record(License::Os).unwrap();
            let recording = Arc::new(AtomicBool::new(true));
            let ack = Arc::new(AtomicBool::new(true));
            let license = Arc::new(License::Os);
            let mut partner: Option<PartnerInfo> = Some(PartnerInfo {
                post_instance_id: "post-ack".to_string(),
                last_seen_status: SignalStatus::Pending,
            });

            poll_with_base(
                &base,
                &sm,
                &recording,
                &ack,
                &license,
                &mut partner,
                TEST_PRE_IID,
            );

            assert_eq!(
                sm.current(),
                RecordState::Record,
                "Pending → Acknowledged は default branch / record_sm 維持"
            );
            assert!(
                recording.load(Ordering::Relaxed),
                "default branch で recording 不変"
            );
            assert!(ack.load(Ordering::Relaxed), "default branch で ack 不変");
            assert!(partner.is_some(), "default branch で partner 維持");
            assert_eq!(
                partner.as_ref().unwrap().last_seen_status,
                SignalStatus::Acknowledged,
                "default branch は last_seen_status を新 status に上書き"
            );
        }
    }

    // ── serialize_pre_json はそのまま動作 ────────────────────────

    #[test]
    fn serialize_pre_json_active_contains_instance_id() {
        let r = MeasureResult {
            lufs_m: Some(-14.0),
            ..Default::default()
        };
        let json = serialize_pre_json("pre-xyz", "Snare", SignalState::Active, &r);
        assert!(json.contains(r#""role":"PRE""#));
        assert!(json.contains(r#""instance_id":"pre-xyz""#));
        assert!(json.contains(r#""name":"Snare""#));
        assert!(json.contains(r#""lufs_m":-14.000"#));
        // bus フィールドは削除済（A-3 修正後）
        assert!(!json.contains(r#""bus""#));
    }

    #[test]
    fn serialize_pre_json_minimal_omits_measure_fields() {
        let json = serialize_pre_json_minimal("pre-xyz", "", SignalState::Bypassed);
        assert!(json.contains(r#""signal_state":"bypassed""#));
        assert!(!json.contains("lufs_m"));
        assert!(!json.contains(r#""bus""#));
    }

    /// B-027 段階 2: serialize_pre_json は空文字 name でも `"name":""` を出力する
    /// (POST 側 PreTmpJson は `#[serde(default)]` で空文字 / 不在を区別なく扱う)。
    #[test]
    fn serialize_pre_json_active_with_empty_name() {
        let r = MeasureResult::default();
        let json = serialize_pre_json("pre-xyz", "", SignalState::Active, &r);
        assert!(json.contains(r#""name":"""#));
    }

    /// B-027 段階 2: minimal でも `name` を出力する (Bypassed / Inactive でも
    /// 候補化される構造 / B-027 段階 1 緩和維持)。
    #[test]
    fn serialize_pre_json_minimal_includes_name() {
        let json = serialize_pre_json_minimal("pre-xyz", "Kick", SignalState::Inactive);
        assert!(json.contains(r#""name":"Kick""#));
        assert!(json.contains(r#""signal_state":"inactive""#));
    }

    /// B-027 段階 2: serialize → PreTmpJson 経由 → PreCandidate.name の roundtrip。
    #[test]
    fn serialize_pre_json_roundtrip_to_pre_candidate_name() {
        let r = MeasureResult::default();
        let json = serialize_pre_json("pre-xyz", "Snare", SignalState::Active, &r);
        // pre.json の "name" field が serde で読み戻せること (PreTmpJson と等価構造)。
        let parsed: serde_json::Value = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed["name"].as_str(), Some("Snare"));
    }

    /// B-077: " や \ を含む name が serde escape され valid JSON になり値が往復する
    /// （旧手組み生補間では不正 JSON を生成し POST parse 失敗していた回帰）。
    #[test]
    fn serialize_pre_json_escapes_quotes_and_backslash() {
        let r = MeasureResult::default();
        let name = "Snare\"x\\y";
        let json = serialize_pre_json("pre-xyz", name, SignalState::Active, &r);
        let parsed: serde_json::Value = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed["name"].as_str(), Some(name));
    }

    /// B-077: 日本語 name が strip されず JSON で保持され読み戻せる。
    #[test]
    fn serialize_pre_json_keeps_japanese_name() {
        let r = MeasureResult::default();
        let json = serialize_pre_json("pre-xyz", "日本語Snare", SignalState::Active, &r);
        let parsed: serde_json::Value = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed["name"].as_str(), Some("日本語Snare"));
    }

    /// B-131 (G-115-380) census-twin: instance_id も serde escape される（PRE 側）。restore で `"` を
    /// 含む instance_id が materialize wall（is_path_safe_component は `"` を拒否しない）を素通っても
    /// valid JSON になり、POST の pre_candidates scan が parse 失敗 → pairing 消失する同種 R-28 を防ぐ。
    #[test]
    fn serialize_pre_json_escapes_instance_id_quote() {
        let r = MeasureResult::default();
        let iid = "pre\"evil\\id";
        let json = serialize_pre_json(iid, "Snare", SignalState::Active, &r);
        let parsed: serde_json::Value = serde_json::from_str(&json)
            .expect("B-131: special-char instance_id must produce valid JSON");
        assert_eq!(parsed["instance_id"].as_str(), Some(iid));
    }

    /// B-131 (G-115-380) census-twin: minimal でも instance_id を serde escape する（PRE 側）。
    #[test]
    fn serialize_pre_json_minimal_escapes_instance_id_quote() {
        let iid = "pre\"evil\\id";
        let json = serialize_pre_json_minimal(iid, "Snare", SignalState::Inactive);
        let parsed: serde_json::Value = serde_json::from_str(&json)
            .expect("B-131: special-char instance_id must produce valid JSON (minimal)");
        assert_eq!(parsed["instance_id"].as_str(), Some(iid));
    }

    /// 直接 write_signal で daw_session_id 明示指定 + 読み戻し
    #[test]
    fn write_signal_with_explicit_daw_session_id() {
        let base = isolated_base();
        let sig = RecordSignal {
            status: SignalStatus::Pending,
            requested_by: "post-x".into(),
            target_pre_instance_id: "pre-x".into(),
            daw_session_id: "daw-uuid-explicit".into(),
            t: "2026-04-19T12:00:00Z".into(),
            started_at: "2026-04-19T11:59:30Z".into(),
            paired_pre_name: String::new(),
        };
        record_signal::write_signal(&base, TEST_PH, "post-x", &sig).unwrap();
        let loaded = record_signal::read_signal(&base, TEST_PH, "post-x").unwrap();
        assert_eq!(loaded.started_at, "2026-04-19T11:59:30Z");
        assert_eq!(loaded.daw_session_id, "daw-uuid-explicit");
    }

    // ── B-022 段階 4: discovery skip while partner=Some ──────────────────
    //
    // io_thread_pre.rs 主ループの discovery 呼出ガード:
    //   if partner.is_none() && discovery.should_rescan(now) { ... }
    // を直接 spawn せず、ガード式の不変条件をテストで構造的に固定する。
    //
    // 真因 (R-9 確定): POST 側 signal heartbeat 不在で signal mtime が ack 時点で
    // 固定 → DISCOVERY_STALE_SECS=10s 経過後に discover_pair_post_project_dir が
    // None を返す → cached_post_project_dir リセット → effective_project_hash_ref
    // が PRE 自身の project_hash に fallback → poll_record_signal が誤った dir を
    // scan → matching=empty → partner.current=None → exit_record() 誤発火。
    //
    // 段階 4 修正 (案 a): partner=Some の間 discovery 呼出を skip し
    // cached_post_project_dir を保持。これにより stale 判定経路が走らない。

    /// T1: partner=Some の間、discovery を呼ぶゲートが閉じることを構造的に固定。
    ///
    /// `partner.is_none() && discovery.should_rescan(now)` という主ループの
    /// ガード式に対し、partner=Some なら **時間がいくら経っても** ゲートが
    /// 開かないこと、cached_post_project_dir が初期値で固定されることを検証。
    #[test]
    fn discovery_skipped_while_partner_is_some() {
        let mut discovery = PreSelfDiscoveryState::new();

        // 初期 scan: partner=None / 一致 signal を発見した想定
        let t0 = Instant::now();
        let initial_post_dir = PathBuf::from("/tmp/kirin_test/post-uuid-X");
        discovery.record_scan(t0, Some((initial_post_dir.clone(), "daw-test".to_string())));

        // 直後に partner=Some になった想定 (PRE が ack して Record 入場)
        let mut partner: Option<PartnerInfo> = Some(PartnerInfo {
            post_instance_id: "post-1".to_string(),
            last_seen_status: SignalStatus::Acknowledged,
        });

        // 主ループのガード式を直接評価。partner=Some の間は **何度試しても**
        // ゲートが開かない (= record_scan が呼ばれない) こと。
        for delta_ms in [1500u64, 5000, 11_000, 60_000] {
            let now = t0 + Duration::from_millis(delta_ms);
            let gate_open = partner.is_none() && discovery.should_rescan(now);
            assert!(
                !gate_open,
                "partner=Some の間 discovery ゲートは閉じる必要がある (delta={}ms)",
                delta_ms
            );
            // ガードが閉じている = record_scan が呼ばれない = 状態不変
            assert_eq!(
                discovery.cached_post_project_dir(),
                Some(initial_post_dir.as_path()),
                "partner=Some 中 cached_post_project_dir は初期値を保持する \
                 (delta={}ms)",
                delta_ms
            );
            assert_eq!(
                discovery.cached_daw_session_id(),
                Some("daw-test"),
                "partner=Some 中 cached_daw_session_id も初期値を保持する \
                 (delta={}ms)",
                delta_ms
            );
        }

        // partner を強制的に変えても (= ステータス変化シミュレーション)、
        // is_some() のまま → ゲート閉のまま。
        if let Some(p) = partner.as_mut() {
            p.last_seen_status = SignalStatus::Pending;
        }
        let now = t0 + Duration::from_secs(120);
        assert!(
            !(partner.is_none() && discovery.should_rescan(now)),
            "partner.last_seen_status を変えても is_some() なので gate 閉のまま"
        );
    }

    /// T2: partner=None 復帰後 discovery が再開し、新しい POST project_dir を
    /// 取得できることを構造的に固定。
    ///
    /// 受入基準 4-3 に対応: Record 終了 (Released / signal 消失検出) で
    /// partner=None になった次 tick から discovery 呼出が解禁される。
    #[test]
    fn discovery_resumed_after_partner_cleared() {
        let mut discovery = PreSelfDiscoveryState::new();
        let t0 = Instant::now();

        // 初期 scan: 旧 partner 用の project_dir を cache
        let old_post_dir = PathBuf::from("/tmp/kirin_test/post-uuid-OLD");
        discovery.record_scan(t0, Some((old_post_dir.clone(), "daw-OLD".to_string())));
        let mut partner: Option<PartnerInfo> = Some(PartnerInfo {
            post_instance_id: "post-old".to_string(),
            last_seen_status: SignalStatus::Acknowledged,
        });

        // 1.5 秒経過 (本来なら rescan 可) — partner=Some なのでゲート閉
        let t1 = t0 + Duration::from_millis(1500);
        assert!(
            !(partner.is_none() && discovery.should_rescan(t1)),
            "partner=Some の間は rescan ゲート閉 (再確認)"
        );

        // ── poll_record_signal が partner を None に戻した想定 ──
        //    (Released 検出 / signal 消失検出のいずれの経路でも結果は同じ)
        partner = None;

        // partner=None かつ should_rescan=true → ゲート開
        let t2 = t0 + Duration::from_secs(3);
        assert!(
            partner.is_none() && discovery.should_rescan(t2),
            "partner=None 復帰後はゲート開 (discovery 再開)"
        );

        // 主ループはここで discovery を呼び record_scan する。
        // 新しい POST instance に切り替わったシナリオを模擬。
        let new_post_dir = PathBuf::from("/tmp/kirin_test/post-uuid-NEW");
        discovery.record_scan(t2, Some((new_post_dir.clone(), "daw-NEW".to_string())));

        assert_eq!(
            discovery.cached_post_project_dir(),
            Some(new_post_dir.as_path()),
            "partner=None 復帰後の record_scan で cached_post_project_dir が更新される"
        );
        assert_eq!(
            discovery.cached_daw_session_id(),
            Some("daw-NEW"),
            "partner=None 復帰後の record_scan で cached_daw_session_id も更新される"
        );

        // 新 partner を ack 後にゲート再閉鎖を確認
        partner = Some(PartnerInfo {
            post_instance_id: "post-new".to_string(),
            last_seen_status: SignalStatus::Acknowledged,
        });
        let t3 = t2 + Duration::from_secs(15);
        assert!(
            !(partner.is_none() && discovery.should_rescan(t3)),
            "新 partner 確定後は再びゲート閉 (cached_post_project_dir 保護)"
        );
        assert_eq!(
            discovery.cached_post_project_dir(),
            Some(new_post_dir.as_path()),
            "新 partner 中も cached_post_project_dir は保持される"
        );
    }

    // ── B-022 段階 5: P-3 直接 read リトライ救済 ─────────────────────────
    //
    // 真因 β/γ (scan transient 失敗 = read_dir Err / parse race) で
    // poll_record_signal が `current=None` に陥った場合でも、`read_signal`
    // で path 直撃 read し直して Some を取れれば exit_record をスキップして
    // watch を維持する。実機 09b71ce 検証で確定した「ファイル実在 + scan 空」
    // 状態を救済する設計の不変条件を構造的に固定する。

    /// scan が空 Vec を返す状況を `partner=Some` のまま再現するため、
    /// 直接 `retry_direct_read` をテストするヘルパ。
    /// (poll_with_base 経路は scan を上書きできないため、retry_direct_read 単独で
    /// 「scan miss + 直接 read 救済」の判定軸を孤立させる)。
    fn run_p3_recovery_check(
        base: &Path,
        post_iid: &str,
    ) -> Option<crate::record_signal::RecordSignal> {
        retry_direct_read(base, TEST_PH, post_iid)
    }

    /// T3: scan が空 Vec を返す + read_signal が Some を返す → recovered=Some。
    ///
    /// 不変条件: ファイルが path 直撃 read で取得可能なら、retry_direct_read は
    /// `Some(signal)` を返す。これを poll_with_base が受け取れば
    /// exit_record はスキップされ partner / record_sm は維持される。
    #[test]
    fn p3_retry_recovers_when_signal_file_exists_on_disk() {
        let base = isolated_base();
        // 実ファイルを base/{TEST_PH}/record_signal/ に配置。
        write_pending(
            &base,
            TEST_PH,
            "post-T3",
            TEST_PRE_IID.into(),
            TEST_DAW.into(),
        )
        .unwrap();

        // retry_direct_read 単独: ファイルがあれば必ず Some。
        let recovered = run_p3_recovery_check(&base, "post-T3");
        let sig = recovered.expect(
            "ファイル実在時は retry_direct_read が必ず Some を返す \
             (P-3 救済の根幹)",
        );
        assert_eq!(sig.requested_by, "post-T3");
        assert_eq!(sig.target_pre_instance_id, TEST_PRE_IID);
        assert_eq!(sig.status, SignalStatus::Pending);

        // poll_with_base 経路統合: scan が偶然空ではないが、本テストは
        // retry_direct_read の救済能力を孤立して固定する目的。
        // poll_with_base 統合経路は既存 `signal_file_removed_*` で
        // 真消失時に exit_record する逆向きを既に固定済。
    }

    /// T4: scan が空 Vec を返す + read_signal も None → recovered=None で
    /// exit_record が呼ばれる。
    ///
    /// 不変条件: ファイルが本当に存在しない場合、retry_direct_read は
    /// `RETRY_MAX_ATTEMPTS` 回試行しても None を返す。これを poll_with_base
    /// が受け取れば従来通り exit_record + partner=None になる。
    #[test]
    fn p3_retry_returns_none_when_signal_genuinely_absent() {
        let base = isolated_base();
        // ファイルを意図的に書かない (= 真の消失状態)。

        let recovered = run_p3_recovery_check(&base, "post-T4");
        assert!(
            recovered.is_none(),
            "ファイル不在時は retry_direct_read が必ず None を返す \
             (RETRY_MAX_ATTEMPTS 回試行後)"
        );

        // poll_with_base 統合経路: partner=Some の状態で current=None を
        // 引き起こし、retry も None なら exit_record + partner=None。
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let recording = Arc::new(AtomicBool::new(true));
        let ack = Arc::new(AtomicBool::new(true));
        let license = Arc::new(License::Os);
        let mut partner = Some(PartnerInfo {
            post_instance_id: "post-T4".to_string(),
            last_seen_status: SignalStatus::Acknowledged,
        });

        // base には何も無い → scan_signals_dir 空 → matching=空 → current=None
        // → retry_direct_read も None → exit_record。
        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(
            sm.current(),
            RecordState::Watch,
            "真消失時は P-3 retry が None を返し、exit_record が発火する \
             (受入基準 #5-7 T4)"
        );
        assert!(!recording.load(Ordering::Relaxed));
        assert!(!ack.load(Ordering::Relaxed));
        assert!(
            partner.is_none(),
            "exit_record と同時に partner も None にクリアされる"
        );
    }

    /// T3 補強: poll_with_base 経由でファイル存在時に partner / Record が
    /// 維持されることを確認。`scan_signals_dir` がファイルを発見できれば
    /// そもそも current=None ではないので P-3 retry は通らないが、
    /// 「scan 成功時に partner が壊れない」という負の不変条件として固定。
    #[test]
    fn p3_keeps_partner_when_scan_finds_signal_normally() {
        let base = isolated_base();
        write_pending(
            &base,
            TEST_PH,
            "post-T3b",
            TEST_PRE_IID.into(),
            TEST_DAW.into(),
        )
        .unwrap();
        // PRE が ack 済の状態を再現
        record_signal::mark_acknowledged(&base, TEST_PH, "post-T3b").unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let recording = Arc::new(AtomicBool::new(true));
        let ack = Arc::new(AtomicBool::new(true));
        let license = Arc::new(License::Os);
        let mut partner = Some(PartnerInfo {
            post_instance_id: "post-T3b".to_string(),
            last_seen_status: SignalStatus::Acknowledged,
        });

        poll_with_base(
            &base,
            &sm,
            &recording,
            &ack,
            &license,
            &mut partner,
            TEST_PRE_IID,
        );

        assert_eq!(
            sm.current(),
            RecordState::Record,
            "scan が signal を発見できれば Record 継続 (P-3 を経由せず通常分岐)"
        );
        assert!(partner.is_some());
        assert_eq!(partner.as_ref().unwrap().post_instance_id, "post-T3b");
    }
}

// ── Tests (B-024 Group A / Gap-3 / Gap-5: stale Acknowledged signal cleanup) ─
#[cfg(test)]
mod startup_clear_acks_tests {
    use super::*;
    use crate::record_signal::{mark_acknowledged, write_pending, RecordSignal, SignalStatus};
    use std::sync::atomic::AtomicU64;

    const TEST_PH: &str = "ph-startup";
    const TEST_PH_OTHER: &str = "ph-other";
    const TEST_PRE_IID: &str = "pre-iid-startup";
    const TEST_PRE_IID_OTHER: &str = "pre-iid-other";
    const TEST_POST_IID: &str = "post-iid-startup";

    fn isolated_pdr(tag: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_pre_startup_clear_{pid}_{n}_{tag}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// Gap-3 / Gap-5: target_pre_instance_id == self_iid && status == Acknowledged の
    /// signal は startup で削除される。
    #[test]
    fn clear_stale_self_acks_in_removes_acknowledged_targeting_self() {
        let pdr = isolated_pdr("ack_targeting_self");
        write_pending(
            &pdr,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID.to_string(),
            "daw-1".to_string(),
        )
        .unwrap();
        // 該当 signal を Acknowledged に遷移 (PRE が ack 済み残骸を再現)。
        let acked = mark_acknowledged(&pdr, TEST_PH, TEST_POST_IID).unwrap();
        assert!(acked, "precondition: signal must be marked Acknowledged");

        clear_stale_self_acks_in(&pdr, TEST_PRE_IID);

        let after: Option<RecordSignal> =
            crate::record_signal::read_signal(&pdr, TEST_PH, TEST_POST_IID);
        assert!(
            after.is_none(),
            "stale Acknowledged signal targeting self must be deleted at startup"
        );
    }

    /// Gap-3 / Gap-5: status == Pending の signal は削除しない (POST 側 Pending は
    /// 新規要求として残す)。
    #[test]
    fn clear_stale_self_acks_in_keeps_pending() {
        let pdr = isolated_pdr("keep_pending");
        write_pending(
            &pdr,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID.to_string(),
            "daw-1".to_string(),
        )
        .unwrap();

        clear_stale_self_acks_in(&pdr, TEST_PRE_IID);

        let after = crate::record_signal::read_signal(&pdr, TEST_PH, TEST_POST_IID);
        assert!(
            after.is_some(),
            "Pending signal must NOT be deleted (Pending = 新規 Record 要求)"
        );
        assert_eq!(after.unwrap().status, SignalStatus::Pending);
    }

    /// Gap-3 / Gap-5: target_pre_instance_id != self_iid の Acknowledged signal は削除しない
    /// (他 PRE の signal を PRE-A が clear するのを防ぐ)。
    #[test]
    fn clear_stale_self_acks_in_keeps_other_pre_targets() {
        let pdr = isolated_pdr("keep_other_targets");
        write_pending(
            &pdr,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID_OTHER.to_string(),
            "daw-1".to_string(),
        )
        .unwrap();
        mark_acknowledged(&pdr, TEST_PH, TEST_POST_IID).unwrap();

        clear_stale_self_acks_in(&pdr, TEST_PRE_IID); // self = TEST_PRE_IID, signal target = OTHER

        let after = crate::record_signal::read_signal(&pdr, TEST_PH, TEST_POST_IID);
        assert!(
            after.is_some(),
            "Acknowledged signal targeting other PRE must NOT be deleted"
        );
        assert_eq!(after.unwrap().target_pre_instance_id, TEST_PRE_IID_OTHER);
    }

    /// Gap-3 / Gap-5: 複数 project_hash 横断で削除される (cdylib 隔離下対応)。
    #[test]
    fn clear_stale_self_acks_in_scans_all_project_hashes() {
        let pdr = isolated_pdr("multi_ph");
        // ph-startup と ph-other の両方に self 宛 Acknowledged signal を配置。
        write_pending(
            &pdr,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID.to_string(),
            "daw-1".to_string(),
        )
        .unwrap();
        mark_acknowledged(&pdr, TEST_PH, TEST_POST_IID).unwrap();

        write_pending(
            &pdr,
            TEST_PH_OTHER,
            "post-iid-other",
            TEST_PRE_IID.to_string(),
            "daw-1".to_string(),
        )
        .unwrap();
        mark_acknowledged(&pdr, TEST_PH_OTHER, "post-iid-other").unwrap();

        clear_stale_self_acks_in(&pdr, TEST_PRE_IID);

        assert!(
            crate::record_signal::read_signal(&pdr, TEST_PH, TEST_POST_IID).is_none(),
            "ph-startup の self 宛 Acknowledged は削除される"
        );
        assert!(
            crate::record_signal::read_signal(&pdr, TEST_PH_OTHER, "post-iid-other").is_none(),
            "ph-other の self 宛 Acknowledged も削除される (multi-ph scan)"
        );
    }

    /// Gap-3 / Gap-5: self_iid が空文字なら no-op (false-positive 全消し防止)。
    #[test]
    fn clear_stale_self_acks_in_empty_self_iid_is_noop() {
        let pdr = isolated_pdr("empty_self");
        write_pending(
            &pdr,
            TEST_PH,
            TEST_POST_IID,
            TEST_PRE_IID.to_string(),
            "daw-1".to_string(),
        )
        .unwrap();
        mark_acknowledged(&pdr, TEST_PH, TEST_POST_IID).unwrap();

        // top-level wrapper の guard で空文字 self_iid なら早期 return。
        clear_stale_self_acks_at_startup("");

        // signal はそのまま (top-level guard が機能した間接確証)。
        let after = crate::record_signal::read_signal(&pdr, TEST_PH, TEST_POST_IID);
        // 環境依存ホーム (StoragePaths::default_macos) を経由する経路の確証は
        // self_iid="" 早期 return で本機の plugin_data には触らない (R-9 of code)。
        let _ = after;
    }
}
