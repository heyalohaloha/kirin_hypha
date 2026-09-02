//! State-chunk identity storage and its stable C ABI.

use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

use super::{read_c_str, write_c_buf, KirinHyphaEngine, ID_BUF_LEN};

/// Identity restored from and persisted to the host-owned state chunk.
///
/// `project_hash` is derived from the resolved `project_uuid` and is not part of the persisted ABI.
#[derive(Default, Clone)]
pub(crate) struct IdentityState {
    pub(crate) instance_id: String,
    pub(crate) project_uuid: String,
    pub(crate) daw_session_uuid: String,
    pub(crate) name: String,
    /// Resolved project shelf used by annotation and plugin-data paths after writes are enabled.
    pub(crate) project_hash: String,
}

/// State-chunk identity exchanged with the host shell.
///
/// Every field is a null-terminated C string with at most 63 bytes plus the terminator.
/// `project_hash` is excluded because it is derived after role identity resolution.
#[repr(C)]
pub struct KirinIdentity {
    pub instance_id: [c_char; ID_BUF_LEN],
    pub project_uuid: [c_char; ID_BUF_LEN],
    pub daw_session_uuid: [c_char; ID_BUF_LEN],
    pub name: [c_char; ID_BUF_LEN],
}

impl KirinHyphaEngine {
    /// Restore host state before enabling PRE or POST writes.
    ///
    /// This is the single materialization boundary for restored path identities. Safe and empty
    /// fields remain unchanged; unsafe fields are replaced before any engine or IO path can read
    /// them. `name` is display metadata rather than a path component and remains verbatim.
    pub fn set_identity(
        &self,
        instance_id: String,
        project_uuid: String,
        daw_session_uuid: String,
        name: String,
    ) {
        if let Ok(mut identity) = self.identity.lock() {
            let instance_id = kirin_measure::materialize_restore_field(
                &instance_id,
                "ffi.set_identity.instance_id",
                None,
            );
            let event_tag = if instance_id.is_empty() {
                None
            } else {
                Some(instance_id.as_str())
            };
            identity.project_uuid = kirin_measure::materialize_restore_field(
                &project_uuid,
                "ffi.set_identity.project_uuid",
                event_tag,
            );
            identity.daw_session_uuid = kirin_measure::materialize_restore_field(
                &daw_session_uuid,
                "ffi.set_identity.daw_session_uuid",
                event_tag,
            );
            identity.instance_id = instance_id;
            identity.name = name;
        }
    }

    /// Snapshot the current state-chunk identity without exposing the live mutex guard.
    pub(crate) fn identity_snapshot(&self) -> IdentityState {
        self.identity
            .lock()
            .map(|identity| identity.clone())
            .unwrap_or_default()
    }
}

/// Restore identity fields from a host state chunk. Call before enabling PRE or POST writes.
///
/// Null string pointers are treated as empty strings. Empty path identities are generated later
/// when writes are enabled.
///
/// # Safety
/// `handle` must be a live engine pointer. Each string pointer must be null or point to a valid
/// null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_identity(
    handle: *mut KirinHyphaEngine,
    instance_id: *const c_char,
    project_uuid: *const c_char,
    daw_session_uuid: *const c_char,
    name: *const c_char,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        let instance_id = unsafe { read_c_str(instance_id) };
        let project_uuid = unsafe { read_c_str(project_uuid) };
        let daw_session_uuid = unsafe { read_c_str(daw_session_uuid) };
        let name = unsafe { read_c_str(name) };
        unsafe { (*handle).set_identity(instance_id, project_uuid, daw_session_uuid, name) };
    }));
}

/// Write the current state-chunk identity to `out` for host persistence.
///
/// # Safety
/// `handle` must be a live engine pointer and `out` must point to writable `KirinIdentity` storage.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_identity(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinIdentity,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return;
        }
        let identity = unsafe { (*handle).identity_snapshot() };
        let out = unsafe { &mut *out };
        write_c_buf(&mut out.instance_id, &identity.instance_id);
        write_c_buf(&mut out.project_uuid, &identity.project_uuid);
        write_c_buf(&mut out.daw_session_uuid, &identity.daw_session_uuid);
        write_c_buf(&mut out.name, &identity.name);
    }));
}

#[cfg(test)]
mod tests {
    use super::{KirinIdentity, ID_BUF_LEN};
    use std::os::raw::c_char;

    #[test]
    fn identity_c_layout_remains_four_fixed_buffers() {
        assert_eq!(
            std::mem::size_of::<KirinIdentity>(),
            4 * ID_BUF_LEN * std::mem::size_of::<c_char>()
        );
        assert_eq!(std::mem::align_of::<KirinIdentity>(), 1);
    }
}
