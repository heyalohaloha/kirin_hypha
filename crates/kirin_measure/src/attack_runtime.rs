//! Default-OFF ATTACK DRUM ingress and isolated SuperFlux worker.
//!
//! The Audio Thread only copies samples and one descriptor into bounded SPSC rings. FFT planning,
//! window assembly, SuperFlux analysis, and history publication stay on the worker.

use std::cell::UnsafeCell;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::{self, JoinHandle};

use rtrb::{Consumer, Producer, RingBuffer};

use crate::{SuperFluxChannelMode, SuperFluxConfig, SuperFluxLayout};

#[path = "attack_runtime_assembler.rs"]
mod assembler;
#[path = "attack_peak.rs"]
mod peak;
#[path = "attack_runtime_state.rs"]
mod state;
#[path = "attack_runtime_worker.rs"]
mod worker;

pub use state::{
    AttackEvent, AttackHistory, AttackOdfFrame, AttackRuntimeStats, ATTACK_EVENT_HISTORY_CAPACITY,
    ATTACK_ODF_HISTORY_CAPACITY,
};

const ATTACK_BLOCK_RING_CAPACITY: usize = 128;
const ATTACK_INGRESS_SECONDS: usize = 2;
const NO_PRESENTATION_POSITION: i64 = i64::MIN;

#[derive(Clone, Copy, Debug)]
struct AttackIngressBlock {
    frames: u32,
    channels: u8,
    presentation_start_samples: i64,
    generation: u64,
}

struct AttackConsumers {
    samples: Consumer<f32>,
    blocks: Consumer<AttackIngressBlock>,
}

pub struct AttackRuntime {
    sample_rate: u32,
    num_channels: usize,
    enabled: AtomicBool,
    shutdown: AtomicBool,
    generation: AtomicU64,
    latest_presentation_end: AtomicI64,
    sample_producer: UnsafeCell<Producer<f32>>,
    block_producer: UnsafeCell<Producer<AttackIngressBlock>>,
    consumers: Mutex<Option<AttackConsumers>>,
    worker: Mutex<Option<JoinHandle<AttackConsumers>>>,
    wake: (Mutex<()>, Condvar),
    history: Mutex<AttackHistory>,
    worker_running: AtomicBool,
    pushed_blocks: AtomicU64,
    dropped_blocks: AtomicU64,
    analyzed_frames: AtomicU64,
}

// SAFETY: one Audio Thread is the sole caller of `push_block_from_audio` and therefore the sole
// mutable accessor to both producers. The consumers belong to one worker. Other state is atomic or
// mutex-protected, and shutdown precedes destruction of the producers.
unsafe impl Sync for AttackRuntime {}

