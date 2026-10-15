use super::*;
use std::time::{Duration, Instant};

fn poll_published_spectrum(
    engine: &KirinHyphaEngine,
    handle: *mut KirinHyphaEngine,
    initial: KirinSpectrumView,
    require_post_data: bool,
) -> KirinSpectrumView {
    let deadline = Instant::now() + Duration::from_secs(3);
    let mut view = initial;
    loop {
        assert!(Instant::now() < deadline, "spectrum publication watchdog");
        // A false C poll is a skipped presentation tick, not a failed publication.
        if unsafe { kirin_hypha_poll_spectrum(handle, &mut view) }
            && (!require_post_data || view.post_has_data == 1)
        {
            assert!(Instant::now() < deadline, "spectrum publication watchdog");
            return view;
        }
        let ticket = engine
            .spectrum
            .request_exchange_tick_for_test()
            .expect("active POST spectrum has an exchange worker");
        let remaining = deadline.saturating_duration_since(Instant::now());
        assert!(!remaining.is_zero(), "spectrum publication watchdog");
        assert!(
            engine
                .spectrum
                .wait_exchange_tick_for_test(ticket, remaining),
            "spectrum publication watchdog"
        );
    }
}

#[test]
fn shipping_ffi_never_returns_the_previous_channel_snapshot_after_selection() {
    let mut engine = post_engine(ChannelLayout::stereo());
    let handle = engine.as_mut() as *mut KirinHyphaEngine;
    assert!(unsafe { kirin_hypha_set_spectrum_visible(handle, true) });
    engine.spectrum.service_post_endpoint("post", None, "Mix");

    for block in 0..40_i64 {
        let start = block * 256;
        let samples = (0..256)
            .flat_map(|offset| {
                let phase = std::f32::consts::TAU * 1_000.0 * (start + offset) as f32 / 48_000.0;
                let sample = phase.sin() * 0.25;
                [sample, sample]
            })
            .collect::<Vec<_>>();
        engine.note_capture_window_with_presentation(
            true,
            start,
            256,
            CaptureClockSource::AudioRenderTimeline,
            PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(0),
            },
            false,
        );
        engine.push_samples(&samples, 2);
        crate::optional_analysis_clock::test_support::analysis(&engine);
    }

    let before = poll_published_spectrum(&engine, handle, empty_c_spectrum(), true);
    assert_eq!(before.post_has_data, 1);
    assert_eq!(before.channel_mode, KIRIN_SPECTRUM_CHANNEL_LR);

    assert!(unsafe { kirin_hypha_set_spectrum_channel_mode(handle, KIRIN_SPECTRUM_CHANNEL_MID) });
    // Inspect the first successful poll; never wait for the desired channel/status to appear.
    let after = poll_published_spectrum(&engine, handle, before, false);
    assert_eq!(after.status, KIRIN_SPECTRUM_WARMING_UP);
    assert_eq!(after.channel_mode, KIRIN_SPECTRUM_CHANNEL_MID);
    assert_eq!(after.has_data, 0);
    assert_eq!(after.post_has_data, 0);
}
