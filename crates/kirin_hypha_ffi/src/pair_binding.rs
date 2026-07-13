use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, RwLock};

use kirin_measure::LatchedPre;

/// The single in-process owner of a POST's pair selection.
///
/// The visible name remains the user's selector. Once discovery resolves that
/// name, `latched_pre` and `recording_pre` carry the exact PRE instance. A
/// target-name transition clears both instance-bound values in the same
/// critical section so a deleted or renamed PRE cannot survive as a hidden
/// binding.
pub(crate) struct PairBinding {
    transition: Mutex<()>,
    desired_name: Arc<RwLock<String>>,
    recording_pre: Arc<Mutex<Option<String>>>,
    latched_pre: Arc<Mutex<Option<LatchedPre>>>,
    generation: AtomicU64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct PairTargetTransition {
    pub changed: bool,
    pub previous_name: String,
    pub previous_pre_instance_id: Option<String>,
    pub generation: u64,
}

impl PairBinding {
    pub(crate) fn new() -> Self {
        Self {
            transition: Mutex::new(()),
            desired_name: Arc::new(RwLock::new(String::new())),
            recording_pre: Arc::new(Mutex::new(None)),
            latched_pre: Arc::new(Mutex::new(None)),
            generation: AtomicU64::new(1),
        }
    }

    pub(crate) fn desired_name(&self) -> Arc<RwLock<String>> {
        Arc::clone(&self.desired_name)
    }

    pub(crate) fn recording_pre(&self) -> Arc<Mutex<Option<String>>> {
        Arc::clone(&self.recording_pre)
    }

    pub(crate) fn latched_pre(&self) -> Arc<Mutex<Option<LatchedPre>>> {
        Arc::clone(&self.latched_pre)
    }

    pub(crate) fn generation(&self) -> u64 {
        self.generation.load(Ordering::Acquire)
    }

    /// IO self-check が採った古い判定で、rename/re-Keep 後の binding を消さないための
    /// generation付きcompare-and-release。name と generation を transition lock 内で再照合し、
    /// 一致した世代だけを人名selector・recording・latchごと解放する。
    pub(crate) fn release_if_current(&self, expected_name: &str, expected_generation: u64) -> bool {
        let _transition = self
            .transition
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if self.generation() != expected_generation {
            return false;
        }
        let mut desired = self
            .desired_name
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if desired.as_str() != expected_name {
            return false;
        }
        desired.clear();
        self.recording_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .take();
        self.latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .take();
        next_generation(&self.generation);
        true
    }

    /// Seed a restored/default name without disturbing a user selection that
    /// was already applied before the IO runtime was enabled.
    pub(crate) fn seed_name_if_empty(&self, name: String) {
        let _transition = self
            .transition
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let mut desired = self
            .desired_name
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if desired.is_empty() {
            *desired = name;
        }
    }

    /// Replace the human selector and detach every exact-instance state tied
    /// to the previous selector. External Record/reservation cleanup is
    /// performed by the caller using `previous_pre_instance_id`.
    pub(crate) fn replace_name(&self, name: String) -> PairTargetTransition {
        let _transition = self
            .transition
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let mut desired = self
            .desired_name
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let previous_name = desired.clone();
        if previous_name == name {
            return PairTargetTransition {
                changed: false,
                previous_name,
                previous_pre_instance_id: None,
                generation: self.generation(),
            };
        }

        let previous_recording = self
            .recording_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .take();
        let previous_latched = self
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .take();
        let previous_pre_instance_id =
            previous_recording.or_else(|| previous_latched.map(|latched| latched.instance_id));
        *desired = name;
        let generation = next_generation(&self.generation);

        PairTargetTransition {
            changed: true,
            previous_name,
            previous_pre_instance_id,
            generation,
        }
    }

    /// Replace the selector with one exact PRE chosen by the user.
    ///
    /// Name and instance latch become visible under the same transition lock. Selecting a second
    /// PRE with the same human name is therefore a real transition instead of the old name-only
    /// no-op.
    pub(crate) fn replace_exact(&self, name: String, selected: LatchedPre) -> PairTargetTransition {
        let _transition = self
            .transition
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let mut desired = self
            .desired_name
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let current_exact = self
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .as_ref()
            .map(|pre| pre.instance_id.clone());
        if desired.as_str() == name && current_exact.as_deref() == Some(&selected.instance_id) {
            return PairTargetTransition {
                changed: false,
                previous_name: desired.clone(),
                previous_pre_instance_id: None,
                generation: self.generation(),
            };
        }

        let previous_name = desired.clone();
        let previous_recording = self
            .recording_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .take();
        let previous_latched = self
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .replace(selected);
        let previous_pre_instance_id =
            previous_recording.or_else(|| previous_latched.map(|latched| latched.instance_id));
        *desired = name;
        let generation = next_generation(&self.generation);
        PairTargetTransition {
            changed: true,
            previous_name,
            previous_pre_instance_id,
            generation,
        }
    }

