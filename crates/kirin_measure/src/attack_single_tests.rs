use super::*;

fn request() -> AttackSingleRequest {
    let source = AttackSourceKey {
        incarnation: [1; 16],
        generation: 7,
        sample_rate: 48_000,
        channels: 1,
        odf_hash: [2; 32],
    };
    AttackSingleRequest {
        key_source: source,
        key_event_sample: 0,
        key_token: 1,
        local_source: source,
        pre_source: None,
        event: AttackEvent {
            generation: 7,
            sample_rate: 48_000,
            channels: 1,
            definition_hash: [2; 32],
            event_sample: 0,
            decision_sample: 2,
            value: 1.0,
        },
        band: None,
        requested_end: 6_240,
        measurement_end: 6_240,
        span_end: BandSpanEnd::Window,
        pre: None,
        pre_detail: None,
        pair_kind: 4,
        target: 0,
        pair_authority_revision: 1,
        proof_token: [0; 32],
    }
}

#[test]
fn deadline_is_exact_and_late_reply_cannot_restore_a_scalar() {
    let mut control = AttackSingleControl::default();
    let token = control.begin(request(), 100);
    let mut late = control.current.clone().unwrap();
    late.finish = AttackFinish::Full;
    control.expire(1_099, true);
    assert_eq!(
        control.current.as_ref().unwrap().finish,
        AttackFinish::Acquiring
    );
    assert!(!control.commit(late, 1_100));
    let retired = control.current.as_ref().unwrap();
    assert_eq!(retired.token, token);
    assert_eq!(retired.finish, AttackFinish::Retired);
    assert_eq!(retired.reason, AttackSingleReason::RequestDeadline);
    assert!(retired.band_observation.is_none() && retired.detail.is_none());
}

#[test]
fn replaced_request_and_terminal_reply_are_immutable() {
    let mut control = AttackSingleControl::default();
    control.begin(request(), 0);
    let mut old = control.current.clone().unwrap();
    old.finish = AttackFinish::Full;
    control.begin(request(), 10);
    assert!(!control.commit(old, 11));
    let mut final_reply = control.current.clone().unwrap();
    final_reply.finish = AttackFinish::NotKept;
    assert!(control.commit(final_reply.clone(), 12));
    final_reply.finish = AttackFinish::Full;
    assert!(!control.commit(final_reply, 13));
    assert_eq!(control.current.unwrap().finish, AttackFinish::NotKept);
}

#[test]
fn worker_unavailable_retires_at_the_first_poll() {
    let mut control = AttackSingleControl::default();
    control.begin(request(), 100);
    control.expire(101, false);
    assert_eq!(
        control.current.as_ref().unwrap().reason,
        AttackSingleReason::WorkerUnavailable
    );
}

#[test]
fn only_own_next_hit_window_may_refine_while_acquiring() {
    let mut original = request();
    original.band = AttackBand::from_index(3);
    original.requested_end = 14_400;
    original.measurement_end = 14_400;
    let mut control = AttackSingleControl::default();
    control.begin(original, 0);
    let mut reply = control.current.clone().unwrap();
    reply.request.measurement_end = 4_800;
    reply.request.span_end = BandSpanEnd::NextHit;
    reply.finish = AttackFinish::Full;
    assert!(control.commit(reply.clone(), 200));
    assert_eq!(
        control.current.as_ref().unwrap().request.requested_end,
        14_400
    );
    reply.request.measurement_end = 14_400;
    assert!(!control.commit(reply, 201));
}

#[test]
fn cancel_status_distinguishes_contention_and_never_cancels_a_replacement() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    let first = runtime.selected.lock().unwrap().begin(request(), 0);
    let second = runtime.selected.lock().unwrap().begin(request(), 10);
    assert_eq!(runtime.try_cancel_single(first), Some(false));
    {
        let _guard = runtime.selected.lock().unwrap();
        assert_eq!(runtime.try_cancel_single(second), None);
    }
    assert_eq!(runtime.try_cancel_single(second), Some(true));
    assert_eq!(runtime.try_cancel_single(second), Some(false));
}

