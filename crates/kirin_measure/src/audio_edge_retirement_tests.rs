use super::*;

#[test]
fn an_absent_edge_preserves_old_frames_and_splits_the_next_range() {
    let journal = TransportStartJournal::new();
    journal.note(0, i64::MIN);
    journal.note(512, i64::MAX);
    journal.note_absent(1_024);
    assert_eq!(journal.latest_at_or_before(511), Some(i64::MIN));
    assert_eq!(journal.latest_at_or_before(1_023), Some(i64::MAX));
    assert_eq!(journal.latest_at_or_before(1_024), None);
    let snapshot = journal.snapshot().unwrap();
    assert_eq!(snapshot.first_range(0, 2_048), (512, Some(i64::MIN)));
    assert_eq!(snapshot.first_range(512, 1_536), (512, Some(i64::MAX)));
    assert_eq!(snapshot.first_range(1_024, 1_024), (1_024, None));
    journal.note(1_024, 10);
    assert_eq!(journal.latest_at_or_before(1_024), Some(10));
    journal.note_absent(1_024);
    assert_eq!(journal.latest_at_or_before(1_024), None);
    assert_eq!(
        journal.snapshot().unwrap().first_range(512, 1_024),
        (512, Some(i64::MAX))
    );
}
