use super::*;
use crate::record_spool::MeasureSampleConsumer;
use std::sync::atomic::Ordering;
use std::sync::{mpsc, Arc};
use std::time::{Duration, Instant};

fn wait_for_slots(consumer: &MeasureSampleConsumer, expected: usize) {
    let deadline = Instant::now() + Duration::from_secs(2);
    while consumer.slots() != expected && Instant::now() < deadline {
        std::thread::yield_now();
    }
    assert_eq!(consumer.slots(), expected);
}

fn finish_and_mark_drained(
    ingress: &RecordIngress,
    generation: u64,
    consumer: &mut MeasureSampleConsumer,
) {
    assert!(ingress.finish_capture_from_measure(generation, Duration::from_secs(2)));
    while consumer.pop().is_ok() {}
    ingress.mark_drained_from_measure(generation);
}

#[test]
fn three_max_blocks_are_preserved_by_the_spool_before_measure_consumes_any_sample() {
    let block_samples = crate::ingest_contract::MAX_AUDIO_BLOCK_FRAMES * 2;
    let ingress =
        RecordIngress::new(block_samples * crate::ingest_contract::RECORD_UNCONSUMED_BURST_BLOCKS);
    assert!(ingress.prepare_for_generation(1));
    // SAFETY: test is the sole Audio Thread owner.
    assert!(unsafe { ingress.adopt_from_audio() });
    let mut consumer = ingress.take_consumer_for_measure(1).unwrap();
    let block = vec![0.25_f32; block_samples];
    for _ in 0..crate::ingest_contract::RECORD_UNCONSUMED_BURST_BLOCKS {
        // SAFETY: same single-thread test contract.
        assert_eq!(unsafe { ingress.push_from_audio(1, &block) }, block.len());
    }
    assert!(ingress.finish_capture_from_measure(1, Duration::from_secs(2)));
    wait_for_slots(&consumer, block_samples * 3);
    while consumer.pop().is_ok() {}
    ingress.mark_drained_from_measure(1);
    assert!(ingress.prepare_for_generation(2));
}

#[test]
#[ignore = "24-instance offline Record spool stress"]
fn twenty_four_instances_spool_twelve_seconds_without_measure_consumption() {
    const SAMPLE_RATE: usize = 96_000;
    const CHANNELS: usize = 2;
    const SECONDS: usize = 12;
    const CALLBACK_SAMPLES: usize = 4_096;
    let expected_samples = SAMPLE_RATE * CHANNELS * SECONDS;
    assert!(
        expected_samples > crate::ingest_contract::record_ring_capacity_samples(CHANNELS),
        "stress must exceed the old finite in-memory lane"
    );
    let mut lanes = Vec::new();
    for _ in 0..crate::capture_contract::MAX_CAPTURE_PAIRS * 2 {
        let ingress = RecordIngress::new(crate::ingest_contract::record_ring_capacity_samples(
            CHANNELS,
        ));
        assert!(ingress.prepare_for_generation(1));
        // SAFETY: this test drives every producer serially from this one thread.
        assert!(unsafe { ingress.adopt_from_audio() });
        let reader = ingress.take_consumer_for_measure(1).unwrap();
        lanes.push((ingress, reader));
    }

    let callback = vec![0.125_f32; CALLBACK_SAMPLES];
    let mut pushed = 0_usize;
    while pushed < expected_samples {
        let sample_count = CALLBACK_SAMPLES.min(expected_samples - pushed);
        for (ingress, _) in &lanes {
            // SAFETY: this loop is the sole producer owner for every instance.
            assert_eq!(
                unsafe { ingress.push_from_audio(1, &callback[..sample_count]) },
                sample_count,
                "Record admission overflowed after {pushed} samples"
            );
        }
        pushed += sample_count;
    }

    for (ingress, reader) in &lanes {
        assert!(ingress.finish_capture_from_measure(1, Duration::from_secs(10)));
        assert_eq!(reader.slots(), expected_samples);
    }
}

