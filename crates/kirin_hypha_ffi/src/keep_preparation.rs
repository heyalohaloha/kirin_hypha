//! Completion feedback for the Keep preparation worker, shared by Keep and All Keep.
use super::*;
use kirin_measure::CaptureGenerationError;

#[path = "keep_notice.rs"]
mod notice;
pub(super) use notice::KeepFailure;

#[derive(Clone)]
struct KeepPreparationFeedback {
    control: Arc<Mutex<()>>,
    notice: Arc<RwLock<Option<String>>>,
    phase: Arc<AtomicU8>,
    phase_generation: Arc<AtomicI64>,
    generation_started_at_ms: i64,
    all: bool,
    #[cfg(test)]
    after_generation_check: Option<Arc<dyn Fn() + Send + Sync>>,
}

/// Lock only on control/worker threads. Audio-thread atomics retain their existing contract.
pub(super) fn with_current_generation(
    control: &Mutex<()>,
    phase_generation: &AtomicI64,
    generation_started_at_ms: i64,
    apply: impl FnOnce(),
) {
    let _control = control.lock().unwrap_or_else(|error| error.into_inner());
    if phase_generation.load(Ordering::Acquire) == generation_started_at_ms {
        apply();
    }
}

impl KeepPreparationFeedback {
    fn while_current(&self, apply: impl FnOnce()) {
        with_current_generation(
            &self.control,
            &self.phase_generation,
            self.generation_started_at_ms,
            || {
                #[cfg(test)]
                if let Some(after_check) = &self.after_generation_check {
                    after_check();
                }
                apply();
            },
        );
    }

    fn complete(&self, result: Result<(), CaptureGenerationError>) {
        self.while_current(|| match result {
            Ok(()) => self.phase.store(KIRIN_KEEP_PHASE_ARMED, Ordering::Release),
            Err(error) => self.fail_current(KeepFailure::from_generation(&error), &error),
        });
    }

    fn fail_current(&self, failure: KeepFailure, error: &CaptureGenerationError) {
        self.phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
        failure.report(
            &self.notice,
            if self.all { "All Keep" } else { "Keep" },
            error,
        );
    }

    fn dispatch_failed(
        &self,
        error: &CaptureGenerationError,
        _control: &std::sync::MutexGuard<'_, ()>,
    ) {
        // Dispatch runs inside the Keep entry's control section; never acquire it recursively.
        if self.phase_generation.load(Ordering::Acquire) == self.generation_started_at_ms {
            self.fail_current(KeepFailure::WorkerUnavailable, error);
        }
    }
}

#[allow(clippy::too_many_arguments)]
pub(super) fn start_keep_preparation(
    transaction: CaptureGenerationTransaction,
    held_control: &std::sync::MutexGuard<'_, ()>,
    control: &Arc<Mutex<()>>,
    notice: &Arc<RwLock<Option<String>>>,
    phase: &Arc<AtomicU8>,
    phase_generation: &Arc<AtomicI64>,
    generation_started_at_ms: i64,
    all: bool,
) -> bool {
    let feedback = KeepPreparationFeedback {
        control: Arc::clone(control),
        notice: Arc::clone(notice),
        phase: Arc::clone(phase),
        phase_generation: Arc::clone(phase_generation),
        generation_started_at_ms,
        all,
        #[cfg(test)]
        after_generation_check: None,
    };
    let completion = feedback.clone();
    match transaction.commit_when_ready_async(CAPTURE_PRODUCER_READY_TIMEOUT, move |result| {
        completion.complete(result);
    }) {
        Ok(()) => true,
        Err(error) => {
            feedback.dispatch_failed(&error, held_control);
            false
        }
    }
}

#[cfg(test)]
#[path = "keep_preparation_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "keep_preparation_race_tests.rs"]
mod race_tests;
