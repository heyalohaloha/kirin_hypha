use super::PollDeadline;
use std::time::{Duration, Instant};

#[test]
fn new_poll_is_due_immediately_and_boundary_is_inclusive() {
    let start = Instant::now();
    let interval = Duration::from_secs(1);
    let mut deadline = PollDeadline::new(start, interval);
    assert!(deadline.is_due(start));
    deadline.complete(start);
    assert!(!deadline.is_due(start + interval - Duration::from_nanos(1)));
    assert!(deadline.is_due(start + interval));
}

#[test]
fn next_deadline_is_measured_from_completion_not_the_previous_deadline() {
    let start = Instant::now();
    let interval = Duration::from_millis(100);
    let mut deadline = PollDeadline::new(start, interval);
    let completed = start + Duration::from_millis(40);
    deadline.complete(completed);
    assert!(!deadline.is_due(start + Duration::from_millis(100)));
    assert!(deadline.is_due(completed + interval));
}
