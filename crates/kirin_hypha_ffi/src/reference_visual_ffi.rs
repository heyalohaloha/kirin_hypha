// Non-RT, single-owner handles; callers serialize create/push/finish/drop.
use kirin_measure::reference_gain::visual::{VisualAdmission, VisualBin, VisualMeter};
use std::panic::{catch_unwind, AssertUnwindSafe};

#[no_mangle]
pub extern "C" fn kirin_reference_visual_create(rate: u32, channels: u32) -> *mut VisualMeter {
    catch_unwind(|| {
        VisualMeter::new(rate, channels as usize)
            .map_or(std::ptr::null_mut(), |m| Box::into_raw(Box::new(m)))
    })
    .unwrap_or(std::ptr::null_mut())
}

// File targets need integrated loudness; leave the existing A observation mode unchanged.
#[no_mangle]
pub extern "C" fn kirin_reference_visual_capture_create(
    rate: u32,
    channels: u32,
) -> *mut VisualMeter {
    catch_unwind(|| {
        VisualMeter::capture(rate, channels as usize)
            .map_or(std::ptr::null_mut(), |m| Box::into_raw(Box::new(m)))
    })
    .unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// Null or a live meter owned by this caller; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_visual_drop(meter: *mut VisualMeter) {
    if !meter.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(meter) })));
    }
}
/// # Safety
/// Meter is live; samples contains count readable floats; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_visual_push(
    meter: *mut VisualMeter,
    samples: *const f32,
    count: usize,
) -> bool {
    if meter.is_null() || samples.is_null() || count == 0 || count > 16384 {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        (*meter).push(std::slice::from_raw_parts(samples, count))
    }))
    .unwrap_or(false)
}
/// # Safety
/// Meter is live and output writable; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_visual_finish(
    meter: *mut VisualMeter,
    output: *mut VisualBin,
) -> bool {
    if meter.is_null() || output.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let Some(bin) = (unsafe { &mut *meter }).finish_bin() else {
            return false;
        };
        unsafe {
            *output = bin;
        }
        true
    }))
    .unwrap_or(false)
}
/// # Safety
/// Meter is live, outputs writable, and no concurrent calls. No push follows sealing.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_visual_seal(
    meter: *mut VisualMeter,
    integrated: *mut f64,
    maximum_true_peak: *mut f64,
) -> bool {
    if meter.is_null() || integrated.is_null() || maximum_true_peak.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let (loudness, peak) = unsafe { &mut *meter }.seal_capture();
        unsafe {
            *integrated = loudness;
            *maximum_true_peak = peak;
        }
        true
    }))
    .unwrap_or(false)
}
#[no_mangle]
pub extern "C" fn kirin_reference_visual_admission_create() -> *mut VisualAdmission {
    catch_unwind(|| Box::into_raw(Box::<VisualAdmission>::default()))
        .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// Admission is live, exclusively owned. Worker/control threads only.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_visual_admission_set(
    admission: *mut VisualAdmission,
    active: bool,
) -> bool {
    if admission.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let a = unsafe { &mut *admission };
        if active {
            a.acquire()
        } else {
            a.release();
            true
        }
    }))
    .unwrap_or(false)
}
/// # Safety
/// Null or exclusively owned live admission; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_visual_admission_drop(admission: *mut VisualAdmission) {
    if !admission.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| {
            drop(unsafe { Box::from_raw(admission) })
        }));
    }
}

#[cfg(test)]
mod target_tests {
    use super::*;

    #[test]
    fn target_finalization_uses_complete_loudness_and_drains_peak_support() {
        unsafe {
            assert!(!kirin_reference_visual_seal(
                std::ptr::null_mut(),
                &mut 0.0,
                &mut 0.0,
            ));
            let meter = kirin_reference_visual_capture_create(48000, 1);
            assert!(!meter.is_null());
            let mut pcm = vec![0.0_f32; 4800];
            for block in 0..40 {
                for (n, value) in pcm.iter_mut().enumerate() {
                    *value = (0.13 * (block * 4800 + n) as f32).sin() * 0.1;
                }
                if block == 39 {
                    *pcm.last_mut().unwrap() = 0.9;
                }
                assert!(kirin_reference_visual_push(meter, pcm.as_ptr(), pcm.len()));
            }
            let (mut integrated, mut peak) = (0.0, 0.0);
            assert!(kirin_reference_visual_seal(
                meter,
                &mut integrated,
                &mut peak
            ));
            assert!(integrated.is_finite() && integrated < -10.0);
            assert!(peak >= 0.8999);
            let (mut repeated_loudness, mut repeated_peak) = (0.0, 0.0);
            assert!(kirin_reference_visual_seal(
                meter,
                &mut repeated_loudness,
                &mut repeated_peak
            ));
            assert_eq!((integrated, peak), (repeated_loudness, repeated_peak));
            assert!(!kirin_reference_visual_seal(
                meter,
                std::ptr::null_mut(),
                &mut peak
            ));
            kirin_reference_visual_drop(meter);
        }
    }
}
