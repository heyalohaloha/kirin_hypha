use crate::MeasureResult;

/// GUI-only maxima for one transport playback pass.
///
/// Record/TRACE/plugin_data never consume this state. Keeping the algorithm in
/// the measurement core lets the egui and JUCE shells render the same values
/// without duplicating the accumulation rules in C++.
#[derive(Debug, Clone, Default)]
pub struct WatchMaxTracker {
    max: MeasureResult,
    current_pass_id: Option<u64>,
    last_sequence: u64,
    previous_playing: bool,
    previous_recording: bool,
    pending_after_sequence: Option<u64>,
    blocked_record_pass_id: Option<u64>,
}

impl WatchMaxTracker {
    pub fn update(
        &mut self,
        raw: &MeasureResult,
        playing: bool,
        playback_pass_id: u64,
        recording: bool,
    ) -> MeasureResult {
        let playback_edge = playing && !self.previous_playing;
        let record_exit = self.previous_recording && !recording;
        self.previous_playing = playing;
        self.previous_recording = recording;

        if recording || record_exit {
            self.clear_values();
            if playback_pass_id != 0 {
                self.blocked_record_pass_id = Some(playback_pass_id);
            }
            return self.max.clone();
        }
        if !playing || playback_pass_id == 0 {
            return self.max.clone();
        }
        if self.blocked_record_pass_id == Some(playback_pass_id) {
            return self.max.clone();
        }
        self.blocked_record_pass_id = None;

        if self.current_pass_id != Some(playback_pass_id) {
            self.clear_values();
            self.current_pass_id = Some(playback_pass_id);
        } else if playback_edge {
            self.clear_values();
            self.current_pass_id = Some(playback_pass_id);
            self.pending_after_sequence = Some(raw.measure_sequence);
        }
        if raw.measure_sequence == 0 || raw.playback_pass_id != playback_pass_id {
            return self.max.clone();
        }
        if let Some(blocked_until) = self.pending_after_sequence {
            if raw.measure_sequence <= blocked_until {
                return self.max.clone();
            }
            self.pending_after_sequence = None;
        }
        if raw.measure_sequence <= self.last_sequence {
            self.last_sequence = 0;
        }
        if raw.measure_sequence == self.last_sequence {
            return self.max.clone();
        }
        self.last_sequence = raw.measure_sequence;
        self.max.lufs_m = max_option(self.max.lufs_m, raw.lufs_m);
        self.max.lufs_s = max_option(self.max.lufs_s, raw.lufs_s);
        // Accumulate the recent 400 ms peak here. Using the engine-lifetime tp_session_max would
        // immediately resurrect a pre-RESET peak and make the explicit Watch RESET ineffective.
        self.max.true_peak = max_option(self.max.true_peak, raw.true_peak);
        self.max.crest = max_option(self.max.crest, raw.crest);
        self.max.clone()
    }

    pub fn reset(&mut self) {
        *self = Self::default();
    }

    fn clear_values(&mut self) {
        self.max = MeasureResult::default();
        self.current_pass_id = None;
        self.last_sequence = 0;
        self.pending_after_sequence = None;
    }
}

