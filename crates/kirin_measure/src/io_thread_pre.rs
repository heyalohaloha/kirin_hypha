//! IO Thread — PRE 側（A-3 修正後）。
//!
//! 100ms ループで:
//! 1. `$TMPDIR/kirin/{project_hash}/{instance_id}/pre.json` にアトミック書込（Watch 値）
//! 2. 1 秒毎に `{plugin_data_dir}/{project_hash}/record_signal/*.json` を全件 polling し、
//!    `target_pre_instance_id == self.instance_id` かつ `daw_session_id == self.daw_session_id`
//!    の signal にだけ追従（Q1 (b) 厳格化 + cross-process 防壁）
//! 3. Record 中: `plugin_data/{project_hash}/{instance_id}/pre/*.json` に Frame / PSB を追記
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
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::plugin_data::Role as PluginDataRole;
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
/// - `instance_id`         : PRE の永続 instance UUID（plugin params 経由で project save に同梱）
/// - `project_hash`        : DAW プロセス単位の project_hash
/// - `daw_session_id`      : DAW プロセス単位の UUID（cross-process 防壁）
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
    instance_id: String,
    project_hash: String,
    daw_session_id: String,
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
        let dir = io_dir(&project_hash, &instance_id);
        let file_path = dir.join("pre.json");
        let tmp_path = dir.join("pre.json.tmp");

        log::info!("[IOThread PRE] started → {}", file_path.display());

        let mut writer_ctx: Option<RecordingCtx> = None;
        let mut last_poll: Option<Instant> = None;
        let mut partner: Option<PartnerInfo> = None;

        loop {
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            // ① pre.json（Watch 値）書き込み
            if let Err(e) = write_json(
                &dir,
                &tmp_path,
                &file_path,
                &instance_id,
                &result,
                &signal_state,
            ) {
                log::warn!("[IOThread PRE] write error: {}", e);
            }

            // ② record_signal poll（1 秒間隔）
            let now = Instant::now();
            let should_poll = last_poll.is_none_or(|t| now.duration_since(t) >= SIGNAL_POLL_INTERVAL);
            if should_poll {
                last_poll = Some(now);
                poll_record_signal(
                    &project_hash,
                    &instance_id,
                    &daw_session_id,
                    &record_sm,
                    &recording,
                    &record_acknowledged,
                    &license,
                    &mut partner,
                );
            }

            // ③ plugin_data/.../pre/*.json ライフサイクル（Record writer）
            // partner が居れば partner の signal.started_at を解決、不在なら現在時刻 fallback
            let project_hash_ref = project_hash.as_str();
            let instance_id_ref = instance_id.as_str();
            let partner_iid = partner.as_ref().map(|p| p.post_instance_id.clone());
            let started_resolver_iid = partner_iid.clone();
            let resolver = move || match started_resolver_iid {
                Some(iid) => match StoragePaths::default_macos() {
                    Ok(paths) => crate::record_writer::resolve_started_at_ms(
                        &paths.plugin_data_dir(),
                        project_hash_ref,
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
                project_hash_ref,
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
        if let Err(e) = fs::remove_file(&file_path) {
            log::debug!("[IOThread PRE] cleanup file: {}", e);
        }
        if let Err(e) = fs::remove_file(&tmp_path) {
            log::debug!("[IOThread PRE] cleanup tmp: {}", e);
        }
        // instance ディレクトリ自体も空なら削除（残骸を残さない）
        let _ = fs::remove_dir(&dir);
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
/// scan_signals_dir で全 signal を取得し、`target_pre_instance_id == instance_id` かつ
/// `daw_session_id == daw_session_id` を **両方** 満たす最初の signal にだけ追従。
/// 追従中の partner（post_instance_id）を `partner` に保持し、消失・released で外す。
#[allow(clippy::too_many_arguments)]
fn poll_record_signal(
    project_hash: &str,
    instance_id: &str,
    daw_session_id: &str,
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

    // 全 signal を読む。条件一致しないものは弾く。
    let signals = record_signal::scan_signals_dir(&base, project_hash);
    let matching: Vec<_> = signals
        .into_iter()
        .filter(|(_, s)| {
            s.target_pre_instance_id == instance_id && s.daw_session_id == daw_session_id
        })
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
    /// 実装と同じロジック（scan_signals_dir + 二重 filter）を直接回す。
    #[allow(clippy::too_many_arguments)]
    fn poll_with_base(
        base: &Path,
        record_sm: &Arc<RecordStateMachine>,
        recording: &Arc<AtomicBool>,
        record_acknowledged: &Arc<AtomicBool>,
        license: &Arc<License>,
        partner: &mut Option<PartnerInfo>,
        instance_id: &str,
        daw_session_id: &str,
    ) {
        let signals = record_signal::scan_signals_dir(base, TEST_PH);
        let matching: Vec<_> = signals
            .into_iter()
            .filter(|(_, s)| {
                s.target_pre_instance_id == instance_id
                    && s.daw_session_id == daw_session_id
            })
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
        );

        // 自分宛てではないので Record に入らない
        assert_eq!(sm.current(), RecordState::Watch);
        assert!(!recording.load(Ordering::Relaxed));
        assert!(partner.is_none());
        // signal は変更されていない
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(sig.status, SignalStatus::Pending);
    }

    /// Q1 補強: daw_session_id が異なる signal は無視される（cross-process 防壁）。
    #[test]
    fn signal_with_wrong_daw_session_id_is_ignored() {
        let base = isolated_base();
        write_pending(
            &base,
            TEST_PH,
            "post-1",
            TEST_PRE_IID.into(),
            "daw-OTHER-process".into(),
        )
        .unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut partner = None;

        poll_with_base(
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
        );

        assert_eq!(
            sm.current(),
            RecordState::Watch,
            "別プロセスからの signal は ack しない"
        );
        assert!(partner.is_none());
        let sig = record_signal::read_signal(&base, TEST_PH, "post-1").unwrap();
        assert_eq!(sig.status, SignalStatus::Pending);
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
        );
        assert!(sm.is_recording());

        record_signal::mark_released(&base, TEST_PH, "post-1").unwrap();
        poll_with_base(
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
        );
        assert!(sm.is_recording());

        record_signal::delete_signal(&base, TEST_PH, "post-1").unwrap();
        poll_with_base(
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
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
            &base, &sm, &recording, &ack, &license, &mut partner, TEST_PRE_IID, TEST_DAW,
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
}
