//! Stable C ABI for the non-RT Local Blind capture request handshake.

use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::time::{SystemTime, UNIX_EPOCH};

use kirin_measure::local_blind_capture_protocol::{
    publish_local_blind_capture_armed, publish_local_blind_capture_request,
    read_matching_local_blind_capture_armed, read_validated_local_blind_capture_request,
    LocalBlindCaptureRequest, LocalBlindPairAuthority, LOCAL_BLIND_CAPTURE_LEASE_MS,
};
use kirin_measure::{PlatformPaths, PluginDataRole};

use super::{copy_exact, KirinHyphaEngine, LOCATOR_CAPACITY};

const REQUEST_ID_CAPACITY: usize = 37;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinLocalBlindCaptureRequest {
    pub request_id: [c_char; REQUEST_ID_CAPACITY],
    pub pair_generation: u64,
    pub capture_generation: u64,
    pub clock_generation: u64,
    pub sample_rate: u32,
    pub channels: u32,
    pub pre_start: i64,
    pub post_start: i64,
    pub frames: i64,
    pub expires_at_unix_ms: i64,
    pub pre_project_hash: [c_char; LOCATOR_CAPACITY],
    pub pre_instance_id: [c_char; LOCATOR_CAPACITY],
}

impl Default for KirinLocalBlindCaptureRequest {
    fn default() -> Self {
        Self {
            request_id: [0; REQUEST_ID_CAPACITY],
            pair_generation: 0,
            capture_generation: 0,
            clock_generation: 0,
            sample_rate: 0,
            channels: 0,
            pre_start: 0,
            post_start: 0,
            frames: 0,
            expires_at_unix_ms: 0,
            pre_project_hash: [0; LOCATOR_CAPACITY],
            pre_instance_id: [0; LOCATOR_CAPACITY],
        }
    }
}

impl KirinHyphaEngine {
    fn is_local_blind_role(&self, expected: PluginDataRole) -> bool {
        self.write_role
            .lock()
            .map(|role| *role == Some(expected))
            .unwrap_or(false)
    }

    fn local_blind_pair_authority(&self) -> Option<LocalBlindPairAuthority> {
        if !self.is_local_blind_role(PluginDataRole::Post) {
            return None;
        }
        let before = self.pair_binding.exact_snapshot()?;
        let claimed_before = self
            .pair_claimed_at
            .read()
            .map(|value| value.to_bits())
            .ok()?;
        let identity = self.identity_snapshot();
        let authority = LocalBlindPairAuthority {
            pair_generation: before.generation,
            pair_owner_id: self.pair_owner.owner_id().to_string(),
            pair_claimed_at_bits: claimed_before,
            host_process_id: kirin_measure::post_candidates::current_host_process_id(),
            post_project_hash: identity.project_hash,
            post_instance_id: identity.instance_id,
            pre_project_hash: before.project_hash,
            pre_instance_id: before.pre_instance_id,
        };
        let after = self.pair_binding.exact_snapshot()?;
        let claimed_after = self
            .pair_claimed_at
            .read()
            .map(|value| value.to_bits())
            .ok()?;
        (before.generation == after.generation
            && authority.pre_project_hash == after.project_hash
            && authority.pre_instance_id == after.pre_instance_id
            && claimed_before == claimed_after)
            .then_some(authority)
    }

    #[allow(clippy::too_many_arguments)]
    fn issue_local_blind_capture_request(
        &self,
        capture_generation: u64,
        clock_generation: u64,
        pre_start: i64,
        post_start: i64,
        frames: i64,
    ) -> Option<LocalBlindCaptureRequest> {
        let authority = self.local_blind_pair_authority()?;
        let root = PlatformPaths::current_kirin_tmp_root();
        let target_dir = root
            .join(&authority.pre_project_hash)
            .join(&authority.pre_instance_id);
        let request = LocalBlindCaptureRequest::new(
            authority,
            capture_generation,
            clock_generation,
            self.sample_rate,
            u8::try_from(self.num_channels).ok()?,
            pre_start,
            post_start,
            frames,
            unix_ms_now()?,
            LOCAL_BLIND_CAPTURE_LEASE_MS,
        )?;
        publish_local_blind_capture_request(&root, &target_dir, &request).ok()?;
        (self.local_blind_pair_authority().as_ref() == Some(&request.authority)).then_some(request)
    }

