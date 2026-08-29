use std::thread;
use std::time::{Duration, Instant};

use super::*;

fn feed(runtime: &SpectrumRuntime, sample_rate: u32, frames: usize, block: usize) {
    feed_with_delay(runtime, sample_rate, frames, block, 1);
}

fn feed_with_delay(
    runtime: &SpectrumRuntime,
    sample_rate: u32,
    frames: usize,
    block: usize,
    delay_ms: u64,
) {
    let mut position = 0_i64;
    while position < frames as i64 {
        let count = block.min(frames - position as usize);
        let mut samples = Vec::with_capacity(count * 2);
        for index in 0..count {
            let phase = std::f32::consts::TAU * 1_000.0 * (position as usize + index) as f32
                / sample_rate as f32;
            let sample = phase.sin();
            samples.extend_from_slice(&[sample, -sample]);
        }
        assert!(runtime.push_block_from_audio(&samples, 2, Some(position)));
        position += count as i64;
        thread::sleep(Duration::from_millis(delay_ms));
    }
}

fn wait_for_frame_at_or_after(runtime: &SpectrumRuntime, expected_end: i64) -> SpectrumFrame {
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline {
        if let Some(frame) = runtime
            .try_history()
            .and_then(|history| history.newest().cloned())
            .filter(|frame| frame.presentation_end_samples >= expected_end)
        {
            return frame;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("Spectrum worker did not publish a frame");
}

fn wait_for_perceptual_frame_at_or_after(
    runtime: &SpectrumRuntime,
    expected_end: i64,
) -> PerceptualFrame {
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if let Some(frame) = runtime
            .try_perceptual_history()
            .and_then(|history| history.newest().cloned())
            .filter(|frame| frame.presentation_end_samples >= expected_end)
        {
            return frame;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("Perceptual worker did not publish a frame");
}

fn wait_for_absolute_frame_at_or_after(
    runtime: &SpectrumRuntime,
    expected_end: i64,
) -> crate::AbsoluteFrame {
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if let Some(frame) = runtime
            .try_absolute_history()
            .and_then(|history| history.newest().copied())
            .filter(|frame| frame.presentation_end_samples >= expected_end)
        {
            return frame;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("Absolute timeline worker did not publish a frame");
}

#[test]
fn disabled_runtime_does_not_start_worker_or_accept_audio() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    let samples = [0.0; 32];
    assert!(!runtime.push_block_from_audio(&samples, 2, Some(0)));
    let stats = runtime.stats();
    assert!(!stats.enabled);
    assert!(!stats.worker_running);
    assert_eq!(stats.channel_mode, SpectrumChannelMode::Lr);
    assert_eq!(stats.channels, 2);
    assert_eq!(stats.pushed_blocks, 0);
    assert_eq!(stats.dropped_blocks, 0);
    assert_eq!(stats.analyzed_frames, 0);
    assert_eq!(stats.analyzed_perceptual_frames, 0);
    runtime.shutdown_and_join();
}

#[test]
fn perceptual_mode_is_exclusive_and_publishes_exact_100ms_apertures() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Perceptual));
    assert!(runtime.set_perceptual_state_epoch(Some(0)));
    assert!(runtime.set_enabled(true));
    feed(&runtime, 48_000, 11_000, 256);
    let frame = wait_for_perceptual_frame_at_or_after(&runtime, 9_600);
    assert_eq!(frame.presentation_end_samples, 9_600);
    assert_eq!(frame.aperture_samples, 4_800);
    assert_eq!(frame.state_epoch_samples, 0);
    assert_eq!(frame.channel_mode, SpectrumChannelMode::Lr);
    assert!(frame.sharpness.is_finite() && frame.sharpness > 0.0);
    assert!(runtime
        .try_history()
        .is_some_and(|history| history.newest().is_none()));
    let stats = runtime.stats();
    assert_eq!(stats.analysis_mode, AnalysisViewMode::Perceptual);
    assert_eq!(stats.analyzed_frames, 0);
    assert!(stats.analyzed_perceptual_frames >= 2);
    runtime.shutdown_and_join();
}