#[test]
fn insufficient_capacity_rejects_the_complete_callback_without_partial_samples() {
    let ingress = RecordIngress::new(5);
    assert!(ingress.prepare_for_generation(1));
    // SAFETY: test owns the single Audio producer.
    assert!(unsafe { ingress.adopt_from_audio() });
    let consumer = ingress.take_consumer_for_measure(1).unwrap();
    assert_eq!(unsafe { ingress.push_from_audio(1, &[1.0; 6]) }, 0);
    assert_eq!(
        consumer.slots(),
        0,
        "oversized callback must remain invisible"
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
    let mut consumer = ingress.take_consumer_for_measure(7).unwrap();
    finish_and_mark_drained(&ingress, 7, &mut consumer);
    assert!(ingress.prepare_for_generation(8));
}

#[test]
fn completed_generation_releases_the_control_spool_before_the_next_keep() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(1));
    // SAFETY: test owns the single Audio producer.
    assert!(unsafe { ingress.adopt_from_audio() });
    let mut consumer = ingress.take_consumer_for_measure(1).unwrap();
    assert_eq!(unsafe { ingress.push_from_audio(1, &[0.5; 8]) }, 8);
    finish_and_mark_drained(&ingress, 1, &mut consumer);
    assert!(
        !ingress.has_current_spool_for_test(),
        "a completed generation must not retain its unlinked backing file"
    );
    drop(consumer);
    assert!(ingress.prepare_for_generation(2));
}

#[test]
fn premature_drain_ack_keeps_the_generation_and_spool_owned() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(1));
    // SAFETY: test owns the single Audio producer.
    assert!(unsafe { ingress.adopt_from_audio() });
    let mut consumer = ingress.take_consumer_for_measure(1).unwrap();
    assert_eq!(unsafe { ingress.push_from_audio(1, &[0.5; 8]) }, 8);
    assert!(ingress.finish_capture_from_measure(1, Duration::from_secs(2)));

    ingress.mark_drained_from_measure(1);
    assert!(
        ingress.has_current_spool_for_test(),
        "control seed must remain until the Measure reader consumes the complete generation"
    );
    assert!(!ingress.prepare_for_generation(2));

    while consumer.pop().is_ok() {}
    ingress.mark_drained_from_measure(1);
    assert!(!ingress.has_current_spool_for_test());
    assert!(ingress.prepare_for_generation(2));
}

#[test]
fn failed_generation_is_terminal_and_does_not_poison_the_next_keep() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(7));
    // SAFETY: test owns the single Audio producer.
    assert!(unsafe { ingress.adopt_from_audio() });
    let consumer = ingress.take_consumer_for_measure(7).unwrap();
    assert_eq!(unsafe { ingress.push_from_audio(7, &[0.25; 8]) }, 8);
    assert!(ingress.mark_failed_from_measure(7));
    assert!(!ingress.has_current_spool_for_test());
    drop(consumer);
    assert!(
        ingress.prepare_for_generation(8),
        "one unavailable TRACE must not disable every later explicit Keep"
    );
}

#[test]
fn take_start_is_emitted_only_for_first_pushed_callback_of_each_generation() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(1));
    assert!(ingress.begin_generation_from_audio(1, 123));
    assert!(!ingress.begin_generation_from_audio(1, 999));
    assert_eq!(ingress.capture_origin_for_measure(1), Some(123));

    let mut consumer = ingress.take_consumer_for_measure(1).unwrap();
    finish_and_mark_drained(&ingress, 1, &mut consumer);
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
        !ingress.has_current_spool_for_test(),
        "a closed generation retired during restart must release its control seed"
    );
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
    let mut consumer = ingress.take_consumer_for_measure(1).unwrap();
    finish_and_mark_drained(&ingress, 1, &mut consumer);
    assert!(ingress.prepare_for_generation(2));
}

#[test]
fn measure_restart_replays_the_live_spool_from_sample_zero() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(1));
    // SAFETY: test owns the sole Audio producer.
    assert!(unsafe { ingress.adopt_from_audio() });
    let mut first_reader = ingress.take_consumer_for_measure(1).unwrap();
    assert_eq!(
        unsafe { ingress.push_from_audio(1, &[1.0, 2.0, 3.0, 4.0]) },
        4
    );
    wait_for_slots(&first_reader, 4);
    assert_eq!(first_reader.pop(), Ok(1.0));
    assert_eq!(first_reader.pop(), Ok(2.0));
    drop(first_reader);

    ingress.replace_after_measure_restart(record_observation(true, 1, true));
    assert_eq!(
        ingress.reconcile_after_measure_restart(record_observation(true, 1, true)),
        RecordIngressRestartOutcome::PreservedLive
    );
    let mut replay = ingress.take_consumer_for_measure(1).unwrap();
    wait_for_slots(&replay, 4);
    let mut observed = Vec::new();
    while let Ok(sample) = replay.pop() {
        observed.push(sample);
    }
    assert_eq!(observed, vec![1.0, 2.0, 3.0, 4.0]);
    assert!(ingress.finish_capture_from_measure(1, Duration::from_secs(2)));
    ingress.mark_drained_from_measure(1);
}

