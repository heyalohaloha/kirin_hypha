//! IO Thread — PRE 側。
//!
//! 100ms ループで:
//! 1. `$TMPDIR/kirin/default/MIX/pre_{instance_id}.json` にアトミック書き込み（Watch 値）
//! 2. 1 秒毎に `record_signal.json` を polling し、POST の Record 要請に追従
//! 3. Record 中: `plugin_data/{project_hash}/{bus}/pre/*.json` に Frame / PSB を追記
//!    （サブ3-B）
//!
//! 3層隔離（guardian_53）:
//! - このスレッドが panic / 権限エラーで止まっても Audio Thread / Measure Thread は継続する。
//! - Drop 時（プラグインアンロード）に自分の pre_*.json を削除する。
//! - Record 中にループ終了した場合、writer は status=closed で flush してから閉じる。
//!
//! # license 分岐（guardian_53 Q4 K1）
//! - `License::Os`: pending 検出で `try_enter_record` + `mark_acknowledged`、writer 起動
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
use crate::{
    load_signal_state, License, MeasureResult, SignalState, BUS_PHASE1, PROJECT_HASH_PHASE1,
};

/// IO Thread ループ間隔（guardian_53: 100ms = 10fps）
const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// record_signal.json を poll する間隔（1 秒）。
const SIGNAL_POLL_INTERVAL: Duration = Duration::from_secs(1);

/// PRE 用 IO Thread を起動して JoinHandle を返す。
///
/// # 引数
/// - `instance_id`         : UUID v4 文字列
/// - `sample_rate`         : Record writer メタデータ用
/// - `record_sm`           : Watch ↔ Record 状態機械（IO Thread が駆動）
/// - `recording`           : editor 表示用ミラー（`record_sm.is_recording()` を反映）
/// - `record_acknowledged` : `pending` を ack した後 true、`released` で false
/// - `license`             : pending 追従可否の判定（Os のみ参加）
/// - `result`              : Measure Thread が更新する計測結果
/// - `signal_state`        : SS-1 シグナル状態
/// - `shutdown`            : `true` になったらループ終了
///
/// # クリーンアップ
/// スレッド終了時に:
/// 1. Record 中なら writer を閉じる（status=closed + 最終 flush）
/// 2. Record 中なら `record_sm.exit_record()`（呼出元 Drop 側でも実施）
/// 3. `pre_{instance_id}.json` と `.tmp` を削除
#[allow(clippy::too_many_arguments)]
pub fn spawn_io_thread_pre(
    instance_id: String,
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
        let dir = io_dir();
        let file_path = dir.join(format!("pre_{}.json", instance_id));
        let tmp_path = dir.join(format!("pre_{}.json.tmp", instance_id));

        log::info!("[IOThread PRE] started → {}", file_path.display());

        let mut writer_ctx: Option<RecordingCtx> = None;
        let mut last_poll: Option<Instant> = None;
        let mut last_seen_status: Option<SignalStatus> = None;

        loop {
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            // ① pre_*.json（Watch 値）書き込み
            if let Err(e) =
                write_json(&dir, &tmp_path, &file_path, &instance_id, &result, &signal_state)
            {
                log::warn!("[IOThread PRE] write error: {}", e);
            }

            // ② record_signal.json poll（1 秒間隔）
            let now = Instant::now();
            let should_poll = last_poll.is_none_or(|t| now.duration_since(t) >= SIGNAL_POLL_INTERVAL);
            if should_poll {
                last_poll = Some(now);
                poll_record_signal(
                    &record_sm,
                    &recording,
                    &record_acknowledged,
                    &license,
                    &mut last_seen_status,
                );
            }

            // ③ plugin_data/.../pre/*.json ライフサイクル（Record writer）
            if let Err(e) = run_record_tick(
                &record_sm,
                PluginDataRole::Pre,
                sample_rate,
                &result,
                &mut writer_ctx,
            ) {
                log::warn!("[writer] tick error: {}", e);
            }

            // editor 用ミラー（run_record_tick 後の最新値を反映）
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
        log::info!("[IOThread PRE] terminated");
    })
}

