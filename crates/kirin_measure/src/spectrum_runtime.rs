//! Optional realtime handoff and isolated Spectrum worker.
//!
//! The audio side performs one atomic enabled check, whole-block capacity checks, and bounded
//! SPSC pushes. FFT planning, window assembly, analysis, mutexes, sleeping, and destruction stay
//! on the worker/control side.

use std::cell::UnsafeCell;
use std::collections::VecDeque;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use rtrb::{Consumer, Producer, RingBuffer};

use crate::spectrum::{
    SpectrumAnalyzer, SpectrumFrame, SPECTRUM_FFT_SIZE, SPECTRUM_PRESENTATION_HZ,
};

pub const SPECTRUM_HISTORY_CAPACITY: usize = 8;
// Two complete FFT windows (128 KiB for stereo f32) cover worker scheduling jitter without
// charging every hidden plug-in instance for a larger resident ring.
const SPECTRUM_RING_FRAMES: usize = SPECTRUM_FFT_SIZE * 2;
const SPECTRUM_BLOCK_RING_CAPACITY: usize = 64;
// The producer ring absorbs normal realtime callbacks. A 10 ms idle poll keeps the optional pair
// responsive at its 100 ms presentation cadence without creating a high-frequency wake loop.
const SPECTRUM_WORKER_IDLE: Duration = Duration::from_millis(10);

#[derive(Clone, Copy, Debug)]
struct SpectrumIngressBlock {
    frames: u32,
    channels: u8,
    presentation_start_samples: i64,
    generation: u64,
}

struct SpectrumConsumers {
    samples: Consumer<f32>,
    blocks: Consumer<SpectrumIngressBlock>,
}

#[derive(Clone, Debug, Default)]
pub struct SpectrumHistory {
    frames: VecDeque<SpectrumFrame>,
}

impl SpectrumHistory {
    pub(crate) fn with_capacity() -> Self {
        Self {
            frames: VecDeque::with_capacity(SPECTRUM_HISTORY_CAPACITY),
        }
    }

    pub(crate) fn push(&mut self, frame: SpectrumFrame) {
        if self.frames.len() == SPECTRUM_HISTORY_CAPACITY {
            self.frames.pop_front();
        }
        self.frames.push_back(frame);
    }

    pub fn newest(&self) -> Option<&SpectrumFrame> {
        self.frames.back()
    }

    pub fn matching_presentation_end(
        &self,
        presentation_end_samples: i64,
    ) -> Option<&SpectrumFrame> {
        self.frames
            .iter()
            .rev()
            .find(|frame| frame.presentation_end_samples == presentation_end_samples)
    }

