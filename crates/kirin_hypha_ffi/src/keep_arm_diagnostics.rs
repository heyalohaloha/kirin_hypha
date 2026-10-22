//! Read-only acceptance diagnostics; never called from an Audio callback.
use super::*;

impl KirinHyphaEngine {
    /// Failure-only fixture diagnostics. Values may straddle a transition and never control it.
    #[doc(hidden)]
    pub fn __keep_arm_diagnostics_for_test(&self) -> serde_json::Value {
        serde_json::json!({
            "recording": self.record_sm.is_recording(),
            "record_generation": self.record_sm.generation(),
            "measure_ready_generation": self.record_sm.measure_ready_generation(),
            "lane_ready_for_record_generation": self.record_ingress.generation_ready(self.record_sm.generation()),
            "record_session": self.record_sm.record_session_id(),
            "closed_session": self.record_sm.last_closed_session_id(),
            "measure_progress": self.record_sm.measure_progress(),
            "measure_preparation_samples": self.record_sm.measure_preparation_samples(self.record_sm.generation()),
            "measure_wake": self.record_sm.measure_wake_diagnostics(),
            "lane": self.record_ingress.diagnostic_snapshot(),
            "worker_alive": self.measure_alive(),
            "worker_generation": self.__measure_worker_generation_for_test(),
            "heartbeat": self.heartbeat.load(Ordering::Relaxed),
            "overflow_samples": self.overflow_count(),
            "keep_phase": self.keep_phase.load(Ordering::Acquire),
            "keep_record_generation": self.keep_record_generation.load(Ordering::Acquire),
            "keep_started_at_ms": self.keep_phase_generation_started_at_ms.load(Ordering::Acquire),
            "record_error": self.record_error_message(),
            "meter_active_frames": self.poll_meter_session().map(|meter| meter.active_frames),
        })
    }

    /// Durable Measure worker generation used by restart acceptance tests and diagnostics.
    #[doc(hidden)]
    pub fn __measure_worker_generation_for_test(&self) -> u64 {
        self.measure_worker_generation.load(Ordering::Acquire)
    }

    /// Test-only: end the Measure worker so its watchdog can exercise the restart path.
    #[doc(hidden)]
    pub fn __force_measure_restart_for_test(&self) {
        self.shutdown.store(true, Ordering::Relaxed);
    }
}

#[cfg(test)]
#[path = "keep_arm_diagnostics_tests.rs"]
mod tests;
