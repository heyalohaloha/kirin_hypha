//! One physical analysis slot shared by the Reference consumers of one engine lifetime.
use super::AnalysisLease;
use std::io;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

struct State {
    lease: AnalysisLease,
    users: usize,
}
#[derive(Clone)]
pub struct ReferenceAnalysisOwner(Arc<Mutex<State>>);
#[derive(Clone)]
pub struct ReferenceAnalysisGrant(Arc<Grant>);
struct Grant(ReferenceAnalysisOwner);
impl Drop for Grant {
    fn drop(&mut self) {
        let mut state = self.0 .0.lock().unwrap_or_else(|e| e.into_inner());
        state.users -= 1;
        if state.users == 0 {
            state.lease.release();
        }
    }
}
impl Default for ReferenceAnalysisOwner {
    fn default() -> Self {
        #[cfg(not(test))]
        let lease = AnalysisLease::for_current_process();
        #[cfg(test)]
        let lease = AnalysisLease::at_path(
            std::env::temp_dir().join(format!("reference-owner-{}.lease", uuid::Uuid::new_v4())),
        );
        Self::new(lease)
    }
}
impl ReferenceAnalysisOwner {
    fn new(lease: AnalysisLease) -> Self {
        Self(Arc::new(Mutex::new(State { lease, users: 0 })))
    }
    pub fn acquire(&self) -> Option<ReferenceAnalysisGrant> {
        let mut state = self.0.lock().ok()?;
        if !state.lease.try_acquire_for("Reference").ok()? {
            return None;
        }
        state.users += 1;
        Some(ReferenceAnalysisGrant(Arc::new(Grant(self.clone()))))
    }
    pub(super) fn compatible(&self, paths: &[PathBuf]) -> bool {
        self.0.lock().is_ok_and(|s| s.lease.paths == paths)
    }
    pub fn same_owner(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}
impl std::fmt::Debug for ReferenceAnalysisGrant {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("ReferenceAnalysisGrant")
            .field(&Arc::strong_count(&self.0))
            .finish()
    }
}

impl super::AuditionAdmission {
    pub fn try_acquire_shared(
        &mut self,
        owner: &ReferenceAnalysisOwner,
        label: &str,
    ) -> io::Result<bool> {
        if self.held {
            return Ok(true);
        }
        if !owner.compatible(&self.analysis.paths) {
            return Ok(false);
        }
        let Some(grant) = owner.acquire() else {
            return Ok(false);
        };
        self.shared_analysis = Some(grant);
        let result = self.try_acquire_for(label);
        if !matches!(result, Ok(true)) {
            self.shared_analysis = None;
        }
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn consumers_and_retiring_jobs_hold_exactly_one_kernel_slot() {
        let temp = tempfile::tempdir().unwrap();
        let paths = [temp.path().join("a"), temp.path().join("b")];
        let owner = ReferenceAnalysisOwner::new(AnalysisLease::at_paths(paths.clone()));
        let second = ReferenceAnalysisOwner::new(AnalysisLease::at_paths(paths.clone()));
        let third = ReferenceAnalysisOwner::new(AnalysisLease::at_paths(paths));
        let live = owner.acquire().unwrap();
        let revisit = owner.acquire().unwrap();
        let capture = owner.acquire().unwrap();
        let other = second.acquire().unwrap();
        assert!(third.acquire().is_none());
        let job = live.clone();
        drop((live, revisit, capture));
        assert!(third.acquire().is_none());
        drop(job);
        assert!(third.acquire().is_some());
        drop(other);
        assert!(!owner.same_owner(&second));
    }
}
