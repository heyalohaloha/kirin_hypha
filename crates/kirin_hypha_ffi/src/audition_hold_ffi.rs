//! Moves a held local Blind scope between two engines of one POST. The leases stay locked
//! throughout, so a format rebuild never reopens the project's capture exclusion.

use super::*;

pub struct KirinHyphaLocalBlindHold {
    admission: kirin_measure::AuditionAdmission,
    epoch: u64,
}

impl KirinHyphaEngine {
    fn detach_local_blind(&self, scope_epoch: u64) -> Option<Box<KirinHyphaLocalBlindHold>> {
        let mut admission = self.audition.admission.lock().ok()?;
        if self.audition.kind.load(Ordering::Acquire) != AUDITION_LOCAL_BLIND
            || scope_epoch == 0
            || scope_epoch != self.audition.epoch()
        {
            return None;
        }
        let held = admission.take()?;
        self.audition.active.store(false, Ordering::Release);
        self.audition.kind.store(AUDITION_NONE, Ordering::Release);
        Some(Box::new(KirinHyphaLocalBlindHold {
            admission: held,
            epoch: scope_epoch,
        }))
    }

    fn attach_local_blind(&self, hold: Box<KirinHyphaLocalBlindHold>) -> bool {
        let Ok(mut admission) = self.audition.admission.lock() else {
            return false;
        };
        if admission.is_some() || self.audition.kind.load(Ordering::Acquire) != AUDITION_NONE {
            return false;
        }
        *admission = Some(hold.admission);
        self.audition.epoch.store(hold.epoch, Ordering::Release);
        self.audition
            .kind
            .store(AUDITION_LOCAL_BLIND, Ordering::Release);
        self.audition.active.store(true, Ordering::Release);
        true
    }
}

/// Move a held local Blind scope out of an engine about to be destroyed. Null when none matches.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_detach_local_blind(
    handle: *mut KirinHyphaEngine,
    scope_epoch: u64,
) -> *mut KirinHyphaLocalBlindHold {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return std::ptr::null_mut();
        }
        unsafe { (*handle).detach_local_blind(scope_epoch) }
            .map_or(std::ptr::null_mut(), Box::into_raw)
    }))
    .unwrap_or(std::ptr::null_mut())
}

/// Give a detached scope, with its epoch, to the replacement engine. Consumes `hold`; a null
/// engine releases it, so a failed rebuild cannot leave the project excluded.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`]. `hold` must be
/// null or the unconsumed result of [`kirin_hypha_detach_local_blind`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_attach_local_blind(
    handle: *mut KirinHyphaEngine,
    hold: *mut KirinHyphaLocalBlindHold,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if hold.is_null() {
            return false;
        }
        let hold = unsafe { Box::from_raw(hold) };
        !handle.is_null() && unsafe { (*handle).attach_local_blind(hold) }
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;
    use uuid::Uuid;

    fn post_engine(project: &str) -> KirinHyphaEngine {
        let engine = KirinHyphaEngine::new(
            48_000,
            kirin_measure::channel_layout::ChannelLayout::stereo(),
        );
        identify(&engine, project);
        engine
    }

    fn identify(engine: &KirinHyphaEngine, project: &str) {
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        let mut identity = engine.identity.lock().unwrap();
        identity.project_hash = project.to_string();
        identity.instance_id = Uuid::new_v4().to_string();
    }

    fn as_handle(engine: &KirinHyphaEngine) -> *mut KirinHyphaEngine {
        engine as *const _ as *mut _
    }

    #[test]
    fn a_detached_scope_keeps_the_project_excluded_through_an_engine_rebuild() {
        let _serial = ADMISSION_TEST.lock().unwrap();
        let project = format!("ffi-audition-{}", Uuid::new_v4());
        let old = post_engine(&project);
        let other = post_engine(&project);
        let epoch = old.begin_local_blind().unwrap();
        let wrong = unsafe { kirin_hypha_detach_local_blind(as_handle(&old), epoch + 1) };
        assert!(wrong.is_null());
        let hold = unsafe { kirin_hypha_detach_local_blind(as_handle(&old), epoch) };
        assert!(!hold.is_null() && !old.audition.is_active());
        assert_eq!(other.begin_local_blind(), None, "detached scope stays held");
        drop(old);
        assert_eq!(other.begin_local_blind(), None, "rebuild never opens a gap");

        let rebuilt = KirinHyphaEngine::new(
            48_000,
            kirin_measure::channel_layout::ChannelLayout::stereo(),
        );
        assert!(unsafe { kirin_hypha_attach_local_blind(as_handle(&rebuilt), hold) });
        assert!(rebuilt.audition.blocks_record() && rebuilt.audition.is_active());
        assert!(
            !rebuilt.end_local_blind(epoch),
            "no identity is enabled yet"
        );
        identify(&rebuilt, &project);
        assert_eq!(rebuilt.begin_local_blind(), Some(epoch), "the same scope");
        assert!(rebuilt.end_local_blind(epoch));
        let next = other.begin_local_blind().unwrap();
        assert!(other.end_local_blind(next));
    }

    #[test]
    fn a_failed_rebuild_releases_the_detached_scope() {
        let _serial = ADMISSION_TEST.lock().unwrap();
        let project = format!("ffi-audition-{}", Uuid::new_v4());
        let old = post_engine(&project);
        let other = post_engine(&project);
        let epoch = old.begin_local_blind().unwrap();
        let hold = unsafe { kirin_hypha_detach_local_blind(as_handle(&old), epoch) };
        assert!(!unsafe { kirin_hypha_attach_local_blind(std::ptr::null_mut(), hold) });
        let next = other.begin_local_blind().unwrap();
        assert!(other.end_local_blind(next));
        let empty = std::ptr::null_mut();
        assert!(!unsafe { kirin_hypha_attach_local_blind(as_handle(&old), empty) });
    }
}
