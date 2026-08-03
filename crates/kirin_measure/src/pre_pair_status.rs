//! PRE-side pair presentation observer.
//!
//! PRE has no in-process access to POST's selector generation. It therefore retains only the last
//! exact claim whose engine marker was proven live and reconciles that proof with the atomic claim
//! index. This module is GUI/status-poll only and is never used by the Audio Thread.

use std::path::Path;
use std::sync::Mutex;

use crate::pair_claim_index::{PairClaim, StablePairClaimObservation};
use crate::pair_status::PairStatus;

/// PRE-only engine-lifetime memory for the last claim whose marker was observed live.
///
/// Claim publication is atomic, but a status reader can still retain the old inode's bytes while
/// the POST commits a newer marker and retires the old marker. Keeping the exact old claim lets a
/// PRE prove that a temporarily missing claim index still belongs to a live POST.
#[derive(Debug, Default)]
pub struct PrePairStatusObserver {
    last_owned_claim: Mutex<Option<PairClaim>>,
}

impl PrePairStatusObserver {
    pub fn new() -> Self {
        Self::default()
    }

    /// Resolve the PRE-side view from its one exact POST ownership claim.
    pub fn observe(&self, kirin_root: &Path, pre_instance_id: &str, _pre_name: &str) -> PairStatus {
        let current_host = crate::post_candidates::current_host_process_id();
        self.observe_with(
            pre_instance_id,
            || {
                crate::pair_claim_index::try_observe_pair_claim(
                    kirin_root,
                    current_host,
                    pre_instance_id,
                )
            },
            |claim| crate::pair_claim_index::pair_claim_is_owned(kirin_root, claim),
        )
    }

    fn observe_with(
        &self,
        pre_instance_id: &str,
        read_stable: impl FnOnce() -> std::io::Result<StablePairClaimObservation>,
        mut claim_is_owned: impl FnMut(&PairClaim) -> bool,
    ) -> PairStatus {
        let mut last_owned = self
            .last_owned_claim
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if pre_instance_id.is_empty() {
            *last_owned = None;
            return PairStatus::Unpaired;
        }

        let current_host = crate::post_candidates::current_host_process_id();
        let matches_this_pre = |claim: &PairClaim| {
            claim.pre_instance_id == pre_instance_id && claim.host_process_id == current_host
        };

        // Check the old proof first. If the claim index was externally unlinked while the POST's
        // engine marker remains locked, the pair is still owned and repair must stay invisible.
        if last_owned
            .as_ref()
            .is_some_and(|claim| matches_this_pre(claim) && claim_is_owned(claim))
        {
            return PairStatus::Paired;
        }

        match read_stable() {
            Ok(StablePairClaimObservation::Contended) | Err(_) => {
                return if last_owned.is_some() {
                    PairStatus::Paired
                } else {
                    PairStatus::Waiting
                };
            }
            Ok(StablePairClaimObservation::Stable {
                claim: Some(claim),
                owned: true,
            }) if matches_this_pre(&claim) => {
                *last_owned = Some(claim);
                return PairStatus::Paired;
            }
            Ok(StablePairClaimObservation::Stable { .. }) => {}
        }

        // Explicit clear and POST engine Drop retire the marker. Once neither the cached marker
        // nor the transaction-stable current claim is owned, stale claim bytes are not authority.
        *last_owned = None;
        PairStatus::Unpaired
    }
}

