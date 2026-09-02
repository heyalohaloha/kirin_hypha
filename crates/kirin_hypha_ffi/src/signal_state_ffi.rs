//! Signal availability, host activation, and their real-time-safe C ABI.

use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::{load_signal_state, stalled_signal_state, store_signal_state, SignalState};

use super::KirinHyphaEngine;

/// Map the internal enum to the stable C ABI codes: Inactive=0, Active=1, Bypassed=2.
///
/// This is the exact inverse of `KirinHyphaEngine::set_signal_state` for known codes.
#[inline]
fn signal_state_to_abi(state: SignalState) -> u8 {
    match state {
        SignalState::Inactive => 0,
        SignalState::Active => 1,
        SignalState::Bypassed => 2,
    }
}

impl KirinHyphaEngine {
    /// Set the signal state from the stable C ABI code.
    pub fn set_signal_state(&self, abi_state: u8) {
        let state = match abi_state {
            1 => SignalState::Active,
            2 => SignalState::Bypassed,
            _ => SignalState::Inactive,
        };
        store_signal_state(&self.signal_state, state);
    }

    /// Publish VST3 component activation without treating transient reconfiguration as bypass.
    pub fn set_host_component_active(&self, active: bool) {
        self.liveness.set_host_component_active(active);
        if !self.liveness.is_live() {
            store_signal_state(&self.signal_state, stalled_signal_state(active));
        }
    }

    /// Read the heartbeat-aware signal state using the stable C ABI code.
    pub fn signal_state_abi(&self) -> u8 {
        signal_state_to_abi(load_signal_state(&self.signal_state))
    }
}

/// Set signal state: 0=Inactive, 1=Active, 2=Bypassed.
///
/// # Safety
/// `handle` must be null or a live engine pointer.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_signal_state(handle: *mut KirinHyphaEngine, state: u8) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).set_signal_state(state) };
    }));
}

/// Notify VST3 component activation separately from transport and silence state.
///
/// A short host reconfiguration remains inside the heartbeat grace period. Only sustained
/// deactivation is published as Bypassed by the shared liveness evaluator.
///
/// # Safety
/// `handle` must be null or a live engine pointer.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_host_component_active(
    handle: *mut KirinHyphaEngine,
    active: bool,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe { (*handle).set_host_component_active(active) };
    }));
}

/// Read signal state: 0=Inactive, 1=Active, 2=Bypassed.
///
/// The measure thread may replace stale Active with Inactive after heartbeat loss, so callers do
/// not retain an obsolete active display after processing stops.
///
/// # Safety
/// `handle` must be null or a live engine pointer.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_signal_state(handle: *mut KirinHyphaEngine) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return 0;
        }
        unsafe { (*handle).signal_state_abi() }
    }))
    .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use kirin_measure::SignalState;

    use super::{
        kirin_hypha_get_signal_state, kirin_hypha_set_host_component_active,
        kirin_hypha_set_signal_state, signal_state_to_abi,
    };

    #[test]
    fn signal_state_to_abi_is_inverse_of_the_setter_mapping() {
        assert_eq!(signal_state_to_abi(SignalState::Inactive), 0);
        assert_eq!(signal_state_to_abi(SignalState::Active), 1);
        assert_eq!(signal_state_to_abi(SignalState::Bypassed), 2);
    }

    #[test]
    fn null_signal_state_calls_fail_closed() {
        unsafe {
            kirin_hypha_set_signal_state(std::ptr::null_mut(), 1);
            kirin_hypha_set_host_component_active(std::ptr::null_mut(), false);
            assert_eq!(kirin_hypha_get_signal_state(std::ptr::null_mut()), 0);
        }
    }
}
