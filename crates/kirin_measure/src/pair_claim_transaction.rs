//! Transactional exact-PRE claim publication under one per-PRE filesystem lock.

use std::fs::{self, File, OpenOptions, TryLockError};
use std::io;
use std::path::{Path, PathBuf};

use super::*;

/// A prepared claim retains both its lock and exact rollback image until selector/marker commit.
pub(crate) struct PreparedPairClaim {
    outcome: PublishPairClaimOutcome,
    proposed: PairClaim,
    rollback: Option<PairClaimRollback>,
    _lock: ClaimLock,
}

struct PairClaimRollback {
    path: PathBuf,
    previous_bytes: Option<Vec<u8>>,
    proposed_bytes: Vec<u8>,
}

impl PreparedPairClaim {
    pub(crate) fn outcome(&self) -> PublishPairClaimOutcome {
        self.outcome
    }

    pub(crate) fn proposed(&self) -> &PairClaim {
        &self.proposed
    }

    pub(crate) fn commit(mut self) -> PublishPairClaimOutcome {
        self.rollback = None;
        self.outcome
    }

    fn rollback(&mut self) -> io::Result<()> {
        let Some(rollback) = self.rollback.take() else {
            return Ok(());
        };
        match fs::read(&rollback.path) {
            Ok(current) if current == rollback.proposed_bytes => {}
            Ok(_) => {
                return Err(io::Error::new(
                    io::ErrorKind::WouldBlock,
                    "pair claim changed outside its PRE lock",
                ));
            }
            Err(error) if error.kind() == io::ErrorKind::NotFound => {
                return Err(io::Error::new(
                    io::ErrorKind::WouldBlock,
                    "prepared pair claim disappeared before rollback",
                ));
            }
            Err(error) => return Err(error),
        }
        if let Some(previous) = rollback.previous_bytes {
            crate::atomic_file::write_bytes_atomic(&rollback.path, &previous)
        } else {
            match fs::remove_file(&rollback.path) {
                Ok(()) => Ok(()),
                Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
                Err(error) => Err(error),
            }
        }
    }
}

impl Drop for PreparedPairClaim {
    fn drop(&mut self) {
        if let Err(error) = self.rollback() {
            log::error!("[pair claim] failed to roll back prepared claim: {error}");
        }
    }
}

pub(super) struct ClaimLock(File);

impl ClaimLock {
    pub(super) fn acquire(path: &Path) -> io::Result<Self> {
        let file = Self::open(path)?;
        file.lock()?;
        Ok(Self(file))
    }

    pub(super) fn try_acquire(path: &Path) -> io::Result<Option<Self>> {
        let file = Self::open(path)?;
        match file.try_lock() {
            Ok(()) => Ok(Some(Self(file))),
            Err(TryLockError::WouldBlock) => Ok(None),
            Err(TryLockError::Error(error)) => Err(error),
        }
    }

    fn open(path: &Path) -> io::Result<File> {
        if let Some(parent) = path.parent() {
            crate::atomic_file::create_private_dir_all(parent)?;
        }
        let mut options = OpenOptions::new();
        options.read(true).write(true).create(true).truncate(false);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        options.open(path)
    }
}

impl Drop for ClaimLock {
    fn drop(&mut self) {
        let _ = self.0.unlock();
    }
}

fn same_v2_engine_owner_for_pre(left: &PairClaim, right: &PairClaim) -> bool {
    left.schema == PAIR_CLAIM_SCHEMA
        && right.schema == PAIR_CLAIM_SCHEMA
        && left.pre_instance_id == right.pre_instance_id
        && left.pair_owner_id == right.pair_owner_id
        && left.host_process_id == right.host_process_id
}

#[allow(clippy::too_many_arguments)]
fn proposed_pair_claim(
    pre_instance_id: &str,
    project_hash: &str,
    post_instance_id: &str,
    pair_owner_id: &str,
    host_process_id: u32,
    pair_claimed_at: f64,
) -> io::Result<PairClaim> {
    let proposed = PairClaim {
        schema: PAIR_CLAIM_SCHEMA.to_string(),
        pre_instance_id: valid_component(pre_instance_id)?.to_string(),
        project_hash: valid_component(project_hash)?.to_string(),
        post_instance_id: valid_component(post_instance_id)?.to_string(),
        post_watch_owner_id: String::new(),
        pair_owner_id: valid_component(pair_owner_id)?.to_string(),
        host_process_id,
        pair_claimed_at_bits: pair_claimed_at.to_bits(),
    };
    if !claim_is_well_formed(&proposed) || host_process_id != current_host_process_id() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "pair claim is not a current, complete runtime identity",
        ));
    }
    Ok(proposed)
}

