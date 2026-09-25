use super::*;

#[test]
fn presentation_alignment_requires_known_wrapper_output_latency() {
    let exact = PendingCaptureWindow {
        position_valid: true,
        position_samples: 9_600,
        num_frames: 480,
        clock_source: CaptureClockSource::ProjectTimeline,
        presentation_latency: PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(2_048),
        },
        auxiliary: AuxiliaryClockSamples::default(),
        force_new_epoch: false,
    };
    assert_eq!(spectrum_presentation_start(exact), Some(11_648));
    assert_eq!(
        spectrum_presentation_start(PendingCaptureWindow {
            presentation_latency: PresentationLatencySamples::default(),
            ..exact
        }),
        None
    );
    assert_eq!(
        spectrum_presentation_start(PendingCaptureWindow {
            position_valid: false,
            ..exact
        }),
        None
    );
}
