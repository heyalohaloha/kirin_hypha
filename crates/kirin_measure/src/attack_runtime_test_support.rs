//! Deterministic lock and lifecycle injection for disposable cross-crate snapshot fixtures.
//! Compiled only in tests or when the explicit dev-dependency feature is enabled.
use super::super::snapshot::AttackObservationSnapshot;
use super::super::AttackHistory;
use super::{AttackRuntime, AttackSingleSnapshot};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::Ordering;
use std::sync::Arc;

#[doc(hidden)]
impl AttackRuntime {
    pub fn fixture_publish_observation(&self, snapshot: AttackObservationSnapshot) {
        if let Some(source) = snapshot.source {
            self.generation
                .store(source.source.generation, Ordering::Release);
        }
        self.enabled.store(true, Ordering::Release);
        self.worker_running.store(true, Ordering::Release);
        self.set_band(snapshot.band);
        *self.observations.lock().unwrap() = Arc::new(snapshot);
    }

    pub fn fixture_select_single(&self, snapshot: AttackSingleSnapshot) {
        let mut selected = self.selected.lock().unwrap();
        selected.serial = snapshot.token;
        selected.current = Some(snapshot);
    }

    pub fn fixture_generation(&self) -> u64 {
        self.generation.load(Ordering::Acquire)
    }

    pub fn fixture_poison_single(&self) {
        assert!(catch_unwind(AssertUnwindSafe(|| {
            self.fixture_with_single_lock(|| panic!("disposable selected-control fixture"));
        }))
        .is_err());
    }

    pub fn fixture_publish_history(&self, history: AttackHistory) {
        *self.history.lock().unwrap() = history;
    }

    pub fn fixture_with_single_lock<T>(&self, action: impl FnOnce() -> T) -> T {
        let _guard = self.selected.lock().unwrap();
        action()
    }

    pub fn fixture_with_observation_lock<T>(&self, action: impl FnOnce() -> T) -> T {
        let _guard = self.observations.lock().unwrap();
        action()
    }
}
