//! Dedicated, on-demand Spectrum filesystem exchange worker.
//!
//! The existing PRE/POST IO workers publish only endpoint changes. Once an exact pair becomes
//! active, this worker owns the optional 30 Hz request/snapshot exchange. A slow filesystem can
//! therefore delay Spectrum without delaying Watch, Record, pairing, or the Audio Thread.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex, Weak};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use crate::spectrum::SPECTRUM_PRESENTATION_HZ;
use crate::spectrum_exchange::{SpectrumCoordinator, SpectrumTarget};

const INACTIVE_WAIT: Duration = Duration::from_millis(250);
const ACTIVE_WAIT: Duration =
    Duration::from_nanos(1_000_000_000_u64 / SPECTRUM_PRESENTATION_HZ as u64);
const ABSOLUTE_ACTIVE_WAIT: Duration = Duration::from_millis(100);
/// The normal PRE/POST IO loop calls `service_*_endpoint()` at 10 Hz. Eight calls without a
/// successfully published request/readiness/snapshot detect a stalled exchange after about
/// 800 ms, leaving margin before the 1.5 s request lease expires. The rescue tick stays on the IO
/// thread and is non-blocking on the exchange session lock, so it can never delay the Audio Thread
/// or create a second owner.
pub(crate) const SUPERVISOR_STALL_SERVICE_TICKS: u8 = 8;

#[derive(Clone, Debug, Eq, PartialEq)]
enum Endpoint {
    None,
    Pre {
        instance_id: String,
        instance_dir: PathBuf,
    },
    Post {
        instance_id: String,
        target: Option<SpectrumTarget>,
        owner_name: String,
    },
}

struct WorkerState {
    endpoint: Mutex<Endpoint>,
    shutdown: AtomicBool,
    wake: (Mutex<()>, Condvar),
    published_updates: AtomicU64,
    #[cfg(test)]
    pause_ticks: AtomicBool,
    #[cfg(test)]
    fail_ticks: AtomicBool,
}

#[derive(Default)]
struct SupervisorState {
    observed_published_updates: u64,
    unchanged_service_ticks: u8,
}

pub(crate) struct SpectrumExchangeWorker {
    state: Arc<WorkerState>,
    handle: Mutex<Option<JoinHandle<()>>>,
    supervisor: Mutex<SupervisorState>,
}

impl SpectrumExchangeWorker {
    pub(crate) fn new() -> Self {
        Self {
            state: Arc::new(WorkerState {
                endpoint: Mutex::new(Endpoint::None),
                shutdown: AtomicBool::new(false),
                wake: (Mutex::new(()), Condvar::new()),
                published_updates: AtomicU64::new(0),
                #[cfg(test)]
                pause_ticks: AtomicBool::new(false),
                #[cfg(test)]
                fail_ticks: AtomicBool::new(false),
            }),
            handle: Mutex::new(None),
            supervisor: Mutex::new(SupervisorState::default()),
        }
    }

    pub(crate) fn update_pre(&self, instance_id: &str, instance_dir: &std::path::Path) {
        self.update_endpoint(Endpoint::Pre {
            instance_id: instance_id.to_string(),
            instance_dir: instance_dir.to_path_buf(),
        });
    }

    pub(crate) fn update_post(
        &self,
        instance_id: &str,
        target: Option<SpectrumTarget>,
        owner_name: &str,
    ) {
        self.update_endpoint(Endpoint::Post {
            instance_id: instance_id.to_string(),
            target,
            owner_name: owner_name.to_string(),
        });
    }

    pub(crate) fn is_started(&self) -> bool {
        let handle = match self.handle.lock() {
            Ok(handle) => handle,
            Err(poisoned) => poisoned.into_inner(),
        };
        handle.as_ref().is_some_and(|handle| !handle.is_finished())
    }

    /// Called only by the existing 10 Hz PRE/POST IO thread. Failed attempts do not advance this
    /// counter. If factual publication stops, one exact exchange tick is allowed through the
    /// stable IO path instead of leaving `DATA —` forever.
    fn needs_supervisor_tick(&self) -> bool {
        let published = self.state.published_updates.load(Ordering::Acquire);
        let mut supervisor = match self.supervisor.lock() {
            Ok(supervisor) => supervisor,
            Err(poisoned) => poisoned.into_inner(),
        };
        if published != supervisor.observed_published_updates {
            supervisor.observed_published_updates = published;
            supervisor.unchanged_service_ticks = 0;
            return false;
        }
        supervisor.unchanged_service_ticks = supervisor.unchanged_service_ticks.saturating_add(1);
        if supervisor.unchanged_service_ticks < SUPERVISOR_STALL_SERVICE_TICKS {
            return false;
        }
        true
    }

