use super::*;

fn interval(
    lower: f64,
    lower_closed: bool,
    upper: f64,
    upper_closed: bool,
) -> KirinSnapshotInterval {
    KirinSnapshotInterval {
        lower: KirinSnapshotEndpoint::finite(lower, lower_closed),
        upper: KirinSnapshotEndpoint::finite(upper, upper_closed),
        unit: KIRIN_INTERVAL_MILLISECONDS,
        reserved: [0; 7],
    }
}

#[test]
fn d7_even_median_preserves_open_endpoint_in_both_orders() {
    let a = interval(0.0, true, 1.0, false);
    let b = interval(2.0, true, 3.0, true);
    let expected = interval(1.0, true, 2.0, false);
    assert_eq!(median_interval(&[a, b]), Some(expected));
    assert_eq!(median_interval(&[b, a]), Some(expected));
}

#[test]
fn d8_lower_bounds_remain_open() {
    let lower = |value, closed| KirinSnapshotInterval {
        lower: KirinSnapshotEndpoint::finite(value, closed),
        upper: KirinSnapshotEndpoint::positive_infinity(),
        unit: 0,
        reserved: [0; 7],
    };
    let result = median_interval(&[lower(10.0, false), lower(20.0, true)]).unwrap();
    assert_eq!(result.lower, KirinSnapshotEndpoint::finite(15.0, false));
    assert_eq!(result.upper, KirinSnapshotEndpoint::positive_infinity());
}

#[test]
fn d9_endpoint_ties_have_fixed_order_under_all_permutations() {
    let values = [
        interval(0.0, true, 10.0, false),
        interval(0.0, true, 10.0, false),
        interval(0.0, false, 10.0, true),
    ];
    let expected = interval(0.0, true, 10.0, false);
    for order in [
        [0, 1, 2],
        [0, 2, 1],
        [1, 0, 2],
        [1, 2, 0],
        [2, 0, 1],
        [2, 1, 0],
    ] {
        assert_eq!(median_interval(&order.map(|i| values[i])), Some(expected));
    }
}

#[test]
fn d10_delta_precedes_median_not_side_medians() {
    let pre = [0.0, 100.0, 101.0].map(|v| KirinSnapshotInterval::point(v, 0));
    let post = [0.0, 1.0, 100.0].map(|v| KirinSnapshotInterval::point(v, 0));
    let deltas: Vec<_> = post
        .into_iter()
        .zip(pre)
        .map(|(b, a)| subtract_intervals(b, a).unwrap())
        .collect();
    assert_eq!(
        median_interval(&deltas).unwrap(),
        KirinSnapshotInterval::point(-1.0, 0)
    );
    assert_eq!(
        subtract_intervals(
            median_interval(&post).unwrap(),
            median_interval(&pre).unwrap()
        )
        .unwrap(),
        KirinSnapshotInterval::point(-99.0, 0)
    );
}

#[test]
fn d12_raw_endpoints_and_extreme_finite_values_are_not_presentation_rounded() {
    for value in [146.6, -52.06, -10.14, f64::from_bits(1), f64::MAX] {
        let point = KirinSnapshotInterval::point(value, 0);
        assert_eq!(median_interval(&[point, point]), Some(point));
    }
    assert_eq!(
        median_interval(&[
            KirinSnapshotInterval::point(f64::from_bits(1), 0),
            KirinSnapshotInterval::point(f64::from_bits(2), 0)
        ])
        .unwrap()
        .lower
        .value
        .to_bits(),
        2
    );
    assert!(subtract_intervals(
        KirinSnapshotInterval::point(f64::MAX, 0),
        KirinSnapshotInterval::point(-f64::MAX, 0)
    )
    .is_none());
}

#[test]
fn invalid_enums_nan_and_empty_open_intervals_are_rejected() {
    let mut value = interval(0.0, true, 1.0, true);
    value.unit = 2;
    assert!(median_interval(&[value]).is_none());
    value.unit = 0;
    value.lower.kind = 9;
    assert!(median_interval(&[value]).is_none());
    value.lower = KirinSnapshotEndpoint::finite(f64::NAN, true);
    assert!(!value.is_valid());
    assert!(!interval(1.0, true, 1.0, false).is_valid());
    assert!(!interval(2.0, true, 1.0, true).is_valid());
    value.lower = KirinSnapshotEndpoint::negative_infinity();
    value.lower.closed = 1;
    assert!(!value.is_valid());
}

#[test]
fn canonical_d11_lower_and_upper_endpoint_ties_are_permutation_invariant() {
    for (values, expected) in [
        (
            [
                interval(0.0, true, 1.0, false),
                interval(0.0, true, 1.0, true),
                interval(0.0, true, 1.0, true),
            ],
            interval(0.0, true, 1.0, true),
        ),
        (
            [
                interval(0.0, false, 1.0, true),
                interval(0.0, false, 1.0, true),
                interval(0.0, true, 1.0, true),
            ],
            interval(0.0, false, 1.0, true),
        ),
    ] {
        for order in [
            [0, 1, 2],
            [0, 2, 1],
            [1, 0, 2],
            [1, 2, 0],
            [2, 0, 1],
            [2, 1, 0],
        ] {
            assert_eq!(median_interval(&order.map(|i| values[i])), Some(expected));
        }
    }
}
