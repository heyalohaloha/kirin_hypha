use kirin_measure::MeasureResult;

/// GUI-only maximum values for the current playback pass.
///
/// This does not feed Record/TRACE/plugin_data. It only remembers the visible
/// absolute maxima shown next to Watch values.
#[derive(Debug, Clone, Default)]
pub struct PlaybackMaxTracker {
    max: MeasureResult,
    prev_playing: bool,
}

impl PlaybackMaxTracker {
    pub fn update(&mut self, raw: &MeasureResult, playing: bool) -> MeasureResult {
        if playing && !self.prev_playing {
            self.max = MeasureResult::default();
        }
        self.prev_playing = playing;

        if playing {
            self.max.lufs_m = max_option(self.max.lufs_m, raw.lufs_m);
            self.max.true_peak =
                max_option(self.max.true_peak, raw.tp_session_max.or(raw.true_peak));
            self.max.crest = max_option(self.max.crest, raw.crest);
        }

        self.max.clone()
    }

    pub fn reset(&mut self) {
        self.max = MeasureResult::default();
        self.prev_playing = false;
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

    fn measure(lufs_m: Option<f64>, true_peak: Option<f64>, crest: Option<f64>) -> MeasureResult {
        MeasureResult {
            lufs_m,
            true_peak,
            crest,
            ..MeasureResult::default()
        }
    }

    #[test]
    fn playback_start_resets_previous_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        let first = tracker.update(&measure(Some(-10.0), Some(-0.5), Some(11.0)), true);
        assert_eq!(first.lufs_m, Some(-10.0));
        assert_eq!(first.true_peak, Some(-0.5));
        assert_eq!(first.crest, Some(11.0));

        tracker.update(&MeasureResult::default(), false);
        let next = tracker.update(&measure(Some(-24.0), Some(-8.0), Some(3.0)), true);
        assert_eq!(next.lufs_m, Some(-24.0));
        assert_eq!(next.true_peak, Some(-8.0));
        assert_eq!(next.crest, Some(3.0));
    }

    #[test]
    fn continuous_playback_keeps_absolute_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&measure(Some(-18.0), Some(-6.0), Some(8.0)), true);
        let next = tracker.update(&measure(Some(-12.0), Some(-9.0), Some(5.0)), true);

        assert_eq!(next.lufs_m, Some(-12.0));
        assert_eq!(next.true_peak, Some(-6.0));
        assert_eq!(next.crest, Some(8.0));
    }

    #[test]
    fn none_candidates_do_not_clear_existing_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&measure(Some(-15.0), Some(-2.0), Some(7.0)), true);
        let next = tracker.update(&MeasureResult::default(), true);

        assert_eq!(next.lufs_m, Some(-15.0));
        assert_eq!(next.true_peak, Some(-2.0));
        assert_eq!(next.crest, Some(7.0));
    }

    #[test]
    fn session_true_peak_max_wins_over_recent_true_peak() {
        let mut tracker = PlaybackMaxTracker::default();
        let raw = MeasureResult {
            true_peak: Some(-12.0),
            tp_session_max: Some(-0.8),
            ..MeasureResult::default()
        };

        assert_eq!(tracker.update(&raw, true).true_peak, Some(-0.8));
    }

    #[test]
    fn stopped_updates_do_not_create_new_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        let max = tracker.update(&measure(Some(-11.0), Some(-1.0), Some(12.0)), false);

        assert_eq!(max.lufs_m, None);
        assert_eq!(max.true_peak, None);
        assert_eq!(max.crest, None);
    }
}
