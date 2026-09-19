use std::sync::atomic::Ordering;
use std::thread;
use std::time::Duration;

use rtrb::Consumer;

use super::assemblers::{AbsoluteAssembler, PerceptualAssembler, SpectrumAssembler};
use super::PerceptualHistory;
use super::{SpectrumConsumers, SpectrumRuntime};
use crate::absolute_timeline::AbsoluteFrame;
use crate::perceptual::PerceptualFrame;
use crate::spectrum::{AnalysisViewMode, SpectrumAnalyzer, SpectrumFrame};
use crate::channel_layout::{SpectrumView, MAX_ABI_CHANNELS, SPECTRUM_VIEW_NONE};
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

    /// いまの view が読む位置。view が読めないときは `None`（worker は何も組み立てない）。
    fn frame_selection(&self) -> Option<FrameSelection> {
        let analysis = self.analysis_channels();
        if analysis == 0 {
            return None;
        }
        Some(match self.selected_channel_index() {
            // 単一チャンネル view。役割で決まった位置を 1 本だけ解析へ渡す。
            Some(index) => FrameSelection {
                input: self.num_channels,
                left: index,
                right: None,
            },
            // 導出 view（LR / MID / SIDE）。stereo family 限定なので先頭 2 本が L と R である。
            None => FrameSelection {
                input: self.num_channels,
                left: 0,
                right: (analysis == 2).then_some(1),
            },
        })
    }

    pub(super) fn run_worker(&self, consumers: &mut SpectrumConsumers) {
        let mut built_for = self.analysis_channels();
        let Some(mut assemblers) = self.build_assemblers(built_for) else {
            return;
        };
        while !self.shutdown.load(Ordering::Acquire) {
            // view が変わって解析器が見る本数が変われば組み直す。前の本数のまま使うと、
            // frame の `channels` が現在の view と食い違い、鮮度判定で全部落ちる。
            let analysis_channels = self.analysis_channels();
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
            if !self.enabled.load(Ordering::Acquire) {
                drain_consumers(consumers);
                reset_assemblers(spectrum, perceptual.as_mut(), absolute.as_mut());
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
            let generation = self.generation.load(Ordering::Acquire);
            let mode = self.analysis_mode();
            if block.generation != generation || block.channels as usize != self.num_channels {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                continue;
            }
            let began = match mode {
                AnalysisViewMode::Spectrum => {
                    spectrum.begin_block(block.presentation_start_samples, block.generation)
                }
                AnalysisViewMode::Perceptual => perceptual.as_mut().is_some_and(|analyzer| {
                    analyzer.begin_block(
                        block.presentation_start_samples,
                        block.generation,
                        self.perceptual_state_epoch(),
                    )
                }),
                AnalysisViewMode::Absolute => absolute.as_mut().is_some_and(|analyzer| {
                    analyzer.begin_block(block.presentation_start_samples, block.generation)
                }),
                AnalysisViewMode::Attack => false,
            };
            if mode == AnalysisViewMode::Absolute
                && absolute
                    .as_mut()
                    .is_some_and(|analyzer| analyzer.take_history_reset_required())
            {
                if let Ok(mut history) = self.absolute_history.lock() {
                    history.clear();
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
                block.frames,
                mode,
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
        frames: u32,
        mode: AnalysisViewMode,
        spectrum: &mut SpectrumAssembler,
        mut perceptual: Option<&mut PerceptualAssembler>,
        mut absolute: Option<&mut AbsoluteAssembler>,
    ) -> bool {
        let channel_mode = self.channel_mode();
        let Some(selection) = self.frame_selection() else {
            return false;
        };
        let view_code = self.view().map_or(SPECTRUM_VIEW_NONE, SpectrumView::to_abi);
        // 1 フレーム分をまとめて読む。**入力チャンネル数ぶん必ず読む**ので、選んだ役割が
        // 先頭でなくても残りが ring に居残らない。B-963 はここで 1 本しか読まず詰まらせた。
        let mut frame = [0.0f32; MAX_ABI_CHANNELS];
        for _ in 0..frames {
            for slot in frame.iter_mut().take(selection.input) {
                let Ok(sample) = consumers.samples.pop() else {
                    return false;
                };
                *slot = sample;
            }
            let left = frame[selection.left];
            let right = selection.right.map(|index| frame[index]);
            match mode {
                AnalysisViewMode::Spectrum => {
                    if self.mid_side_enabled() {
                        if let Some(frame) = spectrum.push_mid_side_frame(left, right) {
                            self.publish_mid_side(frame);
                        }
                    } else if let Some(mut frame) = spectrum.push_frame(left, right, channel_mode)
                    {
                        // どの観測対象で作ったかをフレーム自身に持たせる。view を選んでいるのは
                        // ここだけで、view が変わると組立器は generation でリセットされるので、
                        // この窓はまるごとこの view のものである。
                        frame.view = view_code;
                        self.publish_spectrum(frame);
                    }
                }
                AnalysisViewMode::Perceptual => {
                    let Some(analyzer) = perceptual.as_deref_mut() else {
                        continue;
                    };
                    if let Some(frames) = analyzer.push_frame(left, right, channel_mode) {
                        for frame in frames {
                            self.publish_perceptual(frame);
                        }
                    }
                }
                AnalysisViewMode::Absolute => {
                    let Some(analyzer) = absolute.as_deref_mut() else {
                        continue;
                    };
                    if let Some(frames) = analyzer.push_frame(left, right) {
                        for frame in frames {
                            self.publish_absolute(*frame);
                        }
                    }
                }
                AnalysisViewMode::Attack => return false,
            }
        }
        true
    }

    fn publish_spectrum(&self, frame: SpectrumFrame) {
        if !self.frame_is_current(&frame) {
            return;
        }
        self.analyzed_frames.fetch_add(1, Ordering::Relaxed);
        if let Ok(mut history) = self.history.lock() {
            if self.frame_is_current(&frame) {
                history.push(frame);
            }
        }
    }

    fn publish_mid_side(&self, frame: MidSideSpectrumFrame) {
        if !self.mid_side_frame_is_current(&frame) {
            return;
        }
        self.analyzed_mid_side_frames
            .fetch_add(1, Ordering::Relaxed);
        if let Ok(mut latest) = self.latest_mid_side.lock() {
            if self.mid_side_frame_is_current(&frame) {
                *latest = Some(frame);
            }
        }
    }

    fn publish_perceptual(&self, frame: &PerceptualFrame) {
        if !self.perceptual_frame_is_current(frame) {
            return;
        }
        self.analyzed_perceptual_frames
            .fetch_add(1, Ordering::Relaxed);
        if let Ok(mut history) = self.perceptual_history.lock() {
            if self.perceptual_frame_is_current(frame) {
                history.push(frame.clone());
            }
        }
    }

    fn publish_absolute(&self, frame: AbsoluteFrame) {
        if !self.absolute_frame_is_current(&frame) {
            return;
        }
        self.analyzed_absolute_frames
            .fetch_add(1, Ordering::Relaxed);
        if let Ok(mut history) = self.absolute_history.lock() {
            if self.absolute_frame_is_current(&frame) {
                history.push(frame);
            }
        }
    }

    pub(super) fn frame_is_current(&self, frame: &SpectrumFrame) -> bool {
        let layout_matches =
            crate::spectrum::SpectrumLayout::new(self.sample_rate).is_ok_and(|layout| {
                frame.sample_rate == layout.sample_rate
                    && frame.aperture_samples as usize == layout.aperture_samples
                    && frame.fft_size as usize == layout.fft_size
                    && frame.min_hz.to_bits() == layout.min_hz.to_bits()
                    && frame.max_hz.to_bits() == layout.max_hz.to_bits()
            });
        self.enabled.load(Ordering::Acquire)
            && self.analysis_mode() == AnalysisViewMode::Spectrum
            && !self.mid_side_enabled()
            && layout_matches
            && frame.generation == self.generation.load(Ordering::Acquire)
            && frame.channel_mode == self.channel_mode()
            && frame.channels as usize == self.analysis_channels()
    }

    fn mid_side_frame_is_current(&self, frame: &MidSideSpectrumFrame) -> bool {
        self.enabled.load(Ordering::Acquire)
            && self.analysis_mode() == AnalysisViewMode::Spectrum
            && self.mid_side_enabled()
            && frame.generation() == self.generation.load(Ordering::Acquire)
            && frame.has_valid_layout()
            && frame.mid.sample_rate == self.sample_rate
            && frame.mid.channels as usize == self.analysis_channels()
    }

    fn perceptual_frame_is_current(&self, frame: &PerceptualFrame) -> bool {
        self.enabled.load(Ordering::Acquire)
            && self.analysis_mode() == AnalysisViewMode::Perceptual
            && frame.generation == self.generation.load(Ordering::Acquire)
            && Some(frame.state_epoch_samples) == self.perceptual_state_epoch()
            && frame.channel_mode == self.channel_mode()
            && frame.channels as usize == self.analysis_channels()
    }

    fn absolute_frame_is_current(&self, frame: &AbsoluteFrame) -> bool {
        self.enabled.load(Ordering::Acquire)
            && self.analysis_mode() == AnalysisViewMode::Absolute
            && frame.generation == self.generation.load(Ordering::Acquire)
            && frame.channels as usize == self.analysis_channels()
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
            *history = PerceptualHistory::with_capacity();
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
