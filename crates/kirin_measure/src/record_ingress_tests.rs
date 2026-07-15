use super::*;

#[test]
fn three_max_blocks_fit_before_measure_consumes_any_sample() {
    let block_samples = crate::ingest_contract::MAX_AUDIO_BLOCK_FRAMES * 2;
    let ingress =
        RecordIngress::new(block_samples * crate::ingest_contract::RECORD_UNCONSUMED_BURST_BLOCKS);
    assert!(ingress.prepare_for_generation(1));
    // SAFETY: test is the sole Audio Thread owner.
    assert!(unsafe { ingress.adopt_from_audio() });
    let mut consumer = ingress.take_consumer_for_measure().unwrap();
    let block = vec![0.25_f32; block_samples];
    for _ in 0..crate::ingest_contract::RECORD_UNCONSUMED_BURST_BLOCKS {
        // SAFETY: same single-thread test contract.
        assert_eq!(unsafe { ingress.push_from_audio(1, &block) }, block.len());
    }
    assert_eq!(consumer.slots(), block_samples * 3);
    while consumer.pop().is_ok() {}
    ingress.mark_drained_from_measure(1);
    assert!(ingress.prepare_for_generation(2));
}

#[test]
fn insufficient_capacity_rejects_the_complete_callback_without_partial_samples() {
    let ingress = RecordIngress::new(5);
    assert!(ingress.prepare_for_generation(1));
    // SAFETY: test owns the single Audio producer.
    assert!(unsafe { ingress.adopt_from_audio() });
    let consumer = ingress.take_consumer_for_measure().unwrap();
    assert_eq!(
        unsafe { ingress.push_from_audio(1, &[1.0, 2.0, 3.0, 4.0]) },
        4
    );
    assert_eq!(unsafe { ingress.push_from_audio(1, &[5.0, 6.0]) }, 0);
    assert_eq!(
        consumer.slots(),
        4,
        "second callback must not be partially visible"
    );
}

#[test]
fn rearm_is_rejected_until_previous_generation_is_drained() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(7));
    assert!(
        ingress.prepare_for_generation(7),
        "retrying the same uncommitted generation must be idempotent"
    );
    assert!(!ingress.prepare_for_generation(8));
    ingress.mark_drained_from_measure(7);
    assert!(ingress.prepare_for_generation(8));
}

#[test]
fn take_start_is_emitted_only_for_first_pushed_callback_of_each_generation() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(1));
    assert!(ingress.begin_generation_from_audio(1, 123));
    assert!(!ingress.begin_generation_from_audio(1, 999));
    assert_eq!(ingress.capture_origin_for_measure(1), Some(123));

    ingress.mark_drained_from_measure(1);
    assert!(ingress.prepare_for_generation(2));
    assert!(ingress.begin_generation_from_audio(2, 456));
    assert_eq!(ingress.capture_origin_for_measure(2), Some(456));
}

fn record_observation(
    recording: bool,
    record_generation: u64,
    has_record_session: bool,
) -> RecordIngressGenerationObservation {
    RecordIngressGenerationObservation {
        recording,
        record_generation,
        has_record_session,
    }
}

#[test]
fn measure_restart_retires_only_an_exact_entered_and_closed_generation() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(1));

    ingress.replace_after_measure_restart(record_observation(false, 1, false));

    assert!(
        ingress.prepare_for_generation(2),
        "a generation already entered and closed while Measure was down must not block Keep 2"
    );
}

#[test]
fn measure_restart_preserves_the_same_live_generation() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(1));

    ingress.replace_after_measure_restart(record_observation(true, 1, true));
    assert_eq!(
        ingress.reconcile_after_measure_restart(record_observation(true, 1, true)),
        RecordIngressRestartOutcome::PreservedLive
    );

    assert!(
        !ingress.prepare_for_generation(2),
        "consumer replacement must not call a still-live generation drained"
    );
    ingress.mark_drained_from_measure(1);
    assert!(ingress.prepare_for_generation(2));
}

#[test]
fn replacement_worker_closes_stop_after_watchdog_live_snapshot_race() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(7));

    // Watchdog sees generation 7 live, but Stop wins before the replacement worker starts.
    ingress.replace_after_measure_restart(record_observation(true, 7, true));
    assert_eq!(
        ingress.reconcile_after_measure_restart(record_observation(false, 7, false)),
        RecordIngressRestartOutcome::Retired
    );
    assert!(ingress.prepare_for_generation(8));
}

