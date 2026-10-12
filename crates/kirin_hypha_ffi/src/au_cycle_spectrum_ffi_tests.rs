//! AU local FREQ facts must survive an unavailable correspondence clock.
use super::*;
use kirin_measure::{channel_layout::ChannelLayout, spectrum_exchange::SpectrumTarget};
use std::{thread, time::Duration};

#[test]
fn au_cycle_mono_post_spectrum_survives_without_pair_clock() {
    verify_cycle(ChannelLayout::mono());
}

#[test]
fn au_cycle_stereo_post_spectrum_survives_without_pair_clock() {
    verify_cycle(ChannelLayout::stereo());
}

#[test]
fn au_render_only_pre_keeps_local_spectrum_without_publishing_delta() {
    for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
        let temp = tempfile::tempdir().unwrap();
        let pre_json = temp.path().join("pre.json");
        std::fs::write(&pre_json, b"{}").unwrap();
        let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
        let channels = layout.channel_count();
        let mut pre = Box::new(KirinHyphaEngine::new(48_000, layout));
        let mut post = Box::new(KirinHyphaEngine::new(48_000, layout));
        unsafe {
            kirin_hypha_set_audio_unit_analysis_ingress(pre.as_mut(), false);
            kirin_hypha_set_audio_unit_analysis_ingress(post.as_mut(), true);
        }
        assert!(post
            .spectrum
            .set_post_analysis_mode(AnalysisViewMode::Spectrum));
        post.spectrum.set_post_visible(true);
        let exchange = |pre: &KirinHyphaEngine, post: &KirinHyphaEngine| {
            post.spectrum
                .service_post_endpoint("post", Some(target.clone()), "fixture");
            pre.spectrum
                .service_pre_endpoint("pre", &target.instance_dir);
            post.spectrum
                .service_post_endpoint("post", Some(target.clone()), "fixture");
        };
        exchange(&pre, &post);
        let mut observed = 0;
        for callback in 0..180 {
            let start = callback * 256;
            let audio: Vec<f32> = (start..start + 256)
                .flat_map(|i| std::iter::repeat_n((i as f32 * 0.13).sin() * 0.1, channels))
                .collect();
            for engine in [&mut pre, &mut post] {
                engine.note_capture_window(
                    true,
                    start,
                    256,
                    CaptureClockSource::AudioRenderTimeline,
                );
                engine.push_samples(&audio, channels as u32);
                assert!(
                    unsafe {
                        kirin_hypha_push_paired_analysis(
                            engine.as_mut(),
                            audio.as_ptr(),
                            256,
                            channels as u32,
                            false,
                            0,
                            0,
                            false,
                            0,
                            0,
                        )
                    },
                    "render-only AU retains each role's absolute FREQ"
                );
            }
            thread::sleep(Duration::from_millis(6));
            exchange(&pre, &post);
            if let Some((history, kind)) = pre.spectrum_runtime.try_history_with_clock() {
                assert!(kind.is_none(), "PRE local render is never a shared clock");
                if let Some(frame) = history.newest() {
                    assert!(frame.dbfs.iter().all(|v| v.is_finite()));
                    assert!(frame.dbfs.iter().any(|v| *v > -60.0));
                    assert!(frame.presentation_end_samples <= start + 256);
                    assert!(frame.presentation_end_samples > start - 8_000);
                    observed += 1;
                }
            }
            if let Some(view) = post.spectrum.try_view() {
                assert!(view.difference.is_none());
                assert!(view.perceptual_difference.is_none());
            }
            #[cfg(not(target_os = "windows"))]
            assert!(
                !target.instance_dir.join("spectrum/pre.bin").exists(),
                "local PRE spectrum is never published for another instance"
            );
        }
        assert!(
            observed > 130,
            "PRE FREQ keeps advancing without transport callbacks"
        );
        for engine in [&pre, &post] {
            engine.spectrum.shutdown();
            engine.spectrum_runtime.shutdown_and_join();
        }
    }
}

#[test]
fn au_render_only_pre_retains_absolute_sharpness() {
    for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
        let channels = layout.channel_count();
        let mut pre = Box::new(KirinHyphaEngine::new(48_000, layout));
        unsafe { kirin_hypha_set_audio_unit_analysis_ingress(pre.as_mut(), false) };
        assert!(pre
            .spectrum_runtime
            .set_analysis_mode(AnalysisViewMode::Absolute));
        assert!(pre.spectrum_runtime.set_enabled(true));
        for callback in 0..180 {
            let start = callback * 256;
            let audio: Vec<f32> = (start..start + 256)
                .flat_map(|i| std::iter::repeat_n((i as f32 * 0.13).sin() * 0.1, channels))
                .collect();
            pre.note_capture_window(true, start, 256, CaptureClockSource::AudioRenderTimeline);
            pre.push_samples(&audio, channels as u32);
            thread::sleep(Duration::from_millis(6));
        }
        let history = pre.spectrum_runtime.try_absolute_history().unwrap();
        let latest = history
            .newest()
            .expect("render-only PRE has absolute SHARP");
        assert!(latest
            .sharpness
            .is_some_and(|value| value.is_finite() && value > 0.0));
        assert!(latest.presentation_end_samples >= 38_400);
        pre.spectrum_runtime.shutdown_and_join();
    }
}

