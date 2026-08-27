//! Dedicated preallocated Record ingest lane.
//!
//! Allocation, consumer publication and reclamation belong to control/Measure/Watchdog threads.
//! The Audio Thread only performs atomic pointer adoption and copies samples into an rtrb
//! producer. The lane and its non-audio spool are armed before `RecordStateMachine` enters Record;
//! a new generation is published only after the previous generation has been completely drained.

use crate::watchdog_handoff::WatchProducerHandoff;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;
use std::time::{Duration, Instant};

struct AudioPushGuard<'a> {
    in_flight: &'a AtomicU64,
}

impl Drop for AudioPushGuard<'_> {
    fn drop(&mut self) {
        self.in_flight.fetch_sub(1, Ordering::AcqRel);
    }
}

struct ControlState {
    spool: Option<crate::record_spool::RecordSpool>,
    retired_spools: Vec<crate::record_spool::RecordSpool>,
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
    accepting_audio: AtomicBool,
    audio_pushes_in_flight: AtomicU64,
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
                spool: None,
                retired_spools: Vec::new(),
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
            accepting_audio: AtomicBool::new(false),
            audio_pushes_in_flight: AtomicU64::new(0),
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
        if (!control.allocated || previous != generation)
            && !self.install_fresh_lane(&mut control, generation)
        {
            return false;
        }
        self.armed_generation.store(generation, Ordering::Release);
        if previous != 0 && previous != generation {
            let _ = self.restart_pending_generation.compare_exchange(
                previous,
                0,
                Ordering::AcqRel,
                Ordering::Acquire,
            );
        }
        self.capture_origin_generation.store(0, Ordering::Release);
        self.capture_origin_frame.store(0, Ordering::Release);
        self.accepting_audio.store(true, Ordering::Release);
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
        let mut pushed = 0_usize;
        // SAFETY: forwarded single-Audio-Thread ownership contract.
        let _ = unsafe {
            self.with_producer_from_audio(generation, |producer| {
                if producer.slots() < samples.len() {
                    return;
                }
                for &sample in samples {
                    // The spool consumer normally only frees more slots. A disk-write failure can
                    // close it concurrently, though, and that failure must become an observable
                    // partial/failed callback instead of being counted as accepted audio.
                    if producer.push(sample).is_err() {
                        break;
                    }
                    pushed = pushed.saturating_add(1);
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
        self.audio_pushes_in_flight.fetch_add(1, Ordering::AcqRel);
        let _push_guard = AudioPushGuard {
            in_flight: &self.audio_pushes_in_flight,
        };
        if !self.accepting_audio.load(Ordering::Acquire)
            || !self.producer_installed.load(Ordering::Acquire)
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

    /// Measure Thread opens a replayable reader over the non-audio spool. A replacement Measure
    /// worker can reopen the same generation from sample zero after a worker failure.
    pub(crate) fn take_consumer_for_measure(
        &self,
        generation: u64,
    ) -> Option<crate::record_spool::MeasureSampleConsumer> {
        if generation == 0 || self.armed_generation.load(Ordering::Acquire) != generation {
            return None;
        }
        if self
            .measure_attached
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .is_err()
        {
            return None;
        }
        let control = match self.control.lock() {
            Ok(control) => control,
            Err(poisoned) => poisoned.into_inner(),
        };
        let reader = control.spool.as_ref().and_then(|spool| spool.reader().ok());
        if reader.is_none() {
            self.measure_attached.store(false, Ordering::Release);
        }
        reader.map(crate::record_spool::MeasureSampleConsumer::record)
    }

    /// Stop Record admission, wait for the last Audio callback to leave its atomic copy section,
    /// then seal and fully drain the Audio -> spool lane. The Measure worker may only finalize
    /// after this returns true.
    pub fn finish_capture_from_measure(&self, generation: u64, timeout: Duration) -> bool {
        if generation == 0 || self.armed_generation.load(Ordering::Acquire) != generation {
            return false;
        }
        self.accepting_audio.store(false, Ordering::Release);
        let deadline = Instant::now() + timeout;
        while self.audio_pushes_in_flight.load(Ordering::Acquire) != 0 {
            if Instant::now() >= deadline {
                return false;
            }
            std::thread::yield_now();
        }
        let control = match self.control.lock() {
            Ok(control) => control,
            Err(poisoned) => poisoned.into_inner(),
        };
        let Some(spool) = control.spool.as_ref() else {
            return false;
        };
        spool.seal();
        spool.wait_closed(deadline.saturating_duration_since(Instant::now()))
    }

    pub fn mark_drained_from_measure(&self, generation: u64) {
        if generation != 0 && self.armed_generation.load(Ordering::Acquire) == generation {
            let mut control = match self.control.lock() {
                Ok(control) => control,
                Err(poisoned) => poisoned.into_inner(),
            };
            if !control
                .spool
                .as_ref()
                .is_some_and(|spool| spool.fully_consumed())
            {
                return;
            }
            self.drained_generation
                .fetch_max(generation, Ordering::AcqRel);
            self.measure_attached.store(false, Ordering::Release);
            let _ = self.restart_pending_generation.compare_exchange(
                generation,
                0,
                Ordering::AcqRel,
                Ordering::Acquire,
            );
            control.allocated = false;
            // The Measure reader keeps the unlinked file alive until the caller swaps back to its
            // Watch lane. Removing the seed here guarantees the completed multi-GB spool is freed
            // immediately when that reader is dropped, not at the next Keep or plug-in teardown.
            drop(control.spool.take());
        }
    }

    /// Terminalize a generation whose spool or Measure finalization failed. The failed TRACE stays
    /// unavailable and never advances the Record seal, but one local I/O fault must not permanently
    /// reject every later explicit Keep for this plug-in instance.
    pub fn mark_failed_from_measure(&self, generation: u64) -> bool {
        if generation == 0 || self.armed_generation.load(Ordering::Acquire) != generation {
            return false;
        }
        self.accepting_audio.store(false, Ordering::Release);
        let mut control = match self.control.lock() {
            Ok(control) => control,
            Err(poisoned) => poisoned.into_inner(),
        };
        if self.armed_generation.load(Ordering::Acquire) != generation {
            return false;
        }
        if let Some(spool) = control.spool.as_ref() {
            spool.seal();
        }
        control.allocated = false;
        let failed_spool = control.spool.take();
        self.drained_generation
            .fetch_max(generation, Ordering::AcqRel);
        self.measure_attached.store(false, Ordering::Release);
        let _ = self.restart_pending_generation.compare_exchange(
            generation,
            0,
            Ordering::AcqRel,
            Ordering::Acquire,
        );
        drop(control);
        // RecordSpool::drop is non-blocking; an in-flight disk worker owns its remaining lifetime.
        drop(failed_spool);
        true
    }

    #[cfg(test)]
    fn has_current_spool_for_test(&self) -> bool {
        match self.control.lock() {
            Ok(control) => control.spool.is_some(),
            Err(poisoned) => poisoned.into_inner().spool.is_some(),
        }
    }

    pub fn generation_ready(&self, generation: u64) -> bool {
        generation != 0
            && self.armed_generation.load(Ordering::Acquire) == generation
            && self.producer_installed.load(Ordering::Acquire)
            && self.measure_attached.load(Ordering::Acquire)
    }

    /// Test-only end-to-end drain introspection. This intentionally takes the control mutex and
    /// must never be called from a shipping Audio callback.
    pub fn drained_for_test(&self) -> bool {
        if !self.producer_installed.load(Ordering::Acquire)
            // SAFETY: this test-only probe is called serially with the sole producer owner.
            || unsafe { self.producer_handoff.active_slots_from_audio() } != self.capacity
        {
            return false;
        }
        let spool_caught_up = match self.control.lock() {
            Ok(control) => control
                .spool
                .as_ref()
                .is_some_and(|spool| spool.caught_up()),
            Err(poisoned) => poisoned
                .into_inner()
                .spool
                .as_ref()
                .is_some_and(|spool| spool.caught_up()),
        };
        spool_caught_up
    }

    /// Reclaim retired producers outside the Audio Thread.
    pub fn reclaim_from_watchdog(&self) {
        self.producer_handoff.reclaim_retired_from_watchdog();
        let mut control = match self.control.lock() {
            Ok(control) => control,
            Err(poisoned) => poisoned.into_inner(),
        };
        control.retired_spools.retain(|spool| !spool.is_closed());
    }

    /// Detach a terminated Measure reader without replacing the live Audio -> spool lane. A new
    /// Measure worker replays the same generation from sample zero, so a worker crash cannot erase
    /// already admitted Record audio.
    ///
    /// `observation` is an exact snapshot of the shared Record state. A generation is retired here
    /// only when it is already known to have entered (`record_generation == armed_generation`) and
    /// is now fully closed (`!recording && !has_record_session`). A not-yet-entered generation has
    /// `record_generation < armed_generation` and must remain eligible to enter after restart.
    pub(crate) fn replace_after_measure_restart(
        &self,
        observation: RecordIngressGenerationObservation,
    ) {
        let armed_generation = {
            let control = match self.control.lock() {
                Ok(control) => control,
                Err(poisoned) => poisoned.into_inner(),
            };
            if !control.allocated {
                return;
            }
            let armed_generation = self.armed_generation.load(Ordering::Acquire);
            self.measure_attached.store(false, Ordering::Release);
            self.restart_pending_generation
                .store(armed_generation, Ordering::Release);
            armed_generation
        };
        if observation.proves_closed(armed_generation) {
            let _ = self.retire_restart_generation(armed_generation);
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
            return if self.retire_restart_generation(pending) {
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

    fn retire_restart_generation(&self, generation: u64) -> bool {
        if generation == 0 || self.restart_pending_generation.load(Ordering::Acquire) != generation
        {
            return false;
        }
        if !self.finish_capture_from_measure(generation, Duration::from_secs(1)) {
            // A close timeout is itself terminal for this TRACE. Release the failed generation so
            // it cannot retain disk or permanently poison later explicit Keeps.
            return self.mark_failed_from_measure(generation);
        }
        // No replacement Measure worker owns this closed generation, so retirement is a failed
        // TRACE outcome rather than a successful drain/seal. The same terminalization path also
        // removes the control seed immediately.
        self.mark_failed_from_measure(generation)
    }

    fn install_fresh_lane(&self, control: &mut ControlState, generation: u64) -> bool {
        let (producer, consumer) = rtrb::RingBuffer::new(self.capacity);
        let Ok(spool) = crate::record_spool::RecordSpool::start(consumer, generation) else {
            return false;
        };
        if let Some(previous) = control.spool.replace(spool) {
            previous.seal();
            control.retired_spools.push(previous);
        }
        control.allocated = true;
        self.accepting_audio.store(false, Ordering::Release);
        self.measure_attached.store(false, Ordering::Release);
        self.producer_installed.store(false, Ordering::Release);
        self.capture_origin_generation.store(0, Ordering::Release);
        self.capture_origin_frame.store(0, Ordering::Release);
        self.producer_handoff.publish_from_watchdog(producer);
        true
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
