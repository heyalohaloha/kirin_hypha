//! Engine-lifetime ownership for one POST's exact PRE binding.
//!
//! Watch snapshots are owned by an IO worker and may stop or restart without the plugin instance
//! disappearing. Pair ownership therefore needs a separate lease whose final owner is the POST
//! engine, not one worker generation. Claims use this lease for identity/exclusivity while Watch
//! freshness remains responsible only for measurement availability.

use std::fs::{self, File, OpenOptions, TryLockError};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use sha2::{Digest, Sha256};
use uuid::Uuid;

#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;

const OWNER_DIR: &str = ".pair_owners";
const OWNER_SUFFIX: &str = ".lease";

#[derive(Debug)]
pub struct PairOwnershipLease {
    owner_id: String,
    marker: Mutex<Option<LeaseMarker>>,
}

#[derive(Debug)]
struct LeaseMarker {
    path: PathBuf,
    file: File,
}

impl PairOwnershipLease {
    pub fn new() -> Self {
        Self {
            owner_id: Uuid::new_v4().to_string(),
            marker: Mutex::new(None),
        }
    }

    pub fn owner_id(&self) -> &str {
        &self.owner_id
    }

    /// Bind the engine-owned lease to one exact PRE selection generation.
    ///
    /// The marker is held across IO worker restarts because callers share this object outside the
    /// restart closure. The exact PRE identity and selection timestamp are part of the marker path,
    /// so a later selection cannot accidentally keep an older claim alive. Rebinding creates and
    /// locks the new marker before releasing the old one.
    pub fn bind(
        &self,
        instance_dir: &Path,
        pre_instance_id: Option<&str>,
        pair_claimed_at: f64,
    ) -> io::Result<()> {
        let desired = match pre_instance_id {
            Some(pre_instance_id) if pair_claimed_at.is_finite() && pair_claimed_at > 0.0 => Some(
                owner_marker_path(
                    instance_dir,
                    &self.owner_id,
                    pre_instance_id,
                    pair_claimed_at.to_bits(),
                )
                .ok_or_else(|| {
                    io::Error::new(io::ErrorKind::InvalidInput, "invalid pair owner identity")
                })?,
            ),
            Some(_) | None => None,
        };
        let mut current = self
            .marker
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if current.as_ref().map(|owned| &owned.path) == desired.as_ref()
            && current.as_ref().is_none_or(|owned| owned.path.is_file())
        {
            return Ok(());
        }

        let Some(marker) = desired else {
            if let Some(old) = current.take() {
                release_marker(old);
            }
            return Ok(());
        };

        let owner_dir = marker
            .parent()
            .expect("pair owner marker always has parent");
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

        let old = current.replace(LeaseMarker { path: marker, file });
        if let Some(old) = old {
            if current.as_ref().is_some_and(|owned| owned.path == old.path) {
                drop(old.file);
            } else {
                release_marker(old);
            }
        }
        Ok(())
    }
}

impl Default for PairOwnershipLease {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for PairOwnershipLease {
    fn drop(&mut self) {
        let marker = self
            .marker
            .get_mut()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .take();
        if let Some(marker) = marker {
            release_marker(marker);
        }
    }
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

fn owner_marker_path(
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

fn release_marker(marker: LeaseMarker) {
    let LeaseMarker { path, file } = marker;
    drop(file);
    match fs::remove_file(&path) {
        Ok(()) => {}
        Err(error) if error.kind() == io::ErrorKind::NotFound => {}
        Err(error) => log::warn!(
            "[pair lease] failed to remove {}: {}",
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
    use std::sync::Arc;

    fn isolated_dir(label: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "kirin_pair_lease_{label}_{}_{}",
            std::process::id(),
            Uuid::new_v4()
        ));
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn worker_generation_drop_does_not_release_engine_owned_pair() {
        let instance_dir = isolated_dir("restart");
        let engine_owner = Arc::new(PairOwnershipLease::new());
        engine_owner
            .bind(&instance_dir, Some("pre-a"), 1.0)
            .unwrap();
        let owner_id = engine_owner.owner_id().to_string();

        let worker_generation = Arc::clone(&engine_owner);
        drop(worker_generation);
        assert!(pair_owner_claim_is_live(
            &instance_dir,
            &owner_id,
            "pre-a",
            1.0f64.to_bits()
        ));

        drop(engine_owner);
        assert!(!pair_owner_claim_is_live(
            &instance_dir,
            &owner_id,
            "pre-a",
            1.0f64.to_bits()
        ));
        let _ = fs::remove_dir_all(instance_dir);
    }

    #[test]
    fn reselect_moves_one_engine_owner_without_an_old_live_claim() {
        let first = isolated_dir("first");
        let second = isolated_dir("second");
        let lease = PairOwnershipLease::new();
        let owner_id = lease.owner_id().to_string();

        lease.bind(&first, Some("pre-a"), 1.0).unwrap();
        assert!(pair_owner_claim_is_live(
            &first,
            &owner_id,
            "pre-a",
            1.0f64.to_bits()
        ));
        lease.bind(&second, Some("pre-b"), 2.0).unwrap();
        assert!(!pair_owner_claim_is_live(
            &first,
            &owner_id,
            "pre-a",
            1.0f64.to_bits()
        ));
        assert!(pair_owner_claim_is_live(
            &second,
            &owner_id,
            "pre-b",
            2.0f64.to_bits()
        ));

        drop(lease);
        assert!(!pair_owner_claim_is_live(
            &second,
            &owner_id,
            "pre-b",
            2.0f64.to_bits()
        ));
        let _ = fs::remove_dir_all(first);
        let _ = fs::remove_dir_all(second);
    }

    #[test]
    fn changing_generation_invalidates_the_old_claim_without_dropping_engine() {
        let instance_dir = isolated_dir("generation");
        let lease = PairOwnershipLease::new();
        let owner_id = lease.owner_id().to_string();

        lease.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
        lease.bind(&instance_dir, Some("pre-a"), 2.0).unwrap();
        assert!(!pair_owner_claim_is_live(
            &instance_dir,
            &owner_id,
            "pre-a",
            1.0f64.to_bits()
        ));
        assert!(pair_owner_claim_is_live(
            &instance_dir,
            &owner_id,
            "pre-a",
            2.0f64.to_bits()
        ));

        lease.bind(&instance_dir, None, 0.0).unwrap();
        assert!(!pair_owner_claim_is_live(
            &instance_dir,
            &owner_id,
            "pre-a",
            2.0f64.to_bits()
        ));
        let _ = fs::remove_dir_all(instance_dir);
    }
}
