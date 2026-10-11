//! Include IO work in the 100 ms cycle; never add a second full wait or catch-up ticks.
use std::time::{Duration, Instant};

pub(super) fn remaining(started: Instant, now: Instant) -> Duration {
    Duration::from_millis(100)
        .saturating_sub(now.saturating_duration_since(started))
        .max(Duration::from_millis(10))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cycle_budget_includes_processing_without_catch_up_or_higher_poll_rate() {
        let start = Instant::now();
        for (work, wait) in [(0, 100), (40, 60), (99, 10), (100, 10), (400, 10)] {
            assert_eq!(
                remaining(start, start + Duration::from_millis(work)),
                Duration::from_millis(wait)
            );
            assert!(work + wait >= 100);
        }
    }
}
