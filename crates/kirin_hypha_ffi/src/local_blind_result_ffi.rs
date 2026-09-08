//! Stable C ABI for non-RT Local Blind PRE PCM publication and exact POST consumption.

use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use kirin_measure::local_blind_capture_protocol::{
    read_validated_local_blind_capture_request_for_active_result, LocalBlindCaptureRequest,
};
use kirin_measure::local_blind_capture_result::{
    local_blind_pre_capture_was_consumed, publish_local_blind_pre_capture,
    publish_local_blind_pre_capture_consumed, publish_local_blind_pre_capture_failure,
    read_local_blind_pre_capture, read_local_blind_pre_capture_failure,
    remove_local_blind_pre_capture, LocalBlindPreCaptureReceipt,
};
use kirin_measure::{PlatformPaths, PluginDataRole};

use super::local_blind_capture_ffi::unix_ms_now;
use super::{copy_exact, KirinHyphaEngine};

const REQUEST_ID_CAPACITY: usize = 37;
const SHA256_CAPACITY: usize = 65;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinLocalBlindPreCaptureReceipt {
    pub request_id: [c_char; REQUEST_ID_CAPACITY],
    pub pair_generation: u64,
    pub capture_generation: u64,
    pub clock_generation: u64,
    pub sample_rate: u32,
    pub channels: u32,
    pub start: i64,
    pub frames: i64,
    pub sample_count: u64,
    pub pcm_sha256: [c_char; SHA256_CAPACITY],
}

impl Default for KirinLocalBlindPreCaptureReceipt {
    fn default() -> Self {
        Self {
            request_id: [0; REQUEST_ID_CAPACITY],
            pair_generation: 0,
            capture_generation: 0,
            clock_generation: 0,
            sample_rate: 0,
            channels: 0,
            start: 0,
            frames: 0,
            sample_count: 0,
            pcm_sha256: [0; SHA256_CAPACITY],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct KirinLocalBlindPreCaptureFailure {
    pub owner_failure: u8,
    pub capture_failure: u8,
}

const _: () = assert!(std::mem::size_of::<KirinLocalBlindPreCaptureFailure>() == 2);

impl KirinHyphaEngine {
    fn local_blind_request_for_post(
        &self,
        request_id: &str,
    ) -> Option<(
        LocalBlindCaptureRequest,
        std::path::PathBuf,
        std::path::PathBuf,
    )> {
        if !self.is_local_blind_role(PluginDataRole::Post) {
            return None;
        }
        let authority = self.local_blind_pair_authority()?;
        let root = PlatformPaths::current_kirin_tmp_root();
        let instance_dir = root
            .join(&authority.pre_project_hash)
            .join(&authority.pre_instance_id);
        let request = read_validated_local_blind_capture_request_for_active_result(
            &root,
            &instance_dir,
            &authority.pre_project_hash,
            &authority.pre_instance_id,
            self.sample_rate,
            u8::try_from(self.num_channels).ok()?,
            unix_ms_now()?,
        )?;
        (request.request_id == request_id && request.authority == authority).then_some((
            request,
            root,
            instance_dir,
        ))
    }
}

fn encode_receipt(
    receipt: &LocalBlindPreCaptureReceipt,
) -> Option<KirinLocalBlindPreCaptureReceipt> {
    let mut encoded = KirinLocalBlindPreCaptureReceipt {
        pair_generation: receipt.pair_generation,
        capture_generation: receipt.capture_generation,
        clock_generation: receipt.clock_generation,
        sample_rate: receipt.sample_rate,
        channels: u32::from(receipt.channels),
        start: receipt.start,
        frames: receipt.frames,
        sample_count: receipt.sample_count,
        ..KirinLocalBlindPreCaptureReceipt::default()
    };
    if !copy_exact(&receipt.request_id, &mut encoded.request_id)
        || !copy_exact(&receipt.pcm_sha256, &mut encoded.pcm_sha256)
    {
        return None;
    }
    Some(encoded)
}

fn read_receipt_identity(
    receipt: *const KirinLocalBlindPreCaptureReceipt,
) -> Option<(KirinLocalBlindPreCaptureReceipt, String, String)> {
    if receipt.is_null() {
        return None;
    }
    let value = unsafe { &*receipt };
    let request_id = read_fixed_string(&value.request_id)?;
    let pcm_sha256 = read_fixed_string(&value.pcm_sha256)?;
    (!request_id.is_empty() && canonical_sha256(&pcm_sha256))
        .then_some((*value, request_id, pcm_sha256))
}

fn read_fixed_string<const CAPACITY: usize>(value: &[c_char; CAPACITY]) -> Option<String> {
    let end = value.iter().position(|byte| *byte == 0)?;
    let bytes = value[..end]
        .iter()
        .map(|byte| *byte as u8)
        .collect::<Vec<_>>();
    String::from_utf8(bytes).ok()
}

fn canonical_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn c_receipt_matches_request(
    receipt: &KirinLocalBlindPreCaptureReceipt,
    request: &LocalBlindCaptureRequest,
) -> bool {
    let expected_samples = u64::try_from(request.frames)
        .ok()
        .and_then(|frames| frames.checked_mul(u64::from(request.channels)));
    receipt.pair_generation == request.authority.pair_generation
        && receipt.capture_generation == request.capture_generation
        && receipt.clock_generation == request.clock_generation
        && receipt.sample_rate == request.sample_rate
        && receipt.channels == u32::from(request.channels)
        && receipt.start == request.native_start
        && receipt.frames == request.frames
        && Some(receipt.sample_count) == expected_samples
}

/// Publish a complete PRE capture once. This is non-RT and copies the interleaved input.
///
/// # Safety
/// `handle` and `out_receipt` must be live writable pointers. `interleaved` must point to exactly
/// `sample_count` readable floats for the request currently addressed to this PRE.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_publish_local_blind_pre_capture(
    handle: *mut KirinHyphaEngine,
    request_id: *const c_char,
    interleaved: *const f32,
    sample_count: usize,
    out_receipt: *mut KirinLocalBlindPreCaptureReceipt,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || request_id.is_null()
            || interleaved.is_null()
            || out_receipt.is_null()
        {
            return false;
        }
        let request_id = unsafe { crate::read_c_str(request_id) };
        let Some(request) = (unsafe { &*handle }).read_local_blind_capture_request_for_active_pre()
        else {
            return false;
        };
        if request.request_id != request_id {
            return false;
        }
        let Some(expected) = usize::try_from(request.frames)
            .ok()
            .and_then(|frames| frames.checked_mul(usize::from(request.channels)))
        else {
            return false;
        };
        if sample_count != expected {
            return false;
        }
        let samples = unsafe { std::slice::from_raw_parts(interleaved, sample_count) };
        let root = PlatformPaths::current_kirin_tmp_root();
        let instance_dir = root
            .join(&request.authority.pre_project_hash)
            .join(&request.authority.pre_instance_id);
        let Ok(receipt) = publish_local_blind_pre_capture(
            &root,
            &instance_dir,
            &request,
            samples,
            unix_ms_now().unwrap_or_default(),
        ) else {
            return false;
        };
        let Some(encoded) = encode_receipt(&receipt) else {
            return false;
        };
        unsafe { out_receipt.write(encoded) };
        true
    }))
    .unwrap_or(false)
}

