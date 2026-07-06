//! B-043 — LUFS-I / LRA / PLR + PSR Frame + schema 1.3 統合テスト。
//!
//! 確認項目:
//! 1. `MeasureEngine` が Integrated / LRA / True Peak モードを抱えて初期化される
//! 2. `engine.finalize()` が `SessionSummary { lufs_i, lra, max_true_peak }` を返す
//! 3. `PluginDataWriter::set_session_aggregates()` で JSON に lufs_i / lra / plr が出る
//! 4. Frame に psr Some(finite) が書き出される
//! 5. `PluginDataFile::SCHEMA_VERSION == "1.3"`
//! 6. PLR = max_true_peak − lufs_i の符号・値域が妥当（テスト信号で）
//!
//! テスト信号は 48 kHz 正弦波 (1 kHz, peak ≈ -3 dBFS, ≈ 1 秒) を直接生成する。
//! オフライン ebur128 検証は `tp_offline_reference.rs` 系列と同じ流儀。

use kirin_measure::engine::MeasureEngine;
use kirin_measure::plugin_data::{
    verify_checksum, PluginDataFile, PluginDataWriter, Role, WriterPaths,
};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

const SR: u32 = 48_000;
const CH: usize = 2;

/// テスト専用 base ディレクトリ（並列実行衝突回避）。
fn isolated_base() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_b043_test_{pid}_{n}"));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// 1 kHz 正弦波 (peak amp=0.7, stereo interleaved) を `n_samples` フレーム分生成。
fn gen_sine(n_samples: usize, amp: f64) -> Vec<f64> {
    let mut out = Vec::with_capacity(n_samples * CH);
    let freq = 1000.0_f64;
    for i in 0..n_samples {
        let t = i as f64 / SR as f64;
        let s = amp * (2.0 * std::f64::consts::PI * freq * t).sin();
        out.push(s); // L
        out.push(s); // R
    }
    out
}

fn make_writer(base: &std::path::Path, role: Role) -> PluginDataWriter {
    let paths = WriterPaths::build(base, "b043_ph", "b043_iid", role, "2026-05-16T10:00:00Z");
    PluginDataWriter::create(
        paths,
        "b043_install".to_string(),
        "b043_ph".to_string(),
        "b043_iid".to_string(),
        role,
        None,
        SR,
        (role == Role::Post).then(|| "b043_pre_iid".to_string()),
        (role == Role::Pre).then(|| "b043_post_iid".to_string()),
        None,
        None,
    )
    .unwrap()
}

fn read_output_for_final(final_path: &Path) -> PluginDataFile {
    let failed_path = failed_path_for_final(final_path);
    let pending_path = pair_pending_path_for_final(final_path);
    let path = if final_path.exists() {
        final_path.to_path_buf()
    } else if failed_path.exists() {
        failed_path
    } else if pending_path.exists() {
        pending_path
    } else {
        any_pair_pending_for_final(final_path).unwrap_or(pending_path)
    };
    let bytes = std::fs::read(&path).unwrap();
    serde_json::from_slice(&bytes).unwrap()
}

fn failed_path_for_final(final_path: &Path) -> PathBuf {
    let parent = final_path.parent().expect("role dir");
    let file_name = final_path.file_name().expect("file name");
    parent.join(".failed").join(file_name)
}

fn pair_pending_path_for_final(final_path: &Path) -> PathBuf {
    let parent = final_path.parent().expect("role dir");
    let file_name = final_path.file_name().expect("file name");
    parent.join(".pair_pending").join(file_name)
}

fn any_pair_pending_for_final(final_path: &Path) -> Option<PathBuf> {
    let dir = final_path.parent()?.join(".pair_pending");
    std::fs::read_dir(dir)
        .ok()?
        .flatten()
        .map(|entry| entry.path())
        .find(|path| path.extension().is_some_and(|ext| ext == "json"))
}

#[test]
fn schema_version_is_1_3() {
    assert_eq!(PluginDataFile::SCHEMA_VERSION, "1.3");
}

