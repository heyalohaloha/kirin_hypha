//! Test-only callback completion: queue consumption alone precedes analysis publication.
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::{thread, time::Duration};

#[derive(Default)]
pub(super) struct WorkerReceipt {
    completed: AtomicU64,
    paused: AtomicBool,
    entered: AtomicBool,
    delay_millis: AtomicU64,
}

pub(super) struct Completion<'a>(&'a WorkerReceipt);
impl WorkerReceipt {
    pub fn begin(&self, shutdown: &AtomicBool) -> Completion<'_> {
        self.entered.store(true, Ordering::Release);
        while self.paused.load(Ordering::Acquire) && !shutdown.load(Ordering::Acquire) {
            thread::sleep(Duration::from_millis(1));
        }
        let delay = self.delay_millis.load(Ordering::Acquire);
        if delay != 0 {
            thread::sleep(Duration::from_millis(delay));
        }
        Completion(self)
    }
}
impl Drop for Completion<'_> {
    fn drop(&mut self) {
        // Every branch completes after its last possible publication (including rejection).
        self.0.completed.fetch_add(1, Ordering::Release);
    }
}

impl super::SpectrumRuntime {
    /// Sole producer, between callbacks. The target counts accepted ingress descriptors,
    /// independently of timeline wraps and clock/selection generations. A receipt is not a
    /// successful analysis: tests must still inspect the actual published frame and clock.
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
    }
    pub fn worker_entered_for_test(&self) -> bool {
        self.test_receipt.entered.load(Ordering::Acquire)
    }
}
