use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::{paired_pre_instance_id, PairStatus};

use super::pair_binding::ExactPairBindingSnapshot;
use super::KirinHyphaEngine;

const LOCATOR_CAPACITY: usize = 64;

#[path = "local_blind_capture_ffi.rs"]
mod local_blind_capture_ffi;
pub use local_blind_capture_ffi::*;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinExactPairBinding {
    pub pair_generation: u64,
    pub project_hash: [c_char; LOCATOR_CAPACITY],
    pub pre_instance_id: [c_char; LOCATOR_CAPACITY],
}

impl Default for KirinExactPairBinding {
    fn default() -> Self {
        Self {
            pair_generation: 0,
            project_hash: [0; LOCATOR_CAPACITY],
            pre_instance_id: [0; LOCATOR_CAPACITY],
        }
    }
}

impl KirinHyphaEngine {
    pub fn paired_pre_instance_id(&self) -> Option<String> {
        paired_pre_instance_id(&self.pair_binding.latched_pre())
    }

    pub fn paired_pre_locator(&self) -> Option<(String, String)> {
        let snapshot = self.pair_binding.exact_snapshot()?;
        Some((snapshot.project_hash, snapshot.pre_instance_id))
    }

    /// Local Blind may only retain a pair that remained exact while ownership was observed.
    /// The human name and optional host context do not participate in this authority.
    pub(crate) fn local_blind_pair_binding(&self) -> Option<ExactPairBindingSnapshot> {
        let before = self.pair_binding.exact_snapshot()?;
        if self.pair_status() != PairStatus::Paired {
            return None;
        }
        let after = self.pair_binding.exact_snapshot()?;
        (before == after).then_some(after)
    }
}

fn copy_truncated(value: &str, out: *mut c_char, out_len: usize) {
    let bytes = value.as_bytes();
    let count = bytes.len().min(out_len - 1);
    let destination = unsafe { std::slice::from_raw_parts_mut(out as *mut u8, out_len) };
    destination[..count].copy_from_slice(&bytes[..count]);
    destination[count] = 0;
}

fn copy_exact<const CAPACITY: usize>(value: &str, out: &mut [c_char; CAPACITY]) -> bool {
    let bytes = value.as_bytes();
    if bytes.is_empty() || bytes.len() >= CAPACITY {
        return false;
    }
    for (destination, source) in out.iter_mut().zip(bytes) {
        *destination = *source as c_char;
    }
    true
}

fn to_c_binding(snapshot: ExactPairBindingSnapshot) -> Option<KirinExactPairBinding> {
    if snapshot.generation == 0 {
        return None;
    }
    let mut result = KirinExactPairBinding {
        pair_generation: snapshot.generation,
        ..KirinExactPairBinding::default()
    };
    if !copy_exact(&snapshot.project_hash, &mut result.project_hash)
        || !copy_exact(&snapshot.pre_instance_id, &mut result.pre_instance_id)
    {
        return None;
    }
    Some(result)
}

/// Return the exact PRE instance currently latched by a POST.
///
/// # Safety
///
/// `handle` must be null or point to a live `KirinHyphaEngine`. When `out` is non-null,
/// it must reference a writable buffer of at least `out_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_paired_pre_instance_id(
    handle: *mut KirinHyphaEngine,
    out: *mut c_char,
    out_len: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || out_len == 0 {
            return false;
        }
        let Some(instance_id) = (unsafe { (*handle).paired_pre_instance_id() }) else {
            return false;
        };
        copy_truncated(&instance_id, out, out_len);
        true
    }))
    .unwrap_or(false)
}

/// Return the project shelf and instance ID of the exact PRE from one binding snapshot.
///
/// # Safety
///
/// `handle` must be null or point to a live `KirinHyphaEngine`. Each non-null output
/// pointer must reference a writable buffer of at least its corresponding length.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_paired_pre_locator(
    handle: *mut KirinHyphaEngine,
    project_out: *mut c_char,
    project_out_len: usize,
    instance_out: *mut c_char,
    instance_out_len: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || project_out.is_null()
            || project_out_len == 0
            || instance_out.is_null()
            || instance_out_len == 0
        {
            return false;
        }
        let Some((project_hash, instance_id)) = (unsafe { (*handle).paired_pre_locator() }) else {
            return false;
        };
        copy_truncated(&project_hash, project_out, project_out_len);
        copy_truncated(&instance_id, instance_out, instance_out_len);
        true
    }))
    .unwrap_or(false)
}

/// Return one name-independent exact pair authority for a Local Blind capture request.
/// Output remains unchanged when the pair is absent, waiting, unstable, or does not fit exactly.
///
/// # Safety
///
/// `handle` must be null or point to a live `KirinHyphaEngine`. When `out` is non-null,
/// it must point to writable storage for one `KirinExactPairBinding`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_local_blind_pair_binding(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinExactPairBinding,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(binding) = (unsafe { (*handle).local_blind_pair_binding() }) else {
            return false;
        };
        let Some(encoded) = to_c_binding(binding) else {
            return false;
        };
        unsafe { out.write(encoded) };
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_binding_encoding_rejects_truncation() {
        let valid = ExactPairBindingSnapshot {
            generation: 7,
            project_hash: "project-a".into(),
            pre_instance_id: "pre-a".into(),
        };
        let encoded = to_c_binding(valid).expect("valid binding");
        assert_eq!(encoded.pair_generation, 7);
        assert_eq!(encoded.project_hash[9], 0);
        assert_eq!(encoded.pre_instance_id[5], 0);

        let too_long = ExactPairBindingSnapshot {
            generation: 8,
            project_hash: "x".repeat(LOCATOR_CAPACITY),
            pre_instance_id: "pre-a".into(),
        };
        assert!(to_c_binding(too_long).is_none());
    }
}
