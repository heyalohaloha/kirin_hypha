use std::sync::atomic::Ordering;
use std::thread;
use std::time::Duration;

use super::assembler::AttackAssembler;
use super::band_worker::BandWorker;
use super::detail::AttackDetailTracker;
use super::peak::AttackPeakPicker;
use super::{drum_config, AttackConsumers, AttackRuntime};
use crate::SuperFluxAnalyzer;

const WORKER_IDLE: Duration = Duration::from_millis(5);

impl AttackRuntime {
    pub(super) fn run_worker(&self, consumers: &mut AttackConsumers) {
        // A restarted worker may inherit a descriptor interrupted by its predecessor. Consume
        // only its known remainder before reading the next descriptor; unpublished PCM belongs
        // to a future transaction and must stay in the SPSC until its header is committed.
        consumers.discard_current_samples();
        let Ok(analyzer) = SuperFluxAnalyzer::new(self.sample_rate, drum_config(self.num_channels))
        else {
            return;
        };
        let mut assembler = AttackAssembler::new(analyzer, self.num_channels);
        let mut detail_tracker = AttackDetailTracker::new(self.sample_rate, self.num_channels);
        let mut peak_picker = AttackPeakPicker::new();
        let mut band_worker = BandWorker::new();
        while !self.shutdown.load(Ordering::Acquire) {
            if !self.enabled.load(Ordering::Acquire) {
                drain(consumers);
                assembler.reset();
                detail_tracker.reset();
                peak_picker.reset();
                self.reset_band(&mut band_worker);
                let guard = match self.wake.0.lock() {
                    Ok(guard) => guard,
                    Err(_) => return,
                };
                let _ = self.wake.1.wait_timeout(guard, Duration::from_millis(250));
                continue;
            }
            let Ok(block) = consumers.blocks.pop() else {
                // No audio: the band side finishes what the stopped run allows.
                self.service_band(&mut band_worker, detail_tracker.decided_before(), false);
                thread::sleep(WORKER_IDLE);
                continue;
            };
            consumers.begin_descriptor(block);
            let generation = self.generation.load(Ordering::Acquire);
            if block.generation != generation || block.channels as usize != self.num_channels {
                consumers.discard_current_samples();
                peak_picker.reset();
                detail_tracker.reset();
                continue;
            }
            if !assembler.begin_block(block.presentation_start_samples, block.generation)
                || !detail_tracker.begin_block(block.presentation_start_samples, block.generation)
            {
                consumers.discard_current_samples();
                peak_picker.reset();
                detail_tracker.reset();
                continue;
            }
            if !self.consume_block(
                consumers,
                block.frames,
                block.generation,
                &mut assembler,
                &mut peak_picker,
                &mut detail_tracker,
                &mut band_worker,
            ) {
                consumers.discard_current_samples();
                self.note_drop();
                self.reset_band(&mut band_worker);
                assembler.reset();
                peak_picker.reset();
                detail_tracker.reset();
            }
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn consume_block(
        &self,
        consumers: &mut AttackConsumers,
        frames: u32,
        generation: u64,
        assembler: &mut AttackAssembler,
        peak_picker: &mut AttackPeakPicker,
        detail_tracker: &mut AttackDetailTracker,
        band_worker: &mut BandWorker,
    ) -> bool {
        self.band_begin_block(band_worker, generation);
        for _ in 0..frames {
            let Some(left) = consumers.pop_sample() else {
                return false;
            };
            let right = if self.num_channels == 2 {
                match consumers.pop_sample() {
                    Some(right) => Some(right),
                    None => return false,
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
                self.publish(frame, peak_picker, detail_tracker, band_worker);
            }
        }
        self.band_block_audio(band_worker, detail_tracker);
        for detail in detail_tracker.flush(&self.bins) {
            if let Ok(mut history) = self.history.lock() {
                if self.enabled.load(Ordering::Acquire)
                    && detail.event.generation == self.generation.load(Ordering::Acquire)
                {
                    history.push_detail(detail);
                }
            }
        }
        self.service_band(band_worker, detail_tracker.decided_before(), true);
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
        band_worker: &mut BandWorker,
    ) {
        if !self.frame_is_current(&frame) {
            return;
        }
        let event = peak_picker.push(frame);
        if let Some(event) = event {
            detail_tracker.queue_event(event);
            band_worker.note_event(event);
        }
        detail_tracker.note_decided_before(super::peak::decided_before(
            frame.event_sample,
            frame.sample_rate,
        ));
        self.analyzed_frames.fetch_add(1, Ordering::Relaxed);
        if let Ok(mut history) = self.history.lock() {
            if self.frame_is_current(&frame) {
                history.push(frame);
                if let Some(event) = event {
                    history.push_event(event);
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
    consumers.discard_current_samples();
    while let Ok(block) = consumers.blocks.pop() {
        consumers.begin_descriptor(block);
        consumers.discard_current_samples();
    }
}

impl AttackConsumers {
    fn begin_descriptor(&mut self, block: super::AttackIngressBlock) {
        self.current_samples_remaining = block.frames as usize * usize::from(block.channels);
    }
    fn pop_sample(&mut self) -> Option<f32> {
        match self.samples.pop() {
            Ok(sample) => {
                self.current_samples_remaining = self.current_samples_remaining.saturating_sub(1);
                Some(sample)
            }
            Err(_) => {
                // Trusted ingress commits a descriptor only after all its PCM. If that contract
                // fails, stop at the observed empty ring; never consume a later uncommitted block
                // trying to satisfy an impossible old count.
                self.current_samples_remaining = 0;
                None
            }
        }
    }
    fn discard_current_samples(&mut self) {
        while self.current_samples_remaining > 0 {
            if self.pop_sample().is_none() {
                break;
            }
        }
    }
}

#[cfg(test)]
#[path = "attack_runtime_worker_ingress_tests.rs"]
mod tests;
