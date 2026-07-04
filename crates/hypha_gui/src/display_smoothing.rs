use kirin_measure::{DeltaResult, MeasureResult};

/// GUI-only numeric stabilizer.
///
/// Record/TRACE writers keep raw 10 fps measurements. This filter is only for the tiny plugin
/// display so short drum transients do not make the visible numbers unreadable.
#[derive(Debug, Clone)]
pub struct DisplaySmoother {
    measure: MeasureResult,
    delta: DeltaResult,
    measure_last_secs: Option<f64>,
    delta_last_secs: Option<f64>,
    tau_secs: f64,
}

impl Default for DisplaySmoother {
    fn default() -> Self {
        Self {
            measure: MeasureResult::default(),
            delta: DeltaResult::default(),
            measure_last_secs: None,
            delta_last_secs: None,
            tau_secs: 1.5,
        }
    }
}

impl DisplaySmoother {
    pub const HOLD_SECS: f64 = 3.0;

    pub fn update_measure(&mut self, raw: &MeasureResult, now_secs: f64) -> MeasureResult {
        if is_expired(self.measure_last_secs, now_secs) {
            self.measure = MeasureResult::default();
            self.measure_last_secs = None;
        }
        let dt = dt_since(self.measure_last_secs, now_secs);
        self.measure_last_secs = Some(now_secs);

        let mut out = raw.clone();
        out.lufs_m = smooth_option(self.measure.lufs_m, raw.lufs_m, dt, self.tau_secs, false);
        out.true_peak = smooth_option(
            self.measure.true_peak,
            raw.true_peak,
            dt,
            self.tau_secs,
            true,
        );
        out.tp_session_max = smooth_option(
            self.measure.tp_session_max,
            raw.tp_session_max,
            dt,
            self.tau_secs,
            true,
        );
        out.crest = smooth_option(self.measure.crest, raw.crest, dt, self.tau_secs, false);
        out.psr = smooth_option(self.measure.psr, raw.psr, dt, self.tau_secs, false);
        out.n_prime_total = smooth_option(
            self.measure.n_prime_total,
            raw.n_prime_total,
            dt,
            self.tau_secs,
            false,
        );
        out.sharpness = smooth_option(
            self.measure.sharpness,
            raw.sharpness,
            dt,
            self.tau_secs,
            false,
        );
        self.measure = out.clone();
        out
    }

    pub fn update_delta(&mut self, raw: &DeltaResult, now_secs: f64) -> DeltaResult {
        if is_expired(self.delta_last_secs, now_secs) {
            self.delta = DeltaResult::default();
            self.delta_last_secs = None;
        }
        let dt = dt_since(self.delta_last_secs, now_secs);
        self.delta_last_secs = Some(now_secs);

        let mut out = raw.clone();
        out.lufs = smooth_option(self.delta.lufs, raw.lufs, dt, self.tau_secs, false);
        out.psr = smooth_option(self.delta.psr, raw.psr, dt, self.tau_secs, false);
        out.tp = smooth_option(self.delta.tp, raw.tp, dt, self.tau_secs, false);
        out.n_prime_total = smooth_option(
            self.delta.n_prime_total,
            raw.n_prime_total,
            dt,
            self.tau_secs,
            false,
        );
        out.crest = smooth_option(self.delta.crest, raw.crest, dt, self.tau_secs, false);
        out.sharpness = smooth_option(
            self.delta.sharpness,
            raw.sharpness,
            dt,
            self.tau_secs,
            false,
        );
        self.delta = out.clone();
        out
    }

    pub fn reset(&mut self) {
        self.measure = MeasureResult::default();
        self.delta = DeltaResult::default();
        self.measure_last_secs = None;
        self.delta_last_secs = None;
    }

    pub fn held_measure(&self, now_secs: f64) -> Option<MeasureResult> {
        if is_held(self.measure_last_secs, now_secs) && has_measure_core(&self.measure) {
            Some(self.measure.clone())
        } else {
            None
        }
    }

    pub fn held_delta(&self, now_secs: f64) -> Option<DeltaResult> {
        if is_held(self.delta_last_secs, now_secs) && has_delta_core(&self.delta) {
            Some(self.delta.clone())
        } else {
            None
        }
    }
}

