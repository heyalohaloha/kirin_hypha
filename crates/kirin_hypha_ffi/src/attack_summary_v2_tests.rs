use super::aggregate::fixed_cohort;
use super::*;

fn source() -> KirinSnapshotSourceKey {
    KirinSnapshotSourceKey {
        incarnation: [1; 16],
        generation: 2,
        sample_rate: 1000,
        channels: 2,
        odf_hash: [3; 32],
        reserved: [0; 3],
    }
}
fn header(target: u8) -> KirinSnapshotHeader {
    KirinSnapshotHeader {
        version: 2,
        struct_size: std::mem::size_of::<KirinAttackBandSummaryV2>() as u32,
        kind: KIRIN_SNAPSHOT_BAND_SUMMARY,
        target,
        band: 1,
        signal_state: 1,
        source: source(),
        cutoff_sample: 10_000,
        ..Default::default()
    }
}
fn event(id: i64, scalar: KirinSnapshotScalarEvidence) -> SummaryEvent {
    SummaryEvent {
        key: KirinSnapshotEventKey {
            source: source(),
            event_sample: 6000 + id,
            token: id as u64,
        },
        kind: 0,
        lanes: [scalar; 4],
        pre: None,
        post: None,
        proof_head: [0; 96],
        proof_tail: [0; 64],
    }
}
fn exact(value: f64) -> KirinSnapshotScalarEvidence {
    KirinSnapshotScalarEvidence::numeric(KirinSnapshotInterval::point(value, 0), 0)
}
fn bound(lower: f64) -> KirinSnapshotScalarEvidence {
    KirinSnapshotScalarEvidence::numeric(
        KirinSnapshotInterval {
            lower: KirinSnapshotEndpoint::finite(lower, true),
            upper: KirinSnapshotEndpoint::positive_infinity(),
            unit: 0,
            reserved: [0; 7],
        },
        KIRIN_REASON_LONG_TAIL,
    )
}
fn all_real() -> KirinSnapshotScalarEvidence {
    KirinSnapshotScalarEvidence::numeric(
        KirinSnapshotInterval {
            lower: KirinSnapshotEndpoint::negative_infinity(),
            upper: KirinSnapshotEndpoint::positive_infinity(),
            unit: 0,
            reserved: [0; 7],
        },
        KIRIN_REASON_LONG_TAIL,
    )
}
fn lane(events: &[SummaryEvent]) -> KirinAttackBandLaneSummaryV2 {
    assemble_summary(header(KIRIN_TARGET_DELTA), events)
        .unwrap()
        .lanes[2]
}

#[test]
fn d1_na_remains_in_denominator_and_subset_is_conditional() {
    let result = lane(&[
        event(1, exact(20.0)),
        event(2, exact(20.0)),
        event(
            3,
            KirinSnapshotScalarEvidence::unavailable(
                KIRIN_SCALAR_NOT_APPLICABLE,
                KIRIN_REASON_SILENT,
            ),
        ),
    ]);
    assert_eq!(result.cohort_count, 3);
    assert_eq!(result.class_count, [2, 0, 0, 0, 1]);
    assert_eq!(result.whole_median_available, 0);
    assert_eq!(result.exact_median, 20.0);
    assert_eq!(result.exact_count, 2);
    assert_eq!(result.render_kind, KIRIN_RENDER_CONFIRMED_SUBSET);
}

#[test]
fn d2_interval_median_may_be_point_without_all_events_exact() {
    let result = lane(&[
        event(1, exact(0.0)),
        event(2, exact(10.0)),
        event(3, bound(10.0)),
    ]);
    assert_eq!(result.whole_interval, KirinSnapshotInterval::point(10.0, 0));
    assert_eq!(result.class_count, [2, 1, 0, 0, 0]);
    assert_eq!(result.exact_median, 5.0);
    assert_eq!(result.render_kind, KIRIN_RENDER_WHOLE_POINT);
}

#[test]
fn d3_informative_whole_bound_has_priority_over_misleading_exact_subset() {
    let mut events = vec![event(1, exact(-10.0))];
    events.extend((2..=8).map(|i| event(i, bound(200.0))));
    let result = lane(&events);
    assert_eq!(
        result.whole_interval.lower,
        KirinSnapshotEndpoint::finite(200.0, true)
    );
    assert_eq!(
        result.whole_interval.upper,
        KirinSnapshotEndpoint::positive_infinity()
    );
    assert_eq!(result.exact_median, -10.0);
    assert_eq!(result.exact_count, 1);
    assert_eq!(result.render_kind, KIRIN_RENDER_WHOLE_INTERVAL);
    assert_eq!(result.whole_within_resolution, 0);
}

