//! Cached LEVEL aperture acquisition: missing facts and transient contention are distinct.
use super::*;

// LEVEL uses the same cached, lifecycle-qualified point as TIME. Legacy poll_delta remains
// unchanged; file arrival time alone never qualifies the product's four current LEVEL deltas.
#[derive(Debug)]
pub(crate) struct LevelBusy;
pub(crate) type LevelValues = ([Option<f64>; 6], Option<f64>);

/// POST's absolute values and the POST − PRE difference of ONE confirmed 100 ms boundary.
#[derive(Clone, Copy)]
pub(crate) struct PairedBoundary {
    /// End of the comparison lifetime, counted from the boundary's original completion.
    pub(crate) until: Instant,
    pub(crate) own: LevelValues,
    pub(crate) difference: LevelValues,
}

/// Boundaries kept for the one-second comparison lifetime, plus publication jitter.
const PAIRED_BOUNDARY_TAIL: usize = 16;

/// Err means no coherent new frame (contention or an authority edge), not missing facts.
pub(crate) fn level_values(
    engine: &KirinHyphaEngine,
    snapshot: &MeterSessionSnapshot,
) -> Result<Option<LevelValues>, LevelBusy> {
    acquire_comparison(engine, snapshot, 1, false, |view, span, tail| {
        values_for_current(view, span, tail.last(), Instant::now())
    })
}

/// The newest boundary both sides published, within the comparison lifetime. PRE is reconstructed
/// from POST's own value at that same boundary; an older difference is never subtracted from a
/// newer absolute value. Waiting for the exact current boundary instead made PRE come and go with
/// ordinary publication lag.
pub(crate) fn paired_boundary_values(
    engine: &KirinHyphaEngine,
    snapshot: &MeterSessionSnapshot,
) -> Result<Option<PairedBoundary>, LevelBusy> {
    acquire_comparison(
        engine,
        snapshot,
        PAIRED_BOUNDARY_TAIL,
        true,
        |view, span, tail| paired_boundary_for_current(view, span, tail, Instant::now()),
    )
}

fn acquire_comparison<T>(
    engine: &KirinHyphaEngine,
    snapshot: &MeterSessionSnapshot,
    tail_capacity: usize,
    post_meter_only: bool,
    finish: impl FnOnce(&TimeComparisonView, TimeSourceSpan, &[TimeRawPoint]) -> Option<T>,
) -> Result<Option<T>, LevelBusy> {
    let Some(exchange) = engine.meter_delta_history.as_ref() else {
        return Ok(None);
    };
    let before = authority(engine).ok_or(LevelBusy)?;
    if before.signal != KIRIN_SIGNAL_STATE_ACTIVE {
        return Err(LevelBusy); // A stop raced an active acquisition; retry the local projection.
    }
    if post_meter_only && (before.role != Some(PluginDataRole::Post) || before.audition_active) {
        return Ok(None);
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
    let (local_span, tail) = {
        let session = engine
            .meter_session
            .as_ref()
            .ok_or(LevelBusy)?
            .try_lock()
            .map_err(|_| LevelBusy)?;
        if !session.time_is_active() {
            return Ok(None);
        }
        (
            session.time_source_span(),
            session.time_raw_tail(tail_capacity),
        )
    };
    let after = authority(engine).ok_or(LevelBusy)?;
    if after != before {
        return Err(LevelBusy);
    }
    let Some(point) = view.point.as_ref() else {
        return Ok(None);
    };
    if point.wire.observed > snapshot.observed_frames {
        // The pair was published after this snapshot was read: not a missing pair.
        return if post_meter_only {
            Err(LevelBusy)
        } else {
            Ok(None)
        };
    }
    Ok(finish(&view, local_span, &tail))
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

pub(super) fn paired_boundary_for_current(
    view: &TimeComparisonView,
    span: TimeSourceSpan,
    tail: &[TimeRawPoint],
    now: Instant,
) -> Option<PairedBoundary> {
    let difference = values_for_current(view, span, tail.last(), now)?;
    let point = view.point.as_ref()?;
    let own = tail
        .iter()
        .rev()
        .find(|local| local.wire.observed == point.wire.observed)?;
    if own.wire.span != span
        || own.wire.run != point.wire.run
        || own.wire.exact_key() != point.wire.exact_key()
    {
        return None;
    }
    Some(PairedBoundary {
        until: now + point.comparison_remaining(now),
        own: (own.wire.values, own.wire.crest),
        difference,
    })
}
