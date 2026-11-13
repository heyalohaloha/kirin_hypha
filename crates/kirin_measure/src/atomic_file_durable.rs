//! Durable Record saves: a take's bytes reach the disk before the rename publishes them.
//!
//! Only the IO thread's Record and plugin_data saves and the non-RT immutable facts (Keep archives,
//! Drop acceptances, Local Blind transfers) use this. The Audio Thread never does, and the
//! frequent watch-state heartbeat keeps the plain atomic write. A failed sync never stops a
//! save or a measurement: the write and rename still happen, and the first failure is logged.
//!
//! - macOS: `F_BARRIERFSYNC` orders the file's bytes before its rename, and the rename before any
//!   later write, without flushing the whole drive cache under a running DAW. A filesystem
//!   without barriers falls back to `sync_all` (`F_FULLFSYNC`).
//! - Other Unix: `sync_data` on the file, then `sync_all` on its directory after the rename.
//! - Windows: `sync_data` (FlushFileBuffers) on the file only. A directory cannot be flushed
//!   there, and NTFS journals the rename itself.

use std::fs::{self, File};
use std::io::{self, Write};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

static FILE_SYNC_WARNED: AtomicBool = AtomicBool::new(false);
#[cfg(unix)]
static DIRECTORY_SYNC_WARNED: AtomicBool = AtomicBool::new(false);
/// Directories whose own entry this process already synced into their parent, by path, with the
/// identity (device, inode) the path had then. A directory deleted and made again, or a symlink
/// pointed elsewhere, has another identity and is synced again.
#[cfg(unix)]
static DURABLE_DIRECTORIES: std::sync::Mutex<
    std::collections::BTreeMap<std::path::PathBuf, (u64, u64)>,
> = std::sync::Mutex::new(std::collections::BTreeMap::new());

/// `fs::write`, with the bytes synced before the file is closed.
pub(crate) fn write(path: &Path, bytes: &[u8]) -> io::Result<()> {
    let mut file = File::create(path)?;
    file.write_all(bytes)?;
    sync_file(&file, path);
    Ok(())
}

/// Rename a file whose bytes this process already synced, then sync the new directory entry.
pub(crate) fn rename(from: &Path, to: &Path) -> io::Result<()> {
    fs::rename(from, to)?;
    sync_parent_directory(to);
    Ok(())
}

/// Rename a file an earlier run left behind: its bytes may still be only in the cache.
pub(crate) fn sync_and_rename(from: &Path, to: &Path) -> io::Result<()> {
    match open_for_sync(from) {
        Ok(file) => sync_file(&file, from),
        Err(error) => warn_once(&FILE_SYNC_WARNED, "file", from, &error),
    }
    rename(from, to)
}

/// `fs::create_dir_all`, then the directory chain is made durable (see `create_dir_all_with`).
pub(crate) fn create_dir_all(path: &Path) -> io::Result<()> {
    create_dir_all_with(path, |dir| fs::create_dir_all(dir))
}

/// The private-mode creation used for coordination directories, made durable the same way.
pub(crate) fn create_private_dir_all(path: &Path) -> io::Result<()> {
    create_dir_all_with(path, super::create_private_dir_all)
}

/// Creates `path` with `create`, then on Unix syncs directories of its chain into their parents,
/// outermost first: every directory this call created, and every other one up to the nearest that
/// this process already synced with the same identity. Whoever created an ancestor (another writer
/// may have made it without a sync), a crash then cannot lose it under synced files. Best effort:
/// the first failed sync ends the walk and is logged.
pub(crate) fn create_dir_all_with(
    path: &Path,
    create: impl FnOnce(&Path) -> io::Result<()>,
) -> io::Result<()> {
    #[cfg(unix)]
    let created = chain(path)
        .take_while(|dir| fs::symlink_metadata(dir).is_err())
        .count();
    create(path)?;
    #[cfg(unix)]
    make_chain_durable(path, created);
    Ok(())
}

/// The directories of `path`, deepest first, without the root and an empty relative prefix.
#[cfg(unix)]
fn chain(path: &Path) -> impl Iterator<Item = &Path> {
    path.ancestors()
        .take_while(|dir| !dir.as_os_str().is_empty() && dir.parent().is_some())
}

