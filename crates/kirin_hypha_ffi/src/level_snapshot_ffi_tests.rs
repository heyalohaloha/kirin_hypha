use super::*;
use kirin_measure::{CaptureClockSource, MeterClockStart};
#[path = "level_poll_contention_tests.rs"]
mod contention;

#[test]
fn rejects_wrong_version_capacities_and_missing_buffers() {
    assert!(valid_request(
        600,
        600,
        1,
        std::ptr::dangling_mut(),
        std::ptr::dangling_mut()
    ));
    assert!(valid_request(
        0,
        0,
        0,
        std::ptr::null_mut(),
        std::ptr::null_mut()
    ));
    assert!(!valid_request(
        601,
        600,
        0,
        std::ptr::dangling_mut(),
        std::ptr::null_mut()
    ));
    assert!(!valid_request(
        10,
        11,
        0,
        std::ptr::dangling_mut(),
        std::ptr::null_mut()
    ));
    assert!(!valid_request(
        10,
        10,
        2,
        std::ptr::dangling_mut(),
        std::ptr::dangling_mut()
    ));
    assert!(!valid_request(
        10,
        10,
        1,
        std::ptr::null_mut(),
        std::ptr::dangling_mut()
    ));
    assert_eq!(std::mem::size_of::<KirinLevelSnapshot>(), 2_160);
    assert_eq!(std::mem::offset_of!(KirinLevelSnapshot, frame), 16);
    assert_eq!(std::mem::offset_of!(KirinLevelSnapshot, chain), 2_120);
}