#[test]
fn perceptual_44k1_grid_is_exact_and_mode_edges_clear_both_histories() {
    let runtime = SpectrumRuntime::new(44_100, 2);
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Perceptual));
    assert!(runtime.set_perceptual_state_epoch(Some(0)));
    assert!(runtime.set_enabled(true));
    // The continuous FFT resampler may retain one converter chunk before the first complete
    // 48 kHz Phase D aperture becomes publishable. Endpoints remain on the 44.1 kHz grid.
    feed_with_delay(&runtime, 44_100, 20_000, 441, 10);
    let frame = wait_for_perceptual_frame_at_or_after(&runtime, 8_820);
    assert!(frame.presentation_end_samples >= 8_820);
    assert_eq!(frame.aperture_samples, 4_410);
    assert_eq!(frame.presentation_end_samples % 4_410, 0);

    assert!(runtime.set_analysis_mode(AnalysisViewMode::Spectrum));
    assert!(runtime
        .try_perceptual_history()
        .is_some_and(|history| history.newest().is_none()));
    assert!(runtime
        .try_history()
        .is_some_and(|history| history.newest().is_none()));
    runtime.shutdown_and_join();
}

#[test]
fn perceptual_discontinuity_clears_history_and_requires_a_new_shared_epoch() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Perceptual));
    assert!(runtime.set_perceptual_state_epoch(Some(0)));
    assert!(runtime.set_enabled(true));
    feed(&runtime, 48_000, 9_600, 256);
    let _ = wait_for_perceptual_frame_at_or_after(&runtime, 9_600);

    let samples = [0.125_f32; 512];
    assert!(runtime.push_block_from_audio(&samples, 2, Some(12_000)));
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline && !runtime.perceptual_rearm_required.load(Ordering::Acquire) {
        thread::sleep(Duration::from_millis(2));
    }
    assert!(runtime.take_perceptual_rearm_required());
    assert!(runtime
        .try_perceptual_history()
        .is_some_and(|history| history.newest().is_none()));
    assert!(runtime.set_perceptual_state_epoch(None));
    assert_eq!(runtime.perceptual_state_epoch(), None);
    runtime.shutdown_and_join();
}

#[test]
fn absolute_mode_publishes_three_post_facts_on_one_exact_timeline() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Absolute));
    assert!(runtime.set_enabled(true));
    feed(&runtime, 48_000, 11_000, 256);
    let frame = wait_for_absolute_frame_at_or_after(&runtime, 9_600);
    assert_eq!(frame.presentation_end_samples, 9_600);
    assert_eq!(frame.aperture_samples, 4_800);
    assert_eq!(frame.state_epoch_samples, 0);
    assert!(frame.lufs_m.is_some_and(f64::is_finite));
    assert!(frame.true_peak.is_some_and(f64::is_finite));
    assert!(frame
        .sharpness
        .is_some_and(|value| value.is_finite() && value > 0.0));
    let history = runtime.try_absolute_history().unwrap();
    assert_eq!(history.frames().len(), 2);
    assert_eq!(runtime.stats().analyzed_absolute_frames, 2);
    assert!(runtime
        .try_history()
        .is_some_and(|history| history.newest().is_none()));
    runtime.shutdown_and_join();
}

