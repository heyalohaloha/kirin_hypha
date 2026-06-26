use super::*;
use std::sync::atomic::{AtomicU64, Ordering};

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_signal_test_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

// ── スキーマ・パス ──────────────────────────────────────

#[test]
fn signal_status_serialization_is_lowercase() {
    let s = serde_json::to_string(&SignalStatus::Pending).unwrap();
    assert_eq!(s, "\"pending\"");
    let s = serde_json::to_string(&SignalStatus::Acknowledged).unwrap();
    assert_eq!(s, "\"acknowledged\"");
    let s = serde_json::to_string(&SignalStatus::Released).unwrap();
    assert_eq!(s, "\"released\"");
}

#[test]
fn signal_roundtrip_preserves_all_fields() {
    let s = RecordSignal::new_pending("post-001".into(), "pre-xyz".into(), "daw-uuid-1".into());
    let json = serde_json::to_string(&s).unwrap();
    let back: RecordSignal = serde_json::from_str(&json).unwrap();
    assert_eq!(s, back);
}

#[test]
fn signal_path_uses_post_instance_id_as_filename() {
    let p = signal_path(Path::new("/tmp/kb"), "phash", "post-uuid-A");
    assert_eq!(p, Path::new("/tmp/kb/phash/record_signal/post-uuid-A.json"));
}

// ── I/O ─────────────────────────────────────────────────

#[test]
fn write_pending_creates_file_with_pending_status() {
    let base = isolated_dir();
    let s = write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    assert_eq!(s.status, SignalStatus::Pending);
    assert_eq!(s.requested_by, "post-1");
    assert_eq!(s.target_pre_instance_id, "pre-1");
    assert_eq!(s.daw_session_id, "daw-1");
    let loaded = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(loaded, s);
}

#[test]
fn read_signal_missing_returns_none() {
    let base = isolated_dir();
    assert!(read_signal(&base, "ph", "post-x").is_none());
}

#[test]
fn read_signal_corrupt_returns_none() {
    let base = isolated_dir();
    let dir = signals_dir(&base, "ph");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("post-x.json"), b"not json").unwrap();
    assert!(read_signal(&base, "ph", "post-x").is_none());
}

#[test]
fn mark_acknowledged_updates_status_and_returns_true() {
    let base = isolated_dir();
    write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    let changed = mark_acknowledged(&base, "ph", "post-1").unwrap();
    assert!(changed);
    let loaded = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(loaded.status, SignalStatus::Acknowledged);
    assert_eq!(
        loaded.daw_session_id, "daw-1",
        "daw_session_id preserved on transition"
    );
}

#[test]
fn started_at_preserved_across_transitions() {
    let base = isolated_dir();
    let initial = write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    let first_started = initial.started_at.clone();
    assert!(!first_started.is_empty(), "started_at must be set");
    std::thread::sleep(std::time::Duration::from_millis(1100));
    mark_acknowledged(&base, "ph", "post-1").unwrap();
    let acked = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(acked.started_at, first_started);
    assert_ne!(acked.t, first_started, "t should have advanced");
    mark_released(&base, "ph", "post-1").unwrap();
    let released = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(released.started_at, first_started);
}

#[test]
fn legacy_schema_without_daw_session_id_defaults_to_empty() {
    let base = isolated_dir();
    let dir = signals_dir(&base, "ph");
    fs::create_dir_all(&dir).unwrap();
    let legacy = r#"{"status":"pending","requested_by":"post-1","target_pre_instance_id":"pre-1","t":"2026-01-01T00:00:00Z","started_at":"2026-01-01T00:00:00Z"}"#;
    fs::write(dir.join("post-1.json"), legacy).unwrap();
    let loaded = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(loaded.daw_session_id, "");
}

/// B-023 段階 3: paired_pre_name 不在の旧 schema 読込で空文字 default に
/// なること (daw_session_id / started_at と同パターン / R-28 機能的沈黙)。
#[test]
fn legacy_schema_without_paired_pre_name_defaults_to_empty() {
    let base = isolated_dir();
    let dir = signals_dir(&base, "ph");
    fs::create_dir_all(&dir).unwrap();
    let legacy = r#"{"status":"acknowledged","requested_by":"post-1","target_pre_instance_id":"pre-1","daw_session_id":"daw-1","t":"2026-01-01T00:00:00Z","started_at":"2026-01-01T00:00:00Z"}"#;
    fs::write(dir.join("post-1.json"), legacy).unwrap();
    let loaded = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(loaded.paired_pre_name, "");
    assert_eq!(loaded.status, SignalStatus::Acknowledged);
}

/// B-023 段階 3: mark_acknowledged_with_name で渡した name が
/// signal.paired_pre_name に永続化されること。
#[test]
fn mark_acknowledged_with_name_persists_name() {
    let base = isolated_dir();
    write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    let changed = mark_acknowledged_with_name(&base, "ph", "post-1", "Studio Mix").unwrap();
    assert!(changed);
    let loaded = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(loaded.status, SignalStatus::Acknowledged);
    assert_eq!(loaded.paired_pre_name, "Studio Mix");
}

