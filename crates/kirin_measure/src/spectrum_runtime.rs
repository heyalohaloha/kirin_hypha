//! Optional RT handoff; FFT planning, assembly, and analysis stay on the isolated worker.

use std::cell::UnsafeCell;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::{self, JoinHandle};

use rtrb::{Consumer, Producer, RingBuffer};

use crate::absolute_timeline::AbsoluteTimeline;
use crate::channel_layout::{ChannelLayout, SpectrumView};
use crate::spectrum::{
    AnalysisViewMode, SpectrumChannelMode, SpectrumLayout, SPECTRUM_WINDOW_SIZE,
};
use crate::MidSideSpectrumFrame;

#[path = "analysis_commands.rs"]
mod analysis_commands;
use analysis_commands::AnalysisCommands;

#[path = "analysis_selection.rs"]
mod analysis_selection;
use analysis_selection::AnalysisSelection;

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
use state::StampedSnapshot;
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
    selection: u64,
    stream_generation: u64,
}

struct SpectrumConsumers {
    samples: Consumer<f32>,
    blocks: Consumer<SpectrumIngressBlock>,
}

pub struct SpectrumRuntime {
    sample_rate: u32,
    num_channels: usize,
    layout: ChannelLayout,
    /// Mode, view, Mid/Side, and monotonic generation published as one identity.
    selection: AtomicU64,
    selection_update: Mutex<()>,
    analysis_commands: AnalysisCommands,
    requested_perceptual_state_epoch: AtomicI64,
    applied_selection: AtomicU64,
    stream_generation: AtomicU64,
    enabled: AtomicBool,
    shutdown: AtomicBool,
    latest_presentation_end: AtomicI64,
    perceptual_rearm_required: AtomicBool,
    sample_producer: UnsafeCell<Producer<f32>>,
    block_producer: UnsafeCell<Producer<SpectrumIngressBlock>>,
    consumers: Mutex<Option<SpectrumConsumers>>,
    worker: Mutex<Option<JoinHandle<SpectrumConsumers>>>,
    wake: (Mutex<()>, Condvar),
    history: Mutex<StampedSnapshot<SpectrumHistory>>,
    perceptual_history: Mutex<StampedSnapshot<PerceptualHistory>>,
    absolute_history: Mutex<StampedSnapshot<AbsoluteTimeline>>,
    latest_mid_side: Mutex<StampedSnapshot<Option<MidSideSpectrumFrame>>>,
    worker_running: AtomicBool,
    pushed_blocks: AtomicU64,
    dropped_blocks: AtomicU64,
    analyzed_frames: AtomicU64,
    analyzed_perceptual_frames: AtomicU64,
    analyzed_absolute_frames: AtomicU64,
    analyzed_mid_side_frames: AtomicU64,
}

// SAFETY: only the one Audio Thread calls `push_block_from_audio`, which is the sole mutable
// accessor for both producers. Consumers are moved to one worker. Every other shared field is
// atomic or mutex-protected, and producer destruction occurs only after Audio Thread shutdown.
unsafe impl Sync for SpectrumRuntime {}

