//! Non-audio handoff of a POST's downstream tail to capture admission.
use super::*;
use kirin_measure::candidate_tail::TailLease;

pub struct CandidateTailHold {
    _lease: TailLease,
    pending: Arc<AtomicBool>,
}
impl Drop for CandidateTailHold {
    fn drop(&mut self) {
        self.pending.store(false, Ordering::Release);
    }
}

/// The caller retains the returned hold through engine rebuilds and releases it after drain proof.
/// The existing audition exclusion must still be held while acquiring it.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_candidate_tail_begin(
    handle: *mut KirinHyphaEngine,
) -> *mut CandidateTailHold {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(engine) = (unsafe { handle.as_ref() }) else {
            return std::ptr::null_mut();
        };
        let Ok(identity) = engine.identity.lock() else {
            return std::ptr::null_mut();
        };
        let Ok(paths) = StoragePaths::default_platform() else {
            return std::ptr::null_mut();
        };
        let Ok(Some(lease)) = TailLease::acquire(
            &paths.plugin_data_dir(),
            &identity.project_hash,
            &identity.instance_id,
        ) else {
            return std::ptr::null_mut();
        };
        let pending = Arc::clone(&engine.audition.tail_pending);
        pending.store(true, Ordering::Release);
        Box::into_raw(Box::new(CandidateTailHold {
            _lease: lease,
            pending,
        }))
    }))
    .unwrap_or(std::ptr::null_mut())
}

/// Attach the still-held tail to a replacement engine without releasing its cross-format lease.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_candidate_tail_attach(
    handle: *mut KirinHyphaEngine,
    hold: *mut CandidateTailHold,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let (Some(engine), Some(hold)) = (unsafe { handle.as_ref() }, unsafe { hold.as_mut() })
        else {
            return false;
        };
        let next = Arc::clone(&engine.audition.tail_pending);
        if Arc::ptr_eq(&next, &hold.pending) {
            return true;
        }
        next.store(true, Ordering::Release);
        hold.pending.store(false, Ordering::Release);
        hold.pending = next;
        true
    }))
    .unwrap_or(false)
}

#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_candidate_tail_end(hold: *mut CandidateTailHold) {
    if !hold.is_null() {
        drop(unsafe { Box::from_raw(hold) });
    }
}

#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_all_keep_tail_pending(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(engine) = (unsafe { handle.as_ref() }) else {
            return true;
        };
        let Ok(group) = engine.audition.tail_group.lock() else {
            return true;
        };
        let Some(group) = group.as_ref() else {
            return false;
        };
        let Ok(paths) = StoragePaths::default_platform() else {
            return true;
        };
        group.iter().any(|(project, post, _)| {
            !matches!(
                TailLease::acquire(&paths.plugin_data_dir(), project, post),
                Ok(Some(_))
            )
        })
    }))
    .unwrap_or(true)
}

#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_keep_all_resume(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        unsafe { handle.as_ref() }.is_some_and(|engine| engine.keep_all_impl(true))
    }))
    .unwrap_or(false)
}