fn prepare_pair_claim_locked(
    kirin_root: &Path,
    proposed: PairClaim,
    lock: ClaimLock,
) -> io::Result<PreparedPairClaim> {
    let path = claim_path(
        kirin_root,
        proposed.host_process_id,
        &proposed.pre_instance_id,
    )?;
    if !pair_claim_is_owned(kirin_root, &proposed) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "pair claim does not match an engine-owned binding",
        ));
    }
    let previous_bytes = match fs::read(&path) {
        Ok(bytes) => Some(bytes),
        Err(error) if error.kind() == io::ErrorKind::NotFound => None,
        Err(error) => return Err(error),
    };
    let current = previous_bytes.as_ref().and_then(|bytes| {
        let claim: PairClaim = serde_json::from_slice(bytes).ok()?;
        claim_is_well_formed(&claim).then_some(claim)
    });
    if let Some(current) = current {
        if same_owner(&current, &proposed) && pair_claim_is_owned(kirin_root, &current) {
            return Ok(PreparedPairClaim {
                outcome: PublishPairClaimOutcome::AlreadyOwner,
                proposed,
                rollback: None,
                _lock: lock,
            });
        }
        // Project/POST/timestamp are mutable restore coordinates. The engine owner UUID plus host
        // process and claim pathname are the stable self-identity across a worker/identity move.
        if same_v2_engine_owner_for_pre(&current, &proposed) {
            let bytes = serde_json::to_vec(&proposed).map_err(io::Error::other)?;
            crate::atomic_file::write_bytes_atomic(&path, &bytes)?;
            return Ok(PreparedPairClaim {
                outcome: PublishPairClaimOutcome::Published,
                proposed,
                rollback: Some(PairClaimRollback {
                    path,
                    previous_bytes,
                    proposed_bytes: bytes,
                }),
                _lock: lock,
            });
        }
        if pair_claim_is_owned(kirin_root, &current) {
            return Ok(PreparedPairClaim {
                outcome: PublishPairClaimOutcome::OwnedByOther,
                proposed,
                rollback: None,
                _lock: lock,
            });
        }
    }

    let bytes = serde_json::to_vec(&proposed).map_err(io::Error::other)?;
    crate::atomic_file::write_bytes_atomic(&path, &bytes)?;
    Ok(PreparedPairClaim {
        outcome: PublishPairClaimOutcome::Published,
        proposed,
        rollback: Some(PairClaimRollback {
            path,
            previous_bytes,
            proposed_bytes: bytes,
        }),
        _lock: lock,
    })
}

#[allow(clippy::too_many_arguments)]
pub(crate) fn prepare_pair_claim(
    kirin_root: &Path,
    pre_instance_id: &str,
    project_hash: &str,
    post_instance_id: &str,
    pair_owner_id: &str,
    host_process_id: u32,
    pair_claimed_at: f64,
) -> io::Result<PreparedPairClaim> {
    let proposed = proposed_pair_claim(
        pre_instance_id,
        project_hash,
        post_instance_id,
        pair_owner_id,
        host_process_id,
        pair_claimed_at,
    )?;
    let lock = ClaimLock::acquire(&lock_path(kirin_root, host_process_id, pre_instance_id)?)?;
    prepare_pair_claim_locked(kirin_root, proposed, lock)
}

#[allow(clippy::too_many_arguments)]
pub fn publish_pair_claim(
    kirin_root: &Path,
    pre_instance_id: &str,
    project_hash: &str,
    post_instance_id: &str,
    pair_owner_id: &str,
    host_process_id: u32,
    pair_claimed_at: f64,
) -> io::Result<PublishPairClaimOutcome> {
    prepare_pair_claim(
        kirin_root,
        pre_instance_id,
        project_hash,
        post_instance_id,
        pair_owner_id,
        host_process_id,
        pair_claimed_at,
    )
    .map(PreparedPairClaim::commit)
}
