use super::{Ordering, SpectrumRuntime};
use crate::channel_layout::ChannelLayout;
use std::thread;
use std::time::{Duration, Instant};

#[test]
fn receipt_requires_both_descriptor_and_samples() {
    for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
        let runtime = SpectrumRuntime::new(48_000, layout);
        // Keep the worker absent to exercise each queue edge without a scheduler race.
        runtime.enabled.store(true, Ordering::Release);
        let channels = layout.channel_count();
        let samples = vec![0.125; 256 * channels];
        assert!(unsafe { runtime.input_drained_for_test() });
        assert!(runtime.push_block_from_audio(&samples, channels, Some(0)));
        assert!(!unsafe { runtime.input_drained_for_test() });
        let mut guard = runtime.consumers.lock().unwrap();
        let queues = guard.as_mut().unwrap();
        queues.blocks.pop().unwrap();
        assert!(!unsafe { runtime.input_drained_for_test() });
        for _ in 1..samples.len() {
            queues.samples.pop().unwrap();
        }
        assert!(!unsafe { runtime.input_drained_for_test() });
        queues.samples.pop().unwrap();
        assert!(unsafe { runtime.input_drained_for_test() });
        assert_eq!(runtime.stats().pushed_blocks, 1);
        assert_eq!(runtime.stats().analyzed_frames, 0); // receipt is not an analysis success
    }
}

#[test]
fn receipt_paces_the_real_worker_without_dropping_audio() {
    let runtime = SpectrumRuntime::new(96_000, ChannelLayout::stereo());
    assert!(runtime.set_enabled(true));
    let samples = vec![0.125; 2048 * 2];
    for i in 0..32 {
        assert!(runtime.push_block_from_audio(&samples, 2, Some(i * 2048)));
        let deadline = Instant::now() + Duration::from_secs(5);
        while !unsafe { runtime.input_drained_for_test() } {
            assert!(Instant::now() < deadline, "worker receipt watchdog");
            thread::sleep(Duration::from_millis(1));
        }
    }
    assert_eq!(runtime.stats().pushed_blocks, 32);
    assert_eq!(runtime.stats().dropped_blocks, 0);
    assert!(runtime.stats().analyzed_frames > 0);
    runtime.shutdown_and_join();
}
