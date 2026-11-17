//! Append-only Reference analysis ABI. Non-RT, serialized, single-owner handles only.
use kirin_measure::engine::reference_metrics::ReferenceMetrics;
use std::panic::{catch_unwind, AssertUnwindSafe};

pub struct ReferenceMetricsHandle {
    engine: ReferenceMetrics,
    poisoned: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct KirinReferenceMetricFrame {
    pub end_sample: u64,
    pub crest_db: f64,
    pub psr_db: f64,
    pub sharpness_acum: f64,
}

#[no_mangle]
pub extern "C" fn kirin_reference_metrics_create(
    rate: u32,
    channels: u32,
) -> *mut ReferenceMetricsHandle {
    catch_unwind(|| {
        ReferenceMetrics::new(rate, channels as usize).map_or(std::ptr::null_mut(), |engine| {
            Box::into_raw(Box::new(ReferenceMetricsHandle {
                engine,
                poisoned: false,
            }))
        })
    })
    .unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// `handle` is null or live and owned by this caller. No concurrent calls or reuse after drop.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_metrics_drop(handle: *mut ReferenceMetricsHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !handle.is_null() {
            drop(unsafe { Box::from_raw(handle) });
        }
    }));
}

/// # Safety
/// Handle is live and exclusively owned. Input has `count` readable floats. Output has
/// `capacity` writable, non-overlapping frames and `written` is writable. All buffers must
/// remain valid for the call. This is not an audio-thread API. No callbacks or worker creation.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_metrics_push(
    handle: *mut ReferenceMetricsHandle,
    samples: *const f32,
    count: usize,
    output: *mut KirinReferenceMetricFrame,
    capacity: usize,
    written: *mut usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || samples.is_null()
            || output.is_null()
            || written.is_null()
            || capacity < 8
            || count == 0
            || count > 76_800
        {
            return false;
        }
        let handle = unsafe { &mut *handle };
        if handle.poisoned {
            return false;
        }
        let input = unsafe { std::slice::from_raw_parts(samples, count) };
        // Leave poisoned if the engine unwinds; a normal invalid-input error remains retryable.
        handle.poisoned = true;
        let result = handle.engine.push_aperture(input);
        handle.poisoned = false;
        let Ok(frames) = result else {
            return false;
        };
        if frames.len() > capacity {
            handle.poisoned = true;
            return false;
        }
        for (index, frame) in frames.iter().enumerate() {
            unsafe {
                *output.add(index) = KirinReferenceMetricFrame {
                    end_sample: frame.end_sample,
                    crest_db: frame.crest_db.unwrap_or(f64::NAN),
                    psr_db: frame.psr_db.unwrap_or(f64::NAN),
                    sharpness_acum: frame.sharpness_acum.unwrap_or(f64::NAN),
                };
            }
        }
        unsafe {
            *written = frames.len();
        }
        true
    }))
    .unwrap_or(false)
}

/// # Safety
/// Same exclusive-handle and writable/non-overlapping output requirements as push.
/// Release complete level apertures at EOF without fabricating resampler tail audio.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_metrics_finish(
    handle: *mut ReferenceMetricsHandle,
    output: *mut KirinReferenceMetricFrame,
    capacity: usize,
    written: *mut usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || output.is_null() || written.is_null() || capacity < 8 {
            return false;
        }
        let handle = unsafe { &mut *handle };
        if handle.poisoned {
            return false;
        }
        handle.poisoned = true;
        let result = handle.engine.finish();
        handle.poisoned = false;
        let Ok(frames) = result else {
            return false;
        };
        if frames.len() > capacity {
            handle.poisoned = true;
            return false;
        }
        for (index, frame) in frames.iter().enumerate() {
            unsafe {
                *output.add(index) = KirinReferenceMetricFrame {
                    end_sample: frame.end_sample,
                    crest_db: frame.crest_db.unwrap_or(f64::NAN),
                    psr_db: frame.psr_db.unwrap_or(f64::NAN),
                    sharpness_acum: frame.sharpness_acum.unwrap_or(f64::NAN),
                };
            }
        }
        unsafe {
            *written = frames.len();
        }
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
#[path = "reference_metrics_ffi_tests.rs"]
mod tests;
