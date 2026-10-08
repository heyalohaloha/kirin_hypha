use super::*;
use crate::attack_runtime::semantics::band_semantic_hash;
use crate::attack_runtime::snapshot::{
    AttackObservationSnapshot, AttackSourceEvidence, AttackSourceKey,
};

fn fixture() -> (
    tempfile::TempDir,
    Arc<SpectrumCoordinator>,
    Arc<AttackRuntime>,
    Uuid,
    AttackObservationSnapshot,
) {
    let temp = tempfile::tempdir().unwrap();
    let request_id = Uuid::new_v4();
    write_request(
        temp.path(),
        &AnalysisRequest {
            schema: REQUEST_SCHEMA.into(),
            request_id: request_id.to_string(),
            requested_by_post_instance_id: "post-fixture".into(),
            target_pre_instance_id: "pre-fixture".into(),
            sample_rate: 48_000,
            analysis_mode: AnalysisViewMode::Attack as u8,
            channel_mode: SpectrumChannelMode::Lr as u8,
            state_epoch_samples: None,
            expires_at_unix_ms: unix_ms_now() + 60_000,
            attack_band: None,
        },
    )
    .unwrap();
    let spectrum = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let attack = AttackRuntime::new(48_000, 2).unwrap();
    let coordinator =
        SpectrumCoordinator::new_with_attack(48_000, spectrum, Some(Arc::clone(&attack)));
    *coordinator.pre_session.lock().unwrap() = Some(PreSession {
        request_id,
        last_written_end: None,
        last_write_attempt_end: None,
        last_write_attempt_at: None,
        last_ready_written_at: None,
        instance_dir: temp.path().to_path_buf(),
        state_epoch_samples: None,
    });
    let source = AttackSourceKey {
        incarnation: [4; 16],
        generation: 7,
        sample_rate: 48_000,
        channels: 2,
        odf_hash: [9; 32],
    };
    let mut history = AttackHistory::default();
    history.push(crate::AttackOdfFrame {
        generation: source.generation,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: source.odf_hash,
        window_samples: 2048,
        hop_samples: 256,
        support_start_samples: 0,
        support_end_samples: 2048,
        event_sample: 1024,
        value: 0.0,
    });
    history.push_waveform(crate::AttackWaveformPoint {
        generation: source.generation,
        sample_rate: 48_000,
        channels: 2,
        start_sample: 0,
        end_sample: 4800,
        peak_linear: 0.0,
        rms_dbfs: -120.0,
    });
    assert_eq!(history.frames().len(), 1);
    assert_eq!(history.waveform().len(), 1);
    attack.fixture_publish_history(history);
    let observation = AttackObservationSnapshot {
        source: Some(AttackSourceEvidence {
            source,
            odf_support_start: 0,
            odf_support_end: 2048,
            pcm_start: 0,
            pcm_end: 4800,
            cutoff: 4800,
            band_semantic_hash: band_semantic_hash(),
            clock_policy: 1,
        }),
        revision: 11,
        ..Default::default()
    };
    attack.fixture_publish_observation(observation.clone());
    (temp, coordinator, attack, request_id, observation)
}

#[test]
fn observation_contention_keeps_pre_publication_and_allows_later_revision() {
    let (temp, coordinator, attack, request_id, mut observation) = fixture();
    let tick = || {
        coordinator.publish_pre_snapshot(
            request_id,
            "pre-fixture",
            AnalysisViewMode::Attack,
            temp.path(),
        )
    };
    assert!(tick());
    let published = read_attack_snapshot(temp.path()).expect("initial PRE publication");
    assert_eq!(published.observations.unwrap().revision, 11);
    let written = coordinator
        .pre_session
        .lock()
        .unwrap()
        .as_ref()
        .unwrap()
        .last_written_end;
    // Hold the actual publication mutex. No scheduling or sleep chooses the contention edge.
    attack.fixture_with_observation_lock(|| {
        assert!(tick());
        let retained = read_attack_snapshot(temp.path()).expect("busy read keeps PRE publication");
        assert_eq!(retained.request_id, request_id);
        assert_eq!(retained.observations.unwrap(), observation);
        assert_eq!(
            coordinator
                .pre_session
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .last_written_end,
            written
        );
    });
    observation.revision = 12;
    attack.fixture_publish_observation(observation.clone());
    assert!(tick());
    let next = read_attack_snapshot(temp.path()).expect("retry publishes next PRE revision");
    assert_eq!(next.request_id, request_id);
    assert_eq!(next.observations.unwrap(), observation);
    assert_ne!(
        coordinator
            .pre_session
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .last_written_end,
        written
    );
    coordinator.shutdown();
}

