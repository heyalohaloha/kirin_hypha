use super::*;
use std::sync::{Arc, Mutex};

#[test]
fn a_previous_active_point_is_not_adopted_by_the_next_keep() {
    let mut session =
        crate::MeterSession::new(48_000, crate::channel_layout::ChannelLayout::mono()).unwrap();
    assert!(session.push_active(&vec![0.25; 4_800]));
    let mut old = session.time_raw_tail(1).pop().unwrap();
    old.wire.values[3] = Some(3.0);
    let sm = crate::RecordStateMachine::new();
    sm.mark_record_delta_boundary(8, &session);
    assert!(sm.record_delta_boundary(7).is_none());
    let boundary = sm.record_delta_boundary(8);
    let mut cache = RecordDeltaCache::default();
    cache.select(
        7,
        tests::legacy(),
        Some(&old),
        Some(RecordDeltaBoundary {
            span: old.wire.span,
            observed: 0,
        }),
    );
    let next = cache.select(8, tests::legacy(), Some(&old), boundary);
    assert_eq!(
        next.psr,
        Some(-5.0),
        "the next Keep has no exact observation yet"
    );
    assert!(session.push_active(&vec![0.25; 4_800]));
    let mut fresh = session.time_raw_tail(1).pop().unwrap();
    fresh.wire.values[3] = Some(6.0);
    assert_eq!(
        cache.select(8, tests::legacy(), Some(&fresh), boundary).psr,
        Some(6.0)
    );
    assert_eq!(
        cache.select(8, tests::legacy(), None, boundary).psr,
        Some(6.0)
    );
    sm.mark_record_delta_boundary(9, &session);
    assert_eq!(
        cache
            .select(
                9,
                tests::legacy(),
                Some(&fresh),
                sm.record_delta_boundary(9)
            )
            .psr,
        Some(-5.0)
    );
    let mut different_span = fresh;
    different_span.wire.span.incarnation += 1;
    different_span.wire.observed += 4_800;
    assert_eq!(
        cache
            .select(
                9,
                tests::legacy(),
                Some(&different_span),
                sm.record_delta_boundary(9)
            )
            .psr,
        Some(-5.0)
    );
}

#[test]
fn retained_active_exchange_history_cannot_seed_a_new_keep() {
    let temp = tempfile::tempdir().unwrap();
    let directory = temp.path().join("project").join("pre");
    std::fs::create_dir_all(&directory).unwrap();
    let pre_json = directory.join("pre.json");
    std::fs::write(&pre_json, br#"{"instance_id":"pre","daw_session_id":"session","watch_owner_id":"owner","signal_state":"active"}"#).unwrap();
    let pre_session = Arc::new(Mutex::new(
        crate::MeterSession::new_in_epoch(48_000, crate::channel_layout::ChannelLayout::mono(), 1)
            .unwrap(),
    ));
    let post_session = Arc::new(Mutex::new(
        crate::MeterSession::new_in_epoch(48_000, crate::channel_layout::ChannelLayout::mono(), 1)
            .unwrap(),
    ));
    let pre = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&pre_session));
    let post = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&post_session));
    let target = crate::MeterHistoryTarget::from_pre_json("pre".into(), &pre_json)
        .unwrap()
        .with_post_binding("pair-owner", "post", 1, 1.0)
        .unwrap();
    post.set_pair_authority_revision(1);
    let feed = |from: i64, count: usize| {
        for (session, level) in [(&pre_session, 0.25), (&post_session, 0.5)] {
            let audio: Vec<_> = (from..from + count as i64)
                .map(|n| level * (n as f64 * std::f64::consts::TAU * 997.0 / 48_000.0).sin())
                .collect();
            assert!(session.lock().unwrap().push_active_at(
                &audio,
                crate::MeterClockStart {
                    position_samples: Some(from),
                    epoch: Some(1),
                    source: crate::CaptureClockSource::ProjectTimeline,
                    ..Default::default()
                }
            ));
        }
        pre.service_pre_endpoint("pre", "session", "owner", &directory)
            .unwrap();
        post.service_post_endpoint(Some(target.clone()));
    };
    feed(0, 48_000);
    let view = post
        .time_comparison(MeterHistoryResolution::Hz10, 0, u64::MAX, 0)
        .unwrap()
        .unwrap()
        .unwrap();
    assert_eq!(view.reason, TimeComparisonReason::Active);
    assert!(view.point.unwrap().wire.values[0].is_some_and(|v| (v - 6.0206).abs() < 0.001));
    let sm = crate::RecordStateMachine::new();
    sm.mark_record_delta_boundary(7, &post_session.lock().unwrap());
    let mut cache = RecordDeltaCache::default();
    assert_eq!(
        cache
            .resolve(7, tests::legacy(), Some(&post), sm.record_delta_boundary(7))
            .lufs,
        Some(-8.0)
    );
    feed(48_000, 4_800);
    let fresh = cache.resolve(7, tests::legacy(), Some(&post), sm.record_delta_boundary(7));
    assert!(fresh.lufs.is_some_and(|v| (v - 6.0206).abs() < 0.001));
    sm.mark_record_delta_boundary(8, &post_session.lock().unwrap());
    assert_eq!(
        cache
            .resolve(8, tests::legacy(), Some(&post), sm.record_delta_boundary(8))
            .lufs,
        Some(-8.0)
    );
}