/// `created`: how many of the deepest directories of the chain this call created.
#[cfg(unix)]
fn make_chain_durable(path: &Path, created: usize) {
    use std::os::unix::fs::MetadataExt;
    let durable = || {
        DURABLE_DIRECTORIES
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    };
    let mut pending = Vec::new();
    for (depth, directory) in chain(path).enumerate() {
        let identity = fs::metadata(directory).ok().map(|m| (m.dev(), m.ino()));
        if depth >= created && identity.is_some() && durable().get(directory) == identity.as_ref() {
            break;
        }
        pending.push((directory, identity));
    }
    for (directory, identity) in pending.into_iter().rev() {
        if !sync_parent_directory(directory) {
            return;
        }
        if let Some(identity) = identity {
            durable().insert(directory.to_path_buf(), identity);
        }
    }
}

/// Best effort: a failure is logged, never returned.
pub(crate) fn sync_file(file: &File, path: &Path) {
    if let Err(error) = test_hook::observe("file", path).and_then(|()| sync_bytes(file)) {
        warn_once(&FILE_SYNC_WARNED, "file", path, &error);
    }
}

/// Best effort, after a rename: makes the new name itself survive a crash (Unix only).
/// False when the sync failed (logged); true when it succeeded or the platform has none.
pub(crate) fn sync_parent_directory(path: &Path) -> bool {
    #[cfg(unix)]
    {
        let parent = match path.parent() {
            Some(parent) if !parent.as_os_str().is_empty() => parent,
            _ => Path::new("."),
        };
        let synced = test_hook::observe("directory", parent)
            .and_then(|()| File::open(parent))
            .and_then(|directory| sync_directory(&directory));
        if let Err(error) = synced {
            warn_once(&DIRECTORY_SYNC_WARNED, "directory", parent, &error);
            return false;
        }
    }
    #[cfg(not(unix))]
    let _ = path;
    true
}

fn warn_once(warned: &AtomicBool, kind: &str, path: &Path, error: &io::Error) {
    if warned.swap(true, Ordering::Relaxed) {
        log::debug!(
            "[durable] {kind} sync failed, saving continues: {} ({error})",
            path.display()
        );
    } else {
        log::warn!(
            "[durable] {kind} sync failed, saving continues: {} ({error})",
            path.display()
        );
    }
}

#[cfg(unix)]
fn open_for_sync(path: &Path) -> io::Result<File> {
    File::open(path)
}

#[cfg(not(unix))]
fn open_for_sync(path: &Path) -> io::Result<File> {
    // FlushFileBuffers needs write access; this neither truncates nor changes the bytes.
    fs::OpenOptions::new().write(true).open(path)
}

#[cfg(target_vendor = "apple")]
fn sync_bytes(file: &File) -> io::Result<()> {
    barrier(file)
}

#[cfg(not(target_vendor = "apple"))]
fn sync_bytes(file: &File) -> io::Result<()> {
    file.sync_data()
}

#[cfg(target_vendor = "apple")]
fn sync_directory(directory: &File) -> io::Result<()> {
    barrier(directory)
}

#[cfg(all(unix, not(target_vendor = "apple")))]
fn sync_directory(directory: &File) -> io::Result<()> {
    directory.sync_all()
}

#[cfg(target_vendor = "apple")]
fn barrier(file: &File) -> io::Result<()> {
    use std::os::fd::AsRawFd;
    loop {
        // SAFETY: F_BARRIERFSYNC takes no argument; `file` owns the descriptor for this call.
        if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_BARRIERFSYNC) } != -1 {
            return Ok(());
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return file.sync_all().map_err(|_| error);
        }
    }
}

#[cfg(not(test))]
mod test_hook {
    #[inline]
    pub(super) fn observe(_kind: &'static str, _path: &std::path::Path) -> std::io::Result<()> {
        Ok(())
    }
}

