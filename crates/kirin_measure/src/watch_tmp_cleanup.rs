//! Startup cleanup for orphaned `/tmp/kirin` watch artifacts.
//!
//! PRE/POST watch files are freshness-gated by readers. IO thread teardown must
//! not delete them because DAWs and pluginval can recreate the same restored
//! instance id while the old IO thread is still exiting. This module moves that
//! hygiene to startup. Current snapshots use their OS-locked owner lease as the
//! primary lifetime contract; legacy snapshots and abandoned atomic temp files
//! use a conservative age threshold.

use chrono::{DateTime, Utc};
use std::fs;
use std::path::Path;

/// Startup sweep threshold for stale `pre.json`/`post.json` watch snapshots.
///
/// Reader freshness is currently 10s (`io_thread_post::NO_PRE_SECS`). Cleanup is
/// intentionally much more conservative because it is disk hygiene, not pairing
/// logic. A live writer updates every IO tick; a 10 minute-old snapshot is an
/// orphan for watch/pairing purposes.
pub(crate) const STALE_WATCH_SECS: i64 = 600;

const WATCH_FILES: [&str; 2] = ["pre.json", "post.json"];

pub(crate) fn sweep_stale_watch_files_at_startup() {
    let kirin_root = crate::storage::PlatformPaths::current_kirin_tmp_root();
    let n = sweep_stale_watch_files_in(&kirin_root, Utc::now(), STALE_WATCH_SECS);
    if n > 0 {
        log::info!("[watch_tmp] startup: swept {n} orphaned watch artifact(s)");
    }
}

/// Sweep stale watch snapshots below `{kirin_root}/{project_hash}/{instance_id}/`.
///
/// Current-schema snapshots are kept for as long as their exact writer holds its lease lock, even
/// if their mtime is old. A released current owner is definitive crash/teardown evidence, so its
/// snapshot and marker are removed immediately. Legacy snapshots and atomic temp siblings are
/// removed only when older than `stale_secs`. Instance/project directories are removed only when
/// empty, so a replacement writer's fresh temp file prevents parent removal.
pub(crate) fn sweep_stale_watch_files_in(
    kirin_root: &Path,
    now: DateTime<Utc>,
    stale_secs: i64,
) -> usize {
    let Ok(project_entries) = fs::read_dir(kirin_root) else {
        return 0;
    };

    let mut removed = 0usize;
    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !entry_is_dir(&project_entry) {
            continue;
        }

        let Ok(instance_entries) = fs::read_dir(&project_dir) else {
            continue;
        };
        for instance_entry in instance_entries.flatten() {
            let instance_dir = instance_entry.path();
            if !entry_is_dir(&instance_entry) {
                continue;
            }

            for file_name in WATCH_FILES {
                let path = instance_dir.join(file_name);
                let live_current_owner =
                    crate::watch_snapshot_lease::snapshot_file_has_current_live_owner(&path);
                let released_current_owner =
                    crate::watch_snapshot_lease::snapshot_file_has_released_current_owner(&path);
                let should_remove = released_current_owner
                    || (!live_current_owner && stale_by_mtime(&path, now, stale_secs));
                if should_remove {
                    match fs::remove_file(&path) {
                        Ok(()) => {
                            removed += 1;
                            log::info!("[watch_tmp] startup: swept stale {}", path.display());
                        }
                        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
                        Err(e) => {
                            log::debug!(
                                "[watch_tmp] startup: failed to sweep {}: {}",
                                path.display(),
                                e
                            );
                        }
                    }
                }
            }

            removed += sweep_stale_atomic_temps(&instance_dir, now, stale_secs);
            removed += crate::watch_snapshot_lease::sweep_released_owner_markers(&instance_dir);

            remove_dir_if_empty(&instance_dir);
        }

        remove_dir_if_empty(&project_dir);
    }

    removed
}

fn sweep_stale_atomic_temps(instance_dir: &Path, now: DateTime<Utc>, stale_secs: i64) -> usize {
    let Ok(entries) = fs::read_dir(instance_dir) else {
        return 0;
    };
    let mut removed = 0usize;
    for entry in entries.flatten() {
        let Some(name) = entry.file_name().to_str().map(str::to_owned) else {
            continue;
        };
        if !is_watch_atomic_temp(&name) || !stale_by_mtime(&entry.path(), now, stale_secs) {
            continue;
        }
        match fs::remove_file(entry.path()) {
            Ok(()) => removed += 1,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) => log::debug!(
                "[watch_tmp] startup: failed to sweep temp {}: {}",
                entry.path().display(),
                error
            ),
        }
    }
    removed
}

