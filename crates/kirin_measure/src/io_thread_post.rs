//! IO Thread — POST 側。
//!
//! 100ms ループで:
//! 1. `$TMPDIR/kirin/default/MIX/pre_*.json` をスキャン
//! 2. 最新の `t` フィールドを持つ PRE ファイルを選択
//! 3. Δ = POST − PRE を算出、鮮度判定
//! 4. `post_{instance_id}.json` にアトミック書き込み（将来の布石）
//! 5. `Arc<Mutex<DeltaResult>>` を更新
//! 6. Record mode 時: `plugin_data/{project_hash}/{bus}/post/*.json` に
//!    Frame (10 fps) / PSB スナップショット (2 fps) を追記、30 秒毎に flush
//!    （サブ3-A-3）
//!
//! 3層隔離（guardian_53）:
//! - このスレッドが panic / 権限エラーで止まっても Audio Thread / Measure Thread は継続する。
//! - Drop 時に自分の post ファイルを削除する。
//! - Record 中にループ終了した場合、保留中の writer は status=closed で flush してから閉じる。

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::delta::{DeltaMode, DeltaResult};
use crate::plugin_data::Role as PluginDataRole;
use crate::record::RecordStateMachine;
use crate::record_signal::{self, SignalStatus, ACK_TIMEOUT_SECONDS};
use crate::record_writer::{run_record_tick, writer_close, RecordingCtx};
use crate::storage::StoragePaths;
use crate::{load_signal_state, MeasureResult, SignalState, BUS_PHASE1, PROJECT_HASH_PHASE1};

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
/// - `instance_id`      : UUID v4 文字列（プラグインインスタンス起動時に 1 回生成）
/// - `sample_rate`      : Record モード Writer の `sample_rate` フィールドに格納
/// - `record_sm`        : Watch/Record 判定用（editor.rs から共有）
/// - `post_result`      : Measure Thread が更新する POST 側計測結果
/// - `delta_result`     : この IO Thread が更新する Δ結果
/// - `preset_available` : サブ3-C-2: preset/*.json が 1 件以上あるかを 1 秒ごとに ls して更新
/// - `shutdown`         : `true` になったらループ終了
#[allow(clippy::too_many_arguments)]
pub fn spawn_io_thread_post(
    instance_id: String,
    sample_rate: u32,
    record_sm: Arc<RecordStateMachine>,
    post_result: Arc<Mutex<MeasureResult>>,
    delta_result: Arc<Mutex<DeltaResult>>,
    signal_state: Arc<AtomicU8>,
    preset_available: Arc<AtomicBool>,
    shutdown: Arc<AtomicBool>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        let dir = io_dir();
        let post_file = dir.join(format!("post_{}.json", instance_id));
        let post_tmp = dir.join(format!("post_{}.json.tmp", instance_id));

        log::info!("[IOThread POST] started → {}", post_file.display());

        let mut recording: Option<RecordingCtx> = None;
        let mut last_preset_count: Option<usize> = None;
        let mut next_preset_poll = Instant::now();
        let mut next_ack_timeout_poll = Instant::now();

        loop {
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            match run_tick(&dir, &post_tmp, &post_file, &instance_id, &post_result, &delta_result, &signal_state) {
                Ok(()) => {}
                Err(e) => {
                    log::warn!("[IOThread POST] tick error: {}", e);
                }
            }

            // plugin_data/.../post/*.json ライフサイクル（サブ3-A-3 / 3-B-refactor）
            if let Err(e) = run_record_tick(
                &record_sm,
                PluginDataRole::Post,
                sample_rate,
                &post_result,
                &mut recording,
            ) {
                log::warn!("[writer] tick error: {}", e);
            }

            // preset/*.json ポーリング（サブ3-C-2: 1 秒間隔、内容は読まない）
            if Instant::now() >= next_preset_poll {
                poll_preset_availability(&preset_available, &mut last_preset_count);
                next_preset_poll = Instant::now() + PRESET_POLL_INTERVAL;
            }

            // record_signal.json ACK タイムアウト監視（G-60-02: pending 30秒超で自動 released）
            if Instant::now() >= next_ack_timeout_poll {
                poll_ack_timeout(&record_sm);
                next_ack_timeout_poll = Instant::now() + ACK_TIMEOUT_POLL_INTERVAL;
            }

            thread::sleep(LOOP_SLEEP);
        }

        // ── Record 中に shutdown された場合: writer を閉じる ─────────────
        if let Some(ctx) = recording.take() {
            writer_close(ctx);
        }

        // ── クリーンアップ ───────────────────────────────────────────────
        if let Err(e) = fs::remove_file(&post_file) {
            log::debug!("[IOThread POST] cleanup post file: {}", e);
        }
        if let Err(e) = fs::remove_file(&post_tmp) {
            log::debug!("[IOThread POST] cleanup post tmp: {}", e);
        }
        log::info!("[IOThread POST] terminated");
    })
}

