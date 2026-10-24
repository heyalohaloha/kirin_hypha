use super::*;
use std::sync::{mpsc, Barrier, TryLockError};
use std::thread;

const TIMEOUT: Duration = Duration::from_secs(10);

struct TimedJoin<T> {
    done: mpsc::Receiver<()>,
    handle: thread::JoinHandle<T>,
}

impl<T: Send + 'static> TimedJoin<T> {
    fn spawn(task: impl FnOnce() -> T + Send + 'static) -> Self {
        let (done, receiver) = mpsc::channel();
        let handle = thread::spawn(move || {
            let result = task();
            let _ = done.send(());
            result
        });
        Self {
            done: receiver,
            handle,
        }
    }

    fn join(self) -> T {
        self.done
            .recv_timeout(TIMEOUT)
            .expect("control operation did not finish");
        self.handle
            .join()
            .unwrap_or_else(|panic| std::panic::resume_unwind(panic))
    }
}

fn feedback(engine: &KirinHyphaEngine, all: bool) -> KeepPreparationFeedback {
    KeepPreparationFeedback {
        control: Arc::clone(&engine.keep_control),
        notice: Arc::clone(&engine.keep_action_notice),
        phase: Arc::clone(&engine.keep_phase),
        phase_generation: Arc::clone(&engine.keep_phase_generation_started_at_ms),
        generation_started_at_ms: 7,
        all,
        after_generation_check: None,
    }
}

fn prepare(engine: &KirinHyphaEngine, generation: i64) {
    let _control = engine.keep_control.lock().unwrap();
    engine
        .keep_phase_generation_started_at_ms
        .store(generation, Ordering::Release);
    engine
        .keep_phase
        .store(KIRIN_KEEP_PHASE_PREPARING, Ordering::Release);
    *engine.keep_action_notice.write().unwrap() = None;
}

// Pause at the former check/write gap. If completion does not own the control lock, finish
// Stop before releasing completion; this deterministically reproduces the stale write.
// With the lock, completion must finish first and Stop then retires its result.
fn stop_after_check(all: bool, all_stop: bool, success: bool, restart: bool) {
    let engine = Arc::new(KirinHyphaEngine::new(48_000, ChannelLayout::stereo()));
    prepare(&engine, 7);
    let mut completion = feedback(&engine, all);
    let (checked, checked_rx) = mpsc::channel();
    let (resume, resume_rx) = mpsc::channel();
    let resume_rx = Mutex::new(resume_rx);
    completion.after_generation_check = Some(Arc::new(move || {
        checked.send(()).unwrap();
        resume_rx.lock().unwrap().recv_timeout(TIMEOUT).unwrap();
    }));
    let worker = TimedJoin::spawn(move || {
        completion.complete(if success {
            Ok(())
        } else {
            Err(CaptureGenerationError::Invalid)
        });
    });
    checked_rx
        .recv_timeout(TIMEOUT)
        .expect("generation check was not reached");
    let serialized = match engine.keep_control.try_lock() {
        Ok(_control) => false,
        Err(TryLockError::WouldBlock) => true,
        Err(TryLockError::Poisoned(_)) => panic!("control lock poisoned"),
    };
    let stopping_engine = Arc::clone(&engine);
    let (attempted, attempted_rx) = mpsc::channel();
    let stopper = TimedJoin::spawn(move || {
        attempted.send(()).unwrap();
        if all_stop {
            stopping_engine.stop_all();
        } else {
            stopping_engine.stop();
        }
        if restart {
            prepare(&stopping_engine, 8);
            *stopping_engine.keep_action_notice.write().unwrap() = Some("current action".into());
        }
    });
    attempted_rx.recv_timeout(TIMEOUT).unwrap();
    if serialized {
        resume.send(()).unwrap();
        worker.join();
        stopper.join();
    } else {
        stopper.join();
        resume.send(()).unwrap();
        worker.join();
    }
    assert_eq!(
        engine
            .keep_phase_generation_started_at_ms
            .load(Ordering::Acquire),
        if restart { 8 } else { 0 }
    );
    assert_eq!(
        engine.keep_phase.load(Ordering::Acquire),
        if restart {
            KIRIN_KEEP_PHASE_PREPARING
        } else {
            KIRIN_KEEP_PHASE_IDLE
        },
        "completion wrote a phase after Stop"
    );
    assert_eq!(
        engine.drain_keep_action_notice().as_deref(),
        if restart {
            Some("current action")
        } else {
            None
        },
        "completion published a notice after Stop"
    );
}

