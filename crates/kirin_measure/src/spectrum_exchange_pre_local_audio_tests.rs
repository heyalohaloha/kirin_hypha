//! Real PCM parity at identical continuous-state epochs/endpoints, not a token-only test.
use super::*;
use crate::perceptual::SharpnessContinuousAnalyzer;
use std::collections::BTreeMap;
fn pcm(rate: u32, channels: usize, start: i64, frames: usize) -> Vec<f32> {
    (0..frames)
        .flat_map(|index| {
            let phase = (start + index as i64) as f32 / rate as f32;
            let x = (phase * 997.0 * std::f32::consts::TAU).sin() * 0.2
                + (phase * 7211.0 * std::f32::consts::TAU).sin() * 0.05;
            (0..channels).map(move |ch| if ch == 0 { x } else { -0.7 * x })
        })
        .collect()
}
fn push(runtime: &SpectrumRuntime, input: &[f32], channels: usize, start: i64) {
    for (index, block) in input.chunks(256 * channels).enumerate() {
        assert!(runtime.push_block_from_audio(block, channels, Some(start + index as i64 * 256)));
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
    }
}
#[test]
fn pre_local_real_pcm_matches_native_sharpness_at_exact_epoch_and_endpoints() {
    for rate in [44_100, 48_000, 96_000] {
        for channels in [1, 2] {
            let runtime = SpectrumRuntime::new(
                rate,
                crate::channel_layout::ChannelLayout::mono_or_stereo_by_count(channels).unwrap(),
            );
            let pre = SpectrumCoordinator::new(rate, Arc::clone(&runtime));
            pre.use_test_clock(&clock::TestClock::new());
            let dir = tempfile::tempdir().unwrap();
            let token = pre.request_pre_sharp(0);
            assert_ne!(token, 0);
            assert!(pre.pre_tick("pre", dir.path()));
            push(&runtime, &pcm(rate, channels, 0, 256), channels, 0);
            assert!(pre.pre_tick("pre", dir.path()));
            let epoch = pre
                .pre_local
                .lock()
                .unwrap()
                .epoch
                .expect("local epoch armed");
            assert!(epoch > 256);
            push(
                &runtime,
                &pcm(rate, channels, 256, (epoch - 256) as usize),
                channels,
                256,
            );
            let mut direct = SharpnessContinuousAnalyzer::new(rate, channels).unwrap();
            direct.reset_at_epoch(epoch).unwrap();
            let mut expected = BTreeMap::new();
            let mut seen = BTreeMap::new();
            let aperture = rate as usize / 10;
            for slot in 0..24 {
                let start = epoch + (slot * aperture) as i64;
                let input = pcm(rate, channels, start, aperture);
                for frame in direct
                    .analyze_aperture(&input, SpectrumChannelMode::Lr, start + aperture as i64, 1)
                    .unwrap()
                {
                    expected.insert(frame.presentation_end_samples, frame.sharpness);
                }
                push(&runtime, &input, channels, start);
                assert!(pre.pre_tick("pre", dir.path()));
                if let Some((_, frame)) = pre.poll_pre_sharp(token) {
                    assert_eq!(frame.state_epoch_samples, epoch);
                    assert_eq!(frame.sample_rate, rate);
                    assert_eq!(usize::from(frame.channels), channels);
                    assert!(frame.sharpness.is_finite());
                    let wanted = expected
                        .get(&frame.presentation_end_samples)
                        .expect("identical native endpoint");
                    assert_eq!(
                        frame.sharpness, *wanted,
                        "rate={rate} channels={channels} endpoint={}",
                        frame.presentation_end_samples
                    );
                    seen.insert(frame.presentation_end_samples, frame.sharpness);
                }
            }
            assert!(
                seen.len() >= 20,
                "real finite observations required, got {}",
                seen.len()
            );
            runtime.note_analysis_clock_drop_from_audio();
            assert!(
                pre.poll_pre_sharp(token).is_none(),
                "rearm must immediately reject old frames"
            );
            assert!(pre.pre_tick("pre", dir.path()));
            assert!(pre.poll_pre_sharp(token).is_none());
            assert!(pre.release_pre_sharp(token));
            assert!(
                pre.poll_pre_sharp(token).is_none(),
                "released demand cannot expose history"
            );
            assert!(!runtime.is_enabled());
            pre.shutdown();
            runtime.shutdown_and_join();
        }
    }
}