/// Copy one fully verified PRE capture into POST-owned storage.
///
/// # Safety
/// `handle`, `out_interleaved`, and `out_receipt` must be live writable pointers. The output
/// region must contain exactly `sample_count` floats; no output is changed on rejection.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_read_local_blind_pre_capture(
    handle: *mut KirinHyphaEngine,
    request_id: *const c_char,
    out_interleaved: *mut f32,
    sample_count: usize,
    out_receipt: *mut KirinLocalBlindPreCaptureReceipt,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || request_id.is_null()
            || out_interleaved.is_null()
            || out_receipt.is_null()
        {
            return false;
        }
        let request_id = unsafe { crate::read_c_str(request_id) };
        let Some((request, root, instance_dir)) =
            (unsafe { &*handle }).local_blind_request_for_post(&request_id)
        else {
            return false;
        };
        let Some(capture) = read_local_blind_pre_capture(
            &root,
            &instance_dir,
            &request,
            unix_ms_now().unwrap_or_default(),
        ) else {
            return false;
        };
        if capture.interleaved.len() != sample_count {
            return false;
        }
        let Some(encoded) = encode_receipt(&capture.receipt) else {
            return false;
        };
        unsafe {
            ptr::copy_nonoverlapping(capture.interleaved.as_ptr(), out_interleaved, sample_count);
            out_receipt.write(encoded);
        }
        true
    }))
    .unwrap_or(false)
}

