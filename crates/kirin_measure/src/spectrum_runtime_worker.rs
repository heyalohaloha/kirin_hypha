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

const WORKER_IDLE: Duration = Duration::from_millis(10);

impl SpectrumRuntime {
    pub(super) fn run_worker(&self, consumers: &mut SpectrumConsumers) {
        let Ok(analyzer) = SpectrumAnalyzer::new(self.sample_rate) else {
            return;
        };
        let mut spectrum = SpectrumAssembler::new(analyzer, self.num_channels);
        let mut perceptual = PerceptualAssembler::new(self.sample_rate, self.num_channels).ok();
        let mut absolute = AbsoluteAssembler::new(self.sample_rate, self.num_channels).ok();
        while !self.shutdown.load(Ordering::Acquire) {
            if !self.enabled.load(Ordering::Acquire) {
                drain_consumers(consumers);
                reset_assemblers(&mut spectrum, perceptual.as_mut(), absolute.as_mut());
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
                &mut spectrum,
                perceptual.as_mut(),
                absolute.as_mut(),
            );
            if !complete {
                reset_assemblers(&mut spectrum, perceptual.as_mut(), absolute.as_mut());
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
        for _ in 0..frames {
            let Ok(left) = consumers.samples.pop() else {
                return false;
            };
            let right = if self.num_channels == 2 {
                match consumers.samples.pop() {
                    Ok(right) => Some(right),
                    Err(_) => return false,
                }
            } else {
                None
            };
            match mode {
                AnalysisViewMode::Spectrum => {
                    if let Some(frame) = spectrum.push_frame(left, right, channel_mode) {
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
            && layout_matches
            && frame.generation == self.generation.load(Ordering::Acquire)
            && frame.channel_mode == self.channel_mode()
            && frame.channels as usize == self.num_channels
    }

    fn perceptual_frame_is_current(&self, frame: &PerceptualFrame) -> bool {
        self.enabled.load(Ordering::Acquire)
            && self.analysis_mode() == AnalysisViewMode::Perceptual
            && frame.generation == self.generation.load(Ordering::Acquire)
            && Some(frame.state_epoch_samples) == self.perceptual_state_epoch()
            && frame.channel_mode == self.channel_mode()
            && frame.channels as usize == self.num_channels
    }

    fn absolute_frame_is_current(&self, frame: &AbsoluteFrame) -> bool {
        self.enabled.load(Ordering::Acquire)
            && self.analysis_mode() == AnalysisViewMode::Absolute
            && frame.generation == self.generation.load(Ordering::Acquire)
            && frame.channels as usize == self.num_channels
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