#[test]
fn d4_all_real_input_does_not_hide_informative_whole_point() {
    let result = lane(&[
        event(1, exact(0.0)),
        event(2, exact(0.0)),
        event(3, all_real()),
    ]);
    assert_eq!(result.whole_median_available, 1);
    assert_eq!(result.whole_numeric_informative, 1);
    assert_eq!(result.whole_interval, KirinSnapshotInterval::point(0.0, 0));
    assert_eq!(result.class_count, [2, 1, 0, 0, 0]);
}

#[test]
fn d5_whole_all_real_is_available_but_conditional_subset_is_primary() {
    let mut events = vec![event(1, exact(-10.0))];
    events.extend((2..=8).map(|i| event(i, all_real())));
    let result = lane(&events);
    assert_eq!(result.whole_median_available, 1);
    assert_eq!(result.whole_numeric_informative, 0);
    assert!(result.whole_interval.is_all_real());
    assert_eq!(result.render_kind, KIRIN_RENDER_CONFIRMED_SUBSET);
    assert_eq!(result.exact_median, -10.0);
    assert_eq!(result.exact_count, 1);
}

#[test]
fn d6_latest_eight_selected_first_pending_never_backfills() {
    let ids: Vec<_> = (1..=9).collect();
    let first = fixed_cohort(&ids, 8, 2, |id| *id);
    assert_eq!(
        first.into_iter().copied().collect::<Vec<_>>(),
        (1..=8).collect::<Vec<_>>()
    );
    let second = fixed_cohort(&ids, 9, 2, |id| *id);
    assert_eq!(
        second.into_iter().copied().collect::<Vec<_>>(),
        (2..=9).collect::<Vec<_>>()
    );
    let pending =
        KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_PENDING, KIRIN_REASON_WAITING_AUDIO);
    let mut first: Vec<_> = (1..=8)
        .map(|i| event(i, if i == 8 { pending } else { exact(i as f64) }))
        .collect();
    assert_eq!(lane(&first).exact_median, 4.0);
    assert_eq!(lane(&first).exact_count, 7);
    first.remove(0);
    first[6] = event(8, exact(8.0));
    first.push(event(9, pending));
    assert_eq!(lane(&first).exact_median, 5.0);
    assert_eq!(lane(&first).whole_median_available, 0);
    let keys: Vec<_> = first.iter().map(|e| e.key).collect();
    first[7] = event(9, exact(9.0));
    assert_eq!(
        lane(&first).whole_interval,
        KirinSnapshotInterval::point(5.5, 0)
    );
    assert_eq!(keys, first.iter().map(|e| e.key).collect::<Vec<_>>());
}

fn envelope(value: f64) -> SummaryEnvelope {
    SummaryEnvelope {
        head: [value; 96],
        tail: [value; 64],
        head_valid: [0; 96],
        tail_valid: [0; 64],
    }
}

#[test]
fn d11_mean_uses_the_same_verified_participants_on_both_sides() {
    let mut a = event(1, exact(0.0));
    let mut b = event(2, exact(0.0));
    let mut pre_a = envelope(-20.0);
    pre_a.head_valid[0] = 1;
    let mut post_a = envelope(-18.0);
    post_a.head_valid[0] = 1;
    let mut pre_b = envelope(-40.0);
    pre_b.head_valid[0] = 1;
    a.pre = Some(pre_a);
    a.post = Some(post_a);
    a.proof_head[0] = 1;
    b.pre = Some(pre_b);
    b.proof_head[0] = 1;
    let result = assemble_summary(header(KIRIN_TARGET_DELTA), &[a, b]).unwrap();
    assert_eq!(result.head[0].participating_bits, 1);
    assert_eq!(result.head[0].valid_count, 1);
    assert_eq!(result.head[0].pre_mean, -20.0);
    assert_eq!(result.head[0].post_mean, -18.0);
    assert_eq!(result.head[1].valid_count, 0);
    assert_eq!(result.head[1].connect_previous, 0);
}

#[test]
fn d13_equal_counts_with_different_participants_disconnect_and_floor_is_measured_only() {
    let mut a = event(1, exact(0.0));
    let mut b = event(2, exact(0.0));
    let mut env_a = envelope(-120.0);
    env_a.head_valid[0] = 1;
    let mut env_b = envelope(-120.0);
    env_b.head_valid[1] = 1;
    env_b.head_valid[2] = 1;
    a.post = Some(env_a);
    b.post = Some(env_b);
    let result = assemble_summary(header(KIRIN_TARGET_POST), &[a, b]).unwrap();
    assert_eq!(result.head[0].participating_bits, 1);
    assert_eq!(result.head[1].participating_bits, 2);
    assert_eq!(result.head[0].valid_count, 1);
    assert_eq!(result.head[1].valid_count, 1);
    assert_eq!(result.head[1].connect_previous, 0);
    assert_eq!(result.head[2].connect_previous, 1);
    assert_eq!(result.head[2].post_mean, -120.0);
    assert_eq!(result.head[3].valid_count, 0);
}