impl AttackRuntime {
    pub fn new(sample_rate: u32, num_channels: usize) -> Result<Arc<Self>, &'static str> {
        if !matches!(num_channels, 1 | 2) {
            return Err("ATTACK requires mono or stereo input");
        }
        let config = drum_config(num_channels);
        let _ = SuperFluxLayout::for_rate(sample_rate, config)?;
        let sample_capacity = (sample_rate as usize)
            .checked_mul(ATTACK_INGRESS_SECONDS)
            .and_then(|value| value.checked_mul(num_channels))
            .ok_or("ATTACK ingress capacity overflow")?;
        let (sample_producer, sample_consumer) = RingBuffer::new(sample_capacity);
        let (block_producer, block_consumer) = RingBuffer::new(ATTACK_BLOCK_RING_CAPACITY);
        Ok(Arc::new(Self {
            sample_rate,
            num_channels,
            enabled: AtomicBool::new(false),
            shutdown: AtomicBool::new(false),
            generation: AtomicU64::new(1),
            latest_presentation_end: AtomicI64::new(NO_PRESENTATION_POSITION),
            sample_producer: UnsafeCell::new(sample_producer),
            block_producer: UnsafeCell::new(block_producer),
            consumers: Mutex::new(Some(AttackConsumers {
                samples: sample_consumer,
                blocks: block_consumer,
            })),
            worker: Mutex::new(None),
            wake: (Mutex::new(()), Condvar::new()),
            history: Mutex::new(AttackHistory::with_capacity()),
            worker_running: AtomicBool::new(false),
            pushed_blocks: AtomicU64::new(0),
            dropped_blocks: AtomicU64::new(0),
            analyzed_frames: AtomicU64::new(0),
        }))
    }

    pub fn set_enabled(self: &Arc<Self>, enabled: bool) -> bool {
        if self.shutdown.load(Ordering::Acquire) {
            return false;
        }
        let current = self.enabled.load(Ordering::Acquire);
        if current == enabled && (!enabled || self.worker_running.load(Ordering::Acquire)) {
            return true;
        }
        if enabled && !self.ensure_worker() {
            self.enabled.store(false, Ordering::Release);
            return false;
        }
        if self.enabled.swap(enabled, Ordering::AcqRel) != enabled {
            self.generation.fetch_add(1, Ordering::AcqRel);
            self.latest_presentation_end
                .store(NO_PRESENTATION_POSITION, Ordering::Release);
            if let Ok(mut history) = self.history.lock() {
                *history = AttackHistory::with_capacity();
            }
        }
        self.wake.1.notify_all();
        true
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Acquire)
    }

    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    pub fn num_channels(&self) -> usize {
        self.num_channels
    }

    /// Audio Thread only. No allocation, lock, sleep, I/O, FFT, or signal modification.
    pub fn push_block_from_audio(
        &self,
        interleaved: &[f32],
        num_channels: usize,
        presentation_start_samples: Option<i64>,
    ) -> bool {
        if !self.enabled.load(Ordering::Relaxed) {
            return false;
        }
        let Some(start) = presentation_start_samples else {
            self.note_drop();
            return false;
        };
        if num_channels != self.num_channels
            || interleaved.is_empty()
            || !interleaved.len().is_multiple_of(num_channels)
        {
            self.note_drop();
            return false;
        }
        let frames = interleaved.len() / num_channels;
        let Ok(frames_u32) = u32::try_from(frames) else {
            self.note_drop();
            return false;
        };
        let Some(end) = start.checked_add(frames as i64) else {
            self.note_drop();
            return false;
        };
        // SAFETY: see the sole-producer Sync contract above.
        let samples = unsafe { &mut *self.sample_producer.get() };
        // SAFETY: same sole-producer contract.
        let blocks = unsafe { &mut *self.block_producer.get() };
        if samples.slots() < interleaved.len() || blocks.slots() == 0 {
            self.note_drop();
            return false;
        }
        let previous_end = self.latest_presentation_end.load(Ordering::Acquire);
        let generation = if previous_end != NO_PRESENTATION_POSITION && previous_end != start {
            self.generation.fetch_add(1, Ordering::AcqRel) + 1
        } else {
            self.generation.load(Ordering::Acquire)
        };
        for sample in interleaved {
            let _ = samples.push(*sample);
        }
        let _ = blocks.push(AttackIngressBlock {
            frames: frames_u32,
            channels: num_channels as u8,
            presentation_start_samples: start,
            generation,
        });
        self.latest_presentation_end.store(end, Ordering::Release);
        self.pushed_blocks.fetch_add(1, Ordering::Relaxed);
        true
    }

    pub fn try_history(&self) -> Option<AttackHistory> {
        self.history.try_lock().ok().map(|history| history.clone())
    }

    pub fn latest_presentation_end(&self) -> Option<i64> {
        let value = self.latest_presentation_end.load(Ordering::Acquire);
        (value != NO_PRESENTATION_POSITION).then_some(value)
    }

    pub fn stats(&self) -> AttackRuntimeStats {
        AttackRuntimeStats {
            enabled: self.enabled.load(Ordering::Acquire),
            worker_running: self.worker_running.load(Ordering::Acquire),
            channels: self.num_channels as u8,
            pushed_blocks: self.pushed_blocks.load(Ordering::Relaxed),
            dropped_blocks: self.dropped_blocks.load(Ordering::Relaxed),
            analyzed_frames: self.analyzed_frames.load(Ordering::Relaxed),
        }
    }

    pub fn shutdown_and_join(&self) {
        self.enabled.store(false, Ordering::Release);
        self.shutdown.store(true, Ordering::Release);
        self.wake.1.notify_all();
        if let Ok(mut worker) = self.worker.lock() {
            if let Some(handle) = worker.take() {
                let _ = handle.join();
            }
        }
    }

    fn note_drop(&self) {
        self.dropped_blocks.fetch_add(1, Ordering::Relaxed);
        self.generation.fetch_add(1, Ordering::AcqRel);
    }

    fn ensure_worker(self: &Arc<Self>) -> bool {
        let mut worker_slot = match self.worker.lock() {
            Ok(worker) => worker,
            Err(_) => return false,
        };
        if worker_slot
            .as_ref()
            .is_some_and(|worker| !worker.is_finished())
        {
            return true;
        }
        if let Some(finished) = worker_slot.take() {
            if let Ok(consumers) = finished.join() {
                if let Ok(mut slot) = self.consumers.lock() {
                    *slot = Some(consumers);
                }
            }
        }
        let Some(mut consumers) = self
            .consumers
            .lock()
            .ok()
            .and_then(|mut consumers| consumers.take())
        else {
            return false;
        };
        let runtime = Arc::clone(self);
        match thread::Builder::new()
            .name("kirin-hypha-attack".to_string())
            .spawn(move || {
                runtime.worker_running.store(true, Ordering::Release);
                let _ = catch_unwind(AssertUnwindSafe(|| runtime.run_worker(&mut consumers)));
                runtime.worker_running.store(false, Ordering::Release);
                consumers
            }) {
            Ok(worker) => {
                *worker_slot = Some(worker);
                true
            }
            Err(_) => false,
        }
    }
}