fn dt_since(last: Option<f64>, now_secs: f64) -> Option<f64> {
    last.map(|prev| (now_secs - prev).clamp(0.0, 1.0))
}

fn smooth_option(
    previous: Option<f64>,
    raw: Option<f64>,
    dt_secs: Option<f64>,
    tau_secs: f64,
    peak_hold: bool,
) -> Option<f64> {
    let Some(raw) = raw else {
        return previous;
    };
    let Some(previous) = previous else {
        return Some(raw);
    };
    let Some(dt_secs) = dt_secs else {
        return Some(raw);
    };
    if dt_secs <= f64::EPSILON {
        return Some(previous);
    }
    if peak_hold && raw > previous {
        return Some(raw);
    }
    let alpha = 1.0 - (-dt_secs / tau_secs).exp();
    Some(previous + (raw - previous) * alpha)
}

fn is_held(last: Option<f64>, now_secs: f64) -> bool {
    last.is_some_and(|last| now_secs >= last && now_secs - last <= DisplaySmoother::HOLD_SECS)
}

fn is_expired(last: Option<f64>, now_secs: f64) -> bool {
    last.is_some_and(|last| now_secs >= last && now_secs - last > DisplaySmoother::HOLD_SECS)
}

fn has_measure_core(m: &MeasureResult) -> bool {
    m.lufs_m.is_some() || m.true_peak.is_some() || m.crest.is_some()
}

fn has_delta_core(d: &DeltaResult) -> bool {
    d.lufs.is_some() || d.tp.is_some() || d.crest.is_some()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn display_smoothing_moves_toward_lufs_without_jumping() {
        let mut s = DisplaySmoother::default();
        let mut m = MeasureResult {
            lufs_m: Some(-30.0),
            ..MeasureResult::default()
        };
        assert_eq!(s.update_measure(&m, 0.0).lufs_m, Some(-30.0));
        m.lufs_m = Some(-10.0);
        let next = s.update_measure(&m, 0.1).lufs_m.unwrap();
        assert!(next > -30.0);
        assert!(next < -10.0);
    }

    #[test]
    fn display_smoothing_holds_true_peak_on_release() {
        let mut s = DisplaySmoother::default();
        let mut m = MeasureResult {
            true_peak: Some(-1.0),
            ..MeasureResult::default()
        };
        assert_eq!(s.update_measure(&m, 0.0).true_peak, Some(-1.0));
        m.true_peak = Some(-12.0);
        let next = s.update_measure(&m, 0.1).true_peak.unwrap();
        assert!(next < -1.0);
        assert!(next > -12.0);
        m.true_peak = Some(-0.5);
        assert_eq!(s.update_measure(&m, 0.2).true_peak, Some(-0.5));
    }

    #[test]
    fn held_measure_expires_after_hold_window() {
        let mut s = DisplaySmoother::default();
        let m = MeasureResult {
            lufs_m: Some(-18.0),
            ..MeasureResult::default()
        };
        s.update_measure(&m, 10.0);
        assert!(s.held_measure(12.9).is_some());
        assert!(s.held_measure(13.1).is_none());
    }

    #[test]
    fn active_update_after_hold_expiry_restarts_from_raw() {
        let mut s = DisplaySmoother::default();
        let mut m = MeasureResult {
            lufs_m: Some(-30.0),
            ..MeasureResult::default()
        };
        assert_eq!(s.update_measure(&m, 0.0).lufs_m, Some(-30.0));
        assert!(s.held_measure(3.1).is_none());

        m.lufs_m = Some(-10.0);
        assert_eq!(s.update_measure(&m, 3.2).lufs_m, Some(-10.0));
    }

    #[test]
    fn active_missing_value_keeps_recent_display_value() {
        let mut s = DisplaySmoother::default();
        let mut m = MeasureResult {
            crest: Some(12.0),
            ..MeasureResult::default()
        };
        assert_eq!(s.update_measure(&m, 0.0).crest, Some(12.0));
        m.crest = None;
        assert_eq!(s.update_measure(&m, 0.5).crest, Some(12.0));
    }
}
