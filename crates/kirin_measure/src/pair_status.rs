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

/// Resolve POST presentation state from one coherent in-process binding observation.
///
/// `owns_exact_marker` is authoritative only when the same snapshot contained an exact PRE latch.
/// Watch/claim-file freshness is deliberately absent: repairable filesystem churn must not make a
/// live engine-owned pair disappear from the UI.
pub fn pair_status_from_owned_binding(
    pair_pre_name: &str,
    has_exact_binding: bool,
    owns_exact_marker: bool,
) -> PairStatus {
    pair_status_from_owned_binding_with_intent(
        !pair_pre_name.is_empty(),
        has_exact_binding,
        owns_exact_marker,
    )
}

/// Name-independent variant for explicit exact selection. An unnamed PRE remains Waiting after
/// an internal exact-release; only an explicit user clear removes `selection_intent`.
pub fn pair_status_from_owned_binding_with_intent(
    selection_intent: bool,
    has_exact_binding: bool,
    owns_exact_marker: bool,
) -> PairStatus {
    if has_exact_binding {
        if owns_exact_marker {
            PairStatus::Paired
        } else {
            PairStatus::Waiting
        }
    } else if !selection_intent {
        PairStatus::Unpaired
    } else {
        PairStatus::Waiting
    }
}

/// Keep the last coherent observation while the nonblocking lease observer is busy. Before the
/// first coherent observation, `Waiting` is the truthful neutral state: it avoids a false
/// one-frame `Unpaired` result during startup/restore contention.
pub fn pair_status_or_last_known(
    observed: Option<PairStatus>,
    last_known: Option<PairStatus>,
) -> PairStatus {
    observed.or(last_known).unwrap_or(PairStatus::Waiting)
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
        return pair_status_from_owned_binding(pair_pre_name, false, false);
    };

    let current_host = crate::post_candidates::current_host_process_id();
    let owned = matches!(
        crate::pair_claim_index::try_observe_pair_claim(
            kirin_root,
            current_host,
            &latched.instance_id,
        ),
        Ok(crate::pair_claim_index::StablePairClaimObservation::Stable {
            claim: Some(claim),
            owned: true,
        }) if claim.project_hash == post_project_hash
            && claim.post_instance_id == post_instance_id
            && claim.pair_claimed_at_bits == pair_claimed_at.to_bits()
    );
    pair_status_from_owned_binding(pair_pre_name, true, owned)
}

pub fn paired_pre_instance_id(latched: &Mutex<Option<LatchedPre>>) -> Option<String> {
    latched
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|pre| pre.instance_id.clone())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pre_pair_status::PrePairStatusObserver;
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

    fn pair_status_for_pre(root: &Path, pre_instance_id: &str, pre_name: &str) -> PairStatus {
        crate::pre_pair_status::pair_status_for_pre(
            &PrePairStatusObserver::new(),
            root,
            pre_instance_id,
            pre_name,
        )
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
    fn in_memory_binding_status_requires_an_exact_owned_marker() {
        assert_eq!(
            pair_status_from_owned_binding("2Mix", true, true),
            PairStatus::Paired
        );
        assert_eq!(
            pair_status_from_owned_binding("2Mix", true, false),
            PairStatus::Waiting
        );
        assert_eq!(
            pair_status_from_owned_binding("2Mix", false, false),
            PairStatus::Waiting
        );
        assert_eq!(
            pair_status_from_owned_binding("", false, false),
            PairStatus::Unpaired
        );
        assert_eq!(
            pair_status_from_owned_binding("", false, true),
            PairStatus::Unpaired,
            "an owned bit without an exact latch is not a valid pair observation"
        );
        assert_eq!(
            pair_status_from_owned_binding_with_intent(true, false, false),
            PairStatus::Waiting,
            "an unnamed exact selection remains waiting after internal release"
        );
        assert_eq!(
            pair_status_from_owned_binding_with_intent(false, false, false),
            PairStatus::Unpaired,
            "fresh or explicitly cleared selection is unpaired"
        );
    }

    #[test]
    fn unknown_observation_uses_lkg_and_never_defaults_to_unpaired() {
        assert_eq!(pair_status_or_last_known(None, None), PairStatus::Waiting);
        assert_eq!(
            pair_status_or_last_known(None, Some(PairStatus::Paired)),
            PairStatus::Paired
        );
        assert_eq!(
            pair_status_or_last_known(Some(PairStatus::Unpaired), Some(PairStatus::Paired)),
            PairStatus::Unpaired,
            "a coherent unpaired observation must replace the cache"
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
                post_watch_owner_id: String::new(),
                pair_owner_id: uuid::Uuid::new_v4().to_string(),
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
