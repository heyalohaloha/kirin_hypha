//! IO Thread — PRE 側。
//!
//! Measure Thread が更新した MeasureResult を読み取り、
//! `$TMPDIR/kirin/default/MIX/pre_{instance_id}.json` にアトミック書き込みする。
//!
//! 3層隔離（guardian_53）:
//! - このスレッドが panic / 権限エラーで止まっても Audio Thread / Measure Thread は継続する。
//! - Drop 時（プラグインアンロード）に自分のファイルを削除する。

use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use crate::{load_signal_state, MeasureResult, SignalState, BUS_PHASE1, PROJECT_HASH_PHASE1};

/// IO Thread ループ間隔（guardian_53: 100ms = 10fps）
const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// PRE 用 IO Thread を起動して JoinHandle を返す。
///
/// # 引数
/// - `instance_id` : UUID v4 文字列（プラグインインスタンス起動時に 1 回生成）
/// - `result`      : Measure Thread が更新する計測結果
/// - `shutdown`    : `true` になったらループ終了
///
/// # クリーンアップ
/// スレッド終了時に `pre_{instance_id}.json` を削除する。
/// プラグイン Drop → `shutdown = true` → `handle.join()` → ファイル削除 の順。
pub fn spawn_io_thread_pre(
    instance_id: String,
    result: Arc<Mutex<MeasureResult>>,
    signal_state: Arc<AtomicU8>,
    shutdown: Arc<AtomicBool>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        let dir = io_dir();
        let file_path = dir.join(format!("pre_{}.json", instance_id));
        let tmp_path = dir.join(format!("pre_{}.json.tmp", instance_id));

        log::info!("[IOThread PRE] started → {}", file_path.display());

        loop {
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            match write_json(&dir, &tmp_path, &file_path, &instance_id, &result, &signal_state) {
                Ok(()) => {}
                Err(e) => {
                    // 権限エラー等: 警告ログのみ。Audio / Measure Thread には影響しない。
                    // 次の 100ms で再試行する（ディレクトリが後から作られる場合への対応）。
                    log::warn!("[IOThread PRE] write error: {}", e);
                }
            }

            thread::sleep(LOOP_SLEEP);
        }

        // ── クリーンアップ ───────────────────────────────────────────────
        // 削除失敗は無視（クラッシュ残骸は次回起動時に上書き。害なし）
        if let Err(e) = fs::remove_file(&file_path) {
            log::debug!("[IOThread PRE] cleanup file: {}", e);
        }
        if let Err(e) = fs::remove_file(&tmp_path) {
            log::debug!("[IOThread PRE] cleanup tmp: {}", e);
        }
        log::info!("[IOThread PRE] terminated");
    })
}

/// `$TMPDIR/kirin/{project_hash}/{bus}/` パスを返す。
fn io_dir() -> PathBuf {
    std::env::temp_dir()
        .join("kirin")
        .join(PROJECT_HASH_PHASE1)
        .join(BUS_PHASE1)
}

/// 計測結果を JSON に変換して アトミックに書き込む。
///
/// アトミック書き込み手順:
/// 1. `.tmp` ファイルに書き込む
/// 2. `rename()` で最終パスに置換（POSIX: atomic。POST が壊れた JSON を読まない）
fn write_json(
    dir: &PathBuf,
    tmp_path: &PathBuf,
    file_path: &PathBuf,
    instance_id: &str,
    result: &Arc<Mutex<MeasureResult>>,
    signal_state: &Arc<AtomicU8>,
) -> Result<(), String> {
    // ディレクトリ作成（idempotent: 既存なら no-op）
    fs::create_dir_all(dir).map_err(|e| format!("create_dir_all: {e}"))?;

    let state = load_signal_state(signal_state);

    // JSON シリアライズ（SS-5: signal_state に応じたフォーマット）
    let json = if state == SignalState::Active {
        let measure = result
            .lock()
            .map_err(|e| format!("Mutex poisoned: {e}"))?
            .clone();
        serialize_pre_json(instance_id, state, &measure)
    } else {
        // Bypassed/Inactive: 計測値なし
        serialize_pre_json_minimal(instance_id, state)
    };

    // .tmp に書き込んで rename（atomic）
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
///
/// guardian_53 T-4 では "全数値は小数1桁で丸め" だが、GUI 表示が 1 桁丸め担当。
/// JSON（計測データ伝送）は精度を保持し、POST Δ算出や Step 2 TP 精度検証に使う。
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
