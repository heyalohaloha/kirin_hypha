//! Exact POST ownership index for one PRE runtime.
//!
//! A POST publishes one fixed claim after its own `post.json` contains the exact PRE binding.
//! PRE pair-status and POST conflict checks read only that claim and the one POST snapshot it
//! names. Directory enumeration is deliberately absent from this module.

use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use crate::post_candidates::{current_host_process_id, PostTmpJson};
use crate::pre_discovery::DISCOVERY_STALE_SECS;

pub const PAIR_CLAIM_SCHEMA: &str = "pair_claim.v1";
const PAIR_CLAIM_SUBDIR: &str = "pair_target";

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct PairClaim {
    pub schema: String,
    pub pre_instance_id: String,
    pub project_hash: String,
    pub post_instance_id: String,
    pub post_watch_owner_id: String,
    pub host_process_id: u32,
    pub pair_claimed_at_bits: u64,
}

impl PairClaim {
    pub fn pair_claimed_at(&self) -> f64 {
        f64::from_bits(self.pair_claimed_at_bits)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PublishPairClaimOutcome {
    Published,
    AlreadyOwner,
    OwnedByOther,
}

struct ClaimLock(File);

impl ClaimLock {
    fn acquire(path: &Path) -> io::Result<Self> {
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
        let file = options.open(path)?;
        file.lock()?;
        Ok(Self(file))
    }
}

impl Drop for ClaimLock {
    fn drop(&mut self) {
        let _ = self.0.unlock();
    }
}

fn valid_component(value: &str) -> io::Result<&str> {
    if crate::path_identity::is_path_safe_component(value) {
        Ok(value)
    } else {
        Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "pair claim contains an unsafe identity",
        ))
    }
}

fn host_dir(kirin_root: &Path, host_process_id: u32) -> PathBuf {
    kirin_root
        .join(PAIR_CLAIM_SUBDIR)
        .join(host_process_id.to_string())
}

fn claim_path(
    kirin_root: &Path,
    host_process_id: u32,
    pre_instance_id: &str,
) -> io::Result<PathBuf> {
    Ok(host_dir(kirin_root, host_process_id)
        .join(format!("{}.json", valid_component(pre_instance_id)?)))
}

fn lock_path(
    kirin_root: &Path,
    host_process_id: u32,
    pre_instance_id: &str,
) -> io::Result<PathBuf> {
    Ok(host_dir(kirin_root, host_process_id)
        .join(format!(".{}.lock", valid_component(pre_instance_id)?)))
}

fn read_claim_path(path: &Path) -> Option<PairClaim> {
    let bytes = fs::read(path).ok()?;
    let claim: PairClaim = serde_json::from_slice(&bytes).ok()?;
    claim_is_well_formed(&claim).then_some(claim)
}

fn claim_is_well_formed(claim: &PairClaim) -> bool {
    claim.schema == PAIR_CLAIM_SCHEMA
        && claim.host_process_id != 0
        && valid_component(&claim.pre_instance_id).is_ok()
        && valid_component(&claim.project_hash).is_ok()
        && valid_component(&claim.post_instance_id).is_ok()
        && valid_component(&claim.post_watch_owner_id).is_ok()
        && claim.pair_claimed_at().is_finite()
        && claim.pair_claimed_at() > 0.0
}

pub fn read_pair_claim(
    kirin_root: &Path,
    host_process_id: u32,
    pre_instance_id: &str,
) -> Option<PairClaim> {
    let path = claim_path(kirin_root, host_process_id, pre_instance_id).ok()?;
    let claim = read_claim_path(&path)?;
    (claim.host_process_id == host_process_id && claim.pre_instance_id == pre_instance_id)
        .then_some(claim)
}

fn post_path_for_claim(kirin_root: &Path, claim: &PairClaim) -> Option<PathBuf> {
    claim_is_well_formed(claim).then(|| {
        kirin_root
            .join(&claim.project_hash)
            .join(&claim.post_instance_id)
            .join("post.json")
    })
}

pub fn pair_claim_is_live(kirin_root: &Path, claim: &PairClaim) -> bool {
    if claim.host_process_id != current_host_process_id() {
        return false;
    }
    let Some(post_path) = post_path_for_claim(kirin_root, claim) else {
        return false;
    };
    let Ok(bytes) = fs::read(&post_path) else {
        return false;
    };
    let Ok(post) = serde_json::from_slice::<PostTmpJson>(&bytes) else {
        return false;
    };
    post.v == 2
        && post.role == "POST"
        && post.instance_id == claim.post_instance_id
        && post.watch_owner_id == claim.post_watch_owner_id
        && post.host_process_id == claim.host_process_id
        && post.paired_pre_instance_id == claim.pre_instance_id
        && post.pair_claimed_at.to_bits() == claim.pair_claimed_at_bits
        && crate::watch_snapshot_lease::snapshot_owner_is_live(
            post_path
                .parent()
                .expect("post.json has an instance directory"),
            &post.watch_owner_id,
        )
        && fs::metadata(&post_path)
            .and_then(|metadata| metadata.modified())
            .ok()
            .is_some_and(|modified| {
                SystemTime::now()
                    .duration_since(modified)
                    .map(|age| age <= Duration::from_secs(DISCOVERY_STALE_SECS))
                    .unwrap_or(true)
            })
}

