use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
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
    selection_intent: AtomicBool,
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct ExactPairBindingSnapshot {
    pub generation: u64,
    pub project_hash: String,
    pub pre_instance_id: String,
}

impl PairBinding {
    pub(crate) fn new() -> Self {
        Self {
            transition: Mutex::new(()),
            selection_intent: AtomicBool::new(false),
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

    /// Return one coherent GUI/status view of selection intent and exact PRE identity.
    pub(crate) fn status_snapshot(&self) -> (bool, Option<String>) {
        let _transition = self
            .transition
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let pre_instance_id = self
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .as_ref()
            .map(|pre| pre.instance_id.clone());
        (
            self.selection_intent.load(Ordering::Acquire),
            pre_instance_id,
        )
    }

    /// Return the selected PRE locator and its pair generation under the same transition lock.
    /// Human names and host-specific context are deliberately absent from this authority.
    pub(crate) fn exact_snapshot(&self) -> Option<ExactPairBindingSnapshot> {
        let _transition = self
            .transition
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let binding = self
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let pre = binding.as_ref()?;
        let project_hash = pre.project_dir.file_name()?.to_str()?;
        if !kirin_measure::is_path_safe_component(project_hash)
            || !kirin_measure::is_path_safe_component(&pre.instance_id)
        {
            return None;
        }
        Some(ExactPairBindingSnapshot {
            generation: self.generation(),
            project_hash: project_hash.to_string(),
            pre_instance_id: pre.instance_id.clone(),
        })
    }

    /// True when an explicit name update would change intent or detach an exact binding.
    pub(crate) fn name_change_required(&self, name: &str) -> bool {
        let _transition = self
            .transition
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let desired = self
            .desired_name
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if desired.as_str() != name {
            return true;
        }
        if !name.is_empty() {
            return false;
        }
        self.selection_intent.load(Ordering::Acquire)
            || self
                .latched_pre
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner())
                .is_some()
    }

    pub(crate) fn matches_exact(&self, name: &str, selected: &LatchedPre) -> bool {
        let _transition = self
            .transition
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let desired = self
            .desired_name
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let latched = self
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        self.selection_intent.load(Ordering::Acquire)
            && desired.as_str() == name
            && latched.as_ref().is_some_and(|current| {
                current.instance_id == selected.instance_id && current.pre_json == selected.pre_json
            })
    }

    /// IO self-check が採った古い判定で、rename/re-Keep 後の binding を消さないための
    /// generation付きcompare-and-release。name と generation を transition lock 内で再照合し、
    /// 一致した世代だけを人名selector・recording・latchごと解放する。利用者が選択を解除するまで
    /// selection intent は保持し、名前なし exact pair も Waiting として区別する。
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
        if desired.is_empty() && !self.selection_intent.load(Ordering::Acquire) {
            self.selection_intent
                .store(!name.is_empty(), Ordering::Release);
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
        if previous_name == name
            && (!name.is_empty()
                || (!self.selection_intent.load(Ordering::Acquire)
                    && self
                        .latched_pre
                        .lock()
                        .unwrap_or_else(|poisoned| poisoned.into_inner())
                        .is_none()))
        {
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
        self.selection_intent
            .store(!desired.is_empty(), Ordering::Release);
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
        let exact_unchanged = self
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .as_ref()
            .is_some_and(|current| {
                current.instance_id == selected.instance_id && current.pre_json == selected.pre_json
            });
        if desired.as_str() == name
            && exact_unchanged
            && self.selection_intent.load(Ordering::Acquire)
        {
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
        self.selection_intent.store(true, Ordering::Release);
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
                readiness: kirin_measure::LatchedPreReadiness::Confirmed,
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
#[path = "pair_binding_tests.rs"]
mod tests;
