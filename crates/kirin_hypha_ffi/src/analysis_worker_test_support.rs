//! Callback barriers for real analysis and exchange workers; wall time is only a watchdog.
use super::*;
use std::time::{Duration, Instant};

pub(crate) fn analysis(engine: &KirinHyphaEngine) {
    let runtime = &engine.spectrum_runtime;
    assert!(
        runtime.wait_for_input_for_test(Duration::from_secs(10)),
        "analysis publication watchdog"
    );
    // dropped_blocks also counts deliberate clock-proof rejection. It is not a queue-overrun
    // counter. The accepted descriptor target above is independent of those rejected callbacks.
}

pub(crate) fn exchange(engine: &KirinHyphaEngine) {
    if let Some(ticket) = engine.spectrum.request_exchange_tick_for_test() {
        assert!(
            engine
                .spectrum
                .wait_exchange_tick_for_test(ticket, Duration::from_secs(10)),
            "exchange publication watchdog"
        );
    }
}

pub(crate) fn view(
    engine: &KirinHyphaEngine,
) -> kirin_measure::spectrum_exchange::SpectrumViewSnapshot {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(view) = engine.spectrum.try_view() {
            return view;
        }
        assert!(Instant::now() < deadline, "coherent view watchdog");
        // A contended read waits for a completed publication instead of a short timer.
        let ticket = engine
            .spectrum
            .request_exchange_tick_for_test()
            .expect("contended exchange has a worker");
        assert!(
            engine.spectrum.wait_exchange_tick_for_test(
                ticket,
                deadline.saturating_duration_since(Instant::now())
            ),
            "coherent publication watchdog"
        );
    }
}

#[test]
fn completion_waits_for_publication_even_when_the_worker_is_delayed() {
    use kirin_measure::{channel_layout::ChannelLayout, spectrum_runtime::SpectrumRuntime};
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::mono());
    runtime.pause_worker_for_test(true);
    assert!(runtime.set_enabled(true));
    let audio = vec![0.125; 6400];
    assert!(runtime.push_block_from_audio(&audio, 1, Some(0)));
    assert!(runtime.wait_worker_entered_for_test(Duration::from_secs(10)));
    assert_eq!(runtime.completed_blocks_for_test(), 0);
    assert_eq!(runtime.stats().analyzed_frames, 0);
    runtime.pause_worker_for_test(false);
    assert!(runtime.wait_for_input_for_test(Duration::from_secs(10)));
    let history = runtime
        .try_history()
        .expect("publication lock released before receipt");
    assert!(history
        .newest()
        .is_some_and(|f| f.presentation_end_samples == 6400));
    runtime.shutdown_and_join();
}
