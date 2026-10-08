use super::*;
use crate::KirinHyphaEngine;

fn valid_request() -> KirinAttackSingleV2Request {
    KirinAttackSingleV2Request {
        version: 2,
        struct_size: 96,
        target: KIRIN_TARGET_POST,
        band: 1,
        event: KirinSnapshotEventKey {
            source: KirinSnapshotSourceKey {
                incarnation: [1; 16],
                generation: 1,
                sample_rate: 48_000,
                channels: 2,
                reserved: [0; 3],
                odf_hash: [2; 32],
            },
            event_sample: 20_000,
            token: 20_000,
        },
        ..Default::default()
    }
}

#[test]
fn invalid_single_calls_preserve_output_and_request_token_bytes() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    let mut token = 0xa5a5a5a5a5a5a5a5_u64;
    for (request, expected) in [
        (
            KirinAttackSingleV2Request {
                version: 99,
                ..valid_request()
            },
            KIRIN_SNAPSHOT_UNSUPPORTED,
        ),
        (
            KirinAttackSingleV2Request {
                target: 99,
                ..valid_request()
            },
            KIRIN_SNAPSHOT_INVALID_REQUEST,
        ),
        (
            KirinAttackSingleV2Request {
                band: 9,
                ..valid_request()
            },
            KIRIN_SNAPSHOT_INVALID_REQUEST,
        ),
        (
            KirinAttackSingleV2Request {
                struct_size: 95,
                ..valid_request()
            },
            KIRIN_SNAPSHOT_INVALID_REQUEST,
        ),
    ] {
        assert_eq!(
            unsafe { kirin_hypha_request_attack_single_v2(&engine, 96, &request, &mut token) },
            expected
        );
        assert_eq!(token, 0xa5a5a5a5a5a5a5a5);
    }
    let mut bytes = vec![0xa5_u64; 4560 / 8 + 2];
    let before = bytes.clone();
    let out = bytes.as_mut_ptr().cast::<KirinAttackSingleSnapshotV2>();
    for (handle, token, capacity) in [
        (std::ptr::null(), 1, 4560),
        (&engine as *const _, 0, 4560),
        (&engine as *const _, 1, 4559),
    ] {
        assert_eq!(
            unsafe { kirin_hypha_poll_attack_single_v2(handle, token, capacity, out) },
            KIRIN_SNAPSHOT_INVALID_REQUEST
        );
        assert_eq!(bytes, before);
    }
    assert_eq!(
        unsafe { kirin_hypha_cancel_attack_single_v2(std::ptr::null(), 1) },
        KIRIN_SNAPSHOT_INVALID_REQUEST
    );
    assert_eq!(
        unsafe { kirin_hypha_cancel_attack_single_v2(&engine, 0) },
        KIRIN_SNAPSHOT_INVALID_REQUEST
    );
}

#[test]
fn single_layout_is_independently_pinned_to_c_contract() {
    use std::mem::{offset_of, size_of};
    assert_eq!(size_of::<KirinAttackSingleV2Request>(), 96);
    assert_eq!(size_of::<KirinAttackSingleSnapshotV2>(), 4560);
    assert_eq!(offset_of!(KirinAttackSingleSnapshotV2, event), 136);
    assert_eq!(offset_of!(KirinAttackSingleSnapshotV2, request_token), 216);
    assert_eq!(offset_of!(KirinAttackSingleSnapshotV2, lanes), 240);
    assert_eq!(offset_of!(KirinAttackSingleSnapshotV2, pre), 656);
    assert_eq!(offset_of!(KirinAttackSingleSnapshotV2, all_pre), 3536);
    assert_eq!(offset_of!(KirinAttackSingleSnapshotV2, all_post), 4048);
}