    fn read_local_blind_capture_request_for_pre(&self) -> Option<LocalBlindCaptureRequest> {
        if !self.is_local_blind_role(PluginDataRole::Pre) {
            return None;
        }
        let identity = self.identity_snapshot();
        let root = PlatformPaths::current_kirin_tmp_root();
        let instance_dir = root
            .join(&identity.project_hash)
            .join(&identity.instance_id);
        read_validated_local_blind_capture_request(
            &root,
            &instance_dir,
            &identity.project_hash,
            &identity.instance_id,
            self.sample_rate,
            u8::try_from(self.num_channels).ok()?,
            unix_ms_now()?,
        )
    }

    fn acknowledge_local_blind_capture_request(&self, request_id: &str) -> bool {
        let Some(now_unix_ms) = unix_ms_now() else {
            return false;
        };
        let Some(request) = self.read_local_blind_capture_request_for_pre() else {
            return false;
        };
        if request.request_id != request_id {
            return false;
        }
        let root = PlatformPaths::current_kirin_tmp_root();
        let instance_dir = root
            .join(&request.authority.pre_project_hash)
            .join(&request.authority.pre_instance_id);
        publish_local_blind_capture_armed(&root, &instance_dir, &request, now_unix_ms).is_ok()
    }

    fn local_blind_capture_is_armed(&self, request_id: &str) -> bool {
        let Some(now_unix_ms) = unix_ms_now() else {
            return false;
        };
        let Ok(channels) = u8::try_from(self.num_channels) else {
            return false;
        };
        let Some(authority) = self.local_blind_pair_authority() else {
            return false;
        };
        let root = PlatformPaths::current_kirin_tmp_root();
        let instance_dir = root
            .join(&authority.pre_project_hash)
            .join(&authority.pre_instance_id);
        let Some(request) = read_validated_local_blind_capture_request(
            &root,
            &instance_dir,
            &authority.pre_project_hash,
            &authority.pre_instance_id,
            self.sample_rate,
            channels,
            now_unix_ms,
        ) else {
            return false;
        };
        if request.request_id != request_id || request.authority != authority {
            return false;
        }
        read_matching_local_blind_capture_armed(&root, &instance_dir, &request, now_unix_ms)
            .is_some()
    }
}

fn encode_request(request: &LocalBlindCaptureRequest) -> Option<KirinLocalBlindCaptureRequest> {
    let mut encoded = KirinLocalBlindCaptureRequest {
        pair_generation: request.authority.pair_generation,
        capture_generation: request.capture_generation,
        clock_generation: request.clock_generation,
        sample_rate: request.sample_rate,
        channels: u32::from(request.channels),
        pre_start: request.pre_start,
        post_start: request.post_start,
        frames: request.frames,
        expires_at_unix_ms: request.expires_at_unix_ms,
        ..KirinLocalBlindCaptureRequest::default()
    };
    if !copy_exact(&request.request_id, &mut encoded.request_id)
        || !copy_exact(
            &request.authority.pre_project_hash,
            &mut encoded.pre_project_hash,
        )
        || !copy_exact(
            &request.authority.pre_instance_id,
            &mut encoded.pre_instance_id,
        )
    {
        return None;
    }
    Some(encoded)
}

fn unix_ms_now() -> Option<i64> {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .ok()
        .and_then(|duration| i64::try_from(duration.as_millis()).ok())
}

