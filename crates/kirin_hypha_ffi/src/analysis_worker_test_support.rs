//! Callback barriers for real analysis and exchange workers; wall time is only a watchdog.
use super::*;
use std::{
    thread,
    time::{Duration, Instant},
};

pub(crate) fn wait_until(label: &str, mut ready: impl FnMut() -> bool) {
    let deadline = Instant::now() + Duration::from_secs(10);
    while !ready() {
        assert!(
            Instant::now() < deadline,
            "worker completion watchdog: {label}"
        );
        thread::sleep(Duration::from_millis(1));
    }
}

pub(crate) fn analysis(engine: &KirinHyphaEngine) {
    let runtime = &engine.spectrum_runtime;
    let target = runtime.stats().pushed_blocks;
    wait_until("analysis publication", || {
        runtime.completed_blocks_for_test() >= target
    });
    // dropped_blocks also counts deliberate clock-proof rejection. It is not a queue-overrun
    // counter. The accepted descriptor target above is independent of those rejected callbacks.
}

pub(crate) fn exchange(engine: &KirinHyphaEngine) {
    if let Some(ticket) = engine.spectrum.request_exchange_tick_for_test() {
        wait_until("exchange publication", || {
            engine.spectrum.exchange_tick_completed_for_test(ticket)
        });
    }
}

pub(crate) fn view(
    engine: &KirinHyphaEngine,
) -> kirin_measure::spectrum_exchange::SpectrumViewSnapshot {
    let mut result = None;
    wait_until("coherent view", || {
        result = engine.spectrum.try_view();
        result.is_some()
    });
    result.unwrap()
}

#[test]
fn completion_waits_for_publication_even_when_the_worker_is_delayed() {
    use kirin_measure::{channel_layout::ChannelLayout, spectrum_runtime::SpectrumRuntime};
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::mono());
    runtime.pause_worker_for_test(true);
    assert!(runtime.set_enabled(true));
    let audio = vec![0.125; 6400];
    assert!(runtime.push_block_from_audio(&audio, 1, Some(0)));
    wait_until("injected worker pause", || {
        runtime.worker_entered_for_test()
    });
    assert_eq!(runtime.completed_blocks_for_test(), 0);
    assert_eq!(runtime.stats().analyzed_frames, 0);
    runtime.pause_worker_for_test(false);
    wait_until("real publication", || {
        runtime.completed_blocks_for_test() == 1
    });
    let history = runtime
        .try_history()
        .expect("publication lock released before receipt");
    assert!(history
        .newest()
        .is_some_and(|f| f.presentation_end_samples == 6400));
    runtime.shutdown_and_join();
}
