//! Scope copy / demand / poll are separate so no filesystem work retains an engine handle.
use super::*;
use kirin_measure::pre_candidates::pair_preview::Scope;
use std::sync::{Arc, OnceLock};
#[path = "pair_preview_service.rs"]
mod service;
use service::Ticket;

pub struct KirinPairPreview {
    ticket: Arc<Ticket>,
}
#[repr(C)]
pub struct KirinPairPreviewValue {
    pub generation: u64,
    pub complete: u8,
    pub has_single: u8,
    pub candidate: KirinPreCandidate,
}
fn module_lifetime() -> u64 {
    static ID: OnceLock<u64> = OnceLock::new();
    *ID.get_or_init(|| {
        let bytes = uuid::Uuid::new_v4().into_bytes();
        u64::from_le_bytes(bytes[..8].try_into().unwrap()).max(1)
    })
}

/// # Safety
/// Handle is live and externally protected against replacement during this value-only copy.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_pair_preview_create(
    handle: *mut KirinHyphaEngine,
) -> *mut KirinPairPreview {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(engine) = (unsafe { handle.as_ref() }) else {
            return std::ptr::null_mut();
        };
        let identity = engine.identity_snapshot();
        let scope = Scope {
            root: PlatformPaths::current_kirin_tmp_root(),
            host: current_host_process_id(),
            lifetime: module_lifetime(),
            project: read_shared_id(&engine.project_hash_cell),
            session: read_shared_id(&engine.daw_session_id_cell),
            post: identity.instance_id,
        };
        if scope.root.as_os_str().len() > 4096
            || scope.post.len() >= ID_BUF_LEN
            || scope.project.len() >= ID_BUF_LEN
            || scope.session.len() >= ID_BUF_LEN
        {
            return std::ptr::null_mut();
        }
        Box::into_raw(Box::new(KirinPairPreview {
            ticket: Ticket::new(scope),
        }))
    }))
    .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// Both pointers must remain live for this call; no filesystem access or work submission occurs.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_pair_preview_matches(
    handle: *mut KirinHyphaEngine,
    preview: *const KirinPairPreview,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let (Some(engine), Some(preview)) =
            (unsafe { handle.as_ref() }, unsafe { preview.as_ref() })
        else {
            return false;
        };
        let (Ok(identity), Ok(project), Ok(session)) = (
            engine.identity.try_lock(),
            engine.project_hash_cell.try_read(),
            engine.daw_session_id_cell.try_read(),
        ) else {
            return false;
        };
        PlatformPaths::current_kirin_tmp_root() == preview.ticket.scope.root
            && identity.instance_id == preview.ticket.scope.post
            && *project == preview.ticket.scope.project
            && *session == preview.ticket.scope.session
    }))
    .unwrap_or(false)
}
/// # Safety
/// Preview must be live. Call only outside the engine handle lock, on a non-audio thread.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_pair_preview_request(
    preview: *const KirinPairPreview,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        (unsafe { preview.as_ref() })
            .is_some_and(|p| service::shared().is_some_and(|service| service.request(&p.ticket)))
    }))
    .unwrap_or(false)
}
/// # Safety
/// Preview must be live; out must point to writable storage. Poll never waits for the worker.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_pair_preview_poll(
    preview: *const KirinPairPreview,
    out: *mut KirinPairPreviewValue,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let (Some(preview), Some(out)) = (unsafe { preview.as_ref() }, unsafe { out.as_mut() })
        else {
            return false;
        };
        let Some(result) = preview.ticket.poll() else {
            return false;
        };
        out.generation = result.generation;
        out.complete = result.snapshot.stopped.is_none() as u8;
        out.has_single = 0;
        if let Some(candidate) = result.snapshot.single_available() {
            write_c_buf(&mut out.candidate.instance_id, &candidate.id);
            write_c_buf(
                &mut out.candidate.name,
                candidate.name.as_deref().unwrap_or(""),
            );
            out.candidate.has_name = candidate.name.is_some() as u8;
            out.has_single = 1;
        }
        true
    }))
    .unwrap_or(false)
}
/// # Safety
/// Preview is null or live; cancel never joins the worker.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_pair_preview_cancel(preview: *const KirinPairPreview) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Some(p) = unsafe { preview.as_ref() } {
            p.ticket.cancel();
        }
    }));
}
/// # Safety
/// Preview was returned by create and is destroyed exactly once, with no other caller using it.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_pair_preview_destroy(preview: *mut KirinPairPreview) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !preview.is_null() {
            let p = unsafe { Box::from_raw(preview) };
            p.ticket.cancel();
        }
    }));
}
/// Called by the native module unload guard, after instances are destroyed, on a non-audio thread.
#[no_mangle]
pub extern "C" fn kirin_hypha_pair_preview_shutdown() {
    let _ = catch_unwind(AssertUnwindSafe(service::shutdown));
}
