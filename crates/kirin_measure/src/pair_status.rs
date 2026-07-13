//! Factual PRE/POST pairing state shared by AU and VST3 presentation adapters.

use std::path::Path;
use std::sync::Mutex;
use std::time::Duration;

use crate::pairing_scope::{read_pre_at, LatchedPre};

/// Pair status is deliberately separate from Record/KEEP state.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PairStatus {
    /// No human selector and no exact runtime binding.
    Unpaired = 0,
    /// A selector or exact runtime exists, but both sides have not published fresh evidence yet.
    Waiting = 1,
    /// An exact peer runtime and its fresh owned Watch snapshot are present.
    Paired = 2,
}

pub fn pair_status_for_post(
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

    let exact_and_fresh = read_pre_at(&latched.pre_json).is_some_and(|state| {
        state.instance_id.as_deref() == Some(latched.instance_id.as_str())
            && state.fresh
            && crate::watch_snapshot_lease::snapshot_file_is_fresh_runtime_evidence(
                &latched.pre_json,
                Duration::from_secs(crate::pre_discovery::DISCOVERY_STALE_SECS),
            )
    });
    if exact_and_fresh {
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

/// Resolve the PRE-side view from POST Watch claims.
pub fn pair_status_for_pre(kirin_root: &Path, pre_instance_id: &str, pre_name: &str) -> PairStatus {
    if pre_instance_id.is_empty() {
        return PairStatus::Unpaired;
    }

    let current_host = crate::post_candidates::current_host_process_id();
    let live: Vec<_> = crate::post_candidates::enumerate_live_post_pair_candidates(kirin_root)
        .into_iter()
        .filter(|post| post.host_process_id == Some(current_host))
        .collect();
    if live
        .iter()
        .filter(|post| {
            crate::watch_snapshot_lease::snapshot_file_is_fresh_runtime_evidence(
                &post.path,
                Duration::from_secs(crate::pre_discovery::DISCOVERY_STALE_SECS),
            )
        })
        .any(|post| post.paired_pre_instance_id.as_deref() == Some(pre_instance_id))
    {
        return PairStatus::Paired;
    }

    let exact_waiting = live
        .iter()
        .any(|post| post.paired_pre_instance_id.as_deref() == Some(pre_instance_id));
    let name_waiting = !pre_name.is_empty()
        && live.iter().any(|post| {
            post.paired_pre_instance_id.is_none() && post.pair_pre_name.as_deref() == Some(pre_name)
        });
    if exact_waiting || name_waiting {
        PairStatus::Waiting
    } else {
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

    #[test]
    fn post_without_selector_is_unpaired() {
        assert_eq!(
            pair_status_for_post("", &Mutex::new(None)),
            PairStatus::Unpaired
        );
    }

    #[test]
    fn post_name_without_exact_runtime_is_waiting() {
        assert_eq!(
            pair_status_for_post("2Mix", &Mutex::new(None)),
            PairStatus::Waiting
        );
    }

    #[test]
    fn missing_latched_snapshot_is_waiting() {
        let latch = LatchedPre {
            name: "2Mix".to_string(),
            instance_id: "pre-1".to_string(),
            project_dir: PathBuf::from("/missing"),
            pre_json: PathBuf::from("/missing/pre.json"),
            daw_session_id: None,
            host_process_id: None,
        };
        assert_eq!(
            pair_status_for_post("2Mix", &Mutex::new(Some(latch))),
            PairStatus::Waiting
        );
    }

    #[test]
    fn post_is_paired_only_while_exact_pre_owner_is_live_and_fresh() {
        let root = isolated_root();
        let instance_dir = root.join("project").join("pre-1");
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        let pre_json = instance_dir.join("pre.json");
        fs::write(
            &pre_json,
            serde_json::json!({
                "instance_id": "pre-1",
                "watch_owner_id": lease.owner_id(),
                "name": "2Mix",
                "signal_state": "inactive",
                "host_process_id": std::process::id(),
                "t": chrono::Utc::now().to_rfc3339(),
            })
            .to_string(),
        )
        .unwrap();
        let latch = LatchedPre {
            name: "2Mix".to_string(),
            instance_id: "pre-1".to_string(),
            project_dir: root.join("project"),
            pre_json,
            daw_session_id: None,
            host_process_id: None,
        };
        let latched = Mutex::new(Some(latch));
        assert_eq!(pair_status_for_post("2Mix", &latched), PairStatus::Paired);
        drop(lease);
        assert_eq!(pair_status_for_post("2Mix", &latched), PairStatus::Waiting);
    }

    #[test]
    fn post_does_not_confirm_a_snapshot_with_the_wrong_instance_id() {
        let root = isolated_root();
        let instance_dir = root.join("project").join("pre-1");
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        let pre_json = instance_dir.join("pre.json");
        fs::write(
            &pre_json,
            serde_json::json!({
                "instance_id": "pre-other",
                "watch_owner_id": lease.owner_id(),
                "signal_state": "inactive",
                "t": chrono::Utc::now().to_rfc3339(),
            })
            .to_string(),
        )
        .unwrap();
        let latched = Mutex::new(Some(LatchedPre {
            name: "2Mix".to_string(),
            instance_id: "pre-1".to_string(),
            project_dir: root.join("project"),
            pre_json,
            daw_session_id: None,
            host_process_id: None,
        }));

        assert_eq!(pair_status_for_post("2Mix", &latched), PairStatus::Waiting);
    }

    #[test]
    fn pre_reads_exact_post_claim_as_paired_and_owner_drop_as_unpaired() {
        let root = isolated_root();
        let instance_dir = root.join("project").join("post-1");
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        fs::write(
            instance_dir.join("post.json"),
            serde_json::json!({
                "v": 2,
                "role": "POST",
                "instance_id": "post-1",
                "watch_owner_id": lease.owner_id(),
                "host_process_id": std::process::id(),
                "signal_state": "inactive",
                "t": chrono::Utc::now().to_rfc3339(),
                "pair_pre_name": "2Mix",
                "paired_pre_instance_id": "pre-1",
                "pair_claimed_at": 1.0,
            })
            .to_string(),
        )
        .unwrap();
        assert_eq!(
            pair_status_for_pre(&root, "pre-1", "2Mix"),
            PairStatus::Paired
        );
        drop(lease);
        assert_eq!(
            pair_status_for_pre(&root, "pre-1", "2Mix"),
            PairStatus::Unpaired
        );
    }

    #[test]
    fn exact_claim_does_not_mark_a_duplicate_name_pre_as_waiting() {
        let root = isolated_root();
        let instance_dir = root.join("project").join("post-1");
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        fs::write(
            instance_dir.join("post.json"),
            serde_json::json!({
                "instance_id": "post-1",
                "watch_owner_id": lease.owner_id(),
                "host_process_id": std::process::id(),
                "signal_state": "inactive",
                "t": chrono::Utc::now().to_rfc3339(),
                "pair_pre_name": "2Mix",
                "paired_pre_instance_id": "pre-selected",
            })
            .to_string(),
        )
        .unwrap();

        assert_eq!(
            pair_status_for_pre(&root, "pre-other", "2Mix"),
            PairStatus::Unpaired
        );
    }

    #[test]
    fn foreign_host_claim_never_marks_this_pre_as_paired() {
        let root = isolated_root();
        let instance_dir = root.join("project").join("post-1");
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        fs::write(
            instance_dir.join("post.json"),
            serde_json::json!({
                "instance_id": "post-1",
                "watch_owner_id": lease.owner_id(),
                "host_process_id": std::process::id().saturating_add(1),
                "signal_state": "inactive",
                "t": chrono::Utc::now().to_rfc3339(),
                "pair_pre_name": "2Mix",
                "paired_pre_instance_id": "pre-1",
            })
            .to_string(),
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
        let instance_dir = root.join("project").join("post-1");
        let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
        lease.bind(&instance_dir).unwrap();
        fs::write(
            instance_dir.join("post.json"),
            serde_json::json!({
                "instance_id": "post-1",
                "watch_owner_id": lease.owner_id(),
                "host_process_id": std::process::id(),
                "signal_state": "bypassed",
                "t": chrono::Utc::now().to_rfc3339(),
                "pair_pre_name": "2Mix",
                "paired_pre_instance_id": "pre-1",
            })
            .to_string(),
        )
        .unwrap();

        assert_eq!(
            pair_status_for_pre(&root, "pre-1", "2Mix"),
            PairStatus::Paired
        );
    }
}
