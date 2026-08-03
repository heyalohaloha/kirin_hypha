//! Filesystem marker mechanics for the pair Control Plane.

use std::fs::{self, File, OpenOptions, TryLockError};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};
use uuid::Uuid;

#[cfg(unix)]
use std::os::unix::fs::{MetadataExt, OpenOptionsExt};

const OWNER_DIR: &str = ".pair_owners";
const OWNER_SUFFIX: &str = ".lease";
const ENGINE_BINDING_PROOF_DIR: &str = ".pair_engine_bindings";

#[derive(Debug)]
pub(super) struct LeaseMarker {
    pub(super) path: PathBuf,
    file: Option<File>,
}

#[derive(Debug)]
pub(super) enum MarkerPathOwnership {
    Owned,
    ReplacedOrMissing,
    Indeterminate(io::Error),
}

impl LeaseMarker {
    pub(super) fn path_ownership(&self) -> MarkerPathOwnership {
        marker_path_ownership(self, fs::metadata(&self.path))
    }
}

pub(super) fn desired_marker_path(
    instance_dir: Option<&Path>,
    owner_id: &str,
    pre_instance_id: Option<&str>,
    pair_claimed_at: f64,
) -> io::Result<Option<PathBuf>> {
    let Some(pre_instance_id) = pre_instance_id else {
        return Ok(None);
    };
    let instance_dir = instance_dir.ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "pair owner instance identity is unavailable",
        )
    })?;
    owner_marker_path(
        instance_dir,
        owner_id,
        pre_instance_id,
        pair_claimed_at.to_bits(),
    )
    .map(Some)
    .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "invalid pair owner identity"))
}

/// Engine-lifetime proof for one exact canonical claim.
///
/// This path deliberately lives outside the POST instance directory. Instance-shelf cleanup may
/// unlink the repairable display/worker marker, but it must not erase the independent proof used by
/// another plugin binary to distinguish a live engine from a stale claim left by a dead process.
pub(super) fn desired_engine_binding_proof_path(
    kirin_root: &Path,
    instance_dir: Option<&Path>,
    owner_id: &str,
    host_process_id: u32,
    pre_instance_id: Option<&str>,
    pair_claimed_at: f64,
) -> io::Result<Option<PathBuf>> {
    let Some(pre_instance_id) = pre_instance_id else {
        return Ok(None);
    };
    let instance_dir = instance_dir.ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "pair engine proof requires an instance identity",
        )
    })?;
    engine_binding_proof_path(
        kirin_root,
        instance_dir,
        owner_id,
        host_process_id,
        pre_instance_id,
        pair_claimed_at.to_bits(),
    )
    .map(Some)
    .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "invalid pair engine proof"))
}

pub(super) fn acquire_marker(path: &Path) -> io::Result<LeaseMarker> {
    let owner_dir = path.parent().expect("pair owner marker always has parent");
    crate::atomic_file::create_private_dir_all(owner_dir)?;
    let mut options = OpenOptions::new();
    options.read(true).write(true).create_new(true);
    #[cfg(unix)]
    options.mode(0o600);
    let file = options.open(path)?;
    let mut marker = LeaseMarker {
        path: path.to_path_buf(),
        file: Some(file),
    };
    match marker
        .file
        .as_ref()
        .expect("new marker owns file")
        .try_lock()
    {
        Ok(()) => {}
        Err(TryLockError::WouldBlock) => {
            return Err(io::Error::new(
                io::ErrorKind::WouldBlock,
                "new pair owner marker is unexpectedly locked",
            ));
        }
        Err(TryLockError::Error(error)) => return Err(error),
    }
    marker
        .file
        .as_mut()
        .expect("new marker owns file")
        .write_all(b"1")?;
    marker
        .file
        .as_mut()
        .expect("new marker owns file")
        .flush()?;
    Ok(marker)
}

pub(crate) fn pair_owner_claim_is_live(
    instance_dir: &Path,
    owner_id: &str,
    pre_instance_id: &str,
    pair_claimed_at_bits: u64,
) -> bool {
    owner_marker_path(
        instance_dir,
        owner_id,
        pre_instance_id,
        pair_claimed_at_bits,
    )
    .is_some_and(|path| marker_lock_is_held(&path))
}

pub(crate) fn engine_binding_claim_is_live(
    kirin_root: &Path,
    instance_dir: &Path,
    owner_id: &str,
    host_process_id: u32,
    pre_instance_id: &str,
    pair_claimed_at_bits: u64,
) -> bool {
    engine_binding_proof_path(
        kirin_root,
        instance_dir,
        owner_id,
        host_process_id,
        pre_instance_id,
        pair_claimed_at_bits,
    )
    .is_some_and(|path| marker_lock_is_held(&path))
}

