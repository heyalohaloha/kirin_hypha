use super::*;
use crate::channel_abi::ChannelLayout;

#[test]
fn an_unknown_restart_retires_only_its_new_capture_range() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    engine.note_transport_block(true, true, 1_000, 512, false);
    let tracker = &engine.record_take_tracker;
    tracker.note_capture_window(true, 1_000, 512);
    engine.note_transport_block(false, true, 1_512, 0, false);
    engine.note_transport_block(true, false, 0, 512, false);
    assert_eq!(
        tracker.transport_start_before(511),
        Some(1_000),
        "old chunks retain their authority"
    );
    assert_eq!(
        tracker.transport_start_before(512),
        None,
        "a restart without a position cannot reuse the last playback"
    );
    engine.note_transport_block(true, true, 1_512, 512, true);
    assert_eq!(
        tracker.transport_start_before(512),
        None,
        "a later valid position is not a start witness"
    );
    engine.note_transport_block(false, true, 2_024, 0, false);
    engine.note_transport_block(true, true, 2_024, 512, false);
    assert_eq!(tracker.transport_start_before(512), Some(2_024));
    engine.note_transport_block(true, false, 0, 512, false);
    assert_eq!(
        tracker.transport_start_before(512),
        None,
        "a position gap also retires unverifiable starts"
    );
}
