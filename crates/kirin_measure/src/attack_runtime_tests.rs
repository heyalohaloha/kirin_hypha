use std::thread;
use std::time::{Duration, Instant};

use super::*;

fn feed(runtime: &AttackRuntime, frames: usize, block: usize, impulse_at: Option<usize>) {
    let mut position = 0_usize;
    while position < frames {
        let count = block.min(frames - position);
        let mut samples = Vec::with_capacity(count * runtime.num_channels());
        for index in 0..count {
            let value = if impulse_at == Some(position + index) {
                0.8
            } else {
                0.0
            };
            for _ in 0..runtime.num_channels() {
                samples.push(value);
            }
        }
        assert!(runtime.push_block_from_audio(
            &samples,
            runtime.num_channels(),
            Some(position as i64),
        ));
        position += count;
        thread::sleep(Duration::from_millis(1));
    }
}

fn wait_for_frames(runtime: &AttackRuntime, minimum: usize) -> AttackHistory {
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if let Some(history) = runtime
            .try_history()
            .filter(|history| history.frames().len() >= minimum)
        {
            return history;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("ATTACK worker did not publish {minimum} frames");
}

#[test]
fn runtime_is_default_off_and_rejects_unsupported_topology() {
    assert!(AttackRuntime::new(48_000, 0).is_err());
    assert!(AttackRuntime::new(48_000, 3).is_err());
    assert!(AttackRuntime::new(32_000, 2).is_err());
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(!runtime.is_enabled());
    assert!(!runtime.push_block_from_audio(&[0.0; 16], 2, Some(0)));
    assert_eq!(
        runtime.stats(),
        AttackRuntimeStats {
            channels: 2,
            ..AttackRuntimeStats::default()
        }
    );
    runtime.shutdown_and_join();
}

#[test]
fn malformed_public_frame_layout_fails_without_integer_overflow() {
    let frame = AttackOdfFrame {
        generation: 1,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: [0; 32],
        window_samples: 2_048,
        hop_samples: 256,
        support_start_samples: i64::MIN,
        support_end_samples: i64::MAX,
        event_sample: 0,
        value: 0.0,
    };
    assert!(!frame.has_valid_layout());
}

#[test]
fn selected_drum_superflux_runs_on_source_zero_grid() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    feed(&runtime, 5_000, 256, Some(3_000));
    let history = wait_for_frames(&runtime, 8);
    let frames = history.frames().copied().collect::<Vec<_>>();
    assert_eq!(frames[0].window_samples, 2_048);
    assert_eq!(frames[0].hop_samples, 256);
    assert_eq!(frames[0].event_sample, 512);
    assert_eq!(frames[0].support_start_samples, -512);
    assert_eq!(frames[0].support_end_samples, 1_536);
    assert!(frames
        .windows(2)
        .all(|pair| pair[1].event_sample - pair[0].event_sample == 256));
    assert!(frames.iter().any(|frame| frame.value > 0.0));
    assert_eq!(frames[0].definition_hash, frames[1].definition_hash);
    assert_eq!(runtime.latest_presentation_end(), Some(5_000));
    assert_eq!(runtime.stats().dropped_blocks, 0);
    assert!(history.waveform().len() >= 8);
    assert!(history
        .waveform()
        .all(AttackWaveformPoint::has_valid_layout));
    runtime.shutdown_and_join();
}

#[test]
fn confirmed_runtime_event_receives_real_perceptual_detail() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    feed(&runtime, 14_000, 256, Some(8_000));
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if let Some(detail) = runtime
            .try_history()
            .and_then(|history| history.details().next_back().copied())
        {
            assert!(detail.has_valid_layout());
            assert_eq!(detail.event.channels, 2);
            assert_eq!(detail.features.context_frames, 4_800);
            assert_eq!(detail.features.attack_frames, 1_440);
            assert!(detail.features.contrast_db > 0.0);
            assert!(detail.features.temporal_centroid_ms.is_some());
            assert_eq!(detail.shape.points.len(), ATTACK_SHAPE_POINT_CAPACITY);
            assert!(detail.shape.points.iter().any(|value| *value > 0.0));
            runtime.shutdown_and_join();
            return;
        }
        thread::sleep(Duration::from_millis(2));
    }
    runtime.shutdown_and_join();
    panic!("ATTACK worker did not attach perceptual detail to the confirmed event");
}

#[test]
fn silence_is_a_valid_zero_trace() {
    let runtime = AttackRuntime::new(44_100, 1).unwrap();
    assert!(runtime.set_enabled(true));
    feed(&runtime, 5_000, 235, None);
    let history = wait_for_frames(&runtime, 8);
    assert!(history.frames().all(|frame| frame.value == 0.0));
    let newest = history.newest().unwrap();
    assert_eq!(newest.window_samples, 1_882);
    assert_eq!(newest.hop_samples, 235);
    assert_eq!(newest.channels, 1);
    runtime.shutdown_and_join();
}