#[test]
fn stop_seal_waits_for_the_last_audio_copy_and_rejects_late_audio() {
    let ingress = Arc::new(RecordIngress::new(16));
    assert!(ingress.prepare_for_generation(1));
    // SAFETY: main owns the producer until the spawned Audio surrogate starts.
    assert!(unsafe { ingress.adopt_from_audio() });
    let mut reader = ingress.take_consumer_for_measure(1).unwrap();
    let (entered_tx, entered_rx) = mpsc::sync_channel(0);
    let (release_tx, release_rx) = mpsc::sync_channel(0);
    let audio_ingress = Arc::clone(&ingress);
    let audio = std::thread::spawn(move || {
        // SAFETY: this thread is the sole producer owner for the closure lifetime.
        unsafe {
            audio_ingress.with_producer_from_audio(1, |producer| {
                entered_tx.send(()).unwrap();
                release_rx.recv().unwrap();
                producer.push(0.75).unwrap();
            })
        }
    });
    entered_rx.recv().unwrap();

    let finish_ingress = Arc::clone(&ingress);
    let finish = std::thread::spawn(move || {
        finish_ingress.finish_capture_from_measure(1, Duration::from_secs(2))
    });
    std::thread::sleep(Duration::from_millis(20));
    assert!(
        !finish.is_finished(),
        "the spool must not seal while an Audio copy is in flight"
    );
    release_tx.send(()).unwrap();
    assert_eq!(audio.join().unwrap(), Some(()));
    assert!(finish.join().unwrap());
    wait_for_slots(&reader, 1);
    assert_eq!(reader.pop(), Ok(0.75));
    // SAFETY: the former Audio owner has joined; this call is serial and must be rejected.
    assert_eq!(
        unsafe { ingress.with_producer_from_audio(1, |producer| producer.push(9.0)) },
        None
    );
    ingress.mark_drained_from_measure(1);
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
fn next_generation_clears_a_late_restart_marker_for_the_drained_generation() {
    let ingress = RecordIngress::new(16);
    assert!(ingress.prepare_for_generation(7));
    let mut consumer = ingress.take_consumer_for_measure(7).unwrap();
    finish_and_mark_drained(&ingress, 7, &mut consumer);

    // Model the last instruction of a Watchdog replacement racing just after the ordinary drain
    // acknowledgement. `prepare_for_generation` owns the control-plane cleanup of this marker.
    ingress
        .restart_pending_generation
        .store(7, Ordering::Release);
    assert!(ingress.prepare_for_generation(8));
    assert_eq!(
        ingress.reconcile_after_measure_restart(record_observation(true, 8, true)),
        RecordIngressRestartOutcome::None,
        "a proven-drained restart marker must never leak into Keep 8"
    );
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
    let old_consumer = ingress.take_consumer_for_measure(7).unwrap();
    drop(old_consumer); // crashed Measure worker loses its consumer

    ingress.replace_after_measure_restart(record_observation(true, 7, true));
    // The spool survives a Measure restart, so Audio keeps the same producer generation.
    assert!(!unsafe { ingress.adopt_from_audio() });
    assert_eq!(unsafe { ingress.push_from_audio(7, &[0.25, -0.25]) }, 2);

    assert_eq!(
        ingress.reconcile_after_measure_restart(record_observation(false, 7, false)),
        RecordIngressRestartOutcome::Retired
    );
    assert!(ingress.prepare_for_generation(8));
    // SAFETY: adopt the fresh post-retirement lane published by reconciliation.
    assert!(unsafe { ingress.adopt_from_audio() });
    let next_consumer = ingress.take_consumer_for_measure(8).unwrap();
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
        "pub unsafe fn with_producer_from_audio",
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
            include_str!("io_thread_post_record_ack.rs"),
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
