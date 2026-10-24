//! Non-audio Record control wakes the currently registered measurement worker.
use super::*;

#[derive(Debug, Default)]
pub(super) struct MeasureWakeState {
    thread: std::sync::Mutex<Option<std::thread::Thread>>,
    registrations: AtomicU64,
    requests: AtomicU64,
    unparks: AtomicU64,
    idle_waits: AtomicU64,
    watch_park_returns: AtomicU64,
    record_sleep_returns: AtomicU64,
    #[cfg(test)]
    probe: std::sync::Mutex<Option<crate::measure_thread::worker_probe::Probe>>,
}

impl RecordStateMachine {
    #[cfg(test)]
    pub(crate) fn install_measure_worker_probe(
        &self,
        probe: crate::measure_thread::worker_probe::Probe,
    ) {
        *self.measure_waker.probe.lock().unwrap() = Some(probe);
    }

    #[cfg(test)]
    pub(crate) fn take_measure_worker_probe(
        &self,
    ) -> Option<crate::measure_thread::worker_probe::Probe> {
        self.measure_waker.probe.lock().unwrap().take()
    }

    pub(crate) fn register_measure_thread(&self, thread: std::thread::Thread) {
        *crate::sync_recovery::lock_recover(&self.measure_waker.thread, "Measure waker") =
            Some(thread);
        self.measure_waker
            .registrations
            .fetch_add(1, Ordering::Relaxed);
    }

    pub(super) fn wake_measure(&self) {
        self.measure_waker.requests.fetch_add(1, Ordering::Relaxed);
        if let Some(thread) =
            crate::sync_recovery::lock_recover(&self.measure_waker.thread, "Measure waker").as_ref()
        {
            self.measure_waker.unparks.fetch_add(1, Ordering::Relaxed);
            thread.unpark();
        }
    }

    pub(crate) fn note_measure_idle_wait(&self) {
        self.measure_waker
            .idle_waits
            .fetch_add(1, Ordering::Relaxed);
    }

    pub(crate) fn note_measure_idle_return(&self, recording: bool) {
        let counter = if recording {
            &self.measure_waker.record_sleep_returns
        } else {
            &self.measure_waker.watch_park_returns
        };
        counter.fetch_add(1, Ordering::Relaxed);
    }

    /// Non-RT diagnostic snapshot. A park return includes notifications, timeout and spurious wakes;
    /// `unparks` counts requests delivered to the registered worker, which may coalesce.
    #[doc(hidden)]
    pub fn measure_wake_diagnostics(&self) -> serde_json::Value {
        let state = &self.measure_waker;
        serde_json::json!({
            "registrations": state.registrations.load(Ordering::Relaxed),
            "wake_requests": state.requests.load(Ordering::Relaxed),
            "unparks": state.unparks.load(Ordering::Relaxed),
            "idle_waits": state.idle_waits.load(Ordering::Relaxed),
            "watch_park_returns": state.watch_park_returns.load(Ordering::Relaxed),
            "record_sleep_returns": state.record_sleep_returns.load(Ordering::Relaxed),
        })
    }
}

#[cfg(test)]
#[path = "record_measure_wake_tests.rs"]
mod tests;
