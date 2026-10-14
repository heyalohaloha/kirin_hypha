//! Test-only worker completion, after publication. Never called by the audio producer.
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Condvar, Mutex};
use std::{thread, time::Duration};

#[derive(Default)]
pub(super) struct WorkerReceipt {
    completed: AtomicU64,
    paused: AtomicBool,
    entered: AtomicBool,
    delay_millis: AtomicU64,
    event: (Mutex<()>, Condvar),
}

pub(super) struct Completion<'a>(&'a WorkerReceipt);
impl WorkerReceipt {
    pub fn begin(&self, shutdown: &AtomicBool) -> Completion<'_> {
        let guard = self.event.0.lock().unwrap_or_else(|e| e.into_inner());
        self.entered.store(true, Ordering::Release);
        self.event.1.notify_all();
        let guard = self
            .event
            .1
            .wait_while(guard, |_| {
                self.paused.load(Ordering::Acquire) && !shutdown.load(Ordering::Acquire)
            })
            .unwrap_or_else(|e| e.into_inner());
        drop(guard);
        let delay = self.delay_millis.load(Ordering::Acquire);
        if delay != 0 {
            thread::sleep(Duration::from_millis(delay));
        } // deliberate fault injection
        Completion(self)
    }
    pub fn complete(&self, count: u64) {
        let _guard = self.event.0.lock().unwrap_or_else(|e| e.into_inner());
        self.completed.fetch_add(count, Ordering::Release);
        self.event.1.notify_all();
    }
    pub fn notify(&self) {
        let _guard = self.event.0.lock().unwrap_or_else(|e| e.into_inner());
        self.event.1.notify_all();
    }
    fn wait(&self, timeout: Duration, ready: impl Fn() -> bool) -> bool {
        let guard = self.event.0.lock().unwrap_or_else(|e| e.into_inner());
        let _guard = self
            .event
            .1
            .wait_timeout_while(guard, timeout, |_| !ready())
            .unwrap_or_else(|e| e.into_inner());
        ready()
    }
}
impl Drop for Completion<'_> {
    fn drop(&mut self) {
        self.0.complete(1);
    }
}

impl super::SpectrumRuntime {
    /// Fixture/control thread only, after push returns. The RT ingress never signals a worker.
    pub fn wake_worker_for_test(&self) {
        if let Some(worker) = self
            .worker
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .as_ref()
        {
            worker.thread().unpark();
        }
        // The disabled-worker wait uses this separate existing event.
        let _guard = self.wake.0.lock().unwrap_or_else(|e| e.into_inner());
        self.wake.1.notify_all();
    }
    /// Accepted descriptors, including rejected/disabled work, must complete after publication.
    /// The caller owns the sole producer and does not push again until this returns.
    pub fn wait_for_input_for_test(&self, timeout: Duration) -> bool {
        let target = self.pushed_blocks.load(Ordering::Acquire);
        self.wake_worker_for_test();
        self.test_receipt.wait(timeout, || {
            self.completed_blocks_for_test() >= target || self.shutdown.load(Ordering::Acquire)
        }) && self.completed_blocks_for_test() >= target
    }
    pub fn completed_blocks_for_test(&self) -> u64 {
        self.test_receipt.completed.load(Ordering::Acquire)
    }
    pub fn delay_worker_for_test(&self, millis: u64) {
        self.test_receipt
            .delay_millis
            .store(millis, Ordering::Release);
    }
    pub fn pause_worker_for_test(&self, paused: bool) {
        self.test_receipt.entered.store(false, Ordering::Release);
        self.test_receipt.paused.store(paused, Ordering::Release);
        self.test_receipt.notify();
        self.wake_worker_for_test();
    }
    pub fn worker_entered_for_test(&self) -> bool {
        self.test_receipt.entered.load(Ordering::Acquire)
    }
    pub fn wait_worker_entered_for_test(&self, timeout: Duration) -> bool {
        self.wake_worker_for_test();
        self.test_receipt
            .wait(timeout, || self.worker_entered_for_test())
    }
}
