#[path = "reference_capture_index_ffi.rs"]
mod capture_index;
// Non-RT, single-owner handles; callers serialize create/push/finish/drop.
use kirin_measure::reference_gain::tonal::{TonalMeter, TonalSnapshot};
use kirin_measure::reference_gain::visual::{VisualAdmission, VisualBin, VisualMeter};
use std::panic::{catch_unwind, AssertUnwindSafe};

#[no_mangle]
pub extern "C" fn kirin_reference_capture_create(rate: u32, channels: u32) -> *mut VisualMeter {
    catch_unwind(|| {
        VisualMeter::capture(rate, channels as usize)
            .map_or(std::ptr::null_mut(), |m| Box::into_raw(Box::new(m)))
    })
    .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// Live, exclusively owned meter and writable outputs; worker thread only.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_capture_finish(
    m: *mut VisualMeter,
    bin: *mut VisualBin,
    tp: *mut f64,
) -> bool {
    if m.is_null() || bin.is_null() || tp.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        *tp = (*m).pending_true_peak();
        kirin_reference_visual_finish(m, bin)
    }))
    .unwrap_or(false)
}
/// # Safety
/// Live, exclusively owned meter and writable outputs; worker thread only.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_capture_totals(
    m: *mut VisualMeter,
    loudness: *mut f64,
    tp: *mut f64,
) -> bool {
    if m.is_null() || loudness.is_null() || tp.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        let totals = (*m).seal_capture();
        *loudness = totals.0;
        *tp = totals.1;
        true
    }))
    .unwrap_or(false)
}

#[no_mangle]
pub extern "C" fn kirin_reference_visual_create(rate: u32, channels: u32) -> *mut VisualMeter {
    catch_unwind(|| {
        VisualMeter::new(rate, channels as usize)
            .map_or(std::ptr::null_mut(), |m| Box::into_raw(Box::new(m)))
    })
    .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub extern "C" fn kirin_reference_tonal_create(rate: u32, channels: u32) -> *mut TonalMeter {
    catch_unwind(|| {
        TonalMeter::new(rate, channels as usize)
            .map_or(std::ptr::null_mut(), |meter| Box::into_raw(Box::new(meter)))
    })
    .unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// Null or a live, exclusively owned worker-thread meter.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_tonal_drop(meter: *mut TonalMeter) {
    if !meter.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(meter) })));
    }
}

/// # Safety
/// Meter is live and samples contains count readable floats; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_tonal_push(
    meter: *mut TonalMeter,
    samples: *const f32,
    count: usize,
) -> bool {
    if meter.is_null() || samples.is_null() || count == 0 || count > 16_384 {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        (*meter).push(std::slice::from_raw_parts(samples, count))
    }))
    .unwrap_or(false)
}

/// # Safety
/// Meter is live and output is writable; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_tonal_snapshot(
    meter: *const TonalMeter,
    output: *mut TonalSnapshot,
) -> bool {
    if meter.is_null() || output.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        *output = (*meter).snapshot();
        true
    }))
    .unwrap_or(false)
}

/// # Safety
/// Meter is live and exclusively owned; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_tonal_reset(meter: *mut TonalMeter) -> bool {
    if meter.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        (*meter).reset();
        true
    }))
    .unwrap_or(false)
}

/// # Safety
/// Meter is live and exclusively owned; no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_tonal_allocated_bytes(meter: *const TonalMeter) -> usize {
    if meter.is_null() {
        return 0;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe { (*meter).allocated_bytes() })).unwrap_or(0)
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
