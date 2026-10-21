use super::*;

#[test]
fn lookup_is_bounded_by_frame_and_ring_retention() {
    let journal = TransportStartJournal::new();
    assert_eq!(journal.latest_at_or_before(u64::MAX), None);
    journal.note(10, -25);
    journal.note(20, 40);
    assert_eq!(journal.latest_at_or_before(9), None);
    assert_eq!(journal.latest_at_or_before(10), Some(-25));
    assert_eq!(journal.latest_at_or_before(19), Some(-25));
    assert_eq!(journal.latest_at_or_before(20), Some(40));
    let newest = 20 + AUDIO_EDGE_JOURNAL_CAPACITY as u64;
    for key in 21..=newest {
        journal.note(key, key as i64);
    }
    assert_eq!(journal.latest_at_or_before(20), None);
    assert_eq!(journal.latest_at_or_before(21), Some(21));
    assert_eq!(journal.latest_at_or_before(u64::MAX), Some(newest as i64));
}

#[test]
fn the_newest_note_wins_when_capture_does_not_advance() {
    let journal = ActiveEdgeJournal::new();
    journal.note(0, false);
    journal.note(0, true);
    assert_eq!(journal.latest_at_or_before(0), Some(true));
    journal.note(0, false);
    assert_eq!(journal.latest_at_or_before(0), Some(false));
    let positions = TransportStartJournal::new();
    positions.note(u64::MAX, i64::MIN);
    positions.note(u64::MAX, i64::MAX);
    assert_eq!(positions.latest_at_or_before(u64::MAX), Some(i64::MAX));
}

#[test]
fn an_in_progress_overwrite_cannot_return_an_older_edge() {
    let journal = ActiveEdgeJournal::new();
    journal.note(0, true);
    journal.note(1, false);
    // Sequence 1 is being overwritten: odd version 2 * 1 + 1 is not a completed slot.
    journal.slots[1].version.store(3, Ordering::SeqCst);
    assert_eq!(journal.latest_at_or_before(1), None);
}

#[test]
fn replacing_the_capture_ring_invalidates_both_edge_histories() {
    let tracker = crate::record_take::RecordTakeTracker::new();
    tracker.transport_start_journal.note(0, -100);
    tracker.active_edge_journal.note(0, true);
    assert_eq!(
        tracker.transport_start_journal.latest_at_or_before(0),
        Some(-100)
    );
    assert_eq!(
        tracker.active_edge_journal.latest_at_or_before(0),
        Some(true)
    );
    tracker.reset_capture_clock();
    assert_eq!(
        tracker
            .transport_start_journal
            .latest_at_or_before(u64::MAX),
        None
    );
    assert_eq!(
        tracker.active_edge_journal.latest_at_or_before(u64::MAX),
        None
    );
    tracker.transport_start_journal.note(5, 200);
    tracker.active_edge_journal.note(5, false);
    assert_eq!(tracker.transport_start_journal.latest_at_or_before(4), None);
    assert_eq!(tracker.active_edge_journal.latest_at_or_before(4), None);
    assert_eq!(
        tracker.transport_start_journal.latest_at_or_before(5),
        Some(200)
    );
    assert_eq!(
        tracker.active_edge_journal.latest_at_or_before(5),
        Some(false)
    );
}
