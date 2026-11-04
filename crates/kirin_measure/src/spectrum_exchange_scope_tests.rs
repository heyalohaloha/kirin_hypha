//! LOW on the exchange: the wire, the exact pair, and the edge between FULL and LOW.

use super::integration_tests::{push_stereo_pair_in_blocks, serve_pre, EVENTUAL};
use super::*;
use crate::channel_layout::ChannelLayout;
use crate::spectrum::{SpectrumAnalyzer, SpectrumFrame};

fn low_frame(end: i64) -> SpectrumFrame {
    let mut analyzer = SpectrumAnalyzer::new_for_scope(48_000, SpectrumScope::Low).unwrap();
    let samples = vec![0.1; analyzer.aperture_samples()];
    let mut frame = analyzer
        .analyze(&samples, Some(&samples), end, 7)
        .expect("analysis");
    frame.view = crate::channel_layout::SpectrumView::Lr.to_abi();
    frame
}

fn encoded(frame: SpectrumFrame) -> Vec<u8> {
    let mut history = SpectrumHistory::with_capacity();
    history.push(frame);
    encode_snapshot(Uuid::new_v4(), SpectrumClockKind::Presentation, &history)
}

/// The offset of the first frame's scope byte: header (44) + end + generation + mode, channels, view.
const FIRST_SCOPE_BYTE: usize = 44 + 8 + 8 + 3;

#[test]
fn the_wire_carries_low_and_a_mislabelled_frame_is_refused() {
    let frame = low_frame(16_384);
    let bytes = encoded(frame.clone());
    assert_eq!(bytes[FIRST_SCOPE_BYTE], SpectrumScope::Low as u8);
    let decoded = decode_snapshot(&bytes).expect("LOW decodes");
    assert_eq!(decoded.history.newest(), Some(&frame));

    let mut mislabelled = bytes;
    mislabelled[FIRST_SCOPE_BYTE] = SpectrumScope::Full as u8;
    assert!(decode_snapshot(&mislabelled).is_none());
}

#[test]
fn a_full_frame_on_the_wire_is_byte_for_byte_what_it_was() {
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let samples = vec![0.1; analyzer.aperture_samples()];
    let frame = analyzer
        .analyze(&samples, Some(&samples), 4_800, 7)
        .unwrap();
    let bytes = encoded(frame);
    assert_eq!(bytes[FIRST_SCOPE_BYTE], 0);
    assert!(decode_snapshot(&bytes).is_some());
}

#[test]
fn a_full_history_cloned_before_the_edge_does_not_come_back_after_it() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    coordinator.set_post_visible(true);
    let mut stale = SpectrumHistory::with_capacity();
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let samples = vec![0.1; analyzer.aperture_samples()];
    stale.push(
        analyzer
            .analyze(&samples, Some(&samples), 4_800, 1)
            .unwrap(),
    );

    assert!(coordinator.set_post_spectrum_scope(SpectrumScope::Low));
    coordinator.store_spectrum_view(SpectrumViewStatus::NoPair, None, Some(&stale));

    let view = coordinator.try_view().unwrap();
    assert!(view.post_spectrum.is_none());
    assert_eq!(view.post_spectrum_history.frames().count(), 0);
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn a_full_history_from_before_a_round_trip_does_not_come_back_either() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    coordinator.set_post_visible(true);
    let mut analyzer = SpectrumAnalyzer::new(48_000).unwrap();
    let samples = vec![0.1; analyzer.aperture_samples()];
    // Generation 1: the selection this runtime started with, and the one it returns to in scope.
    let frame = analyzer.analyze(&samples, Some(&samples), 4_800, 1);
    let mut stale = SpectrumHistory::with_capacity();
    stale.push(frame.unwrap());

    assert!(coordinator.set_post_spectrum_scope(SpectrumScope::Low));
    assert!(coordinator.set_post_spectrum_scope(SpectrumScope::Full));
    coordinator.store_spectrum_view(SpectrumViewStatus::NoPair, None, Some(&stale));
    assert!(coordinator.try_view().unwrap().post_spectrum.is_none());
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn the_edge_retires_the_session_a_running_tick_has_cloned() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    coordinator.set_post_visible(true);
    let session = coordinator.new_post_session();
    let cloned_id = session.request_id;
    *coordinator.post_session.lock().unwrap() = Some(session);

    assert!(coordinator.set_post_spectrum_scope(SpectrumScope::Low));
    assert!(coordinator.set_post_spectrum_scope(SpectrumScope::Full));
    let current = coordinator.post_session.lock().unwrap().clone().unwrap();
    assert_ne!(current.request_id, cloned_id);
    assert!(current.last_renewed.is_none());
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

