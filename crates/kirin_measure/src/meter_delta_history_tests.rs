use super::*;
use crate::{MeterClockStart, MeterHistoryRange};

fn post_point(observed: u64, endpoint: i64, value: f64) -> MeterHistoryEntry {
    let exact = |value| MeterHistoryRange {
        min: Some(value),
        max: Some(value),
        mean: Some(value),
    };
    MeterHistoryEntry {
        resolution: MeterHistoryResolution::Hz10,
        generation: 3,
        run_id: 7,
        observation_count: 1,
        first_observed_frames: observed,
        last_observed_frames: observed,
        first_timeline_endpoint_samples: Some(endpoint),
        last_timeline_endpoint_samples: Some(endpoint),
        timeline_source: CaptureClockSource::ProjectTimeline,
        lufs_m: exact(value),
        lufs_s: exact(value - 1.0),
        true_peak: exact(value + 10.0),
        correlation: exact(0.8),
        plr: exact(12.0),
    }
}

fn pre_point(endpoint: i64, value: f64) -> WirePoint {
    WirePoint {
        generation: 2,
        run_id: 5,
        observed_frames: endpoint as u64,
        endpoint_samples: endpoint,
        source: CaptureClockSource::ProjectTimeline as u8,
        lufs_m: Some(value),
        lufs_s: Some(value - 1.0),
        true_peak: Some(value + 10.0),
        correlation: Some(0.5),
        plr: Some(10.5),
    }
}

#[test]
fn joins_only_the_same_unique_presentation_endpoint() {
    let mut delta = DeltaHistoryState::default();
    delta.bind(PairKey {
        instance_id: "pre".into(),
        instance_dir: "/tmp/pre".into(),
        owner_id: "owner".into(),
    });
    delta.ingest(
        &[pre_point(4_800, -20.0), pre_point(9_600, -18.0)],
        &[
            post_point(4_800, 4_800, -18.5),
            post_point(9_600, 9_600, -17.0),
        ],
        48_000,
    );
    let history = delta.history.recent(MeterHistoryResolution::Hz10, 10);
    assert_eq!(history.len(), 2);
    assert_eq!(history[0].lufs_m.mean, Some(1.5));
    assert_eq!(history[1].lufs_m.mean, Some(1.0));
    assert!((history[0].correlation.mean.unwrap() - 0.3).abs() < 1.0e-12);
    assert_eq!(history[0].plr.mean, Some(1.5));
}

#[test]
fn repeated_or_missing_endpoints_never_create_a_delta_fact() {
    let mut delta = DeltaHistoryState::default();
    let repeated = pre_point(4_800, -20.0);
    delta.ingest(
        &[repeated.clone(), repeated],
        &[
            post_point(4_800, 4_800, -18.0),
            post_point(9_600, 9_600, -17.0),
        ],
        48_000,
    );
    assert!(delta
        .history
        .recent(MeterHistoryResolution::Hz10, 10)
        .is_empty());
}

#[test]
fn consumed_occurrences_allow_exact_short_loops_without_cross_loop_guessing() {
    let mut delta = DeltaHistoryState::default();
    let first_pre = [pre_point(4_800, -20.0), pre_point(9_600, -19.0)];
    let first_post = [
        post_point(4_800, 4_800, -18.0),
        post_point(9_600, 9_600, -17.0),
    ];
    delta.ingest(&first_pre, &first_post, 48_000);

    let mut second_pre_0 = pre_point(4_800, -21.0);
    second_pre_0.run_id = 6;
    second_pre_0.observed_frames = 14_400;
    let mut second_pre_1 = pre_point(9_600, -20.0);
    second_pre_1.run_id = 6;
    second_pre_1.observed_frames = 19_200;
    let mut second_post_0 = post_point(14_400, 4_800, -18.5);
    second_post_0.run_id = 8;
    let mut second_post_1 = post_point(19_200, 9_600, -17.5);
    second_post_1.run_id = 8;

    let all_pre = [
        first_pre[0].clone(),
        first_pre[1].clone(),
        second_pre_0,
        second_pre_1,
    ];
    let all_post = [first_post[0], first_post[1], second_post_0, second_post_1];
    delta.ingest(&all_pre, &all_post, 48_000);
    let history = delta.history.recent(MeterHistoryResolution::Hz10, 10);
    assert_eq!(history.len(), 4);
    assert_eq!(history[2].lufs_m.mean, Some(2.5));
    assert_ne!(history[1].run_id, history[2].run_id);

    let mut ambiguous_pre = pre_point(4_800, -22.0);
    ambiguous_pre.run_id = 7;
    ambiguous_pre.observed_frames = 24_000;
    let mut another_ambiguous_pre = ambiguous_pre.clone();
    another_ambiguous_pre.run_id = 8;
    another_ambiguous_pre.observed_frames = 28_800;
    let mut unmatched_post = post_point(24_000, 4_800, -18.0);
    unmatched_post.run_id = 9;
    delta.ingest(
        &[ambiguous_pre, another_ambiguous_pre],
        &[unmatched_post],
        48_000,
    );
    assert_eq!(
        delta.history.recent(MeterHistoryResolution::Hz10, 10).len(),
        4
    );
}

