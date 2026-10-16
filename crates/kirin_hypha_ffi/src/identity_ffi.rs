//! State-chunk identity storage and its stable C ABI.

use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Arc;

#[path = "instance_identity_lease.rs"]
mod instance_identity_lease;

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
    // The engine owns the claim until its Drop has joined every IO/measurement worker. Snapshots
    // can temporarily share it, but watchdog generations never acquire or replace this claim.
    runtime_claim: Option<Arc<instance_identity_lease::InstanceIdentityLease>>,
    claim_failed: bool,
}

impl IdentityState {
    pub(crate) fn claim_for_writes(&mut self, role: &str) -> bool {
        if self.runtime_claim.is_some() {
            return true;
        }
        let root = kirin_measure::PlatformPaths::current_kirin_tmp_root();
        self.claim_at(&root, role)
    }

    fn claim_at(&mut self, root: &std::path::Path, role: &str) -> bool {
        match instance_identity_lease::InstanceIdentityLease::acquire(root, role, &self.instance_id)
        {
            Ok((id, claim)) => {
                self.instance_id = id;
                self.runtime_claim = Some(Arc::new(claim));
                self.claim_failed = false;
                true
            }
            Err(_) => {
                self.claim_failed = true;
                false
            }
        }
    }
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

/// Additive identity DTO that completely represents all legal 64-byte path components.
#[repr(C)]
pub struct KirinIdentityV2 {
    pub instance_id: [c_char; 65],
    pub project_uuid: [c_char; 65],
    pub daw_session_uuid: [c_char; 65],
    pub name: [c_char; ID_BUF_LEN],
}

/// Read the complete non-RT identity. False preserves the caller's entire output.
///
/// # Safety
/// `handle` must be null or a live engine; non-null `out` must point to writable DTO storage.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_get_identity_v2(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinIdentityV2,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(identity) = (unsafe { &*handle }).identity.try_lock().ok() else {
            return false;
        };
        let paths = [
            &identity.instance_id,
            &identity.project_uuid,
            &identity.daw_session_uuid,
        ];
        if identity.claim_failed
            || identity.runtime_claim.is_none()
            || identity.instance_id.is_empty()
            || paths
                .iter()
                .any(|value| !value.is_empty() && !kirin_measure::is_path_safe_component(value))
        {
            return false;
        }
        let mut result = KirinIdentityV2 {
            instance_id: [0; 65],
            project_uuid: [0; 65],
            daw_session_uuid: [0; 65],
            name: [0; ID_BUF_LEN],
        };
        for (value, buffer) in paths.into_iter().zip([
            &mut result.instance_id,
            &mut result.project_uuid,
            &mut result.daw_session_uuid,
        ]) {
            for (destination, byte) in buffer.iter_mut().zip(value.bytes()) {
                *destination = byte as c_char;
            }
        }
        write_c_buf(&mut result.name, &identity.name);
        unsafe {
            out.write(result);
        }
        true
    }))
    .unwrap_or(false)
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
            // IO workers snapshot these fields when the claim is acquired. A shell retry after
            // a busy readback must resume that claim, never restore an old copied ID over it.
            if identity.runtime_claim.is_some() {
                return;
            }
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
        // An attempted writer initialization that failed to claim identity must not authorize
        // shell transports. Preserve the stored identity for recovery; return an empty readback.
        write_c_buf(
            &mut out.instance_id,
            if identity.claim_failed {
                ""
            } else {
                &identity.instance_id
            },
        );
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

    #[test]
    fn failed_claim_keeps_stored_identity_but_returns_no_publication_authority() {
        let root = tempfile::tempdir().unwrap();
        std::fs::write(root.path().join(".instance_identity"), b"blocked").unwrap();
        let mut engine = super::KirinHyphaEngine::new(48000, crate::ChannelLayout::stereo());
        engine.set_identity(
            "restored-id".into(),
            "project-id".into(),
            "".into(),
            "name".into(),
        );
        {
            let mut identity = engine.identity.lock().unwrap();
            assert!(!identity.claim_at(root.path(), "pre"));
            assert_eq!(identity.instance_id, "restored-id");
            assert!(identity.runtime_claim.is_none());
        }
        let mut out: KirinIdentity = unsafe { std::mem::zeroed() };
        unsafe { super::kirin_hypha_get_identity(&mut engine, &mut out) };
        assert_eq!(out.instance_id[0], 0);
        let mut full: super::KirinIdentityV2 = unsafe { std::mem::zeroed() };
        full.instance_id[0] = 42;
        assert!(!unsafe { super::kirin_hypha_get_identity_v2(&mut engine, &mut full) });
        assert_eq!(full.instance_id[0], 42);
        std::fs::remove_file(root.path().join(".instance_identity")).unwrap();
        assert!(engine.identity.lock().unwrap().claim_at(root.path(), "pre"));
        unsafe { super::kirin_hypha_get_identity(&mut engine, &mut out) };
        assert_eq!(out.instance_id[0], b'r' as c_char);
    }

    #[test]
    fn full_readback_and_claim_do_not_alias_a_63_byte_prefix() {
        let root = tempfile::tempdir().unwrap();
        let prefix = "x".repeat(63);
        let full = format!("{prefix}y");
        let mut engines: Vec<_> = [&prefix, &full]
            .into_iter()
            .map(|id| {
                let engine = super::KirinHyphaEngine::new(48000, crate::ChannelLayout::stereo());
                engine.set_identity(id.into(), full.clone(), full.clone(), "name".into());
                assert!(engine.identity.lock().unwrap().claim_at(root.path(), "pre"));
                engine
            })
            .collect();
        for (engine, expected) in engines.iter_mut().zip([prefix, full]) {
            let mut out = super::KirinIdentityV2 {
                instance_id: [1; 65],
                project_uuid: [1; 65],
                daw_session_uuid: [1; 65],
                name: [1; 64],
            };
            assert!(unsafe { super::kirin_hypha_get_identity_v2(engine, &mut out) });
            let value = unsafe { std::ffi::CStr::from_ptr(out.instance_id.as_ptr()) }
                .to_str()
                .unwrap();
            assert_eq!(value, expected);
            assert_eq!(out.project_uuid[64], 0);
            assert_eq!(out.daw_session_uuid[64], 0);
        }
    }

    #[test]
    fn busy_readback_retry_cannot_replace_a_claimed_identity() {
        let root = tempfile::tempdir().unwrap();
        let (_, original) = super::instance_identity_lease::InstanceIdentityLease::acquire(
            root.path(),
            "pre",
            "copied-id",
        )
        .unwrap();
        let engine = super::KirinHyphaEngine::new(48000, crate::ChannelLayout::stereo());
        engine.set_identity(
            "copied-id".into(),
            "project".into(),
            "session".into(),
            "name".into(),
        );
        assert!(engine.identity.lock().unwrap().claim_at(root.path(), "pre"));
        let claimed = engine.identity_snapshot();
        assert_ne!(claimed.instance_id, "copied-id");
        let handle = &engine as *const super::KirinHyphaEngine as *mut super::KirinHyphaEngine;
        let mut out: super::KirinIdentityV2 = unsafe { std::mem::zeroed() };
        out.instance_id[0] = 42;
        {
            let _busy = engine.identity.lock().unwrap();
            assert!(!unsafe { super::kirin_hypha_get_identity_v2(handle, &mut out) });
            assert_eq!(out.instance_id[0], 42);
        }
        engine.set_identity(
            "copied-id".into(),
            "other-project".into(),
            "other-session".into(),
            "old-name".into(),
        );
        assert!(engine.identity.lock().unwrap().claim_for_writes("pre"));
        assert!(unsafe { super::kirin_hypha_get_identity_v2(handle, &mut out) });
        let read = |buffer: &[c_char]| {
            unsafe { std::ffi::CStr::from_ptr(buffer.as_ptr()) }
                .to_str()
                .unwrap()
                .to_owned()
        };
        assert_eq!(read(&out.instance_id), claimed.instance_id);
        assert_eq!(read(&out.project_uuid), claimed.project_uuid);
        assert_eq!(read(&out.daw_session_uuid), claimed.daw_session_uuid);
        assert_eq!(read(&out.name), claimed.name);
        assert!(
            super::instance_identity_lease::InstanceIdentityLease::acquire(
                root.path(),
                "pre",
                &claimed.instance_id
            )
            .unwrap()
            .0 != claimed.instance_id
        );
        drop(original);
    }
}
