use super::*;
use std::sync::{atomic::AtomicBool, mpsc, Arc};
use std::time::Duration;

// A deadline detects a stopped test; only state and the completion message establish success.
const WATCHDOG: Duration = Duration::from_secs(600);

struct ParkedWorker {
    cancel: Arc<AtomicBool>,
    worker: Option<std::thread::JoinHandle<()>>,
    ready: mpsc::Receiver<()>,
    done: mpsc::Receiver<()>,
}

impl ParkedWorker {
    fn new(sm: Arc<RecordStateMachine>, stop: bool) -> Self {
        let cancel = Arc::new(AtomicBool::new(false));
        let worker_cancel = Arc::clone(&cancel);
        let (ready_tx, ready) = mpsc::channel();
        let (done_tx, done) = mpsc::channel();
        let worker = std::thread::spawn(move || {
            sm.register_measure_thread(std::thread::current());
            if ready_tx.send(()).is_err() {
                return;
            }
            loop {
                std::thread::park();
                if worker_cancel.load(Ordering::Acquire) {
                    return;
                }
                if sm.is_recording() != stop {
                    let _ = done_tx.send(());
                    return;
                }
            }
        });
        Self {
            cancel,
            worker: Some(worker),
            ready,
            done,
        }
    }

    fn shutdown(&mut self) -> std::thread::Result<()> {
        self.cancel.store(true, Ordering::Release);
        if let Some(worker) = self.worker.take() {
            worker.thread().unpark();
            worker.join()
        } else {
            Ok(())
        }
    }
}

impl Drop for ParkedWorker {
    fn drop(&mut self) {
        // Also runs when readiness, the edge, or the completion watchdog fails.
        let _ = self.shutdown();
    }
}

#[test]
fn a_record_edge_wakes_the_parked_measure_loop() {
    wakes_on_edge(false);
}

#[test]
fn a_record_stop_wakes_the_parked_measure_loop() {
    wakes_on_edge(true);
}

#[test]
fn a_parked_measure_worker_can_be_joined_without_a_record_edge() {
    let mut worker = ParkedWorker::new(Arc::new(RecordStateMachine::new()), false);
    worker.ready.recv_timeout(WATCHDOG).unwrap();
    worker.shutdown().unwrap();
    assert!(matches!(
        worker.done.try_recv(),
        Err(mpsc::TryRecvError::Disconnected)
    ));
}

fn wakes_on_edge(stop: bool) {
    let sm = Arc::new(RecordStateMachine::new());
    if stop {
        sm.try_enter_record(License::Os).unwrap();
    }
    let mut worker = ParkedWorker::new(Arc::clone(&sm), stop);
    worker.ready.recv_timeout(WATCHDOG).unwrap();
    if stop {
        sm.exit_record();
    } else {
        sm.try_enter_record(License::Os).unwrap();
    }
    worker
        .done
        .recv_timeout(WATCHDOG)
        .expect("the Record edge wakes the worker");
    assert_ne!(sm.is_recording(), stop);
    assert_eq!(sm.measure_wake_diagnostics()["unparks"], 1);
    worker.shutdown().unwrap();
}
