//! Complete, non-RT identity authority for the shared VU presentation preference.

use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::{is_path_safe_component, PluginDataRole};

use crate::KirinHyphaEngine;

fn locator(engine: &KirinHyphaEngine) -> Option<(String, String)> {
    let role = (*engine.write_role.try_lock().ok()?)?;
    if role == PluginDataRole::Post {
        let authority = engine.pair_binding.try_observation_snapshot()?;
        if let Some(exact) = authority.exact {
            return Some((exact.project_hash, exact.pre_instance_id));
        }
        // A selected PRE still being resolved cannot borrow the unpaired POST's preference.
        if authority.selection_intent {
            return None;
        }
    }
    let identity = engine.identity.try_lock().ok()?;
    if !is_path_safe_component(&identity.project_hash)
        || !is_path_safe_component(&identity.instance_id)
    {
        return None;
    }
    Some((identity.project_hash.clone(), identity.instance_id.clone()))
}

/// Return complete project and instance identities for a VU calibration preference.
/// PRE and an unpaired POST use their resolved identity. A selected POST uses the exact PRE
/// locator, including while that PRE is absent. Busy, unresolved, invalid and undersized calls
/// return false without changing either output. No filesystem operation is performed.
///
/// # Safety
/// `handle` must be null or a live engine. Each non-null output must refer to writable storage
/// of its supplied length. This getter belongs to the non-RT control/message thread.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_vu_calibration_locator(
    handle: *mut KirinHyphaEngine,
    project_out: *mut c_char,
    project_out_len: usize,
    instance_out: *mut c_char,
    instance_out_len: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || project_out.is_null() || instance_out.is_null() {
            return false;
        }
        let Some((project, instance)) = locator(unsafe { &*handle }) else {
            return false;
        };
        let project_size = project.len() + 1;
        let instance_size = instance.len() + 1;
        if project_out_len < project_size || instance_out_len < instance_size {
            return false;
        }
        let Some(project_end) = (project_out as usize).checked_add(project_size) else {
            return false;
        };
        let Some(instance_end) = (instance_out as usize).checked_add(instance_size) else {
            return false;
        };
        if (project_out as usize) < instance_end && (instance_out as usize) < project_end {
            return false;
        }
        // Both identities, capacities and regions are validated before the first output write.
        unsafe {
            std::ptr::copy_nonoverlapping(project.as_ptr(), project_out.cast(), project.len());
            project_out.add(project.len()).write(0);
            std::ptr::copy_nonoverlapping(instance.as_ptr(), instance_out.cast(), instance.len());
            instance_out.add(instance.len()).write(0);
        }
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
#[path = "vu_calibration_locator_tests.rs"]
mod tests;
