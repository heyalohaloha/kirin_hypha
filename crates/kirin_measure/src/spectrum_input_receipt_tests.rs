use super::{Ordering, SpectrumRuntime};
use crate::channel_layout::ChannelLayout;
use std::time::Duration;

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
        assert!(
            runtime.wait_for_input_for_test(Duration::from_secs(5)),
            "worker receipt watchdog"
        );
    }
    assert_eq!(runtime.stats().pushed_blocks, 32);
    assert_eq!(runtime.stats().dropped_blocks, 0);
    assert!(runtime.stats().analyzed_frames > 0);
    runtime.shutdown_and_join();
}

#[test]
fn completion_waits_for_publication_and_remembers_an_early_notification() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::mono());
    runtime.pause_worker_for_test(true);
    assert!(runtime.set_enabled(true));
    assert!(runtime.push_block_from_audio(&vec![0.125; 6400], 1, Some(0)));
    assert!(runtime.wait_worker_entered_for_test(Duration::from_secs(5)));
    assert_eq!(runtime.completed_blocks_for_test(), 0);
    assert!(!runtime.wait_for_input_for_test(Duration::ZERO));
    assert_eq!(runtime.stats().analyzed_frames, 0);
    runtime.pause_worker_for_test(false);
    assert!(runtime.wait_for_input_for_test(Duration::from_secs(5)));
    assert!(runtime.try_history().unwrap().newest().is_some());
    assert!(runtime.wait_for_input_for_test(Duration::ZERO));
    assert_eq!(runtime.completed_blocks_for_test(), 1);
    runtime.shutdown_and_join();
}

#[test]
fn disabled_or_stale_input_completes_without_claiming_an_analysis_result() {
    for disable in [false, true] {
        let runtime = SpectrumRuntime::new(48_000, ChannelLayout::mono());
        runtime.pause_worker_for_test(true);
        assert!(runtime.set_enabled(true));
        assert!(runtime.push_block_from_audio(&vec![0.125; 6400], 1, Some(0)));
        assert!(runtime.wait_worker_entered_for_test(Duration::from_secs(5)));
        assert!(runtime.set_enabled(false));
        if !disable {
            assert!(runtime.set_enabled(true));
        }
        runtime.pause_worker_for_test(false);
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(5)));
        assert_eq!(runtime.completed_blocks_for_test(), 1);
        assert_eq!(runtime.stats().analyzed_frames, 0);
        assert!(unsafe { runtime.input_drained_for_test() });
        runtime.shutdown_and_join();
    }
}

#[test]
fn shutdown_releases_a_paused_worker_without_an_unbounded_join() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::mono());
    runtime.pause_worker_for_test(true);
    assert!(runtime.set_enabled(true));
    assert!(runtime.push_block_from_audio(&[0.125; 256], 1, Some(0)));
    assert!(runtime.wait_worker_entered_for_test(Duration::from_secs(5)));
    let (sent, received) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        runtime.shutdown_and_join();
        sent.send(()).unwrap();
    });
    received
        .recv_timeout(Duration::from_secs(5))
        .expect("shutdown notification watchdog");
    worker.join().unwrap();
}
