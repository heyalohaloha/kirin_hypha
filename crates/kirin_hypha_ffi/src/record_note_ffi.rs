//! Sample-exact Record MARK/NOTE UI boundary and legacy closed-file annotation compatibility.

use super::{read_c_str, KirinHyphaEngine};
use kirin_measure::{
    append_annotation_to_latest, can_write_plugin_data, enqueue_record_mark, enqueue_record_note,
    PluginDataRole, StoragePaths,
};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

impl KirinHyphaEngine {
    pub fn add_mark(&self, tag: String) -> bool {
        self.can_add_exact_annotation()
            && enqueue_record_mark(
                &self.record_sm,
                &self.record_take_tracker,
                &self.record_mark_queue,
                &tag,
            )
            .is_ok()
    }

    pub fn add_note(&self, memo: String) -> bool {
        self.can_add_exact_annotation()
            && enqueue_record_note(
                &self.record_sm,
                &self.record_take_tracker,
                &self.record_mark_queue,
                &memo,
            )
            .is_ok()
    }

    fn can_add_exact_annotation(&self) -> bool {
        can_write_plugin_data(self.current_license())
            && self.write_role.lock().ok().and_then(|role| *role) == Some(PluginDataRole::Post)
    }

    pub fn add_annotation(&self, memo: String) -> bool {
        if !can_write_plugin_data(self.current_license()) {
            return false;
        }
        let role = match self.write_role.lock().ok().and_then(|role| *role) {
            Some(role) => role,
            None => return false,
        };
        let (project_hash, instance_id) = match self.identity.lock() {
            Ok(identity)
                if !identity.project_hash.is_empty() && !identity.instance_id.is_empty() =>
            {
                (identity.project_hash.clone(), identity.instance_id.clone())
            }
            _ => return false,
        };
        let base = match StoragePaths::default_platform() {
            Ok(paths) => paths.plugin_data_dir(),
            Err(_) => return false,
        };
        append_annotation_to_latest(&base, &project_hash, &instance_id, role, memo).unwrap_or(false)
    }
}

/// Adds a free-text note at the current sample-exact Record position.
///
/// # Safety
/// `handle` must be null or a live pointer returned by this library. `memo` must be null or point
/// to a readable, NUL-terminated C string for the duration of this call.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_add_note(
    handle: *mut KirinHyphaEngine,
    memo: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (*handle).add_note(read_c_str(memo)) }
    }))
    .unwrap_or(false)
}

/// Adds a legacy wall-clock annotation to the latest closed Record file.
///
/// # Safety
/// `handle` must be null or a live pointer returned by this library. `memo` must be null or point
/// to a readable, NUL-terminated C string for the duration of this call.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_add_annotation(
    handle: *mut KirinHyphaEngine,
    memo: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (*handle).add_annotation(read_c_str(memo)) }
    }))
    .unwrap_or(false)
}

/// Adds a fixed-tag mark at the current sample-exact Record position.
///
/// # Safety
/// `handle` must be null or a live pointer returned by this library. `tag` must be null or point
/// to a readable, NUL-terminated C string for the duration of this call.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_add_mark(
    handle: *mut KirinHyphaEngine,
    tag: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (*handle).add_mark(read_c_str(tag)) }
    }))
    .unwrap_or(false)
}
