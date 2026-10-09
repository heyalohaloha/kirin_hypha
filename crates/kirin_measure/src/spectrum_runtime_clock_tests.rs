//! Real-worker clock authority boundaries; no public schema or clock compensation changes.
use super::*;
use std::time::{Duration, Instant};

fn feed(runtime: &SpectrumRuntime, clock: impl Fn(i64) -> SpectrumInputClock) {
    for position in (0..10_240).step_by(256) {
        let samples = (position..position + 256)
            .flat_map(|index| {
                let sample = (std::f32::consts::TAU * 1_000.0 * index as f32 / 48_000.0).sin();
                [sample, sample]
            })
            .collect::<Vec<_>>();
        assert!(runtime.push_block_from_audio_with_clock(&samples, 2, Some(clock(position))));
        thread::sleep(Duration::from_millis(2));
    }
}

fn wait(runtime: &SpectrumRuntime) -> SpectrumHistory {
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if let Some(history) = runtime.try_history().filter(|history| {
            history
                .newest()
                .is_some_and(|frame| frame.presentation_end_samples >= 9_600)
        }) {
            return history;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("Spectrum worker failed to publish current-clock history");
}

fn runtime() -> Arc<SpectrumRuntime> {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(runtime.set_enabled(true));
    runtime
}

#[test]
fn local_spectrum_is_absolute_and_cannot_be_exported_as_aligned_history() {
    let runtime = runtime();
    feed(&runtime, SpectrumInputClock::LocalProject);
    let history = wait(&runtime);
    let frame = history.newest().unwrap();
    assert_eq!(frame.presentation_end_samples, 9_600);
    assert!(frame.dbfs.iter().any(|value| *value > -12.0));
    assert!(!runtime.presentation_clock_aligned());
    assert!(runtime.try_aligned_history().is_none());
    assert!(runtime
        .try_history_with_alignment()
        .is_some_and(|(_, aligned)| !aligned));
    runtime.shutdown_and_join();
}

#[test]
fn authority_change_invalidates_same_endpoint_values_and_keeps_full_generation_counter() {
    let runtime = runtime();
    feed(&runtime, SpectrumInputClock::LegacyPresentation);
    wait(&runtime);
    let generation = runtime.stream_generation.load(Ordering::Acquire);
    assert!(runtime.push_block_from_audio_with_clock(
        &[0.0; 512],
        2,
        Some(SpectrumInputClock::LocalProject(0))
    ));
    assert_eq!(
        runtime.stream_generation.load(Ordering::Acquire),
        generation + 1
    );
    assert!(runtime.try_history().is_none());
    assert!(!runtime.presentation_clock_aligned());
    feed(&runtime, SpectrumInputClock::LocalProject);
    wait(&runtime);
    assert!(runtime
        .try_history_after_clone_for_test(|| {
            assert!(runtime.push_block_from_audio_with_clock(
                &[0.0; 512],
                2,
                Some(SpectrumInputClock::LocalRender(0))
            ));
        })
        .is_none());
    assert_eq!(
        runtime.stream_generation.load(Ordering::Acquire),
        generation + 2
    );
    runtime.shutdown_and_join();
}

#[test]
fn wrapper_latency_definition_change_invalidates_old_history_then_recovers_alignment() {
    let runtime = runtime();
    feed(&runtime, SpectrumInputClock::LocalProject);
    wait(&runtime);
    let clock = |position| SpectrumInputClock::Presentation {
        start_samples: position,
        source: crate::PresentationLatencySource::Vst3,
        output_latency_samples: 2_048,
    };
    assert!(runtime.push_block_from_audio_with_clock(&[0.0; 512], 2, Some(clock(0))));
    assert!(runtime.try_history().is_none());
    feed(&runtime, clock);
    wait(&runtime);
    assert!(runtime.try_aligned_history().is_some());
    for (source, latency) in [
        (crate::PresentationLatencySource::Vst3, 4_096),
        (crate::PresentationLatencySource::AudioUnitV2, 4_096),
    ] {
        assert!(runtime.push_block_from_audio_with_clock(
            &[0.0; 512],
            2,
            Some(SpectrumInputClock::Presentation {
                start_samples: 0,
                source,
                output_latency_samples: latency,
            })
        ));
        assert!(runtime.try_history().is_none());
    }
    runtime.shutdown_and_join();
}

#[test]
fn local_clock_cannot_enter_state_epoch_modes_and_invalid_coordinates_fail_closed() {
    let runtime = runtime();
    for mode in [AnalysisViewMode::Perceptual, AnalysisViewMode::Absolute] {
        assert!(runtime.set_analysis_mode(mode));
        assert!(!runtime.push_block_from_audio_with_clock(
            &[0.0; 512],
            2,
            Some(SpectrumInputClock::LocalProject(0))
        ));
    }
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Spectrum));
    assert!(!runtime.push_block_from_audio_with_clock(&[0.0; 512], 2, None));
    assert!(!runtime.push_block_from_audio_with_clock(
        &[0.0; 512],
        2,
        Some(SpectrumInputClock::LocalProject(i64::MAX))
    ));
    assert!(!runtime.push_block_from_audio_with_clock(
        &[0.0; 512],
        2,
        Some(SpectrumInputClock::Presentation {
            start_samples: 0,
            source: crate::PresentationLatencySource::Unknown,
            output_latency_samples: 0,
        })
    ));
    runtime.shutdown_and_join();
}

#[test]
fn authority_generation_overflow_never_reuses_a_stream_identity() {
    let runtime = runtime();
    runtime.stream_generation.store(u64::MAX, Ordering::Release);
    assert!(!runtime.push_block_from_audio_with_clock(
        &[0.0; 512],
        2,
        Some(SpectrumInputClock::LocalProject(0))
    ));
    assert_eq!(runtime.stream_generation.load(Ordering::Acquire), 0);
    assert!(!runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
    assert!(runtime.try_history().is_none());
    assert_eq!(runtime.stats().pushed_blocks, 0);
    runtime.shutdown_and_join();
}

impl SpectrumRuntime {
    pub(crate) fn with_locked_history_for_clock_test<T>(&self, inspect: impl FnOnce() -> T) -> T {
        let _guard = self.history.lock().unwrap();
        inspect()
    }
}
