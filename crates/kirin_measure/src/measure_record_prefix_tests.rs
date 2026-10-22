use super::*;
use crate::record_take::RecordTakeBlock;

fn callback(
    tracker: &RecordTakeTracker,
    recording: bool,
    offline: bool,
    position: i64,
    frames: u64,
) {
    tracker.note_block(RecordTakeBlock {
        generation: 1,
        recording,
        rendered: recording,
        playing: true,
        offline,
        position_valid: true,
        position_samples: position,
        num_frames: frames,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    tracker.note_capture_window(true, position, frames);
}

#[test]
fn a_realtime_watch_callback_at_the_record_edge_never_replays_watch_history() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, false, false, 0, 128);
    let watch_epoch = tracker.clock_point_for_captured_frame(1).unwrap().epoch;
    let mut history = RawPreRollHistory::new(512, 2);
    assert!(history.append_f64(Some(watch_epoch), 0, &[0.25; 256]));
    // The control edge chooses the Record lane after this callback observed Watch. The next
    // callback publishes the first selected Record epoch before Measure resolves the reader.
    callback(&tracker, false, false, 128, 16);
    callback(&tracker, true, false, 144, 16);
    assert_ne!(tracker.selected_capture_epoch(1), Some(watch_epoch));
    let binding = bind(&mut history, &tracker, 1, 128).unwrap();
    assert_eq!(binding.native_origin, 128, "only admitted Record audio");
    assert_eq!(binding.position, Some(128));
    assert!(binding.prefix.is_none(), "ordinary Watch is no take prefix");
}

#[test]
fn an_offline_watch_prefix_waits_for_its_producer_promotion() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, false, true, 500, 16);
    let epoch = tracker.clock_point_for_captured_frame(1).unwrap().epoch;
    let mut history = RawPreRollHistory::new(128, 2);
    assert!(history.append_f64(Some(epoch), 0, &[0.5; 32]));
    callback(&tracker, false, true, 516, 16);
    assert!(
        bind(&mut history, &tracker, 1, 16).is_none(),
        "the unpromoted Watch span cannot establish the Record prefix yet"
    );
    callback(&tracker, true, true, 532, 16);
    let binding = bind(&mut history, &tracker, 1, 16).unwrap();
    assert_eq!(binding.native_origin, 0);
    assert_eq!(binding.position, Some(500));
    assert_eq!(binding.prefix.unwrap().samples.len(), 32);
    assert_eq!(tracker.selected_capture_epoch(1), Some(binding.epoch));
}

#[test]
fn a_superseded_offline_watch_candidate_never_holds_the_record_reader() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, false, true, 500, 16);
    let epoch = tracker.clock_point_for_captured_frame(1).unwrap().epoch;
    let mut history = RawPreRollHistory::new(128, 2);
    assert!(history.append_f64(Some(epoch), 0, &[0.5; 32]));
    callback(&tracker, false, true, 516, 16);
    // A discontinuity makes this a different take, so the old Watch candidate will never be
    // promoted. Its pending history is no longer authority for the current Record.
    callback(&tracker, true, true, 900, 16);
    assert_ne!(tracker.selected_capture_epoch(1), Some(epoch));
    let binding = bind(&mut history, &tracker, 1, 16)
        .expect("a producer-selected different take ends the old candidate's wait");
    assert_eq!(binding.native_origin, 16);
    assert!(binding.prefix.is_none());
}

