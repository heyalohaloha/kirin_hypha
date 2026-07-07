use kirin_measure::MeasureResult;

/// GUI-only maximum values for the current playback pass.
///
/// This does not feed Record/TRACE/plugin_data. It only remembers the visible
/// absolute maxima shown next to Watch values.
#[derive(Debug, Clone, Default)]
pub struct PlaybackMaxTracker {
    max: MeasureResult,
    current_pass_id: Option<u64>,
    last_sequence: u64,
    prev_playing: bool,
    pending_after_sequence: Option<u64>,
}

impl PlaybackMaxTracker {
    pub fn update(&mut self, raw: &MeasureResult, playing: bool) -> MeasureResult {
        if playing && !self.prev_playing {
            self.max = MeasureResult::default();
            self.pending_after_sequence = Some(raw.measure_sequence);
        }
        self.prev_playing = playing;

        if !playing || raw.measure_sequence == 0 || raw.playback_pass_id == 0 {
            return self.max.clone();
        }

        if self.current_pass_id != Some(raw.playback_pass_id) {
            self.max = MeasureResult::default();
            self.current_pass_id = Some(raw.playback_pass_id);
            self.last_sequence = 0;
            self.pending_after_sequence = None;
        }

        if let Some(blocked_until) = self.pending_after_sequence {
            if raw.measure_sequence <= blocked_until {
                return self.max.clone();
            }
            self.pending_after_sequence = None;
        }

        if raw.measure_sequence <= self.last_sequence {
            return self.max.clone();
        }
        self.last_sequence = raw.measure_sequence;
        self.max.lufs_m = max_option(self.max.lufs_m, raw.lufs_m);
        self.max.true_peak = max_option(self.max.true_peak, raw.tp_session_max.or(raw.true_peak));
        self.max.crest = max_option(self.max.crest, raw.crest);

        self.max.clone()
    }

    pub fn reset(&mut self) {
        self.max = MeasureResult::default();
        self.current_pass_id = None;
        self.last_sequence = 0;
        self.prev_playing = false;
        self.pending_after_sequence = None;
    }
}

fn max_option(current: Option<f64>, candidate: Option<f64>) -> Option<f64> {
    match (current, candidate.filter(|v| v.is_finite())) {
        (Some(current), Some(candidate)) => Some(current.max(candidate)),
        (None, Some(candidate)) => Some(candidate),
        (current, None) => current,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn measure(
        sequence: u64,
        pass_id: u64,
        lufs_m: Option<f64>,
        true_peak: Option<f64>,
        crest: Option<f64>,
    ) -> MeasureResult {
        MeasureResult {
            measure_sequence: sequence,
            playback_pass_id: pass_id,
            lufs_m,
            true_peak,
            crest,
            ..MeasureResult::default()
        }
    }

    #[test]
    fn playback_start_resets_previous_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false);
        let first = tracker.update(&measure(1, 1, Some(-10.0), Some(-0.5), Some(11.0)), true);
        assert_eq!(first.lufs_m, Some(-10.0));
        assert_eq!(first.true_peak, Some(-0.5));
        assert_eq!(first.crest, Some(11.0));

        tracker.update(&MeasureResult::default(), false);
        let next = tracker.update(&measure(2, 2, Some(-24.0), Some(-8.0), Some(3.0)), true);
        assert_eq!(next.lufs_m, Some(-24.0));
        assert_eq!(next.true_peak, Some(-8.0));
        assert_eq!(next.crest, Some(3.0));
    }

    #[test]
    fn continuous_playback_keeps_absolute_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false);
        tracker.update(&measure(1, 1, Some(-18.0), Some(-6.0), Some(8.0)), true);
        let next = tracker.update(&measure(2, 1, Some(-12.0), Some(-9.0), Some(5.0)), true);

        assert_eq!(next.lufs_m, Some(-12.0));
        assert_eq!(next.true_peak, Some(-6.0));
        assert_eq!(next.crest, Some(8.0));
    }

    #[test]
    fn none_candidates_do_not_clear_existing_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false);
        tracker.update(&measure(1, 1, Some(-15.0), Some(-2.0), Some(7.0)), true);
        let next = tracker.update(
            &MeasureResult {
                measure_sequence: 2,
                playback_pass_id: 1,
                ..MeasureResult::default()
            },
            true,
        );

        assert_eq!(next.lufs_m, Some(-15.0));
        assert_eq!(next.true_peak, Some(-2.0));
        assert_eq!(next.crest, Some(7.0));
    }

    #[test]
    fn true_peak_max_uses_session_max_when_gui_repaint_misses_recent_peak() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false);
        let raw = MeasureResult {
            measure_sequence: 1,
            playback_pass_id: 1,
            true_peak: Some(-12.0),
            tp_session_max: Some(-0.8),
            ..MeasureResult::default()
        };

        assert_eq!(tracker.update(&raw, true).true_peak, Some(-0.8));
    }

    #[test]
    fn stopped_updates_do_not_create_new_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        let max = tracker.update(&measure(1, 1, Some(-11.0), Some(-1.0), Some(12.0)), false);

        assert_eq!(max.lufs_m, None);
        assert_eq!(max.true_peak, None);
        assert_eq!(max.crest, None);
    }

    #[test]
    fn playback_edge_does_not_reinsert_stale_raw_from_previous_pass() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false);
        tracker.update(&measure(10, 1, Some(-8.0), Some(-0.2), Some(12.0)), true);
        tracker.update(&MeasureResult::default(), false);

        let stale = tracker.update(&measure(10, 1, Some(-8.0), Some(-0.2), Some(12.0)), true);

        assert_eq!(stale.lufs_m, None);
        assert_eq!(stale.true_peak, None);
        assert_eq!(stale.crest, None);
    }

    #[test]
    fn pass_id_change_resets_even_when_playing_edge_was_missed() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false);
        tracker.update(&measure(10, 1, Some(-8.0), Some(-0.2), Some(12.0)), true);

        let next = tracker.update(&measure(11, 2, Some(-24.0), Some(-8.0), Some(3.0)), true);
        assert_eq!(next.lufs_m, Some(-24.0));
        assert_eq!(next.true_peak, Some(-8.0));
        assert_eq!(next.crest, Some(3.0));
    }

    #[test]
    fn repeated_gui_frames_do_not_reprocess_same_measurement() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false);
        let raw = measure(1, 1, Some(-12.0), Some(-3.0), Some(9.0));
        tracker.update(&raw, true);
        let repeated = tracker.update(&raw, true);

        assert_eq!(repeated.lufs_m, Some(-12.0));
        assert_eq!(repeated.true_peak, Some(-3.0));
        assert_eq!(repeated.crest, Some(9.0));
    }
}