/// B-023 段階 3: 既存 mark_acknowledged 呼出 (= wrapper) は paired_pre_name
/// を空文字で書く (新旧呼出共存の確認)。
#[test]
fn mark_acknowledged_legacy_calls_with_name_empty() {
    let base = isolated_dir();
    write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    let changed = mark_acknowledged(&base, "ph", "post-1").unwrap();
    assert!(changed);
    let loaded = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(loaded.status, SignalStatus::Acknowledged);
    assert_eq!(loaded.paired_pre_name, "");
}

#[test]
fn mark_released_updates_status() {
    let base = isolated_dir();
    write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    mark_released(&base, "ph", "post-1").unwrap();
    let loaded = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(loaded.status, SignalStatus::Released);
}

#[test]
fn transition_on_missing_returns_false() {
    let base = isolated_dir();
    let changed = mark_acknowledged(&base, "ph", "post-x").unwrap();
    assert!(!changed);
}

#[test]
fn delete_signal_removes_file() {
    let base = isolated_dir();
    write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    delete_signal(&base, "ph", "post-1").unwrap();
    assert!(read_signal(&base, "ph", "post-1").is_none());
}

#[test]
fn delete_signal_on_missing_is_ok() {
    let base = isolated_dir();
    delete_signal(&base, "ph", "post-x").unwrap();
}

/// B-027 段階 3-B α-7 / Group 2 (Gap-6 局所対処): 統合点 #2/#3/#4 の
/// 3 経路から重複呼出されても全て Ok で終わり、file が不在のまま安定する。
/// (#2 trigger_stop / #3 HyphaPost::drop / #4 IO Thread terminate)
#[test]
fn delete_signal_idempotent_under_repeated_calls() {
    let base = isolated_dir();
    write_pending(&base, "ph", "post-rep", "pre-1".into(), "daw-1".into()).unwrap();
    // 1 回目: 実削除
    delete_signal(&base, "ph", "post-rep").unwrap();
    assert!(read_signal(&base, "ph", "post-rep").is_none());
    // 2 回目以降: NotFound → Ok (冪等)
    delete_signal(&base, "ph", "post-rep").unwrap();
    delete_signal(&base, "ph", "post-rep").unwrap();
    assert!(read_signal(&base, "ph", "post-rep").is_none());
}

#[test]
fn atomic_write_leaves_no_tmp_behind() {
    let base = isolated_dir();
    write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    let final_path = signal_path(&base, "ph", "post-1");
    let tmp = tmp_path(&final_path);
    assert!(final_path.exists());
    assert!(!tmp.exists(), "tmp must be renamed away: {tmp:?}");
}

// ── scan_signals_dir ────────────────────────────────────

#[test]
fn scan_signals_returns_empty_when_dir_missing() {
    let base = isolated_dir();
    let v = scan_signals_dir(&base, "ph");
    assert!(v.is_empty());
}

#[test]
fn scan_signals_returns_all_in_alphabetical_order() {
    let base = isolated_dir();
    write_pending(&base, "ph", "post-b", "pre-1".into(), "daw-1".into()).unwrap();
    write_pending(&base, "ph", "post-a", "pre-1".into(), "daw-1".into()).unwrap();
    write_pending(&base, "ph", "post-c", "pre-2".into(), "daw-1".into()).unwrap();
    let v = scan_signals_dir(&base, "ph");
    assert_eq!(v.len(), 3);
    assert_eq!(v[0].0, "post-a");
    assert_eq!(v[1].0, "post-b");
    assert_eq!(v[2].0, "post-c");
}

#[test]
fn scan_signals_skips_corrupt_and_non_json() {
    let base = isolated_dir();
    let dir = signals_dir(&base, "ph");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("bad.json"), b"{ invalid").unwrap();
    fs::write(dir.join("readme.txt"), b"ignore me").unwrap();
    write_pending(&base, "ph", "post-good", "pre-1".into(), "daw-1".into()).unwrap();
    let v = scan_signals_dir(&base, "ph");
    assert_eq!(v.len(), 1);
    assert_eq!(v[0].0, "post-good");
}

// ── タイムアウト ────────────────────────────────────────

fn iso_ago(secs: i64, now: DateTime<Utc>) -> String {
    let t = now - chrono::Duration::seconds(secs);
    t.format("%Y-%m-%dT%H:%M:%SZ").to_string()
}