pub fn pair_status_for_pre(
    observer: &PrePairStatusObserver,
    kirin_root: &Path,
    pre_instance_id: &str,
    pre_name: &str,
) -> PairStatus {
    observer.observe(kirin_root, pre_instance_id, pre_name)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::fs;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_root() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let root = std::env::temp_dir().join(format!(
            "kirin_pre_pair_status_{}_{}",
            std::process::id(),
            n
        ));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root
    }

    fn scripted_claim(owner: &str) -> PairClaim {
        PairClaim {
            schema: crate::pair_claim_index::PAIR_CLAIM_SCHEMA.to_string(),
            pre_instance_id: "pre-1".to_string(),
            project_hash: "project".to_string(),
            post_instance_id: "post-1".to_string(),
            post_watch_owner_id: String::new(),
            pair_owner_id: owner.to_string(),
            host_process_id: std::process::id(),
            pair_claimed_at_bits: 1.0f64.to_bits(),
        }
    }

    fn publish_owned_claim(root: &Path, pair_owner: &crate::PairOwnershipLease) -> PathBuf {
        let instance_dir = root.join("project").join("post-1");
        pair_owner.bind(&instance_dir, Some("pre-1"), 1.0).unwrap();
        crate::pair_claim_index::publish_pair_claim(
            root,
            "pre-1",
            "project",
            "post-1",
            pair_owner.owner_id(),
            std::process::id(),
            1.0,
        )
        .unwrap();
        instance_dir
    }

    #[test]
    fn checks_cached_old_marker_before_reading_replacement_claim() {
        let observer = PrePairStatusObserver::new();
        let old = scripted_claim("old-owner");
        let new = scripted_claim("new-owner");
        assert_eq!(
            observer.observe_with(
                "pre-1",
                || {
                    Ok(StablePairClaimObservation::Stable {
                        claim: Some(old.clone()),
                        owned: true,
                    })
                },
                |_| true,
            ),
            PairStatus::Paired
        );

        let events = RefCell::new(Vec::new());
        let status = observer.observe_with(
            "pre-1",
            || {
                events.borrow_mut().push("read:new");
                Ok(StablePairClaimObservation::Stable {
                    claim: Some(new.clone()),
                    owned: true,
                })
            },
            |claim| {
                if claim.pair_owner_id == "old-owner" {
                    events.borrow_mut().push("marker:old-dead");
                    false
                } else {
                    events.borrow_mut().push("marker:new-live");
                    true
                }
            },
        );

        assert_eq!(status, PairStatus::Paired);
        assert_eq!(events.into_inner(), ["marker:old-dead", "read:new"]);
    }

    #[test]
    fn writer_contention_retains_last_known_pair_without_reading_speculative_bytes() {
        let observer = PrePairStatusObserver::new();
        let old = scripted_claim("old-owner");
        assert_eq!(
            observer.observe_with(
                "pre-1",
                || {
                    Ok(StablePairClaimObservation::Stable {
                        claim: Some(old.clone()),
                        owned: true,
                    })
                },
                |_| true,
            ),
            PairStatus::Paired
        );

        let status = observer.observe_with(
            "pre-1",
            || Ok(StablePairClaimObservation::Contended),
            |_| false,
        );

        assert_eq!(status, PairStatus::Paired);
        let fresh = PrePairStatusObserver::new();
        assert_eq!(
            fresh.observe_with(
                "pre-1",
                || Ok(StablePairClaimObservation::Contended),
                |_| false,
            ),
            PairStatus::Waiting
        );
    }

    #[test]
    fn cached_live_marker_bridges_claim_unlink_but_not_engine_drop() {
        let root = isolated_root();
        let observer = PrePairStatusObserver::new();
        let pair_owner = crate::PairOwnershipLease::new();
        publish_owned_claim(&root, &pair_owner);
        assert_eq!(observer.observe(&root, "pre-1", "2Mix"), PairStatus::Paired);

        fs::remove_file(
            root.join("pair_target")
                .join(std::process::id().to_string())
                .join("pre-1.json"),
        )
        .unwrap();
        assert_eq!(
            observer.observe(&root, "pre-1", "2Mix"),
            PairStatus::Paired,
            "claim-index repair churn must not detach a live engine-owned PRE"
        );

        drop(pair_owner);
        assert_eq!(
            observer.observe(&root, "pre-1", "2Mix"),
            PairStatus::Unpaired,
            "engine Drop retires the cached marker and must clear PRE state"
        );
    }

    #[test]
    fn explicit_clear_retires_cached_marker_and_becomes_unpaired() {
        let root = isolated_root();
        let observer = PrePairStatusObserver::new();
        let pair_owner = crate::PairOwnershipLease::new();
        let instance_dir = publish_owned_claim(&root, &pair_owner);
        assert_eq!(observer.observe(&root, "pre-1", "2Mix"), PairStatus::Paired);

        pair_owner
            .commit_binding(Some(&instance_dir), None, 2.0, || Some(()))
            .unwrap();
        assert_eq!(
            observer.observe(&root, "pre-1", "2Mix"),
            PairStatus::Unpaired
        );
    }
}
