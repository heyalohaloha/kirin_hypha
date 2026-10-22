use super::*;
use crate::channel_abi::ChannelLayout;
#[test]
fn only_host_transport_edges_publish_a_start() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    let start = || engine.record_take_tracker.transport_start_before(0);
    engine.note_transport_block(false, true, 100, 512, true);
    assert_eq!(start(), None);
    engine.note_transport_block(true, true, 100, 512, false);
    assert_eq!(start(), Some(100));
    engine.note_transport_block(true, true, 612, 512, true);
    assert_eq!(
        start(),
        Some(100),
        "signal availability is not a transport start"
    );
    engine.note_transport_block(true, true, 0, 512, false);
    assert_eq!(
        start(),
        Some(0),
        "loop wrap uses the newest edge even without captured audio"
    );
    engine.record_take_tracker.reset_capture_clock();
    assert_eq!(
        start(),
        None,
        "replacement capture ring cannot reuse the retired edge"
    );
}
#[test]
fn an_unknown_position_does_not_grant_start_authority() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    engine.note_transport_block(true, false, 0, 512, false);
    assert_eq!(engine.record_take_tracker.transport_start_before(0), None);
    engine.note_transport_block(true, true, 512, 512, true);
    assert_eq!(engine.record_take_tracker.transport_start_before(0), None);
}
