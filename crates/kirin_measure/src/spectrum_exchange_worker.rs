//! Dedicated, on-demand Spectrum filesystem exchange worker.
//!
//! The existing PRE/POST IO workers publish only endpoint changes. Once an exact pair becomes
//! active, this worker owns the optional 30 Hz request/snapshot exchange. A slow filesystem can
//! therefore delay Spectrum without delaying Watch, Record, pairing, or the Audio Thread.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex, Weak};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use crate::spectrum::SPECTRUM_PRESENTATION_HZ;
use crate::spectrum_exchange::{SpectrumCoordinator, SpectrumTarget};

const INACTIVE_WAIT: Duration = Duration::from_millis(250);
const ACTIVE_WAIT: Duration =
    Duration::from_nanos(1_000_000_000_u64 / SPECTRUM_PRESENTATION_HZ as u64);

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
    },
}

struct WorkerState {
    endpoint: Mutex<Endpoint>,
    shutdown: AtomicBool,
    wake: (Mutex<()>, Condvar),
}

pub(crate) struct SpectrumExchangeWorker {
    state: Arc<WorkerState>,
    handle: Mutex<Option<JoinHandle<()>>>,
}

impl SpectrumExchangeWorker {
    pub(crate) fn new() -> Self {
        Self {
            state: Arc::new(WorkerState {
                endpoint: Mutex::new(Endpoint::None),
                shutdown: AtomicBool::new(false),
                wake: (Mutex::new(()), Condvar::new()),
            }),
            handle: Mutex::new(None),
        }
    }

    pub(crate) fn update_pre(&self, instance_id: &str, instance_dir: &std::path::Path) {
        self.update_endpoint(Endpoint::Pre {
            instance_id: instance_id.to_string(),
            instance_dir: instance_dir.to_path_buf(),
        });
    }

    pub(crate) fn update_post(&self, instance_id: &str, target: Option<SpectrumTarget>) {
        self.update_endpoint(Endpoint::Post {
            instance_id: instance_id.to_string(),
            target,
        });
    }

    pub(crate) fn is_started(&self) -> bool {
        self.handle
            .lock()
            .ok()
            .and_then(|handle| handle.as_ref().map(|handle| !handle.is_finished()))
            .unwrap_or(false)
    }

    pub(crate) fn ensure_started(&self, coordinator: &Arc<SpectrumCoordinator>) -> bool {
        if self.state.shutdown.load(Ordering::Acquire) {
            return false;
        }
        let mut slot = match self.handle.lock() {
            Ok(slot) => slot,
            Err(_) => return false,
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
        if let Ok(mut slot) = self.handle.lock() {
            if let Some(handle) = slot.take() {
                let _ = handle.join();
            }
        }
    }

    fn update_endpoint(&self, endpoint: Endpoint) {
        let mut changed = false;
        if let Ok(mut current) = self.state.endpoint.lock() {
            if *current != endpoint {
                *current = endpoint;
                changed = true;
            }
        }
        if changed {
            self.notify();
        }
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
    ) {
        self.exchange_worker
            .update_post(post_instance_id, target.clone());
        if self.exchange_worker.is_started() {
            return;
        }
        if self.post_tick(post_instance_id, target) {
            let _ = self.exchange_worker.ensure_started(self);
        }
    }

    /// Existing PRE IO thread: discover a valid request at its unchanged 10 Hz cadence. Once
    /// discovered, one isolated worker owns all subsequent request reads and snapshot writes.
    pub fn service_pre_endpoint(self: &Arc<Self>, pre_instance_id: &str, instance_dir: &Path) {
        self.exchange_worker
            .update_pre(pre_instance_id, instance_dir);
        if self.exchange_worker.is_started() {
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
            Err(_) => break,
        };
        let Ok((_guard, _timeout)) = state.wake.1.wait_timeout(guard, wait) else {
            break;
        };
        if state.shutdown.load(Ordering::Acquire) {
            break;
        }
        let Some(coordinator) = coordinator.upgrade() else {
            break;
        };
        let endpoint = match state.endpoint.lock() {
            Ok(endpoint) => endpoint.clone(),
            Err(_) => break,
        };
        let active = match endpoint {
            Endpoint::None => false,
            Endpoint::Pre {
                instance_id,
                instance_dir,
            } => coordinator.pre_tick(&instance_id, &instance_dir),
            Endpoint::Post {
                instance_id,
                target,
            } => coordinator.post_tick(&instance_id, target),
        };
        wait = if active { ACTIVE_WAIT } else { INACTIVE_WAIT };
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
    }
}
