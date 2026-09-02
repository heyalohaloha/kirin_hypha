use super::*;
use std::sync::atomic::{AtomicU64, Ordering};

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_all_keep_test_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write_broadcast(
    base_dir: &Path,
    project_hash: &str,
    originator_post_instance_id: &str,
    daw_session_id: String,
) -> Result<AllKeepBroadcast, AllKeepError> {
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        project_hash.to_string(),
        originator_post_instance_id.to_string(),
        format!("pre-{originator_post_instance_id}"),
        daw_session_id.clone(),
        std::process::id(),
    );
    write_broadcast_for_generation(
        base_dir,
        project_hash,
        originator_post_instance_id,
        daw_session_id,
        std::process::id(),
        &generation,
    )
}

#[test]
fn signals_dir_path_format() {
    let base = PathBuf::from("/base");
    let dir = signals_dir(&base, "ph-A");
    assert_eq!(dir, PathBuf::from("/base/ph-A/all_keep_signal"));
}

#[test]
fn signal_path_format() {
    let base = PathBuf::from("/base");
    let path = signal_path(&base, "ph-A", "iid-X");
    assert_eq!(path, PathBuf::from("/base/ph-A/all_keep_signal/iid-X.json"));
}

#[test]
fn write_broadcast_creates_atomic_file() {
    let base = isolated_dir();
    let result = write_broadcast(&base, "ph", "originator-1", "session-A".to_string()).unwrap();
    assert_eq!(result.v, ALL_KEEP_SCHEMA_VERSION);
    assert_eq!(result.originator_post_instance_id, "originator-1");
    assert_eq!(result.daw_session_id, "session-A");
    assert_eq!(result.host_process_id, std::process::id());
    assert!(!result.started_at.is_empty());
    assert_eq!(result.heartbeat, result.started_at);

    let path = signal_path(&base, "ph", "originator-1");
    assert!(path.exists(), "broadcast file should exist");
    assert_eq!(
        read_current_broadcast(&base, "ph").unwrap(),
        ("originator-1".to_string(), result)
    );
    assert_eq!(crate::atomic_file::remove_temp_siblings(&path).unwrap(), 0);
}

#[test]
fn generation_broadcast_rejects_foreign_project_scope_and_originator() {
    let base = isolated_dir();
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        "project-a".to_string(),
        "post-a".to_string(),
        "pre-a".to_string(),
        "daw-a".to_string(),
        std::process::id(),
    );

    for (project, originator, daw, host) in [
        ("project-b", "post-a", "daw-a", std::process::id()),
        ("project-a", "post-b", "daw-a", std::process::id()),
        ("project-a", "post-a", "daw-b", std::process::id()),
        (
            "project-a",
            "post-a",
            "daw-a",
            std::process::id().saturating_add(1),
        ),
    ] {
        assert!(
                write_broadcast_for_generation(
                    &base,
                    project,
                    originator,
                    daw.to_string(),
                    host,
                    &generation,
                )
                .is_err(),
                "foreign broadcast scope must be rejected: project={project} originator={originator} daw={daw} host={host}"
            );
    }
    assert!(read_current_broadcast(&base, "project-a").is_none());
    assert!(read_current_broadcast(&base, "project-b").is_none());
}

#[test]
fn deleting_old_originator_does_not_remove_new_current_broadcast() {
    let base = isolated_dir();
    write_broadcast(&base, "ph", "originator-old", "session-A".into()).unwrap();
    let newest = write_broadcast(&base, "ph", "originator-new", "session-A".into()).unwrap();

    delete_broadcast(&base, "ph", "originator-old").unwrap();

    assert_eq!(
        read_current_broadcast(&base, "ph"),
        Some(("originator-new".into(), newest))
    );
}

#[test]
fn read_broadcast_roundtrip() {
    let base = isolated_dir();
    let written = write_broadcast(&base, "ph", "originator-1", "session-A".to_string()).unwrap();
    let read = read_broadcast(&base, "ph", "originator-1").unwrap();
    assert_eq!(read, written);
}

#[test]
fn read_broadcast_missing_returns_none() {
    let base = isolated_dir();
    assert!(read_broadcast(&base, "ph", "no-such-originator").is_none());
}

