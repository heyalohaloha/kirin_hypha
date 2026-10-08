use super::*;
use kirin_measure::channel_layout::ChannelLayout;
#[test]
fn navigation_unknown_version_short_storage_and_role_preserve_output() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    let request = KirinAttackNavigationRequestV2 {
        version: 2,
        struct_size: std::mem::size_of::<KirinAttackNavigationRequestV2>() as u32,
        ..Default::default()
    };
    let mut bytes = vec![0xa5_u8; std::mem::size_of::<KirinAttackNavigationV2>() + 16];
    let offset = bytes
        .as_ptr()
        .align_offset(std::mem::align_of::<KirinAttackNavigationV2>());
    let output = unsafe { bytes.as_mut_ptr().add(offset).cast() };
    let before = bytes.clone();
    let future = KirinAttackNavigationRequestV2 {
        version: 3,
        ..request
    };
    assert_eq!(
        unsafe {
            kirin_hypha_poll_attack_navigation_v2(
                &engine,
                4,
                &future,
                std::mem::size_of::<KirinAttackNavigationV2>() as u32,
                output,
            )
        },
        KIRIN_SNAPSHOT_UNSUPPORTED
    );
    for (n, size, expected) in [
        (4, 1, KIRIN_SNAPSHOT_INVALID_REQUEST),
        (16, 1, KIRIN_SNAPSHOT_INVALID_REQUEST),
        (
            16,
            std::mem::size_of::<KirinAttackNavigationV2>() as u32,
            KIRIN_SNAPSHOT_UNSUPPORTED,
        ),
    ] {
        assert_eq!(
            unsafe { kirin_hypha_poll_attack_navigation_v2(&engine, n, &request, size, output) },
            expected
        );
        assert_eq!(before, bytes);
    }
    assert_eq!(before, bytes);
}
#[test]
fn navigation_layout_matches_native_consumer() {
    use std::mem::{offset_of, size_of};
    assert_eq!(size_of::<KirinAttackNavigationRequestV2>(), 16);
    assert_eq!(size_of::<KirinAttackNavigationV2>(), 67_600);
    assert_eq!(offset_of!(KirinAttackNavigationV2, events), 144);
    assert_eq!(offset_of!(KirinAttackNavigationV2, post), 19_584);
    assert_eq!(offset_of!(KirinAttackNavigationV2, pre), 43_592);
}

use crate::pair_binding::{ExactPairBindingSnapshot, PairObservationAuthority};
use kirin_measure::attack_runtime::snapshot::{AttackObservationSnapshot, AttackSourceEvidence};
use kirin_measure::spectrum_exchange::{AttackContentEvent, AttackPairAuthority};
use kirin_measure::{AttackPairEvent, AttackPairEventKind};
use std::sync::Arc;

fn evidence(incarnation: u8) -> AttackSourceEvidence {
    AttackSourceEvidence {
        source: AttackSourceKey {
            incarnation: [incarnation; 16],
            generation: 7,
            sample_rate: 48_000,
            channels: 2,
            odf_hash: [9; 32],
        },
        odf_support_start: 0,
        odf_support_end: 350_000,
        pcm_start: 0,
        pcm_end: 350_000,
        cutoff: 350_000,
        band_semantic_hash: kirin_measure::attack_runtime::semantics::band_semantic_hash(),
        clock_policy: 1,
    }
}

