//! Actual SPSC protocol fixtures, independent of detector/DSP behavior.
use super::*;
use crate::attack_runtime::AttackIngressBlock;
use rtrb::{Producer, RingBuffer};

fn consumers() -> (Producer<f32>, Producer<AttackIngressBlock>, AttackConsumers) {
    let (samples, sample_consumer) = RingBuffer::new(32);
    let (blocks, block_consumer) = RingBuffer::new(8);
    (
        samples,
        blocks,
        AttackConsumers {
            samples: sample_consumer,
            blocks: block_consumer,
            current_samples_remaining: 0,
        },
    )
}
fn block(start: i64) -> AttackIngressBlock {
    AttackIngressBlock {
        frames: 2,
        channels: 2,
        presentation_start_samples: start,
        generation: 1,
    }
}

#[test]
fn mid_descriptor_panic_recovery_discards_only_its_remaining_pcm() {
    let (mut samples, mut blocks, mut consumer) = consumers();
    for sample in [0.1, 0.2, 0.3, 0.4, 1.0, 2.0, 3.0, 4.0] {
        samples.push(sample).unwrap();
    }
    blocks.push(block(0)).unwrap();
    blocks.push(block(2)).unwrap();
    let failure = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let first = consumer.blocks.pop().unwrap();
        consumer.begin_descriptor(first);
        assert_eq!(consumer.pop_sample(), Some(0.1));
        assert_eq!(consumer.pop_sample(), Some(0.2));
        panic!("disposable worker interrupted halfway through a descriptor");
    }));
    assert!(failure.is_err());
    assert_eq!(consumer.current_samples_remaining, 2);
    // The new worker invokes this same boundary before opening the next descriptor.
    consumer.discard_current_samples();
    assert_eq!(consumer.current_samples_remaining, 0);
    let next = consumer.blocks.pop().unwrap();
    assert_eq!(next.presentation_start_samples, 2);
    consumer.begin_descriptor(next);
    for expected in [1.0, 2.0, 3.0, 4.0] {
        assert_eq!(consumer.pop_sample(), Some(expected));
    }
    assert_eq!(consumer.current_samples_remaining, 0);
}

#[test]
fn disabled_drain_discards_only_published_headers_and_keeps_unpublished_pcm() {
    let (mut samples, mut blocks, mut consumer) = consumers();
    for sample in [1.0, 2.0, 3.0, 4.0] {
        samples.push(sample).unwrap();
    }
    drain(&mut consumer); // RT has copied PCM but has not yet published its descriptor.
    assert_eq!(samples.slots(), 28);
    blocks.push(block(0)).unwrap();
    drain(&mut consumer);
    assert_eq!(samples.slots(), 32);
    for sample in [5.0, 6.0, 7.0, 8.0] {
        samples.push(sample).unwrap();
    }
    drain(&mut consumer);
    assert_eq!(samples.slots(), 28);
    // Re-enable can now consume this intact transaction at its original clock.
    blocks.push(block(2)).unwrap();
    let next = consumer.blocks.pop().unwrap();
    consumer.begin_descriptor(next);
    for expected in [5.0, 6.0, 7.0, 8.0] {
        assert_eq!(consumer.pop_sample(), Some(expected));
    }
    assert_eq!(samples.slots(), 32);
}

#[test]
fn missing_pcm_stops_at_empty_without_a_ghost_count_consuming_future_audio() {
    let (mut samples, _, mut consumer) = consumers();
    consumer.begin_descriptor(block(0));
    assert_eq!(consumer.pop_sample(), None);
    assert_eq!(consumer.current_samples_remaining, 0);
    for sample in [1.0, 2.0, 3.0, 4.0] {
        samples.push(sample).unwrap();
    }
    consumer.discard_current_samples();
    assert_eq!(
        samples.slots(),
        28,
        "unpublished later PCM stays outside the failed descriptor"
    );
}
