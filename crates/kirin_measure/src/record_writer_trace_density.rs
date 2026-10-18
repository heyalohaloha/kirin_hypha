//! Whether the Record TRACE covers and densely measures the span it must describe.
use super::*;

/// The span the TRACE must cover: the record's duration, or only the TRACE pass of a Keep whose
/// listed later passes explain the rest of the record (INV-R10).
pub(super) fn trace_duration_ms(ctx: &RecordingCtx) -> u64 {
    let duration_ms = record_duration_ms(ctx);
    let data = ctx.writer.data();
    let duration_samples = record_duration_samples(ctx);
    let span = crate::record_take::trace_span_samples(
        &data.keep_passes,
        duration_samples,
        data.sample_rate,
    );
    if span >= duration_samples || data.sample_rate == 0 {
        return duration_ms;
    }
    span.saturating_mul(1_000) / u64::from(data.sample_rate)
}

pub(super) fn frame_timeline_covers_duration(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let frames = &ctx.writer.data().frames;
    let Some(first) = frames.first() else {
        return false;
    };
    if first.t_ms > FRAME_COVERAGE_EDGE_TOLERANCE_MS {
        return false;
    }
    if duration_ms > 0 {
        let Some(last) = frames.last() else {
            return false;
        };
        if last.t_ms.saturating_add(FRAME_COVERAGE_EDGE_TOLERANCE_MS) < duration_ms {
            return false;
        }
    }
    frames
        .windows(2)
        .all(|pair| pair[1].t_ms.saturating_sub(pair[0].t_ms) <= FRAME_COVERAGE_MAX_GAP_MS)
}

fn frame_timeline_has_gap(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    !frame_timeline_covers_duration(ctx, duration_ms)
}

pub(super) fn timeline_times_cover_duration(times: &mut Vec<u64>, duration_ms: u64) -> bool {
    if times.is_empty() {
        return false;
    }
    times.sort_unstable();
    times.dedup();
    let Some(first) = times.first() else {
        return false;
    };
    if *first > FRAME_COVERAGE_EDGE_TOLERANCE_MS {
        return false;
    }
    if duration_ms > 0 {
        let Some(last) = times.last() else {
            return false;
        };
        if last.saturating_add(FRAME_COVERAGE_EDGE_TOLERANCE_MS) < duration_ms {
            return false;
        }
    }
    times
        .windows(2)
        .all(|pair| pair[1].saturating_sub(pair[0]) <= FRAME_COVERAGE_MAX_GAP_MS)
}

fn trace_density_is_sparse(ctx: &RecordingCtx) -> bool {
    if ctx.writer.data().frames.is_empty() {
        return true;
    }
    let duration_ms = trace_duration_ms(ctx);
    if ctx.raw_trace_timeline_covers_duration == Some(false) {
        return true;
    }
    if frame_timeline_has_gap(ctx, duration_ms) {
        return true;
    }
    if duration_ms < TRACE_DENSITY_MIN_DURATION_MS {
        return false;
    }
    if ctx.trace_sample_count > 0 {
        let expected = expected_trace_samples(duration_ms);
        let measured = if !ctx.trace_samples.is_empty() {
            measured_trace_sample_count(ctx)
        } else {
            ctx.trace_sample_count
        };
        return measured.saturating_mul(TRACE_DENSITY_MIN_RATIO_DEN)
            < expected.saturating_mul(TRACE_DENSITY_MIN_RATIO_NUM);
    }
    fallback_frame_density_is_sparse(ctx, duration_ms)
}

pub(super) fn measured_trace_sample_count(ctx: &RecordingCtx) -> usize {
    ctx.trace_samples
        .iter()
        .filter(|sample| sample.has_measured_core())
        .count()
}

fn fallback_frame_density_is_sparse(ctx: &RecordingCtx, duration_ms: u64) -> bool {
    let frame_count = ctx.writer.data().frames.len();
    if frame_count == 0 {
        return true;
    }
    if frame_timeline_has_gap(ctx, duration_ms) {
        return true;
    }
    let expected = expected_trace_samples(duration_ms);
    frame_count.saturating_mul(TRACE_DENSITY_MIN_RATIO_DEN)
        < expected.saturating_mul(TRACE_DENSITY_MIN_RATIO_NUM)
}

pub(super) fn mark_integrity_if_trace_density_is_sparse(ctx: &mut RecordingCtx) {
    let duration_ms = trace_duration_ms(ctx);
    let has_frame_gap = frame_timeline_has_gap(ctx, duration_ms);
    let has_raw_gap = ctx.raw_trace_timeline_covers_duration == Some(false);
    if trace_density_is_sparse(ctx) {
        log::warn!(
            "[writer] sparse Record density: trace_samples={} frames={} duration_ms={} - marking integrity_degraded",
            ctx.trace_sample_count,
            ctx.writer.data().frames.len(),
            duration_ms
        );
        ctx.writer.mark_integrity_degraded();
        if ctx.writer.data().frames.is_empty() {
            ctx.writer.add_integrity_reason("zero_trace_frames");
        } else if has_raw_gap {
            ctx.writer.add_integrity_reason("raw_trace_timeline_gap");
        } else if has_frame_gap {
            ctx.writer.add_integrity_reason("frame_timeline_gap");
        } else {
            ctx.writer.add_integrity_reason("sparse_trace_density");
        }
    }
}

#[cfg(test)]
#[path = "record_writer_trace_density_tests.rs"]
mod tests;