pub(super) const fn drum_config(channels: usize) -> SuperFluxConfig {
    SuperFluxConfig::new(2_048, 12, 0, -50, SuperFluxChannelMode::Lr, channels)
}

/// Runs the exact ATTACK DRUM front end and causal peak decision on a mono buffer.
///
/// This allocates and is intended only for offline regression diagnostics. The VST Audio Thread
/// continues to use [`AttackRuntime::push_block_from_audio`].
#[doc(hidden)]
pub fn analyze_drum_attacks_mono_offline(
    samples: &[f32],
    sample_rate: u32,
) -> Result<Vec<AttackEvent>, &'static str> {
    analyze_drum_attacks_interleaved_offline(samples, sample_rate, 1)
}

/// Offline ATTACK DRUM analysis for interleaved mono or stereo source audio.
#[doc(hidden)]
pub fn analyze_drum_attacks_interleaved_offline(
    samples: &[f32],
    sample_rate: u32,
    channels: usize,
) -> Result<Vec<AttackEvent>, &'static str> {
    if samples.is_empty() {
        return Err("offline ATTACK input is empty");
    }
    if !matches!(channels, 1 | 2) || !samples.len().is_multiple_of(channels) {
        return Err("offline ATTACK input requires interleaved mono or stereo frames");
    }
    if samples.iter().any(|sample| !sample.is_finite()) {
        return Err("offline ATTACK input contains a non-finite sample");
    }
    let analyzer = crate::SuperFluxAnalyzer::new(sample_rate, drum_config(channels))?;
    let layout = analyzer.layout();
    let tail_samples = layout
        .window_samples
        .checked_add((sample_rate as usize * 30_000).div_ceil(1_000_000))
        .and_then(|value| value.checked_add(layout.hop_samples * 2))
        .ok_or("offline ATTACK tail length overflow")?;
    let source_frames = samples.len() / channels;
    let source_end =
        i64::try_from(source_frames).map_err(|_| "offline ATTACK input is too long")?;
    let mut assembler = assembler::AttackAssembler::new(analyzer, channels);
    let mut peak_picker = peak::AttackPeakPicker::new();
    let mut events = Vec::new();
    if !assembler.begin_block(0, 1) {
        return Err("offline ATTACK could not start at source origin");
    }
    for input in samples
        .chunks_exact(channels)
        .map(|frame| (frame[0], frame.get(1).copied()))
        .chain(std::iter::repeat_n(
            (0.0, (channels == 2).then_some(0.0)),
            tail_samples,
        ))
    {
        if let Some(frame) = assembler.push_frame(input.0, input.1) {
            if let Some(event) = peak_picker.push(frame) {
                if (0..source_end).contains(&event.event_sample) {
                    events.push(event);
                }
            }
        }
    }
    Ok(events)
}

impl Drop for AttackRuntime {
    fn drop(&mut self) {
        self.enabled.store(false, Ordering::Release);
        self.shutdown.store(true, Ordering::Release);
        self.wake.1.notify_all();
    }
}

#[cfg(test)]
#[path = "attack_runtime_tests.rs"]
mod tests;
