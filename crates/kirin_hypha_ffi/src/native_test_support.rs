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
}
