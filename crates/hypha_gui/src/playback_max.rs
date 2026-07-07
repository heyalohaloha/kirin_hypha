use kirin_measure::MeasureResult;

const AUDIO_STALL_SECS: f64 = 0.25;

/// GUI-only maximum values for the current playback pass.
///
/// This does not feed Record/TRACE/plugin_data. It only remembers the visible
/// absolute maxima shown next to Watch values.
#[derive(Debug, Clone, Default)]
pub struct PlaybackMaxTracker {
    max: MeasureResult,
    prev_playing: bool,
    last_heartbeat: Option<u32>,
    last_heartbeat_change_secs: Option<f64>,
}

impl PlaybackMaxTracker {
    pub fn update(
        &mut self,
        raw: &MeasureResult,
        playing: bool,
        heartbeat: u32,
        now_secs: f64,
    ) -> MeasureResult {
        let heartbeat_recent = self.observe_heartbeat(heartbeat, now_secs);
        let active_playback = playing && heartbeat_recent;

        if active_playback && !self.prev_playing {
            self.max = MeasureResult::default();
        }
        self.prev_playing = active_playback;

        if active_playback {
            self.max.lufs_m = max_option(self.max.lufs_m, raw.lufs_m);
            self.max.true_peak = max_option(self.max.true_peak, raw.true_peak);
            self.max.crest = max_option(self.max.crest, raw.crest);
        }

        self.max.clone()
    }

    pub fn reset(&mut self) {
        self.max = MeasureResult::default();
        self.prev_playing = false;
        self.last_heartbeat = None;
        self.last_heartbeat_change_secs = None;
    }

    fn observe_heartbeat(&mut self, heartbeat: u32, now_secs: f64) -> bool {
        let Some(previous) = self.last_heartbeat.replace(heartbeat) else {
            return false;
        };
        if previous != heartbeat {
            self.last_heartbeat_change_secs = Some(now_secs);
            return true;
        }
        self.last_heartbeat_change_secs
            .is_some_and(|last| now_secs >= last && now_secs - last <= AUDIO_STALL_SECS)
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
        tracker.update(&MeasureResult::default(), false, 0, 0.0);
        let first = tracker.update(&measure(Some(-10.0), Some(-0.5), Some(11.0)), true, 1, 0.1);
        assert_eq!(first.lufs_m, Some(-10.0));
        assert_eq!(first.true_peak, Some(-0.5));
        assert_eq!(first.crest, Some(11.0));

        tracker.update(&MeasureResult::default(), false, 2, 0.2);
        let next = tracker.update(&measure(Some(-24.0), Some(-8.0), Some(3.0)), true, 3, 0.3);
        assert_eq!(next.lufs_m, Some(-24.0));
        assert_eq!(next.true_peak, Some(-8.0));
        assert_eq!(next.crest, Some(3.0));
    }

    #[test]
    fn continuous_playback_keeps_absolute_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false, 0, 0.0);
        tracker.update(&measure(Some(-18.0), Some(-6.0), Some(8.0)), true, 1, 0.1);
        let next = tracker.update(&measure(Some(-12.0), Some(-9.0), Some(5.0)), true, 2, 0.2);

        assert_eq!(next.lufs_m, Some(-12.0));
        assert_eq!(next.true_peak, Some(-6.0));
        assert_eq!(next.crest, Some(8.0));
    }

    #[test]
    fn none_candidates_do_not_clear_existing_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false, 0, 0.0);
        tracker.update(&measure(Some(-15.0), Some(-2.0), Some(7.0)), true, 1, 0.1);
        let next = tracker.update(&MeasureResult::default(), true, 2, 0.2);

        assert_eq!(next.lufs_m, Some(-15.0));
        assert_eq!(next.true_peak, Some(-2.0));
        assert_eq!(next.crest, Some(7.0));
    }

    #[test]
    fn true_peak_max_uses_visible_recent_value_not_session_max() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false, 0, 0.0);
        let raw = MeasureResult {
            true_peak: Some(-12.0),
            tp_session_max: Some(-0.8),
            ..MeasureResult::default()
        };

        assert_eq!(tracker.update(&raw, true, 1, 0.1).true_peak, Some(-12.0));
    }

    #[test]
    fn stopped_updates_do_not_create_new_maxima() {
        let mut tracker = PlaybackMaxTracker::default();
        let max = tracker.update(&measure(Some(-11.0), Some(-1.0), Some(12.0)), false, 1, 0.1);

        assert_eq!(max.lufs_m, None);
        assert_eq!(max.true_peak, None);
        assert_eq!(max.crest, None);
    }

    #[test]
    fn stale_true_playing_without_audio_heartbeat_does_not_update() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false, 10, 0.0);
        let max = tracker.update(&measure(Some(-11.0), Some(-1.0), Some(12.0)), true, 10, 0.3);

        assert_eq!(max.lufs_m, None);
        assert_eq!(max.true_peak, None);
        assert_eq!(max.crest, None);
    }

    #[test]
    fn heartbeat_stall_creates_next_playback_reset_even_if_playing_stays_true() {
        let mut tracker = PlaybackMaxTracker::default();
        tracker.update(&MeasureResult::default(), false, 0, 0.0);
        tracker.update(&measure(Some(-8.0), Some(-0.2), Some(12.0)), true, 1, 0.1);
        tracker.update(&measure(Some(-6.0), Some(0.1), Some(14.0)), true, 1, 0.5);

        let next = tracker.update(&measure(Some(-24.0), Some(-8.0), Some(3.0)), true, 2, 0.6);
        assert_eq!(next.lufs_m, Some(-24.0));
        assert_eq!(next.true_peak, Some(-8.0));
        assert_eq!(next.crest, Some(3.0));
    }
}
