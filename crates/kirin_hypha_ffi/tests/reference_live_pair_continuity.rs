//! Isolated storage and the actual Audio -> Measure -> IO -> C ABI path: POST's PSR and CREST faces
//! keep their PRE reading through ordinary publication delay, and never mix two moments.
use kirin_hypha_ffi::*;
use kirin_measure::{
    AuxiliaryClockSamples, CaptureClockSource, PresentationLatencySamples, RecordTakeBlock,
};
use std::ffi::c_void;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread::sleep;
use std::time::{Duration, Instant};

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Frame {
    version: u32,
    struct_size: u32,
    sample_rate: u32,
    channels: u32,
    measurement_epoch: u64,
    generation: u64,
    observed_frames: u64,
    own_crest_db: f64,
    own_psr_db: f64,
    pre_crest_db: f64,
    pre_psr_db: f64,
    valid_mask: u32,
    reserved: u32,
}

extern "C" {
    fn kirin_hypha_poll_reference_live_metrics(handle: *mut c_void, out: *mut Frame) -> bool;
}

const FRAMES: i64 = 512;

fn block(engine: &KirinHyphaEngine, position: i64, gain: f32) {
    engine.set_signal_state(1);
    engine.note_record_block(RecordTakeBlock {
        generation: 0,
        recording: false,
        rendered: false,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: FRAMES as u64,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    engine.note_capture_window_with_clocks(
        true,
        position,
        FRAMES as u64,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples::default(),
        AuxiliaryClockSamples::default(),
        false,
    );
    // Noise under a slow level change: PSR and CREST move by several dB between boundaries.
    let audio: Vec<_> = (position..position + FRAMES)
        .flat_map(|frame| {
            let mut noise = (frame as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15);
            noise ^= noise >> 29;
            noise = noise.wrapping_mul(0xBF58_476D_1CE4_E5B9);
            noise ^= noise >> 32;
            let level = 0.25 + 0.2 * (std::f32::consts::TAU * 0.7 * frame as f32 / 48000.0).sin();
            let sample = gain * level * ((noise & 0xFFFF) as f32 / 32768.0 - 1.0);
            [sample, sample]
        })
        .collect();
    engine.push_samples(&audio, 2);
}

#[test]
fn pre_psr_and_crest_stay_through_publication_delay_and_share_one_moment() {
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
    let mut post = Box::new(KirinHyphaEngine::new(
        48000,
        channel_abi::ChannelLayout::stereo(),
    ));
    post.set_identity(
        "post-fixture".into(),
        "project-fixture".into(),
        "".into(),
        "output".into(),
    );
    post.enable_post_writes();
    post.set_pair_target("mix".into());

    let handle = std::ptr::from_mut(post.as_mut()) as usize;
    let finished = Arc::new(AtomicBool::new(false));
    let display = {
        let finished = Arc::clone(&finished);
        std::thread::spawn(move || {
            // One second of display polls. A loaded machine may need several tries; a reading that
            // only appears when a poll happens to meet the exact boundary never fills one.
            const WINDOW: usize = 30;
            let deadline = Instant::now() + Duration::from_secs(40);
            let (mut recent, mut best) = (std::collections::VecDeque::new(), 0);
            while Instant::now() < deadline && best < WINDOW - 1 {
                sleep(Duration::from_millis(33));
                let mut frame = Frame::default();
                let shown = unsafe {
                    kirin_hypha_poll_reference_live_metrics(handle as *mut c_void, &mut frame)
                } && frame.valid_mask == 15;
                if shown {
                    // Both sides carry the same signal at different gains.
                    assert!((frame.pre_psr_db - frame.own_psr_db).abs() < 0.01);
                    assert!((frame.pre_crest_db - frame.own_crest_db).abs() < 0.01);
                }
                recent.push_back(shown);
                if recent.len() > WINDOW {
                    recent.pop_front();
                }
                best = best.max(recent.iter().filter(|shown| **shown).count());
            }
            finished.store(true, Ordering::Release);
            best
        })
    };
    let (start, mut position) = (Instant::now(), 0);
    while !finished.load(Ordering::Acquire) {
        block(&pre, position, 0.5);
        block(&post, position, 0.25);
        position += FRAMES;
        let due = start + Duration::from_secs_f64(position as f64 / 48000.0);
        sleep(due.saturating_duration_since(Instant::now()));
    }
    let best = display.join().unwrap();
    assert!(best >= 29, "PRE was shown in {best} of 30 polls at best");
}