impl SpectrumRuntime {
    /// `layout` が実チャンネル数と選べる view の両方を決める。
    ///
    /// B-963 以前はここで `num_channels.clamp(1, 2)` していた。広い host buffer は 2ch として
    /// 記録され、`push_block_from_audio` の `num_channels != self.num_channels` が以後すべての
    /// block を拒否して **Spectrum が無言で何も出さない状態**になる（D-13 の C 類型）。
    /// clamp を外したので、測れない layout は「測れない」として現れる。
    pub fn new(sample_rate: u32, layout: ChannelLayout) -> Arc<Self> {
        let num_channels = layout.channel_count();
        let aperture_samples = SpectrumLayout::new(sample_rate)
            .map(|layout| layout.aperture_samples)
            .unwrap_or(SPECTRUM_WINDOW_SIZE);
        let (sample_producer, sample_consumer) =
            RingBuffer::new(aperture_samples * 2 * num_channels);
        let (block_producer, block_consumer) = RingBuffer::new(SPECTRUM_BLOCK_RING_CAPACITY);
        let initial_selection = AnalysisSelection::initial(layout);
        Arc::new(Self {
            sample_rate,
            num_channels,
            layout,
            selection: AtomicU64::new(initial_selection.encode()),
            selection_update: Mutex::new(()),
            analysis_commands: AnalysisCommands::new(initial_selection.generation),
            requested_perceptual_state_epoch: AtomicI64::new(NO_PRESENTATION_POSITION),
            applied_selection: AtomicU64::new(0),
            stream_generation: AtomicU64::new(1),
            enabled: AtomicBool::new(false),
            shutdown: AtomicBool::new(false),
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
            history: Mutex::new(StampedSnapshot::new(SpectrumHistory::with_capacity())),
            perceptual_history: Mutex::new(
                StampedSnapshot::new(PerceptualHistory::with_capacity()),
            ),
            absolute_history: Mutex::new(StampedSnapshot::new(AbsoluteTimeline::default())),
            latest_mid_side: Mutex::new(StampedSnapshot::new(None)),
            worker_running: AtomicBool::new(false),
            pushed_blocks: AtomicU64::new(0),
            dropped_blocks: AtomicU64::new(0),
            analyzed_frames: AtomicU64::new(0),
            analyzed_perceptual_frames: AtomicU64::new(0),
            analyzed_absolute_frames: AtomicU64::new(0),
            analyzed_mid_side_frames: AtomicU64::new(0),
        })
    }