    pub fn frames(&self) -> impl DoubleEndedIterator<Item = &SpectrumFrame> + ExactSizeIterator {
        self.frames.iter()
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct SpectrumRuntimeStats {
    pub enabled: bool,
    pub worker_running: bool,
    pub pushed_blocks: u64,
    pub dropped_blocks: u64,
    pub analyzed_frames: u64,
}

pub struct SpectrumRuntime {
    sample_rate: u32,
    num_channels: usize,
    enabled: AtomicBool,
    shutdown: AtomicBool,
    generation: AtomicU64,
    sample_producer: UnsafeCell<Producer<f32>>,
    block_producer: UnsafeCell<Producer<SpectrumIngressBlock>>,
    consumers: Mutex<Option<SpectrumConsumers>>,
    worker: Mutex<Option<JoinHandle<SpectrumConsumers>>>,
    wake: (Mutex<()>, Condvar),
    history: Mutex<SpectrumHistory>,
    worker_running: AtomicBool,
    pushed_blocks: AtomicU64,
    dropped_blocks: AtomicU64,
    analyzed_frames: AtomicU64,
}

// SAFETY: only the one Audio Thread calls `push_block_from_audio`, which is the sole mutable
// accessor for both producers. Consumers are moved to one worker. Every other shared field is
// atomic or mutex-protected, and producer destruction occurs only after Audio Thread shutdown.
unsafe impl Sync for SpectrumRuntime {}

impl SpectrumRuntime {
    pub fn new(sample_rate: u32, num_channels: usize) -> Arc<Self> {
        let num_channels = num_channels.clamp(1, 2);
        let (sample_producer, sample_consumer) =
            RingBuffer::new(SPECTRUM_RING_FRAMES * num_channels);
        let (block_producer, block_consumer) = RingBuffer::new(SPECTRUM_BLOCK_RING_CAPACITY);
        Arc::new(Self {
            sample_rate,
            num_channels,
            enabled: AtomicBool::new(false),
            shutdown: AtomicBool::new(false),
            generation: AtomicU64::new(1),
            sample_producer: UnsafeCell::new(sample_producer),
            block_producer: UnsafeCell::new(block_producer),
            consumers: Mutex::new(Some(SpectrumConsumers {
                samples: sample_consumer,
                blocks: block_consumer,
            })),
            worker: Mutex::new(None),
            wake: (Mutex::new(()), Condvar::new()),
            history: Mutex::new(SpectrumHistory::with_capacity()),
            worker_running: AtomicBool::new(false),
            pushed_blocks: AtomicU64::new(0),
            dropped_blocks: AtomicU64::new(0),
            analyzed_frames: AtomicU64::new(0),
        })
    }

    pub fn set_enabled(self: &Arc<Self>, enabled: bool) -> bool {
        if self.shutdown.load(Ordering::Acquire) {
            return false;
        }
        let currently_enabled = self.enabled.load(Ordering::Acquire);
        if enabled == currently_enabled && (!enabled || self.worker_running.load(Ordering::Acquire))
        {
            return true;
        }
        if enabled && !self.ensure_worker() {
            self.enabled.store(false, Ordering::Release);
            return false;
        }
        let previous = self.enabled.swap(enabled, Ordering::AcqRel);
        if previous != enabled {
            self.generation.fetch_add(1, Ordering::AcqRel);
            if let Ok(mut history) = self.history.lock() {
                *history = SpectrumHistory::with_capacity();
            }
        }
        self.wake.1.notify_all();
        true
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Acquire)
    }

    /// Audio Thread only. The method never allocates, locks, sleeps, performs I/O, or runs FFT.
    pub fn push_block_from_audio(
        &self,
        interleaved: &[f32],
        num_channels: usize,
        presentation_start_samples: Option<i64>,
    ) -> bool {
        if !self.enabled.load(Ordering::Relaxed) {
            return false;
        }
        let Some(presentation_start_samples) = presentation_start_samples else {
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
        // SAFETY: see the Sync contract above. Both producers belong to the one Audio Thread.
        let sample_producer = unsafe { &mut *self.sample_producer.get() };
        // SAFETY: same sole-producer contract.
        let block_producer = unsafe { &mut *self.block_producer.get() };
        if sample_producer.slots() < interleaved.len() || block_producer.slots() == 0 {
            self.note_drop();
            return false;
        }
        // Samples become visible before their descriptor. Once the worker sees a block, every
        // sample in that block has already been committed to the companion ring.
        for sample in interleaved {
            let _ = sample_producer.push(*sample);
        }
        let block = SpectrumIngressBlock {
            frames: frames_u32,
            channels: num_channels as u8,
            presentation_start_samples,
            generation: self.generation.load(Ordering::Acquire),
        };
        let _ = block_producer.push(block);
        self.pushed_blocks.fetch_add(1, Ordering::Relaxed);
        true
    }

    pub fn try_history(&self) -> Option<SpectrumHistory> {
        self.history.try_lock().ok().map(|history| history.clone())
    }

    pub fn stats(&self) -> SpectrumRuntimeStats {
        SpectrumRuntimeStats {
            enabled: self.enabled.load(Ordering::Acquire),
            worker_running: self.worker_running.load(Ordering::Acquire),
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
        let consumers = match self.consumers.lock() {
            Ok(mut consumers) => consumers.take(),
            Err(_) => None,
        };
        let Some(mut consumers) = consumers else {
            return false;
        };
        let runtime = Arc::clone(self);
        let spawned = thread::Builder::new()
            .name("kirin-hypha-spectrum".to_string())
            .spawn(move || {
                runtime.worker_running.store(true, Ordering::Release);
                let _ = catch_unwind(AssertUnwindSafe(|| runtime.run_worker(&mut consumers)));
                runtime.worker_running.store(false, Ordering::Release);
                consumers
            });
        match spawned {
            Ok(worker) => {
                *worker_slot = Some(worker);
                true
            }
            Err(_) => false,
        }
    }

    fn run_worker(&self, consumers: &mut SpectrumConsumers) {
        let Ok(analyzer) = SpectrumAnalyzer::new(self.sample_rate) else {
            return;
        };
        let mut assembler = SpectrumAssembler::new(analyzer, self.num_channels);
        while !self.shutdown.load(Ordering::Acquire) {
            if !self.enabled.load(Ordering::Acquire) {
                drain_consumers(consumers);
                assembler.reset();
                let guard = match self.wake.0.lock() {
                    Ok(guard) => guard,
                    Err(_) => return,
                };
                let _ = self.wake.1.wait_timeout(guard, Duration::from_millis(250));
                continue;
            }
            let Ok(block) = consumers.blocks.pop() else {
                thread::sleep(SPECTRUM_WORKER_IDLE);
                continue;
            };
            if block.channels as usize != self.num_channels
                || !assembler.begin_block(block.presentation_start_samples, block.generation)
            {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                continue;
            }
            let mut complete = true;
            for _ in 0..block.frames {
                let Ok(left) = consumers.samples.pop() else {
                    complete = false;
                    break;
                };
                let right = if self.num_channels == 2 {
                    match consumers.samples.pop() {
                        Ok(right) => Some(right),
                        Err(_) => {
                            complete = false;
                            break;
                        }
                    }
                } else {
                    None
                };
                if let Some(frame) = assembler.push_frame(left, right) {
                    self.analyzed_frames.fetch_add(1, Ordering::Relaxed);
                    if let Ok(mut history) = self.history.lock() {
                        history.push(frame);
                    }
                }
            }
            if !complete {
                assembler.reset();
            }
        }
    }
}

impl Drop for SpectrumRuntime {
    fn drop(&mut self) {
        self.enabled.store(false, Ordering::Release);
        self.shutdown.store(true, Ordering::Release);
        self.wake.1.notify_all();
    }
}

fn drain_consumers(consumers: &mut SpectrumConsumers) {
    while consumers.blocks.pop().is_ok() {}
    while consumers.samples.pop().is_ok() {}
}

fn discard_samples(consumer: &mut Consumer<f32>, count: usize) {
    for _ in 0..count {
        if consumer.pop().is_err() {
            break;
        }
    }
}

struct SpectrumAssembler {
    analyzer: SpectrumAnalyzer,
    channels: usize,
    cadence_samples: i64,
    left: Vec<f32>,
    right: Vec<f32>,
    ordered_left: Vec<f32>,
    ordered_right: Vec<f32>,
    write_index: usize,
    filled: usize,
    next_position: Option<i64>,
    generation: u64,
}

impl SpectrumAssembler {
    fn new(analyzer: SpectrumAnalyzer, channels: usize) -> Self {
        let cadence_samples = i64::from(analyzer.sample_rate() / SPECTRUM_PRESENTATION_HZ);
        Self {
            analyzer,
            channels,
            cadence_samples,
            left: vec![0.0; SPECTRUM_FFT_SIZE],
            right: vec![0.0; SPECTRUM_FFT_SIZE],
            ordered_left: vec![0.0; SPECTRUM_FFT_SIZE],
            ordered_right: vec![0.0; SPECTRUM_FFT_SIZE],
            write_index: 0,
            filled: 0,
            next_position: None,
            generation: 0,
        }
    }

    fn begin_block(&mut self, start: i64, generation: u64) -> bool {
        if generation == 0 {
            self.reset();
            return false;
        }
        if self.generation != generation || self.next_position.is_some_and(|next| next != start) {
            self.reset();
            self.generation = generation;
        }
        self.next_position = Some(start);
        true
    }

    fn push_frame(&mut self, left: f32, right: Option<f32>) -> Option<SpectrumFrame> {
        if !left.is_finite() || right.is_some_and(|value| !value.is_finite()) {
            self.reset();
            return None;
        }
        self.left[self.write_index] = left;
        self.right[self.write_index] = right.unwrap_or(0.0);
        self.write_index = (self.write_index + 1) % SPECTRUM_FFT_SIZE;
        self.filled = self.filled.saturating_add(1).min(SPECTRUM_FFT_SIZE);
        let end = self.next_position?.checked_add(1)?;
        self.next_position = Some(end);
        if self.filled != SPECTRUM_FFT_SIZE || end.rem_euclid(self.cadence_samples) != 0 {
            return None;
        }
        copy_ordered(&self.left, self.write_index, &mut self.ordered_left);
        let right = if self.channels == 2 {
            copy_ordered(&self.right, self.write_index, &mut self.ordered_right);
            Some(self.ordered_right.as_slice())
        } else {
            None
        };
        self.analyzer
            .analyze(&self.ordered_left, right, end, self.generation)
            .ok()
    }

    fn reset(&mut self) {
        self.write_index = 0;
        self.filled = 0;
        self.next_position = None;
        self.generation = 0;
    }
}

fn copy_ordered(source: &[f32], start: usize, destination: &mut [f32]) {
    let tail = source.len() - start;
    destination[..tail].copy_from_slice(&source[start..]);
    destination[tail..].copy_from_slice(&source[..start]);
}

#[cfg(test)]
#[path = "spectrum_runtime_tests.rs"]
mod tests;