/// `$TMPDIR/kirin/{project_hash}/{bus}/` パスを返す（pre_*.json / Watch 用）。
fn io_dir() -> PathBuf {
    std::env::temp_dir()
        .join("kirin")
        .join(PROJECT_HASH_PHASE1)
        .join(BUS_PHASE1)
}

/// record_signal.json を 1 度だけ読み、PRE 側の record state を同期する。
///
/// status 遷移のエッジで動作する（edge-triggered）:
/// - None → Pending: license == Os なら try_enter_record + mark_acknowledged。
///   Os 以外はログのみ。
/// - Pending → Released / ファイル消失: exit_record。
fn poll_record_signal(
    record_sm: &Arc<RecordStateMachine>,
    recording: &Arc<AtomicBool>,
    record_acknowledged: &Arc<AtomicBool>,
    license: &Arc<License>,
    last_seen: &mut Option<SignalStatus>,
) {
    let base = match StoragePaths::default_macos() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    let signal = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1);
    let current = signal.as_ref().map(|s| s.status);

    if current == *last_seen {
        return;
    }

    match (last_seen.as_ref(), current) {
        // 新規 pending 検出（ファイル不在 → pending、または acked→pending 再要請）
        (_, Some(SignalStatus::Pending)) => {
            if is_os_license(license) {
                match record_sm.try_enter_record(**license) {
                    Ok(()) => {
                        recording.store(true, Ordering::Relaxed);
                        if let Err(e) = record_signal::mark_acknowledged(
                            &base,
                            PROJECT_HASH_PHASE1,
                            BUS_PHASE1,
                        ) {
                            log::warn!("[signal] mark_acknowledged failed: {}", e);
                        } else {
                            record_acknowledged.store(true, Ordering::Relaxed);
                            log::info!("[signal] PRE acknowledged Record request");
                        }
                    }
                    Err(e) => {
                        log::warn!("[signal] try_enter_record rejected: {:?}", e);
                    }
                }
            } else {
                log::info!(
                    "[signal] pending detected, ignored (license: {:?})",
                    **license
                );
            }
        }
        // 既知 pending/acked → released: exit_record
        (Some(_), Some(SignalStatus::Released)) => {
            record_sm.exit_record();
            recording.store(false, Ordering::Relaxed);
            record_acknowledged.store(false, Ordering::Relaxed);
            log::info!("[signal] released, PRE exiting Record");
        }
        // 既知 pending/acked → ファイル消失: exit_record（POST 側 cleanup 済）
        (Some(_), None) => {
            if record_sm.is_recording() {
                record_sm.exit_record();
                recording.store(false, Ordering::Relaxed);
                record_acknowledged.store(false, Ordering::Relaxed);
                log::info!("[signal] file removed, PRE exiting Record");
            }
        }
        // その他の遷移（None → Acknowledged 等の非典型）は記録のみ
        _ => {}
    }

    *last_seen = current;
}

fn is_os_license(license: &License) -> bool {
    matches!(license, License::Os)
}

/// 計測結果を JSON に変換して アトミックに書き込む（Watch 値）。
///
/// アトミック書き込み手順:
/// 1. `.tmp` ファイルに書き込む
/// 2. `rename()` で最終パスに置換（POSIX: atomic。POST が壊れた JSON を読まない）
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
/// serde 不使用（構造が固定なので手動フォーマット）。
/// 数値は小数 3 桁で保持（GUI が 1 桁丸め担当）。
/// Phase D フィールドは値が存在する場合のみ出力（skip_serializing_if 相当）。
pub fn serialize_pre_json(instance_id: &str, state: SignalState, result: &MeasureResult) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","bus":"{bus}","signal_state":"{signal_state}","t":"{t}","lufs_m":{lufs_m},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id = instance_id,
        bus = BUS_PHASE1,
        signal_state = state.as_str(),
        t = t,
        lufs_m = opt_f64(result.lufs_m),
        true_peak = opt_f64(result.true_peak),
        crest = opt_f64(result.crest),
        psr = opt_f64(result.psr),
        phase_d = phase_d_fragment(result),
    )
}

