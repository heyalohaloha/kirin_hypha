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

#[test]
fn reference_reconstruction_uses_one_confirmed_boundary_through_publication_lag() {
    let start = Instant::now();
    let (point, _, view) = tests::joined(start);
    let span = point.wire.span;
    let mut own = point.clone();
    own.wire.values = [Some(-20.0), Some(-21.0), Some(-6.0), Some(12.0), None, None];
    own.wire.crest = Some(9.0);
    let at = |boundaries: u64, psr: f64| {
        let mut later = own.clone();
        later.wire.observed += 4800 * boundaries;
        later.wire.endpoint = later
            .wire
            .endpoint
            .map(|end| end + 4800 * boundaries as i64);
        later.wire.values[3] = Some(psr);
        later.wire.crest = Some(psr - 3.0);
        later
    };
    // POST may be ahead of the newest common boundary for the whole comparison lifetime: the PRE
    // reading stays, and it is that boundary's own value, never POST's newer one.
    for boundaries in 0..10_u64 {
        let tail: Vec<_> = (0..=boundaries)
            .map(|step| at(step, 12.0 + step as f64))
            .collect();
        let now = start + Duration::from_millis(100 * boundaries);
        let pair = level::paired_boundary_for_current(&view, span, &tail, now).unwrap();
        assert_eq!(pair.own.0[3], Some(12.0));
        assert_eq!(pair.own.1, Some(9.0));
        assert_eq!(pair.difference.0[3], point.wire.values[3]);
        assert_eq!(pair.difference.1, point.wire.crest);
    }
    let tail = [own.clone(), at(1, 13.0)];
    // The boundary itself must still be held, and nothing outlives one second.
    assert!(level::paired_boundary_for_current(&view, span, &tail[1..], start).is_none());
    assert!(level::paired_boundary_for_current(&view, span, &[], start).is_none());
    for ms in [1000, 1001, 5000] {
        let now = start + Duration::from_millis(ms);
        assert!(level::paired_boundary_for_current(&view, span, &tail, now).is_none());
    }
    // A newer known missing metric is not publication lag.
    let mut missing = tail.clone();
    missing[1].wire.values[3] = None;
    let pair = level::paired_boundary_for_current(&view, span, &missing, start).unwrap();
    assert!(pair.difference.0[3].is_none() && pair.difference.1.is_some());
    let mut changed = span;
    changed.generation += 1;
    assert!(level::paired_boundary_for_current(&view, changed, &tail, start).is_none());
    for variant in 0..3 {
        let mut other = tail.clone();
        match variant {
            0 => other[0].wire.run += 1,
            1 => other[0].wire.endpoint = Some(0),
            _ => other[0].wire.usable = false,
        }
        assert!(level::paired_boundary_for_current(&view, span, &other, start).is_none());
    }
}
