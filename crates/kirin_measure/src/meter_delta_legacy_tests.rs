//! Exercise the real publisher/service path, rather than only the two in-memory joiners.
use super::*;
use crate::channel_layout::ChannelLayout;
use crate::MeterClockStart;

fn feed(session: &Arc<Mutex<MeterSession>>, position: i64) {
    let audio: Vec<_> = (0..48000)
        .flat_map(|frame| {
            let sample = 0.1 * (frame as f64 * std::f64::consts::TAU * 997.0 / 48000.0).sin();
            [sample, sample]
        })
        .collect();
    assert!(session.lock().unwrap().push_active_at(
        &audio,
        MeterClockStart {
            position_samples: Some(position),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            presentation_latency: crate::PresentationLatencySamples {
                source: crate::PresentationLatencySource::AudioUnitV2,
                input: Some(0),
                output: Some(0),
            },
            ..Default::default()
        }
    ));
}

#[test]
fn pre_reset_and_worker_restart_retire_only_v2_history_and_fresh_slots_resume_both() {
    for worker_restart in [false, true] {
        let directory = tempfile::tempdir().unwrap();
        let pre_session = Arc::new(Mutex::new(
            MeterSession::new_in_epoch(48000, ChannelLayout::stereo(), 1).unwrap(),
        ));
        let post_session = Arc::new(Mutex::new(
            MeterSession::new_in_epoch(48000, ChannelLayout::stereo(), 2).unwrap(),
        ));
        let pre = MeterDeltaHistoryExchange::new(48000, Arc::clone(&pre_session));
        let post = MeterDeltaHistoryExchange::new(48000, Arc::clone(&post_session));
        assert!(post.delta.lock().unwrap().time_history.is_none());
        let pre_json = directory.path().join("pre.json");
        fs::write(&pre_json,
            br#"{"instance_id":"pre","daw_session_id":"song","watch_owner_id":"owner","signal_state":"active"}"#).unwrap();
        let target = MeterHistoryTarget::from_pre_json("pre".into(), &pre_json)
            .unwrap()
            .with_post_binding("pair-owner", "post", 1, 1.0)
            .unwrap();
        feed(&pre_session, 0);
        feed(&post_session, 0);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(Some(target.clone()));
        let legacy = post.recent(MeterHistoryResolution::Hz10, 64);
        let initial = post
            .time_comparison(MeterHistoryResolution::Hz10, 0, u64::MAX, 1200)
            .unwrap()
            .unwrap()
            .unwrap();
        assert_eq!(legacy.len(), 10);
        assert_eq!(initial.history.len(), 10);
        assert!(post.delta.lock().unwrap().time_history.is_some());
        if worker_restart {
            pre_session.lock().unwrap().retire_time_worker_span();
        } else {
            pre_session.lock().unwrap().reset();
        }
        feed(&pre_session, 48000);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(Some(target.clone()));
        assert_eq!(
            post.recent(MeterHistoryResolution::Hz10, 64),
            legacy,
            "PRE lifecycle retirement must not erase or rewrite the old poll's history"
        );
        let retired = post
            .time_comparison(MeterHistoryResolution::Hz10, 0, u64::MAX, 1200)
            .unwrap()
            .unwrap()
            .unwrap();
        assert!(retired.history.is_empty());
        assert!(retired.point.is_none());
        assert_ne!(retired.pre_span, initial.pre_span);
        feed(&post_session, 48000);
        post.service_post_endpoint(Some(target));
        let old = post.recent(MeterHistoryResolution::Hz10, 64);
        let resumed = post
            .time_comparison(MeterHistoryResolution::Hz10, 0, u64::MAX, 1200)
            .unwrap()
            .unwrap()
            .unwrap();
        assert_eq!(old.len(), 20);
        assert_eq!(&old[..10], legacy.as_slice());
        assert_eq!(resumed.history.len(), 10);
        assert!(resumed
            .history
            .iter()
            .all(|row| row.first_observed_frames > 48000));
        assert_eq!(resumed.point.unwrap().wire.endpoint, Some(96000));
    }
}