#[test]
fn paired_all_poll_locator_rejects_head_only_or_another_source_and_mapping() {
    use kirin_measure::attack_runtime::single::AttackSingleRequest;
    use kirin_measure::attack_runtime::snapshot::{AttackSourceEvidence, AttackSourceKey};
    use kirin_measure::spectrum_exchange::AttackMappingProof;
    use kirin_measure::{
        AttackDetailedEvent, AttackEvent, AttackEventShape, AttackPerceptualFeatures,
    };
    let pre = AttackSourceKey {
        incarnation: [1; 16],
        generation: 19,
        sample_rate: 48_000,
        channels: 1,
        odf_hash: [2; 32],
    };
    let post = AttackSourceKey {
        incarnation: [3; 16],
        generation: 7,
        ..pre
    };
    let evidence = |source| AttackSourceEvidence {
        source,
        odf_support_start: 0,
        odf_support_end: 48_000,
        pcm_start: 0,
        pcm_end: 48_000,
        cutoff: 48_000,
        band_semantic_hash: [4; 32],
        clock_policy: 1,
    };
    let proof = AttackMappingProof {
        authority_revision: 1,
        request_id: [5; 16],
        target_hash: [6; 32],
        pre: evidence(pre),
        post: evidence(post),
        mapping_epoch: 19,
        support_start: 0,
        support_end: 48_000,
        band_semantic_hash: [4; 32],
    };
    let event = AttackEvent {
        generation: 7,
        sample_rate: 48_000,
        channels: 1,
        definition_hash: [2; 32],
        event_sample: 0,
        decision_sample: 2,
        value: 1.0,
    };
    let request = AttackSingleRequest {
        key_source: pre,
        key_event_sample: 0,
        key_token: 1,
        local_source: post,
        pre_source: Some(pre),
        event,
        band: None,
        requested_end: 6_240,
        measurement_end: 6_240,
        span_end: kirin_measure::attack_perception::band::BandSpanEnd::Window,
        pre: None,
        pre_detail: None,
        pair_kind: 1,
        target: 1,
        pair_authority_revision: 1,
        proof_token: proof.binding_token(),
    };
    let full = AttackDetailedEvent {
        event: AttackEvent {
            generation: 19,
            ..event
        },
        features: AttackPerceptualFeatures {
            sample_rate: 48_000,
            channels: 1,
            bin_frames: 48,
            window_start_sample: 0,
            attack_rms_dbfs: -18.0,
            sample_peak_dbfs: -6.0,
            crest_db: 12.0,
            complete: true,
            body_end_sample: 6_240,
            body_rms_dbfs: Some(-24.0),
            transient_db: Some(6.0),
            sharpness_acum: Some(1.25),
        },
        shape: AttackEventShape {
            start_sample: 0,
            end_sample: 6_240,
            event_sample: 0,
            points: [0.5; kirin_measure::ATTACK_SHAPE_POINT_CAPACITY],
        },
    };
    assert!(full.has_valid_layout());
    assert_eq!(
        request::pre_completion(&request, Some(proof), Some(full)),
        Some((pre, full))
    );
    let mut head = full;
    head.features.complete = false;
    assert_eq!(
        request::pre_completion(&request, Some(proof), Some(head)),
        None
    );
    let mut other = full;
    other.event.generation += 1;
    assert_eq!(
        request::pre_completion(&request, Some(proof), Some(other)),
        None
    );
    other = full;
    other.event.event_sample += 48;
    assert_eq!(
        request::pre_completion(&request, Some(proof), Some(other)),
        None
    );
    assert_eq!(
        request::pre_completion(
            &request,
            Some(AttackMappingProof {
                request_id: [7; 16],
                ..proof
            }),
            Some(full)
        ),
        None
    );
}

#[test]
fn future_single_version_precedes_size_checks_and_preserves_token() {
    #[repr(C, align(8))]
    struct Prefix {
        version: u32,
        struct_size: u32,
    }
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    let prefix = Prefix {
        version: 3,
        struct_size: 128,
    };
    let request = (&prefix as *const Prefix).cast::<KirinAttackSingleV2Request>();
    let mut token = 0xa5a5a5a5a5a5a5a5;
    // Only the version prefix is present: unknown layouts must never read a V2 body.
    for size in [4, 8, 95, 96, 128] {
        assert_eq!(
            unsafe { kirin_hypha_request_attack_single_v2(&engine, size, request, &mut token) },
            KIRIN_SNAPSHOT_UNSUPPORTED
        );
        assert_eq!(token, 0xa5a5a5a5a5a5a5a5);
    }
    for size in [0, 3] {
        assert_eq!(
            unsafe { kirin_hypha_request_attack_single_v2(&engine, size, request, &mut token) },
            KIRIN_SNAPSHOT_INVALID_REQUEST
        );
        assert_eq!(token, 0xa5a5a5a5a5a5a5a5);
    }
    for size in [4, 8, 95, 128] {
        assert_eq!(
            unsafe {
                kirin_hypha_request_attack_single_v2(&engine, size, &valid_request(), &mut token)
            },
            KIRIN_SNAPSHOT_INVALID_REQUEST
        );
        assert_eq!(token, 0xa5a5a5a5a5a5a5a5);
    }
}

