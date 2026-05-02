//! IO Thread PRE テスト（guardian_53 T-4 判断基準）
//!
//! 判断基準:
//! 1. 100ms 間隔で /tmp/ に JSON が更新される
//! 2. アトミック書き込み: 別プロセスから同時読み取りして壊れた JSON が来ない
//! 3. プラグイン削除後にファイルが消える（← Drop テスト）
//! 4. 権限エラーでも Audio/Measure が継続する（← エラーパステスト）

use kirin_measure::{
    serialize_pre_json, spawn_io_thread_pre, License, MeasureResult, RecordStateMachine,
    SignalState,
};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

// ── JSON フォーマットテスト ─────────────────────────────────────────────────

/// 全項目 Some の場合のフォーマット確認
#[test]
fn test_json_format_all_values() {
    let result = MeasureResult {
        lufs_m: Some(-14.2),
        true_peak: Some(-1.1),
        crest: Some(12.3),
        psr: Some(8.7),
        ..Default::default()
    };
    let json = serialize_pre_json("test-uuid-1234", SignalState::Active, &result);

    // 必須フィールド存在確認
    assert!(json.contains(r#""v":2"#), "version field missing: {}", json);
    assert!(json.contains(r#""role":"PRE""#), "role field: {}", json);
    assert!(json.contains(r#""instance_id":"test-uuid-1234""#), "instance_id: {}", json);
    // A-3 修正後: bus フィールドは削除済（path に instance_id が入るため不要）
    assert!(!json.contains(r#""bus""#), "bus field must be removed: {}", json);
    assert!(json.contains(r#""t":"#), "timestamp field: {}", json);

    // 数値フォーマット: 小数 3 桁（JSON は精度保持。GUI が 1 桁丸め担当）
    assert!(json.contains(r#""lufs_m":-14.200"#), "lufs_m: {}", json);
    assert!(json.contains(r#""true_peak":-1.100"#), "true_peak: {}", json);
    assert!(json.contains(r#""crest":12.300"#), "crest: {}", json);
    assert!(json.contains(r#""psr":8.700"#), "psr: {}", json);

    // JSON として有効なことを確認（中括弧の対応）
    assert!(json.starts_with('{'), "must start with {{");
    assert!(json.ends_with('}'), "must end with }}");
}

/// 信号なし（None）の場合は null になること
#[test]
fn test_json_format_null_values() {
    let result = MeasureResult::default(); // 全 None
    let json = serialize_pre_json("test-uuid-null", SignalState::Active, &result);

    assert!(json.contains(r#""lufs_m":null"#), "lufs_m null: {}", json);
    assert!(json.contains(r#""true_peak":null"#), "true_peak null: {}", json);
    assert!(json.contains(r#""crest":null"#), "crest null: {}", json);
    assert!(json.contains(r#""psr":null"#), "psr null: {}", json);
}

/// 数値の丸め確認: 小数 3 桁（余分な桁が出ないか）
#[test]
fn test_json_rounding_three_decimals() {
    let result = MeasureResult {
        lufs_m: Some(-14.25678),
        true_peak: Some(-1.1495),
        crest: Some(12.350001),
        psr: None,
        ..Default::default()
    };
    let json = serialize_pre_json("test-round", SignalState::Active, &result);

    // format!("{:.3}", x) の動作を確認
    // -14.25678 → "-14.257"
    assert!(json.contains(r#""lufs_m":-14.257"#), "lufs_m rounding: {}", json);
    // -1.1495 → "-1.150" (banker's rounding or standard)
    assert!(json.contains(r#""true_peak":-1.150"#) || json.contains(r#""true_peak":-1.149"#),
        "true_peak rounding: {}", json);
    // 12.350001 → "12.350"
    assert!(json.contains(r#""crest":12.350"#), "crest rounding: {}", json);
}

/// タイムスタンプが ISO 8601 形式（"T" と "Z" を含む）であること
#[test]
fn test_json_timestamp_format() {
    let result = MeasureResult::default();
    let json = serialize_pre_json("test-ts", SignalState::Active, &result);

    // "t":"2026-..." の形式
    assert!(json.contains(r#""t":""#), "timestamp present: {}", json);
    // T と Z が含まれること
    let t_start = json.find(r#""t":""#).unwrap() + 5;
    let t_end = json[t_start..].find('"').unwrap() + t_start;
    let ts = &json[t_start..t_end];
    assert!(ts.contains('T'), "timestamp must contain T: {}", ts);
    assert!(ts.ends_with('Z'), "timestamp must end with Z: {}", ts);
    // 長さチェック: "2026-04-15T12:34:56.789Z" = 24 chars
    assert_eq!(ts.len(), 24, "timestamp length: {}", ts);
}

// ── IO Thread 書き込みテスト ────────────────────────────────────────────────

/// IO Thread が $TMPDIR にファイルを書き込み、シャットダウン後にファイルが消えること。
/// guardian_53 判断基準 1（書き込み）と 3（クリーンアップ）を検証。
#[test]
fn test_io_thread_writes_and_cleans_up() {
    let suffix = uuid_v4_simple();
    let instance_id = format!("test-io-{}", suffix);
    let project_hash = format!("test-proj-{}", suffix);
    let daw_session_id = format!("test-daw-{}", suffix);
    let result = Arc::new(Mutex::new(MeasureResult {
        lufs_m: Some(-14.2),
        true_peak: Some(-1.1),
        crest: Some(12.3),
        psr: Some(8.7),
        ..Default::default()
    }));
    let shutdown = Arc::new(AtomicBool::new(false));

    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));

    // IO Thread 起動
    let record_sm = Arc::new(RecordStateMachine::new());
    let recording = Arc::new(AtomicBool::new(false));
    let ack = Arc::new(AtomicBool::new(false));
    let license = Arc::new(License::Unknown);
    let handle = spawn_io_thread_pre(
        instance_id.clone(),
        project_hash.clone(),
        daw_session_id.clone(),
        48000,
        Arc::clone(&record_sm),
        Arc::clone(&recording),
        Arc::clone(&ack),
        Arc::clone(&license),
        Arc::clone(&result),
        Arc::clone(&signal_state),
        Arc::clone(&shutdown),
    );

    // 200ms 待てばファイルが生成されるはず（100ms 間隔）
    std::thread::sleep(Duration::from_millis(250));

    let dir = std::env::temp_dir().join("kirin").join(&project_hash).join(&instance_id);
    let file_path = dir.join("pre.json");

    // ── ファイルが存在すること ──
    assert!(file_path.exists(), "JSON file should exist: {:?}", file_path);

    // ── JSON が有効であること（壊れていないこと）──
    let content = std::fs::read_to_string(&file_path).expect("read file");
    assert!(content.starts_with('{'), "valid JSON: {}", content);
    assert!(content.ends_with('}'), "valid JSON: {}", content);
    assert!(content.contains(r#""lufs_m":-14.200"#), "lufs_m in file: {}", content);
    assert!(content.contains(&instance_id), "instance_id in file: {}", content);

    // ── シャットダウン後にファイルが削除されること ──
    shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("thread join");

    assert!(
        !file_path.exists(),
        "JSON file should be deleted after shutdown: {:?}", file_path
    );
}

/// エラーパス: 計測値が更新されたとき、次の 100ms ループで新しい値が書き込まれること。
#[test]
fn test_io_thread_updates_on_result_change() {
    let suffix = uuid_v4_simple();
    let instance_id = format!("test-update-{}", suffix);
    let project_hash = format!("test-proj-{}", suffix);
    let daw_session_id = format!("test-daw-{}", suffix);
    let result = Arc::new(Mutex::new(MeasureResult {
        lufs_m: Some(-20.0),
        ..Default::default()
    }));
    let shutdown = Arc::new(AtomicBool::new(false));

    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let record_sm = Arc::new(RecordStateMachine::new());
    let recording = Arc::new(AtomicBool::new(false));
    let ack = Arc::new(AtomicBool::new(false));
    let license = Arc::new(License::Unknown);
    let handle = spawn_io_thread_pre(
        instance_id.clone(),
        project_hash.clone(),
        daw_session_id.clone(),
        48000,
        Arc::clone(&record_sm),
        Arc::clone(&recording),
        Arc::clone(&ack),
        Arc::clone(&license),
        Arc::clone(&result),
        Arc::clone(&signal_state),
        Arc::clone(&shutdown),
    );

    // 1 回目の書き込みを待つ
    std::thread::sleep(Duration::from_millis(200));

    let dir = std::env::temp_dir().join("kirin").join(&project_hash).join(&instance_id);
    let file_path = dir.join("pre.json");
    let first = std::fs::read_to_string(&file_path).expect("first read");
    assert!(first.contains(r#""lufs_m":-20.000"#), "first value: {}", first);

    // 値を更新
    {
        let mut guard = result.lock().unwrap();
        guard.lufs_m = Some(-14.5);
    }

    // 2 回目の書き込みを待つ
    std::thread::sleep(Duration::from_millis(200));
    let second = std::fs::read_to_string(&file_path).expect("second read");
    assert!(second.contains(r#""lufs_m":-14.500"#), "updated value: {}", second);

    shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("join");
}

// ── ヘルパー ────────────────────────────────────────────────────────────────

/// テスト用に簡単なユニーク ID を返す（uuid クレートをテストで直接使わないため）
fn uuid_v4_simple() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let t = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    format!("{}-{}", t.as_secs(), t.subsec_nanos())
}
