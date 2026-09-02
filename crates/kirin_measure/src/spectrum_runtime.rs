//! Optional RT handoff; FFT planning, assembly, and analysis stay on the isolated worker.

use std::cell::UnsafeCell;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::{self, JoinHandle};

use rtrb::{Consumer, Producer, RingBuffer};

use crate::absolute_timeline::AbsoluteTimeline;
use crate::spectrum::{
    AnalysisViewMode, SpectrumChannelMode, SpectrumLayout, SPECTRUM_WINDOW_SIZE,
};

#[path = "spectrum_runtime_assemblers.rs"]
mod assemblers;
#[cfg(test)]
use crate::perceptual::PerceptualFrame;
#[cfg(test)]
use crate::spectrum::{SpectrumAnalyzer, SpectrumFrame};
#[cfg(test)]
use assemblers::SpectrumAssembler;

#[path = "spectrum_runtime_worker.rs"]
mod worker;

#[path = "spectrum_runtime_state.rs"]
mod state;
pub use state::{
    PerceptualHistory, SpectrumHistory, SpectrumRuntimeStats, PERCEPTUAL_HISTORY_CAPACITY,
    SPECTRUM_HISTORY_CAPACITY,
};

// Two time-normalized apertures cover worker scheduling jitter. The ring exists even while hidden,
// but FFT storage and work are still created only by the on-demand worker.
const SPECTRUM_BLOCK_RING_CAPACITY: usize = 64;
pub(super) const NO_PRESENTATION_POSITION: i64 = i64::MIN;

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

pub struct SpectrumRuntime {
    sample_rate: u32,
    num_channels: usize,
    enabled: AtomicBool,
    shutdown: AtomicBool,
    generation: AtomicU64,
    analysis_mode: AtomicU8,
    channel_mode: AtomicU8,
    perceptual_state_epoch: AtomicI64,
    latest_presentation_end: AtomicI64,
    perceptual_rearm_required: AtomicBool,
    sample_producer: UnsafeCell<Producer<f32>>,
    block_producer: UnsafeCell<Producer<SpectrumIngressBlock>>,
    consumers: Mutex<Option<SpectrumConsumers>>,
    worker: Mutex<Option<JoinHandle<SpectrumConsumers>>>,
    wake: (Mutex<()>, Condvar),
    history: Mutex<SpectrumHistory>,
    perceptual_history: Mutex<PerceptualHistory>,
    absolute_history: Mutex<AbsoluteTimeline>,
    worker_running: AtomicBool,
    pushed_blocks: AtomicU64,
    dropped_blocks: AtomicU64,
    analyzed_frames: AtomicU64,
    analyzed_perceptual_frames: AtomicU64,
    analyzed_absolute_frames: AtomicU64,
}

// SAFETY: only the one Audio Thread calls `push_block_from_audio`, which is the sole mutable
// accessor for both producers. Consumers are moved to one worker. Every other shared field is
// atomic or mutex-protected, and producer destruction occurs only after Audio Thread shutdown.
unsafe impl Sync for SpectrumRuntime {}

impl SpectrumRuntime {
    pub fn new(sample_rate: u32, num_channels: usize) -> Arc<Self> {
        let num_channels = num_channels.clamp(1, 2);
        let aperture_samples = SpectrumLayout::new(sample_rate)
            .map(|layout| layout.aperture_samples)
            .unwrap_or(SPECTRUM_WINDOW_SIZE);
        let (sample_producer, sample_consumer) =
            RingBuffer::new(aperture_samples * 2 * num_channels);
        let (block_producer, block_consumer) = RingBuffer::new(SPECTRUM_BLOCK_RING_CAPACITY);
        Arc::new(Self {
            sample_rate,
            num_channels,
            enabled: AtomicBool::new(false),
            shutdown: AtomicBool::new(false),
            generation: AtomicU64::new(1),
            analysis_mode: AtomicU8::new(AnalysisViewMode::Spectrum as u8),
            channel_mode: AtomicU8::new(SpectrumChannelMode::Lr as u8),
            perceptual_state_epoch: AtomicI64::new(NO_PRESENTATION_POSITION),
            latest_presentation_end: AtomicI64::new(NO_PRESENTATION_POSITION),
            perceptual_rearm_required: AtomicBool::new(false),
            sample_producer: UnsafeCell::new(sample_producer),
            block_producer: UnsafeCell::new(block_producer),
            consumers: Mutex::new(Some(SpectrumConsumers {
                samples: sample_consumer,
                blocks: block_consumer,
            })),
            worker: Mutex::new(None),
            wake: (Mutex::new(()), Condvar::new()),
            history: Mutex::new(SpectrumHistory::with_capacity()),
            perceptual_history: Mutex::new(PerceptualHistory::with_capacity()),
            absolute_history: Mutex::new(AbsoluteTimeline::default()),
            worker_running: AtomicBool::new(false),
            pushed_blocks: AtomicU64::new(0),
            dropped_blocks: AtomicU64::new(0),
            analyzed_frames: AtomicU64::new(0),
            analyzed_perceptual_frames: AtomicU64::new(0),
            analyzed_absolute_frames: AtomicU64::new(0),
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
            self.latest_presentation_end
                .store(NO_PRESENTATION_POSITION, Ordering::Release);
            self.perceptual_rearm_required
                .store(false, Ordering::Release);
            if let Ok(mut history) = self.history.lock() {
                *history = SpectrumHistory::with_capacity();
            }
            if let Ok(mut history) = self.perceptual_history.lock() {
                *history = PerceptualHistory::with_capacity();
            }
            if let Ok(mut history) = self.absolute_history.lock() {
                history.clear();
            }
        }
        self.wake.1.notify_all();
        true
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Acquire)
    }

