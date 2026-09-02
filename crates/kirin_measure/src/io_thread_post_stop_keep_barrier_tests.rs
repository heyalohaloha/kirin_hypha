use super::{keep_broadcast_blocked_by_stop, remember_latest_started_at};

#[test]
fn stop_barrier_blocks_older_or_equal_keep_broadcast() {
    let stop = Some("2026-07-04T07:57:31Z");

    assert!(
        keep_broadcast_blocked_by_stop("2026-07-04T07:56:58Z", stop),
        "old all_keep_signal must not re-arm after all_stop_signal"
    );
    assert!(
        keep_broadcast_blocked_by_stop("2026-07-04T07:57:31Z", stop),
        "same-timestamp keep must lose to stop"
    );
}

#[test]
fn stop_barrier_allows_new_keep_after_stop() {
    assert!(
        !keep_broadcast_blocked_by_stop("2026-07-04T07:57:32Z", Some("2026-07-04T07:57:31Z")),
        "a deliberate new Keep after Stop must remain possible"
    );
    assert!(
        !keep_broadcast_blocked_by_stop("2026-07-04T07:57:32Z", None),
        "no stop barrier means normal keep handling"
    );
}

#[test]
fn remember_latest_started_at_keeps_newest_iso_timestamp() {
    let mut latest = None;

    remember_latest_started_at(&mut latest, "2026-07-04T07:57:20Z");
    remember_latest_started_at(&mut latest, "2026-07-04T07:57:19Z");
    remember_latest_started_at(&mut latest, "2026-07-04T07:57:31Z");

    assert_eq!(latest.as_deref(), Some("2026-07-04T07:57:31Z"));
}
