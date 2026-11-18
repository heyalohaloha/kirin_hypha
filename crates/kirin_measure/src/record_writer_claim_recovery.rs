//! Recovery evidence is not ownership, execution, readiness, or closure authority.
use super::*;
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug, PartialEq, Eq)]
struct Evidence {
    body: (SystemTime, u64),
    state: Option<(SystemTime, u64)>,
    latest_ms: i64,
}

fn stamp(path: &Path) -> io::Result<(SystemTime, u64)> {
    let metadata = fs::symlink_metadata(path)?;
    if !metadata.is_file() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "claim is not a regular file",
        ));
    }
    Ok((metadata.modified()?, metadata.len()))
}

fn epoch_ms(time: SystemTime) -> io::Result<i64> {
    let ms = time
        .duration_since(UNIX_EPOCH)
        .map_err(|_| io::Error::new(io::ErrorKind::InvalidData, "claim time precedes epoch"))?
        .as_millis();
    i64::try_from(ms)
        .map_err(|_| io::Error::new(io::ErrorKind::InvalidData, "claim time overflows"))
}

fn evidence(path: &Path) -> io::Result<Evidence> {
    let body = stamp(path)?;
    let state_path = claim_state_path(path);
    let state = match stamp(&state_path) {
        Ok(value) => Some(value),
        Err(error) if error.kind() == io::ErrorKind::NotFound => None,
        Err(error) => return Err(error),
    };
    let mut latest_ms = epoch_ms(body.0)?;
    if let Some(value) = state {
        latest_ms = latest_ms.max(epoch_ms(value.0)?);
        // With a damaged body the sidecar cannot attest an owner. Its heartbeat
        // can only extend the wait, never grant readiness or shorten the deadline.
        if let Ok(bytes) = fs::read(&state_path) {
            if let Ok(value) = serde_json::from_slice::<WriterClaimStateFile>(&bytes) {
                latest_ms = latest_ms.max(value.heartbeat_at_ms);
            }
        }
    }
    Ok(Evidence {
        body,
        state,
        latest_ms,
    })
}

fn active(path: &Path, heartbeat_at_ms: Option<i64>) -> WriterClaimError {
    WriterClaimError::AlreadyActive {
        path: path.to_path_buf(),
        owner_id: None,
        heartbeat_at_ms,
    }
}

pub(super) fn snapshot_for_reclaim(
    path: &Path,
    now: i64,
) -> Result<Option<WriterClaimSnapshot>, WriterClaimError> {
    snapshot_for_reclaim_with(path, now, read_claim_snapshot)
}

fn snapshot_for_reclaim_with(
    path: &Path,
    now: i64,
    mut read: impl FnMut(&Path) -> Result<Option<WriterClaimSnapshot>, WriterClaimError>,
) -> Result<Option<WriterClaimSnapshot>, WriterClaimError> {
    match read(path) {
        Ok(Some(snapshot)) => return Ok(Some(snapshot)),
        Ok(None) if !path.try_exists()? => return Ok(None),
        _ => {}
    }
    // One diagnostic per attempted recovery, without project or user paths.
    log::warn!(
        "writer claim body unreadable or invalid; applying existing 15-minute recovery deadline"
    );
    let first = evidence(path).map_err(|_| active(path, None))?;
    if now.saturating_sub(first.latest_ms) <= WRITER_CLAIM_STALE_MS {
        return Err(active(path, Some(first.latest_ms)));
    }
    // Do not remove an owner repaired/replaced while we collected its timestamps.
    // This preserves the existing reclaim primitive; it is not a cross-process
    // compare-and-unlink transaction and does not claim to close that old race.
    if matches!(read(path), Ok(Some(_))) {
        return Err(active(path, None));
    }
    let second = evidence(path).map_err(|_| active(path, None))?;
    if first != second || now.saturating_sub(second.latest_ms) <= WRITER_CLAIM_STALE_MS {
        return Err(active(path, Some(second.latest_ms)));
    }
    Ok(None)
}

#[cfg(test)]
#[path = "record_writer_claim_recovery_tests.rs"]
mod tests;