fn max_option(current: Option<f64>, candidate: Option<f64>) -> Option<f64> {
    match (current, candidate.filter(|value| value.is_finite())) {
        (Some(current), Some(candidate)) => Some(current.max(candidate)),
        (None, Some(candidate)) => Some(candidate),
        (current, None) => current,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn measure(sequence: u64, pass: u64, lufs: f64, tp: f64, crest: f64) -> MeasureResult {
        MeasureResult {
            measure_sequence: sequence,
            playback_pass_id: pass,
            lufs_m: Some(lufs),
            lufs_s: Some(lufs - 1.0),
            true_peak: Some(tp),
            crest: Some(crest),
            ..MeasureResult::default()
        }
    }

    #[test]
    fn continuous_pass_accumulates_absolute_maxima() {
        let mut tracker = WatchMaxTracker::default();
        tracker.update(&measure(1, 1, -18.0, -2.0, 8.0), true, 1, false);
        let max = tracker.update(&measure(2, 1, -12.0, -6.0, 5.0), true, 1, false);
        assert_eq!(max.lufs_m, Some(-12.0));
        assert_eq!(max.lufs_s, Some(-13.0));
        assert_eq!(max.true_peak, Some(-2.0));
        assert_eq!(max.crest, Some(8.0));
    }

    #[test]
    fn new_pass_resets_all_values() {
        let mut tracker = WatchMaxTracker::default();
        tracker.update(&measure(1, 1, -10.0, -1.0, 12.0), true, 1, false);
        tracker.update(&MeasureResult::default(), false, 1, false);
        let max = tracker.update(&measure(2, 2, -24.0, -9.0, 3.0), true, 2, false);
        assert_eq!(max.lufs_m, Some(-24.0));
        assert_eq!(max.lufs_s, Some(-25.0));
        assert_eq!(max.true_peak, Some(-9.0));
        assert_eq!(max.crest, Some(3.0));
    }

    #[test]
    fn record_values_never_enter_watch_max() {
        let mut tracker = WatchMaxTracker::default();
        tracker.update(&measure(1, 1, -8.0, 0.0, 20.0), true, 1, true);
        let max = tracker.update(&measure(2, 1, -8.0, 0.0, 20.0), true, 1, false);
        assert_eq!(max.lufs_m, None);
        assert_eq!(max.lufs_s, None);
        assert_eq!(max.true_peak, None);
        assert_eq!(max.crest, None);
    }

    #[test]
    fn true_peak_reset_boundary_does_not_import_engine_lifetime_peak() {
        let mut raw = measure(1, 1, -14.0, -12.0, 7.0);
        raw.tp_session_max = Some(-0.8);
        let mut tracker = WatchMaxTracker::default();
        let max = tracker.update(&raw, true, 1, false);
        assert_eq!(max.true_peak, Some(-12.0));
    }

    #[test]
    fn stopped_measurements_never_start_a_maximum_pass() {
        let mut tracker = WatchMaxTracker::default();
        let max = tracker.update(&measure(1, 1, -8.0, -0.2, 12.0), false, 1, false);
        assert_eq!(max.lufs_m, None);
        assert_eq!(max.lufs_s, None);
        assert_eq!(max.true_peak, None);
        assert_eq!(max.crest, None);
    }

    #[test]
    fn missing_fields_do_not_erase_existing_maxima() {
        let mut tracker = WatchMaxTracker::default();
        tracker.update(&measure(1, 1, -15.0, -2.0, 7.0), true, 1, false);
        let max = tracker.update(
            &MeasureResult {
                measure_sequence: 2,
                playback_pass_id: 1,
                ..MeasureResult::default()
            },
            true,
            1,
            false,
        );
        assert_eq!(max.lufs_m, Some(-15.0));
        assert_eq!(max.lufs_s, Some(-16.0));
        assert_eq!(max.true_peak, Some(-2.0));
        assert_eq!(max.crest, Some(7.0));
    }

    #[test]
    fn explicit_reset_starts_a_fresh_maximum_inside_the_same_pass() {
        let mut tracker = WatchMaxTracker::default();
        tracker.update(&measure(1, 1, -8.0, 0.8, 12.0), true, 1, false);
        tracker.reset();
        let max = tracker.update(&measure(2, 1, -20.0, -7.0, 4.0), true, 1, false);
        assert_eq!(max.lufs_m, Some(-20.0));
        assert_eq!(max.true_peak, Some(-7.0));
        assert_eq!(max.crest, Some(4.0));
    }

    #[test]
    fn playback_edge_rejects_a_stale_measurement_from_the_previous_pass() {
        let mut tracker = WatchMaxTracker::default();
        tracker.update(&measure(10, 1, -8.0, -0.2, 12.0), true, 1, false);
        tracker.update(&MeasureResult::default(), false, 1, false);
        let stale = tracker.update(&measure(10, 1, -8.0, -0.2, 12.0), true, 1, false);
        assert_eq!(stale.lufs_m, None);
        assert_eq!(stale.true_peak, None);
        assert_eq!(stale.crest, None);
    }

    #[test]
    fn new_pass_waits_until_the_measure_thread_reports_that_pass() {
        let mut tracker = WatchMaxTracker::default();
        tracker.update(&measure(10, 1, -8.0, -0.2, 12.0), true, 1, false);
        let stale = tracker.update(&measure(10, 1, -8.0, -0.2, 12.0), true, 2, false);
        assert_eq!(stale.lufs_m, None);

        let fresh = tracker.update(&measure(11, 2, -24.0, -8.0, 3.0), true, 2, false);
        assert_eq!(fresh.lufs_m, Some(-24.0));
        assert_eq!(fresh.true_peak, Some(-8.0));
        assert_eq!(fresh.crest, Some(3.0));
    }

    #[test]
    fn measure_thread_sequence_restart_does_not_freeze_maxima() {
        let mut tracker = WatchMaxTracker::default();
        tracker.update(&measure(10_000, 1, -8.0, -2.0, 8.0), true, 1, false);
        let after_restart = tracker.update(&measure(1, 1, -6.0, -1.0, 9.0), true, 1, false);
        assert_eq!(after_restart.lufs_m, Some(-6.0));
        assert_eq!(after_restart.true_peak, Some(-1.0));
        assert_eq!(after_restart.crest, Some(9.0));
    }
}
