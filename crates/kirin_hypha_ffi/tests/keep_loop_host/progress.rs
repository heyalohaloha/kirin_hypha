//! Completed measurement progress, independent of nonblocking read availability.
use std::time::Duration;

#[derive(Default, Debug)]
pub(super) struct MeterProgress {
    pub endpoint: Option<i64>,
    observed_frames: u64,
}

impl MeterProgress {
    pub fn observe(&mut self, endpoint: Option<i64>, frames: Option<u64>) -> bool {
        if let Some(endpoint) = endpoint {
            self.endpoint = Some(endpoint);
        }
        let advanced = frames.is_some_and(|frames| frames > self.observed_frames);
        if advanced {
            self.observed_frames = frames.unwrap();
        }
        advanced
    }
}

/// Wait for both meters; the readers and monotonic clock can also model a delayed Measure Thread.
pub(super) fn wait_for_measurement(
    watchdog: Duration,
    mut read: impl FnMut() -> [(Option<i64>, Option<u64>); 2],
    behind: impl Fn(&MeterProgress) -> bool,
    mut elapsed: impl FnMut() -> Duration,
    mut hold: impl FnMut(),
) -> Result<(), [MeterProgress; 2]> {
    let mut progress = [MeterProgress::default(), MeterProgress::default()];
    let mut last_advance = [elapsed(); 2];
    loop {
        let readings = read();
        let now = elapsed();
        for ((meter, last), (endpoint, frames)) in
            progress.iter_mut().zip(&mut last_advance).zip(readings)
        {
            if meter.observe(endpoint, frames) {
                *last = now;
            }
        }
        if !progress.iter().any(&behind) {
            return Ok(());
        }
        // Only cumulative completed frames reset a meter's inactivity watchdog. Busy reads,
        // timeline changes and the other meter's progress cannot conceal a stopped meter.
        if progress
            .iter()
            .zip(last_advance)
            .any(|(meter, last)| behind(meter) && now.saturating_sub(last) >= watchdog)
        {
            return Err(progress);
        }
        hold();
    }
}

#[cfg(test)]
mod wait_tests {
    use super::*;
    use std::cell::Cell;

    const WATCHDOG: Duration = Duration::from_secs(600);
    const HOLD: Duration = Duration::from_millis(100);

    // Each injected meter publishes cumulative completed frames only when its work finishes.
    fn delayed_measure(now: Duration, finish: Option<Duration>) -> (Option<i64>, Option<u64>) {
        let frames = u64::from(finish.is_some_and(|finish| now >= finish)) * 48_000;
        (Some(frames as i64), Some(frames))
    }

    #[test]
    fn delayed_measure_catches_up_after_more_than_a_hundred_holds() {
        let now = Cell::new(Duration::ZERO);
        let result = wait_for_measurement(
            WATCHDOG,
            || {
                [120, 180]
                    .map(|seconds| delayed_measure(now.get(), Some(Duration::from_secs(seconds))))
            },
            |meter| meter.endpoint.unwrap_or(0) < 48_000,
            || now.get(),
            || now.set(now.get() + HOLD),
        );
        assert!(
            result.is_ok(),
            "delayed measurement must catch up: {result:?}"
        );
        assert_eq!(now.get(), Duration::from_secs(180));
    }

    #[test]
    fn stopped_measure_fails_only_at_the_full_watchdog() {
        let now = Cell::new(Duration::ZERO);
        let result = wait_for_measurement(
            WATCHDOG,
            || {
                [Some(Duration::from_secs(180)), None]
                    .map(|finish| delayed_measure(now.get(), finish))
            },
            |meter| meter.endpoint.unwrap_or(0) < 48_000,
            || now.get(),
            || now.set(now.get() + HOLD),
        );
        let stopped = result.expect_err("a stopped meter must fail the watchdog");
        assert_eq!(now.get(), WATCHDOG);
        assert_eq!(stopped[0].observed_frames, 48_000);
        assert_eq!(stopped[1].observed_frames, 0);
    }

    #[test]
    fn progressing_measure_may_take_longer_than_the_watchdog_in_total() {
        let now = Cell::new(Duration::ZERO);
        let result = wait_for_measurement(
            WATCHDOG,
            || {
                let frames = now.get().as_secs() / 300 * 16_000;
                [(Some(frames as i64), Some(frames)); 2]
            },
            |meter| meter.endpoint.unwrap_or(0) < 48_000,
            || now.get(),
            || now.set(now.get() + HOLD),
        );
        assert!(
            result.is_ok(),
            "completed frames keep progressing: {result:?}"
        );
        assert_eq!(now.get(), Duration::from_secs(900));
    }

    #[test]
    fn busy_reads_and_timeline_changes_do_not_restart_the_watchdog() {
        let now = Cell::new(Duration::ZERO);
        let result = wait_for_measurement(
            WATCHDOG,
            || {
                let reading = if now.get().as_secs().is_multiple_of(2) {
                    (None, None)
                } else {
                    (Some(now.get().as_secs() as i64), Some(0))
                };
                [reading; 2]
            },
            |meter| meter.endpoint.unwrap_or(0) < 48_000,
            || now.get(),
            || now.set(now.get() + HOLD),
        );
        assert!(result.is_err());
        assert_eq!(now.get(), WATCHDOG);
    }

    #[test]
    fn completed_measure_wins_after_a_late_scheduler_wake() {
        let now = Cell::new(Duration::ZERO);
        let result = wait_for_measurement(
            WATCHDOG,
            || [delayed_measure(now.get(), Some(WATCHDOG)); 2],
            |meter| meter.endpoint.unwrap_or(0) < 48_000,
            || now.get(),
            || now.set(now.get() + WATCHDOG * 2),
        );
        assert!(result.is_ok());
        assert_eq!(now.get(), WATCHDOG * 2);
    }
}

#[test]
fn busy_reads_keep_the_last_valid_endpoint_without_progress() {
    let mut progress = MeterProgress::default();
    assert!(progress.observe(Some(48_000), Some(48_000)));
    for _ in 0..200 {
        assert!(!progress.observe(None, None));
        assert_eq!(progress.endpoint, Some(48_000));
        assert!(!progress.observe(Some(48_000), Some(48_000)));
    }
}

#[test]
fn only_completed_frames_advance_even_when_the_timeline_wraps() {
    let mut progress = MeterProgress::default();
    assert!(progress.observe(Some(96_000), Some(96_000)));
    assert!(!progress.observe(Some(0), Some(96_000)));
    assert!(progress.observe(Some(4_800), Some(100_800)));
    assert!(!progress.observe(Some(4_800), Some(96_000)));
    assert!(!progress.observe(Some(4_800), Some(100_800)));
    assert!(progress.observe(None, Some(105_600)));
    assert_eq!(progress.endpoint, Some(4_800));
}
