use std::sync::atomic::Ordering;
use std::thread;
use std::time::Duration;

use rtrb::Consumer;

use super::assemblers::{AbsoluteAssembler, PerceptualAssembler, SpectrumAssembler};
use super::PerceptualHistory;
use super::{AnalysisSelection, SpectrumConsumers, SpectrumRuntime};
use crate::absolute_timeline::AbsoluteFrame;
use crate::channel_layout::{SpectrumView, MAX_ABI_CHANNELS};
use crate::perceptual::PerceptualFrame;
use crate::spectrum::{AnalysisViewMode, SpectrumAnalyzer, SpectrumFrame};
use crate::MidSideSpectrumFrame;

const WORKER_IDLE: Duration = Duration::from_millis(10);

/// 1 フレームのうち、いまの view が使う入力チャンネルの位置。
///
/// **`input` は ring から 1 フレームあたり pop するサンプル数、`left` / `right` はそのフレームの
/// 中の位置である。** B-963 までこの 2 つが同じ `num_channels` で呼ばれていたため、6ch の
/// interleave が 1 本の流れとして読まれた。別の量には別の名前を付ける。
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct FrameSelection {
    input: usize,
    left: usize,
    right: Option<usize>,
    /// この選択を決めた view の ABI コード。**同じ 1 回の読み出しから来る。**
    /// 位置と名札を別々に読むと、その間に view が変わったとき
    /// 「C と名乗るが L/R から作った frame」ができる（B-971 で塞いだはずの G）。
    view_code: u8,
}

/// worker が持つ 3 つの組立器。**解析チャンネル数ごとに作り直す。**
struct Assemblers {
    spectrum: SpectrumAssembler,
    perceptual: Option<PerceptualAssembler>,
    absolute: Option<AbsoluteAssembler>,
}

impl SpectrumRuntime {
    /// 解析チャンネル数ぶんの組立器を作る。`SpectrumAnalyzer` が作れなければ worker は止まる。
    fn build_assemblers(&self, analysis_channels: usize) -> Option<Assemblers> {
        let analyzer = SpectrumAnalyzer::new(self.sample_rate).ok()?;
        Some(Assemblers {
            spectrum: SpectrumAssembler::new(analyzer, analysis_channels),
            perceptual: PerceptualAssembler::new(self.sample_rate, analysis_channels).ok(),
            absolute: AbsoluteAssembler::new(self.sample_rate, analysis_channels).ok(),
        })
    }

    /// いまの view が読む位置と、その view の名札。view が読めないときは `None`。
    ///
    /// `selection` is the one word copied onto the ingress block. Positions, width, and label
    /// must all be derived from it rather than re-reading current control state.
    fn frame_selection(&self, selection: AnalysisSelection) -> Option<FrameSelection> {
        let view = selection.view;
        let analysis = view.analysis_channels(self.layout);
        let (left, right) = match view {
            // 単一チャンネル view。役割で決まった位置を 1 本だけ解析へ渡す。
            SpectrumView::Channel(role) => (self.layout().index_of(role)?, None),
            // 導出 view（LR / MID / SIDE）。stereo family 限定なので先頭 2 本が L と R である。
            _ => (0, (analysis == 2).then_some(1)),
        };
        Some(FrameSelection {
            input: self.num_channels,
            left,
            right,
            view_code: view.to_abi(),
        })
    }

