use super::*;

impl KirinHyphaEngine {
    /// Control-thread boundary coupled to explicit Reference A/B selection.
    ///
    /// The normal POST engine keeps measuring canonical A. Only PRE-derived comparisons are
    /// suspended, and the exact pair latch remains owned by the POST for immediate A return.
    pub fn set_reference_audition_active(&self, active: bool) -> bool {
        let is_post =
            self.write_role.lock().ok().and_then(|role| *role) == Some(PluginDataRole::Post);
        if !is_post {
            return false;
        }
        self.reference_audition_active
            .store(active, Ordering::Release);
        if active {
            *self
                .delta_result
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner()) = DeltaResult::default();
            if let Some(history) = self.meter_delta_history.as_ref() {
                history.reset();
            }
        }
        true
    }
}

/// Couple explicit Reference A/B selection to the PRE-comparison suspension gate.
/// Returns false for null, PRE, or not-yet-enabled engines.
///
/// # Safety
/// `handle` must be null or a live pointer returned by [`kirin_hypha_create`].
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_reference_audition_active(
    handle: *mut KirinHyphaEngine,
    active: bool,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).set_reference_audition_active(active) }
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn post_only_gate_clears_held_delta_and_is_reversible() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        assert!(!engine.set_reference_audition_active(true));
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        *engine.delta_result.lock().unwrap() = DeltaResult {
            lufs: Some(2.0),
            mode: DeltaMode::Active,
            ..Default::default()
        };

        assert!(engine.set_reference_audition_active(true));
        assert!(engine.reference_audition_active.load(Ordering::Acquire));
        assert_eq!(engine.delta_result.lock().unwrap().mode, DeltaMode::NoPre);
        assert!(engine.set_reference_audition_active(false));
        assert!(!engine.reference_audition_active.load(Ordering::Acquire));
    }

    #[test]
    fn null_ffi_handle_fails_closed() {
        assert!(!unsafe { kirin_hypha_set_reference_audition_active(std::ptr::null_mut(), true) });
    }
}
