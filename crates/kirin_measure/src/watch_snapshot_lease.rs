//! Runtime ownership for `/tmp/kirin/*/*/{pre,post}.json` watch snapshots.
//!
//! The snapshot path is stable across DAW state restore. Deleting that shared path during an old
//! instance's teardown can therefore delete a newer instance's write. Conversely, leaving it in
//! place makes a normally removed instance look alive until the mtime timeout expires. A unique
//! per-IO-thread lease separates those two lifetimes: snapshots name their writer, and readers
//! accept a current snapshot only while that writer holds an OS file lock. The marker remains a
//! stable rendezvous path, while the kernel lock is released automatically even if the DAW
//! crashes and Rust destructors never run.

use serde::Deserialize;
use std::fs::{self, File, OpenOptions, TryLockError};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};
use uuid::Uuid;

#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;

const OWNER_DIR: &str = ".watch_owners";
const OWNER_SUFFIX: &str = ".lease";

#[derive(Debug)]
pub(crate) struct WatchSnapshotLease {
    owner_id: String,
    marker: Option<LeaseMarker>,
}

#[derive(Debug)]
struct LeaseMarker {
    path: PathBuf,
    file: File,
}

impl WatchSnapshotLease {
    pub(crate) fn new() -> Self {
        Self {
            owner_id: Uuid::new_v4().to_string(),
            marker: None,
        }
    }

    pub(crate) fn owner_id(&self) -> &str {
        &self.owner_id
    }

    /// Bind this runtime to the current restored instance directory.
    ///
    /// The new marker is created before the old marker is released. If a DAW changes the restored
    /// instance ID while the IO thread is alive, there is no interval where its next snapshot is
    /// owned by neither directory.
    pub(crate) fn bind(&mut self, instance_dir: &Path) -> io::Result<()> {
        let marker = owner_marker_path(instance_dir, &self.owner_id)
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "invalid watch owner id"))?;
        if self
            .marker
            .as_ref()
            .is_some_and(|current| current.path == marker && current.path.is_file())
        {
            return Ok(());
        }

        let owner_dir = marker
            .parent()
            .expect("watch owner marker always has parent");
        crate::atomic_file::create_private_dir_all(owner_dir)?;
        let mut options = OpenOptions::new();
        options.read(true).write(true).create_new(true);
        #[cfg(unix)]
        options.mode(0o600);
        let mut file = options.open(&marker)?;
        if let Err(error) = file.lock() {
            drop(file);
            let _ = fs::remove_file(&marker);
            return Err(error);
        }
        if let Err(error) = file.write_all(b"1").and_then(|()| file.flush()) {
            drop(file);
            let _ = fs::remove_file(&marker);
            return Err(error);
        }

        let old = self.marker.replace(LeaseMarker { path: marker, file });
        if let Some(old) = old {
            if self
                .marker
                .as_ref()
                .is_some_and(|current| current.path == old.path)
            {
                // The old locked inode was externally unlinked and the same path was recreated.
                // Dropping that old lock is correct; unlinking by path would delete the new marker.
                drop(old.file);
            } else {
                release_marker(old);
            }
        }
        Ok(())
    }
}

impl Drop for WatchSnapshotLease {
    fn drop(&mut self) {
        if let Some(marker) = self.marker.take() {
            release_marker(marker);
        }
    }
}

#[derive(Deserialize)]
struct SnapshotOwner {
    #[serde(default)]
    watch_owner_id: String,
}

fn read_snapshot_owner(snapshot_path: &Path) -> Option<(PathBuf, SnapshotOwner)> {
    let bytes = fs::read(snapshot_path).ok()?;
    let snapshot = serde_json::from_slice::<SnapshotOwner>(&bytes).ok()?;
    let instance_dir = snapshot_path.parent()?.to_path_buf();
    Some((instance_dir, snapshot))
}