    pub(super) fn run_worker(&self, consumers: &mut SpectrumConsumers) {
        let mut built_for = self.selection().analysis_channels(self.layout);
        let Some(mut assemblers) = self.build_assemblers(built_for) else {
            return;
        };
        let mut applied_stream_generation = 0;
        while !self.shutdown.load(Ordering::Acquire) {
            if !self.enabled.load(Ordering::Acquire) {
                drain_consumers(consumers);
                reset_assemblers(
                    &mut assemblers.spectrum,
                    assemblers.perceptual.as_mut(),
                    assemblers.absolute.as_mut(),
                );
                let guard = match self.wake.0.lock() {
                    Ok(guard) => guard,
                    Err(_) => return,
                };
                let _ = self.wake.1.wait_timeout(guard, Duration::from_millis(250));
                continue;
            }
            let Ok(block) = consumers.blocks.pop() else {
                thread::sleep(WORKER_IDLE);
                continue;
            };
            let Some(selection) = AnalysisSelection::decode(block.selection, self.layout) else {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                continue;
            };
            if block.selection != self.selection.load(Ordering::Acquire)
                || block.stream_generation == 0
                || block.stream_generation != self.stream_generation.load(Ordering::Acquire)
                || block.channels as usize != self.num_channels
            {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                continue;
            }
            let Some(command) = self.analysis_commands.for_generation(selection.generation) else {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                continue;
            };
            if applied_stream_generation != block.stream_generation {
                reset_assemblers(
                    &mut assemblers.spectrum,
                    assemblers.perceptual.as_mut(),
                    assemblers.absolute.as_mut(),
                );
                applied_stream_generation = block.stream_generation;
            }
            // The assembler width, mode, input positions, and frame label all come from the same
            // selection word captured by the audio thread.
            let analysis_channels = selection.analysis_channels(self.layout);
            if analysis_channels != built_for {
                let Some(rebuilt) = self.build_assemblers(analysis_channels) else {
                    return;
                };
                assemblers = rebuilt;
                built_for = analysis_channels;
            }
            let (spectrum, perceptual, absolute) = (
                &mut assemblers.spectrum,
                &mut assemblers.perceptual,
                &mut assemblers.absolute,
            );
            let generation = selection.generation;
            let mode = selection.mode;
            self.applied_selection
                .store(block.selection, Ordering::Release);
            let began = match mode {
                AnalysisViewMode::Spectrum => {
                    spectrum.begin_block(block.presentation_start_samples, generation)
                }
                AnalysisViewMode::Perceptual => perceptual.as_mut().is_some_and(|analyzer| {
                    analyzer.begin_block(
                        block.presentation_start_samples,
                        generation,
                        command.perceptual_state_epoch,
                    )
                }),
                AnalysisViewMode::Absolute => absolute.as_mut().is_some_and(|analyzer| {
                    analyzer.begin_block(block.presentation_start_samples, generation)
                }),
                AnalysisViewMode::Attack => false,
            };
            if mode == AnalysisViewMode::Absolute
                && absolute
                    .as_mut()
                    .is_some_and(|analyzer| analyzer.take_history_reset_required())
            {
                if let Ok(mut history) = self.absolute_history.lock() {
                    history.clear_to(crate::AbsoluteTimeline::default());
                }
            }
            if !began {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                self.observe_rearm(mode, perceptual.as_mut());
                continue;
            }
            let complete = self.consume_block(
                consumers,
                &block,
                selection,
                spectrum,
                perceptual.as_mut(),
                absolute.as_mut(),
            );
            if !complete {
                reset_assemblers(spectrum, perceptual.as_mut(), absolute.as_mut());
                if mode == AnalysisViewMode::Perceptual {
                    self.require_perceptual_rearm();
                }
            }
        }
    }