    fn observe_supervisor_progress(&self) {
        let published = self.state.published_updates.load(Ordering::Acquire);
        let mut supervisor = match self.supervisor.lock() {
            Ok(supervisor) => supervisor,
            Err(poisoned) => poisoned.into_inner(),
        };
        if published == supervisor.observed_published_updates {
            return;
        }
        supervisor.observed_published_updates = published;
        supervisor.unchanged_service_ticks = 0;
    }

    pub(crate) fn record_published_update(&self) {
        self.state.published_updates.fetch_add(1, Ordering::Release);
    }

    #[cfg(test)]
    pub(crate) fn pause_dedicated_ticks_for_test(&self, paused: bool) {
        self.state.pause_ticks.store(paused, Ordering::Release);
        self.notify();
    }

    #[cfg(test)]
    pub(crate) fn fail_dedicated_ticks_for_test(&self, failing: bool) {
        self.state.fail_ticks.store(failing, Ordering::Release);
        self.notify();
    }

    pub(crate) fn ensure_started(&self, coordinator: &Arc<SpectrumCoordinator>) -> bool {
        if self.state.shutdown.load(Ordering::Acquire) {
            return false;
        }
        let mut slot = match self.handle.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        if slot.as_ref().is_some_and(|handle| !handle.is_finished()) {
            self.notify();
            return true;
        }
        if let Some(finished) = slot.take() {
            let _ = finished.join();
        }
        let state = Arc::clone(&self.state);
        let coordinator = Arc::downgrade(coordinator);
        match thread::Builder::new()
            .name("kirin-hypha-spectrum-exchange".to_string())
            .spawn(move || run_worker(state, coordinator))
        {
            Ok(handle) => {
                *slot = Some(handle);
                self.reset_supervisor();
                true
            }
            Err(_) => false,
        }
    }

    pub(crate) fn notify(&self) {
        self.state.wake.1.notify_all();
    }

    pub(crate) fn shutdown_and_join(&self) {
        self.state.shutdown.store(true, Ordering::Release);
        self.notify();
        let mut slot = match self.handle.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        if let Some(handle) = slot.take() {
            let _ = handle.join();
        }
    }

    fn update_endpoint(&self, endpoint: Endpoint) {
        let mut changed = false;
        let mut current = match self.state.endpoint.lock() {
            Ok(current) => current,
            Err(poisoned) => poisoned.into_inner(),
        };
        if *current != endpoint {
            *current = endpoint;
            changed = true;
        }
        drop(current);
        if changed {
            self.reset_supervisor();
            self.notify();
        }
    }

    fn reset_supervisor(&self) {
        let mut supervisor = match self.supervisor.lock() {
            Ok(supervisor) => supervisor,
            Err(poisoned) => poisoned.into_inner(),
        };
        supervisor.observed_published_updates =
            self.state.published_updates.load(Ordering::Acquire);
        supervisor.unchanged_service_ticks = 0;
    }
}

impl SpectrumCoordinator {
    /// Existing POST IO thread: publish the current exact endpoint, then leave active exchange to
    /// the isolated Spectrum worker. The first tick is synchronous so startup failure stays
    /// factual and the optional worker is never created for an unpaired/hidden POST.
    pub fn service_post_endpoint(
        self: &Arc<Self>,
        post_instance_id: &str,
        target: Option<SpectrumTarget>,
        owner_name: &str,
    ) {
        self.exchange_worker
            .update_post(post_instance_id, target.clone(), owner_name);
        if self.exchange_worker.is_started() {
            if !self.post_visible() || target.is_none() {
                self.exchange_worker.reset_supervisor();
                return;
            }
            if self.exchange_worker.needs_supervisor_tick() {
                let _ = self.post_tick_for_owner(post_instance_id, target, owner_name);
                self.exchange_worker.observe_supervisor_progress();
                self.exchange_worker.notify();
            }
            return;
        }
        if self.post_tick_for_owner(post_instance_id, target, owner_name) {
            let _ = self.exchange_worker.ensure_started(self);
        }
    }

