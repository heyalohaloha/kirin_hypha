//! The band side of the ATTACK worker: keeps the ring while a band is chosen and measures each
//! confirmed hit once its tail is final.
//!
//! A hit's tail runs to 300 ms after its onset, or to the next onset. It is measured once every
//! onset before that limit has been decided and the ring holds the audio to the span end plus
//! the smoothing window. Nothing here allocates on the Audio Thread: the ring is filled from the
//! worker's own block copy, and the work buffers are reused.

use std::collections::VecDeque;
use std::sync::atomic::Ordering;

use super::detail::AttackDetailTracker;
use super::state::{AttackBandDetail, AttackDetailedEvent, AttackEvent};
use super::AttackRuntime;
use crate::attack_perception::band::{
    self, analysis_range, frames_for_micros, span_end_for, AttackBand, AttackBandRing, BandScratch,
    ATTACK_BAND_TAIL_MICROS,
};

const QUEUE_CAPACITY: usize = 64;

pub(super) struct BandWorker {
    band: Option<AttackBand>,
    queue: VecDeque<AttackEvent>,
    scratch: BandScratch,
}

impl BandWorker {
    pub(super) fn new() -> Self {
        Self {
            band: None,
            queue: VecDeque::with_capacity(QUEUE_CAPACITY),
            scratch: BandScratch::default(),
        }
    }

    pub(super) fn reset(&mut self) {
        self.queue.clear();
    }
}

impl AttackRuntime {
    /// Before the tracker flushes its block: the block's audio goes to the ring while a band is
    /// chosen. A band change starts the queue and the ring over.
    pub(super) fn service_band_audio(
        &self,
        worker: &mut BandWorker,
        tracker: &AttackDetailTracker,
    ) {
        let band = self.band();
        if worker.band != band {
            worker.band = band;
            worker.queue.clear();
        }
        let Some(_) = band else {
            return;
        };
        let (start, audio) = tracker.block_audio();
        if audio.is_empty() {
            return;
        }
        let mut ring = match self.band_ring.lock() {
            Ok(ring) => ring,
            Err(poisoned) => poisoned.into_inner(),
        };
        ring.get_or_insert_with(|| AttackBandRing::new(self.sample_rate, self.num_channels))
            .push_block(start, audio);
    }

    /// After the flush: every hit completed in it joins the queue, and every queued hit whose
    /// tail is final is measured and published.
    pub(super) fn service_band_hits(
        &self,
        worker: &mut BandWorker,
        tracker: &AttackDetailTracker,
        completed: &[AttackDetailedEvent],
    ) {
        let Some(band) = worker.band else {
            return;
        };
        for detail in completed.iter().filter(|detail| detail.features.complete) {
            if worker.queue.len() == QUEUE_CAPACITY {
                worker.queue.pop_front();
            }
            if worker
                .queue
                .iter()
                .all(|queued| queued.event_sample != detail.event.event_sample)
            {
                worker.queue.push_back(detail.event);
            }
        }
        let generation = self.generation.load(Ordering::Acquire);
        while let Some(&event) = worker.queue.front() {
            if event.generation != generation {
                worker.queue.pop_front();
                continue;
            }
            let onset = event.event_sample;
            let limit = onset + frames_for_micros(self.sample_rate, ATTACK_BAND_TAIL_MICROS);
            let next = worker
                .queue
                .get(1)
                .map(|next| next.event_sample)
                .or_else(|| tracker.next_pending_onset())
                .filter(|next| *next > onset);
            let tail_known = next.is_some_and(|next| next < limit)
                || tracker
                    .decided_before()
                    .is_some_and(|decided| decided >= limit);
            if !tail_known {
                break;
            }
            let span_end = span_end_for(self.sample_rate, onset, next);
            let (from, to) = analysis_range(band, self.sample_rate, onset, span_end);
            let measured = {
                let ring = match self.band_ring.lock() {
                    Ok(ring) => ring,
                    Err(poisoned) => poisoned.into_inner(),
                };
                let Some(ring) = ring.as_ref() else {
                    worker.queue.pop_front();
                    continue;
                };
                if ring.first() > from {
                    // The audio before this hit is gone: nothing to measure, ever.
                    worker.queue.pop_front();
                    continue;
                }
                if ring.end() < to {
                    break;
                }
                band::measure_from_ring(
                    ring,
                    band,
                    self.sample_rate,
                    self.num_channels,
                    onset,
                    span_end,
                    &mut worker.scratch,
                )
            };
            worker.queue.pop_front();
            let Some(measure) = measured else {
                continue;
            };
            if let Ok(mut history) = self.history.lock() {
                if self.enabled.load(Ordering::Acquire)
                    && event.generation == self.generation.load(Ordering::Acquire)
                    && self.band() == Some(band)
                {
                    history.push_band_detail(AttackBandDetail { event, measure });
                }
            }
        }
    }
}

#[cfg(test)]
#[path = "attack_band_worker_tests.rs"]
mod tests;