#[test]
fn delete_broadcast_removes_file() {
    let base = isolated_dir();
    write_broadcast(&base, "ph", "originator-1", "session-A".to_string()).unwrap();
    let path = signal_path(&base, "ph", "originator-1");
    assert!(path.exists());
    delete_broadcast(&base, "ph", "originator-1").unwrap();
    assert!(!path.exists());
}

#[test]
fn delete_broadcast_missing_is_ok() {
    let base = isolated_dir();
    // 不在でも Ok が返る (R-28 機能的沈黙)
    assert!(delete_broadcast(&base, "ph", "no-such-originator").is_ok());
}

#[test]
fn scan_broadcasts_dir_empty_when_dir_missing() {
    let base = isolated_dir();
    let v = scan_broadcasts_dir(&base, "no-such-ph");
    assert!(v.is_empty());
}

#[test]
fn scan_broadcasts_dir_returns_all_valid() {
    let base = isolated_dir();
    write_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    write_broadcast(&base, "ph", "originator-B", "session-A".to_string()).unwrap();
    write_broadcast(&base, "ph", "originator-C", "session-B".to_string()).unwrap();
    let v = scan_broadcasts_dir(&base, "ph");
    assert_eq!(v.len(), 3);
    // 辞書順
    assert_eq!(v[0].0, "originator-A");
    assert_eq!(v[1].0, "originator-B");
    assert_eq!(v[2].0, "originator-C");
    assert_eq!(v[2].1.daw_session_id, "session-B");
}

#[test]
fn scan_broadcasts_dir_skips_corrupt_json() {
    let base = isolated_dir();
    let dir = signals_dir(&base, "ph");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("corrupt.json"), b"{ not valid json").unwrap();
    write_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    let v = scan_broadcasts_dir(&base, "ph");
    // corrupt は skip / valid 1 件のみ返る
    assert_eq!(v.len(), 1);
    assert_eq!(v[0].0, "originator-A");
}

#[test]
fn scan_broadcasts_dir_skips_non_json_files() {
    let base = isolated_dir();
    let dir = signals_dir(&base, "ph");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("note.txt"), b"hello").unwrap();
    fs::write(dir.join("README.md"), b"docs").unwrap();
    write_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    let v = scan_broadcasts_dir(&base, "ph");
    assert_eq!(v.len(), 1);
    assert_eq!(v[0].0, "originator-A");
}

#[test]
fn broadcast_persists_across_scan_calls() {
    // §4-5 Step 4: broadcast 寿命の正本仕様 (originator が Stop/Drop/Shutdown
    // を実行するまで保持) を assert 化。write 後の repeated scan で file が
    // 残存し続け、明示的 delete_broadcast を呼ぶまで消滅しないことを保証する。
    let base = isolated_dir();
    write_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    let path = signal_path(&base, "ph", "originator-A");
    assert!(path.exists(), "write 直後 file 存在");

    // scan を 5 回繰り返しても file は消滅しない (read-only semantics)
    for i in 0..5 {
        let v = scan_broadcasts_dir(&base, "ph");
        assert_eq!(v.len(), 1, "scan #{} で 1 件残存", i);
        assert!(path.exists(), "scan #{} 後も file 残存", i);
    }

    // read_broadcast も非破壊
    for i in 0..3 {
        let r = read_broadcast(&base, "ph", "originator-A");
        assert!(r.is_some(), "read #{} は Some", i);
        assert!(path.exists(), "read #{} 後も file 残存", i);
    }

    // 明示的 delete_broadcast 呼出時のみ消える
    delete_broadcast(&base, "ph", "originator-A").unwrap();
    assert!(!path.exists(), "delete_broadcast 後は不在");
}

#[test]
fn write_broadcast_overwrites_same_originator() {
    // 同一 originator の連打 → atomic rename で last-wins (Q-A8-6)
    let base = isolated_dir();
    let first = write_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    // started_at が確実に進むよう 1 秒待機 (秒精度 ISO 8601)
    std::thread::sleep(std::time::Duration::from_millis(1100));
    let second = write_broadcast(&base, "ph", "originator-A", "session-A".to_string()).unwrap();
    assert_ne!(first.started_at, second.started_at);
    let read = read_broadcast(&base, "ph", "originator-A").unwrap();
    assert_eq!(read.started_at, second.started_at);
}

