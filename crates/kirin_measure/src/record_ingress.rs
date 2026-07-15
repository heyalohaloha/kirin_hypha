//! Dedicated preallocated Record ingest lane.
//!
//! Allocation, consumer publication and reclamation belong to control/Measure/Watchdog threads.
//! The Audio Thread only performs atomic pointer adoption and copies samples into an rtrb
//! producer. The lane is armed before `RecordStateMachine` enters Record and is reused only after
//! the previous generation has been completely drained.

use crate::watchdog_handoff::WatchProducerHandoff;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;

struct ControlState {
    consumer: Option<rtrb::Consumer<f32>>,
    allocated: bool,
}

/// Shared lifecycle for one plugin instance's Record-only SPSC lane.
pub struct RecordIngress {
    producer_handoff: WatchProducerHandoff,
    control: Mutex<ControlState>,
    capacity: usize,
    armed_generation: AtomicU64,
    audio_generation: AtomicU64,
    drained_generation: AtomicU64,
    /// Exact Record generation whose Measure consumer was replaced after a worker loss.
    ///
    /// The replacement Watchdog publishes this before spawning the next Measure worker. The new
    /// worker then resolves the generation from the durable `RecordStateMachine` facts: a live
    /// generation is preserved, an entered-and-closed generation is retired, and a generation
    /// which has not entered Record yet remains pending. This closes the crash-after-Stop hole
    /// without treating a still-live Record as drained.
    restart_pending_generation: AtomicU64,
    capture_origin_generation: AtomicU64,
    capture_origin_frame: AtomicU64,
    producer_installed: AtomicBool,
    measure_attached: AtomicBool,
}