fn engine_binding_proof_path(
    kirin_root: &Path,
    instance_dir: &Path,
    owner_id: &str,
    host_process_id: u32,
    pre_instance_id: &str,
    pair_claimed_at_bits: u64,
) -> Option<PathBuf> {
    let relative_instance = instance_dir.strip_prefix(kirin_root).ok()?;
    let mut components = relative_instance.components();
    let project = components.next()?.as_os_str().to_str()?;
    let post = components.next()?.as_os_str().to_str()?;
    if components.next().is_some()
        || host_process_id == 0
        || !crate::path_identity::is_path_safe_component(project)
        || !crate::path_identity::is_path_safe_component(post)
        || !crate::path_identity::is_path_safe_component(pre_instance_id)
        || Uuid::parse_str(owner_id).ok()?.to_string() != owner_id
        || !f64::from_bits(pair_claimed_at_bits).is_finite()
        || f64::from_bits(pair_claimed_at_bits) <= 0.0
    {
        return None;
    }
    let mut digest = Sha256::new();
    for field in [project, post, pre_instance_id] {
        digest.update(field.as_bytes());
        digest.update([0]);
    }
    digest.update(pair_claimed_at_bits.to_le_bytes());
    let digest = digest.finalize();
    let binding_id = digest
        .iter()
        .take(16)
        .map(|byte| format!("{byte:02x}"))
        .collect::<String>();
    Some(
        kirin_root
            .join(ENGINE_BINDING_PROOF_DIR)
            .join(host_process_id.to_string())
            .join(format!("{owner_id}-{binding_id}{OWNER_SUFFIX}")),
    )
}

pub(super) fn owner_marker_path(
    instance_dir: &Path,
    owner_id: &str,
    pre_instance_id: &str,
    pair_claimed_at_bits: u64,
) -> Option<PathBuf> {
    let canonical = Uuid::parse_str(owner_id).ok()?.to_string();
    if canonical != owner_id
        || !crate::path_identity::is_path_safe_component(pre_instance_id)
        || !f64::from_bits(pair_claimed_at_bits).is_finite()
        || f64::from_bits(pair_claimed_at_bits) <= 0.0
    {
        return None;
    }
    let mut digest = Sha256::new();
    digest.update(pre_instance_id.as_bytes());
    digest.update([0]);
    digest.update(pair_claimed_at_bits.to_le_bytes());
    let digest = digest.finalize();
    let mut binding_id = String::with_capacity(32);
    for byte in digest.iter().take(16) {
        binding_id.push_str(&format!("{byte:02x}"));
    }
    Some(
        instance_dir
            .join(OWNER_DIR)
            .join(format!("{owner_id}-{binding_id}{OWNER_SUFFIX}")),
    )
}

fn marker_lock_is_held(marker: &Path) -> bool {
    let file = match OpenOptions::new().read(true).write(true).open(marker) {
        Ok(file) => file,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return false,
        Err(error) => {
            log::debug!(
                "[pair lease] failed to open marker {}: {}",
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
            log::debug!(
                "[pair lease] failed to inspect marker lock {}: {}",
                marker.display(),
                error
            );
            true
        }
    }
}

#[cfg(unix)]
fn marker_path_ownership(
    marker: &LeaseMarker,
    path_metadata: io::Result<fs::Metadata>,
) -> MarkerPathOwnership {
    let Some(file) = marker.file.as_ref() else {
        return MarkerPathOwnership::ReplacedOrMissing;
    };
    let open = match file.metadata() {
        Ok(metadata) => metadata,
        Err(error) => return MarkerPathOwnership::Indeterminate(error),
    };
    let path = match path_metadata {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            return MarkerPathOwnership::ReplacedOrMissing;
        }
        Err(error) => return MarkerPathOwnership::Indeterminate(error),
    };
    if open.dev() == path.dev() && open.ino() == path.ino() {
        MarkerPathOwnership::Owned
    } else {
        MarkerPathOwnership::ReplacedOrMissing
    }
}

#[cfg(not(unix))]
fn marker_path_ownership(
    marker: &LeaseMarker,
    path_metadata: io::Result<fs::Metadata>,
) -> MarkerPathOwnership {
    if marker.file.is_none() {
        return MarkerPathOwnership::ReplacedOrMissing;
    }
    match path_metadata {
        Ok(metadata) if metadata.is_file() => MarkerPathOwnership::Owned,
        Ok(_) => MarkerPathOwnership::ReplacedOrMissing,
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            MarkerPathOwnership::ReplacedOrMissing
        }
        Err(error) => MarkerPathOwnership::Indeterminate(error),
    }
}

impl Drop for LeaseMarker {
    fn drop(&mut self) {
        // On Unix, unlink the verified pathname while this exact inode is still open and locked.
        // Closing first creates an ABA window where repair can recreate the pathname and this old
        // guard then deletes the replacement inode. Advisory-lock-aware in-tree writers cannot
        // replace the live pathname during this sequence.
        #[cfg(unix)]
        if matches!(self.path_ownership(), MarkerPathOwnership::Owned) {
            match fs::remove_file(&self.path) {
                Ok(()) => {}
                Err(error) if error.kind() == io::ErrorKind::NotFound => {}
                Err(error) => log::warn!(
                    "[pair lease] failed to remove {}: {}",
                    self.path.display(),
                    error
                ),
            }
        }
        drop(self.file.take());

        // Non-Unix path metadata cannot prove that the pathname still names this open file, and
        // some platforms reject unlink of an open file. Conservatively retain the stale marker;
        // deleting a replacement would violate live pair ownership.
        #[cfg(unix)]
        if let Some(owner_dir) = self.path.parent() {
            let _ = fs::remove_dir(owner_dir);
        }
    }
}