#[test]
fn pair_change_discards_history_instead_of_blending_sources() {
    let mut delta = DeltaHistoryState::default();
    delta.bind(PairKey {
        instance_id: "pre-a".into(),
        instance_dir: "/tmp/pre-a".into(),
        owner_id: "owner-a".into(),
    });
    delta.ingest(
        &[pre_point(4_800, -20.0)],
        &[post_point(4_800, 4_800, -18.0)],
        48_000,
    );
    assert_eq!(
        delta.history.recent(MeterHistoryResolution::Hz10, 10).len(),
        1
    );
    delta.bind(PairKey {
        instance_id: "pre-b".into(),
        instance_dir: "/tmp/pre-b".into(),
        owner_id: "owner-b".into(),
    });
    assert!(delta
        .history
        .recent(MeterHistoryResolution::Hz10, 10)
        .is_empty());
}

fn sine(amplitude: f64) -> Vec<f64> {
    let mut samples = Vec::with_capacity(48_000 * 2);
    for frame in 0..48_000 {
        let value =
            amplitude * (2.0 * std::f64::consts::PI * 1_000.0 * frame as f64 / 48_000.0).sin();
        samples.extend_from_slice(&[value, value]);
    }
    samples
}

#[test]
fn atomic_publication_and_exact_target_join_work_end_to_end() {
    let directory = tempfile::tempdir().unwrap();
    let pre_session = Arc::new(Mutex::new(MeterSession::new(48_000, 2).unwrap()));
    pre_session.lock().unwrap().push_active_at(
        &sine(0.25),
        MeterClockStart {
            position_samples: Some(0),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
        },
    );
    let pre = MeterDeltaHistoryExchange::new(48_000, pre_session);
    pre.service_pre_endpoint("pre", "song", "owner", directory.path())
        .unwrap();
    let pre_json = directory.path().join("pre.json");
    fs::write(
        &pre_json,
        br#"{"instance_id":"pre","daw_session_id":"song","watch_owner_id":"owner","signal_state":"active"}"#,
    )
    .unwrap();

    let post_session = Arc::new(Mutex::new(MeterSession::new(48_000, 2).unwrap()));
    post_session.lock().unwrap().push_active_at(
        &sine(0.5),
        MeterClockStart {
            position_samples: Some(0),
            epoch: Some(9),
            source: CaptureClockSource::ProjectTimeline,
        },
    );
    let post = MeterDeltaHistoryExchange::new(48_000, post_session);
    post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &pre_json));
    let joined = post.recent(MeterHistoryResolution::Hz10, 20);
    assert_eq!(joined.len(), 10);
    let loudness_delta = joined.last().unwrap().lufs_m.mean.unwrap();
    assert!((loudness_delta - 6.020_599_913).abs() < 0.01);
    assert!(joined.last().unwrap().plr.mean.unwrap().abs() < 0.01);

    fs::write(
        &pre_json,
        br#"{"instance_id":"pre","daw_session_id":"song","watch_owner_id":"replacement","signal_state":"active"}"#,
    )
    .unwrap();
    let replacement_post = MeterDeltaHistoryExchange::new(
        48_000,
        Arc::new(Mutex::new(MeterSession::new(48_000, 2).unwrap())),
    );
    replacement_post
        .service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &pre_json));
    assert!(replacement_post
        .recent(MeterHistoryResolution::Hz10, 20)
        .is_empty());
}