impl RecordIngress {
    /// Construct a lane without allocating its Record backlog. The one-slot bootstrap producer
    /// keeps the Audio-side pointer non-null until the first control-thread preparation.
    pub fn new(capacity: usize) -> Self {
        let (bootstrap_producer, bootstrap_consumer) = rtrb::RingBuffer::new(1);
        drop(bootstrap_consumer);
        Self {
            producer_handoff: WatchProducerHandoff::new(bootstrap_producer),
            control: Mutex::new(ControlState {
                consumer: None,
                allocated: false,
            }),
            capacity: capacity.max(1),
            armed_generation: AtomicU64::new(0),
            audio_generation: AtomicU64::new(0),
            drained_generation: AtomicU64::new(0),
            restart_pending_generation: AtomicU64::new(0),
            capture_origin_generation: AtomicU64::new(0),
            capture_origin_frame: AtomicU64::new(0),
            producer_installed: AtomicBool::new(false),
            measure_attached: AtomicBool::new(false),
        }
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    /// Prepare the complete Record lane before the state machine is allowed to enter Record.
    /// Re-arming is accepted only after the previous generation's consumer drain completed.
    pub fn prepare_for_generation(&self, generation: u64) -> bool {
        if generation == 0 {
            return false;
        }
        let mut control = match self.control.lock() {
            Ok(control) => control,
            Err(poisoned) => poisoned.into_inner(),
        };
        // Generation reservation and lane publication are one control-plane transaction. Two IO
        // workers may observe the same Keep request, but they must never both pass a stale
        // `armed_generation == 0` check and let the later caller overwrite the first generation.
        let previous = self.armed_generation.load(Ordering::Acquire);
        if previous == generation {
            return control.allocated;
        }
        if previous != 0 && self.drained_generation.load(Ordering::Acquire) < previous {
            return false;
        }
        if !control.allocated {
            let (producer, consumer) = rtrb::RingBuffer::new(self.capacity);
            control.consumer = Some(consumer);
            control.allocated = true;
            self.measure_attached.store(false, Ordering::Release);
            self.producer_handoff.publish_from_watchdog(producer);
        }
        self.armed_generation.store(generation, Ordering::Release);
        self.capture_origin_generation.store(0, Ordering::Release);
        self.capture_origin_frame.store(0, Ordering::Release);
        true
    }

    /// Adopt a control-thread-published producer. Audio Thread only: no allocation/free/lock/I/O.
    ///
    /// # Safety
    /// Must be called by the single Audio Thread owner.
    #[inline]
    pub unsafe fn adopt_from_audio(&self) -> bool {
        // SAFETY: forwarded single-Audio-Thread ownership contract.
        let adopted = unsafe { self.producer_handoff.swap_pending_from_audio() };
        if adopted {
            self.producer_installed.store(true, Ordering::Release);
        }
        adopted
    }

    /// Copy one Record callback into the dedicated lane.
    ///
    /// # Safety
    /// Must be called by the single Audio Thread owner after `adopt_from_audio`.
    #[inline]
    pub unsafe fn push_from_audio(&self, generation: u64, samples: &[f32]) -> usize {
        let mut pushed = 0;
        // SAFETY: forwarded single-Audio-Thread ownership contract.
        let _ = unsafe {
            self.with_producer_from_audio(generation, |producer| {
                if producer.slots() < samples.len() {
                    return;
                }
                for &sample in samples {
                    // The complete block was admitted above; the sole consumer can only free
                    // more slots while this producer commits the callback.
                    let _ = producer.push(sample);
                    pushed += 1;
                }
            })
        };
        pushed
    }

    /// Run one bounded copy operation against the active Record producer.
    ///
    /// # Safety
    /// Must be called by the single Audio Thread owner. The producer reference must not escape
    /// the closure.
    #[inline]
    pub unsafe fn with_producer_from_audio<R>(
        &self,
        generation: u64,
        operation: impl FnOnce(&mut rtrb::Producer<f32>) -> R,
    ) -> Option<R> {
        if !self.producer_installed.load(Ordering::Acquire)
            || self.armed_generation.load(Ordering::Acquire) != generation
        {
            return None;
        }
        self.audio_generation.store(generation, Ordering::Release);
        // SAFETY: producer access is confined to this Audio callback and closure.
        Some(unsafe {
            self.producer_handoff
                .with_active_producer_from_audio(operation)
        })
    }

    /// Publish the global capture-clock frame at which this dedicated Record lane begins.
    /// Audio Thread only; atomics only.
    #[inline]
    pub fn begin_generation_from_audio(&self, generation: u64, capture_origin_frame: u64) -> bool {
        if generation == 0 || self.armed_generation.load(Ordering::Acquire) != generation {
            return false;
        }
        if self.capture_origin_generation.load(Ordering::Acquire) == 0 {
            self.capture_origin_frame
                .store(capture_origin_frame, Ordering::Relaxed);
            return self
                .capture_origin_generation
                .compare_exchange(0, generation, Ordering::Release, Ordering::Acquire)
                .is_ok();
        }
        false
    }

    pub fn capture_origin_for_measure(&self, generation: u64) -> Option<u64> {
        (generation != 0 && self.capture_origin_generation.load(Ordering::Acquire) == generation)
            .then(|| self.capture_origin_frame.load(Ordering::Acquire))
    }

    /// Measure Thread takes the consumer once. It retains ownership across successive Records.
    pub fn take_consumer_for_measure(&self) -> Option<rtrb::Consumer<f32>> {
        let mut control = match self.control.lock() {
            Ok(control) => control,
            Err(poisoned) => poisoned.into_inner(),
        };
        let consumer = control.consumer.take();
        if consumer.is_some() {
            self.measure_attached.store(true, Ordering::Release);
        }
        consumer
    }

    pub fn mark_drained_from_measure(&self, generation: u64) {
        if generation != 0 && self.armed_generation.load(Ordering::Acquire) == generation {
            self.drained_generation
                .fetch_max(generation, Ordering::AcqRel);
            let _ = self.restart_pending_generation.compare_exchange(
                generation,
                0,
                Ordering::AcqRel,
                Ordering::Acquire,
            );
        }
    }

    pub fn generation_ready(&self, generation: u64) -> bool {
        generation != 0
            && self.armed_generation.load(Ordering::Acquire) == generation
            && self.producer_installed.load(Ordering::Acquire)
            && self.measure_attached.load(Ordering::Acquire)
    }

    /// Read-only Audio/test-thread drain introspection.
    ///
    /// # Safety
    /// Same single Audio Thread ownership as producer push methods.
    pub unsafe fn drained_from_audio(&self) -> bool {
        self.producer_installed.load(Ordering::Acquire)
            // SAFETY: read-only slots query under the single Audio Thread contract.
            && unsafe { self.producer_handoff.active_slots_from_audio() } == self.capacity
    }

    /// Reclaim retired producers outside the Audio Thread.
    pub fn reclaim_from_watchdog(&self) {
        self.producer_handoff.reclaim_retired_from_watchdog();
    }

    /// Replace an allocated lane after the Measure Thread owning its consumer terminates.
    /// Allocation/publication happen on the Watchdog Thread; Audio adopts the producer later.
    ///
    /// `observation` is an exact snapshot of the shared Record state. A generation is retired here
    /// only when it is already known to have entered (`record_generation == armed_generation`) and
    /// is now fully closed (`!recording && !has_record_session`). A not-yet-entered generation has
    /// `record_generation < armed_generation` and must remain eligible to enter after restart.
    pub(crate) fn replace_after_measure_restart(
        &self,
        observation: RecordIngressGenerationObservation,
    ) {
        let mut control = match self.control.lock() {
            Ok(control) => control,
            Err(poisoned) => poisoned.into_inner(),
        };
        if !control.allocated {
            return;
        }
        self.install_fresh_lane(&mut control);
        let armed_generation = self.armed_generation.load(Ordering::Acquire);
        self.restart_pending_generation
            .store(armed_generation, Ordering::Release);
        if observation.proves_closed(armed_generation) {
            // The lane was replaced immediately above and cannot contain old-generation samples.
            self.retire_restart_generation(armed_generation, false);
        }
    }

    /// Resolve one Watchdog replacement from the newly started Measure worker.
    ///
    /// This is deliberately separate from [`Self::replace_after_measure_restart`]. If Stop races
    /// the Watchdog's first live observation, the replacement worker sees the later closed state
    /// and retires the exact old generation. Conversely, an Entering state retains a Record session
    /// while `recording == false`, so it remains pending instead of being misclassified as closed.
    pub(crate) fn reconcile_after_measure_restart(
        &self,
        observation: RecordIngressGenerationObservation,
    ) -> RecordIngressRestartOutcome {
        let pending = self.restart_pending_generation.load(Ordering::Acquire);
        if pending == 0 {
            return RecordIngressRestartOutcome::None;
        }
        if observation.proves_closed(pending) || observation.record_generation > pending {
            return if self.retire_restart_generation(pending, true) {
                RecordIngressRestartOutcome::Retired
            } else {
                RecordIngressRestartOutcome::None
            };
        }
        if observation.record_generation == pending && observation.recording {
            return if self
                .restart_pending_generation
                .compare_exchange(pending, 0, Ordering::AcqRel, Ordering::Acquire)
                .is_ok()
            {
                RecordIngressRestartOutcome::PreservedLive
            } else {
                RecordIngressRestartOutcome::None
            };
        }
        RecordIngressRestartOutcome::Pending
    }

    fn retire_restart_generation(&self, generation: u64, replace_unconsumed_lane: bool) -> bool {
        if generation == 0
            || self
                .restart_pending_generation
                .compare_exchange(generation, 0, Ordering::AcqRel, Ordering::Acquire)
                .is_err()
        {
            return false;
        }
        if replace_unconsumed_lane {
            let mut control = match self.control.lock() {
                Ok(control) => control,
                Err(poisoned) => poisoned.into_inner(),
            };
            if control.allocated {
                // The replacement worker has not acknowledged this generation live, so it has not
                // taken the Record consumer. Audio may nevertheless have adopted/pushed while Stop
                // raced startup. Drop that consumer and publish a fresh lane before allowing the
                // next generation; otherwise old samples could contaminate Keep 2.
                self.install_fresh_lane(&mut control);
            }
            // `prepare_for_generation` takes the same control lock before reading this barrier.
            // Publish retirement while still holding it so lane replacement + reuse authority are
            // one control-plane transaction.
            self.drained_generation
                .fetch_max(generation, Ordering::AcqRel);
            return true;
        }
        self.drained_generation
            .fetch_max(generation, Ordering::AcqRel);
        true
    }

    fn install_fresh_lane(&self, control: &mut ControlState) {
        let (producer, consumer) = rtrb::RingBuffer::new(self.capacity);
        control.consumer = Some(consumer);
        self.measure_attached.store(false, Ordering::Release);
        self.producer_installed.store(false, Ordering::Release);
        self.capture_origin_generation.store(0, Ordering::Release);
        self.capture_origin_frame.store(0, Ordering::Release);
        self.producer_handoff.publish_from_watchdog(producer);
    }
}

/// Lock-free Record facts sampled by the Watchdog/new Measure worker around a consumer restart.
/// `has_record_session` distinguishes the state machine's transient Entering state from a Record
/// generation which has already completed `exit_record()`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct RecordIngressGenerationObservation {
    pub recording: bool,
    pub record_generation: u64,
    pub has_record_session: bool,
}

impl RecordIngressGenerationObservation {
    /// Capture the durable state-machine facts used by restart reconciliation.
    ///
    /// The fields are read conservatively: a concurrent transition can produce `recording=true`
    /// with an already-cleared session, or `recording=false` with a newly-installed session. Both
    /// combinations remain non-terminal. Retirement requires the stricter same-generation,
    /// non-recording, sessionless proof, so an in-flight/live generation cannot be retired by a
    /// torn observation.
    pub(crate) fn capture(record_sm: &crate::record::RecordStateMachine) -> Self {
        Self {
            recording: record_sm.is_recording(),
            record_generation: record_sm.generation(),
            has_record_session: record_sm.record_session_id().is_some(),
        }
    }

    fn proves_closed(self, armed_generation: u64) -> bool {
        armed_generation != 0
            && self.record_generation == armed_generation
            && !self.recording
            && !self.has_record_session
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum RecordIngressRestartOutcome {
    None,
    Pending,
    PreservedLive,
    Retired,
}

#[cfg(test)]
#[path = "record_ingress_tests.rs"]
mod tests;
