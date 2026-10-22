use super::*;
use crate::channel_abi::ChannelLayout;

#[test]
fn producer_activity_keeps_the_pushed_tail_despite_a_worker_freshness_override() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    engine.set_signal_state(1);
    assert_eq!(engine.record_take_tracker.meter_active_at(0), Some(true));
    engine.record_take_tracker.note_capture_window(true, 0, 512);
    store_signal_state(&engine.signal_state, SignalState::Inactive);
    assert_eq!(engine.record_take_tracker.meter_active_at(511), Some(true));
    engine.set_signal_state(0);
    assert_eq!(engine.record_take_tracker.meter_active_at(511), Some(true));
    assert_eq!(engine.record_take_tracker.meter_active_at(512), Some(false));
}

#[test]
fn adopting_a_replacement_ring_rebinds_activity_at_the_new_frame_zero() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    engine.set_signal_state(1);
    engine.record_take_tracker.note_capture_window(true, 0, 512);
    let (producer, _consumer) = rtrb::RingBuffer::new(64);
    engine.producer_handoff.publish_from_watchdog(producer);
    engine.push_samples(&[], 2);
    assert_eq!(engine.record_take_tracker.captured_frames_total(), 0);
    assert_eq!(engine.record_take_tracker.meter_active_at(0), Some(true));
}
