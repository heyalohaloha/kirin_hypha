//! Runtime ownership for `/tmp/kirin/*/*/{pre,post}.json` watch snapshots.
//!
//! The snapshot path is stable across DAW state restore. Deleting that shared path during an old
//! instance's teardown can therefore delete a newer instance's write. Conversely, leaving it in
//! place makes a normally removed instance look alive until the mtime timeout expires. A unique
//! per-IO-thread lease separates those two lifetimes: snapshots name their writer, and readers
//! accept a current snapshot only while that writer's private marker exists.

use serde::Deserialize;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use uuid::Uuid;

const OWNER_DIR: &str = ".watch_owners";
const OWNER_SUFFIX: &str = ".lease";

#[derive(Debug)]
pub(crate) struct WatchSnapshotLease {
    owner_id: String,
    marker_path: Option<PathBuf>,
}

impl WatchSnapshotLease {
    pub(crate) fn new() -> Self {
        Self {
            owner_id: Uuid::new_v4().to_string(),
            marker_path: None,
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
        if self.marker_path.as_deref() == Some(marker.as_path()) && marker.is_file() {
            return Ok(());
        }

        let owner_dir = marker
            .parent()
            .expect("watch owner marker always has parent");
        fs::create_dir_all(owner_dir)?;
        fs::write(&marker, b"1")?;

        let old = self.marker_path.replace(marker);
        if let Some(old) = old {
            remove_marker(&old);
        }
        Ok(())
    }
}

impl Drop for WatchSnapshotLease {
    fn drop(&mut self) {
        if let Some(marker) = self.marker_path.take() {
            remove_marker(&marker);
        }
    }
}

#[derive(Deserialize)]
struct SnapshotOwner {
    #[serde(default)]
    watch_owner_id: String,
}

/// Legacy snapshots without `watch_owner_id` remain mtime-compatible. New snapshots must prove
/// that their exact writer runtime still owns the instance directory.
pub(crate) fn snapshot_owner_is_live(instance_dir: &Path, owner_id: &str) -> bool {
    if owner_id.is_empty() {
        return true;
    }
    owner_marker_path(instance_dir, owner_id).is_some_and(|path| path.is_file())
}

/// Discovery paths read only a snapshot path, so they use this small owner-only parse rather than
/// duplicating PRE/POST wire schemas.
pub(crate) fn snapshot_file_has_live_owner(snapshot_path: &Path) -> bool {
    let Ok(bytes) = fs::read(snapshot_path) else {
        return false;
    };
    let Ok(snapshot) = serde_json::from_slice::<SnapshotOwner>(&bytes) else {
        return false;
    };
    let Some(instance_dir) = snapshot_path.parent() else {
        return false;
    };
    snapshot_owner_is_live(instance_dir, &snapshot.watch_owner_id)
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

fn remove_marker(marker: &Path) {
    match fs::remove_file(marker) {
        Ok(()) => {}
        Err(error) if error.kind() == io::ErrorKind::NotFound => {}
        Err(error) => log::warn!(
            "[watch lease] failed to remove {}: {}",
            marker.display(),
            error
        ),
    }
    if let Some(owner_dir) = marker.parent() {
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
}
