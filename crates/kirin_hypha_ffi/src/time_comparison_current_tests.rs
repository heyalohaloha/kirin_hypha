use super::*;
use std::time::Duration;

#[test]
fn level_and_time_bridge_401_through_999_ms_without_renewing_original_completion() {
    let start = Instant::now();
    let (point, authority, view) = tests::joined(start);
    let mut latest = point.clone();
    latest.wire.observed += 19200;
    latest.wire.endpoint = latest.wire.endpoint.map(|end| end + 19200);
    for ms in 401..1000 {
        let now = start + Duration::from_millis(ms);
        let level = level::values_for_current(&view, point.wire.span, Some(&latest), now).unwrap();
        assert_eq!(level, (point.wire.values, point.wire.crest));
        let time = compared(
            Some(&view),
            &authority,
            point.wire.span,
            Some(&latest),
            true,
            now,
            0,
        );
        assert_eq!(time.current.state, KIRIN_TIME_CURRENT_LIVE);
        assert!((time.current.completion_age_ms - ms as f64).abs() < 1e-9);
        assert!((time.current.remaining_ms - (1000 - ms) as f64).abs() < 1e-9);
        assert_eq!(time.current.cutoff, point.wire.observed);
        assert_eq!(time.current.finite_mask, 63);
        assert!(point.remaining(now).is_zero()); // Absolute POST keeps its 400 ms lifetime.
    }
    for ms in [1000, 1001, 5000] {
        let now = start + Duration::from_millis(ms);
        assert!(level::values_for_current(&view, point.wire.span, Some(&latest), now).is_none());
        let time = compared(
            Some(&view),
            &authority,
            point.wire.span,
            Some(&latest),
            true,
            now,
            0,
        );
        assert_eq!(time.current.state, KIRIN_TIME_CURRENT_EXPIRED);
        assert_eq!(time.current.finite_mask, 0);
    }
}

#[test]
fn continuity_source_and_pair_edges_retire_before_one_second() {
    let start = Instant::now();
    let (point, authority, view) = tests::joined(start);
    for variant in 0..12 {
        let mut local = point.clone();
        let mut changed = view.clone();
        match variant {
            0 => local.wire.run += 1,
            1 => local.wire.endpoint = Some(0), // Seek/loop before IO sees the new run.
            2 => local.wire.observed -= 1,
            3 => local.wire.usable = false,
            4 => local.wire.clock = 2,
            5 => local.wire.span.channels = 1,
            6 => local.wire.span.sample_rate = 44100,
            7 => local.wire.span.token += 1,
            8 => local.wire.span.epoch += 1,
            9 => changed.reason = TimeComparisonReason::Stopped,
            10 => changed.post_run += 1,
            _ => {
                local.wire.observed += 48000;
                local.wire.endpoint = local.wire.endpoint.map(|end| end + 48000);
            }
        }
        let now = start + Duration::from_millis(401);
        assert!(
            level::values_for_current(&changed, point.wire.span, Some(&local), now).is_none(),
            "variant {variant}"
        );
        let time = compared(
            Some(&changed),
            &authority,
            point.wire.span,
            Some(&local),
            true,
            now,
            0,
        );
        assert_eq!(
            time.current.state, KIRIN_TIME_CURRENT_MISSING,
            "variant {variant}"
        );
        assert_eq!(time.current.finite_mask, 0);
    }
    for variant in 0..6 {
        let mut changed = tests::joined(start).1;
        match variant {
            0 => changed.pair.exact = None,
            1 => changed.pair.exact.as_mut().unwrap().generation += 1,
            2 => changed.pair.exact.as_mut().unwrap().pre_instance_id = "other".into(),
            3 => changed.claim += 1,
            4 => changed.owner = "other".into(),
            _ => changed.post_id = "other".into(),
        }
        assert!(!comparison_matches(&view, &changed, point.wire.span));
    }
    let stopped = compared(
        Some(&view),
        &authority,
        point.wire.span,
        Some(&point),
        false,
        start,
        0,
    );
    assert_eq!(stopped.current.state, KIRIN_TIME_CURRENT_STOPPED);
    assert_eq!(stopped.current.finite_mask, 0);

    // A newer known missing metric is not publication lag.
    let mut missing = point.clone();
    missing.wire.values[0] = None;
    missing.wire.crest = None;
    let values = level::values_for_current(&view, point.wire.span, Some(&missing), start).unwrap();
    assert!(values.0[0].is_none() && values.1.is_none());
    assert_eq!(values.0[3], point.wire.values[3]);
}
