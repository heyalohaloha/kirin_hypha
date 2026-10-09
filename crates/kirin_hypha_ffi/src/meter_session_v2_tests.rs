use super::*;
use kirin_measure::channel_layout::ChannelLayout;
#[test]
fn live_mutex_contention_is_busy_and_does_not_touch_sized_packet() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    let mut bytes = vec![0xa5_u8; std::mem::size_of::<KirinMeterSessionV2>() + 16];
    let offset = bytes
        .as_ptr()
        .align_offset(std::mem::align_of::<KirinMeterSessionV2>());
    let output = unsafe { bytes.as_mut_ptr().add(offset).cast() };
    let before = bytes.clone();
    let guard = engine.meter_session.as_ref().unwrap().lock().unwrap();
    assert_eq!(
        unsafe {
            kirin_hypha_poll_meter_session_v2(
                &engine,
                2,
                std::mem::size_of::<KirinMeterSessionV2>() as u32,
                output,
            )
        },
        KIRIN_SNAPSHOT_BUSY
    );
    assert_eq!(before, bytes);
    drop(guard);
    assert_eq!(
        unsafe { kirin_hypha_poll_meter_session_v2(&engine, 3, 4, output) },
        KIRIN_SNAPSHOT_UNSUPPORTED
    );
    assert_eq!(before, bytes);
    assert_eq!(
        unsafe { kirin_hypha_poll_meter_session_v2(&engine, 2, 4, output) },
        KIRIN_SNAPSHOT_INVALID_REQUEST
    );
    assert_eq!(before, bytes);
    let packet = engine.meter_session_v2().unwrap();
    assert_eq!(packet.processed_frames, 0);
    assert_eq!(packet.pending_frames, 0);
    assert_eq!(packet.summary_status, 0);
    assert_eq!(packet.version, 2);
}
#[test]
fn coverage_layout_matches_native_consumer() {
    use std::mem::{offset_of, size_of};
    assert_eq!(size_of::<KirinMeterSessionV2>(), 1_872);
    assert_eq!(offset_of!(KirinMeterSessionV2, session), 8);
    assert_eq!(offset_of!(KirinMeterSessionV2, processed_frames), 1_848);
    assert_eq!(offset_of!(KirinMeterSessionV2, pending_frames), 1_856);
    assert_eq!(offset_of!(KirinMeterSessionV2, summary_status), 1_864);
}
