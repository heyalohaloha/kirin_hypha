use super::*;
use std::fs::{File, FileTimes};
use std::time::Duration;

fn set_mtime(path: &Path, ms: i64) {
    File::options()
        .write(true)
        .open(path)
        .unwrap()
        .set_times(FileTimes::new().set_modified(UNIX_EPOCH + Duration::from_millis(ms as u64)))
        .unwrap();
}

fn sidecar(path: &Path, mtime: i64, heartbeat: i64) {
    let value = WriterClaimStateFile {
        schema_version: WRITER_CLAIM_SCHEMA.into(),
        owner_id: "untrusted".into(),
        heartbeat_at_ms: heartbeat,
        ready_at_ms: Some(heartbeat),
        closed_at_ms: Some(heartbeat),
    };
    let state = claim_state_path(path);
    fs::write(&state, serde_json::to_vec(&value).unwrap()).unwrap();
    set_mtime(&state, mtime);
}

#[test]
fn corrupt_body_matrix_uses_latest_time_and_exact_existing_boundary() {
    let old = 1_700_000_000_000;
    for bytes in [b"".as_slice(), b"{".as_slice()] {
        for state_kind in 0..4 {
            for unreadable in [false, true] {
                let root = tempfile::tempdir().unwrap();
                let path = root.path().join("claim.json");
                fs::write(&path, bytes).unwrap();
                set_mtime(&path, old);
                let newest = match state_kind {
                    0 => old,
                    1 => {
                        sidecar(&path, old - 1000, old - 2000);
                        old
                    }
                    2 => {
                        sidecar(&path, old + 1000, old - 2000);
                        old + 1000
                    }
                    _ => {
                        sidecar(&path, old - 1000, old + 2000);
                        old + 2000
                    }
                };
                for offset in [-1, 0, 1] {
                    // Inject only body read failure; actual independent filesystem
                    // timestamp/state collection runs unchanged, also under root CI.
                    let result = snapshot_for_reclaim_with(
                        &path,
                        newest + WRITER_CLAIM_STALE_MS + offset,
                        |path| {
                            if unreadable {
                                Err(io::Error::new(
                                    io::ErrorKind::PermissionDenied,
                                    "injected body read",
                                )
                                .into())
                            } else {
                                read_claim_snapshot(path)
                            }
                        },
                    );
                    if offset <= 0 {
                        assert!(matches!(
                            result,
                            Err(WriterClaimError::AlreadyActive { .. })
                        ));
                    } else {
                        assert!(matches!(result, Ok(None)));
                    }
                    assert_eq!(fs::read(&path).unwrap(), bytes);
                }
            }
        }
    }
}

#[test]
fn damaged_body_real_claim_api_waits_then_reclaims_without_readiness() {
    for bytes in [b"".as_slice(), b"{".as_slice()] {
        let root = tempfile::tempdir().unwrap();
        let original = claim_writer(root.path(), "p", "s", Role::Post, "i").unwrap();
        fs::write(&original.path, bytes).unwrap();
        assert!(matches!(
            claim_writer(root.path(), "p", "s", Role::Post, "i"),
            Err(WriterClaimError::AlreadyActive { .. })
        ));
        let old = now_epoch_ms() - WRITER_CLAIM_STALE_MS - 10_000;
        set_mtime(&original.path, old);
        sidecar(&original.path, old, old);
        let replacement = claim_writer(root.path(), "p", "s", Role::Post, "i").unwrap();
        assert_ne!(original.claim.owner_id, replacement.claim.owner_id);
        let value = read_claim_snapshot(&replacement.path).unwrap().unwrap();
        assert!(value.ready_at_ms.is_none() && value.closed_at_ms.is_none());
    }
}

