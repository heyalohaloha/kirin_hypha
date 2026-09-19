use std::sync::atomic::Ordering;
use std::thread;
use std::time::Duration;

use rtrb::Consumer;

use super::assemblers::{AbsoluteAssembler, PerceptualAssembler, SpectrumAssembler};
use super::PerceptualHistory;
use super::{SpectrumConsumers, SpectrumRuntime};
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
    /// **view の読み出しはこの 1 回だけ**にする。位置・本数・名札を別々に読むと、
    /// その間に control thread が `set_view` を走らせたとき 3 つが食い違う。
    /// `set_view` は view を swap してから generation を上げるので、その隙間で
    /// 組み上がった frame は旧 generation のまま鮮度判定を通り得る（B-974）。
    fn frame_selection(&self) -> Option<FrameSelection> {
        let view = self.view()?;
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
                    } else if let Some(mut frame) = spectrum.push_frame(left, right, channel_mode) {
                        // どの観測対象で作ったかをフレーム自身に持たせる。view を選んでいるのは
                        // ここだけで、view が変わると組立器は generation でリセットされるので、
                        // この窓はまるごとこの view のものである。
                        frame.view = selection.view_code;
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
            // 名札そのものを照合する。`set_view` は view を swap してから generation を
            // 上げるので、その隙間で組み上がった frame は generation だけでは落ちない。
            // **「C と名乗るが L/R から作った frame」を公開しない**（B-974）。
            && Some(frame.view) == self.view().map(SpectrumView::to_abi)
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

#[cfg(test)]
mod selection_tests {
    use super::*;
    use crate::channel_layout::{ChannelLayout, LayoutId};

    /// 位置・本数・名札が**同じ 1 回の view 読み出しから**来ていること。
    ///
    /// B-974 以前はこの 3 つを別々の atomic 読み出しから作っていた。その間に
    /// `set_view` が走ると、たとえば「`C` と名乗るが `left = 0`（L）から作った」選択ができる。
    /// ここでは layout が提供する全 view について、名札と位置が対応することを固定する。
    #[test]
    fn a_selection_names_the_view_it_actually_reads() {
        for layout in [
            ChannelLayout::mono(),
            ChannelLayout::stereo(),
            ChannelLayout::by_id(LayoutId::Surround5_1),
            ChannelLayout::by_id(LayoutId::Surround7_1_4),
        ] {
            let runtime = SpectrumRuntime::new(48_000, layout);
            for view in SpectrumView::available_in(layout) {
                assert!(runtime.set_view(view), "{view:?} in {:?}", layout.id());
                let selection = runtime.frame_selection().expect("a selection");

                assert_eq!(
                    selection.input,
                    layout.channel_count(),
                    "読む本数は入力のまま"
                );
                assert_eq!(
                    SpectrumView::from_abi(selection.view_code),
                    Some(view),
                    "名札は選んだ view そのもの"
                );
                match view {
                    SpectrumView::Channel(role) => {
                        assert_eq!(
                            selection.left,
                            layout.index_of(role).expect("役割は layout にある"),
                            "{} は自分の位置を読む",
                            role.as_str()
                        );
                        assert_eq!(selection.right, None, "単一チャンネル view は 1 本");
                    }
                    _ => {
                        assert_eq!(selection.left, 0);
                        assert_eq!(
                            selection.right,
                            (layout.channel_count() >= 2).then_some(1),
                            "導出 view は先頭 2 本（mono では 1 本）"
                        );
                    }
                }
            }
        }
    }
}