#[test]
fn stop_after_successful_generation_check_cannot_rearm_keep() {
    for all_stop in [false, true] {
        stop_after_check(false, all_stop, true, false);
    }
}

#[test]
fn stop_after_successful_generation_check_cannot_rearm_all_keep() {
    for all_stop in [false, true] {
        stop_after_check(true, all_stop, true, false);
    }
}

#[test]
fn stop_after_failed_generation_check_cannot_restore_keep_notice() {
    for all_stop in [false, true] {
        stop_after_check(false, all_stop, false, false);
    }
}

#[test]
fn stop_after_failed_generation_check_cannot_restore_all_keep_notice() {
    for all_stop in [false, true] {
        stop_after_check(true, all_stop, false, false);
    }
}

#[test]
fn stop_and_replacement_generation_retire_both_kinds_of_old_completion() {
    for all in [false, true] {
        for success in [false, true] {
            stop_after_check(all, false, success, true);
        }
    }
}

#[test]
fn completion_waiting_behind_stop_cannot_change_the_replacement_generation() {
    for all in [false, true] {
        let engine = Arc::new(KirinHyphaEngine::new(48_000, ChannelLayout::stereo()));
        prepare(&engine, 7);
        let completion = feedback(&engine, all);
        engine.stop();
        prepare(&engine, 8);
        *engine.keep_action_notice.write().unwrap() = Some("new action".into());
        TimedJoin::spawn(move || {
            completion.complete(Ok(()));
            completion.complete(Err(CaptureGenerationError::Invalid));
            completion.dispatch_failed(
                &CaptureGenerationError::Invalid,
                &completion.control.lock().unwrap(),
            );
        })
        .join();
        assert_eq!(
            engine.keep_phase.load(Ordering::Acquire),
            KIRIN_KEEP_PHASE_PREPARING
        );
        assert_eq!(
            engine.drain_keep_action_notice().as_deref(),
            Some("new action")
        );
    }
}

#[test]
fn simultaneous_stop_completion_and_repeated_controls_finish_without_deadlock() {
    for all in [false, true] {
        let engine = Arc::new(KirinHyphaEngine::new(48_000, ChannelLayout::stereo()));
        prepare(&engine, 7);
        let completion = feedback(&engine, all);
        let start = Arc::new(Barrier::new(4));
        let worker_start = Arc::clone(&start);
        let worker = TimedJoin::spawn(move || {
            worker_start.wait();
            for _ in 0..32 {
                completion.complete(Ok(()));
                completion.complete(Err(CaptureGenerationError::Invalid));
                completion.dispatch_failed(
                    &CaptureGenerationError::Invalid,
                    &completion.control.lock().unwrap(),
                );
            }
        });
        let keep_engine = Arc::clone(&engine);
        let keep_start = Arc::clone(&start);
        let keeps = TimedJoin::spawn(move || {
            keep_start.wait();
            for _ in 0..32 {
                keep_engine.keep();
                if all {
                    keep_engine.keep_all();
                } else {
                    keep_engine.keep();
                }
            }
        });
        let stop_engine = Arc::clone(&engine);
        let stop_start = Arc::clone(&start);
        let stops = TimedJoin::spawn(move || {
            stop_start.wait();
            for generation in 8..40 {
                stop_engine.stop();
                prepare(&stop_engine, generation);
                stop_engine.stop_all();
            }
        });
        start.wait();
        worker.join();
        keeps.join();
        stops.join();
        engine.stop();
        assert_eq!(engine.keep_phase(), KIRIN_KEEP_PHASE_IDLE);
        assert!(engine.drain_keep_action_notice().is_none());
    }
}

#[path = "keep_control_concurrency_tests.rs"]
mod control_tests;
