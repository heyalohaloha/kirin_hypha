//! Local absolute clocks rebuild measurement state at transport cuts without inventing points.
use super::assemblers::AbsoluteAssembler;
use super::*;
use std::time::{Duration, Instant};

fn samples(start: i64, frames: usize, channels: usize, level: f32) -> Vec<f32> {
    (start..start + frames as i64)
        .flat_map(|index| {
            let sample = (std::f32::consts::TAU * 1_000.0 * index as f32 / 48_000.0).sin() * level;
            std::iter::repeat_n(sample, channels)
        })
        .collect()
}

fn fresh_frames(
    start: i64,
    frames: usize,
    channels: usize,
    level: f32,
    generation: u64,
) -> Vec<crate::AbsoluteFrame> {
    let mut assembler = AbsoluteAssembler::new(48_000, channels).unwrap();
    assert!(assembler.begin_block(start, generation));
    let pcm = samples(start, frames, channels, level);
    pcm.chunks_exact(channels)
        .flat_map(|frame| {
            assembler
                .push_frame(frame[0], frame.get(1).copied())
                .unwrap_or_default()
                .to_vec()
        })
        .collect()
}

fn feed_and_wait(
    runtime: &SpectrumRuntime,
    clock: fn(i64) -> SpectrumInputClock,
    start: i64,
    frames: usize,
    level: f32,
) -> crate::AbsoluteTimeline {
    let before = runtime.stats().analyzed_absolute_frames;
    let mut expected = fresh_frames(start, frames, runtime.num_channels(), level, 1);
    assert!(!expected.is_empty());
    for offset in (0..frames).step_by(256) {
        let count = 256.min(frames - offset);
        let position = start + offset as i64;
        assert!(runtime.push_block_from_audio_with_clock(
            &samples(position, count, runtime.num_channels(), level),
            runtime.num_channels(),
            Some(clock(position))
        ));
        thread::sleep(Duration::from_millis(2));
    }
    for frame in &mut expected {
        frame.generation = runtime.selection().generation;
    }
    let deadline = Instant::now() + Duration::from_secs(3);
    loop {
        if runtime.stats().analyzed_absolute_frames >= before + expected.len() as u64 {
            if let Some(history) = runtime
                .try_absolute_history()
                .filter(|history| history.newest() == expected.last())
            {
                assert_eq!(runtime.stats().dropped_blocks, 0);
                assert!(!runtime.output_latency_unavailable());
                assert!(!runtime.presentation_clock_aligned());
                return history;
            }
        }
        assert!(
            Instant::now() < deadline,
            "local facts must match a fresh analyzer after the cut at {start}: actual {:?}, expected {:?}",
            runtime.try_absolute_history().and_then(|history| history.newest().copied()),
            expected.last()
        );
        thread::sleep(Duration::from_millis(2));
    }
}

#[test]
fn local_absolute_loop_and_seek_rebuild_state_and_publish_only_measured_endpoints() {
    for clock in [
        SpectrumInputClock::LocalProject,
        SpectrumInputClock::LocalRender,
    ] {
        for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
            let runtime = SpectrumRuntime::new(48_000, layout);
            assert!(runtime.set_analysis_mode(AnalysisViewMode::Absolute));
            assert!(runtime.set_enabled(true));
            feed_and_wait(&runtime, clock, 0, 9_600, 0.8);
            let looped = feed_and_wait(&runtime, clock, 0, 9_600, 0.1);
            assert_eq!(
                looped.frames().copied().collect::<Vec<_>>(),
                fresh_frames(
                    0,
                    9_600,
                    layout.channel_count(),
                    0.1,
                    looped.newest().unwrap().generation
                )
            );
            // A non-grid short seek resets analyzer state but retains earlier measured points.
            let sought = feed_and_wait(&runtime, clock, 14_417, 14_383, 0.05);
            assert_eq!(
                sought
                    .frames()
                    .map(|frame| frame.presentation_end_samples)
                    .collect::<Vec<_>>(),
                [4_800, 9_600, 24_000, 28_800]
            );
            assert_eq!(sought.newest().unwrap().state_epoch_samples, 19_200);
            let distant = feed_and_wait(&runtime, clock, 336_000, 9_600, 0.0);
            assert_eq!(
                distant.frames().copied().collect::<Vec<_>>(),
                fresh_frames(
                    336_000,
                    9_600,
                    layout.channel_count(),
                    0.0,
                    distant.newest().unwrap().generation
                )
            );
            runtime.shutdown_and_join();
        }
    }
}

#[test]
fn absolute_assembler_does_not_splice_partial_apertures_across_loop_or_seek() {
    for channels in [1, 2] {
        for start in [-4_801, 7_201] {
            let mut assembler = AbsoluteAssembler::new(48_000, channels).unwrap();
            assert!(assembler.begin_block(0, 1));
            for _ in 0..4_799 {
                assert!(assembler
                    .push_frame(0.9, (channels == 2).then_some(0.9))
                    .is_none());
            }
            assert!(assembler.begin_block(start, 1));
            assert_eq!(assembler.take_history_reset_required(), start < 4_799);
            let pcm = samples(start, 14_400, channels, 0.02);
            let actual = pcm
                .chunks_exact(channels)
                .flat_map(|frame| {
                    assembler
                        .push_frame(frame[0], frame.get(1).copied())
                        .unwrap_or_default()
                        .to_vec()
                })
                .collect::<Vec<_>>();
            assert_eq!(actual, fresh_frames(start, 14_400, channels, 0.02, 1));
            assert!(!actual.is_empty());
            assert!(actual.iter().all(crate::AbsoluteFrame::is_valid));
        }
    }
}
