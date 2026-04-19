//! IO Thread — POST 側。
//!
//! 100ms ループで:
//! 1. `$TMPDIR/kirin/default/MIX/pre_*.json` をスキャン
//! 2. 最新の `t` フィールドを持つ PRE ファイルを選択
//! 3. Δ = POST − PRE を算出、鮮度判定
//! 4. `post_{instance_id}.json` にアトミック書き込み（将来の布石）
//! 5. `Arc<Mutex<DeltaResult>>` を更新
//!
//! 3層隔離（guardian_53）:
//! - このスレッドが panic / 権限エラーで止まっても Audio Thread / Measure Thread は継続する。
//! - Drop 時に自分の post ファイルを削除する。

use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use crate::delta::{DeltaMode, DeltaResult};
use crate::{load_signal_state, MeasureResult, SignalState, BUS_PHASE1, PROJECT_HASH_PHASE1};

/// IO Thread ループ間隔（guardian_53: 100ms = 10fps）
const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// PRE ファイルが Active とみなされる最大経過時間（秒）
const STALE_SECS: i64 = 2;

/// PRE ファイルが NoPre とみなされる最大経過時間（秒）
const NO_PRE_SECS: i64 = 10;

/// POST 用 IO Thread を起動して JoinHandle を返す。
///
/// # 引数
/// - `instance_id`   : UUID v4 文字列（プラグインインスタンス起動時に 1 回生成）
/// - `post_result`   : Measure Thread が更新する POST 側計測結果
/// - `delta_result`  : この IO Thread が更新する Δ結果
/// - `shutdown`      : `true` になったらループ終了
pub fn spawn_io_thread_post(
    instance_id: String,
    post_result: Arc<Mutex<MeasureResult>>,
    delta_result: Arc<Mutex<DeltaResult>>,
    signal_state: Arc<AtomicU8>,
    shutdown: Arc<AtomicBool>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        let dir = io_dir();
        let post_file = dir.join(format!("post_{}.json", instance_id));
        let post_tmp = dir.join(format!("post_{}.json.tmp", instance_id));

        log::info!("[IOThread POST] started → {}", post_file.display());

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

            thread::sleep(LOOP_SLEEP);
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