#[test]
fn is_broadcast_stale_detects_30s_threshold() {
    let now = Utc::now();
    let fresh = AllKeepBroadcast {
        v: 1,
        originator_post_instance_id: "x".to_string(),
        daw_session_id: "y".to_string(),
        host_process_id: 0,
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        started_at: (now - chrono::Duration::seconds(10))
            .format("%Y-%m-%dT%H:%M:%SZ")
            .to_string(),
        heartbeat: String::new(),
    };
    let stale = AllKeepBroadcast {
        v: 1,
        originator_post_instance_id: "x".to_string(),
        daw_session_id: "y".to_string(),
        host_process_id: 0,
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        started_at: (now - chrono::Duration::seconds(45))
            .format("%Y-%m-%dT%H:%M:%SZ")
            .to_string(),
        heartbeat: String::new(),
    };
    assert!(!is_broadcast_stale(
        &fresh,
        now,
        ALL_KEEP_BROADCAST_STALE_SECS
    ));
    assert!(is_broadcast_stale(
        &stale,
        now,
        ALL_KEEP_BROADCAST_STALE_SECS
    ));
}

#[test]
fn is_broadcast_stale_invalid_iso_returns_false() {
    // パース失敗 → 安全側で false (= 新鮮扱い / 誤 skip 防止)
    let now = Utc::now();
    let bad = AllKeepBroadcast {
        v: 1,
        originator_post_instance_id: "x".to_string(),
        daw_session_id: "y".to_string(),
        host_process_id: 0,
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        started_at: "not-an-iso".to_string(),
        heartbeat: String::new(),
    };
    assert!(!is_broadcast_stale(
        &bad,
        now,
        ALL_KEEP_BROADCAST_STALE_SECS
    ));
}

#[test]
fn is_broadcast_stale_future_time_is_fresh() {
    // 未来時刻 (clock-skew 等) → 安全側で false (= 新鮮扱い)
    let now = Utc::now();
    let future = AllKeepBroadcast {
        v: 1,
        originator_post_instance_id: "x".to_string(),
        daw_session_id: "y".to_string(),
        host_process_id: 0,
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        started_at: (now + chrono::Duration::seconds(60))
            .format("%Y-%m-%dT%H:%M:%SZ")
            .to_string(),
        heartbeat: String::new(),
    };
    assert!(!is_broadcast_stale(
        &future,
        now,
        ALL_KEEP_BROADCAST_STALE_SECS
    ));
}

#[test]
fn schema_version_is_2() {
    assert_eq!(ALL_KEEP_SCHEMA_VERSION, 2);
}

#[test]
fn old_schema_without_heartbeat_parses() {
    // 旧 schema (heartbeat 不在) → serde default で空文字
    let json = r#"{
            "v": 1,
            "originator_post_instance_id": "iid-A",
            "daw_session_id": "sess-A",
            "started_at": "2026-05-04T12:00:00Z"
        }"#;
    let parsed: AllKeepBroadcast = serde_json::from_str(json).unwrap();
    assert_eq!(parsed.heartbeat, "");
    assert_eq!(parsed.host_process_id, 0);
    assert_eq!(parsed.started_at, "2026-05-04T12:00:00Z");
}

#[test]
fn cross_session_broadcasts_distinguished_by_daw_session_id() {
    let base = isolated_dir();
    write_broadcast(&base, "ph", "originator-X", "session-OLD".to_string()).unwrap();
    write_broadcast(&base, "ph", "originator-Y", "session-NEW".to_string()).unwrap();
    let v = scan_broadcasts_dir(&base, "ph");
    assert_eq!(v.len(), 2);
    let map: std::collections::HashMap<_, _> = v
        .iter()
        .map(|(k, b)| (k.clone(), b.daw_session_id.clone()))
        .collect();
    assert_eq!(map.get("originator-X"), Some(&"session-OLD".to_string()));
    assert_eq!(map.get("originator-Y"), Some(&"session-NEW".to_string()));
}
