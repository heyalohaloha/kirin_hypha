//! IO Thread POST テスト（guardian_53 T-5 判断基準）
//!
//! 判断基準:
//! 1. PRE ファイルあり → Active モードで Δ が算出される
//! 2. PRE ファイルなし → DeltaMode::NoPre
//! 3. 複数 PRE ファイル → 最新 t を選択
//! 4. POST ファイルが /tmp/ に書き込まれ、Drop 後に削除される

use kirin_measure::{
    serialize_post_json, serialize_pre_json, spawn_io_thread_post, DeltaMode, DeltaResult,
    MeasureResult, SignalState,
};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

// ── serialize_post_json テスト ────────────────────────────────────────────

#[test]
fn test_post_json_format() {
    let result = MeasureResult {
        lufs_m: Some(-12.0),
        true_peak: Some(-0.5),
        crest: Some(10.0),
        psr: Some(7.0),
        ..Default::default()
    };
    let json = serialize_post_json("post-instance-001", SignalState::Active, Some(SignalState::Active), &result);

    assert!(json.contains(r#""v":2"#), "version: {}", json);
    assert!(json.contains(r#""role":"POST""#), "role: {}", json);
    assert!(json.contains(r#""instance_id":"post-instance-001""#), "instance_id: {}", json);
    assert!(json.contains(r#""bus":"MIX""#), "bus: {}", json);
    assert!(json.contains(r#""lufs_m":-12.000"#), "lufs_m: {}", json);
    assert!(json.starts_with('{'), "starts with {{");
    assert!(json.ends_with('}'), "ends with }}");
}

// ── IO Thread POST 統合テスト ─────────────────────────────────────────────

/// PRE ファイルが存在するとき → Active モードで Δ が算出される
///
/// 実プラグイン（Studio One 等）が同じ MIX ディレクトリに書き込んでいる可能性があるため、
/// 孤立ディレクトリを使って compute_delta を直接呼び出す。
/// IO Thread ループ自体（run_tick の起動）は test_io_thread_post_file_cleanup で検証済み。
#[test]
fn test_io_thread_post_active_delta() {
    use kirin_measure::io_thread_post::compute_delta;

    let pre_instance_id = format!("pre-active-{}", ts_id());
    let isolated_dir = std::env::temp_dir().join(format!("kirin_test_active_{}", ts_id()));
    std::fs::create_dir_all(&isolated_dir).unwrap();

    // PRE ファイルを孤立ディレクトリに書き込む（現在時刻 = Active）
    let pre_result = MeasureResult {
        lufs_m: Some(-14.0),
        true_peak: Some(-1.0),
        crest: Some(12.0),
        psr: Some(8.0),
        ..Default::default()
    };
    let pre_json = serialize_pre_json(&pre_instance_id, SignalState::Active, &pre_result);
    let pre_path = isolated_dir.join(format!("pre_{}.json", pre_instance_id));
    std::fs::write(&pre_path, pre_json.as_bytes()).unwrap();

    let post = MeasureResult {
        lufs_m: Some(-12.0),
        true_peak: Some(-0.5),
        crest: Some(10.0),
        psr: Some(7.0),
        ..Default::default()
    };

    let delta = compute_delta(&isolated_dir, &post).expect("compute_delta should not error");
    assert_eq!(delta.mode, DeltaMode::Active, "mode should be Active: {:?}", delta.mode);

    // Δ = POST - PRE = -12.0 - (-14.0) = +2.0
    let delta_lufs = delta.lufs.expect("delta.lufs should be Some");
    assert!(
        (delta_lufs - 2.0).abs() < 0.01,
        "delta_lufs expected ~2.0, got {}",
        delta_lufs
    );
    // Δ TP = -0.5 - (-1.0) = +0.5
    let delta_tp = delta.tp.expect("delta.tp should be Some");
    assert!(
        (delta_tp - 0.5).abs() < 0.01,
        "delta_tp expected ~0.5, got {}",
        delta_tp
    );
    // Δ Crest = 10.0 - 12.0 = -2.0
    let delta_crest = delta.crest.expect("delta.crest should be Some");
    assert!(
        (delta_crest - (-2.0)).abs() < 0.01,
        "delta_crest expected ~-2.0, got {}",
        delta_crest
    );

    let _ = std::fs::remove_dir_all(&isolated_dir);
}

/// PRE ファイルが存在しない（ディレクトリ自体なし）→ NoPre モード。
/// テスト専用の孤立ディレクトリを使い、並列実行の影響を受けない。
#[test]
fn test_no_pre_dir_missing_returns_nopre() {
    use kirin_measure::io_thread_post::compute_delta;

    let isolated_dir = std::env::temp_dir().join(format!("kirin_test_isolated_{}", ts_id()));
    // ディレクトリを作らない → compute_delta が NoPre を返すはず
    let _ = std::fs::remove_dir_all(&isolated_dir);

    let post = MeasureResult {
        lufs_m: Some(-12.0),
        ..Default::default()
    };

    let result = compute_delta(&isolated_dir, &post).expect("compute_delta should not error");
    assert_eq!(result.mode, DeltaMode::NoPre, "missing dir → NoPre");
}

/// PRE ファイルが 0 個（ディレクトリはある）→ NoPre モード
#[test]
fn test_no_pre_files_returns_nopre() {
    use kirin_measure::io_thread_post::compute_delta;

    let isolated_dir = std::env::temp_dir().join(format!("kirin_test_empty_{}", ts_id()));
    std::fs::create_dir_all(&isolated_dir).unwrap();
    // pre_*.json を置かない

    let post = MeasureResult {
        lufs_m: Some(-12.0),
        ..Default::default()
    };

    let result = compute_delta(&isolated_dir, &post).expect("compute_delta should not error");
    assert_eq!(result.mode, DeltaMode::NoPre, "empty dir → NoPre");

    let _ = std::fs::remove_dir_all(&isolated_dir);
}

/// 古い PRE ファイル（2026-01-01 = 10秒以上前）→ NoPre
#[test]
fn test_stale_pre_file_returns_nopre() {
    use kirin_measure::io_thread_post::compute_delta;

    let isolated_dir = std::env::temp_dir().join(format!("kirin_test_stale_{}", ts_id()));
    std::fs::create_dir_all(&isolated_dir).unwrap();

    // 10秒以上前の timestamp を埋め込んだ PRE ファイル
    let old_json = r#"{"v":1,"role":"PRE","instance_id":"old-001","bus":"MIX","t":"2020-01-01T00:00:00.000Z","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
    let pre_path = isolated_dir.join("pre_old-001.json");
    std::fs::write(&pre_path, old_json.as_bytes()).unwrap();

    let post = MeasureResult {
        lufs_m: Some(-12.0),
        ..Default::default()
    };

    let result = compute_delta(&isolated_dir, &post).expect("compute_delta should not error");
    assert_eq!(result.mode, DeltaMode::NoPre, "10秒超古い PRE → NoPre");

    let _ = std::fs::remove_dir_all(&isolated_dir);
}

/// POST ファイルが /tmp/ に書き込まれ、Drop 後に削除される
#[test]
fn test_io_thread_post_file_cleanup() {
    let instance_id = format!("post-cleanup-{}", ts_id());

    let post_result = Arc::new(Mutex::new(MeasureResult::default()));
    let delta_result = Arc::new(Mutex::new(DeltaResult::default()));
    let shutdown = Arc::new(AtomicBool::new(false));

    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let handle = spawn_io_thread_post(
        instance_id.clone(),
        Arc::clone(&post_result),
        Arc::clone(&delta_result),
        Arc::clone(&signal_state),
        Arc::clone(&shutdown),
    );

    std::thread::sleep(Duration::from_millis(250));

    let dir = std::env::temp_dir().join("kirin").join("default").join("MIX");
    let post_path = dir.join(format!("post_{}.json", instance_id));

    assert!(post_path.exists(), "POST file should exist: {:?}", post_path);

    shutdown.store(true, Ordering::Relaxed);
    handle.join().unwrap();

    assert!(
        !post_path.exists(),
        "POST file should be deleted after shutdown: {:?}",
        post_path
    );
}

/// 複数 PRE ファイルがある場合、最新 t を持つものが選ばれること。
#[test]
fn test_selects_latest_pre_by_timestamp() {
    use kirin_measure::io_thread_post::compute_delta;

    let isolated_dir = std::env::temp_dir().join(format!("kirin_test_multi_{}", ts_id()));
    std::fs::create_dir_all(&isolated_dir).unwrap();

    // 古い PRE（NoPre 圏外に設定）
    let old_json = r#"{"v":1,"role":"PRE","instance_id":"old","bus":"MIX","t":"2020-01-01T00:00:00.000Z","lufs_m":-30.0,"true_peak":-5.0,"crest":20.0,"psr":15.0}"#;
    std::fs::write(isolated_dir.join("pre_old.json"), old_json.as_bytes()).unwrap();

    // 新しい PRE（現在時刻 = Active）
    let new_pre = MeasureResult {
        lufs_m: Some(-14.0),
        true_peak: Some(-1.0),
        crest: Some(12.0),
        psr: None,
        ..Default::default()
    };
    let new_json = serialize_pre_json("new-pre-001", SignalState::Active, &new_pre);
    std::fs::write(isolated_dir.join("pre_new-pre-001.json"), new_json.as_bytes()).unwrap();

    let post = MeasureResult {
        lufs_m: Some(-12.0),
        true_peak: Some(-0.5),
        crest: Some(10.0),
        psr: None,
        ..Default::default()
    };

    let result = compute_delta(&isolated_dir, &post).expect("compute_delta should not error");

    // 新しい PRE が選ばれるので Active
    assert_eq!(result.mode, DeltaMode::Active, "should select newest PRE → Active");

    // Δ = -12.0 - (-14.0) = +2.0（新しい PRE から算出）
    let delta_lufs = result.lufs.expect("delta.lufs");
    assert!(
        (delta_lufs - 2.0).abs() < 0.01,
        "delta_lufs from new PRE: expected ~2.0, got {}",
        delta_lufs
    );

    let _ = std::fs::remove_dir_all(&isolated_dir);
}

// ── ヘルパー ────────────────────────────────────────────────────────────────

fn ts_id() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let t = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    format!("{}-{}", t.as_secs(), t.subsec_nanos())
}