/// Bypassed / Inactive 時の最小 JSON（計測値フィールドなし。SS-5 仕様）。
fn serialize_pre_json_minimal(instance_id: &str, state: SignalState) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","bus":"{bus}","signal_state":"{signal_state}","t":"{t}"}}"#,
        instance_id = instance_id,
        bus = BUS_PHASE1,
        signal_state = state.as_str(),
        t = t,
    )
}

/// `Option<f64>` を小数 3 桁文字列または `"null"` に変換する。
fn opt_f64(v: Option<f64>) -> String {
    match v {
        Some(x) => format!("{:.3}", x),
        None => "null".to_string(),
    }
}

/// Phase D フィールドの JSON フラグメントを生成する。
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
    use crate::record_signal::{write_pending, write_signal, RecordSignal, SignalStatus};
    use std::sync::atomic::AtomicU64;

    fn isolated_tmp_kirin() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir()
            .join(format!("kirin_io_pre_test_{pid}_{n}"))
            .join("kirin");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// poll_record_signal の挙動を検証するため、tmp_kirin_base に依存しない薄いラッパー。
    /// 実装と同じロジックを直接回すユニット。
    fn poll_with_base(
        base: &Path,
        record_sm: &Arc<RecordStateMachine>,
        recording: &Arc<AtomicBool>,
        record_acknowledged: &Arc<AtomicBool>,
        license: &Arc<License>,
        last_seen: &mut Option<SignalStatus>,
    ) {
        let signal = record_signal::read_signal(base, PROJECT_HASH_PHASE1, BUS_PHASE1);
        let current = signal.as_ref().map(|s| s.status);
        if current == *last_seen {
            return;
        }
        match (last_seen.as_ref(), current) {
            (_, Some(SignalStatus::Pending)) => {
                if is_os_license(license)
                    && record_sm.try_enter_record(**license).is_ok()
                {
                    recording.store(true, Ordering::Relaxed);
                    if record_signal::mark_acknowledged(base, PROJECT_HASH_PHASE1, BUS_PHASE1)
                        .is_ok()
                    {
                        record_acknowledged.store(true, Ordering::Relaxed);
                    }
                }
            }
            (Some(_), Some(SignalStatus::Released)) => {
                record_sm.exit_record();
                recording.store(false, Ordering::Relaxed);
                record_acknowledged.store(false, Ordering::Relaxed);
            }
            (Some(_), None) => {
                if record_sm.is_recording() {
                    record_sm.exit_record();
                    recording.store(false, Ordering::Relaxed);
                    record_acknowledged.store(false, Ordering::Relaxed);
                }
            }
            _ => {}
        }
        *last_seen = current;
    }

    // ── poll_record_signal ───────────────────────────────────────

    #[test]
    fn pending_os_license_enters_record_and_acknowledges() {
        let base = isolated_tmp_kirin();
        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut last = None;

        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        assert_eq!(sm.current(), RecordState::Record);
        assert!(recording.load(Ordering::Relaxed));
        assert!(ack.load(Ordering::Relaxed));
        // record_signal が acknowledged に遷移
        let sig = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        assert_eq!(sig.status, SignalStatus::Acknowledged);
    }

    #[test]
    fn pending_sense_license_ignored() {
        let base = isolated_tmp_kirin();
        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Sense);
        let mut last = None;

        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(!recording.load(Ordering::Relaxed));
        assert!(!ack.load(Ordering::Relaxed));
        // シグナルファイルは触らない（POST 側が管理）
        let sig = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        assert_eq!(sig.status, SignalStatus::Pending);
    }

    #[test]
    fn pending_unknown_license_ignored() {
        let base = isolated_tmp_kirin();
        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Unknown);
        let mut last = None;

        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(!recording.load(Ordering::Relaxed));
    }

    #[test]
    fn released_transitions_back_to_watch() {
        let base = isolated_tmp_kirin();
        // pending → ack → Record 突入
        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();
        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut last = None;
        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);
        assert!(sm.is_recording());

        // acknowledged → released（POST 側が更新）
        record_signal::mark_released(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        assert_eq!(sm.current(), RecordState::Watch);
        assert!(!recording.load(Ordering::Relaxed));
        assert!(!ack.load(Ordering::Relaxed));
    }

    #[test]
    fn signal_file_removed_transitions_back_to_watch() {
        let base = isolated_tmp_kirin();
        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();
        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut last = None;
        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);
        assert!(sm.is_recording());

        // POST が cleanup でシグナル削除
        record_signal::delete_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        assert_eq!(sm.current(), RecordState::Watch);
    }

    #[test]
    fn same_status_repeated_is_no_op() {
        let base = isolated_tmp_kirin();
        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();
        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut last = None;

        // 1 回目: Pending 検出 → Record 入り
        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);
        assert!(sm.is_recording());

        // 2 回目: status は Acknowledged（前回 ack 済）なので、
        //   last=Pending → current=Acknowledged の遷移が発生する。
        //   この遷移は明示的に何もしない（Record 継続）。
        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);
        assert_eq!(sm.current(), RecordState::Record);
        assert_eq!(last, Some(SignalStatus::Acknowledged));
    }

    #[test]
    fn no_signal_file_is_noop() {
        let base = isolated_tmp_kirin();
        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut last = None;

        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        assert_eq!(sm.current(), RecordState::Watch);
        assert_eq!(last, None);
    }

    #[test]
    fn legacy_pending_without_started_at_still_transitions() {
        // started_at 無しの旧スキーマでも、pending → Record 遷移が壊れないこと
        let base = isolated_tmp_kirin();
        let dir = base.join(PROJECT_HASH_PHASE1).join(BUS_PHASE1);
        fs::create_dir_all(&dir).unwrap();
        let legacy = r#"{"status":"pending","requested_by":"p","target_pre_instance_id":"r","t":"2026-01-01T00:00:00Z"}"#;
        fs::write(dir.join(record_signal::SIGNAL_FILENAME), legacy).unwrap();

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut last = None;

        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        assert!(sm.is_recording());
        assert!(recording.load(Ordering::Relaxed));
    }

    // ── started_at JSON 存続 ─────────────────────────────────────

    #[test]
    fn started_at_survives_ack_transition() {
        let base = isolated_tmp_kirin();
        let initial =
            write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();
        let started = initial.started_at.clone();
        assert!(!started.is_empty());

        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        let mut last = None;
        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        let after = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        assert_eq!(after.started_at, started, "started_at must not change on ack");
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
    }

    #[test]
    fn serialize_pre_json_minimal_omits_measure_fields() {
        let json = serialize_pre_json_minimal("pre-xyz", SignalState::Bypassed);
        assert!(json.contains(r#""signal_state":"bypassed""#));
        assert!(!json.contains("lufs_m"));
    }

    // ── pending 再要請テスト: acknowledged → pending 遷移 ──────

    #[test]
    fn reacknowledge_on_new_pending_after_acknowledged() {
        // 古い ack 済 signal を見ていた状態から、新しい pending が置かれたとき
        // 再度 Record 入りできること
        let base = isolated_tmp_kirin();
        let sm = Arc::new(RecordStateMachine::new());
        let recording = Arc::new(AtomicBool::new(false));
        let ack = Arc::new(AtomicBool::new(false));
        let license = Arc::new(License::Os);
        // 初期は Acknowledged として last_seen に記録
        let mut last = Some(SignalStatus::Acknowledged);

        // 新 pending
        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p2".into(), "r2".into())
            .unwrap();
        poll_with_base(&base, &sm, &recording, &ack, &license, &mut last);

        assert!(sm.is_recording());
        assert!(ack.load(Ordering::Relaxed));
    }

    // ── 直接 write_signal で started_at 明示指定 ────────────────

    #[test]
    fn write_signal_with_explicit_started_at() {
        let base = isolated_tmp_kirin();
        let sig = RecordSignal {
            status: SignalStatus::Pending,
            requested_by: "post-x".into(),
            target_pre_instance_id: "pre-x".into(),
            t: "2026-04-19T12:00:00Z".into(),
            started_at: "2026-04-19T11:59:30Z".into(),
        };
        write_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, &sig).unwrap();
        let loaded = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        assert_eq!(loaded.started_at, "2026-04-19T11:59:30Z");
    }
}
