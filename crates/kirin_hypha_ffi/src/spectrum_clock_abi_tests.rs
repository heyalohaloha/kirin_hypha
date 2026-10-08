use super::*;
use crate::optional_analysis_clock::{spectrum_input_clock, spectrum_presentation_start};
use kirin_measure::spectrum_runtime::SpectrumInputClock;

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

#[test]
fn optional_spectrum_clock_preserves_local_authority_without_claiming_latency() {
    let local = PendingCaptureWindow {
        position_valid: true,
        position_samples: 9_600,
        num_frames: 480,
        clock_source: CaptureClockSource::ProjectTimeline,
        presentation_latency: PresentationLatencySamples::default(),
        auxiliary: AuxiliaryClockSamples::default(),
        force_new_epoch: false,
    };
    assert_eq!(
        spectrum_input_clock(local),
        Some(SpectrumInputClock::LocalProject(9_600))
    );
    assert_eq!(spectrum_presentation_start(local), None);
    assert_eq!(
        spectrum_input_clock(PendingCaptureWindow {
            clock_source: CaptureClockSource::AudioRenderTimeline,
            ..local
        }),
        Some(SpectrumInputClock::LocalRender(9_600))
    );
    assert_eq!(
        spectrum_input_clock(PendingCaptureWindow {
            clock_source: CaptureClockSource::Unknown,
            ..local
        }),
        None
    );
    assert_eq!(
        spectrum_input_clock(PendingCaptureWindow {
            position_valid: false,
            ..local
        }),
        None
    );
    for source in [
        PresentationLatencySource::Vst3,
        PresentationLatencySource::AudioUnitV2,
    ] {
        let aligned = PendingCaptureWindow {
            presentation_latency: PresentationLatencySamples {
                source,
                input: None,
                output: Some(2_048),
            },
            ..local
        };
        assert_eq!(
            spectrum_input_clock(aligned),
            Some(SpectrumInputClock::Presentation {
                start_samples: 11_648,
                source,
                output_latency_samples: 2_048,
            })
        );
        assert_eq!(
            spectrum_input_clock(PendingCaptureWindow {
                position_samples: i64::MAX,
                ..aligned
            }),
            None
        );
    }
}
