use super::*;

impl KirinHyphaEngine {
    pub fn poll_watch_display(&self, playing: bool) -> Option<(MeasureResult, MeasureResult)> {
        let raw = self.poll_result()?;
        let pass_id = self.watch_playback_pass_id.load(Ordering::Acquire);
        let maximum = self.watch_max.try_lock().ok()?.update(
            &raw,
            playing,
            pass_id,
            self.record_sm.is_recording(),
        );
        Some((raw, maximum))
    }

}

/// Current Watch values and current-playback-pass maxima from one Rust
/// snapshot. UI thread only.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`].
/// `out` must be null or point to writable storage for one [`KirinWatchDisplay`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_watch_display(
    handle: *mut KirinHyphaEngine,
    playing: bool,
    out: *mut KirinWatchDisplay,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some((current, maximum)) = (unsafe { &*handle }).poll_watch_display(playing) else {
            return false;
        };
        unsafe {
            *out = KirinWatchDisplay {
                current: to_c_result(&current),
                maximum: to_c_result(&maximum),
            };
        }
        true
    }))
    .unwrap_or(false)
}
