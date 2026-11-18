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
