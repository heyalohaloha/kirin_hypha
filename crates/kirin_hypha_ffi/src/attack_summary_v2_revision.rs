//! Content fingerprints include late facts, not just the navigation cutoff.
use super::*;

pub(super) fn content_revision(
    snapshot: &KirinAttackBandSummaryV2,
    post_revision: u64,
    pre_revision: u64,
) -> u64 {
    use std::hash::{Hash, Hasher};
    let mut hash = std::collections::hash_map::DefaultHasher::new();
    snapshot.comparison_binding.hash(&mut hash);
    (
        snapshot.has_representative_pre,
        snapshot.has_representative_post,
    )
        .hash(&mut hash);
    for (has, detail) in [
        (
            snapshot.has_representative_pre,
            &snapshot.representative_pre,
        ),
        (
            snapshot.has_representative_post,
            &snapshot.representative_post,
        ),
    ] {
        if has == 0 {
            continue;
        }
        (
            detail.generation,
            detail.event_sample,
            detail.shape_start_sample,
            detail.shape_end_sample,
            detail.body_end_sample,
            detail.complete,
            detail.transient_available,
            detail.sharpness_available,
            detail.shape_count,
            detail.bin_frames,
        )
            .hash(&mut hash);
        for value in [
            detail.value,
            detail.attack_rms_dbfs,
            detail.sample_peak_dbfs,
            detail.crest_db,
            detail.body_rms_dbfs,
            detail.transient_db,
            detail.sharpness_acum,
        ]
        .into_iter()
        .chain(detail.shape)
        {
            value.to_bits().hash(&mut hash);
        }
    }
    (
        post_revision,
        pre_revision,
        snapshot.header.authority_revision,
        snapshot.header.cutoff_sample,
        snapshot.header.target,
        snapshot.header.band,
        snapshot.header.signal_state,
        snapshot.newer_unfinished_count,
    )
        .hash(&mut hash);
    for i in 0..snapshot.cohort_count as usize {
        let key = snapshot.events[i];
        (
            key.source.incarnation,
            key.source.generation,
            key.source.sample_rate,
            key.source.channels,
            key.source.odf_hash,
            key.event_sample,
            key.token,
            snapshot.pair_kind[i],
        )
            .hash(&mut hash);
        for value in snapshot.evidence[i] {
            (
                value.measurement_revision,
                value.proof_revision,
                value.class,
                value.reason,
                value.finish,
                value.requested_start,
                value.requested_end,
                value.actual_start,
                value.actual_end,
            )
                .hash(&mut hash);
            (
                value.interval.lower.kind,
                value.interval.lower.closed,
                value.interval.lower.value.to_bits(),
                value.interval.upper.kind,
                value.interval.upper.closed,
                value.interval.upper.value.to_bits(),
            )
                .hash(&mut hash);
        }
    }
    for point in snapshot.head.iter().chain(snapshot.tail.iter()) {
        (
            point.participating_bits,
            point.valid_count,
            point.connect_previous,
            point.has_pre,
            point.pre_mean.to_bits(),
            point.pre_min.to_bits(),
            point.pre_max.to_bits(),
            point.post_mean.to_bits(),
            point.post_min.to_bits(),
            point.post_max.to_bits(),
        )
            .hash(&mut hash);
    }
    hash.finish()
}
