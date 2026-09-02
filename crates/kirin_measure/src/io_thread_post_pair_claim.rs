//! Exact PRE ownership claim publication for the POST IO coordinator.

use std::path::Path;
use std::time::{Duration, Instant};

use crate::{PairClaim, PairOwnershipLease};

const PAIR_CLAIM_PUBLISH_INTERVAL: Duration = Duration::from_secs(1);

/// Revalidate the currently observed claim, then atomically publish the exact desired binding.
///
/// `validate_binding` runs under the engine-owned pair transaction immediately before commit. It
/// must compare the live selector state with the expected PRE and timestamp supplied here.
#[allow(clippy::too_many_arguments)]
pub(super) fn service_pair_claim(
    kirin_root: &Path,
    instance_dir: &Path,
    post_snapshot_written: bool,
    current_pre: Option<&str>,
    project_hash: &str,
    post_instance_id: &str,
    current_claimed_at: f64,
    pair_owner: &PairOwnershipLease,
    owned_pair_claim: &mut Option<PairClaim>,
    next_pair_claim_publish: &mut Instant,
    validate_binding: impl Fn(Option<&str>, f64) -> bool,
) {
    if let Some(owned) = owned_pair_claim.as_ref() {
        if Instant::now() >= *next_pair_claim_publish {
            let current = crate::pair_claim_index::read_pair_claim(
                kirin_root,
                owned.host_process_id,
                &owned.pre_instance_id,
            );
            if current.as_ref() != Some(owned)
                || !crate::pair_claim_index::pair_claim_is_owned(kirin_root, owned)
            {
                *owned_pair_claim = None;
            }
            *next_pair_claim_publish = Instant::now() + PAIR_CLAIM_PUBLISH_INTERVAL;
        }
    }

    let desired_matches_owned = owned_pair_claim.as_ref().is_some_and(|owned| {
        super::identity::pair_claim_matches_desired_binding(
            owned,
            current_pre,
            project_hash,
            post_instance_id,
            pair_owner.owner_id(),
            current_claimed_at,
        )
    });
    if owned_pair_claim.is_some() && !desired_matches_owned {
        *owned_pair_claim = None;
        *next_pair_claim_publish = Instant::now();
    }

    if owned_pair_claim.is_none() && Instant::now() >= *next_pair_claim_publish {
        let valid_binding = post_snapshot_written
            && current_pre.is_some()
            && current_claimed_at.is_finite()
            && current_claimed_at > 0.0;
        let desired_pre = valid_binding
            .then(|| current_pre.map(str::to_string))
            .flatten();
        let expected_pre = desired_pre.clone();
        let expected_claimed_at = if desired_pre.is_some() {
            current_claimed_at
        } else {
            0.0
        };
        let committed = pair_owner.commit_claimed_binding_if(
            kirin_root,
            Some(instance_dir),
            desired_pre.as_deref(),
            project_hash,
            post_instance_id,
            expected_claimed_at,
            || validate_binding(expected_pre.as_deref(), expected_claimed_at),
            || Some(()),
        );
        match committed {
            Ok(Some(())) => {
                *owned_pair_claim = desired_pre.as_deref().and_then(|pre_instance_id| {
                    crate::pair_claim_index::read_pair_claim(
                        kirin_root,
                        crate::post_candidates::current_host_process_id(),
                        pre_instance_id,
                    )
                });
            }
            Ok(None) => {}
            Err(error) => log::debug!(
                "[POST pair claim] atomic commit deferred: instance_id={} error={}",
                post_instance_id,
                error
            ),
        }
        *next_pair_claim_publish = Instant::now() + PAIR_CLAIM_PUBLISH_INTERVAL;
    }
}
