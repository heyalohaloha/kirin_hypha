//! Control-thread fixture receipts after ATTACK input processing and publication.
use super::*;
use std::time::Duration;

#[derive(Default)]
pub(super) struct Receipt {
    audio_end: AtomicU8,
    completed: AtomicU64,
    revision: AtomicU64,
    event: (Mutex<()>, Condvar),
}
pub(super) struct Completion<'a>(&'a Receipt);
impl Receipt {
    pub(super) fn set_audio_end(&self, ended: bool) {
        self.audio_end
            .store(if ended { 2 } else { 1 }, Ordering::Release);
    }
    pub(super) fn audio_end(&self) -> Option<bool> {
        match self.audio_end.load(Ordering::Acquire) {
            1 => Some(false),
            2 => Some(true),
            _ => None,
        }
    }

    pub(super) fn begin(&self) -> Completion<'_> {
        Completion(self)
    }
    pub(super) fn notify(&self) {
        let _guard = self.event.0.lock().unwrap();
        self.revision.fetch_add(1, Ordering::Release);
        self.event.1.notify_all();
    }
    fn wait(&self, mut ready: impl FnMut() -> bool) {
        let guard = self.event.0.lock().unwrap();
        let (guard, timeout) = self
            .event
            .1
            .wait_timeout_while(guard, Duration::from_secs(60), |_| !ready())
            .unwrap();
        drop(guard);
        assert!(
            !timeout.timed_out() || ready(),
            "ATTACK worker did not complete fixture work"
        );
    }
}
impl Drop for Completion<'_> {
    fn drop(&mut self) {
        self.0.completed.fetch_add(1, Ordering::Release);
        self.0.notify();
    }
}
impl AttackRuntime {
    pub(crate) fn set_audio_end_for_test(&self, ended: bool) {
        self.test_receipt.set_audio_end(ended);
    }

    pub(crate) fn wait_for_input_for_test(&self) {
        let target = self.pushed_blocks.load(Ordering::Acquire);
        self.test_receipt
            .wait(|| self.test_receipt.completed.load(Ordering::Acquire) >= target);
    }
    pub(crate) fn wait_for_cycle_for_test(&self) {
        let before = self.test_receipt.revision.load(Ordering::Acquire);
        self.test_receipt
            .wait(|| self.test_receipt.revision.load(Ordering::Acquire) != before);
    }
    pub(crate) fn wait_for_state_for_test(&self, ready: impl FnMut() -> bool) {
        self.test_receipt.wait(ready);
    }
}
