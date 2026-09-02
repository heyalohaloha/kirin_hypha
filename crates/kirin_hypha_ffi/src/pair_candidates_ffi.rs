//! Read-only pair candidate discovery and its fixed-buffer C ABI.

use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::{
    current_host_process_id, enumerate_live_pre_pair_choices_for_post_project_in_session,
    enumerate_owned_post_pair_candidates_for_operation_group,
    enumerate_ready_post_pair_candidates_for_operation_group, License, PlatformPaths,
};

use super::identity_registry::read_shared_id;
use super::{write_c_buf, KirinHyphaEngine, ID_BUF_LEN};

/// One PRE candidate exposed to the host's exact-pair selector.
#[repr(C)]
pub struct KirinPreCandidate {
    pub instance_id: [c_char; ID_BUF_LEN],
    pub name: [c_char; ID_BUF_LEN],
    pub has_name: u8,
}

/// One POST pair claim used to derive Keep readiness without mutating pair state.
#[repr(C)]
pub struct KirinPostPairClaim {
    pub instance_id: [c_char; ID_BUF_LEN],
    pub pair_pre_name: [c_char; ID_BUF_LEN],
    pub has_pair_pre_name: u8,
    pub paired_pre_instance_id: [c_char; ID_BUF_LEN],
    pub has_paired_pre_instance_id: u8,
}

impl KirinHyphaEngine {
    /// Enumerate PRE instances visible to this POST's current project/session boundary.
    pub fn enumerate_pre_candidates(&self) -> Vec<(String, Option<String>)> {
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw_session_id = read_shared_id(&self.daw_session_id_cell);
        enumerate_live_pre_pair_choices_for_post_project_in_session(
            &kirin_root,
            &project_hash,
            &daw_session_id,
        )
        .into_iter()
        .map(|candidate| (candidate.instance_id, candidate.name))
        .collect()
    }

    /// Count licensed POST claims ready for All Keep in the same explicit-pair operation group.
    pub fn count_keep_ready(&self) -> usize {
        if self.current_license() != License::Os {
            return 0;
        }
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw_session_id = read_shared_id(&self.daw_session_id_cell);
        enumerate_ready_post_pair_candidates_for_operation_group(
            &kirin_root,
            &project_hash,
            &daw_session_id,
            current_host_process_id(),
        )
        .len()
    }

    /// Enumerate owned POST claims in the same operation group as the ready count.
    pub fn enumerate_post_pair_claims(&self) -> Vec<(String, Option<String>, Option<String>)> {
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw_session_id = read_shared_id(&self.daw_session_id_cell);
        enumerate_owned_post_pair_candidates_for_operation_group(
            &kirin_root,
            &project_hash,
            &daw_session_id,
            current_host_process_id(),
        )
        .into_iter()
        .map(|candidate| {
            (
                candidate.instance_id,
                candidate.pair_pre_name,
                candidate.paired_pre_instance_id,
            )
        })
        .collect()
    }
}

/// Return the number of POST claims ready for All Keep. A null handle returns zero.
///
/// # Safety
/// `handle` must be null or a live engine pointer.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_count_keep_ready(handle: *mut KirinHyphaEngine) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return 0;
        }
        unsafe { (*handle).count_keep_ready() }
    }))
    .unwrap_or(0)
}

