//! Sized Session input coverage; old publication and ABI remain unchanged.
use crate::snapshot_types::*;
use crate::{KirinHyphaEngine, KirinMeterSession};
use std::panic::{catch_unwind, AssertUnwindSafe};
#[repr(C)]
pub struct KirinMeterSessionV2 {
    pub version: u32,
    pub struct_size: u32,
    pub session: KirinMeterSession,
    pub processed_frames: u64,
    pub pending_frames: u64,
    pub summary_status: u8,
    pub reserved: [u8; 7],
}
impl KirinHyphaEngine {
    pub fn meter_session_v2(&self) -> Result<KirinMeterSessionV2, u8> {
        let session = self
            .meter_session
            .as_ref()
            .ok_or(KIRIN_SNAPSHOT_UNSUPPORTED)?;
        let session = session.try_lock().map_err(|_| KIRIN_SNAPSHOT_BUSY)?;
        let value = session.snapshot_v2();
        Ok(KirinMeterSessionV2 {
            version: 2,
            struct_size: std::mem::size_of::<KirinMeterSessionV2>() as u32,
            session: crate::meter_session_ffi::to_c_meter_session(&value.session),
            processed_frames: value.processed_frames,
            pending_frames: value.pending_frames,
            summary_status: value.summary_status as u8,
            reserved: [0; 7],
        })
    }
}
/// # Safety
/// Live handle and aligned writable output of declared size. Every failure preserves output.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_meter_session_v2(
    handle: *const KirinHyphaEngine,
    version: u32,
    out_size: u32,
    out: *mut KirinMeterSessionV2,
) -> u8 {
    if version != 2 {
        return KIRIN_SNAPSHOT_UNSUPPORTED;
    }
    if handle.is_null()
        || out.is_null()
        || (out_size as usize) < std::mem::size_of::<KirinMeterSessionV2>()
        || !(out as usize).is_multiple_of(std::mem::align_of::<KirinMeterSessionV2>())
    {
        return KIRIN_SNAPSHOT_INVALID_REQUEST;
    }
    catch_unwind(AssertUnwindSafe(|| {
        match unsafe { &*handle }.meter_session_v2() {
            Ok(packet) => {
                unsafe { out.write(packet) };
                KIRIN_SNAPSHOT_SUCCESS
            }
            Err(status) => status,
        }
    }))
    .unwrap_or(KIRIN_SNAPSHOT_BUSY)
}

#[cfg(test)]
#[path = "meter_session_v2_tests.rs"]
mod tests;