    fn consume_block(
        &self,
        consumers: &mut SpectrumConsumers,
        block: &super::SpectrumIngressBlock,
        selection: AnalysisSelection,
        spectrum: &mut SpectrumAssembler,
        mut perceptual: Option<&mut PerceptualAssembler>,
        mut absolute: Option<&mut AbsoluteAssembler>,
    ) -> bool {
        let channel_mode = selection.channel_mode();
        let Some(frame_selection) = self.frame_selection(selection) else {
            return false;
        };
        // 1 フレーム分をまとめて読む。**入力チャンネル数ぶん必ず読む**ので、選んだ役割が
        // 先頭でなくても残りが ring に居残らない。B-963 はここで 1 本しか読まず詰まらせた。
        let mut frame = [0.0f32; MAX_ABI_CHANNELS];
        for _ in 0..block.frames {
            for slot in frame.iter_mut().take(frame_selection.input) {
                let Ok(sample) = consumers.samples.pop() else {
                    return false;
                };
                *slot = sample;
            }
            let left = frame[frame_selection.left];
            let right = frame_selection.right.map(|index| frame[index]);
            match selection.mode {
                AnalysisViewMode::Spectrum => {
                    if selection.mid_side {
                        if let Some(frame) = spectrum.push_mid_side_frame(left, right) {
                            self.publish_mid_side(frame, block.stream_generation);
                        }
                    } else if let Some(mut frame) = spectrum.push_frame(left, right, channel_mode) {
                        // どの観測対象で作ったかをフレーム自身に持たせる。view を選んでいるのは
                        // ここだけで、view が変わると組立器は generation でリセットされるので、
                        // この窓はまるごとこの view のものである。
                        frame.view = frame_selection.view_code;
                        self.publish_spectrum(frame, block.stream_generation);
                    }
                }
                AnalysisViewMode::Perceptual => {
                    let Some(analyzer) = perceptual.as_deref_mut() else {
                        continue;
                    };
                    if let Some(frames) = analyzer.push_frame(left, right, channel_mode) {
                        for frame in frames {
                            self.publish_perceptual(frame, block.stream_generation);
                        }
                    }
                }
                AnalysisViewMode::Absolute => {
                    let Some(analyzer) = absolute.as_deref_mut() else {
                        continue;
                    };
                    if let Some(frames) = analyzer.push_frame(left, right) {
                        for frame in frames {
                            self.publish_absolute(*frame, block.stream_generation);
                        }
                    }
                }
                AnalysisViewMode::Attack => return false,
            }
        }
        true
    }

    fn publish_spectrum(&self, frame: SpectrumFrame, stream_generation: u64) {
        if !self.frame_is_current(&frame, stream_generation) {
            return;
        }
        self.analyzed_frames.fetch_add(1, Ordering::Relaxed);
        if let Ok(mut history) = self.history.lock() {
            if self.frame_is_current(&frame, stream_generation) {
                if history.stream_generation != stream_generation {
                    history.value = super::SpectrumHistory::with_capacity();
                }
                history.value.push(frame);
                history.stream_generation = stream_generation;
            }
        }
    }

    fn publish_mid_side(&self, frame: MidSideSpectrumFrame, stream_generation: u64) {
        if !self.mid_side_frame_is_current(&frame, stream_generation) {
            return;
        }
        self.analyzed_mid_side_frames
            .fetch_add(1, Ordering::Relaxed);
        if let Ok(mut latest) = self.latest_mid_side.lock() {
            if self.mid_side_frame_is_current(&frame, stream_generation) {
                latest.value = Some(frame);
                latest.stream_generation = stream_generation;
            }
        }
    }

    fn publish_perceptual(&self, frame: &PerceptualFrame, stream_generation: u64) {
        if !self.perceptual_frame_is_current(frame, stream_generation) {
            return;
        }
        self.analyzed_perceptual_frames
            .fetch_add(1, Ordering::Relaxed);
        if let Ok(mut history) = self.perceptual_history.lock() {
            if self.perceptual_frame_is_current(frame, stream_generation) {
                if history.stream_generation != stream_generation {
                    history.value = PerceptualHistory::with_capacity();
                }
                history.value.push(frame.clone());
                history.stream_generation = stream_generation;
            }
        }
    }

    fn publish_absolute(&self, frame: AbsoluteFrame, stream_generation: u64) {
        if !self.absolute_frame_is_current(&frame, stream_generation) {
            return;
        }
        self.analyzed_absolute_frames
            .fetch_add(1, Ordering::Relaxed);
        if let Ok(mut history) = self.absolute_history.lock() {
            if self.absolute_frame_is_current(&frame, stream_generation) {
                if history.stream_generation != stream_generation {
                    history.value.clear();
                }
                history.value.push(frame);
                history.stream_generation = stream_generation;
            }
        }
    }

