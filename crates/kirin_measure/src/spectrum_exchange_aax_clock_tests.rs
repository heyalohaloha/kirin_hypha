use super::*;
use crate::spectrum_runtime::SpectrumInputClock;
use std::thread;

fn exchange_until(
    pre: &SpectrumCoordinator,
    post: &SpectrumCoordinator,
    pre_dir: &Path,
    target: &SpectrumTarget,
    stage: &str,
    ready: impl Fn() -> bool,
) {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        // Feeding and analysis can outlast the request lease on a slow runner. These manual
        // ticks must renew POST before PRE reads it, as the live exchange workers do.
        if post.post_tick("post", Some(target.clone()))
            && pre.pre_tick("pre", pre_dir)
            && post.post_tick("post", Some(target.clone()))
            && ready()
        {
            return;
        }
        assert!(
            Instant::now() < deadline,
            "exchange did not complete: {stage}"
        );
        thread::sleep(Duration::from_millis(2));
    }
}

fn feed(runtime: &SpectrumRuntime, from: i64, to: i64, aax: bool) {
    let mut position = from;
    while position < to {
        let frames = 256.min((to - position) as usize);
        let samples = (position..position + frames as i64)
            .flat_map(|sample| {
                let value = 0.1 * (sample as f32 * 0.13).sin();
                [value, value]
            })
            .collect::<Vec<_>>();
        let clock = if aax {
            SpectrumInputClock::VerifiedAax {
                start_samples: position,
                proof_revision: 1,
            }
        } else {
            SpectrumInputClock::LegacyPresentation(position)
        };
        assert!(runtime.push_block_from_audio_with_clock(&samples, 2, Some(clock)));
        position += frames as i64;
        thread::sleep(Duration::from_millis(2));
    }
}

#[test]
fn aax_perceptual_exchange_joins_the_same_clock_and_rejects_both_mixed_directions() {
    for (pre_aax, post_aax) in [(true, true), (true, false), (false, true)] {
        let temp = tempfile::tempdir().unwrap();
        let pre_dir = temp.path().join("pre");
        let pre_json = pre_dir.join("pre.json");
        crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
        let pre_runtime =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let post_runtime =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
        let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
        let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
        assert!(post.set_post_analysis_mode(AnalysisViewMode::Perceptual));
        post.set_post_visible(true);
        exchange_until(&pre, &post, &pre_dir, &target, "initial request", || true);
        feed(&pre_runtime, 960_000, 962_048, pre_aax);
        feed(&post_runtime, 960_000, 962_048, post_aax);
        exchange_until(&pre, &post, &pre_dir, &target, "shared epoch", || {
            post_runtime.perceptual_state_epoch().is_some()
                && pre_runtime.perceptual_state_epoch() == post_runtime.perceptual_state_epoch()
        });
        let epoch = post_runtime
            .perceptual_state_epoch()
            .expect("future shared reset");
        assert_eq!(pre_runtime.perceptual_state_epoch(), Some(epoch));
        let end = epoch + 9_600;
        feed(&pre_runtime, 962_048, end, pre_aax);
        feed(&post_runtime, 962_048, end, post_aax);
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            let ready = [&pre_runtime, &post_runtime].iter().all(|runtime| {
                runtime.try_perceptual_history().is_some_and(|history| {
                    history
                        .newest()
                        .is_some_and(|frame| frame.presentation_end_samples == end)
                })
            });
            if ready {
                break;
            }
            assert!(
                Instant::now() < deadline,
                "both real workers complete the exact window"
            );
            thread::sleep(Duration::from_millis(2));
        }
        // Exercise a computation pause beyond one lease instead of depending on runner speed.
        thread::sleep(Duration::from_millis(REQUEST_LEASE_MS as u64 + 50));
        let expected_status = if pre_aax == post_aax {
            SpectrumViewStatus::Active
        } else {
            SpectrumViewStatus::Unavailable
        };
        exchange_until(
            &pre,
            &post,
            &pre_dir,
            &target,
            "exact window publication",
            || {
                read_perceptual_snapshot(&pre_dir).is_some_and(|snapshot| {
                    snapshot
                        .history
                        .newest()
                        .is_some_and(|frame| frame.presentation_end_samples == end)
                }) && post
                    .try_view()
                    .is_some_and(|view| view.status == expected_status)
            },
        );
        assert_eq!(pre_runtime.perceptual_state_epoch(), Some(epoch));
        assert_eq!(post_runtime.perceptual_state_epoch(), Some(epoch));
        let view = post.try_view().unwrap();
        if pre_aax == post_aax {
            assert_eq!(view.status, SpectrumViewStatus::Active);
            assert!(view.perceptual_difference.is_some());
            assert!(view.perceptual_difference.unwrap().delta_sharpness.abs() < 1.0e-10);
            assert!(post_runtime.push_block_from_audio_with_clock(
                &[0.1; 512],
                2,
                Some(SpectrumInputClock::VerifiedAax {
                    start_samples: end,
                    proof_revision: 2
                })
            ));
            let changed = post.try_view().unwrap();
            assert_eq!(changed.status, SpectrumViewStatus::WarmingUp);
            assert!(changed.perceptual_difference.is_none());
            // Equal endpoint/epoch cannot carry the prior Active lease across a clock cutover.
            feed(&post_runtime, end + 256, end + 512, false);
            assert!(post.post_tick("post", Some(target)));
            let cut = post.try_view().unwrap();
            assert_eq!(cut.status, SpectrumViewStatus::Unavailable);
            assert!(cut.perceptual_difference.is_none());
        } else {
            assert_eq!(view.status, SpectrumViewStatus::Unavailable);
            assert!(view.perceptual_difference.is_none());
            assert_eq!(view.perceptual_timeline.frames().len(), 0);
        }
        post.shutdown();
        pre.shutdown();
        post_runtime.shutdown_and_join();
        pre_runtime.shutdown_and_join();
    }
}
