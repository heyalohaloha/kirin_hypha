//! Include IO work in the 100 ms cycle; never add a second full wait or catch-up ticks.
use std::time::{Duration, Instant};

pub(super) fn remaining(started: Instant, now: Instant) -> Duration {
    Duration::from_millis(100).saturating_sub(now.saturating_duration_since(started))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cycle_budget_includes_processing_without_catch_up_or_higher_poll_rate() {
        let start = Instant::now();
        for (work, wait) in [(0, 100), (40, 60), (99, 1), (100, 0), (400, 0)] {
            assert_eq!(
                remaining(start, start + Duration::from_millis(work)),
                Duration::from_millis(wait)
            );
            assert!(work + wait >= 100);
        }
    }
}
