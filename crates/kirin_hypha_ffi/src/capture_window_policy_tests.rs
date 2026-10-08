use super::*;

#[test]
fn attack_clock_policy_retires_old_hits_without_admitting_audio_or_record_frames() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    let runtime = engine.attack_runtime.as_ref().unwrap();
    let stage = |position, source, input, output, force| {
        engine.note_capture_window_with_clocks(
            true,
            position,
            32,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples {
                source,
                input: Some(input),
                output: Some(output),
            },
            AuxiliaryClockSamples::default(),
            force,
        );
        let descriptor = engine.take_pending_capture_window(32).unwrap();
        assert_eq!(descriptor.position_samples, position);
        assert_eq!(descriptor.presentation_latency.output, Some(output));
        assert_eq!(descriptor.force_new_epoch, force);
        assert_eq!(engine.record_take_tracker.captured_frames_total(), 0);
        assert_eq!(runtime.stats().pushed_blocks, 0);
        runtime.fixture_generation()
    };
    let initial = runtime.fixture_generation();
    let first = stage(0, PresentationLatencySource::Vst3, 0, 256, false);
    assert_ne!(first, initial);
    assert_eq!(
        stage(32, PresentationLatencySource::Vst3, 0, 256, false),
        first
    );
    // Input-only latency is not a change of the presentation basis.
    assert_eq!(
        stage(64, PresentationLatencySource::Vst3, 64, 256, false),
        first
    );
    let latency = stage(96, PresentationLatencySource::Vst3, 64, 512, false);
    assert_ne!(latency, first);
    let basis = stage(128, PresentationLatencySource::AudioUnitV2, 64, 512, false);
    assert_ne!(basis, latency);
    let record_boundary = stage(160, PresentationLatencySource::AudioUnitV2, 64, 512, true);
    assert_ne!(record_boundary, basis);
    assert_eq!(
        stage(192, PresentationLatencySource::AudioUnitV2, 64, 512, false),
        record_boundary
    );
}
