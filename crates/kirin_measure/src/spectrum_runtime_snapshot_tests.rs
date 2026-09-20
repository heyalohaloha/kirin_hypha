//! Deterministic publication interleavings through the real ingress, assembler, and worker.

use super::*;
use std::time::{Duration, Instant};

fn feed_real_worker(runtime: &SpectrumRuntime, start: i64, amplitude: f32) {
    for offset in (0..10_240).step_by(256) {
        let position = start + offset as i64;
        let samples = (0..256)
            .flat_map(|index| {
                let phase = std::f32::consts::TAU * 1_000.0 * (position + index) as f32 / 48_000.0;
                let sample = amplitude * phase.sin();
                [sample, sample]
            })
            .collect::<Vec<_>>();
        assert!(runtime.push_block_from_audio(&samples, 2, Some(position)));
        thread::sleep(Duration::from_millis(2));
    }
}

fn wait_for_published_end(runtime: &SpectrumRuntime, minimum: i64) -> SpectrumFrame {
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        let frame = runtime.history.lock().unwrap().value.newest().cloned();
        if let Some(frame) = frame.filter(|frame| frame.presentation_end_samples >= minimum) {
            return frame;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("actual Spectrum worker did not publish in time");
}

fn runtime_with_old_stream() -> (Arc<SpectrumRuntime>, SpectrumFrame) {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(runtime.set_enabled(true));
    feed_real_worker(&runtime, 0, 0.5);
    let old = wait_for_published_end(&runtime, 9_600);
    assert_eq!(
        runtime.history.lock().unwrap().identity.stream_generation,
        1
    );
    assert!(!runtime.push_block_from_audio(&[], 2, None));
    assert_eq!(runtime.stream_generation.load(Ordering::Acquire), 2);
    assert!(runtime.try_history().is_none());
    (runtime, old)
}

#[test]
fn copied_history_cannot_borrow_a_new_stream_stamp() {
    let (runtime, old) = runtime_with_old_stream();
    let returned = runtime.try_history_after_clone_for_test(|| {
        feed_real_worker(&runtime, 20_000, 0.125);
        let current = wait_for_published_end(&runtime, 28_800);
        assert_ne!(
            current.presentation_end_samples,
            old.presentation_end_samples
        );
        assert_eq!(
            runtime.history.lock().unwrap().identity.stream_generation,
            2
        );
    });
    runtime.shutdown_and_join();
    assert!(
        returned.is_none(),
        "stream 1 history was admitted with stream 2 identity"
    );
}

#[test]
fn same_endpoint_cannot_reuse_values_from_the_previous_stream() {
    let (runtime, old) = runtime_with_old_stream();
    let returned = runtime.try_history_after_clone_for_test(|| {
        feed_real_worker(&runtime, 0, 0.125);
        let current = wait_for_published_end(&runtime, 9_600);
        let peak =
            |frame: &SpectrumFrame| frame.dbfs.iter().copied().fold(f32::NEG_INFINITY, f32::max);
        assert_eq!(
            old.presentation_end_samples,
            current.presentation_end_samples
        );
        assert!((peak(&old) - peak(&current) - 12.0412).abs() < 0.2);
        assert_eq!(
            runtime.history.lock().unwrap().identity.stream_generation,
            2
        );
    });
    runtime.shutdown_and_join();
    assert!(
        returned.is_none(),
        "old values survived at the restarted sample endpoint"
    );
}

#[test]
fn a_selection_edge_during_clone_rejects_the_old_snapshot() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(runtime.set_enabled(true));
    feed_real_worker(&runtime, 0, 0.5);
    let old = wait_for_published_end(&runtime, 9_600);
    let old_stream = runtime.stream_generation.load(Ordering::Acquire);
    let returned = runtime.try_history_after_clone_for_test(|| {
        assert!(runtime.set_channel_mode(SpectrumChannelMode::Mid));
        assert_eq!(
            runtime.stream_generation.load(Ordering::Acquire),
            old_stream
        );
        assert!(runtime.try_history().unwrap().newest().is_none());
    });
    runtime.shutdown_and_join();
    assert_eq!(returned.and_then(|history| history.newest().cloned()), None);
    assert_eq!(old.channel_mode, SpectrumChannelMode::Lr);
}

#[test]
fn all_four_optional_snapshot_apis_reject_a_selection_edge() {
    let spectrum = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(spectrum
        .try_history_after_clone_for_test(|| {
            assert!(spectrum.set_analysis_mode(AnalysisViewMode::Perceptual));
        })
        .is_none());

    let perceptual = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(perceptual
        .try_perceptual_history_after_clone_for_test(|| {
            assert!(perceptual.set_analysis_mode(AnalysisViewMode::Perceptual));
        })
        .is_none());

    let absolute = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(absolute
        .try_absolute_history_after_clone_for_test(|| {
            assert!(absolute.set_analysis_mode(AnalysisViewMode::Absolute));
        })
        .is_none());

    let mid_side = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(mid_side
        .try_mid_side_after_clone_for_test(|| {
            assert!(mid_side.set_mid_side_enabled(true));
        })
        .is_none());
}
