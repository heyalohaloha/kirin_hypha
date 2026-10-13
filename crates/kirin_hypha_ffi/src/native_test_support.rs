//! Native-fixture observations. This module is absent from shipping static libraries.
use super::KirinHyphaEngine;

/// Observe both analysis queues between callbacks, on their sole producer's thread.
/// This is receipt of input, not successful analysis or a timing certificate.
/// # Safety
/// `handle` must be null or live. Call only on the sole audio producer thread, outside
/// processBlock, with no concurrent producer, prepare, release or destruction.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_test_analysis_input_drained(
    handle: *mut KirinHyphaEngine,
) -> bool {
    let Some(engine) = (unsafe { handle.as_ref() }) else {
        return false;
    };
    unsafe { engine.spectrum_runtime.input_drained_for_test() }
}

#[test]
fn null_is_not_a_receipt() {
    assert!(!unsafe { kirin_hypha_test_analysis_input_drained(std::ptr::null_mut()) });
    assert!(!unsafe { kirin_hypha_test_wait_analysis_input(std::ptr::null_mut(), 0) });
    assert!(!unsafe { kirin_hypha_test_wait_analysis_exchange(std::ptr::null_mut(), 0, false) });
}

/// Wake and await the real analysis worker outside processBlock. Queue receipt alone is
/// insufficient: this returns only after the last accepted descriptor finishes publication.
/// # Safety
/// Same sole-producer/lifetime contract as `kirin_hypha_test_analysis_input_drained`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_test_wait_analysis_input(
    handle: *mut KirinHyphaEngine,
    timeout_millis: u32,
) -> bool {
    let Some(engine) = (unsafe { handle.as_ref() }) else {
        return false;
    };
    engine
        .spectrum_runtime
        .wait_for_input_for_test(std::time::Duration::from_millis(timeout_millis.into()))
}

/// Await one existing exchange worker tick after analysis publication, outside processBlock.
/// # Safety
/// Same lifetime and sole-producer contract as `kirin_hypha_test_analysis_input_drained`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_test_wait_analysis_exchange(
    handle: *mut KirinHyphaEngine,
    timeout_millis: u32,
    await_endpoint: bool,
) -> bool {
    let Some(engine) = (unsafe { handle.as_ref() }) else {
        return false;
    };
    let ticket = engine
        .spectrum
        .request_exchange_tick_for_test()
        .or_else(|| await_endpoint.then(|| engine.spectrum.request_endpoint_tick_for_test()));
    ticket.is_none_or(|ticket| {
        engine.spectrum.wait_exchange_tick_for_test(
            ticket,
            std::time::Duration::from_millis(timeout_millis.into()),
        )
    })
}

#[test]
fn endpoint_receipt_waits_for_real_io_before_optional_analysis_exists() {
    assert!(std::env::var_os("KIRIN_HYPHA_TEST_STORAGE_ROOT").is_some());
    let mut engine = Box::new(KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    ));
    engine.enable_post_writes();
    assert!(unsafe { kirin_hypha_test_wait_analysis_exchange(engine.as_mut(), 5000, true) });
    assert!(!engine.spectrum_runtime.is_enabled());
    let ticket = engine.spectrum.request_endpoint_tick_for_test();
    assert!(engine
        .spectrum
        .wait_exchange_tick_for_test(ticket, std::time::Duration::from_secs(5)));
    assert!(engine
        .spectrum
        .wait_exchange_tick_for_test(ticket, std::time::Duration::ZERO));
}