#[test]
fn invalid_ingress_advances_generation_without_joining_old_frames() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    feed(&runtime, 4_000, 256, Some(3_000));
    let first = wait_for_frames(&runtime, 4);
    let first_generation = first.newest().unwrap().generation;

    assert!(!runtime.push_block_from_audio(&[0.0; 32], 1, Some(4_000)));
    assert_eq!(runtime.stats().dropped_blocks, 1);
    let samples = vec![0.0; 5_000 * 2];
    assert!(runtime.push_block_from_audio(&samples, 2, Some(0)));
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if let Some(history) = runtime.try_history() {
            if history
                .newest()
                .is_some_and(|frame| frame.generation > first_generation)
            {
                assert!(history
                    .frames()
                    .all(|frame| frame.generation > first_generation));
                runtime.shutdown_and_join();
                return;
            }
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("ATTACK worker did not restart after ingress generation changed");
}

#[test]
fn backwards_transport_starts_a_new_generation() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    feed(&runtime, 4_000, 256, None);
    let first_generation = wait_for_frames(&runtime, 4).newest().unwrap().generation;

    let samples = vec![0.0; 5_000 * 2];
    assert!(runtime.push_block_from_audio(&samples, 2, Some(0)));
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if let Some(history) = runtime.try_history() {
            if history
                .newest()
                .is_some_and(|frame| frame.generation > first_generation)
            {
                assert!(history
                    .frames()
                    .all(|frame| frame.generation > first_generation));
                runtime.shutdown_and_join();
                return;
            }
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("ATTACK worker joined frames across backwards transport");
}

#[test]
fn disabling_clears_history_and_stops_accepting_audio() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    feed(&runtime, 4_000, 256, None);
    let _ = wait_for_frames(&runtime, 4);
    assert!(runtime.set_enabled(false));
    assert!(runtime
        .try_history()
        .is_some_and(|history| history.newest().is_none()));
    assert!(!runtime.push_block_from_audio(&[0.0; 32], 2, Some(4_000)));
    runtime.shutdown_and_join();
}

#[test]
fn two_192k_slots_process_one_second_without_ingress_drop() {
    let first = AttackRuntime::new(192_000, 2).unwrap();
    let second = AttackRuntime::new(192_000, 2).unwrap();
    assert!(first.set_enabled(true));
    assert!(second.set_enabled(true));
    let samples = vec![0.0; 192_000 * 2];
    assert!(first.push_block_from_audio(&samples, 2, Some(0)));
    assert!(second.push_block_from_audio(&samples, 2, Some(0)));
    let _ = wait_for_frames(&first, 100);
    let _ = wait_for_frames(&second, 100);
    assert_eq!(first.stats().dropped_blocks, 0);
    assert_eq!(second.stats().dropped_blocks, 0);
    first.shutdown_and_join();
    second.shutdown_and_join();
}

#[test]
fn offline_drum_path_reuses_the_selected_runtime_decision() {
    let sample_rate = 44_100;
    let mut samples = vec![0.0; sample_rate as usize];
    samples[4_410] = 0.9;
    let events = analyze_drum_attacks_mono_offline(&samples, sample_rate).unwrap();
    assert_eq!(events.len(), 1);
    assert!((3_940..=4_410).contains(&events[0].event_sample));
    assert!(events[0].has_valid_layout());
}

#[test]
fn offline_drum_path_rejects_empty_nonfinite_and_unsupported_rate() {
    assert_eq!(
        analyze_drum_attacks_mono_offline(&[], 44_100).unwrap_err(),
        "offline ATTACK input is empty"
    );
    assert_eq!(
        analyze_drum_attacks_mono_offline(&[f32::NAN], 44_100).unwrap_err(),
        "offline ATTACK input contains a non-finite sample"
    );
    assert!(analyze_drum_attacks_mono_offline(&[0.0; 1_000], 32_000).is_err());
    assert!(analyze_drum_attacks_interleaved_offline(&[0.0; 3], 44_100, 2).is_err());
}

#[test]
fn offline_drum_path_accepts_stereo_with_the_product_lr_contract() {
    let sample_rate = 44_100;
    let mut samples = vec![0.0; sample_rate as usize * 2];
    samples[4_410 * 2] = 0.9;
    samples[4_410 * 2 + 1] = 0.9;
    let events = analyze_drum_attacks_interleaved_offline(&samples, sample_rate, 2).unwrap();
    assert_eq!(events.len(), 1);
    assert_eq!(events[0].channels, 2);
}
