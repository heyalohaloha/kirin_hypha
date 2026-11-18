use super::*;
use crate::atomic_file::durable::test_hook::syncs_during;
#[test]
fn owner_release_syncs_both_entries_wrong_owner_changes_nothing() {
    let root = tempfile::tempdir().unwrap();
    let mut claim = claim_writer(root.path(), "project", "session", Role::Post, "post").unwrap();
    claim.mark_ready().unwrap();
    let wrong = syncs_during(false, || {
        assert!(release_if_current_owner(&claim.path, "not-owner").is_err());
    });
    assert!(wrong.is_empty());
    let events = syncs_during(false, || {
        release_if_current_owner(&claim.path, &claim.claim.owner_id).unwrap()
    });
    #[cfg(unix)]
    assert_eq!(
        events
            .iter()
            .filter(|(kind, _)| *kind == "directory")
            .count(),
        2
    );
    #[cfg(not(unix))]
    assert!(events.is_empty());
    assert!(!claim.path.exists());
    assert!(!claim_state_path(&claim.path).exists());
}

#[test]
fn ready_and_closed_sync_but_heartbeat_does_not() {
    for fail_sync in [false, true] {
        let root = tempfile::tempdir().unwrap();
        let mut guard =
            claim_writer(root.path(), "project", "session", Role::Post, "post").unwrap();
        for closed in [false, true] {
            let events = syncs_during(fail_sync, || {
                if closed {
                    guard.mark_closed().unwrap();
                } else {
                    guard.mark_ready().unwrap();
                }
            });
            assert_eq!(events.iter().filter(|(kind, _)| *kind == "file").count(), 1);
            #[cfg(unix)]
            assert!(events
                .iter()
                .any(|(kind, path)| *kind == "directory"
                    && Some(path.as_path()) == guard.path.parent()));
            let value = read_claim_snapshot(&guard.path).unwrap().unwrap();
            assert!(value.ready_at_ms.is_some());
            assert_eq!(value.closed_at_ms.is_some(), closed);
            if !closed {
                let events = syncs_during(fail_sync, || guard.heartbeat().unwrap());
                assert!(
                    events.is_empty(),
                    "heartbeat remains asynchronous: {events:?}"
                );
            }
        }
    }
}

#[test]
fn transition_write_failure_does_not_publish_ready_or_closed() {
    for closed in [false, true] {
        let root = tempfile::tempdir().unwrap();
        let mut guard =
            claim_writer(root.path(), "project", "session", Role::Post, "post").unwrap();
        let state = claim_state_path(&guard.path);
        fs::create_dir(&state).unwrap();
        fs::write(state.join("block"), b"unchanged").unwrap();
        assert!(if closed {
            guard.mark_closed()
        } else {
            guard.mark_ready()
        }
        .is_err());
        assert!(!guard.finalized);
        assert_eq!(fs::read(state.join("block")).unwrap(), b"unchanged");
        let value = read_claim_snapshot(&guard.path).unwrap().unwrap();
        assert!(value.ready_at_ms.is_none() && value.closed_at_ms.is_none());
        assert!(guard.claim.ready_at_ms.is_none() && guard.claim.closed_at_ms.is_none());
        fs::remove_dir_all(&state).unwrap();
        let events = syncs_during(false, || guard.heartbeat().unwrap());
        assert!(events.is_empty());
        let after_heartbeat = read_claim_snapshot(&guard.path).unwrap().unwrap();
        assert!(after_heartbeat.ready_at_ms.is_none() && after_heartbeat.closed_at_ms.is_none());
        assert!(!guard.finalized);
    }
}

#[test]
fn damaged_sidecar_uses_only_original_heartbeat_and_never_readiness() {
    for bytes in [b"".as_slice(), b"{\"owner_id\":".as_slice()] {
        let root = tempfile::tempdir().unwrap();
        let mut guard =
            claim_writer(root.path(), "project", "session", Role::Post, "post").unwrap();
        guard.mark_ready().unwrap();
        fs::write(claim_state_path(&guard.path), bytes).unwrap();
        let value = read_claim_snapshot(&guard.path).unwrap().unwrap();
        assert_eq!(value.heartbeat_at_ms, guard.claim.claimed_at_ms);
        assert!(value.ready_at_ms.is_none());
        assert!(matches!(
            claim_writer(root.path(), "project", "session", Role::Post, "post"),
            Err(WriterClaimError::AlreadyActive { .. })
        ));
        guard.claim.heartbeat_at_ms = now_epoch_ms() - WRITER_CLAIM_STALE_MS - 1;
        fs::write(&guard.path, serde_json::to_vec(&guard.claim).unwrap()).unwrap();
        let replacement =
            claim_writer(root.path(), "project", "session", Role::Post, "post").unwrap();
        assert_ne!(replacement.claim.owner_id, guard.claim.owner_id);
    }
}

#[test]
fn damaged_immutable_owner_waits_for_existing_deadline() {
    for bytes in [b"".as_slice(), b"{".as_slice()] {
        let root = tempfile::tempdir().unwrap();
        let guard = claim_writer(root.path(), "project", "session", Role::Post, "post").unwrap();
        fs::write(&guard.path, bytes).unwrap();
        assert!(matches!(
            claim_writer(root.path(), "project", "session", Role::Post, "post"),
            Err(WriterClaimError::AlreadyActive { .. })
        ));
        assert_eq!(fs::read(&guard.path).unwrap(), bytes);
    }
}