pub(super) fn selected_fixture() -> (
    KirinHyphaEngine,
    kirin_measure::attack_runtime::snapshot::AttackObservationSnapshot,
) {
    use kirin_measure::attack_runtime::single::{
        AttackSingleReason, AttackSingleRequest, AttackSingleSnapshot,
    };
    use kirin_measure::attack_runtime::snapshot::{
        AttackFinish, AttackObservationSnapshot, AttackSourceEvidence, AttackSourceKey,
    };
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *engine.write_role.lock().unwrap() = Some(kirin_measure::PluginDataRole::Post);
    let source = AttackSourceKey {
        incarnation: [1; 16],
        generation: 1,
        sample_rate: 48_000,
        channels: 2,
        odf_hash: [2; 32],
    };
    let observation = AttackObservationSnapshot {
        source: Some(AttackSourceEvidence {
            source,
            odf_support_start: 0,
            odf_support_end: 48_000,
            pcm_start: 0,
            pcm_end: 48_000,
            cutoff: 48_000,
            band_semantic_hash: [4; 32],
            clock_policy: 1,
        }),
        revision: 7,
        ..Default::default()
    };
    let event = kirin_measure::AttackEvent {
        generation: 1,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: [2; 32],
        event_sample: 0,
        decision_sample: 2,
        value: 1.0,
    };
    let detail = kirin_measure::AttackDetailedEvent {
        event,
        features: kirin_measure::AttackPerceptualFeatures {
            sample_rate: 48_000,
            channels: 2,
            bin_frames: 48,
            window_start_sample: 0,
            attack_rms_dbfs: -18.0,
            sample_peak_dbfs: -6.0,
            crest_db: 12.0,
            complete: true,
            body_end_sample: 6_240,
            body_rms_dbfs: Some(-24.0),
            transient_db: Some(6.0),
            sharpness_acum: Some(1.25),
        },
        shape: kirin_measure::AttackEventShape {
            start_sample: 0,
            end_sample: 6_240,
            event_sample: 0,
            points: [0.5; kirin_measure::ATTACK_SHAPE_POINT_CAPACITY],
        },
    };
    assert!(detail.has_valid_layout());
    let runtime = engine.attack_runtime.as_ref().unwrap();
    runtime.fixture_publish_observation(observation.clone());
    runtime.fixture_select_single(AttackSingleSnapshot {
        request: AttackSingleRequest {
            key_source: source,
            key_event_sample: 0,
            key_token: 1,
            local_source: source,
            pre_source: None,
            event,
            band: None,
            requested_end: 6_240,
            measurement_end: 6_240,
            span_end: kirin_measure::attack_perception::band::BandSpanEnd::Window,
            pre: None,
            pre_detail: None,
            pair_kind: 4,
            target: KIRIN_TARGET_POST,
            pair_authority_revision: 1,
            proof_token: [0; 32],
        },
        token: 31,
        revision: 9,
        finish: AttackFinish::Full,
        reason: AttackSingleReason::None,
        band_observation: None,
        detail: Some(detail),
    });
    (engine, observation)
}

#[test]
fn contended_single_and_observation_reads_preserve_bytes_and_retry_same_selection() {
    let (engine, _) = selected_fixture();
    let runtime = engine.attack_runtime.as_ref().unwrap();
    let mut storage = vec![0xa5a5a5a5a5a5a5a5_u64; 4560 / 8 + 2];
    let before = storage.clone();
    let out = storage.as_mut_ptr().cast::<KirinAttackSingleSnapshotV2>();
    let expected = engine.attack_single_v2(31).unwrap();
    assert_eq!(
        (
            expected.request_token,
            expected.finish,
            expected.has_all_post
        ),
        (31, KIRIN_FINISH_FULL, 1)
    );
    for lock in [0, 1] {
        let poll = || unsafe { kirin_hypha_poll_attack_single_v2(&engine, 31, 4560, out) };
        let status = if lock == 0 {
            runtime.fixture_with_single_lock(|| {
                assert_eq!(
                    unsafe { kirin_hypha_cancel_attack_single_v2(&engine, 31) },
                    KIRIN_SNAPSHOT_BUSY
                );
                poll()
            })
        } else {
            runtime.fixture_with_observation_lock(poll)
        };
        assert_eq!(status, KIRIN_SNAPSHOT_BUSY);
        assert_eq!(storage, before);
        assert_eq!(engine.attack_single_v2(31).unwrap(), expected);
    }
    assert_eq!(
        unsafe { kirin_hypha_poll_attack_single_v2(&engine, 31, 4560, out) },
        KIRIN_SNAPSHOT_SUCCESS
    );
    assert_eq!(unsafe { out.read() }, expected);
    assert_eq!(&storage[4560 / 8..], &before[4560 / 8..]);
}

#[test]
fn actual_source_change_or_missing_source_retires_single_and_missing_token_is_retired() {
    for missing in [false, true] {
        let (engine, mut observation) = selected_fixture();
        let runtime = engine.attack_runtime.as_ref().unwrap();
        if missing {
            observation.source = None;
        } else {
            observation.source.as_mut().unwrap().source.generation += 1;
        }
        runtime.fixture_publish_observation(observation);
        let snapshot = engine.attack_single_v2(31).unwrap();
        assert_eq!(
            (snapshot.finish, snapshot.reason),
            (KIRIN_FINISH_RETIRED, KIRIN_REASON_SOURCE_CHANGED)
        );
        assert_eq!(snapshot.has_all_post, 0);
        assert_eq!(engine.attack_single_v2(32), Err(KIRIN_SNAPSHOT_RETIRED));
        let mut storage = vec![0xa5a5a5a5a5a5a5a5_u64; 4560 / 8 + 2];
        let before = storage.clone();
        assert_eq!(
            unsafe {
                kirin_hypha_poll_attack_single_v2(
                    &engine,
                    32,
                    4560,
                    storage.as_mut_ptr().cast::<KirinAttackSingleSnapshotV2>(),
                )
            },
            KIRIN_SNAPSHOT_RETIRED
        );
        assert_eq!(storage, before);
    }
}
