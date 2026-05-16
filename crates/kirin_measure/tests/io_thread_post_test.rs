//! IO Thread POST テスト（guardian_53 T-5 判断基準）
//!
//! 判断基準:
//! 1. PRE ファイルあり → Active モードで Δ が算出される
//! 2. PRE ファイルなし → DeltaMode::NoPre
//! 3. 複数 PRE ファイル → 最新 t を選択
//! 4. POST ファイルが /tmp/ に書き込まれ、Drop 後に削除される

use kirin_measure::{
    serialize_post_json, serialize_pre_json, spawn_io_thread_post, DeltaMode, DeltaResult,
    DeltaSnapshot, MeasureResult, RecordStateMachine, SignalState,
};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
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
    let json = serialize_post_json(
        "post-instance-001",
        SignalState::Active,
        Some(SignalState::Active),
        &result,
        "PRE-A",
    );

    assert!(json.contains(r#""v":2"#), "version: {}", json);
    assert!(json.contains(r#""role":"POST""#), "role: {}", json);
    assert!(json.contains(r#""instance_id":"post-instance-001""#), "instance_id: {}", json);
    // A-3 修正後: bus フィールドは削除済み
    assert!(!json.contains(r#""bus""#), "bus field must be removed: {}", json);
    assert!(json.contains(r#""lufs_m":-12.000"#), "lufs_m: {}", json);
    // B-027 段階 3-B α-7-1: pair_pre_name field
    assert!(
        json.contains(r#""pair_pre_name":"PRE-A""#),
        "pair_pre_name field: {}",
        json
    );
    assert!(json.starts_with('{'), "starts with {{");
    assert!(json.ends_with('}'), "ends with }}");
}

// ── IO Thread POST 統合テスト ─────────────────────────────────────────────

/// PRE ファイルが存在するとき → Active モードで Δ が算出される
///
/// 新構造: `project_dir/{instance_id}/pre.json` を走査するため、
/// 孤立 project_dir 内に instance サブディレクトリを作って pre.json を配置する。
#[test]
fn test_io_thread_post_active_delta() {
    use kirin_measure::io_thread_post::compute_delta;

    let pre_instance_id = format!("pre-active-{}", ts_id());
    let isolated_dir = std::env::temp_dir().join(format!("kirin_test_active_{}", ts_id()));
    let pre_instance_dir = isolated_dir.join(&pre_instance_id);
    std::fs::create_dir_all(&pre_instance_dir).unwrap();

    // PRE ファイルを孤立ディレクトリ内 instance サブディレクトリに書き込む（現在時刻 = Active）
    let pre_result = MeasureResult {
        lufs_m: Some(-14.0),
        true_peak: Some(-1.0),
        crest: Some(12.0),
        psr: Some(8.0),
        ..Default::default()
    };
    let pre_json = serialize_pre_json(&pre_instance_id, "", SignalState::Active, &pre_result);
    let pre_path = pre_instance_dir.join("pre.json");
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
    // instance_id サブディレクトリも pre.json も置かない

    let post = MeasureResult {
        lufs_m: Some(-12.0),
        ..Default::default()
    };

    let result = compute_delta(&isolated_dir, &post).expect("compute_delta should not error");
    assert_eq!(result.mode, DeltaMode::NoPre, "empty dir → NoPre");

    let _ = std::fs::remove_dir_all(&isolated_dir);
}

/// 古い PRE ファイル（2020-01-01 = 鮮度切れ）→ NoPre
#[test]
fn test_stale_pre_file_returns_nopre() {
    use kirin_measure::io_thread_post::compute_delta;

    let isolated_dir = std::env::temp_dir().join(format!("kirin_test_stale_{}", ts_id()));
    let pre_instance_dir = isolated_dir.join("old-001");
    std::fs::create_dir_all(&pre_instance_dir).unwrap();

    // 鮮度切れ timestamp を埋め込んだ PRE ファイル（A-3 修正後 bus フィールド無し）
    let old_json = r#"{"v":2,"role":"PRE","instance_id":"old-001","t":"2020-01-01T00:00:00.000Z","signal_state":"active","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
    let pre_path = pre_instance_dir.join("pre.json");
    std::fs::write(&pre_path, old_json.as_bytes()).unwrap();

    let post = MeasureResult {
        lufs_m: Some(-12.0),
        ..Default::default()
    };

    let result = compute_delta(&isolated_dir, &post).expect("compute_delta should not error");
    assert_eq!(result.mode, DeltaMode::NoPre, "鮮度切れ PRE → NoPre");

    let _ = std::fs::remove_dir_all(&isolated_dir);
}

/// POST ファイルが /tmp/ に書き込まれ、Drop 後に削除される
#[test]
fn test_io_thread_post_file_cleanup() {
    let suffix = ts_id();
    let instance_id = format!("post-cleanup-{}", suffix);
    let project_hash = format!("test-proj-{}", suffix);

    let post_result = Arc::new(Mutex::new(MeasureResult::default()));
    let delta_result = Arc::new(Mutex::new(DeltaResult::default()));
    let shutdown = Arc::new(AtomicBool::new(false));

    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let record_sm = Arc::new(RecordStateMachine::new());
    let preset_available = Arc::new(AtomicBool::new(false));
    let paired_pre_target = Arc::new(Mutex::new(None));
    // Step 11: trigger_pair_resolution は本テストで触らない (broadcast 受信は別経路) →
    // no-op closure を渡す。
    let trigger_pair_resolution: kirin_measure::TriggerPairResolutionFn =
        Arc::new(|_, _| {});
    let trigger_stop_resolution: kirin_measure::TriggerStopResolutionFn =
        Arc::new(|_, _| {});
    let handle = spawn_io_thread_post(
        Arc::new(RwLock::new(instance_id.clone())),
        // §4-5 Step 1: project_hash を Arc<RwLock<String>> 化 (instance_id 同位相)。
        Arc::new(RwLock::new(project_hash.clone())),
        48000,
        Arc::clone(&record_sm),
        Arc::clone(&post_result),
        Arc::clone(&delta_result),
        Arc::clone(&signal_state),
        Arc::clone(&preset_available),
        Arc::clone(&paired_pre_target),
        Arc::clone(&shutdown),
        Arc::new(Mutex::new(String::new())),
        // Step 11: license 引数撤去 / 末尾 trigger_pair_resolution 追加
        // §4-5 Step 1: daw_session_id も Arc<RwLock<String>> 化。
        // α-7': trigger_stop_resolution 引数追加 (count 15)。
        Arc::new(RwLock::new(String::new())),
        Arc::new(RwLock::new(String::new())),
        trigger_pair_resolution,
        trigger_stop_resolution,
        Arc::new(RwLock::new(None)),
    );

    std::thread::sleep(Duration::from_millis(250));

    let dir = std::env::temp_dir().join("kirin").join(&project_hash).join(&instance_id);
    let post_path = dir.join("post.json");

    assert!(post_path.exists(), "POST file should exist: {:?}", post_path);

    shutdown.store(true, Ordering::Relaxed);
    handle.join().unwrap();

    assert!(
        !post_path.exists(),
        "POST file should be deleted after shutdown: {:?}",
        post_path
    );
}

/// B-027 段階 3-B α-7-1 / Step 6: pair_pre_name `Arc<RwLock<String>>` を設定
/// → IO Thread tick → post.json の `pair_pre_name` field に反映 (Q-A7 採用案 A
/// 完成 / cross-instance 公開機構 roundtrip)。
#[test]
fn test_pair_pre_name_arc_roundtrip_to_post_json() {
    let suffix = ts_id();
    let instance_id = format!("post-roundtrip-{}", suffix);
    let project_hash = format!("test-proj-rt-{}", suffix);

    let post_result = Arc::new(Mutex::new(MeasureResult {
        lufs_m: Some(-12.0),
        true_peak: Some(-0.5),
        crest: Some(10.0),
        psr: Some(7.0),
        ..Default::default()
    }));
    let delta_result = Arc::new(Mutex::new(DeltaResult::default()));
    let shutdown = Arc::new(AtomicBool::new(false));

    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let record_sm = Arc::new(RecordStateMachine::new());
    let preset_available = Arc::new(AtomicBool::new(false));
    let paired_pre_target = Arc::new(Mutex::new(None));
    // pair_pre_name に明示値を設定。
    let pair_pre_name_arc = Arc::new(RwLock::new(String::from("PRE-Master")));

    let trigger_pair_resolution: kirin_measure::TriggerPairResolutionFn =
        Arc::new(|_, _| {});
    let trigger_stop_resolution: kirin_measure::TriggerStopResolutionFn =
        Arc::new(|_, _| {});
    let handle = spawn_io_thread_post(
        Arc::new(RwLock::new(instance_id.clone())),
        // §4-5 Step 1: project_hash / daw_session_id を Arc<RwLock<String>> 化。
        // α-7': trigger_stop_resolution 引数追加。
        Arc::new(RwLock::new(project_hash.clone())),
        48000,
        Arc::clone(&record_sm),
        Arc::clone(&post_result),
        Arc::clone(&delta_result),
        Arc::clone(&signal_state),
        Arc::clone(&preset_available),
        Arc::clone(&paired_pre_target),
        Arc::clone(&shutdown),
        Arc::new(Mutex::new(String::new())),
        Arc::new(RwLock::new(String::new())),
        Arc::clone(&pair_pre_name_arc),
        trigger_pair_resolution,
        trigger_stop_resolution,
        Arc::new(RwLock::new(None)),
    );

    std::thread::sleep(Duration::from_millis(250));

    let dir = std::env::temp_dir().join("kirin").join(&project_hash).join(&instance_id);
    let post_path = dir.join("post.json");
    assert!(post_path.exists(), "POST file should exist: {:?}", post_path);

    let content = std::fs::read_to_string(&post_path).expect("read post.json");
    assert!(
        content.contains(r#""pair_pre_name":"PRE-Master""#),
        "post.json must contain pair_pre_name=PRE-Master: {}",
        content
    );

    shutdown.store(true, Ordering::Relaxed);
    handle.join().unwrap();
}

/// 複数 PRE ファイルがある場合、最新 t を持つものが選ばれること。
#[test]
fn test_selects_latest_pre_by_timestamp() {
    use kirin_measure::io_thread_post::compute_delta;

    let isolated_dir = std::env::temp_dir().join(format!("kirin_test_multi_{}", ts_id()));

    // 古い PRE（鮮度切れ）
    let old_dir = isolated_dir.join("old");
    std::fs::create_dir_all(&old_dir).unwrap();
    let old_json = r#"{"v":2,"role":"PRE","instance_id":"old","t":"2020-01-01T00:00:00.000Z","signal_state":"active","lufs_m":-30.0,"true_peak":-5.0,"crest":20.0,"psr":15.0}"#;
    std::fs::write(old_dir.join("pre.json"), old_json.as_bytes()).unwrap();

    // 新しい PRE（現在時刻 = Active）
    let new_dir = isolated_dir.join("new-pre-001");
    std::fs::create_dir_all(&new_dir).unwrap();
    let new_pre = MeasureResult {
        lufs_m: Some(-14.0),
        true_peak: Some(-1.0),
        crest: Some(12.0),
        psr: None,
        ..Default::default()
    };
    let new_json = serialize_pre_json("new-pre-001", "", SignalState::Active, &new_pre);
    std::fs::write(new_dir.join("pre.json"), new_json.as_bytes()).unwrap();

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

// ── B-048 / G-115-245 Last Known Good merge ロジック単体テスト ─────────────
//
// `run_tick` 内で実際に呼ばれる pure 関数 `merge_last_active` を直接呼んで
// 3 種の遷移 (Active→Active / Active→NoPre / NoPre→Active) で last_active が
// 仕様通りに更新/保持されることを保証する。Mutex / fs 副作用なしの unit test。

/// Helper: 指定 mode + 値で `DeltaResult` を構築するテスト専用 builder。
/// `last_active` は呼び出し側で `merge_last_active` の `prev_last_active` に
/// 別途渡すため、ここでは常に None。
fn make_delta(mode: DeltaMode, lufs: Option<f64>, psr: Option<f64>) -> DeltaResult {
    DeltaResult {
        lufs,
        psr,
        tp: None,
        n_prime_total: None,
        crest: None,
        sharpness: None,
        mode,
        last_active: None,
    }
}

/// (3) `Active → Active` 遷移で `last_active` が **新 6 field 値で上書き** される。
#[test]
fn test_merge_last_active_active_to_active_updates_snapshot() {
    use kirin_measure::io_thread_post::merge_last_active;

    // 前回 Active 結果 (prev_last_active に渡す snapshot)
    let prev_snap = DeltaSnapshot {
        lufs: Some(-1.0),
        psr: Some(2.0),
        tp: None,
        n_prime_total: None,
        crest: None,
        sharpness: None,
    };

    // 新 Active 結果
    let new_delta = make_delta(DeltaMode::Active, Some(-3.0), Some(4.0));

    let merged = merge_last_active(Some(prev_snap), new_delta);

    // mode は Active のまま継承
    assert_eq!(merged.mode, DeltaMode::Active);
    // 新値が表示用 field に
    assert_eq!(merged.lufs, Some(-3.0));
    assert_eq!(merged.psr, Some(4.0));
    // last_active は **新値 snapshot** で上書き (前回値 -1.0/2.0 は破棄)
    let snap = merged.last_active.expect("last_active must be Some after Active");
    assert_eq!(snap.lufs, Some(-3.0));
    assert_eq!(snap.psr, Some(4.0));
}

/// (4) `Active → NoPre` 遷移で `last_active` が **前回 Active 値を保持** する。
#[test]
fn test_merge_last_active_active_to_nopre_preserves_snapshot() {
    use kirin_measure::io_thread_post::merge_last_active;

    let prev_snap = DeltaSnapshot {
        lufs: Some(-5.5),
        psr: Some(7.7),
        tp: Some(-0.3),
        n_prime_total: None,
        crest: None,
        sharpness: None,
    };

    let new_delta = make_delta(DeltaMode::NoPre, None, None);
    let merged = merge_last_active(Some(prev_snap), new_delta);

    // mode は NoPre
    assert_eq!(merged.mode, DeltaMode::NoPre);
    // 表示用 field は None (新 NoPre 値)
    assert!(merged.lufs.is_none());
    assert!(merged.psr.is_none());
    // last_active は **前回 Active 値** で保持されている
    let snap = merged.last_active.expect("last_active must be preserved across NoPre");
    assert_eq!(snap.lufs, Some(-5.5));
    assert_eq!(snap.psr, Some(7.7));
    assert_eq!(snap.tp, Some(-0.3));
}

/// (5) `NoPre → Active` 遷移で `last_active` が **新 Active 値で更新** される。
/// 初回 Active 検出経路 (prev_last_active = None) を網羅。
#[test]
fn test_merge_last_active_nopre_to_active_creates_snapshot() {
    use kirin_measure::io_thread_post::merge_last_active;

    // 前回 last_active なし (初回 PRE 検出ケース)
    let prev_last_active: Option<DeltaSnapshot> = None;

    let new_delta = make_delta(DeltaMode::Active, Some(-8.0), Some(9.0));
    let merged = merge_last_active(prev_last_active, new_delta);

    assert_eq!(merged.mode, DeltaMode::Active);
    assert_eq!(merged.lufs, Some(-8.0));
    // last_active が新規 Some(snapshot) に
    let snap = merged.last_active.expect("last_active must be Some after NoPre→Active");
    assert_eq!(snap.lufs, Some(-8.0));
    assert_eq!(snap.psr, Some(9.0));
}

// ── ヘルパー ────────────────────────────────────────────────────────────────

fn ts_id() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let t = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    format!("{}-{}", t.as_secs(), t.subsec_nanos())
}
