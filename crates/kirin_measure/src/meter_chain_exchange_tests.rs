use super::*;
use crate::channel_layout::ChannelLayout;
use crate::MeterClockStart;
use crate::{
    AuxiliaryClockSamples, AuxiliaryClockSource, PresentationLatencySamples,
    PresentationLatencySource,
};

fn advance(session: &Arc<Mutex<MeterSession>>, start: i64, amplitude: f64) {
    let samples: Vec<_> = (0..48_000)
        .flat_map(|i| {
            let v = amplitude * (i as f64 * std::f64::consts::TAU / 48.0).sin();
            [v, v]
        })
        .collect();
    assert!(session.lock().unwrap().push_active_at(
        &samples,
        MeterClockStart {
            position_samples: Some(start),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            ..MeterClockStart::default()
        }
    ));
}

fn advance_exact(
    session: &Arc<Mutex<MeterSession>>,
    project: i64,
    auxiliary: i64,
    output_latency: u32,
    amplitude: f64,
) {
    advance_format_exact(
        session,
        project,
        auxiliary,
        output_latency,
        amplitude,
        AuxiliaryClockSource::Vst3Continuous,
        PresentationLatencySource::Vst3,
    );
}

fn advance_content_exact(
    session: &Arc<Mutex<MeterSession>>,
    project: i64,
    auxiliary: i64,
    output_latency: u32,
    amplitude: f64,
) {
    advance_content_frames(
        session,
        project,
        auxiliary,
        output_latency,
        amplitude,
        4_800,
    );
}

fn advance_content_frames(
    session: &Arc<Mutex<MeterSession>>,
    project: i64,
    auxiliary: i64,
    output_latency: u32,
    amplitude: f64,
    frames: usize,
) {
    let samples: Vec<_> = (0..frames as i64)
        .flat_map(|frame| {
            let content = auxiliary + frame;
            let value = if content < 0 {
                0.0
            } else {
                let texture = 0.4 + 0.15 * (content / 3_137 % 4) as f64;
                amplitude
                    * texture
                    * ((content as f64 * std::f64::consts::TAU * 997.0 / 48_000.0).sin()
                        + 0.12 * (content as f64 * std::f64::consts::TAU * 503.0 / 48_000.0).sin())
            };
            [value, value]
        })
        .collect();
    assert!(session.lock().unwrap().push_active_at(
        &samples,
        MeterClockStart {
            position_samples: Some(project),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            auxiliary: AuxiliaryClockSamples {
                source: AuxiliaryClockSource::Vst3Continuous,
                samples: Some(auxiliary),
            },
            presentation_latency: PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: None,
                output: Some(output_latency),
            },
        }
    ));
}

fn advance_format_exact(
    session: &Arc<Mutex<MeterSession>>,
    project: i64,
    auxiliary: i64,
    output_latency: u32,
    amplitude: f64,
    auxiliary_source: AuxiliaryClockSource,
    presentation_source: PresentationLatencySource,
) {
    let samples: Vec<_> = (0..4_800)
        .flat_map(|i| {
            let value = amplitude * (i as f64 * std::f64::consts::TAU / 48.0).sin();
            [value, value]
        })
        .collect();
    assert!(session.lock().unwrap().push_active_at(
        &samples,
        MeterClockStart {
            position_samples: Some(project),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
            auxiliary: AuxiliaryClockSamples {
                source: auxiliary_source,
                samples: Some(auxiliary),
            },
            presentation_latency: PresentationLatencySamples {
                source: presentation_source,
                input: None,
                output: Some(output_latency),
            },
        }
    ));
}

type ActivePair = (
    Arc<Mutex<MeterSession>>,
    Arc<Mutex<MeterSession>>,
    Arc<MeterDeltaHistoryExchange>,
    Arc<MeterDeltaHistoryExchange>,
    std::path::PathBuf,
);

fn active_pair(directory: &std::path::Path) -> ActivePair {
    let pre_session = Arc::new(Mutex::new(
        MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 31).unwrap(),
    ));
    let post_session = Arc::new(Mutex::new(
        MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 41).unwrap(),
    ));
    let pre = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&pre_session));
    let post = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&post_session));
    pre.set_clock_policy(CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3);
    post.set_clock_policy(CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3);
    let identity = directory.join("pre.json");
    fs::write(&identity, br#"{"instance_id":"pre","watch_owner_id":"owner","daw_session_id":"song","signal_state":"active"}"#).unwrap();
    (pre_session, post_session, pre, post, identity)
}

#[path = "meter_chain_clock_policy_tests.rs"]
mod clock_policy_tests;