/// Legacy snapshots without `watch_owner_id` remain mtime-compatible. New snapshots must prove
/// that their exact writer runtime still owns the instance directory. The marker's existence alone
/// is insufficient: a DAW crash can leave the directory entry behind, but it cannot retain the
/// kernel lock after the process closes.
pub(crate) fn snapshot_owner_is_live(instance_dir: &Path, owner_id: &str) -> bool {
    if owner_id.is_empty() {
        return true;
    }
    owner_marker_path(instance_dir, owner_id).is_some_and(|path| marker_lock_is_held(&path))
}

/// Remove valid owner markers whose producer no longer holds its kernel lock. Live marker files
/// are never removed. Returns the number of orphan marker files removed.
#[cfg(test)]
pub(crate) fn sweep_released_owner_markers(instance_dir: &Path) -> usize {
    let owner_dir = instance_dir.join(OWNER_DIR);
    let Ok(entries) = fs::read_dir(&owner_dir) else {
        return 0;
    };

    let mut removed = 0usize;
    for entry in entries.flatten() {
        let Ok(file_type) = entry.file_type() else {
            continue;
        };
        if !file_type.is_file() {
            continue;
        }
        let Some(file_name) = entry.file_name().to_str().map(str::to_owned) else {
            continue;
        };
        let Some(owner_id) = file_name.strip_suffix(OWNER_SUFFIX) else {
            continue;
        };
        if owner_marker_path(instance_dir, owner_id).as_deref() != Some(entry.path().as_path()) {
            continue;
        }
        if marker_lock_is_held(&entry.path()) {
            continue;
        }
        match fs::remove_file(entry.path()) {
            Ok(()) => removed += 1,
            Err(error) if error.kind() == io::ErrorKind::NotFound => {}
            Err(error) => log::debug!(
                "[watch lease] failed to sweep released marker {}: {}",
                entry.path().display(),
                error
            ),
        }
    }
    let _ = fs::remove_dir(&owner_dir);
    removed
}

/// Discovery paths read only a snapshot path, so they use this small owner-only parse rather than
/// duplicating PRE/POST wire schemas.
pub(crate) fn snapshot_file_has_live_owner(snapshot_path: &Path) -> bool {
    let Some((instance_dir, snapshot)) = read_snapshot_owner(snapshot_path) else {
        return false;
    };
    snapshot_owner_is_live(&instance_dir, &snapshot.watch_owner_id)
}

/// Current-schema evidence requires a non-empty lease ID. Legacy snapshots are intentionally not
/// accepted here: freshness alone cannot prove that their producer runtime still exists.
pub(crate) fn snapshot_file_has_current_live_owner(snapshot_path: &Path) -> bool {
    let Some((instance_dir, snapshot)) = read_snapshot_owner(snapshot_path) else {
        return false;
    };
    !snapshot.watch_owner_id.is_empty()
        && snapshot_owner_is_live(&instance_dir, &snapshot.watch_owner_id)
}

/// Pair-choice discovery separates runtime presence from measurement freshness.
///
/// Snapshots produced by current Hypha builds carry a unique owner lease. While that lease is
/// present, the plugin runtime still exists and must remain selectable even if a transient writer
/// failure stopped the JSON timestamp. Legacy snapshots have no lifetime proof, so they retain the
/// bounded mtime rule and cannot remain visible forever.
pub(crate) fn snapshot_file_is_live_pair_choice(
    snapshot_path: &Path,
    legacy_max_age: Duration,
) -> bool {
    let Some((instance_dir, snapshot)) = read_snapshot_owner(snapshot_path) else {
        return false;
    };
    if !snapshot.watch_owner_id.is_empty() {
        return snapshot_owner_is_live(&instance_dir, &snapshot.watch_owner_id);
    }

    let Ok(modified) = fs::metadata(snapshot_path).and_then(|meta| meta.modified()) else {
        return false;
    };
    SystemTime::now()
        .duration_since(modified)
        .map(|age| age <= legacy_max_age)
        .unwrap_or(true)
}