fn verify_cycle(layout: ChannelLayout) {
    let temp = tempfile::tempdir().unwrap();
    let pre_json = temp.path().join("pre.json");
    std::fs::write(&pre_json, b"{}").unwrap();
    let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
    let channels = layout.channel_count();
    let mut post = Box::new(KirinHyphaEngine::new(48_000, layout));
    unsafe { kirin_hypha_set_audio_unit_analysis_ingress(post.as_mut(), true) };
    assert!(post
        .spectrum
        .set_post_analysis_mode(AnalysisViewMode::Spectrum));
    post.spectrum.set_post_visible(true);
    // Pair presence must not decide whether the local observation is available.
    for paired in [true, false] {
        let selected = paired.then_some(target.clone());
        post.spectrum
            .service_post_endpoint("post", selected.clone(), "fixture");
        let mut observations = 0;
        for callback in 0..180 {
            // First playback is already cycling. No previous linear proof can be reused.
            let start = (callback * 256) % 16_000;
            let audio: Vec<f32> = (start..start + 256)
                .flat_map(|i| std::iter::repeat_n((i as f32 * 0.13).sin() * 0.1, channels))
                .collect();
            post.note_capture_window(true, start, 256, CaptureClockSource::ProjectTimeline);
            post.push_samples(&audio, channels as u32);
            assert!(
                unsafe {
                    kirin_hypha_push_paired_analysis(
                        post.as_mut(),
                        audio.as_ptr(),
                        256,
                        channels as u32,
                        false,
                        0,
                        0,
                        false,
                        0,
                        0,
                    )
                },
                "AU POST must accept its own host coordinate without a comparison clock"
            );
            // Feed at host cadence; the production exchange publishes at 30 Hz.
            thread::sleep(Duration::from_millis(6));
            post.spectrum
                .service_post_endpoint("post", selected.clone(), "fixture");
            // A concurrent nonblocking publication read leaves the UI's previous frame intact.
            let Some(snapshot) = post.spectrum.try_view() else {
                continue;
            };
            let view = to_c_spectrum_batch(snapshot);
            assert_eq!(
                view.latest.has_data, 0,
                "local clocks must never grant delta"
            );
            if start >= 6_400 {
                assert_eq!(
                    view.latest.post_has_data, 1,
                    "POST survives every cycle lap"
                );
                assert!(view.latest.post_dbfs.iter().all(|v| v.is_finite()));
                assert!(view.latest.post_dbfs.iter().any(|v| *v > -60.0));
                assert!((start - 3_200..=start + 256).contains(&view.latest.presentation_end_samples),
                    "fresh local observation: paired={paired}, callback={callback}, start={start}, end={}",
                    view.latest.presentation_end_samples);
                assert!(
                    view.count > 0,
                    "absolute six-second field receives measured history"
                );
                observations += 1;
            }
        }
        assert!(observations > 90);
    }
    post.spectrum.shutdown();
    post.spectrum_runtime.shutdown_and_join();
}

#[test]
fn au_local_spectrum_requires_one_current_admitted_host_clock() {
    for (mode, post_role) in [
        (AnalysisViewMode::Spectrum, true),
        (AnalysisViewMode::Spectrum, false),
        (AnalysisViewMode::Perceptual, true),
        (AnalysisViewMode::Perceptual, false),
    ] {
        let mut engine = Box::new(KirinHyphaEngine::new(48_000, ChannelLayout::mono()));
        unsafe { kirin_hypha_set_audio_unit_analysis_ingress(engine.as_mut(), post_role) };
        assert!(engine.spectrum_runtime.set_analysis_mode(mode));
        assert!(engine.spectrum_runtime.set_enabled(true));
        let audio = [0.1_f32; 256];
        for (valid, source, start) in [
            (true, CaptureClockSource::AudioRenderTimeline, 0),
            (false, CaptureClockSource::AudioRenderTimeline, 256),
            (true, CaptureClockSource::Unknown, 512),
            (true, CaptureClockSource::ProjectTimeline, i64::MAX - 128),
            (true, CaptureClockSource::ProjectTimeline, 1024),
        ] {
            engine.note_capture_window(valid, start, 256, source);
            engine.push_samples(&audio, 1);
            let push = |engine: &mut KirinHyphaEngine| unsafe {
                kirin_hypha_push_paired_analysis(
                    engine,
                    audio.as_ptr(),
                    256,
                    1,
                    false,
                    0,
                    0,
                    false,
                    0,
                    0,
                )
            };
            let expected = mode == AnalysisViewMode::Spectrum
                && valid
                && source != CaptureClockSource::Unknown
                && start.checked_add(256).is_some();
            assert_eq!(push(engine.as_mut()), expected);
            assert!(
                !push(engine.as_mut()),
                "the staged callback cannot be replayed"
            );
        }
        engine.spectrum_runtime.shutdown_and_join();
    }
}