    /// Existing PRE IO thread: discover a valid request at its unchanged 10 Hz cadence. Once
    /// discovered, one isolated worker owns all subsequent request reads and snapshot writes.
    pub fn service_pre_endpoint(self: &Arc<Self>, pre_instance_id: &str, instance_dir: &Path) {
        self.exchange_worker
            .update_pre(pre_instance_id, instance_dir);
        if self.exchange_worker.is_started() {
            if !self.has_valid_pre_request(pre_instance_id, instance_dir) {
                self.exchange_worker.reset_supervisor();
                return;
            }
            if self.exchange_worker.needs_supervisor_tick() {
                let _ = self.pre_tick(pre_instance_id, instance_dir);
                self.exchange_worker.observe_supervisor_progress();
                self.exchange_worker.notify();
            }
            return;
        }
        if self.pre_tick(pre_instance_id, instance_dir) {
            let _ = self.exchange_worker.ensure_started(self);
        }
    }
}

impl Drop for SpectrumExchangeWorker {
    fn drop(&mut self) {
        self.state.shutdown.store(true, Ordering::Release);
        self.state.wake.1.notify_all();
    }
}

fn run_worker(state: Arc<WorkerState>, coordinator: Weak<SpectrumCoordinator>) {
    let mut wait = ACTIVE_WAIT;
    while !state.shutdown.load(Ordering::Acquire) {
        let guard = match state.wake.0.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        let (_guard, _timeout) = match state.wake.1.wait_timeout(guard, wait) {
            Ok(result) => result,
            Err(poisoned) => poisoned.into_inner(),
        };
        if state.shutdown.load(Ordering::Acquire) {
            break;
        }
        let Some(coordinator) = coordinator.upgrade() else {
            break;
        };
        let endpoint = match state.endpoint.lock() {
            Ok(endpoint) => endpoint.clone(),
            Err(poisoned) => poisoned.into_inner().clone(),
        };
        #[cfg(test)]
        if state.pause_ticks.load(Ordering::Acquire) {
            wait = INACTIVE_WAIT;
            continue;
        }
        #[cfg(test)]
        let forced_failure = state.fail_ticks.load(Ordering::Acquire);
        #[cfg(not(test))]
        let forced_failure = false;
        let active = if forced_failure {
            false
        } else {
            match endpoint {
                Endpoint::None => false,
                Endpoint::Pre {
                    instance_id,
                    instance_dir,
                } => coordinator.pre_tick(&instance_id, &instance_dir),
                Endpoint::Post {
                    instance_id,
                    target,
                    owner_name,
                } => coordinator.post_tick_for_owner(&instance_id, target, &owner_name),
            }
        };
        wait = if !active {
            INACTIVE_WAIT
        } else if matches!(
            coordinator.active_analysis_mode(),
            crate::AnalysisViewMode::Absolute | crate::AnalysisViewMode::Attack
        ) {
            ABSOLUTE_ACTIVE_WAIT
        } else {
            ACTIVE_WAIT
        };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn active_period_is_the_exact_30hz_floor_without_affecting_io_cadence() {
        assert_eq!(SPECTRUM_PRESENTATION_HZ, 30);
        assert_eq!(ACTIVE_WAIT, Duration::from_nanos(33_333_333));
        assert!(ACTIVE_WAIT < Duration::from_millis(100));
        assert_eq!(ABSOLUTE_ACTIVE_WAIT, Duration::from_millis(100));
    }

    #[test]
    fn supervisor_rescues_before_the_request_lease_can_expire() {
        let worker = SpectrumExchangeWorker::new();
        for _ in 1..SUPERVISOR_STALL_SERVICE_TICKS {
            assert!(!worker.needs_supervisor_tick());
        }
        assert!(worker.needs_supervisor_tick());
        // A busy exchange session is retried on the next 10 Hz service call rather than waiting
        // through another full watchdog interval and letting the request lease expire.
        worker.observe_supervisor_progress();
        assert!(worker.needs_supervisor_tick());
        worker.record_published_update();
        worker.observe_supervisor_progress();
        assert!(!worker.needs_supervisor_tick());

        assert!(
            Duration::from_millis(
                u64::from(SUPERVISOR_STALL_SERVICE_TICKS.saturating_add(1)) * 100
            ) < Duration::from_millis(crate::spectrum_exchange::REQUEST_LEASE_MS as u64)
        );
    }
}
