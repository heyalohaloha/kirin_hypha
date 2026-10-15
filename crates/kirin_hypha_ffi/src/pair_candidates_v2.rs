//! Complete opaque IDs for the shipping shell; display labels keep their legacy capacity.
use super::{write_c_buf, KirinHyphaEngine};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

#[repr(C)]
pub struct KirinPreCandidateV2 {
    pub instance_id: [c_char; 65],
    pub name: [c_char; 64],
    pub has_name: u8,
}

#[repr(C)]
pub struct KirinPostPairClaimV2 {
    pub instance_id: [c_char; 65],
    pub pair_pre_name: [c_char; 64],
    pub has_pair_pre_name: u8,
    pub paired_pre_instance_id: [c_char; 65],
    pub has_paired_pre_instance_id: u8,
}

fn complete_id(value: &str) -> Option<[c_char; 65]> {
    if !kirin_measure::is_path_safe_component(value) {
        return None;
    }
    let mut out = [0; 65];
    for (slot, byte) in out.iter_mut().zip(value.bytes()) {
        *slot = byte as c_char;
    }
    Some(out)
}

/// Complete PRE candidates. Invalid IDs are excluded, never shortened into another identity.
/// # Safety
/// A non-null handle must be live and out must hold at least cap writable V2 elements.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enumerate_pre_candidates_v2(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinPreCandidateV2,
    cap: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || cap == 0 {
            return 0;
        }
        let mut count = 0;
        for (id, name) in unsafe { (*handle).enumerate_pre_candidates() } {
            let Some(instance_id) = complete_id(&id) else {
                continue;
            };
            let mut value = KirinPreCandidateV2 {
                instance_id,
                name: [0; 64],
                has_name: u8::from(name.is_some()),
            };
            write_c_buf(&mut value.name, name.as_deref().unwrap_or(""));
            unsafe {
                out.add(count).write(value);
            }
            count += 1;
            if count == cap {
                break;
            }
        }
        count
    }))
    .unwrap_or(0)
}

/// Complete POST claims for the same operation group as the legacy getter.
/// # Safety
/// A non-null handle must be live and out must hold at least cap writable V2 elements.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enumerate_post_pair_claims_v2(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinPostPairClaimV2,
    cap: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || cap == 0 {
            return 0;
        }
        let mut count = 0;
        for (id, name, pre) in unsafe { (*handle).enumerate_post_pair_claims() } {
            let Some(instance_id) = complete_id(&id) else {
                continue;
            };
            let paired_pre_instance_id = match pre.as_deref() {
                Some(id) => {
                    let Some(id) = complete_id(id) else {
                        continue;
                    };
                    id
                }
                None => [0; 65],
            };
            let mut value = KirinPostPairClaimV2 {
                instance_id,
                pair_pre_name: [0; 64],
                has_pair_pre_name: u8::from(name.is_some()),
                paired_pre_instance_id,
                has_paired_pre_instance_id: u8::from(pre.is_some()),
            };
            write_c_buf(&mut value.pair_pre_name, name.as_deref().unwrap_or(""));
            unsafe {
                out.add(count).write(value);
            }
            count += 1;
            if count == cap {
                break;
            }
        }
        count
    }))
    .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn full_candidate_identity_does_not_alias_its_prefix() {
        let short = "x".repeat(63);
        let full = format!("{short}y");
        assert_ne!(complete_id(&short).unwrap(), complete_id(&full).unwrap());
        assert_eq!(complete_id(&full).unwrap()[64], 0);
        assert!(complete_id(&"x".repeat(65)).is_none());
    }
}
