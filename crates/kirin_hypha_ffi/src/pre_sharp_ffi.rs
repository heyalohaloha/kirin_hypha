//! Reserved unsupported PRE-own-SHARP ABI. The prototype exceeded its CPU budget.
//! No runtime/lease/IO access. Existing POST-only perceptual/delta APIs are unchanged.
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
/// Unsupported: always returns zero and does not inspect the handle or acquire demand.
/// # Safety
/// This reserved entry does not dereference its pointer arguments.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_request_pre_sharp(
    _handle: *mut KirinHyphaEngine,
    _token: u64,
) -> u64 {
    std::panic::catch_unwind(|| 0).unwrap_or(0)
}

/// Unsupported: always false, leaving output completely unchanged.
/// # Safety
/// This reserved entry does not dereference its pointer arguments.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_pre_sharp(
    _handle: *mut KirinHyphaEngine,
    _token: u64,
    _out: *mut KirinPreSharpFrame,
) -> bool {
    std::panic::catch_unwind(|| false).unwrap_or(false)
}

/// Unsupported: always false; no PRE-local demand can be acquired in this build.
/// # Safety
/// This reserved entry does not dereference its pointer arguments.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_release_pre_sharp(
    _handle: *mut KirinHyphaEngine,
    _token: u64,
) -> bool {
    std::panic::catch_unwind(|| false).unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn pre_sharp_reserved_abi_is_inert_and_preserves_output() {
        assert_eq!(std::mem::size_of::<KirinPreSharpFrame>(), 64);
        assert_eq!(
            std::mem::offset_of!(KirinPreSharpFrame, owner_generation),
            8
        );
        assert_eq!(std::mem::offset_of!(KirinPreSharpFrame, sharpness_acum), 56);
        let mut output = KirinPreSharpFrame {
            version: 77,
            struct_size: 78,
            owner_generation: 79,
            analysis_generation: 80,
            state_epoch_samples: 81,
            presentation_end_samples: 82,
            sample_rate: 83,
            aperture_samples: 84,
            channels: 85,
            reserved: 86,
            sharpness_acum: 87.0,
        };
        unsafe {
            for token in [0, 1, u64::MAX] {
                assert_eq!(
                    kirin_hypha_request_pre_sharp(std::ptr::null_mut(), token),
                    0
                );
                assert!(!kirin_hypha_release_pre_sharp(std::ptr::null_mut(), token));
                assert!(!kirin_hypha_poll_pre_sharp(
                    std::ptr::null_mut(),
                    token,
                    &mut output
                ));
                assert!(!kirin_hypha_poll_pre_sharp(
                    std::ptr::null_mut(),
                    token,
                    std::ptr::null_mut()
                ));
            }
        }
        assert_eq!((output.version, output.struct_size), (77, 78));
        assert_eq!(
            (output.owner_generation, output.analysis_generation),
            (79, 80)
        );
        assert_eq!(
            (output.state_epoch_samples, output.presentation_end_samples),
            (81, 82)
        );
        assert_eq!(
            (
                output.sample_rate,
                output.aperture_samples,
                output.channels,
                output.reserved
            ),
            (83, 84, 85, 86)
        );
        assert_eq!(output.sharpness_acum, 87.0);
    }
}
