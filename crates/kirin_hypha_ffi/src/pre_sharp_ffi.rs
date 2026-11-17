//! PRE's own sharpness only. Existing POST-only perceptual/delta entry points are unchanged.
use super::*;

#[repr(C)]
pub struct KirinPreSharpFrame {
    pub version: u32,
    pub struct_size: u32,
    pub owner_generation: u64,
    pub analysis_generation: u64,
    pub state_epoch_samples: i64,
    pub presentation_end_samples: i64,
    pub sample_rate: u32,
    pub aperture_samples: u32,
    pub channels: u32,
    pub reserved: u32,
    pub sharpness_acum: f64,
}
fn allowed(engine: &KirinHyphaEngine) -> bool {
    engine.write_role.lock().ok().and_then(|role| *role) == Some(PluginDataRole::Pre)
        && engine.supports_optional_analysis()
}
/// UI/non-RT only. 0 obtains a token, nonzero renews its own live token for 1.5 seconds.
/// # Safety
/// handle must be null or live. Caller must not destroy it concurrently.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_request_pre_sharp(
    handle: *mut KirinHyphaEngine,
    token: u64,
) -> u64 {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(engine) = (unsafe { handle.as_ref() }) else {
            return 0;
        };
        if !allowed(engine) || engine.signal_state_abi() != KIRIN_SIGNAL_STATE_ACTIVE {
            return 0;
        }
        engine.spectrum.request_pre_sharp(token)
    }))
    .unwrap_or(0)
}
/// Poll does not renew demand. Output is unchanged on false.
/// # Safety
/// handle must be null or live, out null or writable for one complete frame.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_pre_sharp(
    handle: *mut KirinHyphaEngine,
    token: u64,
    out: *mut KirinPreSharpFrame,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(engine) = (unsafe { handle.as_ref() }) else {
            return false;
        };
        if out.is_null()
            || !allowed(engine)
            || engine.signal_state_abi() != KIRIN_SIGNAL_STATE_ACTIVE
        {
            return false;
        }
        let Some((generation, frame)) = engine.spectrum.poll_pre_sharp(token) else {
            return false;
        };
        unsafe {
            out.write(KirinPreSharpFrame {
                version: 1,
                struct_size: std::mem::size_of::<KirinPreSharpFrame>() as u32,
                owner_generation: generation,
                analysis_generation: frame.generation,
                state_epoch_samples: frame.state_epoch_samples,
                presentation_end_samples: frame.presentation_end_samples,
                sample_rate: frame.sample_rate,
                aperture_samples: frame.aperture_samples,
                channels: u32::from(frame.channels),
                reserved: 0,
                sharpness_acum: frame.sharpness,
            });
        }
        true
    }))
    .unwrap_or(false)
}
/// UI/non-RT. False means busy or foreign; retry a busy release or let its lease expire.
/// # Safety
/// handle must be null or live.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_release_pre_sharp(
    handle: *mut KirinHyphaEngine,
    token: u64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(engine) = (unsafe { handle.as_ref() }) else {
            return false;
        };
        if !allowed(engine) {
            return false;
        }
        engine.spectrum.release_pre_sharp(token)
    }))
    .unwrap_or(false)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn pre_sharp_ffi_null_and_layout_contract() {
        assert_eq!(std::mem::size_of::<KirinPreSharpFrame>(), 64);
        unsafe {
            assert_eq!(kirin_hypha_request_pre_sharp(std::ptr::null_mut(), 0), 0);
            assert!(!kirin_hypha_release_pre_sharp(std::ptr::null_mut(), 1));
            assert!(!kirin_hypha_poll_pre_sharp(
                std::ptr::null_mut(),
                1,
                std::ptr::null_mut()
            ));
        }
    }
}
