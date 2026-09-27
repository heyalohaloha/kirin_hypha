use super::*;

#[test]
fn malformed_zero_rate_content_point_is_rejected_without_division() {
    let point = ContentWirePoint {
        measurement_epoch: 1,
        incarnation: 1,
        generation: 1,
        run_id: 1,
        run_origin: 1,
        observed_frames: 19_200,
        endpoint_samples: 19_200,
        timeline_endpoint_samples: Some(19_200),
        timeline_source: 1,
        auxiliary_source: 1,
        presentation_source: 1,
        output_presentation_samples: Some(0),
        lufs_m: Some(-12.0),
        true_peak: Some(-3.0),
    };
    assert!(!point.valid(0));
    assert!(!point.valid(44_101));
    assert!(point.valid(48_000));
    let invalid_origin = ContentWirePoint {
        run_origin: 0,
        ..point
    };
    assert!(!invalid_origin.valid(48_000));
    let unknown_origin = ContentWirePoint {
        run_origin: 255,
        ..point
    };
    assert!(!unknown_origin.valid(48_000));
}

#[test]
fn host_policy_is_explicit_and_a_change_republishes_without_resetting_measurement() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    post.set_clock_policy(CLOCK_POLICY_UNKNOWN);
    for block in 0..12_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let snapshot = post_session.lock().unwrap().snapshot();
    let unknown = post.chain_snapshot(0, &snapshot).unwrap();
    assert_eq!(unknown.status, chain::Status::Unavailable);
    assert!(unknown.points.is_empty());
    assert_eq!(read_publication(directory.path()).unwrap().clock_policy, 1);

    pre.set_clock_policy(CLOCK_POLICY_UNKNOWN);
    pre.service_pre_endpoint("pre", "song", "owner", directory.path())
        .unwrap();
    assert_eq!(read_publication(directory.path()).unwrap().clock_policy, 0);
    pre.set_clock_policy(CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3);
    post.set_clock_policy(CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3);
    for block in 12..24_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let snapshot = post_session.lock().unwrap().snapshot();
    let qualified = post.chain_snapshot(0, &snapshot).unwrap();
    assert_eq!(qualified.status, chain::Status::Active);
    assert!(!qualified.points.is_empty());
}

#[test]
fn exact_host_identity_does_not_promote_missing_output_latency_to_zero() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    let samples = vec![0.25_f64; 4_800 * 2];
    for block in 0..16_i64 {
        let at = block * 4_800;
        for session in [&pre_session, &post_session] {
            assert!(session.lock().unwrap().push_active_at(
                &samples,
                MeterClockStart {
                    position_samples: Some(at),
                    epoch: Some(1),
                    source: CaptureClockSource::ProjectTimeline,
                    auxiliary: AuxiliaryClockSamples {
                        source: AuxiliaryClockSource::Vst3Continuous,
                        samples: Some(at),
                    },
                    presentation_latency: PresentationLatencySamples {
                        source: PresentationLatencySource::Vst3,
                        input: None,
                        output: None,
                    },
                }
            ));
        }
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let snapshot = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &snapshot).unwrap();
    assert_eq!(chain.status, chain::Status::Unavailable);
    assert!(chain.points.is_empty());
}

#[test]
fn previous_exchange_schema_is_rejected_instead_of_mixing_old_and_new_facts() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
    }
    pre.service_pre_endpoint("pre", "song", "owner", directory.path())
        .unwrap();
    let mut old = read_publication(directory.path()).unwrap();
    assert_eq!(old.schema, METER_HISTORY_EXCHANGE_SCHEMA);
    assert!(!old.content_windows.is_empty());
    old.schema = 6;
    old.clock_policy = CLOCK_POLICY_UNKNOWN;
    fs::write(
        directory.path().join(METER_HISTORY_EXCHANGE_FILE),
        serde_json::to_vec(&old).unwrap(),
    )
    .unwrap();
    post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    assert!(post.recent(MeterHistoryResolution::Hz10, 32).is_empty());
    let snapshot = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &snapshot).unwrap();
    assert_eq!(chain.status, chain::Status::Unavailable);
    assert!(chain.points.is_empty());
    let mut malformed = serde_json::to_value(&old).unwrap();
    malformed["schema"] = serde_json::json!(METER_HISTORY_EXCHANGE_SCHEMA);
    malformed.as_object_mut().unwrap().remove("clock_policy");
    fs::write(
        directory.path().join(METER_HISTORY_EXCHANGE_FILE),
        serde_json::to_vec(&malformed).unwrap(),
    )
    .unwrap();
    assert!(read_publication(directory.path()).is_err());
}
