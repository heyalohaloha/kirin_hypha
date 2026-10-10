//! Cached LEVEL aperture acquisition: missing facts and transient contention are distinct.
use super::*;

// LEVEL uses the same cached, lifecycle-qualified point as TIME. Legacy poll_delta remains
// unchanged; file arrival time alone never qualifies the product's four current LEVEL deltas.
#[derive(Debug)]
pub(crate) struct LevelBusy;
type LevelValues = ([Option<f64>; 6], Option<f64>);

/// Err means no coherent new frame (contention or an authority edge), not missing facts.
pub(crate) fn level_values(
    engine: &KirinHyphaEngine,
    snapshot: &MeterSessionSnapshot,
) -> Result<Option<LevelValues>, LevelBusy> {
    let Some(exchange) = engine.meter_delta_history.as_ref() else {
        return Ok(None);
    };
    let before = authority(engine).ok_or(LevelBusy)?;
    if before.signal != KIRIN_SIGNAL_STATE_ACTIVE {
        return Err(LevelBusy); // A stop raced an active acquisition; retry the local projection.
    }
    let Some(view) = exchange
        .time_comparison(
            MeterHistoryResolution::Hz10,
            snapshot.observed_frames,
            snapshot.observed_frames,
            0,
        )
        .ok_or(LevelBusy)?
        .ok()
        .flatten()
    else {
        return Ok(None);
    };
    let span = view.post_span;
    if span.epoch != snapshot.measurement_epoch
        || span.generation != snapshot.generation
        || span.token != before.span_token
        || !comparison_matches(&view, &before, span)
        || view.reason != TimeComparisonReason::Active
    {
        return Ok(None);
    }
    let (local_span, local) = {
        let session = engine
            .meter_session
            .as_ref()
            .ok_or(LevelBusy)?
            .try_lock()
            .map_err(|_| LevelBusy)?;
        if !session.time_is_active() {
            return Ok(None);
        }
        (session.time_source_span(), session.time_raw_tail(1).pop())
    };
    let after = authority(engine).ok_or(LevelBusy)?;
    if after != before {
        return Err(LevelBusy);
    }
    let Some(point) = view.point.as_ref() else {
        return Ok(None);
    };
    if point.wire.observed > snapshot.observed_frames {
        return Ok(None);
    }
    Ok(values_for_current(
        &view,
        local_span,
        local.as_ref(),
        Instant::now(),
    ))
}

pub(super) fn values_for_current(
    view: &TimeComparisonView,
    span: TimeSourceSpan,
    local: Option<&TimeRawPoint>,
    now: Instant,
) -> Option<LevelValues> {
    let point = view.point.as_ref()?;
    if !current::same_live_axis(view, span, local) || point.comparison_remaining(now).is_zero() {
        return None;
    }
    let latest = local?;
    Some((
        std::array::from_fn(|i| latest.wire.values[i].and(point.wire.values[i])),
        latest.wire.crest.and(point.wire.crest),
    ))
}
