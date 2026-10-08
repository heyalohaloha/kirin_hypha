//! Optional analysis clock policy; independent from Record and capture coordinates.
use super::{CaptureClockSource, PendingCaptureWindow, PresentationLatencySource};

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
