use super::ReservationLeaseRefresh;
use std::cell::Cell;
use std::time::{Duration, Instant};

fn interval() -> Duration {
    Duration::from_secs(crate::reservation::RESERVATION_LEASE_REFRESH_SECS)
}

#[test]
fn first_record_tick_refreshes_immediately_then_waits_one_lease_interval() {
    let start = Instant::now();
    let mut state = ReservationLeaseRefresh::new(start);
    let calls = Cell::new(0);

    state.service_with(start, true, || calls.set(calls.get() + 1), || start);
    state.service_with(
        start + interval() - Duration::from_nanos(1),
        true,
        || calls.set(calls.get() + 1),
        || panic!("throttled work has no completion time"),
    );
    assert_eq!(calls.get(), 1);

    state.service_with(
        start + interval(),
        true,
        || calls.set(calls.get() + 1),
        || start + interval(),
    );
    assert_eq!(calls.get(), 2, "interval boundary is inclusive");
}

#[test]
fn watch_tick_rearms_the_next_record_for_immediate_refresh() {
    let start = Instant::now();
    let mut state = ReservationLeaseRefresh::new(start);
    let calls = Cell::new(0);
    state.service_with(start, true, || calls.set(calls.get() + 1), || start);
    state.service_with(
        start + Duration::from_secs(1),
        false,
        || calls.set(calls.get() + 1),
        || panic!("Watch has no completion time"),
    );
    state.service_with(
        start + Duration::from_secs(1),
        true,
        || calls.set(calls.get() + 1),
        || start + Duration::from_secs(1),
    );
    assert_eq!(calls.get(), 2);
}

#[test]
fn watch_never_executes_a_refresh_callback() {
    let start = Instant::now();
    let mut state = ReservationLeaseRefresh::new(start);
    state.service_with(
        start,
        false,
        || panic!("Watch must not refresh a Record lease"),
        || panic!("Watch has no completion time"),
    );
}

#[test]
fn failed_or_empty_refresh_attempt_is_still_throttled() {
    let start = Instant::now();
    let mut state = ReservationLeaseRefresh::new(start);
    let attempts = Cell::new(0);
    state.service_with(start, true, || attempts.set(attempts.get() + 1), || start);
    state.service_with(
        start + Duration::from_secs(1),
        true,
        || attempts.set(attempts.get() + 1),
        || panic!("throttled work has no completion time"),
    );
    assert_eq!(attempts.get(), 1);
}

#[test]
fn slow_refresh_is_throttled_from_completion_instead_of_attempt_start() {
    let start = Instant::now();
    let completion = start + Duration::from_secs(2);
    let mut state = ReservationLeaseRefresh::new(start);
    let calls = Cell::new(0);

    state.service_with(start, true, || calls.set(calls.get() + 1), || completion);
    state.service_with(
        completion + interval() - Duration::from_nanos(1),
        true,
        || calls.set(calls.get() + 1),
        || panic!("throttled work has no completion time"),
    );
    assert_eq!(calls.get(), 1);

    state.service_with(
        completion + interval(),
        true,
        || calls.set(calls.get() + 1),
        || completion + interval(),
    );
    assert_eq!(calls.get(), 2);
}
