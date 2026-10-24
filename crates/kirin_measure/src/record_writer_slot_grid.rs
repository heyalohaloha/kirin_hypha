//! Producer-owned Record slot positions before and after a WAV clock is attached.
use super::*;

pub(super) fn producer_take_slot_positions(
    ctx: &RecordingCtx,
    expected_len: usize,
    slot_samples: i64,
) -> Option<Vec<i64>> {
    if expected_len == 0 || slot_samples <= 0 {
        return None;
    }
    let duration_samples = i64::try_from(trace_duration_samples(ctx)).ok()?;
    let bwf_start = expected_wav(ctx)
        .and_then(|expected| expected.wav_time_reference_samples)
        .and_then(|start| i64::try_from(start).ok());
    let start = bwf_start.or_else(|| ctx.presentation_range.map(|range| range.0))?;
    let end = start.checked_add(duration_samples)?;
    if bwf_start.is_none()
        && ctx
            .presentation_range
            .is_some_and(|range| range != (start, end))
    {
        return None;
    }
    // Before a WAV is known, count whole-duration slots backwards from the last absolute
    // boundary. A partial start can contribute an extra observed boundary: selecting forwards
    // from it would lose the measured endpoint or shift all retained slot identities.
    // Once BWF sample zero is known, the public WAV contract remains explicitly start-relative.
    let first = if bwf_start.is_some() {
        start.checked_add(slot_samples)?
    } else {
        let last = end.checked_sub(end.rem_euclid(slot_samples))?;
        let span = i64::try_from(expected_len.checked_sub(1)?)
            .ok()?
            .checked_mul(slot_samples)?;
        let first = last.checked_sub(span)?;
        if first <= start {
            return None;
        }
        first
    };
    let positions = (0..expected_len)
        .map(|index| {
            i64::try_from(index)
                .ok()?
                .checked_mul(slot_samples)?
                .checked_add(first)
        })
        .collect::<Option<Vec<_>>>()?;
    let grid_end = positions.last().copied()?;
    (grid_end <= end && end.saturating_sub(grid_end) < slot_samples).then_some(positions)
}

#[cfg(test)]
#[path = "record_writer_slot_grid_tests.rs"]
mod tests;