/// Publish one terminal PRE failure for an already-armed request.
///
/// # Safety
/// `handle` must be live and `request_id` must be a readable null-terminated string.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_publish_local_blind_pre_capture_failure(
    handle: *mut KirinHyphaEngine,
    request_id: *const c_char,
    owner_failure: u8,
    capture_failure: u8,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || request_id.is_null() {
            return false;
        }
        let request_id = unsafe { crate::read_c_str(request_id) };
        let engine = unsafe { &*handle };
        let Some(request) = engine.read_local_blind_capture_request_for_active_pre() else {
            return false;
        };
        if request.request_id != request_id {
            return false;
        }
        let root = PlatformPaths::current_kirin_tmp_root();
        let instance_dir = root
            .join(&request.authority.pre_project_hash)
            .join(&request.authority.pre_instance_id);
        publish_local_blind_pre_capture_failure(
            &root,
            &instance_dir,
            &request,
            owner_failure,
            capture_failure,
            unix_ms_now().unwrap_or_default(),
        )
        .is_ok()
    }))
    .unwrap_or(false)
}

/// Read a terminal failure only for this POST's current exact request.
///
/// # Safety
/// `handle` and `out_failure` must be live writable pointers. `request_id` must be readable.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_read_local_blind_pre_capture_failure(
    handle: *mut KirinHyphaEngine,
    request_id: *const c_char,
    out_failure: *mut KirinLocalBlindPreCaptureFailure,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || request_id.is_null() || out_failure.is_null() {
            return false;
        }
        let request_id = unsafe { crate::read_c_str(request_id) };
        let Some((request, root, instance_dir)) =
            (unsafe { &*handle }).local_blind_request_for_post(&request_id)
        else {
            return false;
        };
        let Some(failure) = read_local_blind_pre_capture_failure(
            &root,
            &instance_dir,
            &request,
            unix_ms_now().unwrap_or_default(),
        ) else {
            return false;
        };
        unsafe {
            out_failure.write(KirinLocalBlindPreCaptureFailure {
                owner_failure: failure.owner_failure,
                capture_failure: failure.capture_failure,
            });
        }
        true
    }))
    .unwrap_or(false)
}

/// Acknowledge only the exact PRE artifact already verified and copied by this POST.
///
/// # Safety
/// `handle` and `receipt` must point to live values.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_ack_local_blind_pre_capture(
    handle: *mut KirinHyphaEngine,
    receipt: *const KirinLocalBlindPreCaptureReceipt,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let Some((encoded, request_id, pcm_sha256)) = read_receipt_identity(receipt) else {
            return false;
        };
        let Some((request, root, instance_dir)) =
            (unsafe { &*handle }).local_blind_request_for_post(&request_id)
        else {
            return false;
        };
        if !c_receipt_matches_request(&encoded, &request) {
            return false;
        }
        publish_local_blind_pre_capture_consumed(
            &root,
            &instance_dir,
            &request,
            &pcm_sha256,
            unix_ms_now().unwrap_or_default(),
        )
        .is_ok()
    }))
    .unwrap_or(false)
}

/// Return true only while this PRE still owns the exact request and matching consumed receipt.
///
/// # Safety
/// `handle` and `receipt` must point to live values.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_local_blind_pre_capture_was_consumed(
    handle: *mut KirinHyphaEngine,
    receipt: *const KirinLocalBlindPreCaptureReceipt,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let Some((encoded, request_id, pcm_sha256)) = read_receipt_identity(receipt) else {
            return false;
        };
        let engine = unsafe { &*handle };
        let Some(request) = engine.read_local_blind_capture_request_for_active_pre() else {
            return false;
        };
        if request.request_id != request_id {
            return false;
        }
        if !c_receipt_matches_request(&encoded, &request) {
            return false;
        }
        let root = PlatformPaths::current_kirin_tmp_root();
        let instance_dir = root
            .join(&request.authority.pre_project_hash)
            .join(&request.authority.pre_instance_id);
        local_blind_pre_capture_was_consumed(
            &root,
            &instance_dir,
            &request,
            &pcm_sha256,
            unix_ms_now().unwrap_or_default(),
        )
    }))
    .unwrap_or(false)
}

/// Remove this PRE instance's exact result and acknowledgement after retirement or failure.
///
/// # Safety
/// `handle` must be live and `request_id` must be a readable null-terminated string.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_retire_local_blind_pre_capture(
    handle: *mut KirinHyphaEngine,
    request_id: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || request_id.is_null() {
            return false;
        }
        let engine = unsafe { &*handle };
        if !engine.is_local_blind_role(PluginDataRole::Pre) {
            return false;
        }
        let request_id = unsafe { crate::read_c_str(request_id) };
        let identity = engine.identity_snapshot();
        let instance_dir = PlatformPaths::current_kirin_tmp_root()
            .join(identity.project_hash)
            .join(identity.instance_id);
        remove_local_blind_pre_capture(&instance_dir, &request_id).is_ok()
    }))
    .unwrap_or(false)
}
