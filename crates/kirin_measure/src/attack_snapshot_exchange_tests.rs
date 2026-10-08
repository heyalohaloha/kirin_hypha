use super::*;
use crate::{AttackOdfFrame, SpectrumRuntime};

fn source(incarnation: u8) -> AttackSourceEvidence {
    AttackSourceEvidence {
        source: AttackSourceKey {
            incarnation: [incarnation; 16],
            generation: 7,
            sample_rate: 48_000,
            channels: 2,
            odf_hash: [9; 32],
        },
        odf_support_start: 0,
        odf_support_end: 35_000,
        pcm_start: 0,
        pcm_end: 35_000,
        cutoff: 35_000,
        band_semantic_hash: band_semantic_hash(),
        clock_policy: 1,
    }
}

fn proof() -> AttackMappingProof {
    AttackMappingProof {
        authority_revision: 1,
        request_id: [2; 16],
        target_hash: [3; 32],
        pre: source(1),
        post: source(2),
        mapping_epoch: 4,
        support_start: 0,
        support_end: 35_000,
        band_semantic_hash: band_semantic_hash(),
    }
}

fn history(generation: u64, skipped: Option<i64>) -> AttackHistory {
    let mut history = AttackHistory::with_capacity();
    for index in 0..128_i64 {
        if skipped == Some(index) {
            continue;
        }
        let at = 1024 + index * 256;
        history.push(AttackOdfFrame {
            generation,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: [9; 32],
            window_samples: 2048,
            hop_samples: 256,
            support_start_samples: at - 1024,
            support_end_samples: at + 1024,
            event_sample: at,
            value: 0.0,
        });
    }
    history
}

#[test]
fn proof_needs_full_filter_preroll_and_rms_tail_not_just_arrival_clock() {
    let band = AttackBand::from_index(5).unwrap();
    let value = proof();
    assert!(value.validates_span(band, 10_000, 24_400, value.pre.source, value.post.source));
    // 1kHz: HEAD lead960 + four periods192 + half RMS25; end needs25 extra frames.
    let mut too_short = value;
    too_short.post.pcm_start = 8824;
    assert!(!too_short.validates_span(band, 10_000, 24_400, value.pre.source, value.post.source));
    too_short.post.pcm_start = 8823;
    too_short.post.pcm_end = 24424;
    assert!(!too_short.validates_span(band, 10_000, 24_400, value.pre.source, value.post.source));
    too_short.post.pcm_end = 24425;
    assert!(too_short.validates_span(band, 10_000, 24_400, value.pre.source, value.post.source));
    let mut bad = value;
    bad.post.clock_policy = 2;
    assert!(!bad.validates_span(band, 10_000, 24_400, value.pre.source, value.post.source));
    let mut bad = value;
    bad.pre.band_semantic_hash = [8; 32];
    assert!(!bad.validates_span(band, 10_000, 24_400, value.pre.source, value.post.source));
    let mut foreign = value.pre.source;
    foreign.incarnation = [8; 16];
    assert!(!value.validates_span(band, 10_000, 24_400, foreign, value.post.source));
}

#[test]
fn zero_detected_post_events_still_have_mapping_but_odf_gaps_limit_support() {
    let pre = history(7, None);
    let post = history(7, None);
    assert_eq!(post.events().len(), 0);
    assert_eq!(
        mapped_support(&pre, &post, source(1).source, source(2).source),
        Some((0, 34_560))
    );
    let gap = history(7, Some(64));
    // The last continuous region begins at frame65; never claim all old support across the gap.
    assert_eq!(
        mapped_support(&pre, &gap, source(1).source, source(2).source),
        Some((16_640, 34_560))
    );
    let mut shifted = AttackHistory::with_capacity();
    for mut frame in post.frames().copied() {
        frame.event_sample += 1;
        frame.support_start_samples += 1;
        frame.support_end_samples += 1;
        shifted.push(frame);
    }
    assert!(mapped_support(&pre, &shifted, source(1).source, source(2).source).is_none());
}

fn origin(generation: u64, pre: &str) -> AttackPairAuthority {
    AttackPairAuthority {
        generation,
        project_hash: "project".into(),
        pre_instance_id: pre.into(),
        post_instance_id: "post".into(),
        owner_id: "owner".into(),
        claimed_at_bits: 1.0_f64.to_bits(),
    }
}

