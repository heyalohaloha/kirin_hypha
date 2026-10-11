use super::*;
use std::time::{Duration, Instant};

fn verified(start_samples: i64, proof_revision: u64) -> SpectrumInputClock {
    SpectrumInputClock::VerifiedAax {
        start_samples,
        proof_revision,
    }
}

fn runtime(mode: AnalysisViewMode) -> Arc<SpectrumRuntime> {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(runtime.set_analysis_mode(mode));
    if mode == AnalysisViewMode::Perceptual {
        assert!(runtime.set_perceptual_state_epoch(Some(960_000)));
    }
    runtime.set_enabled(true);
    runtime
}

fn feed(runtime: &SpectrumRuntime) {
    for position in (960_000..1_008_128).step_by(256) {
        let audio = (position..position + 256)
            .flat_map(|sample| {
                let value = 0.1 * (sample as f32 * 0.13).sin();
                [value, value]
            })
            .collect::<Vec<_>>();
        assert!(runtime.push_block_from_audio_with_clock(&audio, 2, Some(verified(position, 1))));
        thread::sleep(Duration::from_millis(10));
    }
}

#[test]
fn qualified_aax_clock_runs_real_sharp_live_and_psb_workers() {
    for mode in [AnalysisViewMode::Perceptual, AnalysisViewMode::Absolute] {
        let runtime = runtime(mode);
        let coordinator = crate::SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
        if mode == AnalysisViewMode::Absolute {
            assert!(coordinator.set_post_analysis_mode(mode));
            coordinator.set_post_visible(true);
            assert!(coordinator.post_tick("post", None));
        }
        feed(&runtime);
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            let ready = if mode == AnalysisViewMode::Perceptual {
                runtime.try_perceptual_history().is_some_and(|history| {
                    history.newest().is_some_and(|frame| frame.psb.is_some())
                })
            } else {
                runtime.try_absolute_history().is_some_and(|history| {
                    history.newest().is_some_and(|frame| {
                        frame.lufs_m.is_some() && frame.true_peak.is_some() && frame.psb.is_some()
                    })
                })
            };
            if ready {
                break;
            }
            assert!(
                Instant::now() < deadline,
                "{mode:?} must publish measured stateful facts"
            );
            thread::sleep(Duration::from_millis(2));
        }
        assert_eq!(
            runtime.spectrum_clock_kind(),
            Some(SpectrumClockKind::AaxEngine)
        );
        assert!(!runtime.output_latency_unavailable());
        if mode == AnalysisViewMode::Absolute {
            assert!(coordinator.post_tick("post", None));
            assert_eq!(
                coordinator.try_view().unwrap().status,
                crate::SpectrumViewStatus::Active
            );
        }
        runtime.note_analysis_clock_drop_from_audio();
        if mode == AnalysisViewMode::Absolute {
            let waiting = coordinator.try_view().unwrap();
            assert_eq!(waiting.status, crate::SpectrumViewStatus::WarmingUp);
            assert!(waiting.absolute_timeline.newest().is_none());
        }
        assert!(!runtime.output_latency_unavailable());
        assert!(runtime
            .try_perceptual_history()
            .is_none_or(|h| h.newest().is_none()));
        assert!(runtime
            .try_absolute_history()
            .is_none_or(|h| h.newest().is_none()));
        assert!(runtime.push_block_from_audio_with_clock(
            &[0.1; 512],
            2,
            Some(verified(1_008_128, 2))
        ));
        assert!(runtime
            .try_perceptual_history()
            .is_none_or(|h| h.newest().is_none()));
        assert!(runtime
            .try_absolute_history()
            .is_none_or(|h| h.newest().is_none()));
        assert!(!runtime.output_latency_unavailable());
        coordinator.shutdown();
        runtime.shutdown_and_join();
    }
}

#[test]
fn aax_proof_is_separate_from_presentation_and_never_promotes_freq() {
    let runtime = runtime(AnalysisViewMode::Perceptual);
    assert!(runtime.push_block_from_audio_with_clock(&[0.1; 512], 2, Some(verified(960_000, 1))));
    assert_eq!(
        runtime.spectrum_clock_kind(),
        Some(SpectrumClockKind::AaxEngine)
    );
    assert!(!runtime.presentation_clock_aligned());
    for invalid in [0, 1_u64 << 62, u64::MAX] {
        assert!(!runtime.push_block_from_audio_with_clock(
            &[0.1; 512],
            2,
            Some(verified(960_256, invalid))
        ));
    }
    assert!(runtime.push_block_from_audio(&[0.1; 512], 2, Some(960_256)));
    assert_eq!(
        runtime.spectrum_clock_kind(),
        Some(SpectrumClockKind::Presentation)
    );
    assert!(runtime.set_analysis_mode(AnalysisViewMode::Spectrum));
    assert!(!runtime.push_block_from_audio_with_clock(&[0.1; 512], 2, Some(verified(960_512, 1))));
    assert!(runtime.push_block_from_audio_with_clock(
        &[0.1; 512],
        2,
        Some(SpectrumInputClock::LocalProject(0))
    ));
    assert_eq!(
        runtime.spectrum_clock_kind(),
        Some(SpectrumClockKind::ProjectTimeline)
    );
    runtime.shutdown_and_join();
}