/// Records each sync on the calling thread, and can make every sync on it fail.
#[cfg(test)]
pub(crate) mod test_hook {
    use std::cell::{Cell, RefCell};
    use std::path::{Path, PathBuf};

    thread_local! {
        static FAIL: Cell<bool> = const { Cell::new(false) };
        static SEEN: RefCell<Vec<(&'static str, PathBuf)>> = const { RefCell::new(Vec::new()) };
    }

    pub(super) fn observe(kind: &'static str, path: &Path) -> std::io::Result<()> {
        SEEN.with_borrow_mut(|seen| seen.push((kind, path.to_path_buf())));
        if FAIL.get() {
            return Err(std::io::Error::other("injected sync failure"));
        }
        Ok(())
    }

    /// Clears the record, sets failure injection, and returns the syncs `run` performed.
    pub(crate) fn syncs_during(fail: bool, run: impl FnOnce()) -> Vec<(&'static str, PathBuf)> {
        SEEN.with_borrow_mut(Vec::clear);
        FAIL.set(fail);
        run();
        FAIL.set(false);
        SEEN.take()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn directory_syncs(parent: &Path) -> Vec<(&'static str, std::path::PathBuf)> {
        if cfg!(unix) {
            vec![("directory", parent.to_path_buf())]
        } else {
            Vec::new()
        }
    }

    #[test]
    fn durable_write_and_rename_sync_the_file_then_its_directory() {
        let root = tempfile::tempdir().unwrap();
        let tmp = root.path().join("take.json.tmp");
        let target = root.path().join("take.json");
        let syncs = test_hook::syncs_during(false, || {
            write(&tmp, b"complete").unwrap();
            rename(&tmp, &target).unwrap();
        });
        let mut expected = vec![("file", tmp.clone())];
        expected.extend(directory_syncs(root.path()));
        assert_eq!(syncs, expected);
        assert_eq!(fs::read(&target).unwrap(), b"complete");
        assert!(!tmp.exists());
    }

    #[test]
    fn failed_sync_never_stops_the_save() {
        let root = tempfile::tempdir().unwrap();
        let left_over = root.path().join("pending.json.tmp");
        fs::write(&left_over, b"earlier run").unwrap();
        let target = root.path().join("pending.json");
        let fresh = root.path().join("fresh.json.tmp");
        let syncs = test_hook::syncs_during(true, || {
            sync_and_rename(&left_over, &target).unwrap();
            write(&fresh, b"fresh").unwrap();
        });
        assert_eq!(syncs.first(), Some(&("file", left_over.clone())));
        assert_eq!(fs::read(&target).unwrap(), b"earlier run");
        assert_eq!(fs::read(&fresh).unwrap(), b"fresh");
    }

    #[test]
    fn an_existing_ancestor_made_by_another_writer_is_synced_once_with_the_new_chain() {
        let root = tempfile::tempdir().unwrap();
        let project = root.path().join("project");
        // Another writer made this directory without a sync.
        fs::create_dir_all(&project).unwrap();
        let session = project.join("session").join("pre");
        let syncs = test_hook::syncs_during(false, || create_dir_all(&session).unwrap());
        let parents: Vec<_> = syncs.iter().map(|(_, path)| path.clone()).collect();
        if cfg!(unix) {
            assert!(syncs.iter().all(|(kind, _)| *kind == "directory"));
            let wanted = [root.path(), &project, &project.join("session")].map(Path::to_path_buf);
            assert!(parents.ends_with(&wanted), "{parents:?}");
        } else {
            assert!(syncs.is_empty());
        }
        let again = test_hook::syncs_during(false, || create_dir_all(&session).unwrap());
        assert!(
            again.is_empty(),
            "a chain already made durable needs no sync"
        );
    }

    #[test]
    fn a_failed_directory_sync_still_creates_and_is_retried_later() {
        let root = tempfile::tempdir().unwrap();
        let deepest = root.path().join("record_sessions/session");
        let failed = test_hook::syncs_during(true, || create_dir_all(&deepest).unwrap());
        assert!(deepest.is_dir());
        assert!(
            failed.len() <= 1,
            "the first failure ends the walk: {failed:?}"
        );
        let retried = test_hook::syncs_during(false, || create_private_dir_all(&deepest).unwrap());
        if cfg!(unix) {
            let tail = [
                root.path().to_path_buf(),
                root.path().join("record_sessions"),
            ];
            let parents: Vec<_> = retried.iter().map(|(_, path)| path.clone()).collect();
            assert!(parents.ends_with(&tail), "{parents:?}");
        }
    }

    #[cfg(unix)]
    #[test]
    fn a_directory_made_again_at_a_synced_path_is_synced_again() {
        use std::os::unix::fs::MetadataExt;
        let root = tempfile::tempdir().unwrap();
        let project = root.path().join("project");
        let session = project.join("session");
        create_dir_all(&session).unwrap();
        // Another writer replaced it without a sync. The old one stays, so its inode is not reused.
        fs::rename(&session, root.path().join("old")).unwrap();
        fs::create_dir(&session).unwrap();
        let replaced = test_hook::syncs_during(false, || create_dir_all(&session).unwrap());
        assert_eq!(replaced, directory_syncs(&project));
        assert!(test_hook::syncs_during(false, || create_dir_all(&session).unwrap()).is_empty());
        // This call makes it again on a filesystem that reuses the inode: still synced.
        fs::remove_dir(&session).unwrap();
        let remade = test_hook::syncs_during(false, || {
            create_dir_all_with(&session, |dir| {
                fs::create_dir_all(dir)?;
                let reused = fs::metadata(dir)?;
                let mut known = DURABLE_DIRECTORIES.lock().unwrap();
                known.insert(dir.to_path_buf(), (reused.dev(), reused.ino()));
                Ok(())
            })
            .unwrap()
        });
        assert_eq!(remade, directory_syncs(&project));
    }

    #[cfg(unix)]
    #[test]
    fn a_symlink_pointed_at_another_directory_is_synced_again() {
        let root = tempfile::tempdir().unwrap();
        let (first, second) = (root.path().join("first"), root.path().join("second"));
        fs::create_dir_all(first.join("project")).unwrap();
        // Another writer made the new target's project without a sync.
        fs::create_dir_all(second.join("project")).unwrap();
        let link = root.path().join("records");
        std::os::unix::fs::symlink(&first, &link).unwrap();
        let project = link.join("project");
        create_dir_all(&project).unwrap();
        assert!(test_hook::syncs_during(false, || create_dir_all(&project).unwrap()).is_empty());
        fs::remove_file(&link).unwrap();
        std::os::unix::fs::symlink(&second, &link).unwrap();
        let syncs = test_hook::syncs_during(false, || create_dir_all(&project).unwrap());
        let parents = [root.path().to_path_buf(), link];
        assert_eq!(syncs, parents.map(|parent| ("directory", parent)));
    }

    #[test]
    fn the_frequent_watch_state_write_stays_unsynced() {
        let root = tempfile::tempdir().unwrap();
        let syncs = test_hook::syncs_during(false, || {
            super::super::write_bytes_atomic(&root.path().join("pre.json"), b"{}").unwrap();
        });
        assert!(syncs.is_empty());
        let take = root.path().join("take.json");
        let syncs = test_hook::syncs_during(false, || {
            super::super::write_bytes_atomic_durable(&take, b"{}").unwrap();
        });
        assert_eq!(syncs[0].0, "file");
        assert_eq!(syncs[1..], directory_syncs(root.path())[..]);
    }

    #[test]
    fn a_failed_rename_is_still_reported() {
        let root = tempfile::tempdir().unwrap();
        let missing = root.path().join("missing.tmp");
        assert!(rename(&missing, &root.path().join("x.json")).is_err());
        assert!(sync_and_rename(&missing, &root.path().join("y.json")).is_err());
    }

    #[test]
    fn real_syncs_succeed_on_the_local_filesystem() {
        let root = tempfile::tempdir().unwrap();
        let file = File::create(root.path().join("a")).unwrap();
        sync_bytes(&file).unwrap();
        #[cfg(unix)]
        sync_directory(&File::open(root.path()).unwrap()).unwrap();
    }
}
