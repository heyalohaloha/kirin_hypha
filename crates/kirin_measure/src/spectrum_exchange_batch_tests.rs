use super::*;
use crate::spectrum::{
    SpectrumChannelMode, SpectrumFrame, SPECTRUM_BAND_COUNT, SPECTRUM_FFT_SIZE,
    SPECTRUM_SCHEMA_VERSION, SPECTRUM_WINDOW_SIZE,
};
use crate::SpectrumRuntime;
use std::sync::Arc;

fn frame(index: i64, value: f32) -> SpectrumFrame {
    SpectrumFrame {
        schema_version: SPECTRUM_SCHEMA_VERSION,
        sample_rate: 48_000,
        aperture_samples: SPECTRUM_WINDOW_SIZE as u32,
        fft_size: SPECTRUM_FFT_SIZE as u32,
        band_count: SPECTRUM_BAND_COUNT as u16,
        presentation_end_samples: index * 1_600,
        generation: 7,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        min_hz: 10.0,
        max_hz: 22_000.0,
        dbfs: [value; SPECTRUM_BAND_COUNT],
    }
}

#[test]
fn delayed_exchange_preserves_all_exact_frames_without_inventing_gaps() {
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    let mut pre = SpectrumHistory::with_capacity();
    let mut post = SpectrumHistory::with_capacity();
    for i in 1..=4 {
        post.push(frame(i, -12.0));
    }
    for i in [1, 2, 4] {
        pre.push(frame(i, -18.0));
    }
    let joined = exact_spectrum_differences(&post, &pre);
    assert_eq!(joined.len(), 3);
    coordinator.store_spectrum_sequence(&joined, Some(&post), false);
    let first = coordinator.try_view().unwrap();
    let endpoints = |view: &crate::SpectrumViewSnapshot| {
        view.spectrum_timeline
            .frames()
            .map(|frame| frame.presentation_end_samples)
            .collect::<Vec<_>>()
    };
    assert_eq!(endpoints(&first), vec![1_600, 3_200, 6_400]);
    post.push(frame(5, -11.0));
    pre.push(frame(5, -18.0));
    coordinator.store_spectrum_sequence(
        &exact_spectrum_differences(&post, &pre),
        Some(&post),
        false,
    );
    let second = coordinator.try_view().unwrap();
    assert_eq!(endpoints(&second), vec![1_600, 3_200, 6_400, 8_000]);
    assert_eq!(second.difference.unwrap().presentation_end_samples, 8_000);
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn recovery_batch_never_replays_retained_frames_from_before_a_seek() {
    let mut pre = SpectrumHistory::with_capacity();
    let mut post = SpectrumHistory::with_capacity();
    for i in [100, 101, 2, 3, 4] {
        post.push(frame(i, -12.0));
    }
    for i in [100, 101, 2, 3, 5] {
        pre.push(frame(i, -18.0));
    }
    let endpoints = |pre: &SpectrumHistory, post: &SpectrumHistory| {
        exact_spectrum_differences(post, pre)
            .iter()
            .map(|f| f.presentation_end_samples)
            .collect::<Vec<_>>()
    };
    assert_eq!(endpoints(&pre, &post), vec![3_200, 4_800]);
    pre.push(frame(1, -16.0)); // Only PRE has entered the next run.
    assert!(endpoints(&pre, &post).is_empty());
    post.push(frame(1, -10.0));
    assert_eq!(endpoints(&pre, &post), vec![1_600]);
}
