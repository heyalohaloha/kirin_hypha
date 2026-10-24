//! Instance-local rendezvous for real measurement-worker tests. Absent from production builds.
use super::*;
use std::cell::RefCell;
use std::sync::mpsc::{self, Receiver, Sender};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Iteration {
    pub recording: bool,
    pub consumed: u64,
    pub parked_watch_consumed: u64,
    pub native_frames: u64,
    pub endpoint: Option<i64>,
    pub playback_pass: u64,
    pub queued: usize,
}

#[derive(Debug)]
pub(crate) enum Event {
    Iteration(Iteration),
    Idle {
        recording: bool,
    },
    Feed {
        samples: Vec<f64>,
        clock: MeterClockStart,
    },
}

#[derive(Debug)]
pub(crate) struct Probe {
    events: Sender<Event>,
    resume: Arc<Mutex<Receiver<()>>>,
}

pub(super) struct Controller {
    events: Receiver<Event>,
    resume: Option<Sender<()>>,
}

impl Controller {
    pub(super) fn new() -> (Self, Probe) {
        let (events_tx, events_rx) = mpsc::channel();
        let (resume_tx, resume_rx) = mpsc::channel();
        (
            Self {
                events: events_rx,
                resume: Some(resume_tx),
            },
            Probe {
                events: events_tx,
                resume: Arc::new(Mutex::new(resume_rx)),
            },
        )
    }

    pub(super) fn receive(&self) -> Event {
        // The worker exclusively owns the sender, so a worker exit also notifies this receiver.
        // Order and progress are determined by messages, with no scheduler/CPU deadline.
        self.events.recv().expect("worker checkpoint")
    }

    pub(super) fn release(&self) {
        self.resume.as_ref().unwrap().send(()).expect("live worker");
    }

    pub(super) fn disconnect(&mut self) {
        self.resume.take();
    }
}

thread_local! {
    static CURRENT: RefCell<Option<Probe>> = const { RefCell::new(None) };
}

pub(super) struct Installed;

pub(super) fn install(probe: Option<Probe>) -> Installed {
    CURRENT.with(|current| *current.borrow_mut() = probe);
    Installed
}

impl Drop for Installed {
    fn drop(&mut self) {
        CURRENT.with(|current| current.borrow_mut().take());
    }
}

fn checkpoint(event: impl FnOnce() -> Event) {
    CURRENT.with(|current| {
        let current = current.borrow();
        if let Some(probe) = current.as_ref() {
            if probe.events.send(event()).is_ok() {
                let _ = probe.resume.lock().unwrap().recv();
            }
        }
    });
}

pub(super) fn iteration(state: Iteration) {
    checkpoint(|| Event::Iteration(state));
}

pub(super) fn idle(recording: bool) {
    checkpoint(|| Event::Idle { recording });
}

pub(super) fn feed(samples: &[f64], clock: MeterClockStart) {
    checkpoint(|| Event::Feed {
        samples: samples.to_vec(),
        clock,
    });
}
