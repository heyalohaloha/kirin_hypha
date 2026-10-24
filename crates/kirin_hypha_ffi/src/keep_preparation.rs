//! Completion feedback for the Keep preparation worker, shared by Keep and All Keep.
use super::*;
use kirin_measure::CaptureGenerationError;

#[path = "keep_notice.rs"]
mod notice;
pub(super) use notice::KeepFailure;

#[derive(Clone)]
struct KeepPreparationFeedback {
    notice: Arc<RwLock<Option<String>>>,
    phase: Arc<AtomicU8>,
    phase_generation: Arc<AtomicI64>,
    generation_started_at_ms: i64,
    all: bool,
}

impl KeepPreparationFeedback {
    fn complete(&self, result: Result<(), CaptureGenerationError>) {
        if self.phase_generation.load(Ordering::Acquire) != self.generation_started_at_ms {
            return;
        }
        match result {
            Ok(()) => self.phase.store(KIRIN_KEEP_PHASE_ARMED, Ordering::Release),
            Err(error) => self.fail(KeepFailure::from_generation(&error), &error),
        }
    }

    fn fail(&self, failure: KeepFailure, error: &CaptureGenerationError) {
        if self.phase_generation.load(Ordering::Acquire) != self.generation_started_at_ms {
            return;
        }
        self.phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
        failure.report(
            &self.notice,
            if self.all { "All Keep" } else { "Keep" },
            error,
        );
    }

    fn dispatch_failed(&self, error: &CaptureGenerationError) {
        self.fail(KeepFailure::WorkerUnavailable, error);
    }
}

pub(super) fn start_keep_preparation(
    transaction: CaptureGenerationTransaction,
    notice: &Arc<RwLock<Option<String>>>,
    phase: &Arc<AtomicU8>,
    phase_generation: &Arc<AtomicI64>,
    generation_started_at_ms: i64,
    all: bool,
) -> bool {
    let feedback = KeepPreparationFeedback {
        notice: Arc::clone(notice),
        phase: Arc::clone(phase),
        phase_generation: Arc::clone(phase_generation),
        generation_started_at_ms,
        all,
    };
    let completion = feedback.clone();
    match transaction.commit_when_ready_async(CAPTURE_PRODUCER_READY_TIMEOUT, move |result| {
        completion.complete(result);
    }) {
        Ok(()) => true,
        Err(error) => {
            feedback.dispatch_failed(&error);
            false
        }
    }
}

#[cfg(test)]
#[path = "keep_preparation_tests.rs"]
mod tests;