/// Publish one bounded request from the POST's currently owned exact PRE pair.
///
/// # Safety
///
/// `handle` must be null or point to a live `KirinHyphaEngine`. When `out` is non-null,
/// it must point to writable storage for one `KirinLocalBlindCaptureRequest`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_issue_local_blind_capture_request(
    handle: *mut KirinHyphaEngine,
    capture_generation: u64,
    clock_generation: u64,
    pre_start: i64,
    post_start: i64,
    frames: i64,
    out: *mut KirinLocalBlindCaptureRequest,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(request) = (unsafe {
            (*handle).issue_local_blind_capture_request(
                capture_generation,
                clock_generation,
                pre_start,
                post_start,
                frames,
            )
        }) else {
            return false;
        };
        let Some(encoded) = encode_request(&request) else {
            return false;
        };
        unsafe { out.write(encoded) };
        true
    }))
    .unwrap_or(false)
}

/// Read the currently valid request addressed to this exact PRE.
///
/// # Safety
///
/// `handle` must be null or point to a live `KirinHyphaEngine`. When `out` is non-null,
/// it must point to writable storage for one `KirinLocalBlindCaptureRequest`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_local_blind_capture_request(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinLocalBlindCaptureRequest,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(request) = (unsafe { (*handle).read_local_blind_capture_request_for_pre() })
        else {
            return false;
        };
        let Some(encoded) = encode_request(&request) else {
            return false;
        };
        unsafe { out.write(encoded) };
        true
    }))
    .unwrap_or(false)
}

/// Echo one request only after the PRE shell has installed its matching capture object.
///
/// # Safety
///
/// `handle` must be null or point to a live `KirinHyphaEngine`. `request_id` must be null or
/// point to a readable null-terminated string.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_ack_local_blind_capture_request(
    handle: *mut KirinHyphaEngine,
    request_id: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || request_id.is_null() {
            return false;
        }
        let request_id = unsafe { crate::read_c_str(request_id) };
        unsafe { (*handle).acknowledge_local_blind_capture_request(&request_id) }
    }))
    .unwrap_or(false)
}

/// Return true only while the exact request's PRE armed echo and pair claim remain current.
///
/// # Safety
///
/// `handle` must be null or point to a live `KirinHyphaEngine`. `request_id` must be null or
/// point to a readable null-terminated string.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_local_blind_capture_is_armed(
    handle: *mut KirinHyphaEngine,
    request_id: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || request_id.is_null() {
            return false;
        }
        let request_id = unsafe { crate::read_c_str(request_id) };
        unsafe { (*handle).local_blind_capture_is_armed(&request_id) }
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn c_request_encoding_is_exact_and_rejects_locator_truncation() {
        let authority = LocalBlindPairAuthority {
            pair_generation: 11,
            pair_owner_id: uuid::Uuid::new_v4().to_string(),
            pair_claimed_at_bits: 7.0f64.to_bits(),
            host_process_id: std::process::id(),
            post_project_hash: "post-project".into(),
            post_instance_id: "post-a".into(),
            pre_project_hash: "pre-project".into(),
            pre_instance_id: "pre-a".into(),
        };
        let request = LocalBlindCaptureRequest::new(
            authority.clone(),
            22,
            33,
            48_000,
            2,
            -96,
            0,
            192_000,
            1_000,
            10_000,
        )
        .unwrap();
        let encoded = encode_request(&request).unwrap();
        assert_eq!(encoded.request_id[36], 0);
        assert_eq!(encoded.pre_project_hash[11], 0);
        assert_eq!(encoded.pre_instance_id[5], 0);
        assert_eq!(encoded.clock_generation, 33);

        let mut too_long = authority;
        too_long.pre_project_hash = "x".repeat(LOCATOR_CAPACITY);
        let request =
            LocalBlindCaptureRequest::new(too_long, 22, 33, 48_000, 2, 0, 0, 1, 1_000, 10_000)
                .unwrap();
        assert!(encode_request(&request).is_none());
    }
}
