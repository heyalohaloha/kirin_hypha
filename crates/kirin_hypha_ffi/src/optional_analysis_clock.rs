//! Optional analysis clock policy; independent from Record and capture coordinates.
use super::{CaptureClockSource, PendingCaptureWindow, PresentationLatencySource};
use kirin_measure::spectrum_runtime::SpectrumInputClock;

/// Missing optional latency retains project coordinates; FREQ pairs only the same clock kind.
#[inline]
pub(super) fn spectrum_input_clock(clock: PendingCaptureWindow) -> Option<SpectrumInputClock> {
    if !clock.position_valid {
        return None;
    }
    if matches!(
        clock.presentation_latency.source,
        PresentationLatencySource::Vst3 | PresentationLatencySource::AudioUnitV2
    ) && clock.presentation_latency.output.is_some()
    {
        return Some(SpectrumInputClock::Presentation {
            start_samples: spectrum_presentation_start(clock)?,
            source: clock.presentation_latency.source,
            output_latency_samples: clock.presentation_latency.output?,
        });
    }
    match clock.clock_source {
        CaptureClockSource::ProjectTimeline => {
            Some(SpectrumInputClock::LocalProject(clock.position_samples))
        }
        CaptureClockSource::AudioRenderTimeline => {
            Some(SpectrumInputClock::LocalRender(clock.position_samples))
        }
        _ => None,
    }
}

#[inline]
pub(super) fn spectrum_presentation_start(clock: PendingCaptureWindow) -> Option<i64> {
    if !clock.position_valid
        || !matches!(
            clock.presentation_latency.source,
            PresentationLatencySource::Vst3 | PresentationLatencySource::AudioUnitV2
        )
    {
        return None;
    }
    clock
        .presentation_latency
        .output
        .and_then(|latency| clock.position_samples.checked_add(i64::from(latency)))
}

/// Clock for the POST-only on-demand ATTACK worker.
///
/// Prefer the host's output-presentation clock when the optional VST3/AU extension is present.
/// Studio Pro can omit that optional callback while still supplying the exact project sample
/// position on every rendered block. The ATTACK worker may use that producer clock because it
/// displays only relative POST event positions; it does not perform the public PRE/POST join.
/// Unknown or invalid producer clocks remain fail-closed.
#[inline]
pub(super) fn attack_timeline_start(clock: PendingCaptureWindow) -> Option<i64> {
    spectrum_presentation_start(clock).or_else(|| {
        (clock.position_valid
            && matches!(
                clock.clock_source,
                CaptureClockSource::ProjectTimeline | CaptureClockSource::AudioRenderTimeline
            ))
        .then_some(clock.position_samples)
    })
}
