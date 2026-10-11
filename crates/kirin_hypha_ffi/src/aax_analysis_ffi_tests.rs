use super::*;
use kirin_measure::channel_layout::ChannelLayout;

fn engine(mode: AnalysisViewMode, post: bool) -> Box<KirinHyphaEngine> {
    let engine = Box::new(KirinHyphaEngine::new(48_000, ChannelLayout::stereo()));
    engine.aax_analysis.configure(true, post);
    engine.spectrum_runtime.set_analysis_mode(mode);
    engine.spectrum_runtime.set_enabled(true);
    engine
}

fn stage(engine: &KirinHyphaEngine, audio: &[f32]) {
    engine.note_capture_window_with_clocks(
        true,
        0,
        (audio.len() / 2) as u64,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples::default(),
        AuxiliaryClockSamples {
            source: AuxiliaryClockSource::AaxNative,
            samples: Some(960_000),
        },
        false,
    );
    engine.push_samples(audio, 2);
}

fn push(
    engine: &mut KirinHyphaEngine,
    audio: &[f32],
    local: bool,
    paired: bool,
    revision: u64,
) -> bool {
    unsafe {
        optional_analysis_clock::kirin_hypha_push_aax_analysis(
            engine,
            audio.as_ptr(),
            audio.len() / 2,
            2,
            local,
            960_000,
            revision,
            paired,
            958_976,
            revision,
        )
    }
}

#[test]
fn aax_absolute_keeps_the_project_timeline_route_and_never_waits_for_engine_proof() {
    let mut engine = engine(AnalysisViewMode::Absolute, true);
    let audio = [0.1; 512];
    stage(&engine, &audio);
    // The ordinary ingress already admitted the block on the project timeline.
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 1);
    assert!(!engine.spectrum_runtime.output_latency_unavailable());
    // Nothing was staged for the deferred route, with or without a proof.
    assert!(!push(&mut engine, &audio, true, true, 1));
    assert!(!push(&mut engine, &audio, false, false, 1));
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 1);
    assert_eq!(engine.spectrum_runtime.stats().dropped_blocks, 0);
}

#[test]
fn aax_sharp_post_requires_pair_proof_and_pre_publishes_its_local_clock() {
    let audio = [0.1; 512];
    for post in [true, false] {
        let mut engine = engine(AnalysisViewMode::Perceptual, post);
        stage(&engine, &audio);
        assert_eq!(push(&mut engine, &audio, true, false, 1), !post);
        if post {
            assert!(!engine.spectrum_runtime.output_latency_unavailable());
            stage(&engine, &audio);
            assert!(push(&mut engine, &audio, true, true, 1));
        }
        assert!(!engine.spectrum_runtime.output_latency_unavailable());
    }
}

#[test]
fn aax_sharp_staged_once_and_missing_or_exhausted_proofs_wait_without_data() {
    let mut engine = engine(AnalysisViewMode::Perceptual, true);
    let audio = [0.1; 512];
    for (valid, revision) in [(false, 1), (true, 0), (true, 1_u64 << 62), (true, u64::MAX)] {
        stage(&engine, &audio);
        assert!(!push(&mut engine, &audio, valid, valid, revision));
        assert!(!engine.spectrum_runtime.output_latency_unavailable());
    }
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 0);
    stage(&engine, &audio);
    assert!(push(&mut engine, &audio, true, true, (1_u64 << 62) - 1));
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 1);
    // The staged cardinality is consumed by the first call, proven or not.
    assert!(!push(&mut engine, &audio, true, true, (1_u64 << 62) - 1));
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 1);
}

#[test]
fn aax_sharp_proof_gaps_drop_once_and_recover_for_both_roles() {
    let audio = [0.1; 512];
    for post in [false, true] {
        let mut engine = engine(AnalysisViewMode::Perceptual, post);
        for revision in [1, 2] {
            stage(&engine, &audio);
            let before = engine.spectrum_runtime.stats();
            assert!(!push(&mut engine, &audio, false, false, revision));
            let after = engine.spectrum_runtime.stats();
            assert_eq!(after.pushed_blocks, before.pushed_blocks);
            assert_eq!(after.dropped_blocks, before.dropped_blocks + 1);
            assert!(!engine.spectrum_runtime.output_latency_unavailable());
            stage(&engine, &audio);
            assert!(push(&mut engine, &audio, true, true, revision));
        }
    }
}

#[test]
fn aax_late_audio_transaction_rejects_wrong_cardinality_and_nulls() {
    let mut engine = engine(AnalysisViewMode::Perceptual, true);
    let audio = [0.1; 512];
    stage(&engine, &audio);
    assert!(!push(&mut engine, &audio[..256], true, true, 1));
    assert!(!push(&mut engine, &audio, true, true, 1)); // failed transaction is consumed
    stage(&engine, &audio);
    assert!(!unsafe {
        optional_analysis_clock::kirin_hypha_push_aax_analysis(
            engine.as_mut(),
            std::ptr::null(),
            256,
            2,
            true,
            0,
            1,
            true,
            0,
            1,
        )
    });
    assert!(!unsafe {
        optional_analysis_clock::kirin_hypha_push_aax_analysis(
            std::ptr::null_mut(),
            std::ptr::null(),
            0,
            0,
            false,
            0,
            0,
            false,
            0,
            0,
        )
    });
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 0);
}

#[test]
fn aax_route_does_not_replace_freq_live_or_the_unconfigured_au_vst3_route() {
    let audio = [0.1; 512];
    let mut engine = engine(AnalysisViewMode::Spectrum, true);
    stage(&engine, &audio);
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 1);
    assert!(!push(&mut engine, &audio, true, true, 1));
    // LIVE stays on the project timeline while the AAX route is configured.
    engine
        .spectrum_runtime
        .set_analysis_mode(AnalysisViewMode::Absolute);
    stage(&engine, &audio);
    assert!(!engine.spectrum_runtime.output_latency_unavailable());
    assert!(!push(&mut engine, &audio, true, true, 1));
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 2);
    // An unconfigured route (AU / VST3) keeps the ordinary Perceptual latency refusal.
    engine.aax_analysis.configure(false, true);
    engine
        .spectrum_runtime
        .set_analysis_mode(AnalysisViewMode::Perceptual);
    stage(&engine, &audio);
    assert!(engine.spectrum_runtime.output_latency_unavailable());
    assert!(!push(&mut engine, &audio, true, true, 1));
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 2);
    engine
        .spectrum_runtime
        .set_analysis_mode(AnalysisViewMode::Absolute);
    assert!(!engine.spectrum_runtime.output_latency_unavailable());
    stage(&engine, &audio);
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 3);
    // The configured AAX route never raises that refusal for Perceptual; it waits for proof.
    engine.aax_analysis.configure(true, true);
    engine
        .spectrum_runtime
        .set_analysis_mode(AnalysisViewMode::Perceptual);
    stage(&engine, &audio);
    assert!(!engine.spectrum_runtime.output_latency_unavailable());
    assert_eq!(engine.spectrum_runtime.stats().pushed_blocks, 3);
    assert!(!push(&mut engine, &audio, true, false, 1));
    assert!(!engine.spectrum_runtime.output_latency_unavailable());
}
