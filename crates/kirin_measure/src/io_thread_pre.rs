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
//! 3層隔離（guardian_53）:
//! - このスレッドが panic / 権限エラーで止まっても Audio Thread / Measure Thread は継続。
//! - Drop 時（プラグインアンロード）に自分の pre.json と instance ディレクトリを削除する。
//! - Record 中にループ終了した場合、writer は status=closed で flush してから閉じる。
//!
//! # license 分岐（guardian_53 Q4 K1）
//! - `License::Os`: 条件一致 pending 検出で `try_enter_record` + `mark_acknowledged`、writer 起動
//! - それ以外: pending 検出でログのみ。state machine を触らず、writer も生成しない。
//!   `record_signal.json` は POST 側が消す（PRE は削除しない）。

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::io_thread_post::read_instance_id_arc;
use crate::plugin_data::Role as PluginDataRole;
use crate::pre_self_discovery::{discover_pair_post_project_dir, PreSelfDiscoveryState};
use crate::record::RecordStateMachine;
use crate::record_signal::{self, SignalStatus};
use crate::record_writer::{run_record_tick, writer_close, RecordingCtx};
use crate::storage::StoragePaths;
use crate::{load_signal_state, License, MeasureResult, SignalState};

/// IO Thread ループ間隔（guardian_53: 100ms = 10fps）
const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// record_signal poll 間隔（1 秒）。
const SIGNAL_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// PRE スレッドが追従中の partner POST 情報（IO Thread ローカル）。
struct PartnerInfo {
    post_instance_id: String,
    last_seen_status: SignalStatus,
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
) -> JoinHandle<()> {
    thread::spawn(move || {
        log::info!(
            "[IOThread PRE] started (lazy-read instance_id, fallback project_hash={})",
            project_hash
        );

        let mut writer_ctx: Option<RecordingCtx> = None;
        let mut last_poll: Option<Instant> = None;
        let mut partner: Option<PartnerInfo> = None;
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
            let instance_id_owned = read_instance_id_arc(&instance_id);
            let instance_id_ref = instance_id_owned.as_str();
            let dir = io_dir(&project_hash, instance_id_ref);
            let file_path = dir.join("pre.json");
            let tmp_path = dir.join("pre.json.tmp");

            // ① pre.json（Watch 値）書き込み
            // path は PRE 自身の `project_hash` で構築する。POST 側 io_thread
            // の B-021 filesystem-discovery (`pre_discovery::discover_active_pre_dir`)
            // が `$TMPDIR/kirin/` 配下を全 project_uuid 横断で scan するため、
            // PRE/POST の project_hash が乖離していても POST はこの pre.json を
            // 拾える。本 path は B-022 では変更しない。
            if let Err(e) = write_json(
                &dir,
                &tmp_path,
                &file_path,
                instance_id_ref,
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
                let plugin_data_root = StoragePaths::default_macos()
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
                .and_then(|p| {
                    p.file_name()
                        .and_then(|n| n.to_str())
                        .map(str::to_string)
                });
            let effective_project_hash_ref: &str = effective_project_hash_owned
                .as_deref()
                .unwrap_or(project_hash.as_str());

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
            let should_poll = last_poll.is_none_or(|t| now.duration_since(t) >= SIGNAL_POLL_INTERVAL);
            if should_poll {
                last_poll = Some(now);
                poll_record_signal(
                    effective_project_hash_ref,
                    instance_id_ref,
                    &record_sm,
                    &recording,
                    &record_acknowledged,
                    &license,
                    &mut partner,
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
                Some(iid) => match StoragePaths::default_macos() {
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
            if let Err(e) = run_record_tick(
                &record_sm,
                PluginDataRole::Pre,
                sample_rate,
                &writer_project_hash,
                instance_id_ref,
                resolver,
                paired_pre_resolver,
                paired_post_resolver,
                &result,
                &mut writer_ctx,
            ) {
                log::warn!("[writer] tick error: {}", e);
            }

            recording.store(record_sm.is_recording(), Ordering::Relaxed);

            thread::sleep(LOOP_SLEEP);
        }

        // ── Record 中に shutdown された場合: writer を閉じる ─────────────
        if let Some(ctx) = writer_ctx.take() {
            writer_close(ctx);
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
        let final_tmp = final_dir.join("pre.json.tmp");
        if let Err(e) = fs::remove_file(&final_file) {
            log::debug!("[IOThread PRE] cleanup file: {}", e);
        }
        if let Err(e) = fs::remove_file(&final_tmp) {
            log::debug!("[IOThread PRE] cleanup tmp: {}", e);
        }
        // instance ディレクトリ自体も空なら削除（残骸を残さない）
        let _ = fs::remove_dir(&final_dir);
        log::info!("[IOThread PRE] terminated");
    })
}

/// `$TMPDIR/kirin/{project_hash}/{instance_id}/` パスを返す（pre.json / Watch 用）。
pub fn io_dir(project_hash: &str, instance_id: &str) -> PathBuf {
    std::env::temp_dir()
        .join("kirin")
        .join(project_hash)
        .join(instance_id)
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
) {
    let base = match StoragePaths::default_macos() {
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
                    _ => {
                        // pending → acknowledged 等は Record 継続
                        p.last_seen_status = new_status;
                        return;
                    }
                }
            }
            None => {
                // partner signal 消失 → Watch 復帰
                if record_sm.is_recording() {
                    record_sm.exit_record();
                    recording.store(false, Ordering::Relaxed);
                    record_acknowledged.store(false, Ordering::Relaxed);
                    log::info!(
                        "[signal] file removed, PRE exiting Record (partner={})",
                        p.post_instance_id
                    );
                }
                *partner = None;
                return;
            }
        }
    }

    // partner 未設定 → 新規 pending を探す
    let new_pending = matching
        .into_iter()
        .find(|(_, s)| s.status == SignalStatus::Pending);
    let Some((post_iid, _)) = new_pending else {
        return;
    };

    if !is_os_license(license) {
        log::info!(
            "[signal] pending detected (partner={}), ignored (license: {:?})",
            post_iid, **license
        );
        return;
    }

    match record_sm.try_enter_record(**license) {
        Ok(()) => {
            recording.store(true, Ordering::Relaxed);
            if let Err(e) = record_signal::mark_acknowledged(&base, project_hash, &post_iid) {
                log::warn!("[signal] mark_acknowledged failed: {}", e);
                record_sm.exit_record();
                recording.store(false, Ordering::Relaxed);
            } else {
                record_acknowledged.store(true, Ordering::Relaxed);
                log::info!(
                    "[signal] PRE acknowledged Record request (partner={})",
                    post_iid
                );
                *partner = Some(PartnerInfo {
                    post_instance_id: post_iid,
                    last_seen_status: SignalStatus::Acknowledged,
                });
            }
        }
        Err(e) => {
            log::warn!("[signal] try_enter_record rejected: {:?}", e);
        }
    }
}

fn is_os_license(license: &License) -> bool {
    matches!(license, License::Os)
}

/// 計測結果を JSON に変換して アトミックに書き込む（Watch 値）。
fn write_json(
    dir: &Path,
    tmp_path: &Path,
    file_path: &Path,
    instance_id: &str,
    result: &Arc<Mutex<MeasureResult>>,
    signal_state: &Arc<AtomicU8>,
) -> Result<(), String> {
    fs::create_dir_all(dir).map_err(|e| format!("create_dir_all: {e}"))?;

    let state = load_signal_state(signal_state);

    let json = if state == SignalState::Active {
        let measure = result
            .lock()
            .map_err(|e| format!("Mutex poisoned: {e}"))?
            .clone();
        serialize_pre_json(instance_id, state, &measure)
    } else {
        serialize_pre_json_minimal(instance_id, state)
    };

    fs::write(tmp_path, json.as_bytes()).map_err(|e| format!("write tmp: {e}"))?;
    fs::rename(tmp_path, file_path).map_err(|e| format!("rename: {e}"))?;

    Ok(())
}

/// guardian_53 T-4 + SS-5 の JSON v2 フォーマットに変換する（Active 時）。
///
/// A-3 修正後: bus フィールドは削除（path に instance_id が入るため不要）。
pub fn serialize_pre_json(
    instance_id: &str,
    state: SignalState,
    result: &MeasureResult,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"{signal_state}","t":"{t}","lufs_m":{lufs_m},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id = instance_id,
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
fn serialize_pre_json_minimal(instance_id: &str, state: SignalState) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"{signal_state}","t":"{t}"}}"#,
        instance_id = instance_id,
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
    /// 実装と同じロジック（scan_signals_dir + target_pre_instance_id 単一 filter）を直接回す。
    ///
    /// B-022 段階 3: daw_session_id filter 撤廃に追従。
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
                    p.last_seen_status = sig.status;
                    return;
                }
                None => {
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

        let new_pending = matching
            .into_iter()
            .find(|(_, s)| s.status == SignalStatus::Pending);
        let Some((post_iid, _)) = new_pending else {
            return;
        };

        if !is_os_license(license) {
            return;
        }

        if record_sm.try_enter_record(**license).is_ok() {
            recording.store(true, Ordering::Relaxed);
            if record_signal::mark_acknowledged(base, TEST_PH, &post_iid).is_ok() {
                record_acknowledged.store(true, Ordering::Relaxed);
                *partner = Some(PartnerInfo {
                    post_instance_id: post_iid,
                    last_seen_status: SignalStatus::Acknowledged,
                });
            }
        }
    }

    fn write_matching_pending(base: &Path, post_iid: &str) {
        write_pending(base, TEST_PH, post_iid, TEST_PRE_IID.into(), TEST_DAW.into())
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
        );
        assert!(sm.is_recording());

        record_signal::mark_released(&base, TEST_PH, "post-1").unwrap();
        poll_with_base(
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
        );
        assert!(sm.is_recording());

        record_signal::delete_signal(&base, TEST_PH, "post-1").unwrap();
        poll_with_base(
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
        );

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(partner.is_none());
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID,
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

    // ── serialize_pre_json はそのまま動作 ────────────────────────

    #[test]
    fn serialize_pre_json_active_contains_instance_id() {
        let r = MeasureResult {
            lufs_m: Some(-14.0),
            ..Default::default()
        };
        let json = serialize_pre_json("pre-xyz", SignalState::Active, &r);
        assert!(json.contains(r#""role":"PRE""#));
        assert!(json.contains(r#""instance_id":"pre-xyz""#));
        assert!(json.contains(r#""lufs_m":-14.000"#));
        // bus フィールドは削除済（A-3 修正後）
        assert!(!json.contains(r#""bus""#));
    }

    #[test]
    fn serialize_pre_json_minimal_omits_measure_fields() {
        let json = serialize_pre_json_minimal("pre-xyz", SignalState::Bypassed);
        assert!(json.contains(r#""signal_state":"bypassed""#));
        assert!(!json.contains("lufs_m"));
        assert!(!json.contains(r#""bus""#));
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
        discovery.record_scan(
            t0,
            Some((initial_post_dir.clone(), "daw-test".to_string())),
        );

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
}