#[test]
fn exact_auxiliary_occurrence_and_pdc_produce_chain_facts_after_warmup() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let project = block * 4_800;
        let common_occurrence = project;
        advance_exact(&pre_session, project, common_occurrence, 0, 0.25);
        advance_exact(&post_session, project, common_occurrence, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Active);
    assert!(!chain.points.is_empty());
    let point = chain.points.last().unwrap();
    assert!(point.raw.pre_tp.zip(point.raw.post_tp).is_some());
    assert_eq!(point.raw.endpoint, 16 * 4_800);
}

#[test]
fn a_4096_sample_host_delay_joins_only_identical_content_windows() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_content_exact(&pre_session, at, at, 4_096, 0.25);
        advance_content_exact(&post_session, at, at - 4_096, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Active);
    assert!(!chain.points.is_empty());
    assert!(chain
        .points
        .iter()
        .all(|point| point.raw.endpoint % 4_800 == 0));
    let last = chain.points.last().unwrap();
    assert!((last.delta_m.unwrap() - 6.020_599_913).abs() < 0.01);
    assert!((last.delta_tp.unwrap() - 6.020_599_913).abs() < 0.01);
    assert!(last.relation.unwrap().abs() < 0.01);
}

#[test]
fn host_sized_528_frame_callbacks_preserve_the_common_grid() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    let mut at = 0_i64;
    let mut callbacks = 0_usize;
    while at < 3 * 48_000 {
        let frames = (3 * 48_000 - at).min(528) as usize;
        advance_content_frames(&pre_session, at, at, 4_096, 0.25, frames);
        advance_content_frames(&post_session, at, at - 4_096, 0, 0.5, frames);
        at += frames as i64;
        callbacks += 1;
        if callbacks.is_multiple_of(9) || at == 3 * 48_000 {
            pre.service_pre_endpoint("pre", "song", "owner", directory.path())
                .unwrap();
            post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
        }
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Active);
    assert!(chain.points.len() >= 20);
    for point in &chain.points {
        assert_eq!(point.raw.endpoint.rem_euclid(4_800), 0);
    }
    let last = chain.points.last().unwrap();
    assert!((last.delta_m.unwrap() - 6.020_599_913).abs() < 0.01);
    assert!((last.delta_tp.unwrap() - 6.020_599_913).abs() < 0.01);
    assert!(last.relation.unwrap().abs() < 0.01);
}

#[test]
fn a_one_sample_clock_mapping_error_never_enters_the_chain() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 4_096, 0.25);
        advance_exact(&post_session, at, at - 4_095, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_ne!(chain.status, chain::Status::Active);
    assert!(chain.points.is_empty());
}

#[test]
fn one_sided_pause_reestablishes_a_new_comparison_run_without_resetting_session() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..28_i64 {
        let at = block * 4_800;
        if block != 16 {
            advance_exact(&pre_session, at, at, 0, 0.25);
        } else {
            pre_session.lock().unwrap().pause();
        }
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Active);
    assert!(chain
        .points
        .last()
        .is_some_and(|point| point.raw.endpoint >= 28 * 4_800));
}

#[test]
fn project_loop_wrap_recovers_on_the_same_monotonic_auxiliary_occurrence() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..24_i64 {
        let project = (block % 8) * 4_800;
        let occurrence = block * 4_800;
        advance_exact(&pre_session, project, occurrence, 0, 0.25);
        advance_exact(&post_session, project, occurrence, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Active);
    assert_eq!(chain.points.last().unwrap().raw.endpoint, 24 * 4_800);
}

#[test]
fn reused_auxiliary_coordinates_in_a_short_loop_remain_fail_closed() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..48_i64 {
        let at = (block % 4) * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_ne!(chain.status, chain::Status::Active);
    assert!(chain.points.is_empty());
}

#[test]
fn a_seek_reusing_auxiliary_samples_invalidates_chain_until_old_high_water_is_passed() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    let service = |pre: &Arc<MeterDeltaHistoryExchange>, post: &Arc<MeterDeltaHistoryExchange>| {
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    };
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        service(&pre, &post);
    }
    let before_meter = post_session.lock().unwrap().snapshot();
    let before = post.chain_snapshot(0, &before_meter).unwrap();
    assert_eq!(before.status, chain::Status::Active);
    assert_eq!(before.points.last().unwrap().raw.endpoint, 16 * 4_800);
    for block in 0..8_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        service(&pre, &post);
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Ambiguous);
    assert!(chain.points.is_empty());
    for block in 8..21_i64 {
        let at = block * 4_800;
        advance_exact(&pre_session, at, at, 0, 0.25);
        advance_exact(&post_session, at, at, 0, 0.5);
        service(&pre, &post);
    }
    let recovered_meter = post_session.lock().unwrap().snapshot();
    let recovered = post.chain_snapshot(0, &recovered_meter).unwrap();
    assert_eq!(recovered.status, chain::Status::Active);
    assert!(recovered.points.last().unwrap().raw.endpoint > 16 * 4_800);
}