    pub fn set_enabled(self: &Arc<Self>, enabled: bool) -> bool {
        if self.shutdown.load(Ordering::Acquire) {
            return false;
        }
        // B-965 はここで「解析できる view が無い layout」を閉じていた。B-970 で worker が
        // 入力チャンネル数ぶん pop して役割で選ぶようになったので、認識済み layout には必ず
        // 測れる view がある。門は `set_view` 側（layout が提供しない view の拒否）に残る。
        let currently_enabled = self.enabled.load(Ordering::Acquire);
        if enabled == currently_enabled && (!enabled || self.worker_running.load(Ordering::Acquire))
        {
            return true;
        }
        if enabled && !self.ensure_worker() {
            self.enabled.store(false, Ordering::Release);
            return false;
        }
        if enabled != currently_enabled && !self.advance_selection_generation() {
            return false;
        }
        let previous = self.enabled.swap(enabled, Ordering::AcqRel);
        if previous != enabled {
            self.latest_presentation_end
                .store(NO_PRESENTATION_POSITION, Ordering::Release);
            self.perceptual_rearm_required
                .store(false, Ordering::Release);
            if let Ok(mut history) = self.history.lock() {
                history.clear_to(SpectrumHistory::with_capacity());
            }
            if let Ok(mut history) = self.perceptual_history.lock() {
                history.clear_to(PerceptualHistory::with_capacity());
            }
            if let Ok(mut history) = self.absolute_history.lock() {
                history.clear_to(AbsoluteTimeline::default());
            }
            self.clear_mid_side_frame();
        }
        self.wake.1.notify_all();
        true
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Acquire)
    }

    pub fn channel_mode(&self) -> SpectrumChannelMode {
        self.selection().channel_mode()
    }

    pub fn analysis_mode(&self) -> AnalysisViewMode {
        self.selection().mode
    }

    /// 現在の観測対象。`None` は ABI 値が既知の view を指していない状態であり、既定値ではない。
    pub fn view(&self) -> Option<SpectrumView> {
        Some(self.selection().view)
    }

    /// 現在の view で解析器が見る信号の本数。**入力チャンネル数（`num_channels`）とは別である。**
    /// view が読めないときは 0 を返し、worker は何も組み立てない。
    pub fn analysis_channels(&self) -> usize {
        self.selection().analysis_channels(self.layout)
    }

    /// この runtime が作られた layout。
    pub fn layout(&self) -> ChannelLayout {
        self.layout
    }

    pub fn num_channels(&self) -> usize {
        self.num_channels
    }

    pub fn mid_side_enabled(&self) -> bool {
        self.selection().mid_side
    }

    /// Control thread only. Mid/Side is one stereo-only Spectrum processing selection.
    ///
    /// **いまの view が 2 本を解析していないと成立しない。** 1 本の view のまま受理すると、
    /// 組立器が `channels != 2` で `None` を返し続け、**何も出ないまま有効に見える**（D-13 の C）。
    pub fn set_mid_side_enabled(&self, enabled: bool) -> bool {
        let Some(changed) = self.update_selection(|current| {
            if enabled
                && (current.analysis_channels(self.layout) != 2
                    || current.mode != AnalysisViewMode::Spectrum)
            {
                return None;
            }
            Some(AnalysisSelection {
                mid_side: enabled,
                ..current
            })
        }) else {
            return false;
        };
        if changed {
            if let Ok(mut history) = self.history.lock() {
                history.clear_to(SpectrumHistory::with_capacity());
            }
            self.clear_mid_side_frame();
            self.wake.1.notify_all();
        }
        true
    }

    /// Control/worker thread only. A mode edge invalidates every queued presentation frame so
    /// PRE and POST must warm up again on one exact channel definition.
    pub fn set_channel_mode(&self, mode: SpectrumChannelMode) -> bool {
        let view = match mode {
            SpectrumChannelMode::Lr => SpectrumView::Lr,
            SpectrumChannelMode::Mid => SpectrumView::Mid,
            SpectrumChannelMode::Side => SpectrumView::Side,
        };
        self.set_view(view)
    }

    /// 観測対象を選ぶ。この layout が提供しない view は拒否する。
    ///
    /// **役割で比べるので、layout が変われば「役割が残る」か「消える」かのどちらかになり、
    /// どちらも検出できる。** index だと別チャンネルの履歴が無言で連結する（契約表 §11.3.1）。
    pub fn set_view(&self, view: SpectrumView) -> bool {
        if !view.is_available_in(self.layout) {
            return false;
        }
        let Some(changed) = self.update_selection(|current| {
            Some(AnalysisSelection {
                view,
                mid_side: current.mid_side && view.analysis_channels(self.layout) == 2,
                ..current
            })
        }) else {
            return false;
        };
        if changed {
            if let Ok(mut history) = self.history.lock() {
                history.clear_to(SpectrumHistory::with_capacity());
            }
            if let Ok(mut history) = self.perceptual_history.lock() {
                history.clear_to(PerceptualHistory::with_capacity());
            }
            if let Ok(mut history) = self.absolute_history.lock() {
                history.clear_to(AbsoluteTimeline::default());
            }
            self.clear_mid_side_frame();
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
        let stream_generation = self.stream_generation.load(Ordering::Acquire);
        if stream_generation == 0 {
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
            selection: self.selection.load(Ordering::Acquire),
            stream_generation,
        };
        let _ = block_producer.push(block);
        self.pushed_blocks.fetch_add(1, Ordering::Relaxed);
        true
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
        let current = self.stream_generation.load(Ordering::Relaxed);
        let next = current.checked_add(1).unwrap_or(0);
        let _ = self.stream_generation.compare_exchange(
            current,
            next,
            Ordering::AcqRel,
            Ordering::Relaxed,
        );
        if self.analysis_mode() == AnalysisViewMode::Perceptual {
            self.perceptual_rearm_required
                .store(true, Ordering::Release);
        }
    }

    fn clear_mid_side_frame(&self) {
        if let Ok(mut frame) = self.latest_mid_side.lock() {
            frame.clear_to(None);
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

#[cfg(test)]
#[path = "spectrum_runtime_view_tests.rs"]
mod view_tests;

#[cfg(test)]
#[path = "spectrum_runtime_snapshot_tests.rs"]
mod snapshot_tests;
