//! Non-RT admission shared by Reference and local PRE/POST Blind audition.

use super::*;

pub(crate) const AUDITION_NONE: u8 = 0;
pub(crate) const AUDITION_REFERENCE: u8 = 1;
const AUDITION_LOCAL_BLIND: u8 = 2;

pub(crate) struct AuditionState {
    active: Arc<AtomicBool>,
    admission: Mutex<Option<kirin_measure::AuditionAdmission>>,
    kind: Arc<AtomicU8>,
    epoch: AtomicU64,
}

impl AuditionState {
    pub(crate) fn new() -> Self {
        Self {
            active: Arc::new(AtomicBool::new(false)),
            admission: Mutex::new(None),
            kind: Arc::new(AtomicU8::new(AUDITION_NONE)),
            epoch: AtomicU64::new(0),
        }
    }

    pub(crate) fn active_handle(&self) -> Arc<AtomicBool> {
        Arc::clone(&self.active)
    }

    pub(crate) fn blocks_record(&self) -> bool {
        self.kind.load(Ordering::Acquire) != AUDITION_NONE
    }

    pub(crate) fn reject_keep(&self, notice: &RwLock<Option<String>>) -> bool {
        if !self.blocks_record() {
            return false;
        }
        if let Ok(mut message) = notice.write() {
            *message = Some("Blind Compare active".to_string());
        }
        true
    }

    #[cfg(test)]
    pub(crate) fn is_active(&self) -> bool {
        self.active.load(Ordering::Acquire)
    }
}

impl KirinHyphaEngine {
    fn audition_project(&self) -> Option<String> {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return None;
        }
        let identity = self.identity.lock().ok()?;
        (!identity.project_hash.is_empty() && !identity.instance_id.is_empty())
            .then(|| identity.project_hash.clone())
    }

    pub(crate) fn begin_audition(&self, kind: u8, owner: &str) -> Option<u64> {
        if !matches!(kind, AUDITION_REFERENCE | AUDITION_LOCAL_BLIND) {
            return None;
        }
        let project_hash = self.audition_project()?;
        if self.record_sm.is_recording()
            || self.keep_phase.load(Ordering::Acquire) != KIRIN_KEEP_PHASE_IDLE
        {
            return None;
        }
        let mut admission = self.audition.admission.lock().ok()?;
        let current = self.audition.kind.load(Ordering::Acquire);
        if current == kind {
            return Some(self.audition.epoch.load(Ordering::Acquire)).filter(|epoch| *epoch != 0);
        }
        if current != AUDITION_NONE || admission.is_some() {
            return None;
        }
        let plugin_data_dir = StoragePaths::default_platform().ok()?.plugin_data_dir();
        let mut candidate =
            kirin_measure::AuditionAdmission::for_current_project(&plugin_data_dir, &project_hash);
        if !candidate.try_acquire_for(owner).ok()? {
            return None;
        }
        if self.record_sm.is_recording()
            || self.keep_phase.load(Ordering::Acquire) != KIRIN_KEEP_PHASE_IDLE
        {
            candidate.release();
            return None;
        }
        *admission = Some(candidate);
        let mut epoch = self
            .audition
            .epoch
            .fetch_add(1, Ordering::AcqRel)
            .wrapping_add(1);
        if epoch == 0 {
            epoch = 1;
            self.audition.epoch.store(epoch, Ordering::Release);
        }
        self.audition.kind.store(kind, Ordering::Release);
        self.audition.active.store(true, Ordering::Release);
        *self
            .delta_result
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = DeltaResult::default();
        if let Some(history) = self.meter_delta_history.as_ref() {
            history.reset();
        }
        Some(epoch)
    }

    pub(crate) fn end_audition(&self, kind: u8, expected_epoch: Option<u64>) -> bool {
        if self.audition_project().is_none() {
            return false;
        }
        let Ok(mut admission) = self.audition.admission.lock() else {
            return false;
        };
        let current = self.audition.kind.load(Ordering::Acquire);
        if current == AUDITION_NONE {
            return true;
        }
        if current != kind {
            return false;
        }
        if expected_epoch
            .is_some_and(|epoch| epoch == 0 || epoch != self.audition.epoch.load(Ordering::Acquire))
        {
            return false;
        }
        self.audition.active.store(false, Ordering::Release);
        if let Some(mut held) = admission.take() {
            held.release();
        }
        self.audition.kind.store(AUDITION_NONE, Ordering::Release);
        true
    }

    pub fn begin_local_blind(&self) -> Option<u64> {
        self.begin_audition(AUDITION_LOCAL_BLIND, "PRE POST Blind")
    }

    pub fn end_local_blind(&self, scope_epoch: u64) -> bool {
        self.end_audition(AUDITION_LOCAL_BLIND, Some(scope_epoch))
    }
}

/// Reserve the POST's project-wide local Blind scope on the control thread.
///
/// # Safety
/// `handle` and `out_scope_epoch` must be null or valid writable/live pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_begin_local_blind(
    handle: *mut KirinHyphaEngine,
    out_scope_epoch: *mut u64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out_scope_epoch.is_null() {
            return false;
        }
        let Some(epoch) = (unsafe { (*handle).begin_local_blind() }) else {
            return false;
        };
        unsafe { *out_scope_epoch = epoch };
        true
    }))
    .unwrap_or(false)
}

/// Release a local Blind scope after its RT normal-output receipt has been observed.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_end_local_blind(
    handle: *mut KirinHyphaEngine,
    scope_epoch: u64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (*handle).end_local_blind(scope_epoch) }
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn post_engine(project: &str) -> KirinHyphaEngine {
        let engine = KirinHyphaEngine::new(48_000, 2);
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        let mut identity = engine.identity.lock().unwrap();
        identity.project_hash = project.to_string();
        identity.instance_id = Uuid::new_v4().to_string();
        drop(identity);
        engine
    }

    #[test]
    fn local_blind_is_post_only_and_returns_a_stable_epoch() {
        let pre = KirinHyphaEngine::new(48_000, 2);
        assert_eq!(pre.begin_local_blind(), None);
        let post = post_engine(&format!("ffi-audition-{}", Uuid::new_v4()));
        let epoch = post.begin_local_blind().unwrap();
        assert_ne!(epoch, 0);
        assert_eq!(post.begin_local_blind(), Some(epoch));
        assert!(post.audition.is_active());
        assert!(!post.end_local_blind(epoch.wrapping_add(1)));
        assert!(post.end_local_blind(epoch));
        assert!(!post.audition.is_active());
    }

    #[test]
    fn local_blind_rejects_record_and_keep_preparation() {
        let post = post_engine(&format!("ffi-audition-{}", Uuid::new_v4()));
        post.keep_phase
            .store(KIRIN_KEEP_PHASE_PREPARING, Ordering::Release);
        assert_eq!(post.begin_local_blind(), None);
        post.keep_phase
            .store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
        post.record_sm.try_enter_record(License::Os).unwrap();
        assert_eq!(post.begin_local_blind(), None);
    }

    #[test]
    fn null_local_blind_ffi_fails_closed() {
        let mut epoch = 0;
        assert!(!unsafe { kirin_hypha_begin_local_blind(std::ptr::null_mut(), &mut epoch) });
        assert!(!unsafe { kirin_hypha_end_local_blind(std::ptr::null_mut(), 1) });
    }
}
