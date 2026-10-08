//! Exact Meter Session and intermediate Record summaries; final Record sealing stays canonical.
use super::*;

impl MeasureEngine {
    pub(crate) fn enable_session_summary_cache(&mut self) -> Result<(), String> {
        self.ebu
            .enable_cached_summary_queries()
            .map_err(|error| format!("enable_cached_summary_queries: {error:?}"))
    }

    /// Per-channel sample frames actually submitted to EBU, excluding pending <10 ms input.
    pub(crate) fn session_processed_frames(&self) -> u64 {
        self.analysis_frames
    }

    /// Original gating energies and logarithmic queries. Record keeps its scalar finalization.
    pub(crate) fn cached_session_summary(&self) -> SessionSummary {
        SessionSummary {
            lufs_i: self
                .ebu
                .loudness_global_cached()
                .ok()
                .filter(|v| v.is_finite()),
            lra: self
                .ebu
                .loudness_range_cached()
                .ok()
                .filter(|v| v.is_finite()),
            max_true_peak: self.session_true_peak_dbtp(),
            layout: Some(self.layout),
        }
    }
}