/// Fresh peer evidence requires both a live runtime owner (for current snapshots) and a recently
/// published file. Pair choices can outlive this window, but the blue Paired state cannot.
pub(crate) fn snapshot_file_is_fresh_runtime_evidence(
    snapshot_path: &Path,
    max_age: Duration,
) -> bool {
    if !snapshot_file_has_current_live_owner(snapshot_path) {
        return false;
    }
    let Ok(modified) = fs::metadata(snapshot_path).and_then(|meta| meta.modified()) else {
        return false;
    };
    SystemTime::now()
        .duration_since(modified)
        .map(|age| age <= max_age)
        .unwrap_or(true)
}

fn owner_marker_path(instance_dir: &Path, owner_id: &str) -> Option<PathBuf> {
    let canonical = Uuid::parse_str(owner_id).ok()?.to_string();
    if canonical != owner_id {
        return None;
    }
    Some(
        instance_dir
            .join(OWNER_DIR)
            .join(format!("{owner_id}{OWNER_SUFFIX}")),
    )
}

fn marker_lock_is_held(marker: &Path) -> bool {
    let file = match OpenOptions::new().read(true).write(true).open(marker) {
        Ok(file) => file,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return false,
        Err(error) => {
            // An inspection failure is not evidence that a producer died. Keep the pair visible
            // and let a later poll retry rather than turning a permission/filesystem issue into a
            // false disconnect.
            log::debug!(
                "[watch lease] failed to open marker {}: {}",
                marker.display(),
                error
            );
            return true;
        }
    };
    match file.try_lock() {
        Err(TryLockError::WouldBlock) => true,
        Ok(()) => {
            let _ = file.unlock();
            false
        }
        Err(TryLockError::Error(error)) => {
            // As above, only a successfully-acquired lock proves that no producer owns it.
            log::debug!(
                "[watch lease] failed to inspect marker lock {}: {}",
                marker.display(),
                error
            );
            true
        }
    }
}

