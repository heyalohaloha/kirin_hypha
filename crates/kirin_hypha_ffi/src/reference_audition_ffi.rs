use super::*;

impl KirinHyphaEngine {
    /// Couple explicit Reference selection to the shared comparison admission owner.
    pub fn set_reference_audition_active(&self, active: bool) -> bool {
        if active {
            self.begin_audition(
                super::audition_admission_ffi::AUDITION_REFERENCE,
                "Reference",
            )
            .is_some()
        } else {
            self.end_audition(super::audition_admission_ffi::AUDITION_REFERENCE, None)
        }
    }
}

/// Couple explicit Reference A/B selection to the PRE-comparison suspension gate.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_reference_audition_active(
    handle: *mut KirinHyphaEngine,
    active: bool,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (*handle).set_reference_audition_active(active) }
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reference_and_local_blind_share_one_owner() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        assert!(!engine.set_reference_audition_active(true));
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        let mut identity = engine.identity.lock().unwrap();
        identity.project_hash = format!("ffi-reference-{}", Uuid::new_v4());
        identity.instance_id = Uuid::new_v4().to_string();
        drop(identity);
        *engine.delta_result.lock().unwrap() = DeltaResult {
            lufs: Some(2.0),
            mode: DeltaMode::Active,
            ..Default::default()
        };

        assert!(engine.set_reference_audition_active(true));
        assert!(engine.audition.is_active());
        assert_eq!(engine.delta_result.lock().unwrap().mode, DeltaMode::NoPre);
        assert_eq!(engine.begin_local_blind(), None);
        assert!(!engine.end_local_blind(1));
        assert!(engine.set_reference_audition_active(false));
        assert!(!engine.audition.is_active());
    }

    #[test]
    fn null_ffi_handle_fails_closed() {
        assert!(!unsafe { kirin_hypha_set_reference_audition_active(std::ptr::null_mut(), true) });
    }
}