/// Write at most `cap` POST pair claims to caller-owned storage and return the written count.
///
/// # Safety
/// `handle` must be a live engine pointer. `out` must point to at least `cap` writable
/// `KirinPostPairClaim` elements. Null pointers and zero capacity return zero.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enumerate_post_pair_claims(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinPostPairClaim,
    cap: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || cap == 0 {
            return 0;
        }
        let claims = unsafe { (*handle).enumerate_post_pair_claims() };
        let count = claims.len().min(cap);
        let out = unsafe { std::slice::from_raw_parts_mut(out, count) };
        for (destination, (instance_id, pair_pre_name, paired_pre_instance_id)) in
            out.iter_mut().zip(claims.into_iter().take(count))
        {
            write_c_buf(&mut destination.instance_id, &instance_id);
            if let Some(name) = pair_pre_name {
                write_c_buf(&mut destination.pair_pre_name, &name);
                destination.has_pair_pre_name = 1;
            } else {
                write_c_buf(&mut destination.pair_pre_name, "");
                destination.has_pair_pre_name = 0;
            }
            if let Some(instance_id) = paired_pre_instance_id {
                write_c_buf(&mut destination.paired_pre_instance_id, &instance_id);
                destination.has_paired_pre_instance_id = 1;
            } else {
                write_c_buf(&mut destination.paired_pre_instance_id, "");
                destination.has_paired_pre_instance_id = 0;
            }
        }
        count
    }))
    .unwrap_or(0)
}

/// Write at most `cap` visible PRE candidates to caller-owned storage and return the written count.
///
/// # Safety
/// `handle` must be a live engine pointer. `out` must point to at least `cap` writable
/// `KirinPreCandidate` elements. Null pointers and zero capacity return zero.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_enumerate_pre_candidates(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinPreCandidate,
    cap: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() || cap == 0 {
            return 0;
        }
        let candidates = unsafe { (*handle).enumerate_pre_candidates() };
        let count = candidates.len().min(cap);
        let out = unsafe { std::slice::from_raw_parts_mut(out, count) };
        for (destination, (instance_id, name)) in
            out.iter_mut().zip(candidates.into_iter().take(count))
        {
            write_c_buf(&mut destination.instance_id, &instance_id);
            if let Some(name) = name {
                write_c_buf(&mut destination.name, &name);
                destination.has_name = 1;
            } else {
                write_c_buf(&mut destination.name, "");
                destination.has_name = 0;
            }
        }
        count
    }))
    .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::{
        kirin_hypha_count_keep_ready, kirin_hypha_enumerate_post_pair_claims,
        kirin_hypha_enumerate_pre_candidates, KirinPostPairClaim, KirinPreCandidate, ID_BUF_LEN,
    };

    #[test]
    fn candidate_c_layouts_match_the_published_header() {
        assert_eq!(std::mem::size_of::<KirinPreCandidate>(), 2 * ID_BUF_LEN + 1);
        assert_eq!(std::mem::align_of::<KirinPreCandidate>(), 1);
        assert_eq!(std::mem::offset_of!(KirinPreCandidate, name), ID_BUF_LEN);
        assert_eq!(
            std::mem::offset_of!(KirinPreCandidate, has_name),
            2 * ID_BUF_LEN
        );

        assert_eq!(
            std::mem::size_of::<KirinPostPairClaim>(),
            3 * ID_BUF_LEN + 2
        );
        assert_eq!(std::mem::align_of::<KirinPostPairClaim>(), 1);
        assert_eq!(
            std::mem::offset_of!(KirinPostPairClaim, pair_pre_name),
            ID_BUF_LEN
        );
        assert_eq!(
            std::mem::offset_of!(KirinPostPairClaim, has_pair_pre_name),
            2 * ID_BUF_LEN
        );
        assert_eq!(
            std::mem::offset_of!(KirinPostPairClaim, paired_pre_instance_id),
            2 * ID_BUF_LEN + 1
        );
        assert_eq!(
            std::mem::offset_of!(KirinPostPairClaim, has_paired_pre_instance_id),
            3 * ID_BUF_LEN + 1
        );
    }

    #[test]
    fn null_candidate_calls_fail_closed() {
        unsafe {
            assert_eq!(kirin_hypha_count_keep_ready(std::ptr::null_mut()), 0);
            assert_eq!(
                kirin_hypha_enumerate_pre_candidates(std::ptr::null_mut(), std::ptr::null_mut(), 1,),
                0
            );
            assert_eq!(
                kirin_hypha_enumerate_post_pair_claims(
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                    1,
                ),
                0
            );
        }
    }
}