#[test]
fn actual_source_loss_invalid_evidence_or_end_removes_pre_publication() {
    for unavailable in [0, 1, 2] {
        let (temp, coordinator, attack, request_id, mut observation) = fixture();
        let tick = || {
            coordinator.publish_pre_snapshot(
                request_id,
                "pre-fixture",
                AnalysisViewMode::Attack,
                temp.path(),
            )
        };
        assert!(tick());
        assert!(read_attack_snapshot(temp.path()).is_some());
        if unavailable == 2 {
            assert!(attack.set_enabled(false));
        } else {
            if unavailable == 0 {
                observation.source = None;
            } else {
                observation.source.as_mut().unwrap().pcm_end = 0;
            }
            attack.fixture_publish_observation(observation);
        }
        assert!(tick());
        assert!(read_attack_snapshot(temp.path()).is_none());
        coordinator.shutdown();
    }
}

#[test]
fn same_revision_is_republished_after_temporary_source_invalidity() {
    let (temp, coordinator, attack, request_id, observation) = fixture();
    let tick = || {
        coordinator.publish_pre_snapshot(
            request_id,
            "pre-fixture",
            AnalysisViewMode::Attack,
            temp.path(),
        )
    };
    assert!(tick());
    let written = coordinator
        .pre_session
        .lock()
        .unwrap()
        .as_ref()
        .unwrap()
        .last_written_end;
    // Band B invalidates the previously published A facts before the worker produces anything.
    attack.set_band(AttackBand::from_index(3));
    assert!(tick());
    assert!(read_attack_snapshot(temp.path()).is_none());
    {
        let cleared = coordinator.pre_session.lock().unwrap();
        let current = cleared.as_ref().unwrap();
        assert_eq!(current.last_written_end, None);
        assert_eq!(current.last_write_attempt_end, None);
    }
    // Returning to A without a worker update restores precisely the old observation revision.
    attack.set_band(None);
    assert!(tick());
    assert_eq!(
        read_attack_snapshot(temp.path())
            .unwrap()
            .observations
            .unwrap(),
        observation
    );
    assert_eq!(
        coordinator
            .pre_session
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .last_written_end,
        written
    );
    coordinator.shutdown();
}

#[test]
fn unavailable_source_does_not_remove_another_requests_publication() {
    let (temp, coordinator, attack, request_id, observation) = fixture();
    let other_request = Uuid::new_v4();
    let bytes = encode_attack_observation_snapshot(
        other_request,
        &attack.try_history().unwrap(),
        &observation,
    );
    write_attack_snapshot(temp.path(), &bytes).unwrap();
    attack.set_band(AttackBand::from_index(3));
    assert!(coordinator.publish_pre_snapshot(
        request_id,
        "pre-fixture",
        AnalysisViewMode::Attack,
        temp.path()
    ));
    assert_eq!(
        read_attack_snapshot(temp.path()).unwrap().request_id,
        other_request
    );
    coordinator
        .pre_session
        .lock()
        .unwrap()
        .as_mut()
        .unwrap()
        .request_id = other_request;
    assert!(!coordinator.publish_pre_snapshot(
        request_id,
        "pre-fixture",
        AnalysisViewMode::Attack,
        temp.path()
    ));
    assert_eq!(
        read_attack_snapshot(temp.path()).unwrap().request_id,
        other_request
    );
    coordinator.shutdown();
}