fn is_watch_atomic_temp(name: &str) -> bool {
    WATCH_FILES
        .iter()
        .any(|watch| name == format!("{watch}.tmp") || name.starts_with(&format!(".{watch}.tmp.")))
}

fn entry_is_dir(entry: &fs::DirEntry) -> bool {
    entry.file_type().map(|t| t.is_dir()).unwrap_or(false)
}

fn stale_by_mtime(path: &Path, now: DateTime<Utc>, stale_secs: i64) -> bool {
    let Ok(meta) = fs::metadata(path) else {
        return false;
    };
    if !meta.is_file() {
        return false;
    }
    let Ok(modified) = meta.modified() else {
        return false;
    };
    let modified: DateTime<Utc> = modified.into();
    now.signed_duration_since(modified).num_seconds() > stale_secs
}

fn remove_dir_if_empty(path: &Path) {
    match fs::remove_dir(path) {
        Ok(()) => {}
        Err(e)
            if matches!(
                e.kind(),
                std::io::ErrorKind::NotFound | std::io::ErrorKind::DirectoryNotEmpty
            ) => {}
        Err(e) => log::debug!(
            "[watch_tmp] startup: failed to remove empty dir {}: {}",
            path.display(),
            e
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::{File, FileTimes};
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::{Duration, SystemTime};

    static TEST_ID: AtomicUsize = AtomicUsize::new(0);

    fn isolated_root(label: &str) -> PathBuf {
        let n = TEST_ID.fetch_add(1, Ordering::Relaxed);
        let root = std::env::temp_dir().join(format!(
            "kirin_watch_tmp_cleanup_{label}_{}_{}",
            std::process::id(),
            n
        ));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root
    }

    fn write_watch(root: &Path, ph: &str, iid: &str, file_name: &str) -> PathBuf {
        let path = root.join(ph).join(iid).join(file_name);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(&path, br#"{"t":"fixture"}"#).unwrap();
        path
    }

    fn set_mtime(path: &Path, t: SystemTime) {
        let times = FileTimes::new().set_modified(t).set_accessed(t);
        File::options()
            .read(true)
            .open(path)
            .unwrap()
            .set_times(times)
            .unwrap();
    }

    #[test]
    fn sweep_removes_only_stale_watch_files() {
        let root = isolated_root("stale_only");
        let stale_pre = write_watch(&root, "ph", "iid-stale", "pre.json");
        let fresh_post = write_watch(&root, "ph", "iid-fresh", "post.json");
        let unrelated = write_watch(&root, "ph", "iid-stale", "note.txt");
        let now_system = SystemTime::now();
        let stale_system = now_system - Duration::from_secs(STALE_WATCH_SECS as u64 + 1);
        set_mtime(&stale_pre, stale_system);
        set_mtime(&unrelated, stale_system);

        let now: DateTime<Utc> = now_system.into();
        let removed = sweep_stale_watch_files_in(&root, now, STALE_WATCH_SECS);

        assert_eq!(removed, 1, "only the stale watch file is swept");
        assert!(!stale_pre.exists());
        assert!(fresh_post.exists(), "fresh watch files are kept");
        assert!(unrelated.exists(), "non-watch files are not touched");
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn sweep_boundary_is_strictly_greater_than_ttl() {
        let root = isolated_root("boundary");
        let pre = write_watch(&root, "ph", "iid", "pre.json");
        let now_system = SystemTime::now();
        let now: DateTime<Utc> = now_system.into();
        set_mtime(
            &pre,
            now_system - Duration::from_secs(STALE_WATCH_SECS as u64),
        );

        assert_eq!(
            sweep_stale_watch_files_in(&root, now, STALE_WATCH_SECS),
            0,
            "age == ttl is kept"
        );
        assert!(pre.exists());

        set_mtime(
            &pre,
            now_system - Duration::from_secs(STALE_WATCH_SECS as u64 + 1),
        );
        assert_eq!(
            sweep_stale_watch_files_in(&root, now, STALE_WATCH_SECS),
            1,
            "age > ttl is removed"
        );
        assert!(!pre.exists());
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn sweep_removes_parent_dirs_only_when_empty() {
        let root = isolated_root("parents");
        let stale = write_watch(&root, "ph-empty", "iid", "post.json");
        let kept_dir_file = write_watch(&root, "ph-nonempty", "iid", "pre.json");
        let live_tmp = kept_dir_file.with_file_name(".pre.json.tmp.123.1");
        fs::write(&live_tmp, b"live writer temp").unwrap();
        let now_system = SystemTime::now();
        let stale_system = now_system - Duration::from_secs(STALE_WATCH_SECS as u64 + 1);
        set_mtime(&stale, stale_system);
        set_mtime(&kept_dir_file, stale_system);

        let now: DateTime<Utc> = now_system.into();
        let removed = sweep_stale_watch_files_in(&root, now, STALE_WATCH_SECS);

        assert_eq!(removed, 2);
        assert!(!stale.exists());
        assert!(
            !root.join("ph-empty").exists(),
            "empty instance/project dirs are removed"
        );
        assert!(
            root.join("ph-nonempty").join("iid").exists(),
            "replacement writer temp keeps the parent dir"
        );
        assert!(live_tmp.exists(), "fresh writer temp is not removed");
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn live_owner_prevents_age_cleanup_of_stalled_snapshot() {
        let root = isolated_root("live_owner");
        let instance_dir = root.join("ph").join("iid");
        fs::create_dir_all(&instance_dir).unwrap();
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        let snapshot = instance_dir.join("pre.json");
        fs::write(
            &snapshot,
            format!(r#"{{"watch_owner_id":"{}"}}"#, lease.owner_id()),
        )
        .unwrap();
        let now_system = SystemTime::now();
        set_mtime(
            &snapshot,
            now_system - Duration::from_secs(STALE_WATCH_SECS as u64 + 1),
        );

        let removed =
            sweep_stale_watch_files_in(&root, DateTime::<Utc>::from(now_system), STALE_WATCH_SECS);

        assert_eq!(removed, 0);
        assert!(snapshot.exists(), "live kernel lease outranks old mtime");
        drop(lease);
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn released_current_owner_and_crash_marker_are_removed_without_waiting() {
        let root = isolated_root("released_owner");
        let instance_dir = root.join("ph").join("iid");
        let owner_id = uuid::Uuid::new_v4().to_string();
        let marker_dir = instance_dir.join(".watch_owners");
        fs::create_dir_all(&marker_dir).unwrap();
        let marker = marker_dir.join(format!("{owner_id}.lease"));
        fs::write(&marker, b"1").unwrap();
        let snapshot = instance_dir.join("post.json");
        fs::write(&snapshot, format!(r#"{{"watch_owner_id":"{owner_id}"}}"#)).unwrap();

        let removed = sweep_stale_watch_files_in(&root, Utc::now(), STALE_WATCH_SECS);

        assert_eq!(removed, 2, "one snapshot plus one released marker");
        assert!(!snapshot.exists());
        assert!(!marker.exists());
        assert!(!root.join("ph").exists(), "empty parents are reclaimed");
    }

    #[test]
    fn stale_atomic_temps_are_removed_but_fresh_writer_temp_is_kept() {
        let root = isolated_root("atomic_temps");
        let instance_dir = root.join("ph").join("iid");
        fs::create_dir_all(&instance_dir).unwrap();
        let stale_unique = instance_dir.join(".pre.json.tmp.123.1");
        let stale_legacy = instance_dir.join("post.json.tmp");
        let fresh = instance_dir.join(".post.json.tmp.456.2");
        let unrelated = instance_dir.join(".note.json.tmp.123.1");
        for path in [&stale_unique, &stale_legacy, &fresh, &unrelated] {
            fs::write(path, b"tmp").unwrap();
        }
        let now_system = SystemTime::now();
        let stale_system = now_system - Duration::from_secs(STALE_WATCH_SECS as u64 + 1);
        set_mtime(&stale_unique, stale_system);
        set_mtime(&stale_legacy, stale_system);
        set_mtime(&unrelated, stale_system);

        let removed =
            sweep_stale_watch_files_in(&root, DateTime::<Utc>::from(now_system), STALE_WATCH_SECS);

        assert_eq!(removed, 2);
        assert!(!stale_unique.exists());
        assert!(!stale_legacy.exists());
        assert!(fresh.exists());
        assert!(unrelated.exists());
        let _ = fs::remove_dir_all(root);
    }
}