fn same_owner(left: &PairClaim, right: &PairClaim) -> bool {
    left.pre_instance_id == right.pre_instance_id
        && left.project_hash == right.project_hash
        && left.post_instance_id == right.post_instance_id
        && left.post_watch_owner_id == right.post_watch_owner_id
        && left.host_process_id == right.host_process_id
        && left.pair_claimed_at_bits == right.pair_claimed_at_bits
}

fn outranks(left: &PairClaim, right: &PairClaim) -> bool {
    left.pair_claimed_at() > right.pair_claimed_at()
        || (left.pair_claimed_at_bits == right.pair_claimed_at_bits
            && left.post_instance_id < right.post_instance_id)
}

#[allow(clippy::too_many_arguments)]
pub fn publish_pair_claim(
    kirin_root: &Path,
    pre_instance_id: &str,
    project_hash: &str,
    post_instance_id: &str,
    post_watch_owner_id: &str,
    host_process_id: u32,
    pair_claimed_at: f64,
) -> io::Result<PublishPairClaimOutcome> {
    let proposed = PairClaim {
        schema: PAIR_CLAIM_SCHEMA.to_string(),
        pre_instance_id: valid_component(pre_instance_id)?.to_string(),
        project_hash: valid_component(project_hash)?.to_string(),
        post_instance_id: valid_component(post_instance_id)?.to_string(),
        post_watch_owner_id: valid_component(post_watch_owner_id)?.to_string(),
        host_process_id,
        pair_claimed_at_bits: pair_claimed_at.to_bits(),
    };
    if !claim_is_well_formed(&proposed) || host_process_id != current_host_process_id() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "pair claim is not a current, complete runtime identity",
        ));
    }

    let path = claim_path(kirin_root, host_process_id, pre_instance_id)?;
    let _lock = ClaimLock::acquire(&lock_path(kirin_root, host_process_id, pre_instance_id)?)?;
    if !pair_claim_is_live(kirin_root, &proposed) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "pair claim does not match one fresh owned POST snapshot",
        ));
    }
    if let Some(current) = read_claim_path(&path) {
        if same_owner(&current, &proposed) && pair_claim_is_live(kirin_root, &current) {
            return Ok(PublishPairClaimOutcome::AlreadyOwner);
        }
        if pair_claim_is_live(kirin_root, &current) && !outranks(&proposed, &current) {
            return Ok(PublishPairClaimOutcome::OwnedByOther);
        }
    }

    let bytes = serde_json::to_vec(&proposed).map_err(io::Error::other)?;
    crate::atomic_file::write_bytes_atomic(&path, &bytes)?;
    Ok(PublishPairClaimOutcome::Published)
}

pub fn release_pair_claim(kirin_root: &Path, owned: &PairClaim) -> io::Result<bool> {
    if !claim_is_well_formed(owned) {
        return Ok(false);
    }
    let path = claim_path(kirin_root, owned.host_process_id, &owned.pre_instance_id)?;
    let _lock = ClaimLock::acquire(&lock_path(
        kirin_root,
        owned.host_process_id,
        &owned.pre_instance_id,
    )?)?;
    if read_claim_path(&path).is_some_and(|current| same_owner(&current, owned)) {
        match fs::remove_file(path) {
            Ok(()) => return Ok(true),
            Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(false),
            Err(error) => return Err(error),
        }
    }
    Ok(false)
}