#[test]
fn actual_record_state_machine_stop_after_watchdog_snapshot_retires_exact_generation() {
    let ingress = RecordIngress::new(16);
    let record_sm = crate::record::RecordStateMachine::new();
    assert!(ingress.prepare_for_generation(1));
    record_sm
        .try_enter_record_started_at_clock_transaction(
            crate::identity::License::Os,
            1,
            Some(48_000),
            "restart-contract-session",
        )
        .unwrap();

    let watchdog_observation = RecordIngressGenerationObservation::capture(&record_sm);
    assert_eq!(watchdog_observation.record_generation, 1);
    assert!(watchdog_observation.recording);
    assert!(watchdog_observation.has_record_session);
    ingress.replace_after_measure_restart(watchdog_observation);

    record_sm.exit_record();
    assert_eq!(
        ingress.reconcile_after_measure_restart(RecordIngressGenerationObservation::capture(
            &record_sm
        )),
        RecordIngressRestartOutcome::Retired
    );
    assert!(ingress.prepare_for_generation(2));
}

#[test]
fn retired_restart_generation_cannot_leak_queued_samples_into_keep_two() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(7));
    // SAFETY: this test is the sole Audio Thread owner.
    assert!(unsafe { ingress.adopt_from_audio() });
    let old_consumer = ingress.take_consumer_for_measure().unwrap();
    drop(old_consumer); // crashed Measure worker loses its consumer

    ingress.replace_after_measure_restart(record_observation(true, 7, true));
    // SAFETY: same sole Audio Thread; adopt replacement and race one old-generation callback.
    assert!(unsafe { ingress.adopt_from_audio() });
    assert_eq!(unsafe { ingress.push_from_audio(7, &[0.25, -0.25]) }, 2);

    assert_eq!(
        ingress.reconcile_after_measure_restart(record_observation(false, 7, false)),
        RecordIngressRestartOutcome::Retired
    );
    assert!(ingress.prepare_for_generation(8));
    // SAFETY: adopt the fresh post-retirement lane published by reconciliation.
    assert!(unsafe { ingress.adopt_from_audio() });
    let next_consumer = ingress.take_consumer_for_measure().unwrap();
    assert_eq!(
        next_consumer.slots(),
        0,
        "Keep 2 must begin on a physically fresh Record lane"
    );
}

#[test]
fn restart_does_not_retire_a_generation_that_has_not_entered_or_is_entering() {
    let pending = RecordIngress::new(16);
    assert!(pending.prepare_for_generation(1));
    pending.replace_after_measure_restart(record_observation(false, 0, false));
    assert_eq!(
        pending.reconcile_after_measure_restart(record_observation(false, 0, false)),
        RecordIngressRestartOutcome::Pending
    );
    assert!(!pending.prepare_for_generation(2));

    let entering = RecordIngress::new(16);
    assert!(entering.prepare_for_generation(1));
    entering.replace_after_measure_restart(record_observation(false, 1, true));
    assert_eq!(
        entering.reconcile_after_measure_restart(record_observation(false, 1, true)),
        RecordIngressRestartOutcome::Pending
    );
    assert!(!entering.prepare_for_generation(2));
}

#[test]
fn audio_methods_contain_no_lock_allocator_io_or_deallocation() {
    let source = include_str!("record_ingress.rs");
    for function in [
        "pub unsafe fn adopt_from_audio",
        "pub unsafe fn push_from_audio",
        "pub fn begin_generation_from_audio",
    ] {
        let start = source.find(function).unwrap();
        let tail = &source[start..];
        let end = tail.find("\n    }").unwrap() + 6;
        let body = &tail[..end];
        for forbidden in [".lock(", "Mutex", "Box::", "Vec::", "drop(", "File::"] {
            assert!(!body.contains(forbidden), "{function} contains {forbidden}");
        }
    }
}

#[test]
fn durable_entry_claim_is_never_taken_before_record_lane_preparation() {
    for (source, function) in [
        (
            include_str!("io_thread_pre.rs"),
            "fn enter_pre_record_if_barrier_ready",
        ),
        (
            include_str!("io_thread_post.rs"),
            "fn poll_record_signal_ack_with_base",
        ),
    ] {
        let start = source.find(function).unwrap();
        let body = &source[start..];
        let prepare = body.find("record_ingress.prepare_for_generation").unwrap();
        let claim = body.find("record_entry_lock::claim_record_entry").unwrap();
        assert!(
            prepare < claim,
            "{function} can strand an entry claim while a prior lane drains"
        );
    }
}