#[test]
fn packet_history_and_meter_have_one_cutoff_and_failed_calls_leave_outputs_untouched() {
    let mut engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    let samples = vec![0.1_f64; 4_800 * 2];
    assert!(engine
        .meter_session
        .as_ref()
        .unwrap()
        .lock()
        .unwrap()
        .push_active_at(
            &samples,
            MeterClockStart {
                position_samples: Some(0),
                epoch: Some(1),
                source: CaptureClockSource::ProjectTimeline,
                ..Default::default()
            },
        ));
    {
        let session = engine.meter_session.as_ref().unwrap().lock().unwrap();
        let snapshot = session.snapshot();
        let points = session.recent_history_decimated(MeterHistoryResolution::Hz10, 8, 8);
        assert!(!points.is_empty());
        assert!(points.iter().all(|point| {
            point.measurement_epoch == snapshot.measurement_epoch
                && point.generation == snapshot.generation
                && point.last_observed_frames <= snapshot.observed_frames
        }));
        assert!(build_observatory_frame(&engine, &snapshot, engine.signal_state_abi()).is_some());
    }
    let mut history: [KirinMeterHistoryEntry; 8] =
        std::array::from_fn(|_| unsafe { std::mem::zeroed() });
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    packet.version = 99;
    assert!(!unsafe {
        kirin_hypha_poll_level_snapshot(
            &mut engine,
            0,
            8,
            history.as_mut_ptr(),
            8,
            0,
            std::ptr::null_mut(),
            0,
            &mut packet,
        )
    });
    assert_eq!(packet.version, 99);
    let mut success = false;
    for _ in 0..500 {
        success = unsafe {
            kirin_hypha_poll_level_snapshot(
                &mut engine,
                KIRIN_LEVEL_SNAPSHOT_VERSION,
                8,
                history.as_mut_ptr(),
                8,
                0,
                std::ptr::null_mut(),
                0,
                &mut packet,
            )
        };
        if success {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
    assert!(success);
    assert_eq!(packet.version, KIRIN_LEVEL_SNAPSHOT_VERSION);
    assert_eq!(
        packet.frame.version,
        abi_contract::KIRIN_OBSERVATORY_FRAME_VERSION
    );
    assert!(packet.history_count > 0 && packet.history_count <= 8);
    assert_eq!(packet.chain_updated, 0);
    for point in &history[..packet.history_count as usize] {
        assert_eq!(
            point.measurement_epoch,
            packet.frame.meter.measurement_epoch
        );
        assert_eq!(point.generation, packet.frame.meter.generation);
        assert!(point.last_observed_frames <= packet.frame.meter.observed_frames);
    }
}

#[test]
fn post_packet_reports_unpaired_chain_without_inventing_points() {
    let mut engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    let mut chain: KirinChainPoint = unsafe { std::mem::zeroed() };
    let mut success = false;
    for _ in 0..500 {
        success = unsafe {
            kirin_hypha_poll_level_snapshot(
                &mut engine,
                KIRIN_LEVEL_SNAPSHOT_VERSION,
                0,
                std::ptr::null_mut(),
                0,
                u64::MAX,
                &mut chain,
                1,
                &mut packet,
            )
        };
        if success {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
    assert!(success);
    assert_eq!(packet.history_count, 0);
    assert_eq!(packet.chain_updated, 1);
    assert_eq!(packet.chain.version, KIRIN_CHAIN_VERSION_LATEST);
    assert_eq!(packet.chain.count, 0);
    assert_ne!(
        packet.chain.status,
        kirin_measure::meter_delta_history::chain::Status::Active as u8
    );
}

#[test]
fn absent_role_keeps_absolute_meter_and_reports_no_chain() {
    let mut engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    let mut chain: KirinChainPoint = unsafe { std::mem::zeroed() };
    let mut success = false;
    for _ in 0..500 {
        success = unsafe {
            kirin_hypha_poll_level_snapshot(
                &mut engine,
                KIRIN_LEVEL_SNAPSHOT_VERSION,
                0,
                std::ptr::null_mut(),
                0,
                0,
                &mut chain,
                1,
                &mut packet,
            )
        };
        if success {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
    assert!(success);
    assert_eq!(
        packet.frame.version,
        abi_contract::KIRIN_OBSERVATORY_FRAME_VERSION
    );
    assert_eq!(packet.chain_updated, 1);
    assert_eq!(packet.chain.revision, UNCONFIGURED_ROLE_REVISION);
    assert_eq!(packet.chain.count, 0);
    assert_eq!(packet.chain.status, 0);
    success = false;
    for _ in 0..500 {
        success = unsafe {
            kirin_hypha_poll_level_snapshot(
                &mut engine,
                KIRIN_LEVEL_SNAPSHOT_VERSION,
                0,
                std::ptr::null_mut(),
                0,
                UNCONFIGURED_ROLE_REVISION,
                &mut chain,
                1,
                &mut packet,
            )
        };
        if success {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
    assert!(success);
    assert_eq!(packet.chain_updated, 0);
}

#[test]
fn stable_audition_keeps_absolute_meter_without_republishing_suppression() {
    let mut engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    engine
        .audition
        .active_handle()
        .store(true, Ordering::Release);
    let mut packet: KirinLevelSnapshot = unsafe { std::mem::zeroed() };
    let mut chain: KirinChainPoint = unsafe { std::mem::zeroed() };
    for (known, expected_update) in [(0, 1), (u64::MAX, 0)] {
        let mut success = false;
        for _ in 0..500 {
            success = unsafe {
                kirin_hypha_poll_level_snapshot(
                    &mut engine,
                    KIRIN_LEVEL_SNAPSHOT_VERSION,
                    0,
                    std::ptr::null_mut(),
                    0,
                    known,
                    &mut chain,
                    1,
                    &mut packet,
                )
            };
            if success {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(1));
        }
        assert!(success);
        assert_eq!(packet.chain_updated, expected_update);
        if expected_update == 1 {
            assert_eq!(packet.chain.status, KIRIN_CHAIN_SUPPRESSED);
            assert_eq!(packet.chain.count, 0);
        }
        assert_eq!(
            packet.frame.version,
            abi_contract::KIRIN_OBSERVATORY_FRAME_VERSION
        );
    }
}
