//! Non-RT admission shared by Reference and local PRE/POST Blind audition.

use super::*;
#[path = "reference_analysis_ffi.rs"]
mod reference_analysis;

#[cfg(test)]
pub(crate) static ADMISSION_TEST: Mutex<()> = Mutex::new(());

pub(crate) const AUDITION_NONE: u8 = 0;
pub(crate) const AUDITION_REFERENCE: u8 = 1;
const AUDITION_LOCAL_BLIND: u8 = 2;

pub(crate) struct AuditionState {
    active: Arc<AtomicBool>,
    admission: Mutex<Option<kirin_measure::AuditionAdmission>>,
    kind: Arc<AtomicU8>,
    epoch: AtomicU64,
    version_blind: Mutex<Option<kirin_measure::reference_gain::visual::BlindCaptureExclusion>>,
    capture: Mutex<Option<kirin_measure::reference_gain::visual::CaptureAdmission>>,
    reference_owner: kirin_measure::reference_gain::visual::ReferenceAnalysisOwner,
}

impl AuditionState {
    pub(crate) fn new() -> Self {
        Self {
            active: Arc::new(AtomicBool::new(false)),
            admission: Mutex::new(None),
            kind: Arc::new(AtomicU8::new(AUDITION_NONE)),
            epoch: AtomicU64::new(0),
            capture: Mutex::new(None),
            reference_owner: Default::default(),
            version_blind: Mutex::new(None),
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
        let accepted = if kind == AUDITION_LOCAL_BLIND {
            candidate.try_acquire_blind_for(owner)
        } else {
            candidate.try_acquire_shared(&self.audition.reference_owner, owner)
        };
        if !accepted.ok()? {
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

    fn set_version_blind_capture_exclusion(&self, active: bool) -> bool {
        let Some(project) = self.audition_project() else {
            return false;
        };
        let Ok(mut held) = self.audition.version_blind.lock() else {
            return false;
        };
        if !active {
            *held = None;
            return true;
        }
        if held.is_some() {
            return true;
        }
        let Ok(storage) = StoragePaths::default_platform() else {
            return false;
        };
        let mut guard =
            kirin_measure::reference_gain::visual::BlindCaptureExclusion::for_current_project(
                &storage.plugin_data_dir(),
                &project,
            );
        if !guard.acquire() {
            return false;
        }
        *held = Some(guard);
        true
    }
    fn set_reference_capture(&self, active: bool) -> bool {
        let Some(project) = self.audition_project() else {
            return false;
        };
        let Ok(mut audition) = self.audition.admission.lock() else {
            return false;
        };
        let Ok(mut capture) = self.audition.capture.lock() else {
            return false;
        };
        if !active {
            if let Some(mut held) = capture.take() {
                held.release(audition.as_mut());
            }
            return true;
        }
        if capture.is_some() {
            return true;
        }
        if self.audition.kind.load(Ordering::Acquire) == AUDITION_LOCAL_BLIND {
            return false;
        }
        let Ok(storage) = StoragePaths::default_platform() else {
            return false;
        };
        let mut held = kirin_measure::reference_gain::visual::CaptureAdmission::for_current_project(
            &storage.plugin_data_dir(),
            &project,
        );
        if !held
            .try_acquire_shared(&self.audition.reference_owner)
            .unwrap_or(false)
        {
            return false;
        }
        *capture = Some(held);
        true
    }
}

/// # Safety
/// Null or a live engine pointer; non-RT control thread only.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_reference_capture_active(
    handle: *mut KirinHyphaEngine,
    active: bool,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (*handle).set_reference_capture(active) }
    }))
    .unwrap_or(false)
}

