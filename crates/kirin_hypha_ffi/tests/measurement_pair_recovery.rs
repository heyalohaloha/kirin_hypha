//! Isolated storage and the actual Audio -> Measure -> IO -> C ABI path.
use kirin_hypha_ffi::*;
use kirin_measure::{
    AuxiliaryClockSamples, CaptureClockSource, PresentationLatencySamples, RecordTakeBlock,
};
use std::thread::sleep;
use std::time::{Duration, Instant};

fn block(engine: &KirinHyphaEngine, position: i64, frames: usize) {
    engine.set_signal_state(1);
    engine.note_record_block(RecordTakeBlock {
        generation: 0,
        recording: false,
        rendered: false,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: frames as u64,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    engine.note_capture_window_with_clocks(
        true,
        position,
        frames as u64,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples::default(),
        AuxiliaryClockSamples::default(),
        false,
    );
    let audio: Vec<_> = (position..position + frames as i64)
        .flat_map(|frame| {
            let sample = 0.1 * (std::f32::consts::TAU * 997.0 * frame as f32 / 48000.0).sin();
            [sample, sample]
        })
        .collect();
    engine.push_samples(&audio, 2);
}

fn feed(pre: &KirinHyphaEngine, post: &KirinHyphaEngine, from: i64, to: i64) {
    let mut position = from;
    while position < to {
        let frames = 512.min((to - position) as usize);
        block(pre, position, frames);
        block(post, position, frames);
        position += frames as i64;
        sleep(Duration::from_millis(11));
    }
}

fn current(
    pre: &KirinHyphaEngine,
    post: &KirinHyphaEngine,
    position: &mut i64,
) -> KirinObservatoryFrame {
    let end = Instant::now() + Duration::from_secs(3);
    loop {
        let mut frame = unsafe { std::mem::zeroed::<KirinObservatoryFrame>() };
        let request = KirinTimeSnapshotRequestV2 {
            version: 2,
            struct_size: std::mem::size_of::<KirinTimeSnapshotRequestV2>() as u32,
            packet_size: std::mem::size_of::<KirinTimeSnapshotV2>() as u32,
            entry_size: std::mem::size_of::<KirinTimeHistoryEntryV2>() as u32,
            duration_frames: 48000,
            main_target: u32::from(KIRIN_TARGET_DELTA),
            resolution: 0,
        };
        let mut time = KirinTimeSnapshotV2::default();
        let mut history = [KirinTimeHistoryEntryV2::default(); 32];
        let mut psr = [KirinTimeHistoryEntryV2::default(); 32];
        let status = unsafe {
            kirin_hypha_poll_time_snapshot_v2(
                post,
                &request,
                history.as_mut_ptr(),
                32,
                psr.as_mut_ptr(),
                32,
                &mut time,
            )
        };
        if unsafe { kirin_hypha_poll_observatory_frame(post as *const _ as *mut _, &mut frame) }
            && frame.delta_available != 0
            && frame.delta.lufs.is_finite()
            && status == KIRIN_SNAPSHOT_SUCCESS
            && time.main.current.state == KIRIN_TIME_CURRENT_LIVE
            && time.main.current.values[0].is_finite()
            && time.main.current.values[2].is_finite()
        {
            assert!(frame.delta.lufs.abs() < 0.01);
            assert!(frame.delta.true_peak.abs() < 0.01);
            assert!(time.main.current.values[0].abs() < 0.01);
            assert!(time.main.current.values[2].abs() < 0.01);
            return frame;
        }
        assert!(
            Instant::now() < end,
            "paired measurement did not resume: signal={}, reason={}, observed={}, time_status={}, time={:?}, delta={:?}",
            frame.signal_state,
            frame.comparison_reason,
            frame.meter.observed_frames,
            status,
            time,
            post.poll_delta()
        );
        block(pre, *position, 512);
        block(post, *position, 512);
        *position += 512;
        sleep(Duration::from_millis(11));
    }
}

#[test]
fn threaded_fractional_reset_and_bypass_resume_level_and_time_delta() {
    let sandbox = tempfile::tempdir().unwrap();
    std::env::set_var(kirin_measure::TEST_STORAGE_ROOT_ENV, sandbox.path());
    let pre = KirinHyphaEngine::new(48000, channel_abi::ChannelLayout::stereo());
    pre.set_identity(
        "pre-fixture".into(),
        "project-fixture".into(),
        "".into(),
        "mix".into(),
    );
    pre.enable_pre_writes();
    let post = KirinHyphaEngine::new(48000, channel_abi::ChannelLayout::stereo());
    post.set_identity(
        "post-fixture".into(),
        "project-fixture".into(),
        "".into(),
        "output".into(),
    );
    post.enable_post_writes();
    post.set_pair_target("mix".into());
    feed(&pre, &post, 0, 51200);
    let mut position = 51200;
    current(&pre, &post, &mut position);
    assert!(post.reset_meter_session());
    let end = position + 192000;
    feed(&pre, &post, position, end);
    position = end;
    current(&pre, &post, &mut position);
    // The IO thread must see both the bypass and its end; PCM is deliberately missing on POST.
    post.set_signal_state(2);
    sleep(Duration::from_millis(250));
    block(&pre, position, 733);
    position += 733;
    let end = position + 192000;
    feed(&pre, &post, position, end);
    position = end;
    current(&pre, &post, &mut position);
    let history = post
        .poll_meter_delta_history(kirin_measure::MeterHistoryResolution::Hz10, 32)
        .unwrap();
    assert!(history
        .last()
        .is_some_and(|point| point.lufs_m.mean.is_some()));
}
