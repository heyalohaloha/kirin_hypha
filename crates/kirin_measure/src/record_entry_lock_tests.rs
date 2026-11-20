use super::*;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

static TEST_SEQ: AtomicU64 = AtomicU64::new(0);

pub(super) fn isolated_base() -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "kirin_record_entry_lock_test_{}_{}",
        std::process::id(),
        TEST_SEQ.fetch_add(1, Ordering::Relaxed)
    ));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn same_session_role_instance_rejects_second_active_entry() {
    let base = isolated_base();

    claim_record_entry(&base, "ph", "session-a", Role::Post, "post-1").unwrap();
    let second = claim_record_entry(&base, "ph", "session-a", Role::Post, "post-1");

    assert!(matches!(
        second,
        Err(RecordEntryLockError::AlreadyActive { .. })
    ));
}

#[test]
fn different_role_can_enter_same_session() {
    let base = isolated_base();

    claim_record_entry(&base, "ph", "session-b", Role::Post, "post-1").unwrap();
    let pre = claim_record_entry(&base, "ph", "session-b", Role::Pre, "pre-1");

    assert!(pre.is_ok());
}

#[test]
fn stale_entry_can_be_reclaimed() {
    let base = isolated_base();
    claim_record_entry(&base, "ph", "session-stale", Role::Post, "post-1").unwrap();
    let path = entry_lock_path(&base, "ph", "session-stale", Role::Post, "post-1");
    let mut lock: RecordEntryLockFile = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    lock.heartbeat_at_ms = now_epoch_ms() - RECORD_ENTRY_LOCK_STALE_MS - 1_000;
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&lock).unwrap()).unwrap();

    let reclaimed = claim_record_entry(&base, "ph", "session-stale", Role::Post, "post-1");

    assert!(reclaimed.is_ok());
}

#[test]
fn concurrent_entry_claims_exactly_one_wins() {
    const THREADS: usize = 12;
    let base = Arc::new(isolated_base());
    let mut starts = Vec::new();
    let outcomes = Arc::new(Mutex::new(Vec::new()));
    let mut handles = Vec::new();

    for _ in 0..THREADS {
        let base = Arc::clone(&base);
        let (start, ready) = std::sync::mpsc::sync_channel(1);
        starts.push(start);
        let outcomes = Arc::clone(&outcomes);
        handles.push(std::thread::spawn(move || {
            ready
                .recv_timeout(std::time::Duration::from_secs(5))
                .unwrap();
            let outcome =
                claim_record_entry(&base, "ph", "session-concurrent", Role::Post, "post-1")
                    .map(|_| "created")
                    .unwrap_or_else(|e| match e {
                        RecordEntryLockError::AlreadyActive { .. } => "already_active",
                        other => panic!("unexpected entry lock outcome: {other}"),
                    });
            outcomes.lock().unwrap().push(outcome);
        }));
    }

    for start in starts {
        start.send(()).unwrap();
    }
    for handle in handles {
        handle.join().unwrap();
    }

    let outcomes = outcomes.lock().unwrap();
    assert_eq!(
        outcomes
            .iter()
            .filter(|outcome| **outcome == "created")
            .count(),
        1
    );
    assert_eq!(
        outcomes
            .iter()
            .filter(|outcome| **outcome == "already_active")
            .count(),
        THREADS - 1
    );
}