pub fn live_claim_owned_by_other(
    kirin_root: &Path,
    pre_instance_id: &str,
    project_hash: &str,
    post_instance_id: &str,
    pair_claimed_at: f64,
) -> bool {
    let host_process_id = current_host_process_id();
    let Some(claim) = read_pair_claim(kirin_root, host_process_id, pre_instance_id) else {
        return false;
    };
    if !pair_claim_is_live(kirin_root, &claim) {
        return false;
    }
    claim.project_hash != project_hash
        || claim.post_instance_id != post_instance_id
        || claim.pair_claimed_at_bits != pair_claimed_at.to_bits()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_root() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let root = std::env::temp_dir().join(format!(
            "kirin_pair_claim_index_{}_{}",
            std::process::id(),
            n
        ));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root
    }

    fn write_post(
        root: &Path,
        project_hash: &str,
        post_instance_id: &str,
        pre_instance_id: &str,
        pair_claimed_at: f64,
        lease: &mut crate::watch_snapshot_lease::WatchSnapshotLease,
    ) {
        let instance_dir = root.join(project_hash).join(post_instance_id);
        lease.bind(&instance_dir).unwrap();
        fs::write(
            instance_dir.join("post.json"),
            serde_json::json!({
                "v": 2,
                "role": "POST",
                "instance_id": post_instance_id,
                "watch_owner_id": lease.owner_id(),
                "host_process_id": current_host_process_id(),
                "signal_state": "inactive",
                "t": chrono::Utc::now().to_rfc3339(),
                "paired_pre_instance_id": pre_instance_id,
                "pair_claimed_at": pair_claimed_at,
            })
            .to_string(),
        )
        .unwrap();
    }

    fn publish(
        root: &Path,
        project_hash: &str,
        post_instance_id: &str,
        pre_instance_id: &str,
        pair_claimed_at: f64,
        lease: &crate::watch_snapshot_lease::WatchSnapshotLease,
    ) -> PublishPairClaimOutcome {
        publish_pair_claim(
            root,
            pre_instance_id,
            project_hash,
            post_instance_id,
            lease.owner_id(),
            current_host_process_id(),
            pair_claimed_at,
        )
        .unwrap()
    }

    #[test]
    fn exact_claim_is_live_only_while_its_post_owner_holds_the_lease() {
        let root = isolated_root();
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        write_post(&root, "project-a", "post-a", "pre-a", 1.0, &mut lease);
        assert_eq!(
            publish(&root, "project-a", "post-a", "pre-a", 1.0, &lease),
            PublishPairClaimOutcome::Published
        );
        let claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
        assert!(pair_claim_is_live(&root, &claim));
        drop(lease);
        assert!(!pair_claim_is_live(&root, &claim));
    }

    #[test]
    fn newer_owner_replaces_live_claim_and_old_release_cannot_delete_it() {
        let root = isolated_root();
        let mut old_lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        let mut new_lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        write_post(&root, "project-a", "post-old", "pre-a", 1.0, &mut old_lease);
        assert_eq!(
            publish(&root, "project-a", "post-old", "pre-a", 1.0, &old_lease),
            PublishPairClaimOutcome::Published
        );
        let old_claim = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();

        write_post(&root, "project-a", "post-new", "pre-a", 2.0, &mut new_lease);
        assert_eq!(
            publish(&root, "project-a", "post-new", "pre-a", 2.0, &new_lease),
            PublishPairClaimOutcome::Published
        );
        assert!(!release_pair_claim(&root, &old_claim).unwrap());
        let current = read_pair_claim(&root, current_host_process_id(), "pre-a").unwrap();
        assert_eq!(current.post_instance_id, "post-new");
        assert!(pair_claim_is_live(&root, &current));
    }

    #[test]
    fn older_live_owner_cannot_take_a_newer_claim() {
        let root = isolated_root();
        let mut older = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        let mut newer = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        write_post(&root, "project-a", "post-b", "pre-a", 2.0, &mut newer);
        assert_eq!(
            publish(&root, "project-a", "post-b", "pre-a", 2.0, &newer),
            PublishPairClaimOutcome::Published
        );
        write_post(&root, "project-a", "post-a", "pre-a", 1.0, &mut older);
        assert_eq!(
            publish(&root, "project-a", "post-a", "pre-a", 1.0, &older),
            PublishPairClaimOutcome::OwnedByOther
        );
    }

    #[test]
    fn unsafe_identity_is_rejected_without_creating_an_escape_path() {
        let root = isolated_root();
        let result = publish_pair_claim(
            &root,
            "../escape",
            "project-a",
            "post-a",
            "owner-a",
            current_host_process_id(),
            1.0,
        );
        assert_eq!(result.unwrap_err().kind(), io::ErrorKind::InvalidInput);
        assert!(!root.parent().unwrap().join("escape.json").exists());
    }

    #[test]
    fn unrelated_decoy_directories_do_not_participate_in_exact_lookup() {
        let root = isolated_root();
        for n in 0..64 {
            let dir = root.join(format!("decoy-{n}")).join("post-decoy");
            fs::create_dir_all(&dir).unwrap();
            fs::write(dir.join("post.json"), b"not-json").unwrap();
        }
        assert!(read_pair_claim(&root, current_host_process_id(), "pre-a").is_none());
    }
}
