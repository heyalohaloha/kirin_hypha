use super::{idle_stop_message, IdleRecordStop};
use std::time::{Duration, Instant};

#[test]
fn continuous_inactive_record_stops_at_the_inclusive_boundary() {
    let start = Instant::now();
    let timeout = Duration::from_secs(600);
    let mut state = IdleRecordStop::new(start, Some(timeout));
    assert_eq!(
        state.observe(start + timeout - Duration::from_nanos(1), true, false),
        None
    );
    assert_eq!(state.observe(start + timeout, true, false), Some(timeout));
}

#[test]
fn active_audio_and_watch_each_reset_the_continuous_idle_clock() {
    let start = Instant::now();
    let timeout = Duration::from_secs(10);
    let mut state = IdleRecordStop::new(start, Some(timeout));
    assert_eq!(
        state.observe(start + Duration::from_secs(9), true, true),
        None
    );
    assert_eq!(
        state.observe(start + Duration::from_secs(18), true, false),
        None
    );
    assert_eq!(
        state.observe(start + Duration::from_secs(19), true, false),
        Some(timeout)
    );

    assert_eq!(
        state.observe(start + Duration::from_secs(25), false, false),
        None
    );
    assert_eq!(
        state.observe(start + Duration::from_secs(34), true, false),
        None
    );
    assert_eq!(
        state.observe(start + Duration::from_secs(35), true, false),
        Some(timeout)
    );
}

#[test]
fn disabled_idle_stop_never_fires() {
    let start = Instant::now();
    let mut state = IdleRecordStop::new(start, None);
    assert_eq!(
        state.observe(start + Duration::from_secs(86_400), true, false),
        None
    );
}

#[test]
fn notification_claims_saved_take_only_when_a_writer_existed() {
    assert_eq!(
        idle_stop_message(Duration::from_secs(600), true),
        "Auto-stopped after 10 min idle. Take saved."
    );
    assert_eq!(
        idle_stop_message(Duration::from_secs(5), false),
        "Auto-stopped after 5 sec idle."
    );
}