#[test]
fn au_clock_is_carried_but_cannot_reuse_vst3_host_evidence() {
    let directory = tempfile::tempdir().unwrap();
    let (pre_session, post_session, pre, post, identity) = active_pair(directory.path());
    for block in 0..16_i64 {
        let at = block * 4_800;
        advance_format_exact(
            &pre_session,
            at,
            at,
            4_096,
            0.25,
            AuxiliaryClockSource::AudioUnitRender,
            PresentationLatencySource::AudioUnitV2,
        );
        advance_format_exact(
            &post_session,
            at,
            at,
            0,
            0.5,
            AuxiliaryClockSource::AudioUnitRender,
            PresentationLatencySource::AudioUnitV2,
        );
        pre.service_pre_endpoint("pre", "song", "owner", directory.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json("pre".into(), &identity));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Unavailable);
    assert!(chain.points.is_empty());
}

#[test]
fn matching_content_position_cannot_certify_pdc_or_the_same_loop_occurrence() {
    let dir = tempfile::tempdir().unwrap();
    let pre_session = Arc::new(Mutex::new(
        MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 11).unwrap(),
    ));
    let post_session = Arc::new(Mutex::new(
        MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 22).unwrap(),
    ));
    let pre = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&pre_session));
    let post = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&post_session));
    let identity_path = dir.path().join("pre.json");
    fs::write(&identity_path, br#"{"instance_id":"pre","watch_owner_id":"owner","daw_session_id":"song","signal_state":"active"}"#).unwrap();
    for start in [0, 48_000] {
        advance(&pre_session, start, 0.25);
        advance(&post_session, start, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", dir.path())
            .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json(
            "pre".into(),
            &identity_path,
        ));
    }
    let legacy = post.recent(MeterHistoryResolution::Hz10, 64);
    assert_eq!(legacy.len(), 20);
    assert!((legacy.last().unwrap().lufs_m.mean.unwrap() - 6.020_599_913).abs() < 0.01);
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Unavailable);
    assert!(chain.points.is_empty()); // No fabricated exact chain proof from legacy TIME success.
    for _ in 0..1_000 {
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json(
            "pre".into(),
            &identity_path,
        ));
        assert!(post.chain_snapshot(chain.revision, &meter).is_none());
    }
    let mut old_wire = read_publication(dir.path()).unwrap();
    old_wire.schema = METER_HISTORY_EXCHANGE_SCHEMA;
    let identity = read_pre_identity(&identity_path).unwrap();
    assert!(old_wire.valid_for(&identity, 48_000, &pre.layout));
    old_wire.schema = 5;
    assert!(!old_wire.valid_for(&identity, 48_000, &pre.layout));
    old_wire.schema = 255;
    assert!(!old_wire.valid_for(&identity, 48_000, &pre.layout));

    // A newer/different peer claiming authority cannot certify the local POST clock.
    for start in [96_000, 144_000] {
        advance(&pre_session, start, 0.25);
        advance(&post_session, start, 0.5);
        pre.service_pre_endpoint("pre", "song", "owner", dir.path())
            .unwrap();
        let mut claimed = read_publication(dir.path()).unwrap();
        claimed.clock_policy = CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3;
        fs::write(
            dir.path().join(METER_HISTORY_EXCHANGE_FILE),
            serde_json::to_vec(&claimed).unwrap(),
        )
        .unwrap();
        post.service_post_endpoint(MeterHistoryTarget::from_pre_json(
            "pre".into(),
            &identity_path,
        ));
    }
    let meter = post_session.lock().unwrap().snapshot();
    let chain = post.chain_snapshot(0, &meter).unwrap();
    assert_eq!(chain.status, chain::Status::Unavailable);
    assert!(chain.points.is_empty());
}

#[test]
fn reader_rejects_oversized_and_malformed_exchange_without_partial_facts() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join(METER_HISTORY_EXCHANGE_FILE);
    fs::write(&path, vec![b' '; MAX_EXCHANGE_BYTES as usize + 1]).unwrap();
    assert!(read_publication(dir.path()).is_err());
    fs::write(&path, b"{\"schema\":4,").unwrap();
    assert!(read_publication(dir.path()).is_err());
    fs::remove_file(path).unwrap();
    assert!(read_publication(dir.path()).is_err());
}