fn paired_all_request() -> (AttackSingleRequest, AttackSourceKey, AttackDetailedEvent) {
    let mut request = request();
    let mut pre_source = request.local_source;
    pre_source.generation = 19;
    request.key_source = pre_source;
    request.pre_source = Some(pre_source);
    request.pair_kind = 1;
    request.proof_token = [9; 32];
    let mut pre_event = request.event;
    pre_event.generation = 19;
    let full = AttackDetailedEvent {
        event: pre_event,
        features: crate::AttackPerceptualFeatures {
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
        shape: super::super::AttackEventShape {
            start_sample: 0,
            end_sample: 6_240,
            event_sample: 0,
            points: [0.5; super::super::ATTACK_SHAPE_POINT_CAPACITY],
        },
    };
    assert!(full.has_valid_layout());
    let mut head = full;
    head.features.complete = false;
    head.features.body_end_sample = 1_440;
    head.features.body_rms_dbfs = None;
    head.features.transient_db = None;
    head.features.sharpness_acum = None;
    head.shape.end_sample = 2_016;
    assert!(head.has_valid_layout());
    request.pre_detail = Some(head);
    (request, pre_source, full)
}

#[test]
fn paired_all_head_waits_for_same_pre_completion_without_restarting_the_deadline() {
    let (request, pre_source, full) = paired_all_request();
    let mut control = AttackSingleControl::default();
    let token = control.begin(request.clone(), 100);
    let mut other = pre_source;
    other.generation += 1;
    assert!(!control.complete_pre(token, other, [9; 32], full, 150, true));
    assert!(!control.complete_pre(token, pre_source, [8; 32], full, 150, true));
    assert!(!control.complete_pre(
        token,
        pre_source,
        [9; 32],
        request.pre_detail.unwrap(),
        150,
        true
    ));
    assert!(control.complete_pre(token, pre_source, [9; 32], full, 1_099, true));
    let current = control.current.as_ref().unwrap();
    assert_eq!(current.token, token);
    assert_eq!(current.request.key_source, request.key_source);
    assert_eq!(current.request.key_event_sample, request.key_event_sample);
    assert_eq!(current.request.key_token, request.key_token);
    assert_eq!(current.request.requested_end, 6_240);
    assert_eq!(current.request.pre_detail, Some(full));
    assert_eq!(current.finish, AttackFinish::Acquiring);
    assert_eq!(control.accepted_millis, 100);
    let mut reply = current.clone();
    reply.finish = AttackFinish::Full;
    assert!(control.commit(reply, 1_099));
    let terminal = control.current.clone();
    assert!(!control.complete_pre(token, pre_source, [9; 32], full, 1_100, true));
    assert_eq!(control.current, terminal);
}

#[test]
fn paired_all_pre_completion_at_deadline_cannot_restore_a_numeric_result() {
    let (request, source, full) = paired_all_request();
    let mut control = AttackSingleControl::default();
    let token = control.begin(request, 100);
    assert!(!control.complete_pre(token, source, [9; 32], full, 1_100, true));
    let retired = control.current.clone().unwrap();
    assert_eq!(retired.finish, AttackFinish::Retired);
    assert_eq!(retired.reason, AttackSingleReason::RequestDeadline);
    assert!(retired.detail.is_none());
    assert!(!control.complete_pre(token, source, [9; 32], full, 1_101, true));
    assert_eq!(control.current.as_ref(), Some(&retired));
}

#[test]
fn paired_all_late_next_hit_refines_only_the_physical_window() {
    let (request, source, mut full) = paired_all_request();
    full.features.body_end_sample = 2_400;
    full.shape.end_sample = 2_400;
    assert!(full.has_valid_layout());
    let mut control = AttackSingleControl::default();
    let token = control.begin(request.clone(), 100);
    assert!(control.complete_pre(token, source, [9; 32], full, 150, true));
    let current = control.current.unwrap();
    assert_eq!(current.token, token);
    assert_eq!(current.request.key_source, request.key_source);
    assert_eq!(current.request.requested_end, 6_240);
    assert_eq!(current.request.measurement_end, 2_400);
    assert_eq!(current.request.span_end, BandSpanEnd::NextHit);
}

#[test]
fn paired_all_head_only_is_not_finalized_as_audio_end_when_post_is_idle() {
    let (request, _, _) = paired_all_request();
    let runtime = AttackRuntime::new(48_000, 1).unwrap();
    runtime.selected.lock().unwrap().begin(request, 0);
    let mut worker = super::super::band_worker::BandWorker::new();
    runtime.service_single(&mut worker, true, Some(48_000));
    let selected = runtime.selected.lock().unwrap();
    assert_eq!(
        selected.current.as_ref().unwrap().finish,
        AttackFinish::Acquiring
    );
    assert!(selected.current.as_ref().unwrap().detail.is_none());
}