    pub fn channel_mode(&self) -> SpectrumChannelMode {
        SpectrumChannelMode::try_from(self.channel_mode.load(Ordering::Acquire))
            .unwrap_or(SpectrumChannelMode::Lr)
    }

    pub fn analysis_mode(&self) -> AnalysisViewMode {
        AnalysisViewMode::try_from(self.analysis_mode.load(Ordering::Acquire))
            .unwrap_or(AnalysisViewMode::Spectrum)
    }

    pub fn num_channels(&self) -> usize {
        self.num_channels
    }

    /// Control/worker thread only. A mode edge invalidates every queued presentation frame so
    /// PRE and POST must warm up again on one exact channel definition.
    pub fn set_channel_mode(&self, mode: SpectrumChannelMode) -> bool {
        if mode == SpectrumChannelMode::Side && self.num_channels != 2 {
            return false;
        }
        let previous = self.channel_mode.swap(mode as u8, Ordering::AcqRel);
        if previous != mode as u8 {
            self.generation.fetch_add(1, Ordering::AcqRel);
            if let Ok(mut history) = self.history.lock() {
                *history = SpectrumHistory::with_capacity();
            }
            if let Ok(mut history) = self.perceptual_history.lock() {
                *history = PerceptualHistory::with_capacity();
            }
            if let Ok(mut history) = self.absolute_history.lock() {
                history.clear();
            }
            self.wake.1.notify_all();
        }
        true
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
        let Some(presentation_end_samples) = presentation_start_samples.checked_add(frames as i64)
        else {
            self.note_drop();
            return false;
        };
        self.latest_presentation_end
            .store(presentation_end_samples, Ordering::Release);
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

    pub fn try_perceptual_history(&self) -> Option<PerceptualHistory> {
        self.perceptual_history
            .try_lock()
            .ok()
            .map(|history| history.clone())
    }

    pub fn try_absolute_history(&self) -> Option<AbsoluteTimeline> {
        self.absolute_history
            .try_lock()
            .ok()
            .map(|history| history.clone())
    }

    pub fn stats(&self) -> SpectrumRuntimeStats {
        SpectrumRuntimeStats {
            enabled: self.enabled.load(Ordering::Acquire),
            worker_running: self.worker_running.load(Ordering::Acquire),
            analysis_mode: self.analysis_mode(),
            channel_mode: self.channel_mode(),
            channels: self.num_channels as u8,
            pushed_blocks: self.pushed_blocks.load(Ordering::Relaxed),
            dropped_blocks: self.dropped_blocks.load(Ordering::Relaxed),
            analyzed_frames: self.analyzed_frames.load(Ordering::Relaxed),
            analyzed_perceptual_frames: self.analyzed_perceptual_frames.load(Ordering::Relaxed),
            analyzed_absolute_frames: self.analyzed_absolute_frames.load(Ordering::Relaxed),
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
        if self.analysis_mode() == AnalysisViewMode::Perceptual {
            self.perceptual_rearm_required
                .store(true, Ordering::Release);
        }
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
}

impl Drop for SpectrumRuntime {
    fn drop(&mut self) {
        self.enabled.store(false, Ordering::Release);
        self.shutdown.store(true, Ordering::Release);
        self.wake.1.notify_all();
    }
}

#[cfg(test)]
#[path = "spectrum_runtime_tests.rs"]
mod tests;
