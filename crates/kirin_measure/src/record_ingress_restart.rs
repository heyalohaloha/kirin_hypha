//! Measure worker replacement and generation reconciliation.
use super::*;

impl RecordIngress {
    /// Detach a terminated Measure reader without replacing the live Audio -> spool lane. A new
    /// Measure worker replays the same generation from sample zero, so a worker crash cannot erase
    /// already admitted Record audio.
    ///
    /// `observation` is an exact snapshot of the shared Record state. A generation is retired here
    /// only when it is already known to have entered (`record_generation == armed_generation`) and
    /// is now fully closed (`!recording && !has_record_session`). A not-yet-entered generation has
    /// `record_generation < armed_generation` and must remain eligible to enter after restart.
    pub(crate) fn replace_after_measure_restart(
        &self,
        observation: RecordIngressGenerationObservation,
    ) {
        let armed_generation = {
            let control = match self.control.lock() {
                Ok(control) => control,
                Err(poisoned) => poisoned.into_inner(),
            };
            if !control.allocated {
                return;
            }
            let armed_generation = self.armed_generation.load(Ordering::Acquire);
            self.measure_attached.store(false, Ordering::Release);
            self.restart_pending_generation
                .store(armed_generation, Ordering::Release);
            armed_generation
        };
        if observation.proves_closed(armed_generation) {
            let _ = self.retire_restart_generation(armed_generation);
        }
    }

    /// Resolve one Watchdog replacement from the newly started Measure worker.
    ///
    /// This is deliberately separate from [`Self::replace_after_measure_restart`]. If Stop races
    /// the Watchdog's first live observation, the replacement worker sees the later closed state
    /// and retires the exact old generation. Conversely, an Entering state retains a Record session
    /// while `recording == false`, so it remains pending instead of being misclassified as closed.
    pub(crate) fn reconcile_after_measure_restart(
        &self,
        observation: RecordIngressGenerationObservation,
    ) -> RecordIngressRestartOutcome {
        let pending = self.restart_pending_generation.load(Ordering::Acquire);
        if pending == 0 {
            return RecordIngressRestartOutcome::None;
        }
        if observation.proves_closed(pending) || observation.record_generation > pending {
            return if self.retire_restart_generation(pending) {
                RecordIngressRestartOutcome::Retired
            } else {
                RecordIngressRestartOutcome::None
            };
        }
        if observation.record_generation == pending && observation.recording {
            return if self
                .restart_pending_generation
                .compare_exchange(pending, 0, Ordering::AcqRel, Ordering::Acquire)
                .is_ok()
            {
                RecordIngressRestartOutcome::PreservedLive
            } else {
                RecordIngressRestartOutcome::None
            };
        }
        RecordIngressRestartOutcome::Pending
    }

    fn retire_restart_generation(&self, generation: u64) -> bool {
        if generation == 0 || self.restart_pending_generation.load(Ordering::Acquire) != generation
        {
            return false;
        }
        if !self.finish_capture_from_measure(generation, Duration::from_secs(1)) {
            // A close timeout is itself terminal for this TRACE. Release the failed generation so
            // it cannot retain disk or permanently poison later explicit Keeps.
            return self.mark_failed_from_measure(generation);
        }
        // No replacement Measure worker owns this closed generation, so retirement is a failed
        // TRACE outcome rather than a successful drain/seal. The same terminalization path also
        // removes the control seed immediately.
        self.mark_failed_from_measure(generation)
    }
}