    #[cfg(test)]
    fn set_exact_binding_for_test(&self, instance_id: &str) {
        if let Ok(mut recording) = self.recording_pre.lock() {
            *recording = Some(instance_id.to_string());
        }
        if let Ok(mut latched) = self.latched_pre.lock() {
            *latched = Some(LatchedPre {
                name: "mix".to_string(),
                instance_id: instance_id.to_string(),
                project_dir: Default::default(),
                pre_json: Default::default(),
                daw_session_id: Some(String::new()),
                host_process_id: None,
            });
        }
    }
}

fn next_generation(counter: &AtomicU64) -> u64 {
    let mut current = counter.load(Ordering::Acquire);
    loop {
        let next = if current == u64::MAX { 1 } else { current + 1 };
        match counter.compare_exchange_weak(current, next, Ordering::AcqRel, Ordering::Acquire) {
            Ok(_) => return next,
            Err(observed) => current = observed,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_name_preserves_exact_binding_and_generation() {
        let binding = PairBinding::new();
        binding.replace_name("mix".to_string());
        binding.set_exact_binding_for_test("pre-old");
        let before = binding.generation();

        let transition = binding.replace_name("mix".to_string());

        assert!(!transition.changed);
        assert_eq!(binding.generation(), before);
        assert_eq!(
            binding.recording_pre.lock().unwrap().as_deref(),
            Some("pre-old")
        );
        assert_eq!(
            binding
                .latched_pre
                .lock()
                .unwrap()
                .as_ref()
                .map(|p| p.instance_id.as_str()),
            Some("pre-old")
        );
    }

    #[test]
    fn renamed_target_detaches_every_old_instance_state_atomically() {
        let binding = PairBinding::new();
        binding.replace_name("mix".to_string());
        binding.set_exact_binding_for_test("pre-old");

        let transition = binding.replace_name("master".to_string());

        assert!(transition.changed);
        assert_eq!(transition.previous_name, "mix");
        assert_eq!(
            transition.previous_pre_instance_id.as_deref(),
            Some("pre-old")
        );
        assert!(binding.recording_pre.lock().unwrap().is_none());
        assert!(binding.latched_pre.lock().unwrap().is_none());
        assert_eq!(binding.desired_name.read().unwrap().as_str(), "master");
    }

    #[test]
    fn clearing_name_is_a_real_unpair() {
        let binding = PairBinding::new();
        binding.replace_name("mix".to_string());
        binding.set_exact_binding_for_test("pre-old");

        let transition = binding.replace_name(String::new());

        assert!(transition.changed);
        assert!(binding.desired_name.read().unwrap().is_empty());
        assert!(binding.recording_pre.lock().unwrap().is_none());
        assert!(binding.latched_pre.lock().unwrap().is_none());
    }

    #[test]
    fn stale_self_check_generation_cannot_release_new_binding() {
        let binding = PairBinding::new();
        binding.replace_name("mix".to_string());
        let stale_generation = binding.generation();
        binding.replace_name("master".to_string());
        binding.replace_name("mix".to_string());
        binding.set_exact_binding_for_test("pre-new");

        assert!(!binding.release_if_current("mix", stale_generation));
        assert_eq!(binding.desired_name.read().unwrap().as_str(), "mix");
        assert_eq!(
            binding.recording_pre.lock().unwrap().as_deref(),
            Some("pre-new")
        );
    }

    #[test]
    fn current_self_check_generation_releases_whole_binding() {
        let binding = PairBinding::new();
        binding.replace_name("mix".to_string());
        binding.set_exact_binding_for_test("pre-current");
        let generation = binding.generation();

        assert!(binding.release_if_current("mix", generation));
        assert!(binding.desired_name.read().unwrap().is_empty());
        assert!(binding.recording_pre.lock().unwrap().is_none());
        assert!(binding.latched_pre.lock().unwrap().is_none());
        assert_ne!(binding.generation(), generation);
    }

    #[test]
    fn same_name_can_move_to_an_explicit_second_instance() {
        let binding = PairBinding::new();
        binding.replace_name("mix".to_string());
        binding.set_exact_binding_for_test("pre-old");
        let selected = LatchedPre {
            name: "mix".to_string(),
            instance_id: "pre-new".to_string(),
            project_dir: Default::default(),
            pre_json: Default::default(),
            daw_session_id: None,
            host_process_id: None,
        };

        let transition = binding.replace_exact("mix".to_string(), selected);

        assert!(transition.changed);
        assert_eq!(
            transition.previous_pre_instance_id.as_deref(),
            Some("pre-old")
        );
        assert_eq!(
            binding
                .latched_pre
                .lock()
                .unwrap()
                .as_ref()
                .map(|pre| pre.instance_id.as_str()),
            Some("pre-new")
        );
    }
}
