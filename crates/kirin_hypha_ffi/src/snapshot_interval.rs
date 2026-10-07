//! Interval subtraction and order-statistic median. No midpoint substitutes a bound.

use crate::snapshot_types::*;

pub fn subtract_intervals(
    post: KirinSnapshotInterval,
    pre: KirinSnapshotInterval,
) -> Option<KirinSnapshotInterval> {
    if !post.is_valid() || !pre.is_valid() || post.unit != pre.unit {
        return None;
    }
    fn subtract(
        a: KirinSnapshotEndpoint,
        b: KirinSnapshotEndpoint,
        lower: bool,
    ) -> Option<KirinSnapshotEndpoint> {
        if a.kind != KIRIN_ENDPOINT_FINITE || b.kind != KIRIN_ENDPOINT_FINITE {
            return Some(if lower {
                KirinSnapshotEndpoint::negative_infinity()
            } else {
                KirinSnapshotEndpoint::positive_infinity()
            });
        }
        let value = a.value - b.value;
        value
            .is_finite()
            .then(|| KirinSnapshotEndpoint::finite(value, a.closed == 1 && b.closed == 1))
    }
    let interval = KirinSnapshotInterval {
        lower: subtract(post.lower, pre.upper, true)?,
        upper: subtract(post.upper, pre.lower, false)?,
        unit: post.unit,
        reserved: [0; 7],
    };
    interval.is_valid().then_some(interval)
}

pub fn median_interval(intervals: &[KirinSnapshotInterval]) -> Option<KirinSnapshotInterval> {
    let unit = intervals.first()?.unit;
    if intervals.iter().any(|i| !i.is_valid() || i.unit != unit) {
        return None;
    }
    fn median(
        mut endpoints: Vec<KirinSnapshotEndpoint>,
        lower: bool,
    ) -> Option<KirinSnapshotEndpoint> {
        endpoints.sort_by(|a, b| {
            a.extended_value()
                .total_cmp(&b.extended_value())
                .then_with(|| {
                    if lower {
                        b.closed.cmp(&a.closed)
                    } else {
                        a.closed.cmp(&b.closed)
                    }
                })
        });
        let high = endpoints.len() / 2;
        if endpoints.len() % 2 == 1 {
            return Some(endpoints[high]);
        }
        let a = endpoints[high - 1];
        let b = endpoints[high];
        if a.kind != KIRIN_ENDPOINT_FINITE || b.kind != KIRIN_ENDPOINT_FINITE {
            return Some(if lower {
                KirinSnapshotEndpoint::negative_infinity()
            } else {
                KirinSnapshotEndpoint::positive_infinity()
            });
        }
        // Standard midpoint avoids overflow and preserves equal subnormal endpoints.
        let value = a.value.midpoint(b.value);
        value
            .is_finite()
            .then(|| KirinSnapshotEndpoint::finite(value, a.closed == 1 && b.closed == 1))
    }
    let interval = KirinSnapshotInterval {
        lower: median(intervals.iter().map(|i| i.lower).collect(), true)?,
        upper: median(intervals.iter().map(|i| i.upper).collect(), false)?,
        unit,
        reserved: [0; 7],
    };
    interval.is_valid().then_some(interval)
}

#[cfg(test)]
#[path = "snapshot_interval_tests.rs"]
mod tests;
