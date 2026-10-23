//! Measurement worker idle-state predicates.
use super::*;

#[inline]
pub(super) fn idle_sleep_for_record_state(is_recording: bool) -> Duration {
    if is_recording {
        RECORD_LOOP_SLEEP
    } else {
        LOOP_SLEEP
    }
}

pub(super) fn should_process_phase_d(is_recording: bool) -> bool {
    is_recording
}

/// Queued PCM alone is not progress: its cursor, frame or generation proof may be pending.
pub(super) fn should_continue_measurement(processed_samples: usize, queued_samples: usize) -> bool {
    processed_samples > 0 && queued_samples > 0
}

/// SignalState controls Watch freshness and GUI visibility. Record admission is owned by the
/// Record state machine and capture clock; once Audio Thread has admitted a buffer, silence is
/// still factual audio and must pass through every Record engine. Treating Inactive as a Record
/// boundary resets Phase D and creates sparse 100 ms optional fields on quiet buses.
#[inline]
pub(super) fn should_suspend_measurement(state: SignalState, is_recording: bool) -> bool {
    !is_recording && state != SignalState::Active
}

impl RecordStateMachine {
    pub(crate) fn wait_for_measure_work(&self, is_recording: bool) {
        self.note_measure_idle_wait();
        if is_recording {
            thread::sleep(idle_sleep_for_record_state(true));
        } else {
            thread::park_timeout(idle_sleep_for_record_state(false));
        }
        self.note_measure_idle_return(is_recording);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn queued_but_unreadable_pcm_waits_instead_of_spinning() {
        assert!(!should_continue_measurement(0, 32));
        assert!(!should_continue_measurement(32, 0));
        assert!(!should_continue_measurement(0, 0));
        assert!(should_continue_measurement(32, 32));
    }
}
