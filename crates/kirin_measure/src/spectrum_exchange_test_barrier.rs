//! Non-audio test barriers. This module is absent from shipping builds.
use super::{Duration, Ordering, SpectrumCoordinator};
impl SpectrumCoordinator {
    /// Test barrier through the real worker, after the caller has awaited analysis completion.
    /// No worker means service_*_endpoint already performed the synchronous tick.
    pub fn request_exchange_tick_for_test(&self) -> Option<u64> {
        if !self.exchange_worker.is_started() {
            return None;
        }
        Some(self.request_endpoint_tick_for_test())
    }

    pub fn request_endpoint_tick_for_test(&self) -> u64 {
        let _guard = self
            .exchange_worker
            .state
            .wake
            .0
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        let ticket = self
            .exchange_worker
            .state
            .requested_tick
            .fetch_add(1, Ordering::AcqRel)
            + 1;
        self.exchange_worker.notify();
        ticket
    }
    pub fn exchange_tick_completed_for_test(&self, ticket: u64) -> bool {
        self.exchange_worker
            .state
            .completed_tick
            .load(Ordering::Acquire)
            >= ticket
    }

    /// Non-audio fixture barrier; an early notification is retained by the ticket predicate.
    pub fn wait_exchange_tick_for_test(&self, ticket: u64, timeout: Duration) -> bool {
        let state = &self.exchange_worker.state;
        let guard = state.wake.0.lock().unwrap_or_else(|e| e.into_inner());
        let _guard = state
            .wake
            .1
            .wait_timeout_while(guard, timeout, |_| {
                state.completed_tick.load(Ordering::Acquire) < ticket
                    && !state.shutdown.load(Ordering::Acquire)
            })
            .unwrap_or_else(|e| e.into_inner());
        state.completed_tick.load(Ordering::Acquire) >= ticket
    }

    pub(super) fn complete_endpoint_tick_for_test(&self, requested: u64) {
        let state = &self.exchange_worker.state;
        let _guard = state.wake.0.lock().unwrap_or_else(|e| e.into_inner());
        state.completed_tick.fetch_max(requested, Ordering::Release);
        state.wake.1.notify_all();
    }
}