#[test]
fn absolute_mode_clears_on_discontinuity_and_mode_change() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Absolute));
    assert!(runtime.set_enabled(true));
    feed(&runtime, 48_000, 9_600, 256);
    let first = wait_for_absolute_frame_at_or_after(&runtime, 9_600);
    assert_eq!(first.state_epoch_samples, 0);

    let first_block = [0.125_f32; 512];
    assert!(runtime.push_block_from_audio(&first_block, 2, Some(14_400)));
    let clear_deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < clear_deadline
        && runtime
            .try_absolute_history()
            .is_none_or(|history| history.newest().is_some())
    {
        thread::sleep(Duration::from_millis(2));
    }
    assert!(runtime
        .try_absolute_history()
        .is_some_and(|history| history.newest().is_none()));
    let remaining = vec![0.125_f32; (4_800 - 256) * 2];
    assert!(runtime.push_block_from_audio(&remaining, 2, Some(14_656)));
    let restarted = wait_for_absolute_frame_at_or_after(&runtime, 19_200);
    assert_eq!(restarted.state_epoch_samples, 14_400);
    assert_eq!(runtime.try_absolute_history().unwrap().frames().len(), 1);

    assert!(runtime.set_analysis_mode(AnalysisViewMode::Spectrum));
    assert!(runtime
        .try_absolute_history()
        .is_some_and(|history| history.newest().is_none()));
    runtime.shutdown_and_join();
}

#[test]
#[ignore = "release-mode hidden-path performance probe; run explicitly with --nocapture"]
fn disabled_audio_path_cost_is_quantified_without_starting_worker() {
    use std::hint::black_box;

    let runtime = SpectrumRuntime::new(48_000, 2);
    let samples = [0.0; 32];
    let iterations = 1_000_000;
    let started = Instant::now();
    for _ in 0..iterations {
        black_box(runtime.push_block_from_audio(&samples, 2, Some(0)));
    }
    let nanos_per_call = started.elapsed().as_secs_f64() * 1_000_000_000.0 / iterations as f64;
    eprintln!("hidden Spectrum ingress: {nanos_per_call:.2} ns/callback");
    let stats = runtime.stats();
    assert!(!stats.enabled);
    assert!(!stats.worker_running);
    assert_eq!(stats.channel_mode, SpectrumChannelMode::Lr);
    assert_eq!(stats.channels, 2);
    assert_eq!(stats.pushed_blocks, 0);
    assert_eq!(stats.dropped_blocks, 0);
    assert_eq!(stats.analyzed_frames, 0);
    runtime.shutdown_and_join();
}

#[test]
#[ignore = "release-mode enabled audio-ingress performance probe; run explicitly with --nocapture"]
fn enabled_48k_audio_ingress_budget_is_quantified() {
    use std::hint::black_box;

    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_enabled(true));
    let block_frames = 512_usize;
    let samples = [0.125_f32; 1_024];
    let mut accepted = 0_u64;
    let mut presentation_start = 0_i64;
    let mut measured = Duration::ZERO;
    while accepted < 1_000 {
        let started = Instant::now();
        let pushed =
            runtime.push_block_from_audio(black_box(&samples), 2, Some(presentation_start));
        measured += started.elapsed();
        if pushed {
            accepted += 1;
            presentation_start += block_frames as i64;
        }
        // The probe drives about twice realtime. This keeps capacity backpressure in scope while
        // excluding the deliberate wait from the measured Audio Thread section.
        thread::sleep(Duration::from_millis(5));
    }
    let micros_per_block = measured.as_secs_f64() * 1_000_000.0 / accepted as f64;
    let callbacks_per_second = 48_000.0 / block_frames as f64;
    let projected_pair_cpu_percent = micros_per_block * callbacks_per_second * 2.0 / 10_000.0;
    eprintln!(
        "48k Spectrum ingress: {micros_per_block:.2} us/block, \
         projected PRE+POST Audio Thread CPU {projected_pair_cpu_percent:.3}%"
    );
    assert!(projected_pair_cpu_percent < 0.5);
    assert_eq!(runtime.stats().dropped_blocks, 0);
    runtime.shutdown_and_join();
}

#[test]
fn enabled_runtime_publishes_on_the_shared_48k_30hz_grid() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_enabled(true));
    feed(&runtime, 48_000, 10_000, 256);
    let frame = wait_for_frame_at_or_after(&runtime, 9_600);
    assert_eq!(frame.presentation_end_samples, 9_600);
    assert_eq!(frame.presentation_end_samples % 1_600, 0);
    assert!(runtime.stats().analyzed_frames >= 1);
    runtime.shutdown_and_join();
}

