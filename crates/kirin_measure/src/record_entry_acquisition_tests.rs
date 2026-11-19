use super::*;
use std::sync::{Arc, Barrier};

#[test]
fn concurrent_stale_entry_acquisitions_have_one_disk_owner() {
    let base = super::tests::isolated_base();
    claim_record_entry(&base, "ph", "stale-race", Role::Post, "post").unwrap();
    let path = entry_lock_path(&base, "ph", "stale-race", Role::Post, "post");
    let mut old: RecordEntryLockFile = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    old.heartbeat_at_ms = now_epoch_ms() - RECORD_ENTRY_LOCK_STALE_MS - 10_000;
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&old).unwrap()).unwrap();
    let gate = Arc::new(Barrier::new(2));
    let outcomes = std::thread::scope(|scope| {
        let jobs: Vec<_> = (0..2)
            .map(|_| {
                let gate = gate.clone();
                let base = &base;
                scope.spawn(move || {
                    gate.wait();
                    claim_record_entry(base, "ph", "stale-race", Role::Post, "post")
                })
            })
            .collect();
        jobs.into_iter()
            .map(|job| job.join().unwrap())
            .collect::<Vec<_>>()
    });
    assert_eq!(outcomes.iter().filter(|r| r.is_ok()).count(), 1);
    assert!(outcomes
        .iter()
        .filter_map(|r| r.as_ref().err())
        .all(|e| matches!(e, RecordEntryLockError::AlreadyActive { .. })));
    let current: RecordEntryLockFile = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    assert_ne!(current.owner_id, old.owner_id);
    assert!(current.heartbeat_at_ms > old.heartbeat_at_ms);
}

#[test]
fn reserved_path_rejects_other_thread_without_blocking_other_entries() {
    let base = super::tests::isolated_base();
    let dir = entry_lock_dir(&base, "ph", "reserved");
    fs::create_dir_all(&dir).unwrap();
    let path = entry_lock_path(&base, "ph", "reserved", Role::Post, "post");
    let guard = acquisition::reserve(&path).unwrap();
    std::thread::scope(|scope| {
        scope
            .spawn(|| {
                assert!(matches!(
                    claim_record_entry(&base, "ph", "reserved", Role::Post, "post"),
                    Err(RecordEntryLockError::AlreadyActive { .. })
                ));
                claim_record_entry(&base, "ph", "other", Role::Post, "post").unwrap();
            })
            .join()
            .unwrap();
    });
    drop(guard);
    claim_record_entry(&base, "ph", "reserved", Role::Post, "post").unwrap();
}

#[test]
fn failed_disk_read_releases_acquisition_reservation() {
    let base = super::tests::isolated_base();
    let dir = entry_lock_dir(&base, "ph", "failure");
    fs::create_dir_all(&dir).unwrap();
    let path = entry_lock_path(&base, "ph", "failure", Role::Post, "post");
    fs::write(&path, b"invalid-json").unwrap();
    assert!(claim_record_entry(&base, "ph", "failure", Role::Post, "post").is_err());
    fs::remove_file(&path).unwrap();
    claim_record_entry(&base, "ph", "failure", Role::Post, "post").unwrap();
}