fn release_marker(marker: LeaseMarker) {
    let LeaseMarker { path, file } = marker;
    drop(file);
    match fs::remove_file(&path) {
        Ok(()) => {}
        Err(error) if error.kind() == io::ErrorKind::NotFound => {}
        Err(error) => log::warn!(
            "[watch lease] failed to remove {}: {}",
            path.display(),
            error
        ),
    }
    if let Some(owner_dir) = path.parent() {
        let _ = fs::remove_dir(owner_dir);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn isolated_dir(label: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "kirin_watch_lease_{label}_{}_{}",
            std::process::id(),
            Uuid::new_v4()
        ));
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn write_owned_snapshot(instance_dir: &Path, owner_id: &str) -> PathBuf {
        let snapshot = instance_dir.join("post.json");
        fs::write(&snapshot, format!(r#"{{"watch_owner_id":"{owner_id}"}}"#)).unwrap();
        snapshot
    }

    #[test]
    fn live_runtime_is_visible_and_drop_releases_immediately() {
        let instance_dir = isolated_dir("drop");
        let mut lease = WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        let snapshot = write_owned_snapshot(&instance_dir, lease.owner_id());
        assert!(snapshot_file_has_live_owner(&snapshot));

        drop(lease);
        assert!(!snapshot_file_has_live_owner(&snapshot));
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn old_teardown_cannot_release_recreated_runtime_in_same_directory() {
        let instance_dir = isolated_dir("recreate");
        let mut old = WatchSnapshotLease::new();
        old.bind(&instance_dir).unwrap();
        let mut new = WatchSnapshotLease::new();
        new.bind(&instance_dir).unwrap();
        let snapshot = write_owned_snapshot(&instance_dir, new.owner_id());

        drop(old);
        assert!(snapshot_file_has_live_owner(&snapshot));
        drop(new);
        assert!(!snapshot_file_has_live_owner(&snapshot));
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn legacy_snapshot_without_owner_keeps_mtime_compatibility() {
        let instance_dir = isolated_dir("legacy");
        let snapshot = instance_dir.join("pre.json");
        fs::write(&snapshot, br#"{"v":2,"role":"PRE"}"#).unwrap();
        assert!(snapshot_file_has_live_owner(&snapshot));
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn invalid_owner_cannot_escape_instance_directory() {
        let instance_dir = isolated_dir("invalid");
        assert!(!snapshot_owner_is_live(&instance_dir, "../foreign"));
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn unlocked_crash_orphan_is_not_live_even_while_marker_remains() {
        let instance_dir = isolated_dir("crash_orphan");
        let owner_id = Uuid::new_v4().to_string();
        let marker = owner_marker_path(&instance_dir, &owner_id).unwrap();
        fs::create_dir_all(marker.parent().unwrap()).unwrap();
        fs::write(&marker, b"1").unwrap();

        assert!(marker.is_file(), "fixture must retain the crash residue");
        assert!(!snapshot_owner_is_live(&instance_dir, &owner_id));
        assert_eq!(sweep_released_owner_markers(&instance_dir), 1);
        assert!(!marker.exists());
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn startup_marker_sweep_preserves_locked_live_owner() {
        let instance_dir = isolated_dir("live_sweep");
        let mut lease = WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();

        assert_eq!(sweep_released_owner_markers(&instance_dir), 0);
        assert!(snapshot_owner_is_live(&instance_dir, lease.owner_id()));
        drop(lease);
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn rebinding_after_external_unlink_does_not_delete_recreated_marker() {
        let instance_dir = isolated_dir("external_unlink");
        let mut lease = WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        let marker = owner_marker_path(&instance_dir, lease.owner_id()).unwrap();
        fs::remove_file(&marker).unwrap();

        lease.bind(&instance_dir).unwrap();

        assert!(marker.is_file());
        assert!(snapshot_owner_is_live(&instance_dir, lease.owner_id()));
        drop(lease);
        assert!(!marker.exists());
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn live_owner_keeps_stale_snapshot_selectable_but_legacy_does_not() {
        let instance_dir = isolated_dir("pair_choice");
        let mut lease = WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        let owned = write_owned_snapshot(&instance_dir, lease.owner_id());
        assert!(snapshot_file_is_live_pair_choice(&owned, Duration::ZERO));

        let legacy = instance_dir.join("pre.json");
        fs::write(&legacy, br#"{"v":2,"role":"PRE"}"#).unwrap();
        std::thread::sleep(Duration::from_millis(5));
        assert!(!snapshot_file_is_live_pair_choice(&legacy, Duration::ZERO));
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn paired_evidence_requires_a_recent_write_even_with_a_live_owner() {
        let instance_dir = isolated_dir("pair_evidence");
        let mut lease = WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        let snapshot = write_owned_snapshot(&instance_dir, lease.owner_id());
        assert!(snapshot_file_is_fresh_runtime_evidence(
            &snapshot,
            Duration::from_secs(1)
        ));
        std::thread::sleep(Duration::from_millis(5));
        assert!(!snapshot_file_is_fresh_runtime_evidence(
            &snapshot,
            Duration::ZERO
        ));
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn legacy_snapshot_is_not_current_runtime_evidence() {
        let instance_dir = isolated_dir("legacy_evidence");
        let snapshot = instance_dir.join("pre.json");
        fs::write(&snapshot, br#"{"v":2,"role":"PRE"}"#).unwrap();

        assert!(snapshot_file_has_live_owner(&snapshot));
        assert!(!snapshot_file_has_current_live_owner(&snapshot));
        assert!(!snapshot_file_is_fresh_runtime_evidence(
            &snapshot,
            Duration::from_secs(1)
        ));
        let _ = fs::remove_dir_all(instance_dir);
    }
}
