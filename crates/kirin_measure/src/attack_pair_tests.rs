use super::*;

fn frame(index: i64, generation: u64, value: f32) -> AttackOdfFrame {
    AttackOdfFrame {
        generation,
        sample_rate: 1_000,
        channels: 2,
        definition_hash: [11; 32],
        window_samples: 20,
        hop_samples: 10,
        support_start_samples: index * 10 - 10,
        support_end_samples: index * 10 + 10,
        event_sample: index * 10,
        value,
    }
}

fn collect(
    joiner: &mut AttackPairJoiner,
    pre_values: &[f32],
    post_values: &[f32],
) -> Vec<AttackPairEvent> {
    pre_values
        .iter()
        .zip(post_values)
        .enumerate()
        .filter_map(|(index, (pre, post))| {
            joiner
                .push(frame(index as i64, 7, *pre), frame(index as i64, 9, *post))
                .unwrap()
        })
        .collect()
}

#[test]
fn exact_common_event_produces_signed_post_minus_pre_delta() {
    let events = collect(
        &mut AttackPairJoiner::new(),
        &[0.0, 0.10, 0.0, 0.0, 0.0, 0.0],
        &[0.0, 0.15, 0.0, 0.0, 0.0, 0.0],
    );
    assert_eq!(events.len(), 1);
    let event = events[0];
    assert_eq!(event.kind, AttackPairEventKind::Matched);
    assert_eq!(event.pre_event_sample, Some(10));
    assert_eq!(event.post_event_sample, Some(10));
    assert!((event.delta_value.unwrap() - 0.05).abs() < 1.0e-6);
    assert!(event.has_valid_layout());
}

#[test]
fn bounded_local_matching_keeps_a_shifted_post_peak_as_the_same_event() {
    let events = collect(
        &mut AttackPairJoiner::new(),
        &[0.0, 0.20, 0.0, 0.0, 0.0, 0.0, 0.0],
        &[0.0, 0.0, 0.25, 0.0, 0.0, 0.0, 0.0],
    );
    assert_eq!(events.len(), 1);
    assert_eq!(events[0].kind, AttackPairEventKind::Matched);
    assert_eq!(events[0].pre_event_sample, Some(10));
    assert_eq!(events[0].post_event_sample, Some(20));
    assert_eq!(events[0].event_sample, 20);
}

#[test]
fn a_missing_post_candidate_never_invents_a_delta() {
    let events = collect(
        &mut AttackPairJoiner::new(),
        &[0.0, 0.20, 0.0, 0.0, 0.0, 0.0],
        &[0.0; 6],
    );
    assert_eq!(events.len(), 1);
    assert_eq!(events[0].kind, AttackPairEventKind::PreOnly);
    assert_eq!(events[0].pre_value, Some(0.20));
    assert_eq!(events[0].post_value, None);
    assert_eq!(events[0].delta_value, None);
    assert!(events[0].has_valid_layout());
}

#[test]
fn identity_change_starts_a_new_pair_generation_and_drops_pending_state() {
    let mut joiner = AttackPairJoiner::new();
    assert!(joiner
        .push(frame(0, 1, 0.20), frame(0, 2, 0.20))
        .unwrap()
        .is_none());
    for index in 0..6 {
        assert!(joiner
            .push(frame(index, 3, 0.0), frame(index, 4, 0.0))
            .unwrap()
            .is_none());
    }
    let events = collect(
        &mut joiner,
        &[0.0, 0.20, 0.0, 0.0, 0.0, 0.0],
        &[0.0, 0.20, 0.0, 0.0, 0.0, 0.0],
    );
    assert_eq!(events.len(), 1);
    assert_eq!(events[0].pair_generation, 3);
}

#[test]
fn mismatched_definition_or_content_time_fails_closed() {
    let mut joiner = AttackPairJoiner::new();
    let pre = frame(0, 1, 0.0);
    let mut post = frame(0, 2, 0.0);
    post.definition_hash[0] ^= 1;
    assert_eq!(
        joiner.push(pre, post).unwrap_err(),
        AttackPairError::DefinitionMismatch
    );
    let mut shifted = frame(0, 2, 0.0);
    shifted.support_start_samples += 1;
    shifted.support_end_samples += 1;
    shifted.event_sample += 1;
    assert_eq!(
        joiner.push(pre, shifted).unwrap_err(),
        AttackPairError::ContentTimeMismatch
    );
}

fn collect_with_offset(offset_millis: i64) -> AttackPairEvent {
    let mut joiner = AttackPairJoiner::new();
    let mut event = None;
    for sample in 0..=80 {
        let pre_value = if sample == 10 { 0.20 } else { 0.0 };
        let post_value = if sample == 10 + offset_millis {
            0.25
        } else {
            0.0
        };
        let make = |generation, value| AttackOdfFrame {
            generation,
            sample_rate: 1_000,
            channels: 2,
            definition_hash: [12; 32],
            window_samples: 20,
            hop_samples: 1,
            support_start_samples: sample - 10,
            support_end_samples: sample + 10,
            event_sample: sample,
            value,
        };
        if let Some(emitted) = joiner
            .push(make(1, pre_value), make(2, post_value))
            .unwrap()
        {
            event = Some(emitted);
        }
    }
    event.unwrap()
}

#[test]
fn twenty_five_millisecond_match_boundary_is_inclusive() {
    let event = collect_with_offset(25);
    assert_eq!(event.kind, AttackPairEventKind::Matched);
    assert_eq!(event.pre_event_sample, Some(10));
    assert_eq!(event.post_event_sample, Some(35));
}

#[test]
fn twenty_six_milliseconds_does_not_create_a_false_delta() {
    let event = collect_with_offset(26);
    assert_eq!(event.kind, AttackPairEventKind::PostOnly);
    assert_eq!(event.pre_event_sample, None);
    assert_eq!(event.post_event_sample, Some(36));
    assert_eq!(event.delta_value, None);
}

#[test]
fn one_side_candidate_cannot_be_reused_by_two_common_events() {
    let candidates = VecDeque::from([CandidateFrame {
        sample: 15,
        pre_candidate: true,
        post_candidate: false,
        pre_value: 0.2,
        post_value: 0.0,
    }]);
    let first = best_match(&candidates, 0, 25, true, None).unwrap();
    assert_eq!(first.sample, 15);
    assert!(best_match(&candidates, 31, 25, true, Some(first.sample)).is_none());
}
