use super::wait_for_producers;
use std::cell::Cell;
use std::time::{Duration, Instant};

#[path = "capture_generation_post_preparation_tests.rs"]
mod post_preparation;
#[path = "capture_generation_preparation_tests.rs"]
mod preparation;

const STEP: Duration = Duration::from_millis(10);
const TIMEOUT: Duration = Duration::from_secs(3);

#[test]
fn a_stalled_wait_reports_only_the_no_progress_deadline() {
    let origin = Instant::now();
    let elapsed = Cell::new(Duration::ZERO);
    let report = super::ready::wait_for_producers_report(
        TIMEOUT,
        || (0, 0),
        2,
        || origin + elapsed.get(),
        || elapsed.set(elapsed.get() + Duration::from_secs(1)),
    );
    assert!(!report.ready && report.no_progress_expired && !report.safety_window_expired);
    assert_eq!(report.elapsed_ms, Some(3_000));
    assert_eq!(report.since_progress_ms, Some(3_000));
    assert_eq!(report.no_progress_timeout_ms, 3_000);
    assert_eq!(report.safety_window_ms, 10_000);
}

#[test]
fn a_long_scheduling_gap_reports_both_expired_deadlines() {
    let origin = Instant::now();
    let elapsed = Cell::new(Duration::ZERO);
    let report = super::ready::wait_for_producers_report(
        TIMEOUT,
        || (0, 0),
        2,
        || origin + elapsed.get(),
        || elapsed.set(Duration::from_secs(11)),
    );
    assert!(!report.ready && report.no_progress_expired && report.safety_window_expired);
    assert_eq!(report.elapsed_ms, Some(11_000));
}

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
        || {
            let ready = ready_at.iter().filter(|at| **at <= elapsed.get()).count();
            (ready, ready)
        },
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
fn a_progressing_roster_can_finish_beyond_the_initial_safety_window() {
    // Three PRE/POST pairs, with one writer already ready when the barrier starts.
    let arrivals = [0, 2_500, 5_000, 7_500, 9_900, 12_000].map(Duration::from_millis);
    assert_eq!(barrier_of(&arrivals, 6), (true, Duration::from_secs(12)));
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
    // A roster with a missing producer still ends three seconds after its last milestone.
    let steady = [2_500, 5_000, 7_500, 12_000].map(Duration::from_millis);
    assert_eq!(
        barrier_of(&steady, 4),
        (false, Duration::from_millis(10_500))
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
        || (1, 1),
        2,
        || origin,
        || pauses.set(pauses.get() + 1),
    );
    assert!(!ready && pauses.get() == 0);
    assert!(wait_for_producers(
        Duration::ZERO,
        || (2, 2),
        2,
        || origin,
        || {}
    ));
}

/// A slow PC: PRE acknowledges at 2.5 s (its Record and Measure are up), its writer is ready at
/// 5 s and POST's at 7 s. No producer is ready within the first 3 s, yet every step is progress.
#[test]
fn a_pre_that_acknowledged_is_progress_before_its_writer_is_ready() {
    let origin = Instant::now();
    let elapsed = Cell::new(Duration::ZERO);
    let at = |ms: u64| elapsed.get() >= Duration::from_millis(ms);
    let ready = wait_for_producers(
        TIMEOUT,
        || {
            let ready = usize::from(at(5_000)) + usize::from(at(7_000));
            (ready, ready + usize::from(at(2_500)))
        },
        2,
        || origin + elapsed.get(),
        || elapsed.set(elapsed.get() + STEP),
    );
    assert!(ready, "a slow PC that keeps progressing arms");
    assert_eq!(elapsed.get(), Duration::from_millis(7_000));
}

#[test]
fn exact_entry_and_ack_are_progress_but_never_producer_readiness() {
    use super::*;
    use crate::capture_generation::CaptureGeneration;
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    );
    let member = &generation.members[0];
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        "project",
        "post",
        "pre".into(),
        "daw".into(),
        None,
        &generation,
    )
    .unwrap();
    assert_eq!(acknowledged_member_count(temp.path(), &generation), 0);
    crate::record_entry_lock::claim_record_entry(
        temp.path(),
        "project",
        &member.record_session_id,
        Role::Pre,
        "pre",
    )
    .unwrap();
    assert_eq!(acknowledged_member_count(temp.path(), &generation), 1);
    let mut signal = read_signal(temp.path(), "project", "post").unwrap();
    signal.status = crate::record_signal::SignalStatus::Acknowledged;
    crate::record_signal::write_signal(temp.path(), "project", "post", &signal).unwrap();
    assert_eq!(acknowledged_member_count(temp.path(), &generation), 2);
    assert!(!generation_producers_ready(temp.path(), &generation));
    // An old ACK cannot contribute progress to another immutable capture generation.
    signal.capture_generation_id = "other".into();
    crate::record_signal::write_signal(temp.path(), "project", "post", &signal).unwrap();
    assert_eq!(acknowledged_member_count(temp.path(), &generation), 1);
}