#[test]
fn an_expired_offline_candidate_without_a_selected_take_releases_the_reader() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, false, true, 500, 16);
    let epoch = tracker.clock_point_for_captured_frame(1).unwrap().epoch;
    let mut history = RawPreRollHistory::new(128, 2);
    assert!(history.append_f64(Some(epoch), 0, &[0.5; 32]));
    callback(&tracker, false, true, 516, 16);
    assert!(bind(&mut history, &tracker, 1, 16).is_none());

    // The offline pass ends before any armed audio can promote it. No later audio or selected
    // take is needed to release the already admitted Record input.
    tracker.note_block(RecordTakeBlock {
        generation: 1,
        recording: true,
        rendered: false,
        playing: false,
        offline: false,
        position_valid: true,
        position_samples: 532,
        num_frames: 0,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    assert_eq!(tracker.selected_capture_epoch(1), None);
    let binding = bind(&mut history, &tracker, 1, 16)
        .expect("the producer expired the candidate even though no take was selected");
    assert_eq!(binding.native_origin, 16);
    assert_eq!(binding.position, Some(516));
    assert!(binding.prefix.is_none());
    assert!(history.take_positioned_prefix(0, 16, epoch).is_none());
}

#[test]
fn a_replaced_offline_candidate_releases_the_reader_before_any_take_is_selected() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, false, true, 500, 16);
    let epoch = tracker.clock_point_for_captured_frame(1).unwrap().epoch;
    let mut history = RawPreRollHistory::new(128, 2);
    assert!(history.append_f64(Some(epoch), 0, &[0.5; 32]));
    callback(&tracker, false, true, 516, 16);
    assert!(bind(&mut history, &tracker, 1, 16).is_none());

    // Watch itself can start a different offline pass without having selected any Record take.
    callback(&tracker, false, true, 900, 16);
    assert_eq!(tracker.selected_capture_epoch(1), None);
    let binding = bind(&mut history, &tracker, 1, 16)
        .expect("a different live candidate cannot keep the old epoch pending");
    assert_eq!(binding.native_origin, 16);
    assert_eq!(binding.position, Some(516));
    assert!(binding.prefix.is_none());
    assert!(history.take_positioned_prefix(0, 16, epoch).is_none());
}

#[test]
fn a_later_record_generation_cannot_keep_the_earlier_reader_pending() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, false, true, 500, 16);
    let epoch = tracker.clock_point_for_captured_frame(1).unwrap().epoch;
    let mut history = RawPreRollHistory::new(128, 2);
    assert!(history.append_f64(Some(epoch), 0, &[0.5; 32]));
    callback(&tracker, false, true, 516, 16);
    assert!(bind(&mut history, &tracker, 1, 16).is_none());

    // An armed empty callback establishes the later generation without selecting a take yet.
    tracker.note_block(RecordTakeBlock {
        generation: 2,
        recording: true,
        rendered: false,
        playing: true,
        offline: true,
        position_valid: true,
        position_samples: 532,
        num_frames: 0,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    assert_eq!(tracker.selected_capture_epoch(1), None);
    assert_eq!(tracker.selected_capture_epoch(2), None);
    let binding = bind(&mut history, &tracker, 1, 16)
        .expect("the producer has moved beyond this reader's generation");
    assert_eq!(binding.native_origin, 16);
    assert!(binding.prefix.is_none());

    let mut later_history = RawPreRollHistory::new(128, 2);
    assert!(later_history.append_f64(Some(epoch), 0, &[0.5; 32]));
    assert!(bind(&mut later_history, &tracker, 2, 16).is_none());
}

#[test]
fn a_promotion_between_span_read_and_candidate_check_preserves_the_prefix() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, false, true, 500, 16);
    let epoch = tracker.clock_point_for_captured_frame(1).unwrap().epoch;
    let mut history = RawPreRollHistory::new(128, 2);
    assert!(history.append_f64(Some(epoch), 0, &[0.5; 32]));
    callback(&tracker, false, true, 516, 16);
    let watch_span = tracker.capture_span_for_frame(17).unwrap();
    assert_eq!(watch_span.generation, 0);

    // Producer promotion occurs after Measure's first span read, before its liveness check.
    callback(&tracker, true, true, 532, 16);
    let binding = bind_span(&mut history, &tracker, 1, 16, watch_span)
        .or_else(|| bind(&mut history, &tracker, 1, 16))
        .expect("the published promotion must preserve its prefix across a reader retry");
    assert_eq!(binding.native_origin, 0);
    assert_eq!(binding.position, Some(500));
    assert_eq!(binding.epoch, epoch);
    assert_eq!(binding.prefix.unwrap().samples.len(), 32);
    assert_eq!(tracker.selected_capture_epoch(1), Some(epoch));
}