#[test]
fn a_b_a_control_never_relabels_original_session_or_rolls_back_revision() {
    let coordinator = SpectrumCoordinator::new(
        48_000,
        SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo()),
    );
    coordinator.set_attack_pair_authority(1, Some(origin(1, "A")));
    let original = coordinator.new_post_session();
    coordinator.set_attack_pair_authority(2, Some(origin(2, "B")));
    coordinator.set_attack_pair_authority(3, Some(origin(3, "A")));
    coordinator.set_attack_pair_authority(1, Some(origin(1, "A")));
    let current = coordinator.new_post_session();
    assert_eq!(original.authority_revision, 1);
    assert_eq!(original.attack_origin, Some(origin(1, "A")));
    assert_eq!(current.authority_revision, 3);
    assert_eq!(current.attack_origin, Some(origin(3, "A")));
    assert_ne!(original.request_id, current.request_id);
    assert!(!coordinator.store_attack_observations(
        &original,
        &history(7, None),
        &history(7, None),
        Arc::new(AttackObservationSnapshot::default()),
        &[]
    ));
    let empty = coordinator.try_attack_observation_view().unwrap();
    assert_eq!(empty.authority_revision, 3);
    assert!(empty.proof.is_none());
}

#[test]
fn solo_empty_view_is_readable_and_busy_is_distinct_from_no_pair() {
    let coordinator = SpectrumCoordinator::new(
        48_000,
        SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo()),
    );
    let empty = coordinator.try_attack_observation_view().unwrap();
    assert_eq!(empty.authority_revision, 1);
    assert!(empty.proof.is_none());
    let _busy = coordinator.attack_observation_view.lock().unwrap();
    assert!(coordinator.try_attack_observation_view().is_none());
}

#[test]
fn proof_binding_identity_survives_normal_support_growth_but_fact_identity_changes() {
    let a = proof();
    let mut b = a;
    b.support_end += 256;
    assert_eq!(a.binding_token(), b.binding_token());
    assert_ne!(a.token(), b.token());
    b.post.source.generation += 1;
    assert_ne!(a.binding_token(), b.binding_token());
}

#[test]
fn actual_clear_keeps_only_same_source_ledger_and_retires_either_changed_source() {
    // All three admissions reuse the original UUID; source qualification must be independent.
    for changed_side in [None, Some(false), Some(true)] {
        let coordinator = SpectrumCoordinator::new(
            48_000,
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo()),
        );
        let original = proof();
        let event = AttackContentEvent {
            source: original.pre.source,
            event_sample: 10_000,
            token: 17,
            pair: AttackPairEvent {
                pair_generation: 4,
                pre_generation: 7,
                post_generation: 7,
                sample_rate: 48_000,
                channels: 2,
                definition_hash: [9; 32],
                event_sample: 10_000,
                decision_sample: 12_048,
                kind: AttackPairEventKind::Matched,
                pre_event_sample: Some(10_000),
                post_event_sample: Some(10_000),
                pre_value: Some(0.5),
                post_value: Some(0.25),
                delta_value: Some(-0.25),
            },
        };
        {
            let mut view = coordinator.attack_observation_view.lock().unwrap();
            view.qualify_content_ledger(&original);
            view.authority_revision = 1;
            view.status = SpectrumViewStatus::Active;
            view.proof = Some(original);
            view.pre = Some(Arc::new(AttackObservationSnapshot {
                source: Some(original.pre),
                revision: 41,
                ..Default::default()
            }));
            view.post = Some(Arc::new(AttackObservationSnapshot {
                source: Some(original.post),
                revision: 42,
                ..Default::default()
            }));
            view.pre_history = Some(Arc::new(history(7, None)));
            view.post_history = Some(Arc::new(history(7, None)));
            view.events = vec![event];
            view.aliases = vec![(11, 17)];
        }
        coordinator.clear_attack_observations();
        let mut view = coordinator.attack_observation_view.lock().unwrap();
        assert_eq!(view.status, SpectrumViewStatus::Unavailable);
        assert!(view.proof.is_none() && view.pre.is_none() && view.post.is_none());
        assert!(view.pre_history.is_none() && view.post_history.is_none());
        assert_eq!(view.mapping_request_id, [2; 16]);
        assert_eq!(
            view.mapping_source_pair,
            Some((original.pre.source, original.post.source))
        );
        assert_eq!(view.events, [event]);
        assert_eq!(view.aliases, [(11, 17)]);
        let mut next = original;
        match changed_side {
            Some(false) => next.pre.source.generation = 8,
            Some(true) => next.post.source.generation = 8,
            None => {}
        }
        view.qualify_content_ledger(&next);
        assert_eq!(view.mapping_request_id, [2; 16]);
        assert_eq!(
            view.mapping_source_pair,
            Some((next.pre.source, next.post.source))
        );
        assert!(view.proof.is_none() && view.pre.is_none() && view.post.is_none());
        assert!(view.pre_history.is_none() && view.post_history.is_none());
        if changed_side.is_none() {
            assert_eq!(view.events, [event]);
            assert_eq!(view.aliases, [(11, 17)]);
        } else {
            assert!(view.events.is_empty() && view.aliases.is_empty());
        }
    }
}