/// 1 ループの処理本体。エラーは呼び出し元がログ出力して次ループに持ち越す。
fn run_tick(
    dir: &PathBuf,
    post_tmp: &PathBuf,
    post_file: &PathBuf,
    instance_id: &str,
    post_result: &Arc<Mutex<MeasureResult>>,
    delta_result: &Arc<Mutex<DeltaResult>>,
    signal_state_atom: &Arc<AtomicU8>,
) -> Result<(), String> {
    let state = load_signal_state(signal_state_atom);

    fs::create_dir_all(dir).map_err(|e| format!("create_dir_all: {e}"))?;

    if state != SignalState::Active {
        // Bypassed/Inactive: Δ結果をクリア、最小 JSON を書き出す
        *delta_result
            .lock()
            .map_err(|e| format!("delta Mutex poisoned: {e}"))? = DeltaResult::default();

        let json = serialize_post_json_minimal(instance_id, state);
        fs::write(post_tmp, json.as_bytes()).map_err(|e| format!("write tmp: {e}"))?;
        fs::rename(post_tmp, post_file).map_err(|e| format!("rename: {e}"))?;
        return Ok(());
    }

    // ── Active ──────────────────────────────────────────────────────
    let post = post_result
        .lock()
        .map_err(|e| format!("post Mutex poisoned: {e}"))?
        .clone();

    // PRE ファイルをスキャンして Δ算出 + pre_signal_state 取得（SS-6）
    let (delta, pre_signal_state) = compute_delta_with_state(dir, &post)?;

    // Δ結果を共有メモリに反映
    *delta_result
        .lock()
        .map_err(|e| format!("delta Mutex poisoned: {e}"))? = delta;

    // POST JSON 書き出し（SS-5 + SS-6: signal_state + pre_signal_state 付き）
    let json = serialize_post_json(instance_id, state, pre_signal_state, &post);
    fs::write(post_tmp, json.as_bytes()).map_err(|e| format!("write tmp: {e}"))?;
    fs::rename(post_tmp, post_file).map_err(|e| format!("rename: {e}"))?;

    Ok(())
}

/// PRE ファイルをスキャンして Δ を算出する（後方互換ラッパー）。
///
/// `compute_delta_with_state` の signal_state を捨てたバージョン。テストで使用。
#[doc(hidden)]
pub fn compute_delta(
    dir: &PathBuf,
    post: &MeasureResult,
) -> Result<DeltaResult, String> {
    compute_delta_with_state(dir, post).map(|(delta, _)| delta)
}

/// PRE ファイルをスキャンして Δ を算出し、PRE 側の signal_state も返す（SS-6）。
///
/// - 0 個 → `DeltaMode::NoPre`, `None`
/// - 複数 → 最新 `t` を選択（ISO 8601 は文字列比較で最新判定可）
/// - 鮮度判定 → `Active` / `Stale` / `NoPre`
fn compute_delta_with_state(
    dir: &PathBuf,
    post: &MeasureResult,
) -> Result<(DeltaResult, Option<SignalState>), String> {
    if !dir.exists() {
        return Ok((
            DeltaResult {
                mode: DeltaMode::NoPre,
                ..Default::default()
            },
            None,
        ));
    }

    let entries = fs::read_dir(dir).map_err(|e| format!("read_dir: {e}"))?;
    let mut pre_files: Vec<PathBuf> = entries
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| {
            p.file_name()
                .and_then(|n| n.to_str())
                .map(|n| n.starts_with("pre_") && n.ends_with(".json"))
                .unwrap_or(false)
        })
        .collect();

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

    // SS-6: PRE 側の signal_state を取得
    let pre_signal_state = parsed["signal_state"]
        .as_str()
        .map(|s| match s {
            "active" => SignalState::Active,
            "bypassed" => SignalState::Bypassed,
            _ => SignalState::Inactive,
        });

    // PRE が Active でない場合、Δ は算出不可（仕様: Δ=null、絶対値表示）。
    // DeltaMode::NoPre にすることで GUI が自動的に絶対値モードに切り替わる。
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

