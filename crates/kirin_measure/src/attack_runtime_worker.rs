use std::sync::atomic::Ordering;
use std::thread;
use std::time::Duration;

use rtrb::Consumer;

use super::assembler::AttackAssembler;
use super::detail::AttackDetailTracker;
use super::peak::AttackPeakPicker;
use super::{drum_config, AttackConsumers, AttackRuntime};
use crate::SuperFluxAnalyzer;

const WORKER_IDLE: Duration = Duration::from_millis(5);

impl AttackRuntime {
    pub(super) fn run_worker(&self, consumers: &mut AttackConsumers) {
        let Ok(analyzer) = SuperFluxAnalyzer::new(self.sample_rate, drum_config(self.num_channels))
        else {
            return;
        };
        let mut assembler = AttackAssembler::new(analyzer, self.num_channels);
        let mut detail_tracker = AttackDetailTracker::new(self.sample_rate, self.num_channels);
        let mut peak_picker = AttackPeakPicker::new();
        while !self.shutdown.load(Ordering::Acquire) {
            if !self.enabled.load(Ordering::Acquire) {
                drain(consumers);
                assembler.reset();
                detail_tracker.reset();
                peak_picker.reset();
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
            if block.generation != generation || block.channels as usize != self.num_channels {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                peak_picker.reset();
                detail_tracker.reset();
                continue;
            }
            if !assembler.begin_block(block.presentation_start_samples, block.generation)
                || !detail_tracker.begin_block(block.presentation_start_samples, block.generation)
            {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                peak_picker.reset();
                detail_tracker.reset();
                continue;
            }
            if !self.consume_block(
                consumers,
                block.frames,
                &mut assembler,
                &mut peak_picker,
                &mut detail_tracker,
            ) {
                assembler.reset();
                peak_picker.reset();
                detail_tracker.reset();
            }
        }
    }

    fn consume_block(
        &self,
        consumers: &mut AttackConsumers,
        frames: u32,
        assembler: &mut AttackAssembler,
        peak_picker: &mut AttackPeakPicker,
        detail_tracker: &mut AttackDetailTracker,
    ) -> bool {
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
            match detail_tracker.push_frame(left, right) {
                Ok(Some(waveform)) => self.publish_waveform(waveform),
                Ok(None) => {}
                Err(()) => return false,
            }
            if let Some(frame) = assembler.push_frame(left, right) {
                self.publish(frame, peak_picker, detail_tracker);
            }
        }
        true
    }

    fn publish_waveform(&self, point: super::AttackWaveformPoint) {
        if self.generation.load(Ordering::Acquire) != point.generation {
            return;
        }
        if let Ok(mut history) = self.history.lock() {
            if self.generation.load(Ordering::Acquire) == point.generation {
                history.push_waveform(point);
            }
        }
    }

    fn publish(
        &self,
        frame: super::AttackOdfFrame,
        peak_picker: &mut AttackPeakPicker,
        detail_tracker: &mut AttackDetailTracker,
    ) {
        if !self.frame_is_current(&frame) {
            return;
        }
        let event = peak_picker.push(frame);
        let detail = event.and_then(|event| detail_tracker.capture(event));
        self.analyzed_frames.fetch_add(1, Ordering::Relaxed);
        if let Ok(mut history) = self.history.lock() {
            if self.frame_is_current(&frame) {
                history.push(frame);
                if let Some(event) = event {
                    history.push_event(event);
                }
                if let Some(detail) = detail {
                    history.push_detail(detail);
                }
            }
        }
    }

    fn frame_is_current(&self, frame: &super::AttackOdfFrame) -> bool {
        self.enabled.load(Ordering::Acquire)
            && frame.generation == self.generation.load(Ordering::Acquire)
            && frame.sample_rate == self.sample_rate
            && frame.channels as usize == self.num_channels
            && frame.has_valid_layout()
    }
}

fn drain(consumers: &mut AttackConsumers) {
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
