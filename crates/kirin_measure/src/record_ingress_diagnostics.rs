//! Read-only control/test snapshots; never evidence for lane retirement or readiness.
use super::*;

impl RecordIngress {
    /// Non-RT only. Atomics can span a transition; the control mutex is never waited on.
    #[doc(hidden)]
    pub fn diagnostic_snapshot(&self) -> serde_json::Value {
        let control = self.control.try_lock().ok();
        serde_json::json!({
            "snapshot_is_atomic": false,
            "armed_generation": self.armed_generation.load(Ordering::Acquire),
            "audio_generation": self.audio_generation.load(Ordering::Acquire),
            "drained_generation": self.drained_generation.load(Ordering::Acquire),
            "abandoned_generation": self.abandoned_generation.load(Ordering::Acquire),
            "restart_pending_generation": self.restart_pending_generation.load(Ordering::Acquire),
            "capture_origin_generation": self.capture_origin_generation.load(Ordering::Acquire),
            "capture_origin_frame": self.capture_origin_frame.load(Ordering::Acquire),
            "producer_installed": self.producer_installed.load(Ordering::Acquire),
            "measure_attached": self.measure_attached.load(Ordering::Acquire),
            "accepting_audio": self.accepting_audio.load(Ordering::Acquire),
            "audio_pushes_in_flight": self.audio_pushes_in_flight.load(Ordering::Acquire),
            "control_available": control.is_some(),
            "allocated": control.as_ref().map(|state| state.allocated),
            "spool_present": control.as_ref().map(|state| state.spool.is_some()),
            "retired_spools": control.as_ref().map(|state| state.retired_spools.len()),
        })
    }
}