/// # Safety
/// Null or live engine pointer, non-RT only.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_version_blind_capture_exclusion(
    handle: *mut KirinHyphaEngine,
    active: bool,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (*handle).set_version_blind_capture_exclusion(active) }
    }))
    .unwrap_or(false)
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
        let engine = KirinHyphaEngine::new(
            48_000,
            kirin_measure::channel_layout::ChannelLayout::stereo(),
        );
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        let mut identity = engine.identity.lock().unwrap();
        identity.project_hash = project.to_string();
        identity.instance_id = Uuid::new_v4().to_string();
        drop(identity);
        engine
    }

    #[test]
    fn local_blind_is_post_only_and_returns_a_stable_epoch() {
        let _serial = ADMISSION_TEST.lock().unwrap();
        let pre = KirinHyphaEngine::new(
            48_000,
            kirin_measure::channel_layout::ChannelLayout::stereo(),
        );
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
    fn capture_keeps_pre_delta_and_shares_two_slots_with_reference() {
        let _serial = ADMISSION_TEST.lock().unwrap();
        let project = format!("ffi-capture-{}", Uuid::new_v4());
        let a = post_engine(&project);
        let b = post_engine(&project);
        let c = post_engine(&project);
        *a.delta_result.lock().unwrap() = DeltaResult {
            lufs: Some(2.0),
            mode: DeltaMode::Active,
            ..Default::default()
        };
        assert!(a.set_reference_capture(true));
        assert!(b.set_reference_capture(true));
        assert!(!a.audition.is_active());
        assert!(!a.audition.blocks_record());
        assert_eq!(a.delta_result.lock().unwrap().lufs, Some(2.0));
        assert!(!c.set_reference_capture(true));
        assert_eq!(c.begin_local_blind(), None);
        assert!(!c.set_version_blind_capture_exclusion(true));
        assert!(a.set_reference_audition_active(true));
        assert!(a.set_reference_capture(false));
        assert!(!c.set_reference_capture(true)); // B output retains the transferred slot.
        assert!(a.set_reference_capture(true));
        assert!(a.set_reference_audition_active(false));
        assert!(!c.set_reference_capture(true)); // Return A does not release Capture's slot.
        assert!(a.set_reference_capture(false));
        assert!(b.set_reference_capture(false));
        assert!(c.set_version_blind_capture_exclusion(true));
        assert!(!a.set_reference_capture(true));
        assert!(c.set_version_blind_capture_exclusion(false));
        assert!(a.set_reference_capture(true));
        assert!(a.set_reference_capture(false));
        assert!(!unsafe { kirin_hypha_set_reference_capture_active(std::ptr::null_mut(), true) });
    }
    #[test]
    fn engine_owner_survives_shutdown_until_all_async_jobs_retire() {
        use super::reference_analysis::*;
        let _serial = ADMISSION_TEST.lock().unwrap();
        let project = format!("ffi-owner-{}", Uuid::new_v4());
        let a = post_engine(&project);
        let b = post_engine(&project);
        let c = post_engine(&project);
        unsafe {
            let owner = kirin_hypha_reference_analysis_owner(&a);
            let same = kirin_hypha_reference_analysis_owner(&a);
            assert!(kirin_reference_analysis_same(owner, same));
            let live = kirin_reference_analysis_acquire(owner);
            let revisit = kirin_reference_analysis_acquire(owner);
            assert!(!live.is_null() && !revisit.is_null());
            assert!(a.set_reference_capture(true));
            assert!(a.set_reference_audition_active(true));
            assert!(b.set_reference_capture(true));
            assert!(!c.set_reference_capture(true));
            assert!(a.set_reference_audition_active(false));
            assert!(a.set_reference_capture(false));
            kirin_reference_analysis_owner_drop(same);
            kirin_reference_analysis_owner_drop(owner);
            drop(a);
            assert!(!c.set_reference_capture(true));
            kirin_reference_analysis_grant_drop(live);
            assert!(!c.set_reference_capture(true));
            kirin_reference_analysis_grant_drop(revisit);
            assert!(c.set_reference_capture(true));
            assert!(b.set_reference_capture(false) && c.set_reference_capture(false));
            assert!(kirin_hypha_reference_analysis_owner(std::ptr::null()).is_null());
            assert!(kirin_reference_analysis_acquire(std::ptr::null()).is_null());
        }
    }
    #[test]
    fn null_local_blind_ffi_fails_closed() {
        let mut epoch = 0;
        assert!(!unsafe { kirin_hypha_begin_local_blind(std::ptr::null_mut(), &mut epoch) });
        assert!(!unsafe { kirin_hypha_end_local_blind(std::ptr::null_mut(), 1) });
    }
}
