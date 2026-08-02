//! Factual PRE/POST pairing state shared by AU and VST3 presentation adapters.

use std::path::Path;
use std::sync::Mutex;

use crate::pairing_scope::LatchedPre;

/// Pair status is deliberately separate from Record/KEEP state.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PairStatus {
    /// No human selector and no exact runtime binding.
    Unpaired = 0,
    /// A selector/exact target exists, but this POST has not acquired the shared ownership claim.
    Waiting = 1,
    /// The exact PRE/POST relationship is owned. Watch freshness does not change this state.
    Paired = 2,
}

pub fn pair_status_for_post(
    kirin_root: &Path,
    post_project_hash: &str,
    post_instance_id: &str,
    pair_claimed_at: f64,
    pair_pre_name: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> PairStatus {
    let latched = latched
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .clone();
    let Some(latched) = latched else {
        return if pair_pre_name.is_empty() {
            PairStatus::Unpaired
        } else {
            PairStatus::Waiting
        };
    };

    let current_host = crate::post_candidates::current_host_process_id();
    let owned =
        crate::pair_claim_index::read_pair_claim(kirin_root, current_host, &latched.instance_id)
            .is_some_and(|claim| {
                claim.project_hash == post_project_hash
                    && claim.post_instance_id == post_instance_id
                    && claim.pair_claimed_at_bits == pair_claimed_at.to_bits()
                    && crate::pair_claim_index::pair_claim_is_owned(kirin_root, &claim)
            });
    if owned {
        PairStatus::Paired
    } else {
        PairStatus::Waiting
    }
}

pub fn paired_pre_instance_id(latched: &Mutex<Option<LatchedPre>>) -> Option<String> {
    latched
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|pre| pre.instance_id.clone())
}

