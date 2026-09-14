//! Worker-only, exclusively owned streaming feature handles; no audio callback caller.
use kirin_measure::reference_gain::capture_index::{self, CaptureIndex, CaptureUnit};
use std::panic::{catch_unwind, AssertUnwindSafe};
#[no_mangle]
pub extern "C" fn kirin_reference_index_create(
    rate: u32,
    channels: u32,
    frames: u32,
) -> *mut CaptureIndex {
    catch_unwind(|| {
        CaptureIndex::new(rate, channels as usize, frames)
            .map_or(std::ptr::null_mut(), |v| Box::into_raw(Box::new(v)))
    })
    .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// Null or an exclusively owned live handle.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_index_drop(p: *mut CaptureIndex) {
    if !p.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(p) })));
    }
}
/// # Safety
/// Live exclusive handle and `count` readable floats. Worker only.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_index_push(
    p: *mut CaptureIndex,
    pcm: *const f32,
    count: usize,
) -> bool {
    if p.is_null() || pcm.is_null() || count > 16384 {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        (*p).push(std::slice::from_raw_parts(pcm, count))
    }))
    .unwrap_or(false)
}
/// # Safety
/// Live exclusive handle, writable unit. Returns accepted frames, or zero on failure.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_index_finish(
    p: *mut CaptureIndex,
    out: *mut CaptureUnit,
) -> u32 {
    if p.is_null() || out.is_null() {
        return 0;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        let frames = (*p).frames();
        let value = (*p).finish();
        (*p).reset();
        if let Some(value) = value {
            *out = value;
            frames
        } else {
            0
        }
    }))
    .unwrap_or(0)
}
/// # Safety
/// Both pointers must refer to readable units.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_index_compare(
    a: *const CaptureUnit,
    b: *const CaptureUnit,
    channels: u32,
) -> u32 {
    if a.is_null() || b.is_null() {
        return 0;
    }
    catch_unwind(|| unsafe { capture_index::compare(&*a, &*b, channels as usize) }).unwrap_or(0)
}