fn paired_fixture() -> (
    AttackSnapshotAuthority,
    AttackObservationSnapshot,
    AttackObservationView,
) {
    let origin = AttackPairAuthority {
        generation: 3,
        project_hash: "fixture-project".into(),
        pre_instance_id: "fixture-pre".into(),
        post_instance_id: "fixture-post".into(),
        owner_id: "fixture-owner".into(),
        claimed_at_bits: 1.0_f64.to_bits(),
    };
    let authority = AttackSnapshotAuthority {
        pair: PairObservationAuthority {
            generation: 3,
            selection_intent: true,
            exact: Some(ExactPairBindingSnapshot {
                generation: 3,
                project_hash: origin.project_hash.clone(),
                pre_instance_id: origin.pre_instance_id.clone(),
            }),
        },
        role: Some(PluginDataRole::Post),
        post_id: origin.post_instance_id.clone(),
        project_hash: origin.project_hash.clone(),
        owner: origin.owner_id.clone(),
        claim: origin.claimed_at_bits,
        signal: 1,
    };
    let local = AttackObservationSnapshot {
        source: Some(evidence(2)),
        revision: 17,
        ..Default::default()
    };
    let pre = AttackObservationSnapshot {
        source: Some(evidence(1)),
        revision: 11,
        ..Default::default()
    };
    let proof = AttackMappingProof {
        authority_revision: 3,
        request_id: [4; 16],
        target_hash: [5; 32],
        pre: evidence(1),
        post: evidence(2),
        mapping_epoch: 6,
        support_start: 0,
        support_end: 350_000,
        band_semantic_hash: evidence(2).band_semantic_hash,
    };
    let mut view = AttackObservationView {
        status: SpectrumViewStatus::Active,
        authority_revision: 3,
        target_hash: proof.target_hash,
        proof: Some(proof),
        origin: Some(origin),
        pre: Some(Arc::new(pre)),
        post: Some(Arc::new(local.clone())),
        mapping_request_id: proof.request_id,
        mapping_source_pair: Some((proof.pre.source, proof.post.source)),
        ..Default::default()
    };
    for (sample, token) in [(340_000, 29), (100_000, 7), (1_000, 3)] {
        let pair = AttackPairEvent {
            pair_generation: 3,
            pre_generation: 7,
            post_generation: 7,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: [9; 32],
            event_sample: sample,
            decision_sample: sample + 256,
            kind: AttackPairEventKind::Matched,
            pre_event_sample: Some(sample),
            post_event_sample: Some(sample + 64),
            pre_value: Some(1.0),
            post_value: Some(1.0),
            delta_value: Some(0.0),
        };
        assert!(pair.has_valid_layout());
        view.events.push(AttackContentEvent {
            source: proof.pre.source,
            event_sample: sample,
            token,
            pair,
        });
    }
    assert!(proof.pre.valid() && proof.post.valid() && authority.matches_view(&view));
    (authority, local, view)
}

#[test]
fn producer_ledger_keys_survive_numeric_unavailability_and_match_single_lookup_mode() {
    let (authority, local, mut view) = paired_fixture();
    let history = AttackHistory::default();
    let packet =
        assemble_navigation(&authority, &local, &history, &view, KIRIN_TARGET_DELTA).unwrap();
    assert_eq!(
        (
            packet.header.kind,
            packet.header.band,
            packet.header.target,
            packet.header.flags
        ),
        (4, 0, KIRIN_TARGET_DELTA, 1)
    );
    assert_eq!(packet.header.cutoff_sample, 350_000);
    assert_eq!(packet.header.source, source_key(evidence(2).source));
    assert_eq!(packet.count, 2); // First hit is outside the source-cutoff six-second cohort.
    assert_eq!(
        (packet.events[0].event_sample, packet.events[0].token),
        (100_000, 7)
    );
    assert_eq!(
        (packet.events[1].event_sample, packet.events[1].token),
        (340_000, 29)
    );
    assert_eq!(packet.events[0].source, source_key(evidence(1).source));
    view.status = SpectrumViewStatus::Unavailable;
    assert!(ledger_available(&authority, &view, evidence(2).source));
    let held =
        assemble_navigation(&authority, &local, &history, &view, KIRIN_TARGET_DELTA).unwrap();
    assert_eq!(
        (held.header.target, held.header.flags),
        (KIRIN_TARGET_POST, 0)
    );
    assert_eq!(&held.events[..2], &packet.events[..2]);
    view.proof = None;
    assert!(!ledger_available(&authority, &view, evidence(2).source));
    let own = assemble_navigation(&authority, &local, &history, &view, KIRIN_TARGET_DELTA).unwrap();
    assert_eq!(
        (own.header.target, own.header.flags, own.count),
        (KIRIN_TARGET_POST, 0, 0)
    );
}

