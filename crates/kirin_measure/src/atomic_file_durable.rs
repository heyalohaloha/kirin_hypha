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

#[path = "atomic_directory_mutation.rs"]
pub(crate) mod entry;

static FILE_SYNC_WARNED: AtomicBool = AtomicBool::new(false);
#[cfg(unix)]
static DIRECTORY_SYNC_WARNED: AtomicBool = AtomicBool::new(false);
#[cfg(unix)]
#[path = "atomic_directory_cache_policy.rs"]
mod directory_cache_policy;

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
/// outermost first: every directory this call created, every one this process has not synced with
/// its present identity, and every one moved into such a new directory. Whoever created or replaced
/// an ancestor (another writer may have done it without a sync), a crash then cannot lose it under
/// synced files. Best effort: the first failed sync ends the walk and is logged.
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

/// `created`: how many of the deepest directories of the chain this call created. Every level is
/// checked, outermost first (a stat each): a directory this call created, one never synced or
/// synced with another identity (made again, a symlink pointed elsewhere), and one whose parent is
/// such a new directory (moved into it) has its entry synced into its parent. A directory is
/// remembered only after its sync succeeded; the first failure forgets it and ends the walk, so the
/// next call tries again. Only cache-eligible unchanged chains may skip syncing.
/// Unknown/non-APFS Unix filesystems cannot use dev/inode alone as a generation witness.
#[cfg(unix)]
fn make_chain_durable(path: &Path, created: usize) {
    use std::os::unix::fs::MetadataExt;
    let durable = || {
        DURABLE_DIRECTORIES
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    };
    let directories: Vec<_> = chain(path).collect();
    let mut parent_new = false;
    for (depth, directory) in directories.into_iter().enumerate().rev() {
        let identity = fs::metadata(directory).ok().map(|m| (m.dev(), m.ino()));
        let cacheable = directory_cache_policy::permits_cache(directory);
        if !cacheable {
            durable().remove(directory);
        }
        let new = !cacheable
            || depth < created
            || identity.is_none()
            || durable().get(directory) != identity.as_ref();
        if new || parent_new {
            if !sync_parent_directory(directory) {
                durable().remove(directory);
                return;
            }
            if cacheable {
                if let Some(identity) = identity {
                    durable().insert(directory.to_path_buf(), identity);
                }
            }
        }
        parent_new = new;
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

#[cfg(any(target_vendor = "apple", test))]
#[path = "atomic_file_sync_fallback.rs"]
mod sync_fallback;

#[cfg(target_vendor = "apple")]
fn barrier(file: &File) -> io::Result<()> {
    use std::os::fd::AsRawFd;
    static FALLBACK_WARNED: AtomicBool = AtomicBool::new(false);
    sync_fallback::run(
        || {
            // SAFETY: F_BARRIERFSYNC takes no argument; file owns this descriptor.
            if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_BARRIERFSYNC) } != -1 {
                Ok(())
            } else {
                Err(io::Error::last_os_error())
            }
        },
        || file.sync_all(),
        |primary, fallback| {
            if FALLBACK_WARNED.swap(true, Ordering::Relaxed) {
                log::debug!(
                    "[durable] F_BARRIERFSYNC failed: {primary}; sync_all fallback failed: {fallback}"
                );
            } else {
                log::warn!(
                    "[durable] F_BARRIERFSYNC failed: {primary}; sync_all fallback failed: {fallback}"
                );
            }
        },
    )
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
#[path = "atomic_file_durable_existing_tests.rs"]
mod tests;

#[cfg(all(test, unix))]
#[path = "atomic_directory_cache_policy_tests.rs"]
mod directory_policy_tests;
