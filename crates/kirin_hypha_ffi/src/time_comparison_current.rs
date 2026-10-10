//! Common LEVEL/TIME admission for a cached match; no poll-time renewal or new join.
use super::*;
use kirin_measure::meter_session::time_observation::TIME_COMPARISON_TTL;

pub(super) fn same_live_axis(
    view: &TimeComparisonView,
    span: TimeSourceSpan,
    local: Option<&TimeRawPoint>,
) -> bool {
    let (Some(point), Some(latest)) = (&view.point, local) else {
        return false;
    };
    let (Some((clock, endpoint)), Some((latest_clock, latest_endpoint))) =
        (point.wire.exact_key(), latest.wire.exact_key())
    else {
        return false;
    };
    let Some(lag) = latest.wire.observed.checked_sub(point.wire.observed) else {
        return false;
    };
    point.wire.span == span
        && latest.wire.span == span
        && point.wire.run == view.post_run
        && latest.wire.run == view.post_run
        && view.reason == TimeComparisonReason::Active
        && clock == latest_clock
        && latest_endpoint.checked_sub(endpoint) == i64::try_from(lag).ok()
        && u128::from(lag) * 1000 < u128::from(span.sample_rate) * TIME_COMPARISON_TTL.as_millis()
}
