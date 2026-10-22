use super::*;
use crate::meter_session::MeterSessionState;

fn session() -> Arc<Mutex<MeterSession>> {
    Arc::new(Mutex::new(
        MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 1).unwrap(),
    ))
}
fn capture(tracker: &RecordTakeTracker, active: bool, position: i64, frames: u64) {
    tracker.note_meter_active(active);
    tracker.note_capture_window(true, position, frames);
}
fn plan(
    tracker: &RecordTakeTracker,
    before: u64,
    samples: usize,
) -> capture_plan::CaptureChunkPlan {
    capture_plan::capture_chunk_plan(tracker, before, samples, 2, 48_000)
}

#[test]
fn an_inactive_edge_feeds_the_active_tail_before_pausing() {
    let tracker = RecordTakeTracker::new();
    capture(&tracker, true, 0, 4_800);
    tracker.note_meter_active(false);
    let session = session();
    let input = vec![0.25; 9_600];
    let feed = MeterFeed {
        tracker: &tracker,
        session: Some(&session),
        publication: None,
        n_channels: 2,
        fallback_active: false,
    };
    assert!(feed.feed(&input, 0, plan(&tracker, 0, input.len()), 0));
    pause_meter_session(Some(&session), None);
    let measured = session.lock().unwrap().snapshot();
    assert_eq!(measured.active_frames, 4_800);
    assert_eq!(measured.state, MeterSessionState::Paused);
}

#[test]
fn record_mode_feeds_only_samples_pushed_while_active() {
    let tracker = RecordTakeTracker::new();
    capture(&tracker, true, 0, 4_800);
    capture(&tracker, false, 4_800, 4_800);
    capture(&tracker, true, 9_600, 4_800);
    let session = session();
    let input = vec![0.25; 28_800];
    let feed = MeterFeed {
        tracker: &tracker,
        session: Some(&session),
        publication: None,
        n_channels: 2,
        fallback_active: true,
    };
    feed.feed(&input, 0, plan(&tracker, 0, input.len()), 0);
    assert_eq!(session.lock().unwrap().snapshot().active_frames, 9_600);
}

#[test]
fn duplicate_frame_edges_use_the_latest_producer_state() {
    let tracker = RecordTakeTracker::new();
    tracker.note_meter_active(true);
    tracker.note_meter_active(false);
    capture(&tracker, true, 0, 4_800);
    let session = session();
    let feed = MeterFeed {
        tracker: &tracker,
        session: Some(&session),
        publication: None,
        n_channels: 2,
        fallback_active: false,
    };
    feed.feed(&vec![0.25; 9_600], 0, plan(&tracker, 0, 9_600), 0);
    assert_eq!(session.lock().unwrap().snapshot().active_frames, 4_800);
}

#[test]
fn replayed_record_prefix_never_enters_meter_session_twice() {
    let tracker = RecordTakeTracker::new();
    capture(&tracker, true, 0, 9_600);
    let session = session();
    let feed = MeterFeed {
        tracker: &tracker,
        session: Some(&session),
        publication: None,
        n_channels: 2,
        fallback_active: false,
    };
    feed.feed(&vec![0.25; 9_600], 0, plan(&tracker, 0, 9_600), 0);
    feed.feed(&vec![0.25; 19_200], 0, plan(&tracker, 0, 19_200), 9_600);
    assert_eq!(session.lock().unwrap().snapshot().active_frames, 9_600);
}

#[test]
fn unknown_activity_history_keeps_the_existing_fallback_without_inventing_frames() {
    for fallback in [false, true] {
        let tracker = RecordTakeTracker::new();
        let session = session();
        let feed = MeterFeed {
            tracker: &tracker,
            session: Some(&session),
            publication: None,
            n_channels: 2,
            fallback_active: fallback,
        };
        feed.feed(&[0.25; 20], 0, capture_plan::CaptureChunkPlan::default(), 0);
        assert_eq!(
            session.lock().unwrap().snapshot().active_frames,
            if fallback { 10 } else { 0 }
        );
    }
}

#[test]
fn empty_invalid_and_fully_replayed_input_does_not_advance_meter_session() {
    let tracker = RecordTakeTracker::new();
    capture(&tracker, true, 0, 4_800);
    let session = session();
    let feed = MeterFeed {
        tracker: &tracker,
        session: Some(&session),
        publication: None,
        n_channels: 2,
        fallback_active: true,
    };
    let plan = plan(&tracker, 0, 9_600);
    assert!(!feed.feed(&[], 0, plan, 0));
    assert!(!feed.feed(&[f64::NAN, 0.0], 0, plan, 0));
    assert!(!feed.feed(&[0.25, 0.25], 0, plan, 2));
    assert!(!feed.feed(&[0.25, 0.25], 0, plan, 1));
    assert_eq!(session.lock().unwrap().snapshot().active_frames, 0);
}