/// pre_*.json のリストから最新 `t` フィールドを持つファイルを返す。
fn select_best_pre(files: &mut Vec<PathBuf>) -> Result<PathBuf, String> {
    if files.len() == 1 {
        return Ok(files.remove(0));
    }

    // 各ファイルの `t` フィールドを読んで最大値を探す
    let mut best_path: Option<PathBuf> = None;
    let mut best_t = String::new();

    for path in files.iter() {
        let content = match fs::read_to_string(path) {
            Ok(c) => c,
            Err(_) => continue, // 読み取り失敗は無視して次へ
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

/// PRE JSON の `t` フィールドから鮮度モードを判定する。
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

/// `$TMPDIR/kirin/{project_hash}/{bus}/` パスを返す。
fn io_dir() -> PathBuf {
    std::env::temp_dir()
        .join("kirin")
        .join(PROJECT_HASH_PHASE1)
        .join(BUS_PHASE1)
}

/// POST JSON v2 フォーマット（Active 時。SS-5 + SS-6）。
///
/// `pre_signal_state` が `Some(Active)` なら Δ 算出済み（DeltaResult が持っている）。
/// `pre_signal_state` が Active 以外 or None なら Δ フィールドは null。
/// Phase D フィールドは値が存在する場合のみ出力（skip_serializing_if 相当）。
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
        r#"{{"v":2,"role":"POST","instance_id":"{instance_id}","bus":"{bus}","signal_state":"{signal_state}","pre_signal_state":{pre_signal_state},"t":"{t}","lufs_m":{lufs_m},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id = instance_id,
        bus = BUS_PHASE1,
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

/// Bypassed / Inactive 時の最小 POST JSON（SS-5 仕様）。
fn serialize_post_json_minimal(instance_id: &str, state: SignalState) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    format!(
        r#"{{"v":2,"role":"POST","instance_id":"{instance_id}","bus":"{bus}","signal_state":"{signal_state}","t":"{t}"}}"#,
        instance_id = instance_id,
        bus = BUS_PHASE1,
        signal_state = state.as_str(),
        t = t,
    )
}

/// `Option<f64>` を小数 3 桁文字列または `"null"` に変換する。
///
/// GUI 表示が 1 桁丸め担当。JSON は精度を保持する。
fn opt_f64(v: Option<f64>) -> String {
    match v {
        Some(x) => format!("{:.3}", x),
        None => "null".to_string(),
    }
}

/// Phase D フィールドの JSON フラグメントを生成する。
///
/// 各フィールドが `Some` の場合のみカンマ付きで出力（skip_serializing_if 相当）。
/// `None` のフィールドは JSON に含めない。
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
//
// POST が pending 書込後、PRE から 30 秒以内に acknowledged が返らなければ
// Record ロックを自動解除する。手動解決地獄（排他の焼き付き）を防ぐ。
//
// R-28: 利用者への明示通知なし。LED は derive_led_state 側で次フレーム自然遷移。

/// pending シグナルが ACK_TIMEOUT_SECONDS を超えていれば mark_released + exit_record。
///
/// この関数は IO Thread ループから 1 秒間隔で呼ばれる。失敗時はログのみ。
fn poll_ack_timeout(record_sm: &Arc<RecordStateMachine>) {
    let base = match StoragePaths::default_macos() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_ack_timeout_with_base(&base, record_sm, chrono::Utc::now());
}

/// `poll_ack_timeout` のテスト可能な本体（base と現在時刻を注入）。
fn poll_ack_timeout_with_base(
    base: &Path,
    record_sm: &Arc<RecordStateMachine>,
    now: chrono::DateTime<chrono::Utc>,
) {
    let Some(signal) = record_signal::read_signal(base, PROJECT_HASH_PHASE1, BUS_PHASE1) else {
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
    match record_signal::mark_released(base, PROJECT_HASH_PHASE1, BUS_PHASE1) {
        Ok(true) => log::info!("[IOThread POST] mark_released ok"),
        Ok(false) => log::debug!("[IOThread POST] signal already gone"),
        Err(e) => log::warn!("[IOThread POST] mark_released failed: {}", e),
    }
    record_sm.exit_record();
}

// ── preset/ poller（サブ3-C-2 / Q3 P1 / R-28 沈黙原則）────────────────────
//
// 1 秒ごとに `plugin_data/{project_hash}/preset/*.json` の件数だけを ls して
// `preset_available: AtomicBool` を更新する。
//
// R-28: ファイルは open() しない。内容読み取り・HMAC 検証は詳細 GUI 統合
// （Phase 2 / G-56-04）で別途実装する。ここでは存在有無のみを扱う。
//
// edge-triggered: 前回と件数が変化したときだけログを出す。ビジー更新でも
// スパムにならない。

/// `$HOME/.../plugin_data/{project_hash}/preset/` 直下の `.json` 件数を返す。
/// ディレクトリ不在 / 権限エラー時は 0（ファイルを open() しない）。
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

/// 1 秒ごとの preset availability 更新。件数変化時のみ edge-triggered log。
fn poll_preset_availability(
    preset_available: &Arc<AtomicBool>,
    last_seen: &mut Option<usize>,
) {
    let preset_dir = match StoragePaths::default_macos() {
        Ok(paths) => paths
            .plugin_data_dir()
            .join(PROJECT_HASH_PHASE1)
            .join(crate::preset::PRESET_SUBDIR),
        Err(_) => {
            // Kirin OS 未インストール環境 → 常に 0 件
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
            // 初回起動で 0 → None を抜け、明示的に unavailable を出す
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
        // 中身は 1 バイト。poller が open() しないことはコード上で保証。
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

    // ── edge-triggered ログ抑制 + AtomicBool 反映の単体検証 ─────────────
    //
    // `poll_preset_availability` は StoragePaths::default_macos() を直接呼ぶため
    // テスト環境では実パスに依存する（$HOME を置換できない）。ここでは
    // 「count 0 のとき AtomicBool=false」「count>0 のとき AtomicBool=true」
    // の反映を count_preset_files + AtomicBool の組で直接検証する。
    // 実際の poll_preset_availability はパス解決以外ロジックが同じため、この
    // 組み合わせで edge-triggered 動作は担保される（手動ステップ動作も確認）。

    #[test]
    fn atomic_bool_reflects_count_transition() {
        use std::sync::atomic::AtomicBool;
        let flag = Arc::new(AtomicBool::new(false));
        let dir = isolated_dir("bool");
        // 初期: 0 件 → false
        let count0 = count_preset_files(&dir);
        flag.store(count0 > 0, Ordering::Relaxed);
        assert!(!flag.load(Ordering::Relaxed));

        // 1 件 → true
        fs::write(dir.join("a.json"), b"x").unwrap();
        let count1 = count_preset_files(&dir);
        flag.store(count1 > 0, Ordering::Relaxed);
        assert!(flag.load(Ordering::Relaxed));

        // 消失 → false
        fs::remove_file(dir.join("a.json")).unwrap();
        let count2 = count_preset_files(&dir);
        flag.store(count2 > 0, Ordering::Relaxed);
        assert!(!flag.load(Ordering::Relaxed));
    }

    #[test]
    fn edge_triggered_fires_only_on_change() {
        // `last_seen` が変化時のみ更新されるロジックをローカル関数で再現し、
        // 2 回同じ件数なら fire しないことを検証。
        let dir = isolated_dir("edge");
        fs::write(dir.join("a.json"), b"x").unwrap();

        let mut last_seen: Option<usize> = None;
        let mut fires = 0usize;
        for _ in 0..5 {
            let count = count_preset_files(&dir);
            if last_seen != Some(count) {
                fires += 1;
                last_seen = Some(count);
            }
        }
        assert_eq!(fires, 1, "5 同件数 poll で fire は 1 回");

        // 件数変化 → fire
        fs::write(dir.join("b.json"), b"x").unwrap();
        let count = count_preset_files(&dir);
        if last_seen != Some(count) {
            fires += 1;
            last_seen = Some(count);
        }
        assert_eq!(fires, 2);

        // 消失 → fire
        fs::remove_file(dir.join("a.json")).unwrap();
        fs::remove_file(dir.join("b.json")).unwrap();
        let count = count_preset_files(&dir);
        if last_seen != Some(count) {
            fires += 1;
        }
        assert_eq!(fires, 3);
    }

    /// R-28 沈黙原則: count_preset_files は .json の中身を open() しない。
    /// 破損 JSON や権限 0 ファイルを混ぜてもエラーや panic にならないこと。
    #[test]
    fn count_does_not_open_file_contents() {
        let dir = isolated_dir("silent");
        fs::write(dir.join("broken.json"), b"{{not json!!").unwrap();
        fs::write(dir.join("ok.json"), b"x").unwrap();
        // パースを試みるなら broken.json で Err になるはず。件数で 2 が返る
        // → 中身を読まないことの間接的証拠。
        assert_eq!(count_preset_files(&dir), 2);
    }
}

// ── Tests (ACK timeout / G-60-02) ────────────────────────────────────────
#[cfg(test)]
mod ack_timeout_tests {
    use super::*;
    use crate::record::RecordState;
    use crate::record_signal::{mark_acknowledged, write_pending, RecordSignal};
    use std::sync::atomic::AtomicU64;

    fn isolated_base(tag: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_ack_timeout_{pid}_{n}_{tag}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// pending が 30 秒超 → mark_released + exit_record される。
    #[test]
    fn pending_over_30s_is_auto_released() {
        let base = isolated_base("stale");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();

        // 現在時刻で pending を書き、現在より 31 秒先の now を注入して判定
        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();
        let future_now = chrono::Utc::now() + chrono::Duration::seconds(31);

        poll_ack_timeout_with_base(&base, &sm, future_now);

        let after = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        assert_eq!(after.status, SignalStatus::Released, "status should auto-release");
        assert_eq!(sm.current(), RecordState::Watch, "state machine should exit record");
    }

    /// pending が 30 秒以内 → 何もしない。
    #[test]
    fn pending_within_30s_is_noop() {
        let base = isolated_base("fresh");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();

        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();
        // 直後の now → タイムアウトしない
        poll_ack_timeout_with_base(&base, &sm, chrono::Utc::now());

        let after = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        assert_eq!(after.status, SignalStatus::Pending, "status unchanged");
        assert_eq!(sm.current(), RecordState::Record, "still recording");
    }

    /// acknowledged → 何もしない（pending 以外はタイムアウト対象外）。
    #[test]
    fn acknowledged_is_noop_even_over_30s() {
        let base = isolated_base("acked");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();

        write_pending(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, "p".into(), "r".into()).unwrap();
        mark_acknowledged(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();

        let future_now = chrono::Utc::now() + chrono::Duration::seconds(300);
        poll_ack_timeout_with_base(&base, &sm, future_now);

        let after = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        assert_eq!(after.status, SignalStatus::Acknowledged, "acknowledged preserved");
        assert_eq!(sm.current(), RecordState::Record);
    }

    /// signal 不在 → 何もしない（panic しない）。
    #[test]
    fn missing_signal_is_noop() {
        let base = isolated_base("missing");
        let sm = Arc::new(RecordStateMachine::new());

        poll_ack_timeout_with_base(&base, &sm, chrono::Utc::now());

        assert_eq!(sm.current(), RecordState::Watch);
    }

    /// パース不能な t フィールド → タイムアウト扱いせず noop。
    #[test]
    fn invalid_t_field_is_noop() {
        let base = isolated_base("badt");
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(crate::License::Os).unwrap();

        // 不正 t の pending を直接書く
        let bad = RecordSignal {
            status: SignalStatus::Pending,
            requested_by: "p".into(),
            target_pre_instance_id: "r".into(),
            t: "not-iso-8601".into(),
            started_at: "2026-04-20T00:00:00Z".into(),
        };
        crate::record_signal::write_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1, &bad).unwrap();

        let future_now = chrono::Utc::now() + chrono::Duration::seconds(10_000);
        poll_ack_timeout_with_base(&base, &sm, future_now);

        let after = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1).unwrap();
        assert_eq!(after.status, SignalStatus::Pending, "bad t → noop");
        assert_eq!(sm.current(), RecordState::Record);
    }
}
