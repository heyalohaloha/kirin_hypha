//! Non-realtime MATCH meter C bindings.
use kirin_measure::live_match_span::{KirinLiveMatchSpan, KirinLiveMatchSpanFacts};
use std::panic::{catch_unwind, AssertUnwindSafe};

#[no_mangle]
pub extern "C" fn kirin_hypha_live_match_span_create(
    rate: u32,
    channels: u32,
) -> *mut KirinLiveMatchSpan {
    catch_unwind(|| {
        KirinLiveMatchSpan::new(rate, channels)
            .map(|meter| Box::into_raw(Box::new(meter)))
            .unwrap_or(std::ptr::null_mut())
    })
    .unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// The unique owner must supply its live handle, or null; never from the audio callback.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_live_match_span_destroy(handle: *mut KirinLiveMatchSpan) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !handle.is_null() {
            drop(unsafe { Box::from_raw(handle) });
        }
    }));
}

/// # Safety
/// One unique live handle and both validated buffers contain exactly frames × channels samples.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_live_match_span_feed(
    handle: *mut KirinLiveMatchSpan,
    pre: *const f32,
    post: *const f32,
    frames: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(meter) = (unsafe { handle.as_mut() }) else {
            return false;
        };
        if pre.is_null() || post.is_null() || frames == 0 || frames > 262_144 {
            return false;
        }
        let size = frames * meter.channels;
        meter.feed(unsafe { std::slice::from_raw_parts(pre, size) }, unsafe {
            std::slice::from_raw_parts(post, size)
        })
    }))
    .unwrap_or(false)
}

/// # Safety
/// The unique owner supplies a live handle, or null. Never call from the audio callback.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_live_match_span_cut(handle: *mut KirinLiveMatchSpan) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        (unsafe { handle.as_mut() }).is_some_and(KirinLiveMatchSpan::cut)
    }))
    .unwrap_or(false)
}

/// # Safety
/// The handle is live and the distinct output pointer is writable. No result is written on failure.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_live_match_span_measure(
    handle: *const KirinLiveMatchSpan,
    out: *mut KirinLiveMatchSpanFacts,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(meter) = (unsafe { handle.as_ref() }) else {
            return false;
        };
        let Some(out) = (unsafe { out.as_mut() }) else {
            return false;
        };
        let Some(facts) = meter.measure() else {
            return false;
        };
        *out = facts;
        true
    }))
    .unwrap_or(false)
}
