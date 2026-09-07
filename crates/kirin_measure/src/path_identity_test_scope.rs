//! Opt-in sink ownership for tests asserting exact notifications. Production keeps the global
//! role sink; an unrelated test/worker cannot insert into or drain a fixture's private queue.
use super::PathEvent;
use std::cell::RefCell;
use std::marker::PhantomData;
use std::rc::Rc;
use std::sync::{Arc, Mutex};

type Sink = Arc<Mutex<Vec<PathEvent>>>;
thread_local! {
    static ACTIVE: RefCell<Option<Sink>> = const { RefCell::new(None) };
}

pub(super) fn active_sink() -> Option<Sink> {
    ACTIVE.with(|active| active.borrow().clone())
}

pub(super) struct Capture {
    previous: Option<Sink>,
    _same_thread: PhantomData<Rc<()>>,
}

pub(super) fn capture() -> Capture {
    Capture {
        previous: ACTIVE.with(|active| active.replace(Some(Arc::new(Mutex::new(Vec::new()))))),
        _same_thread: PhantomData,
    }
}

impl Drop for Capture {
    fn drop(&mut self) {
        ACTIVE.with(|active| active.replace(self.previous.take()));
    }
}

#[test]
fn concurrent_captures_cannot_drain_each_others_events() {
    use super::{drain_path_events, surface_path_event};
    let _local = capture();
    surface_path_event("local");
    let other = std::thread::spawn(|| {
        let _other = capture();
        surface_path_event("other");
        drain_path_events()
    })
    .join()
    .unwrap();
    assert_eq!(other, ["other"]);
    assert_eq!(drain_path_events(), ["local"]);
}

#[test]
fn nested_capture_restores_the_outer_queue_even_after_unwind() {
    use super::{drain_path_events, surface_path_event};
    let _outer = capture();
    surface_path_event("outer");
    let result = std::panic::catch_unwind(|| {
        let _inner = capture();
        surface_path_event("inner");
        panic!("fixture interrupted");
    });
    assert!(result.is_err());
    assert_eq!(drain_path_events(), ["outer"]);
}
