//! Diagnostic only: run alone in release mode with --ignored --exact --nocapture --test-threads=1.
//! Process user+system CPU, not elapsed wall time. This measures the optional core increment;
//! it is not a substitute for the real PRE editor/DAW acceptance measurement.
#![cfg(unix)]
use super::*;
#[cfg(unix)]
fn process_cpu_seconds() -> f64 {
    let mut usage = std::mem::MaybeUninit::<libc::rusage>::uninit();
    assert_eq!(
        unsafe { libc::getrusage(libc::RUSAGE_SELF, usage.as_mut_ptr()) },
        0
    );
    let usage = unsafe { usage.assume_init() };
    let seconds = |time: libc::timeval| time.tv_sec as f64 + time.tv_usec as f64 / 1_000_000.0;
    seconds(usage.ru_utime) + seconds(usage.ru_stime)
}
#[cfg(unix)]
fn measured_run(enabled: bool) -> (f64, f64, u64) {
    let rate = 48_000;
    let block_frames = 256;
    let runtime = SpectrumRuntime::new(rate, crate::channel_layout::ChannelLayout::stereo());
    let pre = SpectrumCoordinator::new(rate, Arc::clone(&runtime));
    let dir = tempfile::tempdir().unwrap();
    let token = if enabled { pre.request_pre_sharp(0) } else { 0 };
    if enabled {
        assert_ne!(token, 0);
        assert!(pre.pre_tick("pre", dir.path()));
    }
    let blocks = (rate * 32 / block_frames) as usize;
    let audio: Vec<Vec<f32>> = (0..blocks)
        .map(|i| {
            (0..block_frames)
                .flat_map(|j| {
                    let t = (i * block_frames as usize + j as usize) as f32 / rate as f32;
                    let x = (t * 997.0 * std::f32::consts::TAU).sin() * 0.2;
                    [x, -0.7 * x]
                })
                .collect()
        })
        .collect();
    let origin = Instant::now();
    let warmup_blocks = (rate * 2 / block_frames) as usize;
    let mut cpu_start = 0.0;
    let mut wall_start = origin;
    let mut finite_polls = 0;
    let mut last_endpoint = None;
    for (i, input) in audio.iter().enumerate() {
        if i == warmup_blocks {
            cpu_start = process_cpu_seconds();
            wall_start = Instant::now();
        }
        if i % 32 == 0 {
            if enabled {
                assert_eq!(pre.request_pre_sharp(token), token);
            }
            let active = pre.pre_tick("pre", dir.path());
            assert_eq!(active, enabled);
        }
        let accepted =
            runtime.push_block_from_audio(input, 2, Some((i * block_frames as usize) as i64));
        if enabled {
            assert!(accepted);
        }
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
        if let Some((_, frame)) = pre.poll_pre_sharp(token) {
            if last_endpoint != Some(frame.presentation_end_samples) {
                assert!(frame.sharpness.is_finite());
                finite_polls += 1;
                last_endpoint = Some(frame.presentation_end_samples);
            }
        }
        // Audio-time pacing, not a polling sleep. Never sleep when processing fell behind.
        let due =
            origin + Duration::from_secs_f64((i + 1) as f64 * block_frames as f64 / rate as f64);
        if let Some(remaining) = due.checked_duration_since(Instant::now()) {
            std::thread::sleep(remaining);
        }
    }
    let cpu = process_cpu_seconds() - cpu_start;
    let wall = wall_start.elapsed().as_secs_f64();
    if enabled {
        assert!(finite_polls >= 280);
        assert!(pre.release_pre_sharp(token));
    }
    pre.shutdown();
    runtime.shutdown_and_join();
    (cpu, wall, finite_polls)
}
#[cfg(unix)]
#[test]
#[ignore = "isolated release-mode realtime optional-core CPU acceptance; run alone"]
fn pre_local_cpu_increment_uses_process_cpu_not_wall_time() {
    // Reversed order avoids silently selecting only a favourable warm-cache run.
    let mut deltas = Vec::new();
    for local_first in [false, true] {
        let first = measured_run(local_first);
        let second = measured_run(!local_first);
        let (baseline, local) = if local_first {
            (second, first)
        } else {
            (first, second)
        };
        let baseline_pct = 100.0 * baseline.0 / baseline.1;
        let local_pct = 100.0 * local.0 / local.1;
        let delta = local_pct - baseline_pct;
        eprintln!("PRE_SHARP_CORE_CPU baseline_cpu_s={:.6} baseline_wall_s={:.6} local_cpu_s={:.6} local_wall_s={:.6} baseline_one_core_pct={:.4} local_one_core_pct={:.4} delta_one_core_percentage_points={:.4} finite_polls={}", baseline.0, baseline.1, local.0, local.1, baseline_pct, local_pct, delta, local.2);
        deltas.push(delta);
    }
    assert!(deltas.iter().all(|delta| delta.is_finite() && *delta <= 5.0), "PRE own SHARP exceeds optional-core 5% gate: {deltas:?}; actual editor/DAW measurement is still required");
}
