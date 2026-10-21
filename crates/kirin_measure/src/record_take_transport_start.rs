//! Capture-ring replacement invalidates the edge histories on the same Audio callback.
use super::*;

impl RecordTakeTracker {
    /// Start the capture clock for a replacement SPSC ring.
    ///
    /// The Measure Thread consuming the new ring starts its native frame cursor at zero. The
    /// Audio Thread must therefore reset this producer-side mapping in the same callback that
    /// installs the replacement Producer. Atomics only; no allocation, lock, logging, or I/O.
    pub fn reset_capture_clock(&self) {
        self.transport_start_journal.invalidate();
        self.active_edge_journal.invalidate();
        self.capture_frames_total.store(0, Ordering::Release);
        // Epoch IDs remain monotonic across SPSC replacement. A previous Keep may still be closing
        // on IO Thread and holds its selected epoch; reusing sequence 1 would silently redirect it
        // to the replacement ring. Current-frame lookups are isolated by the floor while direct
        // selected-epoch lookups can still finish the previous generation from immutable slots.
        self.capture_clock_floor_sequence.store(
            self.capture_span_sequence
                .load(Ordering::Acquire)
                .saturating_add(1),
            Ordering::Release,
        );
        self.capture_last_span_sequence.store(0, Ordering::Release);
        self.capture_last_generation.store(0, Ordering::Release);
        self.capture_last_position_valid
            .store(false, Ordering::Release);
        self.capture_last_position_end_samples
            .store(i64::MIN, Ordering::Release);
        self.capture_last_raw_host_position_valid
            .store(false, Ordering::Release);
        self.capture_last_raw_host_position_end_samples
            .store(i64::MIN, Ordering::Release);
        self.capture_last_auxiliary_valid
            .store(false, Ordering::Release);
        self.capture_last_auxiliary_end_samples
            .store(i64::MIN, Ordering::Release);
        self.capture_last_auxiliary_source
            .store(AuxiliaryClockSource::Unknown as u8, Ordering::Release);
        self.capture_last_source
            .store(CaptureClockSource::Unknown as u8, Ordering::Release);
        self.capture_last_presentation_source
            .store(PresentationLatencySource::Unknown as u8, Ordering::Release);
        self.capture_last_input_presentation_samples
            .store(u64::MAX, Ordering::Release);
        self.capture_last_output_presentation_samples
            .store(u64::MAX, Ordering::Release);
        self.capture_block_offline.store(false, Ordering::Release);
        self.watch_offline_active.store(false, Ordering::Release);
        self.watch_offline_force_pending
            .store(false, Ordering::Release);
        self.watch_offline_candidate_epoch
            .store(0, Ordering::Release);
    }
}
