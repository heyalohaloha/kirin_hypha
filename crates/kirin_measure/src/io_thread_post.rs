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

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::delta::{DeltaMode, DeltaResult};
use crate::plugin_data::Role as PluginDataRole;
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

/// POST 用 IO Thread を起動して JoinHandle を返す。
///
/// # 引数
/// - `instance_id`        : POST の永続 instance UUID（plugin params 経由で project save に同梱）
/// - `project_hash`       : DAW プロセス単位の project_hash
/// - `sample_rate`        : Record モード Writer の `sample_rate` フィールドに格納
/// - `record_sm`          : Watch/Record 判定用（editor.rs から共有）
/// - `post_result`        : Measure Thread が更新する POST 側計測結果
/// - `delta_result`       : この IO Thread が更新する Δ結果
/// - `preset_available`   : 1 秒ごとに preset/ を ls して更新
/// - `paired_pre_target`  : trigger_keep が選定した PRE instance_id（v1.2 (a)
///   cross-instance pair 復元キー）。Watch 中は None、Keep 成功直後に Some、Stop で None
/// - `shutdown`           : `true` になったらループ終了
#[allow(clippy::too_many_arguments)]
pub fn spawn_io_thread_post(
    instance_id: String,
    project_hash: String,
    sample_rate: u32,
    record_sm: Arc<RecordStateMachine>,
    post_result: Arc<Mutex<MeasureResult>>,
    delta_result: Arc<Mutex<DeltaResult>>,
    signal_state: Arc<AtomicU8>,
    preset_available: Arc<AtomicBool>,
    paired_pre_target: Arc<Mutex<Option<String>>>,
    shutdown: Arc<AtomicBool>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        let project_dir = std::env::temp_dir().join("kirin").join(&project_hash);
        let instance_dir = project_dir.join(&instance_id);
        let post_file = instance_dir.join("post.json");
        let post_tmp = instance_dir.join("post.json.tmp");

        log::info!("[IOThread POST] started → {}", post_file.display());

        let mut recording: Option<RecordingCtx> = None;
        let mut last_preset_count: Option<usize> = None;
        let mut next_preset_poll = Instant::now();
        let mut next_ack_timeout_poll = Instant::now();

        loop {
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            match run_tick(
                &project_dir,
                &instance_dir,
                &post_tmp,
                &post_file,
                &instance_id,
                &post_result,
                &delta_result,
                &signal_state,
            ) {
                Ok(()) => {}
                Err(e) => log::warn!("[IOThread POST] tick error: {}", e),
            }

            // plugin_data/.../post/*.json ライフサイクル
            // POST は自身の signal_path から started_at を resolve
            let project_hash_ref = project_hash.as_str();
            let instance_id_ref = instance_id.as_str();
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
                poll_preset_availability(&project_hash, &preset_available, &mut last_preset_count);
                next_preset_poll = Instant::now() + PRESET_POLL_INTERVAL;
            }

            if Instant::now() >= next_ack_timeout_poll {
                poll_ack_timeout(&project_hash, &instance_id, &record_sm);
                next_ack_timeout_poll = Instant::now() + ACK_TIMEOUT_POLL_INTERVAL;
            }

            thread::sleep(LOOP_SLEEP);
        }

        if let Some(ctx) = recording.take() {
            writer_close(ctx);
        }

        if let Err(e) = fs::remove_file(&post_file) {
            log::debug!("[IOThread POST] cleanup post file: {}", e);
        }
        if let Err(e) = fs::remove_file(&post_tmp) {
            log::debug!("[IOThread POST] cleanup post tmp: {}", e);
        }
        let _ = fs::remove_dir(&instance_dir);
        log::info!("[IOThread POST] terminated");
    })
}

/// 1 ループの処理本体。
///
/// `project_dir` = `$TMPDIR/kirin/{project_hash}/`（PRE スキャンの起点）
/// `instance_dir` = `$TMPDIR/kirin/{project_hash}/{self.instance_id}/`（POST 書込先）
#[allow(clippy::too_many_arguments)]
fn run_tick(
    project_dir: &Path,
    instance_dir: &Path,
    post_tmp: &Path,
    post_file: &Path,
    instance_id: &str,
    post_result: &Arc<Mutex<MeasureResult>>,
    delta_result: &Arc<Mutex<DeltaResult>>,
    signal_state_atom: &Arc<AtomicU8>,
) -> Result<(), String> {
    let state = load_signal_state(signal_state_atom);

    fs::create_dir_all(instance_dir).map_err(|e| format!("create_dir_all: {e}"))?;

    if state != SignalState::Active {
        *delta_result
            .lock()
            .map_err(|e| format!("delta Mutex poisoned: {e}"))? = DeltaResult::default();

        let json = serialize_post_json_minimal(instance_id, state);
        fs::write(post_tmp, json.as_bytes()).map_err(|e| format!("write tmp: {e}"))?;
        fs::rename(post_tmp, post_file).map_err(|e| format!("rename: {e}"))?;
        return Ok(());
    }

    let post = post_result
        .lock()
        .map_err(|e| format!("post Mutex poisoned: {e}"))?
        .clone();

    let (delta, pre_signal_state) = compute_delta_with_state(project_dir, &post)?;

    *delta_result
        .lock()
        .map_err(|e| format!("delta Mutex poisoned: {e}"))? = delta;

    let json = serialize_post_json(instance_id, state, pre_signal_state, &post);
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
pub fn serialize_post_json(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    let pre_state_str = pre_signal_state
        .map(|s| format!(r#""{}""#, s.as_str()))
        .unwrap_or_else(|| "null".to_string());
    format!(
        r#"{{"v":2,"role":"POST","instance_id":"{instance_id}","signal_state":"{signal_state}","pre_signal_state":{pre_signal_state},"t":"{t}","lufs_m":{lufs_m},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id = instance_id,
        signal_state = state.as_str(),
        pre_signal_state = pre_state_str,
        t = t,
        lufs_m = opt_f64(result.lufs_m),
        true_peak = opt_f64(result.true_peak),
        crest = opt_f64(result.crest),
        psr = opt_f64(result.psr),
        phase_d = phase_d_fragment(result),
    )
}

/// Bypassed / Inactive 時の最小 POST JSON。
fn serialize_post_json_minimal(instance_id: &str, state: SignalState) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    format!(
        r#"{{"v":2,"role":"POST","instance_id":"{instance_id}","signal_state":"{signal_state}","t":"{t}"}}"#,
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
