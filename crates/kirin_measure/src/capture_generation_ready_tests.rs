use super::wait_for_producers;
use crate::capture_contract::CAPTURE_PRODUCER_READY_LIMIT;
use std::cell::Cell;
use std::time::{Duration, Instant};

const STEP: Duration = Duration::from_millis(10);
const TIMEOUT: Duration = Duration::from_secs(3);

/// Runs the barrier on a simulated clock: `ready_at` lists when each producer becomes ready.
/// Returns whether it committed and how long it waited.
fn barrier(ready_at: &[Duration]) -> (bool, Duration) {
    barrier_of(ready_at, 2)
}

fn barrier_of(ready_at: &[Duration], total: usize) -> (bool, Duration) {
    let origin = Instant::now();
    let elapsed = Cell::new(Duration::ZERO);
    let ready = wait_for_producers(
        TIMEOUT,
        || ready_at.iter().filter(|at| **at <= elapsed.get()).count(),
        total,
        || origin + elapsed.get(),
        || elapsed.set(elapsed.get() + STEP),
    );
    (ready, elapsed.get())
}

#[test]
fn a_slow_host_that_keeps_bringing_producers_up_is_not_failed() {
    // PRE is ready at 0.8 s and POST at 3.1 s: past a fixed 3 s deadline, but progressing.
    let (ready, waited) = barrier(&[Duration::from_millis(800), Duration::from_millis(3_100)]);
    assert!(ready, "a progressing slow host arms");
    assert_eq!(waited, Duration::from_millis(3_100));
}

#[test]
fn a_producer_that_never_arrives_still_ends_the_wait() {
    // Nothing ever: the wait ends at the no-progress timeout.
    assert_eq!(barrier(&[]), (false, TIMEOUT));
    // PRE at 1 s, POST never: 3 s after the last progress.
    assert_eq!(
        barrier(&[Duration::from_secs(1)]),
        (false, Duration::from_secs(4))
    );
    // Steady progress (an All Keep roster arriving one by one) cannot stretch the wait past the
    // hard limit.
    let steady = [2_500, 5_000, 7_500, 12_000].map(Duration::from_millis);
    assert_eq!(
        barrier_of(&steady, 4),
        (false, CAPTURE_PRODUCER_READY_LIMIT)
    );
    assert_eq!(
        barrier_of(&steady[..3], 3),
        (true, Duration::from_millis(7_500))
    );
}

#[test]
fn a_zero_timeout_is_one_immediate_check() {
    let origin = Instant::now();
    let pauses = Cell::new(0);
    let ready = wait_for_producers(
        Duration::ZERO,
        || 1,
        2,
        || origin,
        || pauses.set(pauses.get() + 1),
    );
    assert!(!ready && pauses.get() == 0);
    assert!(wait_for_producers(
        Duration::ZERO,
        || 2,
        2,
        || origin,
        || {}
    ));
}
