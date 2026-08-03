//! Coherent nonblocking observation of engine-owned pair state.

use std::path::PathBuf;
use std::sync::TryLockError;

use super::{desired_marker_path, PairOwnershipLease};

/// Exact fields needed to derive one lease marker without reading the filesystem.
#[derive(Clone, Debug)]
pub struct PairOwnershipBinding {
    instance_dir: PathBuf,
    pre_instance_id: String,
    pair_claimed_at: f64,
}

impl PairOwnershipBinding {
    pub fn new(instance_dir: PathBuf, pre_instance_id: String, pair_claimed_at: f64) -> Self {
        Self {
            instance_dir,
            pre_instance_id,
            pair_claimed_at,
        }
    }
}

impl PairOwnershipLease {
    /// Observe selector/latch state only when no pair transaction is in progress.
    ///
    /// Contention returns `None`; GUI callers retain their last coherent state. The control lock is
    /// try-only for the reader, so pair publication is never delayed by a waiting UI thread.
    pub fn observe_binding_if_stable<R>(
        &self,
        snapshot: impl FnOnce() -> (R, Option<PairOwnershipBinding>),
    ) -> Option<(R, bool)> {
        let _control = match self.control.try_lock() {
            Ok(guard) => guard,
            Err(TryLockError::Poisoned(error)) => error.into_inner(),
            Err(TryLockError::WouldBlock) => return None,
        };
        let (value, binding) = snapshot();
        let desired = binding.as_ref().and_then(|binding| {
            desired_marker_path(
                Some(&binding.instance_dir),
                &self.owner_id,
                Some(&binding.pre_instance_id),
                binding.pair_claimed_at,
            )
            .ok()
            .flatten()
        });
        let state = match self.state.try_lock() {
            Ok(state) => state,
            Err(TryLockError::Poisoned(error)) => error.into_inner(),
            Err(TryLockError::WouldBlock) => return None,
        };
        let owns_exact_marker = desired.as_ref().is_some_and(|desired| {
            state
                .marker
                .as_ref()
                .is_some_and(|marker| marker.path == *desired)
        });
        Some((value, owns_exact_marker))
    }
}