#[test]
fn d14_invalid_payload_is_rejected_before_assembly() {
    let mut bad = event(1, exact(1.0));
    bad.lanes[0].class = 99;
    assert!(assemble_summary(header(0), &[bad]).is_none());
    let mut bad = event(1, exact(1.0));
    bad.kind = 5;
    assert!(assemble_summary(header(0), &[bad]).is_none());
    assert!(assemble_summary(header(0), &[event(1, exact(1.0)), event(1, exact(2.0))]).is_none());
    let mut request = KirinAttackBandSummaryV2Request {
        version: 2,
        struct_size: std::mem::size_of::<KirinAttackBandSummaryV2Request>() as u32,
        band: 1,
        ..Default::default()
    };
    assert!(request.is_valid());
    request.target = 2;
    assert!(!request.is_valid());
}

#[test]
fn d15_exact_age_is_per_lane_not_the_latest_cohort_event() {
    let mut old = event(1, exact(20.0));
    old.key.event_sample = 7000;
    let mut fresh = event(
        2,
        KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_PENDING, KIRIN_REASON_WAITING_AUDIO),
    );
    fresh.key.event_sample = 9900;
    fresh.lanes[1] = exact(35.0);
    let result = assemble_summary(header(0), &[old, fresh]).unwrap();
    assert_eq!(
        result.header.cutoff_sample - result.lanes[1].exact_latest_event_sample,
        100
    );
    assert_eq!(
        result.header.cutoff_sample - result.lanes[2].exact_latest_event_sample,
        3000
    );
}

#[test]
fn six_second_window_is_inclusive_and_future_keys_are_excluded() {
    let keys = [3999, 4000, 9999, 10_000, 10_001];
    assert_eq!(
        fixed_cohort(&keys, 10_000, 1000, |v| *v)
            .into_iter()
            .copied()
            .collect::<Vec<_>>(),
        vec![4000, 9999, 10_000]
    );
}

#[test]
fn new_summary_layout_has_literal_offsets_and_never_changes_the_legacy_layout() {
    use std::mem::{align_of, offset_of, size_of};
    assert_eq!(size_of::<KirinSnapshotSourceKey>(), 64);
    assert_eq!(size_of::<KirinSnapshotEventKey>(), 80);
    assert_eq!(size_of::<KirinSnapshotEndpoint>(), 16);
    assert_eq!(size_of::<KirinSnapshotInterval>(), 40);
    assert_eq!(size_of::<KirinSnapshotScalarEvidence>(), 104);
    assert_eq!(offset_of!(KirinSnapshotScalarEvidence, class), 96);
    assert_eq!(size_of::<KirinSnapshotHeader>(), 136);
    assert_eq!(offset_of!(KirinSnapshotHeader, source), 40);
    assert_eq!(size_of::<KirinAttackBandSummaryV2Request>(), 16);
    assert_eq!(size_of::<KirinAttackBandLaneSummaryV2>(), 112);
    assert_eq!(offset_of!(KirinAttackBandLaneSummaryV2, whole_interval), 8);
    assert_eq!(offset_of!(KirinAttackBandLaneSummaryV2, exact_count), 104);
    assert_eq!(size_of::<KirinAttackBandAveragePointV2>(), 56);
    assert_eq!(offset_of!(KirinAttackBandSummaryV2, cohort_count), 136);
    assert_eq!(offset_of!(KirinAttackBandSummaryV2, events), 144);
    assert_eq!(offset_of!(KirinAttackBandSummaryV2, pair_kind), 784);
    assert_eq!(offset_of!(KirinAttackBandSummaryV2, evidence), 792);
    assert_eq!(offset_of!(KirinAttackBandSummaryV2, lanes), 4120);
    assert_eq!(offset_of!(KirinAttackBandSummaryV2, head), 4568);
    assert_eq!(offset_of!(KirinAttackBandSummaryV2, tail), 9944);
    assert_eq!(size_of::<KirinAttackBandSummaryV2>(), 13528);
    assert_eq!(align_of::<KirinAttackBandSummaryV2>(), 8);
    assert_eq!(size_of::<crate::KirinAttackBandSummary>(), 2880);
}