#[test]
fn channel_mode_edge_clears_history_and_mono_side_fails_closed() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_enabled(true));
    feed(&runtime, 48_000, 10_000, 256);
    let lr = wait_for_frame_at_or_after(&runtime, 9_600);
    assert_eq!(lr.channel_mode, SpectrumChannelMode::Lr);
    assert_eq!(lr.channels, 2);
    assert!(lr
        .dbfs
        .iter()
        .any(|value| *value > crate::SPECTRUM_FLOOR_DBFS + 20.0));

    assert!(runtime.set_channel_mode(SpectrumChannelMode::Mid));
    assert!(runtime
        .try_history()
        .is_some_and(|history| history.newest().is_none()));
    feed(&runtime, 48_000, 10_000, 256);
    let mid = wait_for_frame_at_or_after(&runtime, 9_600);
    assert_eq!(mid.channel_mode, SpectrumChannelMode::Mid);
    assert!(mid
        .dbfs
        .iter()
        .all(|value| value.to_bits() == crate::SPECTRUM_FLOOR_DBFS.to_bits()));
    runtime.shutdown_and_join();

    let mono = SpectrumRuntime::new(48_000, 1);
    assert!(mono.set_channel_mode(SpectrumChannelMode::Mid));
    assert!(!mono.set_channel_mode(SpectrumChannelMode::Side));
    assert_eq!(mono.channel_mode(), SpectrumChannelMode::Mid);
    mono.shutdown_and_join();
}

#[test]
fn stale_generation_or_channel_mode_can_never_be_republished() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    assert!(runtime.set_enabled(true));
    let generation = runtime.generation.load(Ordering::Acquire);
    let frame = SpectrumFrame {
        schema_version: crate::SPECTRUM_SCHEMA_VERSION,
        sample_rate: 48_000,
        fft_size: crate::SPECTRUM_FFT_SIZE as u32,
        band_count: crate::SPECTRUM_BAND_COUNT as u16,
        presentation_end_samples: 4_800,
        generation,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        min_hz: 10.0,
        max_hz: 22_000.0,
        dbfs: [-24.0; crate::SPECTRUM_BAND_COUNT],
    };
    assert!(runtime.frame_is_current(&frame));

    assert!(runtime.set_channel_mode(SpectrumChannelMode::Mid));
    assert!(!runtime.frame_is_current(&frame));
    let mut current = frame.clone();
    current.generation = runtime.generation.load(Ordering::Acquire);
    current.channel_mode = SpectrumChannelMode::Mid;
    assert!(runtime.frame_is_current(&current));

    assert!(runtime.set_enabled(false));
    assert!(!runtime.frame_is_current(&current));
    runtime.shutdown_and_join();
}

#[test]
fn forty_four_one_uses_a_1470_sample_grid_without_drift() {
    let runtime = SpectrumRuntime::new(44_100, 2);
    assert!(runtime.set_enabled(true));
    feed(&runtime, 44_100, 9_000, 147);
    let frame = wait_for_frame_at_or_after(&runtime, 8_820);
    assert_eq!(frame.presentation_end_samples, 8_820);
    assert_eq!(frame.presentation_end_samples % 1_470, 0);
    runtime.shutdown_and_join();
}

#[test]
fn discontinuity_requires_a_new_complete_window() {
    let analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let mut assembler = SpectrumAssembler::new(analyzer, 1);
    assert!(assembler.begin_block(0, 1));
    for _ in 0..SPECTRUM_WINDOW_SIZE - 1 {
        assert!(assembler
            .push_frame(0.0, None, SpectrumChannelMode::Lr)
            .is_none());
    }
    assert!(assembler.begin_block(SPECTRUM_WINDOW_SIZE as i64 + 1_000, 1));
    for _ in 0..SPECTRUM_WINDOW_SIZE - 1 {
        assert!(assembler
            .push_frame(0.0, None, SpectrumChannelMode::Lr)
            .is_none());
    }
}
