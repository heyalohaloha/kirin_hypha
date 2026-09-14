//! Opaque, non-RT owning handles. Jobs retain their grant after UI demand is revoked.
use super::*;
use kirin_measure::reference_gain::visual::{ReferenceAnalysisGrant, ReferenceAnalysisOwner};
#[no_mangle]
pub extern "C" fn kirin_reference_analysis_create() -> *mut ReferenceAnalysisOwner {
    catch_unwind(|| Box::into_raw(Box::<ReferenceAnalysisOwner>::default()))
        .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// Live engine or null. Non-RT, engine access serialized by the caller.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_reference_analysis_owner(
    handle: *const KirinHyphaEngine,
) -> *mut ReferenceAnalysisOwner {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return std::ptr::null_mut();
        }
        Box::into_raw(Box::new(
            unsafe { &*handle }.audition.reference_owner.clone(),
        ))
    }))
    .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// Owner is live or null, and is not being dropped concurrently. Non-RT only.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_analysis_acquire(
    owner: *const ReferenceAnalysisOwner,
) -> *mut ReferenceAnalysisGrant {
    catch_unwind(AssertUnwindSafe(|| {
        if owner.is_null() {
            return std::ptr::null_mut();
        }
        unsafe { &*owner }
            .acquire()
            .map(|g| Box::into_raw(Box::new(g)))
            .unwrap_or(std::ptr::null_mut())
    }))
    .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// Both owners are live or null; non-RT only.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_analysis_same(
    a: *const ReferenceAnalysisOwner,
    b: *const ReferenceAnalysisOwner,
) -> bool {
    !a.is_null() && !b.is_null() && unsafe { (&*a).same_owner(&*b) }
}
/// # Safety
/// Uniquely owned opaque handle or null. No concurrent access through this handle.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_analysis_owner_drop(owner: *mut ReferenceAnalysisOwner) {
    if !owner.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(owner) })));
    }
}
/// # Safety
/// Uniquely owned opaque grant or null. Non-RT only.
#[no_mangle]
pub unsafe extern "C" fn kirin_reference_analysis_grant_drop(grant: *mut ReferenceAnalysisGrant) {
    if !grant.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(grant) })));
    }
}
