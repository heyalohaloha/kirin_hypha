use super::*;
use std::time::{Duration, Instant};

#[test]
fn changing_the_scope_while_running_rebuilds_the_analysis_with_the_longer_aperture() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    assert!(runtime.set_enabled(true));
    let mut position = 0_i64;
    let feed = |position: &mut i64| {
        let count = 2_048usize;
        let samples = vec![0.1_f32; count * 2];
        runtime.push_block_from_audio(&samples, 2, Some(*position));
        *position += count as i64;
        // A long aperture needs every block: wait for the worker instead of outrunning its ring.
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
    };
    let newest = || runtime.try_history().and_then(|h| h.newest().cloned());

    let deadline = Instant::now() + Duration::from_secs(10);
    while Instant::now() < deadline && newest().is_none() {
        feed(&mut position);
    }
    assert_eq!(newest().expect("FULL frame").aperture_samples, 4_096);

    assert!(runtime.set_spectrum_scope(SpectrumScope::Low));
    assert!(newest().is_none(), "the edge clears the old frames");
    let deadline = Instant::now() + Duration::from_secs(10);
    while Instant::now() < deadline && newest().is_none() {
        feed(&mut position);
    }
    let low = newest().expect("LOW frame after the edge");
    assert_eq!(low.scope, SpectrumScope::Low);
    assert_eq!(low.aperture_samples, 16_384);
    assert_eq!(low.fft_size, 32_768);
    assert_eq!(low.min_hz, 20.0);
    assert_eq!(low.max_hz, 300.0);
    runtime.shutdown_and_join();
}

#[test]
fn a_run_shorter_than_the_window_is_reported_until_it_fills() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    assert!(runtime.set_enabled(true));
    assert!(runtime.set_spectrum_scope(SpectrumScope::Low));
    let feed = |start: i64, frames: usize| {
        let samples = vec![0.1_f32; frames * 2];
        assert!(runtime.push_block_from_audio(&samples, 2, Some(start)));
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
    };
    feed(0, 8_192);
    assert!(runtime.spectrum_window_short());
    feed(8_192, 8_192);
    assert!(!runtime.spectrum_window_short());
    // The position jumps back, as at a loop point: the run starts again.
    feed(0, 8_192);
    assert!(runtime.spectrum_window_short());
    runtime.shutdown_and_join();
}

#[test]
fn asking_for_the_scope_already_selected_keeps_the_generation() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    let generation = runtime.selection().generation;
    assert!(runtime.set_spectrum_scope(SpectrumScope::Full));
    assert_eq!(runtime.selection().generation, generation);
    assert!(runtime.set_spectrum_scope(SpectrumScope::Low));
    assert_eq!(runtime.selection().generation, generation + 1);
    assert_eq!(
        AnalysisSelection::decode(runtime.selection().encode(), ChannelLayout::stereo())
            .map(|selection| selection.scope),
        Some(SpectrumScope::Low)
    );
    runtime.shutdown_and_join();
}