#[test]
fn engine_finalize_returns_session_summary_with_lufs_i_lra_tp() {
    let mut engine = MeasureEngine::new(SR, CH).expect("engine init");

    // 10 秒以上必要（loudness_global の最小ウィンドウ）。15 秒積む。
    let chunk_size = SR as usize / 10; // 100 ms
    for _ in 0..150 {
        let chunk = gen_sine(chunk_size, 0.7);
        let _ = engine.push(&chunk);
    }

    let summary = engine.finalize();
    let lufs_i = summary.lufs_i.expect("lufs_i must be Some after 15s");
    let lra = summary.lra; // 単一トーンでは LRA が極めて狭いか None になり得る
    let max_tp = summary.max_true_peak.expect("max_true_peak must be Some");

    // B-079: 緩い range (-20..0) を理論値基準へ締める。dual-mono 0.7 amp 正弦:
    // peak_dBFS = 20·log10(0.7) = -3.10。LUFS-I = peak - 0.691 + G_k(1kHz)（正弦 RMS -3.01 と
    // ITU stereo 合算 +3.01 が相殺。0.691=ITU 校正定数）≈ -3.10 + 0.70 - 0.691 ≈ -3.10 LUFS。
    // G_k(1kHz)≈0.70 dB（golden_psr_crest で実証）。許容 ±1.5。
    assert!(
        (-4.6..-1.6).contains(&lufs_i),
        "lufs_i={lufs_i} expected ≈ -3.10 (peak -3.10 dBFS - 0.691 + G_k(1kHz))"
    );
    // 定常トーンはロードネス変動なし → LRA ≈ 0（None もあり得る）。緩い 0..5 を < 1.5 へ。
    if let Some(lra) = lra {
        assert!(lra < 1.5, "steady-tone lra={lra} expected ≈ 0");
    }
    // max_true_peak は dBTP。0.7 amp の sine → 真ピーク ≒ -3 dBTP 付近
    assert!(
        (-10.0..2.0).contains(&max_tp),
        "max_true_peak out of range: {max_tp}"
    );
}

#[test]
fn set_session_aggregates_writes_lufs_i_lra_plr_to_json() {
    use kirin_measure::engine::SessionSummary;

    let base = isolated_base();
    let mut w = make_writer(&base, Role::Post);
    // 1 frame だけ書いておく（空 file は frames assert で 0 になる）
    w.append_frame(0, [1.0; 20], 1.5, -14.0, -1.0, 12.0, Some(8.7));

    let summary = SessionSummary {
        lufs_i: Some(-14.3),
        lra: Some(6.4),
        max_true_peak: Some(-1.2),
    };
    w.set_session_aggregates(summary);
    let final_path = {
        // close() は self を消費するので path は事前にコピー
        let path = w.data() as *const _;
        let _ = path; // unused; we'll use a fresh path via flush+close below
                      // 取得しやすい方法: writer 内部の final_path を見るには公開メソッドが無いため
                      // ここでは flush 後に WriterPaths から構築した path を再利用する。
        WriterPaths::build(
            &base,
            "b043_ph",
            "b043_iid",
            Role::Post,
            "2026-05-16T10:00:00Z",
        )
        .final_path
    };
    w.close().unwrap();

    let loaded = read_output_for_final(&final_path);

    assert_eq!(loaded.schema_version, "1.3");
    assert_eq!(loaded.lufs_i, Some(-14.3));
    assert_eq!(loaded.lra, Some(6.4));
    // PLR = -1.2 - (-14.3) = 13.1
    assert_eq!(loaded.plr, Some(13.1));
    // Frame psr が JSON に出ている
    assert_eq!(loaded.frames[0].psr, Some(8.7));
    assert!(verify_checksum(&loaded));
}

#[test]
fn plr_omitted_when_lufs_i_is_none() {
    use kirin_measure::engine::SessionSummary;

    let base = isolated_base();
    let mut w = make_writer(&base, Role::Post);
    w.set_session_aggregates(SessionSummary {
        lufs_i: None,
        lra: None,
        max_true_peak: Some(-0.5),
    });

    // 注入後の json で lufs_i / lra / plr が省略されること
    let json = serde_json::to_string(w.data()).unwrap();
    assert!(
        !json.contains("\"lufs_i\""),
        "lufs_i must be omitted: {json}"
    );
    assert!(!json.contains("\"lra\""), "lra must be omitted: {json}");
    assert!(!json.contains("\"plr\""), "plr must be omitted: {json}");
}

#[test]
fn frame_psr_none_is_omitted_from_json() {
    let base = isolated_base();
    let mut w = make_writer(&base, Role::Post);
    // psr=None を渡したフレーム
    w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0, None);
    let json = serde_json::to_string(w.data()).unwrap();
    // schema 1.3 で psr は skip_serializing_if = Option::is_none
    assert!(
        !json.contains("\"psr\""),
        "psr=None must be omitted: {json}"
    );
}

#[test]
fn finalize_handles_inf_or_neg_inf_loudness_gracefully() {
    // 無音入力では loudness_global / loudness_range が -inf or finite 端値を返し得る。
    // `is_finite()` フィルタで Some(NaN/Inf) が漏れないことを確認する。
    let mut engine = MeasureEngine::new(SR, CH).expect("engine init");
    // 100ms 無音だけ push して finalize
    let chunk_size = SR as usize / 10;
    let silent: Vec<f64> = vec![0.0; chunk_size * CH];
    let _ = engine.push(&silent);

    let s = engine.finalize();
    // -inf 等を弾く: いずれも is_finite() を経ているはず
    for v in [s.lufs_i, s.lra, s.max_true_peak].into_iter().flatten() {
        assert!(v.is_finite(), "finalize must filter non-finite: {v}");
    }
}