fn expired_session(coordinator: &SpectrumCoordinator, scope: SpectrumScope) -> PostSession {
    let mut session = coordinator.new_post_session();
    session.analysis_mode = AnalysisViewMode::Spectrum;
    session.spectrum_scope = scope;
    session.started_at = Some(Instant::now() - WARMUP_LIMIT);
    session
}

#[test]
fn a_low_window_that_never_fills_stays_a_wait_while_other_gaps_stay_unavailable() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    assert!(runtime.set_enabled(true));
    assert!(runtime.set_spectrum_scope(SpectrumScope::Low));
    let status = |session: &mut PostSession| {
        store_joined_spectrum(&coordinator, session, Instant::now(), None, None);
        coordinator.try_view().unwrap().status
    };

    assert!(!runtime.spectrum_window_short(), "no audio has arrived");
    let mut low = expired_session(&coordinator, SpectrumScope::Low);
    assert_eq!(status(&mut low), SpectrumViewStatus::Unavailable);

    let samples = vec![0.1_f32; 8_192 * 2];
    assert!(runtime.push_block_from_audio(&samples, 2, Some(0)));
    assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
    assert!(runtime.spectrum_window_short());
    assert_eq!(status(&mut low), SpectrumViewStatus::WarmingUp);

    assert!(runtime.set_spectrum_scope(SpectrumScope::Full));
    let mut full = expired_session(&coordinator, SpectrumScope::Full);
    assert_eq!(status(&mut full), SpectrumViewStatus::Unavailable);
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn a_clock_renewed_at_each_wrap_does_not_interrupt_the_wait_for_the_low_window() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
    assert!(runtime.set_enabled(true));
    assert!(runtime.set_spectrum_scope(SpectrumScope::Low));
    let feed = |start: i64| {
        let samples = vec![0.1_f32; 8_192 * 2];
        assert!(runtime.push_block_from_audio(&samples, 2, Some(start)));
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
    };
    let after_renewal = || {
        coordinator.reset_spectrum_clock_view(runtime.spectrum_clock_revision());
        coordinator.try_view().unwrap().status
    };

    feed(0);
    assert_eq!(after_renewal(), SpectrumViewStatus::WarmingUp);
    feed(8_192);
    assert!(!runtime.spectrum_window_short());
    assert_eq!(after_renewal(), SpectrumViewStatus::Unavailable);
    coordinator.shutdown();
    runtime.shutdown_and_join();
}

#[test]
fn low_pairs_only_with_low_and_returns_to_full_end_to_end() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    let post_runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    post.set_post_visible(true);
    post.post_tick("post", Some(target.clone()));
    assert!(serve_pre(&pre, &post, &target));
    assert!(post.set_post_spectrum_scope(SpectrumScope::Low));
    assert!(serve_pre(&pre, &post, &target));
    assert_eq!(pre_runtime.spectrum_scope(), SpectrumScope::Low);

    let frames = 19_200_usize;
    let tone = |index: usize| (std::f32::consts::TAU * 60.0 * index as f32 / 48_000.0).sin();
    let pre_samples: Vec<f32> = (0..frames).flat_map(|i| [tone(i), tone(i)]).collect();
    let post_samples: Vec<f32> = pre_samples.iter().map(|sample| sample * 0.5).collect();
    push_stereo_pair_in_blocks(&pre_runtime, &post_runtime, &pre_samples, &post_samples);

    let deadline = Instant::now() + EVENTUAL;
    let mut paired = None;
    while Instant::now() < deadline && paired.is_none() {
        assert!(serve_pre(&pre, &post, &target));
        post.post_tick("post", Some(target.clone()));
        paired = post.try_view().and_then(|view| view.difference);
        std::thread::yield_now();
    }
    let difference = paired.expect("LOW frames pair exactly");
    assert_eq!(difference.aperture_samples, 16_384);
    assert_eq!(difference.min_hz, crate::spectrum::SPECTRUM_LOW_MIN_HZ);
    assert_eq!(difference.max_hz, crate::spectrum::SPECTRUM_LOW_MAX_HZ);
    assert!(difference
        .display_db
        .iter()
        .any(|db| (db + 6.02).abs() < 0.2));

    // Back to FULL: LOW leaves nothing behind on either side.
    assert!(post.set_post_spectrum_scope(SpectrumScope::Full));
    assert!(post.try_view().unwrap().difference.is_none());
    assert!(serve_pre(&pre, &post, &target));
    assert_eq!(pre_runtime.spectrum_scope(), SpectrumScope::Full);
    assert_eq!(
        post_runtime.try_history().map(|h| h.frames().count()),
        Some(0)
    );

    post.shutdown();
    pre.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}
