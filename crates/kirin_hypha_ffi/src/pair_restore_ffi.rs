//! Exact saved-pair restore that carries the human label as metadata, never as lookup authority.

use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::{current_host_process_id, sanitize_name, PlatformPaths};

use super::{epoch_secs_now, read_c_str, restored_pair_latch, KirinHyphaEngine};

impl KirinHyphaEngine {
    pub(crate) fn restore_pair_candidate_v2(
        &self,
        pre_project_hash: &str,
        instance_id: &str,
        display_name: &str,
    ) -> bool {
        let display_name = sanitize_name(display_name);
        let daw_session_id = self
            .identity
            .lock()
            .map(|identity| identity.daw_session_uuid.clone())
            .unwrap_or_default();
        let Some(latch) = restored_pair_latch(
            &PlatformPaths::current_kirin_tmp_root(),
            pre_project_hash,
            &daw_session_id,
            &display_name,
            instance_id,
            current_host_process_id(),
        ) else {
            return false;
        };
        if self.pair_binding.matches_exact(&display_name, &latch) {
            return true;
        }
        let (project_hash, post_iid) = self.begin_pair_reselection();
        let transition = self.pair_binding.replace_exact(display_name, latch);
        self.finish_pair_reselection(transition, &project_hash, &post_iid, epoch_secs_now());
        true
    }
}

/// Restore a saved exact PRE and its optional label without running name discovery.
///
/// # Safety
/// A non-null `handle` must be live. Other pointers may be null or readable C strings.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_restore_pair_candidate_v2(
    handle: *mut KirinHyphaEngine,
    pre_project_hash: *const c_char,
    instance_id: *const c_char,
    display_name: *const c_char,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        let project = unsafe { read_c_str(pre_project_hash) };
        let instance = unsafe { read_c_str(instance_id) };
        let name = unsafe { read_c_str(display_name) };
        unsafe { (*handle).restore_pair_candidate_v2(&project, &instance, &name) }
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_restore_keeps_optional_name_as_metadata() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        engine.set_identity(
            "post-a".into(),
            "project-a".into(),
            "session-a".into(),
            String::new(),
        );
        assert!(engine.restore_pair_candidate_v2("project-hash", "pre-a", "Same Name"));
        let exact = engine.pair_binding.exact_snapshot().unwrap();
        assert_eq!(exact.project_hash, "project-hash");
        assert_eq!(exact.pre_instance_id, "pre-a");
        assert_eq!(
            engine.pair_binding.desired_name().read().unwrap().as_str(),
            "Same Name"
        );
    }

    #[test]
    fn exact_restore_rejects_unsafe_locator_without_name_fallback() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        assert!(!engine.restore_pair_candidate_v2("../other", "pre-a", "Same Name"));
        assert!(engine.pair_binding.exact_snapshot().is_none());
        assert_eq!(engine.pair_binding.status_snapshot(), (false, None));
    }
}