#[test]
fn waveform_comparison_uses_common_contiguous_support_and_both_pcm_ranges() {
    let (_, _, view) = paired_fixture();
    let mut proof = view.proof.unwrap();
    proof.support_start = 300_000;
    proof.support_end = 340_000;
    proof.pre.pcm_start = 302_000;
    let spans = [
        (295_200, 300_000),
        (300_000, 304_800),
        (304_800, 309_600),
        (332_800, 337_600),
        (337_600, 342_400),
        (347_200, 352_000),
    ];
    let mut points: Vec<_> = spans
        .into_iter()
        .map(|(start_sample, end_sample)| AttackWaveformPoint {
            generation: 7,
            sample_rate: 48_000,
            channels: 2,
            start_sample,
            end_sample,
            peak_linear: 0.5,
            rms_dbfs: -12.0,
        })
        .collect();
    points.push(AttackWaveformPoint {
        generation: 8,
        ..points[2]
    });
    let absolute = qualified_waveform(points.iter(), proof.post.source, 350_000, None);
    assert_eq!(absolute.count, 5); // Absolute POST is independent of comparison support.
    let comparison = qualified_waveform(points.iter(), proof.pre.source, 350_000, Some(&proof));
    assert_eq!(comparison.count, 2);
    assert_eq!(
        (
            comparison.points[0].start_sample,
            comparison.points[1].end_sample
        ),
        (304_800, 337_600)
    );
    proof.post.clock_policy = 2;
    assert_eq!(
        qualified_waveform(points.iter(), proof.pre.source, 350_000, Some(&proof)).count,
        0
    );
}

#[test]
fn final_binding_rejects_proof_loss_replacement_and_stale_observation_sources() {
    let (_, _, view) = paired_fixture();
    let mut progress = view.clone();
    progress.proof.as_mut().unwrap().support_end += 256;
    progress.proof.as_mut().unwrap().post.cutoff += 256;
    assert!(same_binding(&view, &progress)); // Normal same-binding publication progress.
    let changes: [fn(&mut AttackObservationView); 7] = [
        |v| v.proof = None,
        |v| v.status = SpectrumViewStatus::Unavailable,
        |v| v.target_hash[0] ^= 1,
        |v| v.proof.as_mut().unwrap().mapping_epoch += 1,
        |v| v.proof.as_mut().unwrap().request_id[0] ^= 1,
        |v| v.origin.as_mut().unwrap().owner_id.push('x'),
        |v| {
            Arc::make_mut(v.pre.as_mut().unwrap())
                .source
                .as_mut()
                .unwrap()
                .source
                .generation += 1
        },
    ];
    for change in changes {
        let mut after = view.clone();
        change(&mut after);
        assert!(!same_binding(&view, &after));
    }
    let (authority, local, mut foreign) = paired_fixture();
    foreign.events[0].source.incarnation = [99; 16];
    assert!(matches!(
        assemble_navigation(
            &authority,
            &local,
            &AttackHistory::default(),
            &foreign,
            KIRIN_TARGET_DELTA
        ),
        Err(KIRIN_SNAPSHOT_BUSY)
    ));
}

#[test]
fn actual_navigation_returns_retired_for_source_loss_and_preserves_band_transition() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    let runtime = engine.attack_runtime.as_ref().unwrap();
    let (_, local, _) = paired_fixture();
    runtime.fixture_publish_observation(local);
    let request = KirinAttackNavigationRequestV2 {
        version: 2,
        struct_size: std::mem::size_of::<KirinAttackNavigationRequestV2>() as u32,
        target: KIRIN_TARGET_DELTA,
        ..Default::default()
    };
    runtime.set_band(kirin_measure::attack_perception::band::AttackBand::from_index(3));
    assert!(matches!(
        runtime.try_observation_snapshot_result(),
        Err(AttackObservationReadError::SourceUnavailable)
    ));
    let packet = engine.attack_navigation_v2(request).unwrap();
    assert_eq!(
        (packet.header.band, packet.header.target),
        (0, KIRIN_TARGET_POST)
    );
    runtime.fixture_with_observation_lock(|| {
        assert!(matches!(
            engine.attack_navigation_v2(request),
            Err(KIRIN_SNAPSHOT_BUSY)
        ));
    });
    runtime.note_clock_policy_from_audio(1, true);
    let mut output = packet;
    assert_eq!(
        unsafe {
            kirin_hypha_poll_attack_navigation_v2(
                &engine,
                16,
                &request,
                std::mem::size_of_val(&output) as u32,
                &mut output,
            )
        },
        KIRIN_SNAPSHOT_RETIRED
    );
    assert_eq!(output.header.source, packet.header.source);
    assert_eq!(
        output.header.snapshot_revision,
        packet.header.snapshot_revision
    );
    assert!(runtime.set_enabled(false));
    assert!(matches!(
        engine.attack_navigation_v2(request),
        Err(KIRIN_SNAPSHOT_RETIRED)
    ));
}