/// Resolve the PRE-side view from its one exact POST ownership claim.
pub fn pair_status_for_pre(
    kirin_root: &Path,
    pre_instance_id: &str,
    _pre_name: &str,
) -> PairStatus {
    if pre_instance_id.is_empty() {
        return PairStatus::Unpaired;
    }

    let current_host = crate::post_candidates::current_host_process_id();
    let Some(claim) =
        crate::pair_claim_index::read_pair_claim(kirin_root, current_host, pre_instance_id)
    else {
        return PairStatus::Unpaired;
    };
    if crate::pair_claim_index::pair_claim_is_owned(kirin_root, &claim) {
        PairStatus::Paired
    } else {
        // A released/crashed POST must not leave PRE looking half-paired forever. The stale fixed
        // pointer is non-authoritative and a later POST can replace it under the per-PRE lock.
        PairStatus::Unpaired
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_root() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let root =
            std::env::temp_dir().join(format!("kirin_pair_status_{}_{}", std::process::id(), n));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root
    }

    fn write_post_snapshot_and_claim(
        root: &Path,
        project: &str,
        post: &str,
        pre: &str,
        signal_state: &str,
        pair_owner: &crate::PairOwnershipLease,
        watch_owner: &mut crate::watch_snapshot_lease::WatchSnapshotLease,
    ) {
        let instance_dir = root.join(project).join(post);
        pair_owner.bind(&instance_dir, Some(pre), 1.0).unwrap();
        watch_owner.bind(&instance_dir).unwrap();
        fs::write(
            instance_dir.join("post.json"),
            serde_json::json!({
                "v": 2,
                "role": "POST",
                "instance_id": post,
                "watch_owner_id": watch_owner.owner_id(),
                "host_process_id": std::process::id(),
                "signal_state": signal_state,
                "t": chrono::Utc::now().to_rfc3339(),
                "pair_pre_name": "2Mix",
                "paired_pre_instance_id": pre,
                "pair_claimed_at": 1.0,
            })
            .to_string(),
        )
        .unwrap();
        crate::pair_claim_index::publish_pair_claim(
            root,
            pre,
            project,
            post,
            pair_owner.owner_id(),
            std::process::id(),
            1.0,
        )
        .unwrap();
    }

    fn exact_latch(root: &Path, pre: &str) -> LatchedPre {
        LatchedPre {
            name: "2Mix".to_string(),
            instance_id: pre.to_string(),
            project_dir: root.join("pre-project"),
            pre_json: root.join("pre-project").join(pre).join("pre.json"),
            daw_session_id: None,
            host_process_id: Some(std::process::id()),
            readiness: crate::LatchedPreReadiness::Confirmed,
        }
    }

    #[test]
    fn post_without_selector_is_unpaired() {
        let root = isolated_root();
        assert_eq!(
            pair_status_for_post(&root, "project", "post-1", 0.0, "", &Mutex::new(None)),
            PairStatus::Unpaired
        );
    }

    #[test]
    fn post_name_without_exact_runtime_is_waiting() {
        let root = isolated_root();
        assert_eq!(
            pair_status_for_post(&root, "project", "post-1", 1.0, "2Mix", &Mutex::new(None),),
            PairStatus::Waiting
        );
    }

    #[test]
    fn missing_latched_snapshot_is_waiting() {
        let root = isolated_root();
        let latch = exact_latch(&root, "pre-1");
        assert_eq!(
            pair_status_for_post(
                &root,
                "project",
                "post-1",
                1.0,
                "2Mix",
                &Mutex::new(Some(latch)),
            ),
            PairStatus::Waiting
        );
    }

    #[test]
    fn owned_pair_survives_watch_writer_restart_and_missing_pre_snapshot() {
        let root = isolated_root();
        let pair_owner = crate::PairOwnershipLease::new();
        let mut first_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        write_post_snapshot_and_claim(
            &root,
            "project",
            "post-1",
            "pre-1",
            "inactive",
            &pair_owner,
            &mut first_watch,
        );
        let latched = Mutex::new(Some(exact_latch(&root, "pre-1")));
        assert_eq!(
            pair_status_for_post(&root, "project", "post-1", 1.0, "2Mix", &latched,),
            PairStatus::Paired
        );
        assert_eq!(
            pair_status_for_pre(&root, "pre-1", "2Mix"),
            PairStatus::Paired
        );

        drop(first_watch);
        assert_eq!(
            pair_status_for_post(&root, "project", "post-1", 1.0, "2Mix", &latched,),
            PairStatus::Paired,
            "Watch worker loss must not release POST binding"
        );
        assert_eq!(
            pair_status_for_pre(&root, "pre-1", "2Mix"),
            PairStatus::Paired,
            "Watch worker loss must not release PRE view"
        );

        let mut restarted_watch = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        write_post_snapshot_and_claim(
            &root,
            "project",
            "post-1",
            "pre-1",
            "inactive",
            &pair_owner,
            &mut restarted_watch,
        );
        assert_eq!(
            pair_status_for_post(&root, "project", "post-1", 1.0, "2Mix", &latched,),
            PairStatus::Paired
        );

        drop(pair_owner);
        assert_eq!(
            pair_status_for_post(&root, "project", "post-1", 1.0, "2Mix", &latched,),
            PairStatus::Waiting,
            "only POST engine destruction releases shared ownership"
        );
        assert_eq!(
            pair_status_for_pre(&root, "pre-1", "2Mix"),
            PairStatus::Unpaired
        );
    }

    #[test]
    fn another_posts_claim_does_not_confirm_this_post() {
        let root = isolated_root();
        let pair_owner = crate::PairOwnershipLease::new();
        let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        write_post_snapshot_and_claim(
            &root,
            "project",
            "post-other",
            "pre-1",
            "inactive",
            &pair_owner,
            &mut watch_owner,
        );
        let latched = Mutex::new(Some(exact_latch(&root, "pre-1")));
        assert_eq!(
            pair_status_for_post(&root, "project", "post-self", 1.0, "2Mix", &latched,),
            PairStatus::Waiting
        );
    }

    #[test]
    fn exact_claim_does_not_mark_a_duplicate_name_pre_as_waiting() {
        let root = isolated_root();
        let pair_owner = crate::PairOwnershipLease::new();
        let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        write_post_snapshot_and_claim(
            &root,
            "project",
            "post-1",
            "pre-selected",
            "inactive",
            &pair_owner,
            &mut watch_owner,
        );

        assert_eq!(
            pair_status_for_pre(&root, "pre-other", "2Mix"),
            PairStatus::Unpaired
        );
    }

    #[test]
    fn foreign_host_claim_never_marks_this_pre_as_paired() {
        let root = isolated_root();
        let claim_dir = root
            .join("pair_target")
            .join(std::process::id().to_string());
        fs::create_dir_all(&claim_dir).unwrap();
        fs::write(
            claim_dir.join("pre-1.json"),
            serde_json::to_vec(&crate::pair_claim_index::PairClaim {
                schema: crate::pair_claim_index::PAIR_CLAIM_SCHEMA.to_string(),
                pre_instance_id: "pre-1".to_string(),
                project_hash: "project".to_string(),
                post_instance_id: "post-1".to_string(),
                post_watch_owner_id: uuid::Uuid::new_v4().to_string(),
                host_process_id: std::process::id().saturating_add(1),
                pair_claimed_at_bits: 1.0f64.to_bits(),
            })
            .unwrap(),
        )
        .unwrap();

        assert_eq!(
            pair_status_for_pre(&root, "pre-1", "2Mix"),
            PairStatus::Unpaired
        );
    }

    #[test]
    fn bypassed_post_keeps_its_exact_pair_visible() {
        let root = isolated_root();
        let pair_owner = crate::PairOwnershipLease::new();
        let mut watch_owner = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        write_post_snapshot_and_claim(
            &root,
            "project",
            "post-1",
            "pre-1",
            "bypassed",
            &pair_owner,
            &mut watch_owner,
        );

        assert_eq!(
            pair_status_for_pre(&root, "pre-1", "2Mix"),
            PairStatus::Paired
        );
    }
}
