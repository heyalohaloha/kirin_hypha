//! A continued Keep must accept new exact facts after an explicit meter reset.
use super::*;
use crate::{channel_layout::ChannelLayout, MeterClockStart, MeterSession};
use std::sync::{Arc, Mutex};

#[test]
fn an_explicit_post_session_reset_refreshes_the_continuing_keeps_delta() {
    let temp = tempfile::tempdir().unwrap();
    let directory = temp.path().join("project").join("pre");
    std::fs::create_dir_all(&directory).unwrap();
    let pre_json = directory.join("pre.json");
    std::fs::write(&pre_json, br#"{"instance_id":"pre","daw_session_id":"session","watch_owner_id":"owner","signal_state":"active"}"#).unwrap();
    let session = || {
        Arc::new(Mutex::new(
            MeterSession::new_in_epoch(48_000, ChannelLayout::mono(), 1).unwrap(),
        ))
    };
    let pre_session = session();
    let post_session = session();
    let pre = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&pre_session));
    let post = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&post_session));
    let target = crate::MeterHistoryTarget::from_pre_json("pre".into(), &pre_json)
        .unwrap()
        .with_post_binding("pair-owner", "post", 1, 1.0)
        .unwrap();
    post.set_pair_authority_revision(1);
    let feed = |from: i64, frames: usize, post_level: f64| {
        for (session, level) in [(&pre_session, 0.25), (&post_session, post_level)] {
            let audio: Vec<_> = (from..from + frames as i64)
                .map(|n| level * (n as f64 * std::f64::consts::TAU * 997.0 / 48_000.0).sin())
                .collect();
            assert!(session.lock().unwrap().push_active_at(
                &audio,
                MeterClockStart {
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
    let sm = crate::RecordStateMachine::new();
    sm.try_enter_record(crate::identity::License::Os).unwrap();
    let generation = sm.generation();
    sm.mark_record_delta_boundary(generation, &post_session.lock().unwrap());
    let original_boundary = sm.record_delta_boundary(generation).unwrap();
    let mut cache = RecordDeltaCache::default();
    feed(0, 48_000, 0.5);
    let before = cache.resolve(
        generation,
        tests::legacy(),
        Some(&post),
        sm.record_delta_boundary(generation),
    );
    assert!(before.lufs.is_some_and(|v| (v - 6.0206).abs() < 0.001));
    let old_point = post
        .time_comparison(MeterHistoryResolution::Hz10, 0, u64::MAX, 0)
        .unwrap()
        .unwrap()
        .unwrap()
        .point
        .unwrap();
    sm.reset_meter_session(&mut post_session.lock().unwrap());
    post.reset();
    assert_eq!(sm.generation(), generation);
    feed(48_000, 48_000, 0.125);
    let view = post
        .time_comparison(MeterHistoryResolution::Hz10, 0, u64::MAX, 0)
        .unwrap()
        .unwrap()
        .unwrap();
    assert_eq!(
        view.reason,
        TimeComparisonReason::Active,
        "the exact comparison itself has recovered"
    );
    assert!(view.point.as_ref().unwrap().wire.values[0].is_some_and(|v| (v + 6.0206).abs() < 0.001));
    let after = cache.resolve(
        generation,
        tests::legacy(),
        Some(&post),
        sm.record_delta_boundary(generation),
    );
    assert!(
        after.lufs.is_some_and(|v| (v + 6.0206).abs() < 0.001),
        "new Keep delta after reset: {:?}",
        after.lufs
    );
    let rebound = sm.record_delta_boundary(generation).unwrap();
    assert_ne!(original_boundary.span, rebound.span);
    assert!(
        !rebound.admits(&old_point),
        "reset never admits old-span facts"
    );
}

#[test]
fn session_reset_cannot_bootstrap_an_unadmitted_keep() {
    let mut session = MeterSession::new_in_epoch(48_000, ChannelLayout::mono(), 1).unwrap();
    let sm = crate::RecordStateMachine::new();
    sm.reset_meter_session(&mut session);
    assert!(sm.record_delta_boundary(sm.generation()).is_none());
    sm.try_enter_record(crate::identity::License::Os).unwrap();
    let generation = sm.generation();
    sm.reset_meter_session(&mut session);
    assert!(
        sm.record_delta_boundary(generation).is_none(),
        "Watch has not drained yet"
    );
    sm.mark_record_delta_boundary(generation, &session);
    let before = sm.record_delta_boundary(generation).unwrap();
    sm.reset_meter_session(&mut session);
    let after = sm.record_delta_boundary(generation).unwrap();
    assert_ne!(before.span, after.span);
    assert_eq!(after.observed, 0);
    sm.exit_record();
    sm.reset_meter_session(&mut session);
    assert_eq!(
        sm.record_delta_boundary(generation).unwrap().span,
        after.span,
        "a closed Keep retains its own boundary"
    );
    sm.try_enter_record(crate::identity::License::Os).unwrap();
    let next = sm.generation();
    sm.reset_meter_session(&mut session);
    assert!(
        sm.record_delta_boundary(next).is_none(),
        "an old Keep's boundary cannot admit its successor"
    );
}