#[test]
fn repaired_or_refreshed_claim_is_not_removed() {
    let root = tempfile::tempdir().unwrap();
    let owner = claim_writer(root.path(), "p", "s", Role::Post, "i").unwrap();
    let old = now_epoch_ms() - WRITER_CLAIM_STALE_MS - 10_000;
    set_mtime(&owner.path, old);
    let mut reads = 0;
    let result = snapshot_for_reclaim_with(&owner.path, now_epoch_ms(), |path| {
        reads += 1;
        if reads == 1 {
            Err(io::Error::other("injected transient read").into())
        } else {
            read_claim_snapshot(path)
        }
    });
    assert!(matches!(
        result,
        Err(WriterClaimError::AlreadyActive { .. })
    ));
    fs::write(&owner.path, b"{").unwrap();
    set_mtime(&owner.path, old);
    let mut reads = 0;
    let result = snapshot_for_reclaim_with(&owner.path, now_epoch_ms(), |path| {
        reads += 1;
        if reads == 2 {
            sidecar(path, now_epoch_ms(), now_epoch_ms());
        }
        read_claim_snapshot(path)
    });
    assert!(matches!(
        result,
        Err(WriterClaimError::AlreadyActive { .. })
    ));
    assert_eq!(fs::read(&owner.path).unwrap(), b"{");
}

#[test]
fn readable_owner_recovery_uses_all_four_times_without_renewing_readiness() {
    let old = 1_700_000_000_000;
    for newest_field in 0..4 {
        let root = tempfile::tempdir().unwrap();
        let owner = claim_writer(root.path(), "p", "s", Role::Post, "i").unwrap();
        let mut body = owner.claim.clone();
        body.heartbeat_at_ms = old + if newest_field == 0 { 2000 } else { 0 };
        fs::write(&owner.path, serde_json::to_vec(&body).unwrap()).unwrap();
        set_mtime(&owner.path, old + if newest_field == 1 { 2000 } else { 0 });
        sidecar(
            &owner.path,
            old + if newest_field == 2 { 2000 } else { 0 },
            old + if newest_field == 3 { 2000 } else { 0 },
        );
        for offset in [-1, 0, 1] {
            let result =
                snapshot_for_reclaim(&owner.path, old + 2000 + WRITER_CLAIM_STALE_MS + offset);
            if offset <= 0 {
                assert!(matches!(
                    result,
                    Err(WriterClaimError::AlreadyActive { .. })
                ));
            } else {
                assert!(result.unwrap().is_some());
            }
            let normal = read_claim_snapshot(&owner.path).unwrap().unwrap();
            assert_eq!(normal.heartbeat_at_ms, body.heartbeat_at_ms);
            assert!(normal.ready_at_ms.is_none() && normal.closed_at_ms.is_none());
        }
    }
}

#[test]
fn readable_owner_is_not_reclaimed_after_refresh_replacement_or_read_failure() {
    for mode in 0..3 {
        let root = tempfile::tempdir().unwrap();
        let owner = claim_writer(root.path(), "p", "s", Role::Post, "i").unwrap();
        let mut body = owner.claim.clone();
        let old = now_epoch_ms() - WRITER_CLAIM_STALE_MS - 10_000;
        body.heartbeat_at_ms = old;
        fs::write(&owner.path, serde_json::to_vec(&body).unwrap()).unwrap();
        set_mtime(&owner.path, old);
        let mut reads = 0;
        let result = snapshot_for_reclaim_with(&owner.path, now_epoch_ms(), |path| {
            reads += 1;
            if reads == 2 {
                match mode {
                    0 => sidecar(path, now_epoch_ms(), now_epoch_ms()),
                    1 => {
                        body.owner_id = "replacement".into();
                        fs::write(path, serde_json::to_vec(&body).unwrap()).unwrap();
                        set_mtime(path, old);
                    }
                    _ => {
                        return Err(io::Error::new(
                            io::ErrorKind::PermissionDenied,
                            "injected read failure",
                        )
                        .into())
                    }
                }
            }
            read_claim_snapshot(path)
        });
        assert!(matches!(
            result,
            Err(WriterClaimError::AlreadyActive { .. })
        ));
    }
}
