use std::sync::atomic::Ordering;
use std::thread;
use std::time::Duration;

use rtrb::Consumer;

use super::assembler::AttackAssembler;
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
        while !self.shutdown.load(Ordering::Acquire) {
            if !self.enabled.load(Ordering::Acquire) {
                drain(consumers);
                assembler.reset();
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
                continue;
            }
            if !assembler.begin_block(block.presentation_start_samples, block.generation) {
                discard_samples(
                    &mut consumers.samples,
                    block.frames as usize * block.channels as usize,
                );
                continue;
            }
            if !self.consume_block(consumers, block.frames, &mut assembler) {
                assembler.reset();
            }
        }
    }

    fn consume_block(
        &self,
        consumers: &mut AttackConsumers,
        frames: u32,
        assembler: &mut AttackAssembler,
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
            if let Some(frame) = assembler.push_frame(left, right) {
                self.publish(frame);
            }
        }
        true
    }

    fn publish(&self, frame: super::AttackOdfFrame) {
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
