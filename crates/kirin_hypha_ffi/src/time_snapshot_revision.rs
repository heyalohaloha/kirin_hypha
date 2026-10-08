//! Revision describes immutable measurement/proof/history facts, not poll-time aging.
use super::*;

pub(super) fn content_revision(
    main: &KirinTimeComponentV2,
    psr: &KirinTimeComponentV2,
    main_rows: &[MeterHistoryEntry],
    psr_rows: &[MeterHistoryEntry],
    context: (TimeSourceSpan, u64, u64, u64, u8, bool),
) -> u64 {
    let (span, cutoff, lower, binding, signal, selection) = context;
    let mut hash = std::collections::hash_map::DefaultHasher::new();
    (
        span.epoch,
        span.incarnation,
        span.generation,
        span.token,
        span.sample_rate,
        span.channels,
        cutoff,
        lower,
        binding,
        signal,
        selection,
    )
        .hash(&mut hash);
    for component in [main, psr] {
        let current = component.current;
        (
            current.cutoff,
            current.run,
            current.endpoint,
            current.target,
            current.state,
            current.clock,
            current.finite_mask,
            component.history_count,
            component.history_hold,
            component.reason,
        )
            .hash(&mut hash);
        for value in current.values {
            value.to_bits().hash(&mut hash);
        }
        let pre = component.pre_span;
        (
            pre.epoch,
            pre.incarnation,
            pre.generation,
            pre.token,
            pre.sample_rate,
            pre.channels,
            component.pre_run,
            component.binding_revision,
            component.locator_identity,
            component.owner_identity,
            component.claim_identity,
        )
            .hash(&mut hash);
    }
    for rows in [main_rows, psr_rows] {
        rows.len().hash(&mut hash);
        for row in rows {
            (
                row.measurement_epoch,
                row.generation,
                row.run_id,
                row.segment_id,
                row.first_observed_frames,
                row.last_observed_frames,
                row.first_timeline_endpoint_samples,
                row.last_timeline_endpoint_samples,
            )
                .hash(&mut hash);
            (
                row.timeline_source as u8,
                row.observation_count,
                row.valid_count,
                row.connects_previous,
                row.resolution as u8,
            )
                .hash(&mut hash);
            row.clip_event_count.hash(&mut hash);
            for range in [
                row.lufs_m,
                row.lufs_s,
                row.true_peak,
                row.correlation,
                row.psr,
            ] {
                (
                    range.min.map(f64::to_bits),
                    range.max.map(f64::to_bits),
                    range.mean.map(f64::to_bits),
                )
                    .hash(&mut hash);
            }
        }
    }
    hash.finish()
}