#[test]
fn canonical_d1_ten_keys_select_last_eight_before_classifying_silent_and_pending() {
    let keys: Vec<_> = (1..=10).collect();
    let selected = fixed_cohort(&keys, 10, 2, |id| *id);
    assert_eq!(
        selected.iter().map(|id| **id).collect::<Vec<_>>(),
        (3..=10).collect::<Vec<_>>()
    );
    let events: Vec<_> = selected
        .into_iter()
        .map(|id| {
            event(
                *id,
                match id {
                    9 => KirinSnapshotScalarEvidence::unavailable(
                        KIRIN_SCALAR_NOT_APPLICABLE,
                        KIRIN_REASON_SILENT,
                    ),
                    10 => KirinSnapshotScalarEvidence::unavailable(
                        KIRIN_SCALAR_PENDING,
                        KIRIN_REASON_WAITING_AUDIO,
                    ),
                    _ => exact(*id as f64),
                },
            )
        })
        .collect();
    let result = lane(&events);
    assert_eq!(result.class_count, [6, 0, 0, 1, 1]);
    assert_eq!(result.class_count.iter().sum::<u8>(), 8);
    assert_eq!(result.exact_count, 6);
    assert_eq!(result.whole_median_available, 0);
}

#[test]
fn canonical_d3_non_exact_upper_hit_does_not_change_whole_median_ten() {
    let result = lane(&[
        event(1, exact(10.0)),
        event(2, exact(10.0)),
        event(3, bound(20.0)),
    ]);
    assert_eq!(result.whole_interval, KirinSnapshotInterval::point(10.0, 0));
    assert_eq!(result.class_count, [2, 1, 0, 0, 0]);
    assert_eq!(result.exact_count, 2);
}

#[test]
fn canonical_d5_seven_next_hits_never_become_six_second_rel_bounds() {
    let mut events = vec![event(1, exact(20.0))];
    events.extend((2..=8).map(|i| {
        event(
            i,
            KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_UNKNOWN, KIRIN_REASON_NEXT_HIT),
        )
    }));
    let result = lane(&events);
    assert_eq!(result.class_count, [1, 0, 7, 0, 0]);
    assert_eq!(result.reason_count[KIRIN_REASON_NEXT_HIT as usize], 7);
    assert_eq!(result.exact_median, 20.0);
    assert_eq!(result.exact_count, 1);
    assert_eq!(result.whole_median_available, 0);
}

#[test]
fn canonical_d15_step_latency_is_window_response_not_publication_delay() {
    let mut events: Vec<_> = (1..=8).map(|id| event(id, exact(10.0))).collect();
    for (index, expected) in [10.0, 10.0, 10.0, 25.0, 40.0].into_iter().enumerate() {
        events.remove(0);
        events.push(event(9 + index as i64, exact(40.0)));
        let result = lane(&events);
        assert_eq!(
            result.whole_interval,
            KirinSnapshotInterval::point(expected, 0)
        );
        assert_eq!(result.cohort_count, 8);
        assert_eq!(result.exact_count, 8);
    }
}

#[test]
fn canonical_d10_both_side_means_and_fill_disconnect_on_any_participant_change() {
    let mut a = event(1, exact(0.0));
    let mut b = event(2, exact(0.0));
    let mut pre_a = envelope(-20.0);
    let mut post_a = envelope(-10.0);
    let mut pre_b = envelope(-60.0);
    let mut post_b = envelope(-60.0);
    for i in [0, 1] {
        pre_a.head_valid[i] = 1;
        post_a.head_valid[i] = 1;
        a.proof_head[i] = 1;
    }
    for i in [0, 2] {
        pre_b.head_valid[i] = 1;
        post_b.head_valid[i] = 1;
        b.proof_head[i] = 1;
    }
    a.pre = Some(pre_a);
    a.post = Some(post_a);
    b.pre = Some(pre_b);
    b.post = Some(post_b);
    let result = assemble_summary(header(KIRIN_TARGET_DELTA), &[a, b]).unwrap();
    assert_eq!(result.head[0].participating_bits, 3);
    assert_eq!(result.head[0].pre_mean, -40.0);
    assert_eq!(result.head[0].post_mean, -35.0);
    assert_eq!(result.head[1].pre_mean, -20.0);
    assert_eq!(result.head[1].post_mean, -10.0);
    assert_eq!(result.head[1].participating_bits, 1);
    assert_eq!(result.head[2].participating_bits, 2);
    assert_eq!(result.head[1].connect_previous, 0);
    assert_eq!(result.head[2].connect_previous, 0);
    assert_eq!(result.head[3].valid_count, 0);
}