#[test]
fn timeout_fires_strictly_after_30s() {
    let now = Utc::now();
    let mut sig = RecordSignal::new_pending("p".into(), "r".into(), "d".into());
    sig.t = iso_ago(30, now);
    assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    sig.t = iso_ago(31, now);
    assert!(is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
}

#[test]
fn timeout_not_considered_for_non_pending() {
    let now = Utc::now();
    let mut sig = RecordSignal::new_pending("p".into(), "r".into(), "d".into());
    sig.t = iso_ago(3600, now);
    sig.status = SignalStatus::Acknowledged;
    assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
    sig.status = SignalStatus::Released;
    assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
}

#[test]
fn timeout_invalid_iso_is_false() {
    let now = Utc::now();
    let mut sig = RecordSignal::new_pending("p".into(), "r".into(), "d".into());
    sig.t = "not-iso".to_string();
    assert!(!is_timed_out(&sig, now, ACK_TIMEOUT_SECONDS));
}

// ── シーケンス統合 ──────────────────────────────────────

#[test]
fn full_post_to_pre_handshake_sequence() {
    let base = isolated_dir();
    let sig = write_pending(&base, "ph", "post-1", "pre-1".into(), "daw-1".into()).unwrap();
    assert_eq!(sig.status, SignalStatus::Pending);
    assert_eq!(sig.daw_session_id, "daw-1");

    let scanned = scan_signals_dir(&base, "ph");
    assert_eq!(scanned.len(), 1);
    assert_eq!(scanned[0].0, "post-1");
    assert_eq!(scanned[0].1.target_pre_instance_id, "pre-1");

    mark_acknowledged(&base, "ph", "post-1").unwrap();
    mark_released(&base, "ph", "post-1").unwrap();
    let after = read_signal(&base, "ph", "post-1").unwrap();
    assert_eq!(after.status, SignalStatus::Released);
    delete_signal(&base, "ph", "post-1").unwrap();
    assert!(read_signal(&base, "ph", "post-1").is_none());
}

// ── B-103: sweep_stale_pending_in（起動時 dead Pending 掃除）─────────────────────

/// status を保ったまま `t` を `secs_ago` 古くする（stale fixture 作成）。`now` 共有で境界を厳密化。
fn backdate_signal_t(base: &Path, ph: &str, post_iid: &str, now: DateTime<Utc>, secs_ago: i64) {
    let mut sig = read_signal(base, ph, post_iid).expect("signal exists");
    sig.t = iso_ago(secs_ago, now);
    write_signal(base, ph, post_iid, &sig).unwrap();
}

/// 古い Pending（age > STALE_PENDING_SECS）だけ掃除し、fresh Pending と Acknowledged は保持。
#[test]
fn sweep_stale_pending_removes_only_dead_pending() {
    let base = isolated_dir();
    let now = Utc::now();
    // (a) stale Pending（書込 POST 消失相当）: t を STALE_PENDING_SECS+10 古く。
    write_pending(&base, "ph", "post-stale", "pre-x".into(), "daw".into()).unwrap();
    backdate_signal_t(&base, "ph", "post-stale", now, STALE_PENDING_SECS + 10);
    // (b) fresh Pending（進行中 Keep）: t=now。
    write_pending(&base, "ph", "post-fresh", "pre-x".into(), "daw".into()).unwrap();
    // (c) Acknowledged（古くても対象外＝生記録扱い）。
    write_pending(&base, "ph", "post-ack", "pre-y".into(), "daw".into()).unwrap();
    mark_acknowledged(&base, "ph", "post-ack").unwrap();
    backdate_signal_t(&base, "ph", "post-ack", now, STALE_PENDING_SECS + 10);

    let cleared = sweep_stale_pending_in(&base, now, STALE_PENDING_SECS);
    assert_eq!(cleared, 1, "stale Pending 1 件のみ掃除");
    assert!(
        read_signal(&base, "ph", "post-stale").is_none(),
        "stale Pending は削除"
    );
    assert!(
        read_signal(&base, "ph", "post-fresh").is_some(),
        "fresh Pending は保持"
    );
    assert!(
        read_signal(&base, "ph", "post-ack").is_some(),
        "Acknowledged は保持（age 無関係）"
    );
}

/// しきい値ちょうど(=stale_secs)は保持、超過(+1)で掃除（is_timed_out と同じ strict-greater 境界）。
#[test]
fn sweep_stale_pending_boundary() {
    let base = isolated_dir();
    let now = Utc::now();
    write_pending(&base, "ph", "post-edge", "pre".into(), "daw".into()).unwrap();
    backdate_signal_t(&base, "ph", "post-edge", now, STALE_PENDING_SECS);
    assert_eq!(
        sweep_stale_pending_in(&base, now, STALE_PENDING_SECS),
        0,
        "境界ちょうど = 保持"
    );
    backdate_signal_t(&base, "ph", "post-edge", now, STALE_PENDING_SECS + 1);
    assert_eq!(
        sweep_stale_pending_in(&base, now, STALE_PENDING_SECS),
        1,
        "超過 = 掃除"
    );
}
