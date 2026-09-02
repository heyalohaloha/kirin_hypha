use super::*;
use std::sync::atomic::{AtomicU64, Ordering};

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_all_stop_test_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn stop_signals_dir_path_format() {
    let base = PathBuf::from("/base");
    let dir = stop_signals_dir(&base, "ph-A");
    assert_eq!(dir, PathBuf::from("/base/ph-A/all_stop_signal"));
}

#[test]
fn stop_signal_path_format() {
    let base = PathBuf::from("/base");
    let path = stop_signal_path(&base, "ph-A", "iid-X");
    assert_eq!(path, PathBuf::from("/base/ph-A/all_stop_signal/iid-X.json"));
}

#[test]
fn write_stop_broadcast_creates_atomic_file() {
    let base = isolated_dir();
    let result =
        write_stop_broadcast(&base, "ph", "originator-1", "session-A".to_string()).unwrap();
    assert_eq!(result.v, ALL_STOP_SCHEMA_VERSION);
    assert_eq!(result.originator_post_instance_id, "originator-1");
    assert_eq!(result.daw_session_id, "session-A");
    assert_eq!(result.host_process_id, std::process::id());
    let path = stop_signal_path(&base, "ph", "originator-1");
    assert!(path.exists());
    assert_eq!(
        read_current_stop_broadcast(&base, "ph").unwrap(),
        ("originator-1".to_string(), result)
    );
    assert_eq!(crate::atomic_file::remove_temp_siblings(&path).unwrap(), 0);
}

#[test]
fn generation_stop_is_addressed_to_the_exact_roster() {
    let base = isolated_dir();
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        "ph".into(),
        "originator-1".into(),
        "pre-1".into(),
        "daw-1".into(),
        std::process::id(),
    );
    let broadcast = write_stop_broadcast_for_generation(
        &base,
        "ph",
        "originator-1",
        "daw-1".into(),
        std::process::id(),
        &generation,
    )
    .unwrap();

    assert!(broadcast.has_generation());
    assert_eq!(
        broadcast.capture_generation_id,
        generation.capture_generation_id
    );
    assert_eq!(broadcast.generation_started_at_ms, generation.started_at_ms);
    assert!(write_stop_broadcast_for_generation(
        &base,
        "foreign-project",
        "originator-1",
        "daw-1".into(),
        std::process::id(),
        &generation,
    )
    .is_err());
}

#[test]
fn deleting_old_originator_does_not_remove_new_current_stop() {
    let base = isolated_dir();
    write_stop_broadcast(&base, "ph", "originator-old", "session-A".into()).unwrap();
    let newest = write_stop_broadcast(&base, "ph", "originator-new", "session-A".into()).unwrap();

    delete_stop_broadcast(&base, "ph", "originator-old").unwrap();

    assert_eq!(
        read_current_stop_broadcast(&base, "ph"),
        Some(("originator-new".into(), newest))
    );
}

#[test]
fn new_broadcast_uses_millisecond_barrier_precision() {
    let broadcast = AllStopBroadcast::new("originator-1".into(), "session-A".into());
    assert_eq!(broadcast.host_process_id, std::process::id());
    DateTime::parse_from_rfc3339(&broadcast.started_at).unwrap();
    let fraction = broadcast
        .started_at
        .split('.')
        .nth(1)
        .and_then(|s| s.strip_suffix('Z'))
        .expect("started_at must include a millisecond fraction");

    assert_eq!(fraction.len(), 3);
    assert_eq!(
        broadcast.heartbeat, broadcast.started_at,
        "heartbeat must preserve the same stop barrier"
    );
}

#[test]
fn old_schema_without_host_process_id_parses() {
    let json = r#"{
            "v": 1,
            "originator_post_instance_id": "iid-A",
            "daw_session_id": "sess-A",
            "started_at": "2026-05-04T12:00:00.000Z",
            "heartbeat": "2026-05-04T12:00:00.000Z"
        }"#;
    let parsed: AllStopBroadcast = serde_json::from_str(json).unwrap();
    assert_eq!(parsed.host_process_id, 0);
    assert_eq!(parsed.daw_session_id, "sess-A");
}

#[test]
fn delete_stop_broadcast_removes_file() {
    let base = isolated_dir();
    write_stop_broadcast(&base, "ph", "originator-1", "session-A".to_string()).unwrap();
    let path = stop_signal_path(&base, "ph", "originator-1");
    assert!(path.exists());
    delete_stop_broadcast(&base, "ph", "originator-1").unwrap();
    assert!(!path.exists());
}

#[test]
fn delete_stop_broadcast_missing_is_ok() {
    let base = isolated_dir();
    assert!(delete_stop_broadcast(&base, "ph", "no-such-originator").is_ok());
}

#[test]
fn scan_stop_broadcasts_dir_returns_all_valid() {
    let base = isolated_dir();
    write_stop_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    write_stop_broadcast(&base, "ph", "originator-B", "session-A".to_string()).unwrap();
    let v = scan_stop_broadcasts_dir(&base, "ph");
    assert_eq!(v.len(), 2);
    assert_eq!(v[0].0, "originator-A");
    assert_eq!(v[1].0, "originator-B");
}

#[test]
fn stop_broadcast_persists_across_scan_calls() {
    // 寿命仕様 = originator が Stop/Drop/Shutdown を実行するまで保持。
    let base = isolated_dir();
    write_stop_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    let path = stop_signal_path(&base, "ph", "originator-A");
    for i in 0..3 {
        let v = scan_stop_broadcasts_dir(&base, "ph");
        assert_eq!(v.len(), 1, "scan #{} で 1 件残存", i);
        assert!(path.exists());
    }
    delete_stop_broadcast(&base, "ph", "originator-A").unwrap();
    assert!(!path.exists());
}

#[test]
fn stop_broadcast_independent_of_keep_broadcast() {
    // α-7' Step 6 主バグ修正後の lifecycle 不変条件:
    // all_keep_signal の delete (#2) は all_stop_signal を touch しない。
    // all_stop_signal の lifecycle は #3 Drop / #4 IO Thread shutdown のみで管理。
    use crate::all_keep_signal;
    let base = isolated_dir();
    // 同一 originator が両 broadcast を配置 (All Keep → All Stop の連続押下シナリオ)
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        "ph".to_string(),
        "originator-A".to_string(),
        "pre-A".to_string(),
        "session-A".to_string(),
        std::process::id(),
    );
    all_keep_signal::write_broadcast_for_generation(
        &base,
        "ph",
        "originator-A",
        "session-A".to_string(),
        std::process::id(),
        &generation,
    )
    .unwrap();
    write_stop_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    let keep_path = all_keep_signal::signal_path(&base, "ph", "originator-A");
    let stop_path = stop_signal_path(&base, "ph", "originator-A");
    assert!(keep_path.exists());
    assert!(stop_path.exists());

    // #2 (trigger_stop) は all_keep_signal::delete_broadcast のみ呼ぶ → all_stop_signal 不触
    all_keep_signal::delete_broadcast(&base, "ph", "originator-A").unwrap();
    assert!(!keep_path.exists(), "keep broadcast deleted (#2)");
    assert!(
        stop_path.exists(),
        "stop broadcast preserved (主バグ修正後)"
    );

    // #3/#4 で初めて all_stop_signal が削除される
    delete_stop_broadcast(&base, "ph", "originator-A").unwrap();
    assert!(!stop_path.exists());
}