    pub(super) fn frame_is_current(&self, frame: &SpectrumFrame, stream_generation: u64) -> bool {
        let selection = self.selection();
        let layout_matches =
            crate::spectrum::SpectrumLayout::new(self.sample_rate).is_ok_and(|layout| {
                frame.sample_rate == layout.sample_rate
                    && frame.aperture_samples as usize == layout.aperture_samples
                    && frame.fft_size as usize == layout.fft_size
                    && frame.min_hz.to_bits() == layout.min_hz.to_bits()
                    && frame.max_hz.to_bits() == layout.max_hz.to_bits()
            });
        self.enabled.load(Ordering::Acquire)
            && stream_generation != 0
            && stream_generation == self.stream_generation.load(Ordering::Acquire)
            && selection.mode == AnalysisViewMode::Spectrum
            && !selection.mid_side
            && layout_matches
            && frame.generation == selection.generation
            && frame.channel_mode == selection.channel_mode()
            && frame.view == selection.view.to_abi()
            && frame.channels as usize == selection.analysis_channels(self.layout)
    }

    fn mid_side_frame_is_current(
        &self,
        frame: &MidSideSpectrumFrame,
        stream_generation: u64,
    ) -> bool {
        let selection = self.selection();
        self.enabled.load(Ordering::Acquire)
            && stream_generation != 0
            && stream_generation == self.stream_generation.load(Ordering::Acquire)
            && selection.mode == AnalysisViewMode::Spectrum
            && selection.mid_side
            && frame.generation() == selection.generation
            && frame.has_valid_layout()
            && frame.mid.sample_rate == self.sample_rate
            && frame.mid.channels as usize == selection.analysis_channels(self.layout)
    }

    fn perceptual_frame_is_current(&self, frame: &PerceptualFrame, stream_generation: u64) -> bool {
        let selection = self.selection();
        let command = self.analysis_commands.for_generation(selection.generation);
        self.enabled.load(Ordering::Acquire)
            && stream_generation != 0
            && stream_generation == self.stream_generation.load(Ordering::Acquire)
            && selection.mode == AnalysisViewMode::Perceptual
            && frame.generation == selection.generation
            && command.is_some_and(|command| {
                Some(frame.state_epoch_samples) == command.perceptual_state_epoch
            })
            && frame.channel_mode == selection.channel_mode()
            && frame.channels as usize == selection.analysis_channels(self.layout)
    }

    fn absolute_frame_is_current(&self, frame: &AbsoluteFrame, stream_generation: u64) -> bool {
        let selection = self.selection();
        self.enabled.load(Ordering::Acquire)
            && stream_generation != 0
            && stream_generation == self.stream_generation.load(Ordering::Acquire)
            && selection.mode == AnalysisViewMode::Absolute
            && frame.generation == selection.generation
            && frame.channels as usize == selection.analysis_channels(self.layout)
            && frame.is_valid()
    }

    fn observe_rearm(&self, mode: AnalysisViewMode, perceptual: Option<&mut PerceptualAssembler>) {
        if mode == AnalysisViewMode::Perceptual
            && perceptual.is_some_and(PerceptualAssembler::take_rearm_required)
        {
            self.require_perceptual_rearm();
        }
    }

    fn require_perceptual_rearm(&self) {
        self.perceptual_rearm_required
            .store(true, Ordering::Release);
        if let Ok(mut history) = self.perceptual_history.lock() {
            history.clear_to(PerceptualHistory::with_capacity());
        }
    }
}

fn reset_assemblers(
    spectrum: &mut SpectrumAssembler,
    perceptual: Option<&mut PerceptualAssembler>,
    absolute: Option<&mut AbsoluteAssembler>,
) {
    spectrum.reset();
    if let Some(perceptual) = perceptual {
        perceptual.reset();
    }
    if let Some(absolute) = absolute {
        absolute.reset();
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

#[cfg(test)]
#[path = "spectrum_runtime_selection_tests.rs"]
mod selection_tests;
