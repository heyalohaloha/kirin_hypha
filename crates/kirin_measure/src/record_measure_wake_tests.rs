use super::*;
use std::sync::{mpsc, Arc};
use std::time::{Duration, Instant};

#[test]
fn a_record_edge_wakes_the_parked_measure_loop() {
    wakes_on_edge(false);
}

#[test]
fn a_record_stop_wakes_the_parked_measure_loop() {
    wakes_on_edge(true);
}

fn wakes_on_edge(stop: bool) {
    let sm = Arc::new(RecordStateMachine::new());
    if stop {
        sm.try_enter_record(License::Os).unwrap();
    }
    let (ready_tx, ready_rx) = mpsc::channel();
    let worker_sm = Arc::clone(&sm);
    let worker = std::thread::spawn(move || {
        worker_sm.register_measure_thread(std::thread::current());
        let started = Instant::now();
        ready_tx.send(()).unwrap();
        let timeout = Duration::from_secs(10);
        loop {
            std::thread::park_timeout(timeout.saturating_sub(started.elapsed()));
            if worker_sm.is_recording() != stop || started.elapsed() >= timeout {
                break;
            }
        }
        worker_sm.is_recording() != stop && started.elapsed() < timeout
    });
    ready_rx.recv_timeout(Duration::from_secs(60)).unwrap();
    if stop {
        sm.exit_record();
    } else {
        sm.try_enter_record(License::Os).unwrap();
    }
    assert!(
        worker.join().unwrap(),
        "the edge wakes the worker before its idle timeout"
    );
}
